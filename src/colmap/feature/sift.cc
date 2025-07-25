// Copyright (c), ETH Zurich and UNC Chapel Hill.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of ETH Zurich and UNC Chapel Hill nor the names of
//       its contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#include "colmap/feature/sift.h"

#include "colmap/feature/utils.h"
#include "colmap/math/math.h"
#include "colmap/util/cuda.h"
#include "colmap/util/file.h"
#include "colmap/util/logging.h"
#include "colmap/util/misc.h"
#include "colmap/util/opengl_utils.h"

#if defined(COLMAP_GPU_ENABLED)
#include "thirdparty/SiftGPU/SiftGPU.h"
#if !defined(COLMAP_GUI_ENABLED)
// GLEW symbols are already defined by Qt.
#include <GL/glew.h>
#endif  // COLMAP_GUI_ENABLED
#endif  // COLMAP_GPU_ENABLED
#include "colmap/util/eigen_alignment.h"

#include "thirdparty/VLFeat/covdet.h"
#include "thirdparty/VLFeat/sift.h"

#include <array>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>

#include <Eigen/Geometry>

namespace colmap {

// SIFT descriptors are normalized to length 512 (w/ quantization errors).
constexpr int kSqSiftDescriptorNorm = 512 * 512;

bool SiftExtractionOptions::Check() const {
  CHECK_OPTION_GT(max_image_size, 0);
  CHECK_OPTION_GT(max_num_features, 0);
  CHECK_OPTION_GT(octave_resolution, 0);
  CHECK_OPTION_GT(peak_threshold, 0.0);
  CHECK_OPTION_GT(edge_threshold, 0.0);
  CHECK_OPTION_GT(max_num_orientations, 0);
  if (domain_size_pooling) {
    CHECK_OPTION_GT(dsp_min_scale, 0);
    CHECK_OPTION_GE(dsp_max_scale, dsp_min_scale);
    CHECK_OPTION_GT(dsp_num_scales, 0);
  }
  return true;
}

bool SiftMatchingOptions::Check() const {
  CHECK_OPTION_GT(max_ratio, 0.0);
  CHECK_OPTION_GT(max_distance, 0.0);
  return true;
}

namespace {

void WarnDarknessAdaptivityNotAvailable() {
  LOG(WARNING) << "Darkness adaptivity only available for GLSL SiftGPU.";
}

// VLFeat uses a different convention to store its descriptors. This transforms
// the VLFeat format into the original SIFT format that is also used by SiftGPU.
FeatureDescriptors TransformVLFeatToUBCFeatureDescriptors(
    const FeatureDescriptors& vlfeat_descriptors) {
  FeatureDescriptors ubc_descriptors(vlfeat_descriptors.rows(),
                                     vlfeat_descriptors.cols());
  const std::array<int, 8> q{{0, 7, 6, 5, 4, 3, 2, 1}};
  for (FeatureDescriptors::Index n = 0; n < vlfeat_descriptors.rows(); ++n) {
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        for (int k = 0; k < 8; ++k) {
          ubc_descriptors(n, 8 * (j + 4 * i) + q[k]) =
              vlfeat_descriptors(n, 8 * (j + 4 * i) + k);
        }
      }
    }
  }
  return ubc_descriptors;
}

class SiftCPUFeatureExtractor : public FeatureExtractor {
 public:
  using VlSiftType = std::unique_ptr<VlSiftFilt, void (*)(VlSiftFilt*)>;

  explicit SiftCPUFeatureExtractor(const FeatureExtractionOptions& options)
      : options_(options), sift_(nullptr, &vl_sift_delete) {
    THROW_CHECK(options_.Check());
    THROW_CHECK(!options_.sift->estimate_affine_shape);
    THROW_CHECK(!options_.sift->domain_size_pooling);
    if (options_.sift->darkness_adaptivity) {
      WarnDarknessAdaptivityNotAvailable();
    }
  }

  static std::unique_ptr<FeatureExtractor> Create(
      const FeatureExtractionOptions& options) {
    return std::make_unique<SiftCPUFeatureExtractor>(options);
  }

  bool Extract(const Bitmap& bitmap,
               FeatureKeypoints* keypoints,
               FeatureDescriptors* descriptors) {
    THROW_CHECK(bitmap.IsGrey());
    THROW_CHECK_NOTNULL(keypoints);

    if (sift_ == nullptr || sift_->width != bitmap.Width() ||
        sift_->height != bitmap.Height()) {
      sift_ = VlSiftType(vl_sift_new(bitmap.Width(),
                                     bitmap.Height(),
                                     options_.sift->num_octaves,
                                     options_.sift->octave_resolution,
                                     options_.sift->first_octave),
                         &vl_sift_delete);
      if (!sift_) {
        return false;
      }
    }

    vl_sift_set_peak_thresh(sift_.get(), options_.sift->peak_threshold);
    vl_sift_set_edge_thresh(sift_.get(), options_.sift->edge_threshold);

    // Iterate through octaves.
    std::vector<size_t> level_num_features;
    std::vector<FeatureKeypoints> level_keypoints;
    std::vector<FeatureDescriptors> level_descriptors;
    bool first_octave = true;
    while (true) {
      if (first_octave) {
        const std::vector<uint8_t> data_uint8 = bitmap.ConvertToRowMajorArray();
        std::vector<float> data_float(data_uint8.size());
        for (size_t i = 0; i < data_uint8.size(); ++i) {
          data_float[i] = static_cast<float>(data_uint8[i]) / 255.0f;
        }
        if (vl_sift_process_first_octave(sift_.get(), data_float.data())) {
          break;
        }
        first_octave = false;
      } else {
        if (vl_sift_process_next_octave(sift_.get())) {
          break;
        }
      }

      // Detect keypoints.
      vl_sift_detect(sift_.get());

      // Extract detected keypoints.
      const VlSiftKeypoint* vl_keypoints = vl_sift_get_keypoints(sift_.get());
      const int num_keypoints = vl_sift_get_nkeypoints(sift_.get());
      if (num_keypoints == 0) {
        continue;
      }

      // Extract features with different orientations per DOG level.
      size_t level_idx = 0;
      int prev_level = -1;
      FeatureDescriptorsFloat desc(1, 128);
      for (int i = 0; i < num_keypoints; ++i) {
        if (vl_keypoints[i].is != prev_level) {
          if (i > 0) {
            // Resize containers of previous DOG level.
            level_keypoints.back().resize(level_idx);
            if (descriptors != nullptr) {
              level_descriptors.back().conservativeResize(level_idx, 128);
            }
          }

          // Add containers for new DOG level.
          level_idx = 0;
          level_num_features.push_back(0);
          level_keypoints.emplace_back(options_.sift->max_num_orientations *
                                       num_keypoints);
          if (descriptors != nullptr) {
            level_descriptors.emplace_back(
                options_.sift->max_num_orientations * num_keypoints, 128);
          }
        }

        level_num_features.back() += 1;
        prev_level = vl_keypoints[i].is;

        // Extract feature orientations.
        double angles[4];
        int num_orientations;
        if (options_.sift->upright) {
          num_orientations = 1;
          angles[0] = 0.0;
        } else {
          num_orientations = vl_sift_calc_keypoint_orientations(
              sift_.get(), angles, &vl_keypoints[i]);
        }

        // Note that this is different from SiftGPU, which selects the top
        // global maxima as orientations while this selects the first two
        // local maxima. It is not clear which procedure is better.
        const int num_used_orientations =
            std::min(num_orientations, options_.sift->max_num_orientations);

        for (int o = 0; o < num_used_orientations; ++o) {
          level_keypoints.back()[level_idx] =
              FeatureKeypoint(vl_keypoints[i].x + 0.5f,
                              vl_keypoints[i].y + 0.5f,
                              vl_keypoints[i].sigma,
                              angles[o]);
          if (descriptors != nullptr) {
            vl_sift_calc_keypoint_descriptor(
                sift_.get(), desc.data(), &vl_keypoints[i], angles[o]);
            if (options_.sift->normalization ==
                SiftExtractionOptions::Normalization::L2) {
              L2NormalizeFeatureDescriptors(&desc);
            } else if (options_.sift->normalization ==
                       SiftExtractionOptions::Normalization::L1_ROOT) {
              L1RootNormalizeFeatureDescriptors(&desc);
            } else {
              LOG(FATAL_THROW) << "Normalization type not supported";
            }

            level_descriptors.back().row(level_idx) =
                FeatureDescriptorsToUnsignedByte(desc);
          }

          level_idx += 1;
        }
      }

      // Resize containers for last DOG level in octave.
      level_keypoints.back().resize(level_idx);
      if (descriptors != nullptr) {
        level_descriptors.back().conservativeResize(level_idx, 128);
      }
    }

    // Determine how many DOG levels to keep to satisfy max_num_features option.
    int first_level_to_keep = 0;
    int num_features = 0;
    int num_features_with_orientations = 0;
    for (int i = level_keypoints.size() - 1; i >= 0; --i) {
      num_features += level_num_features[i];
      num_features_with_orientations += level_keypoints[i].size();
      if (num_features > options_.sift->max_num_features) {
        first_level_to_keep = i;
        break;
      }
    }

    // Extract the features to be kept.
    {
      size_t k = 0;
      keypoints->resize(num_features_with_orientations);
      for (size_t i = first_level_to_keep; i < level_keypoints.size(); ++i) {
        for (size_t j = 0; j < level_keypoints[i].size(); ++j) {
          (*keypoints)[k] = level_keypoints[i][j];
          k += 1;
        }
      }
    }

    // Compute the descriptors for the detected keypoints.
    if (descriptors != nullptr) {
      size_t k = 0;
      descriptors->resize(num_features_with_orientations, 128);
      for (size_t i = first_level_to_keep; i < level_keypoints.size(); ++i) {
        for (size_t j = 0; j < level_keypoints[i].size(); ++j) {
          descriptors->row(k) = level_descriptors[i].row(j);
          k += 1;
        }
      }
      *descriptors = TransformVLFeatToUBCFeatureDescriptors(*descriptors);
    }

    return true;
  }

 private:
  const FeatureExtractionOptions options_;
  VlSiftType sift_;
};

class CovariantSiftCPUFeatureExtractor : public FeatureExtractor {
 public:
  explicit CovariantSiftCPUFeatureExtractor(
      const FeatureExtractionOptions& options)
      : options_(options) {
    THROW_CHECK(options_.Check());
    if (options_.sift->darkness_adaptivity) {
      WarnDarknessAdaptivityNotAvailable();
    }
  }

  static std::unique_ptr<FeatureExtractor> Create(
      const FeatureExtractionOptions& options) {
    return std::make_unique<CovariantSiftCPUFeatureExtractor>(options);
  }

  bool Extract(const Bitmap& bitmap,
               FeatureKeypoints* keypoints,
               FeatureDescriptors* descriptors) {
    THROW_CHECK(bitmap.IsGrey());
    THROW_CHECK_NOTNULL(keypoints);

    // Setup covariant SIFT detector.
    std::unique_ptr<VlCovDet, void (*)(VlCovDet*)> covdet(
        vl_covdet_new(VL_COVDET_METHOD_DOG), &vl_covdet_delete);
    if (!covdet) {
      return false;
    }

    const int kMaxOctaveResolution = 1000;
    THROW_CHECK_LE(options_.sift->octave_resolution, kMaxOctaveResolution);

    vl_covdet_set_first_octave(covdet.get(), options_.sift->first_octave);
    vl_covdet_set_octave_resolution(covdet.get(),
                                    options_.sift->octave_resolution);
    vl_covdet_set_peak_threshold(covdet.get(), options_.sift->peak_threshold);
    vl_covdet_set_edge_threshold(covdet.get(), options_.sift->edge_threshold);

    {
      const std::vector<uint8_t> data_uint8 = bitmap.ConvertToRowMajorArray();
      std::vector<float> data_float(data_uint8.size());
      for (size_t i = 0; i < data_uint8.size(); ++i) {
        data_float[i] = static_cast<float>(data_uint8[i]) / 255.0f;
      }
      vl_covdet_put_image(
          covdet.get(), data_float.data(), bitmap.Width(), bitmap.Height());
    }

    vl_covdet_detect(covdet.get(), options_.sift->max_num_features);

    if (options_.sift->estimate_affine_shape) {
      vl_covdet_extract_affine_shape(covdet.get());
    }

    if (!options_.sift->upright) {
      vl_covdet_extract_orientations(covdet.get());
    }

    const int num_features = vl_covdet_get_num_features(covdet.get());
    VlCovDetFeature* features = vl_covdet_get_features(covdet.get());

    // Sort features according to detected octave and scale.
    std::sort(
        features,
        features + num_features,
        [](const VlCovDetFeature& feature1, const VlCovDetFeature& feature2) {
          if (feature1.o == feature2.o) {
            return feature1.s > feature2.s;
          } else {
            return feature1.o > feature2.o;
          }
        });

    const size_t max_num_features =
        static_cast<size_t>(options_.sift->max_num_features);

    // Copy detected keypoints and clamp when maximum number of features
    // reached.
    int prev_octave_scale_idx = std::numeric_limits<int>::max();
    for (int i = 0; i < num_features; ++i) {
      FeatureKeypoint keypoint;
      keypoint.x = features[i].frame.x + 0.5;
      keypoint.y = features[i].frame.y + 0.5;
      keypoint.a11 = features[i].frame.a11;
      keypoint.a12 = features[i].frame.a12;
      keypoint.a21 = features[i].frame.a21;
      keypoint.a22 = features[i].frame.a22;
      keypoints->push_back(keypoint);

      const int octave_scale_idx =
          features[i].o * kMaxOctaveResolution + features[i].s;
      THROW_CHECK_LE(octave_scale_idx, prev_octave_scale_idx);

      if (octave_scale_idx != prev_octave_scale_idx &&
          keypoints->size() >= max_num_features) {
        break;
      }

      prev_octave_scale_idx = octave_scale_idx;
    }

    // Compute the descriptors for the detected keypoints.
    if (descriptors != nullptr) {
      descriptors->resize(keypoints->size(), 128);

      const size_t kPatchResolution = 15;
      const size_t kPatchSide = 2 * kPatchResolution + 1;
      const double kPatchRelativeExtent = 7.5;
      const double kPatchRelativeSmoothing = 1;
      const double kPatchStep = kPatchRelativeExtent / kPatchResolution;
      const double kSigma =
          kPatchRelativeExtent / (3.0 * (4 + 1) / 2) / kPatchStep;

      std::vector<float> patch(kPatchSide * kPatchSide);
      std::vector<float> patchXY(2 * kPatchSide * kPatchSide);

      float dsp_min_scale = 1;
      float dsp_scale_step = 0;
      int dsp_num_scales = 1;
      if (options_.sift->domain_size_pooling) {
        dsp_min_scale = options_.sift->dsp_min_scale;
        dsp_scale_step =
            (options_.sift->dsp_max_scale - options_.sift->dsp_min_scale) /
            options_.sift->dsp_num_scales;
        dsp_num_scales = options_.sift->dsp_num_scales;
      }

      FeatureDescriptorsFloat descriptor(1, 128);
      FeatureDescriptorsFloat scaled_descriptors(dsp_num_scales, 128);

      std::unique_ptr<VlSiftFilt, void (*)(VlSiftFilt*)> sift(
          vl_sift_new(16, 16, 1, 3, 0), &vl_sift_delete);
      if (!sift) {
        return false;
      }

      vl_sift_set_magnif(sift.get(), 3.0);

      for (size_t i = 0; i < keypoints->size(); ++i) {
        for (int s = 0; s < dsp_num_scales; ++s) {
          const double dsp_scale = dsp_min_scale + s * dsp_scale_step;

          VlFrameOrientedEllipse scaled_frame = features[i].frame;
          scaled_frame.a11 *= dsp_scale;
          scaled_frame.a12 *= dsp_scale;
          scaled_frame.a21 *= dsp_scale;
          scaled_frame.a22 *= dsp_scale;

          vl_covdet_extract_patch_for_frame(covdet.get(),
                                            patch.data(),
                                            kPatchResolution,
                                            kPatchRelativeExtent,
                                            kPatchRelativeSmoothing,
                                            scaled_frame);

          vl_imgradient_polar_f(patchXY.data(),
                                patchXY.data() + 1,
                                2,
                                2 * kPatchSide,
                                patch.data(),
                                kPatchSide,
                                kPatchSide,
                                kPatchSide);

          vl_sift_calc_raw_descriptor(sift.get(),
                                      patchXY.data(),
                                      scaled_descriptors.row(s).data(),
                                      kPatchSide,
                                      kPatchSide,
                                      kPatchResolution,
                                      kPatchResolution,
                                      kSigma,
                                      0);
        }

        if (options_.sift->domain_size_pooling) {
          descriptor = scaled_descriptors.colwise().mean();
        } else {
          descriptor = scaled_descriptors;
        }

        THROW_CHECK_EQ(descriptor.cols(), 128);

        if (options_.sift->normalization ==
            SiftExtractionOptions::Normalization::L2) {
          L2NormalizeFeatureDescriptors(&descriptor);
        } else if (options_.sift->normalization ==
                   SiftExtractionOptions::Normalization::L1_ROOT) {
          L1RootNormalizeFeatureDescriptors(&descriptor);
        } else {
          LOG(FATAL_THROW) << "Normalization type not supported";
        }

        descriptors->row(i) = FeatureDescriptorsToUnsignedByte(descriptor);
      }

      *descriptors = TransformVLFeatToUBCFeatureDescriptors(*descriptors);
    }

    return true;
  }

 private:
  const FeatureExtractionOptions options_;
};

#if defined(COLMAP_GPU_ENABLED)
// Mutexes that ensure that only one thread extracts/matches on the same GPU
// at the same time, since SiftGPU internally uses static variables.
static std::map<int, std::unique_ptr<std::mutex>> sift_gpu_mutexes_;

class SiftGPUFeatureExtractor : public FeatureExtractor {
 public:
  explicit SiftGPUFeatureExtractor(const FeatureExtractionOptions& options)
      : options_(options) {
    THROW_CHECK(options_.Check());
    THROW_CHECK(!options_.sift->estimate_affine_shape);
    THROW_CHECK(!options_.sift->domain_size_pooling);
  }

  static std::unique_ptr<FeatureExtractor> Create(
      const FeatureExtractionOptions& options) {
    // SiftGPU uses many global static state variables and the initialization
    // must be thread-safe in order to work correctly. This is enforced here.
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<int> gpu_indices = CSVToVector<int>(options.gpu_index);
    THROW_CHECK_EQ(gpu_indices.size(), 1) << "SiftGPU can only run on one GPU";

    std::vector<std::string> sift_gpu_args;

    sift_gpu_args.push_back("./sift_gpu");

#if defined(COLMAP_CUDA_ENABLED)
    // Use CUDA version by default if darkness adaptivity is disabled.
    if (!options.sift->darkness_adaptivity && gpu_indices[0] < 0) {
      gpu_indices[0] = 0;
    }

    if (gpu_indices[0] >= 0) {
      sift_gpu_args.push_back("-cuda");
      sift_gpu_args.push_back(std::to_string(gpu_indices[0]));
    }
#endif  // COLMAP_CUDA_ENABLED

    // Darkness adaptivity (hidden feature). Significantly improves
    // distribution of features. Only available in GLSL version.
    if (options.sift->darkness_adaptivity) {
      if (gpu_indices[0] >= 0) {
        WarnDarknessAdaptivityNotAvailable();
      }
      sift_gpu_args.push_back("-da");
    }

    // No verbose logging.
    sift_gpu_args.push_back("-v");
    sift_gpu_args.push_back("0");

    // Set maximum image dimension.
    // Note the max dimension of SiftGPU is the maximum dimension of the
    // first octave in the pyramid (which is the 'first_octave').
    const int compensation_factor = 1
                                    << -std::min(0, options.sift->first_octave);
    sift_gpu_args.push_back("-maxd");
    sift_gpu_args.push_back(
        std::to_string(options.sift->max_image_size * compensation_factor));

    // Keep the highest level features.
    sift_gpu_args.push_back("-tc2");
    sift_gpu_args.push_back(std::to_string(options.sift->max_num_features));

    // First octave level.
    sift_gpu_args.push_back("-fo");
    sift_gpu_args.push_back(std::to_string(options.sift->first_octave));

    // Number of octave levels.
    sift_gpu_args.push_back("-d");
    sift_gpu_args.push_back(std::to_string(options.sift->octave_resolution));

    // Peak threshold.
    sift_gpu_args.push_back("-t");
    sift_gpu_args.push_back(std::to_string(options.sift->peak_threshold));

    // Edge threshold.
    sift_gpu_args.push_back("-e");
    sift_gpu_args.push_back(std::to_string(options.sift->edge_threshold));

    if (options.sift->upright) {
      // Fix the orientation to 0 for upright features.
      sift_gpu_args.push_back("-ofix");
      // Maximum number of orientations.
      sift_gpu_args.push_back("-mo");
      sift_gpu_args.push_back("1");
    } else {
      // Maximum number of orientations.
      sift_gpu_args.push_back("-mo");
      sift_gpu_args.push_back(
          std::to_string(options.sift->max_num_orientations));
    }

    std::vector<const char*> sift_gpu_args_cstr;
    sift_gpu_args_cstr.reserve(sift_gpu_args.size());
    for (const auto& arg : sift_gpu_args) {
      sift_gpu_args_cstr.push_back(arg.c_str());
    }

    auto extractor = std::make_unique<SiftGPUFeatureExtractor>(options);

    // Note that the SiftGPU object is not movable (for whatever reason).
    // If we instead create the object here and move it to the constructor, the
    // program segfaults inside SiftGPU.

    extractor->sift_gpu_.ParseParam(sift_gpu_args_cstr.size(),
                                    sift_gpu_args_cstr.data());

    extractor->sift_gpu_.gpu_index = gpu_indices[0];
    if (sift_gpu_mutexes_.count(gpu_indices[0]) == 0) {
      sift_gpu_mutexes_.emplace(gpu_indices[0], std::make_unique<std::mutex>());
    }

    if (extractor->sift_gpu_.VerifyContextGL() !=
        SiftGPU::SIFTGPU_FULL_SUPPORTED) {
      return nullptr;
    }

    return extractor;
  }

  bool Extract(const Bitmap& bitmap,
               FeatureKeypoints* keypoints,
               FeatureDescriptors* descriptors) override {
    THROW_CHECK(bitmap.IsGrey());
    THROW_CHECK_NOTNULL(keypoints);
    THROW_CHECK_NOTNULL(descriptors);

    // Note the max dimension of SiftGPU is the maximum dimension of the
    // first octave in the pyramid (which is the 'first_octave').
    const int compensation_factor =
        1 << -std::min(0, options_.sift->first_octave);
    THROW_CHECK_EQ(options_.sift->max_image_size * compensation_factor,
                   sift_gpu_.GetMaxDimension());

    std::lock_guard<std::mutex> lock(*sift_gpu_mutexes_[sift_gpu_.gpu_index]);

    // Note, that this produces slightly different results than using SiftGPU
    // directly for RGB->GRAY conversion, since it uses different weights.
    const std::vector<uint8_t> bitmap_raw_bits = bitmap.ConvertToRawBits();
    const int code = sift_gpu_.RunSIFT(bitmap.Pitch(),
                                       bitmap.Height(),
                                       bitmap_raw_bits.data(),
                                       GL_LUMINANCE,
                                       GL_UNSIGNED_BYTE);

    const int kSuccessCode = 1;
    if (code != kSuccessCode) {
      return false;
    }

    const size_t num_features = static_cast<size_t>(sift_gpu_.GetFeatureNum());

    keypoints_buffer_.resize(num_features);

    FeatureDescriptorsFloat descriptors_float(num_features, 128);

    // Download the extracted keypoints and descriptors.
    sift_gpu_.GetFeatureVector(keypoints_buffer_.data(),
                               descriptors_float.data());

    keypoints->resize(num_features);
    for (size_t i = 0; i < num_features; ++i) {
      (*keypoints)[i] = FeatureKeypoint(keypoints_buffer_[i].x,
                                        keypoints_buffer_[i].y,
                                        keypoints_buffer_[i].s,
                                        keypoints_buffer_[i].o);
    }

    // Save and normalize the descriptors.
    if (options_.sift->normalization ==
        SiftExtractionOptions::Normalization::L2) {
      L2NormalizeFeatureDescriptors(&descriptors_float);
    } else if (options_.sift->normalization ==
               SiftExtractionOptions::Normalization::L1_ROOT) {
      L1RootNormalizeFeatureDescriptors(&descriptors_float);
    } else {
      LOG(FATAL_THROW) << "Normalization type not supported";
    }

    *descriptors = FeatureDescriptorsToUnsignedByte(descriptors_float);

    return true;
  }

 private:
  const FeatureExtractionOptions options_;
  SiftGPU sift_gpu_;
  std::vector<SiftKeypoint> keypoints_buffer_;
};
#endif  // COLMAP_GPU_ENABLED

}  // namespace

std::unique_ptr<FeatureExtractor> CreateSiftFeatureExtractor(
    const FeatureExtractionOptions& options) {
  if (options.sift->estimate_affine_shape ||
      options.sift->domain_size_pooling ||
      options.sift->force_covariant_extractor) {
    LOG(INFO) << "Creating Covariant SIFT CPU feature extractor";
    return CovariantSiftCPUFeatureExtractor::Create(options);
  } else if (options.use_gpu) {
#if defined(COLMAP_GPU_ENABLED)
    LOG(INFO) << "Creating SIFT GPU feature extractor";
    return SiftGPUFeatureExtractor::Create(options);
#else
    return nullptr;
#endif  // COLMAP_GPU_ENABLED
  } else {
    LOG(INFO) << "Creating SIFT CPU feature extractor";
    return SiftCPUFeatureExtractor::Create(options);
  }
}

namespace {

size_t FindBestMatchesOneWayBruteForce(
    const Eigen::RowMajorMatrixXf& dot_products,
    const float max_ratio,
    const float max_distance,
    std::vector<int>* matches) {
  constexpr float kInvSqDescriptorNorm =
      static_cast<float>(1. / kSqSiftDescriptorNorm);

  size_t num_matches = 0;
  matches->resize(dot_products.rows(), -1);

  for (Eigen::Index i1 = 0; i1 < dot_products.rows(); ++i1) {
    int best_d2_idx = -1;
    float best_dot_product = 0;
    float second_best_dot_product = 0;
    for (Eigen::Index i2 = 0; i2 < dot_products.cols(); ++i2) {
      const float dot_product = dot_products(i1, i2);
      if (dot_product > best_dot_product) {
        best_d2_idx = i2;
        second_best_dot_product = best_dot_product;
        best_dot_product = dot_product;
      } else if (dot_product > second_best_dot_product) {
        second_best_dot_product = dot_product;
      }
    }

    // Check if any match found.
    if (best_d2_idx == -1) {
      continue;
    }

    // Convert to L2 distance in which the thresholds are defined.
    const float best_dist_normed =
        std::acos(std::min(kInvSqDescriptorNorm * best_dot_product, 1.0f));

    // Check if match distance passes threshold.
    if (best_dist_normed > max_distance) {
      continue;
    }

    const float second_best_dist_normed = std::acos(
        std::min(kInvSqDescriptorNorm * second_best_dot_product, 1.0f));

    // Check if match passes ratio test. Keep this comparison >= in order to
    // ensure that the case of best == second_best is detected.
    if (best_dist_normed >= max_ratio * second_best_dist_normed) {
      continue;
    }

    ++num_matches;
    (*matches)[i1] = best_d2_idx;
  }

  return num_matches;
}

void FindBestMatchesBruteForce(const Eigen::RowMajorMatrixXf& dot_products,
                               const float max_ratio,
                               const float max_distance,
                               const bool cross_check,
                               FeatureMatches* matches) {
  matches->clear();

  std::vector<int> matches_1to2;
  const size_t num_matches_1to2 = FindBestMatchesOneWayBruteForce(
      dot_products, max_ratio, max_distance, &matches_1to2);

  if (cross_check) {
    std::vector<int> matches_2to1;
    const size_t num_matches_2to1 = FindBestMatchesOneWayBruteForce(
        dot_products.transpose(), max_ratio, max_distance, &matches_2to1);
    matches->reserve(std::min(num_matches_1to2, num_matches_2to1));
    for (size_t i1 = 0; i1 < matches_1to2.size(); ++i1) {
      if (matches_1to2[i1] != -1 && matches_2to1[matches_1to2[i1]] != -1 &&
          matches_2to1[matches_1to2[i1]] == static_cast<int>(i1)) {
        FeatureMatch match;
        match.point2D_idx1 = i1;
        match.point2D_idx2 = matches_1to2[i1];
        matches->push_back(match);
      }
    }
  } else {
    matches->reserve(num_matches_1to2);
    for (size_t i1 = 0; i1 < matches_1to2.size(); ++i1) {
      if (matches_1to2[i1] != -1) {
        FeatureMatch match;
        match.point2D_idx1 = i1;
        match.point2D_idx2 = matches_1to2[i1];
        matches->push_back(match);
      }
    }
  }
}

size_t FindBestMatchesOneWayIndex(const Eigen::RowMajorMatrixXi& indices,
                                  const Eigen::RowMajorMatrixXf& l2_dists,
                                  const float max_ratio,
                                  const float max_distance,
                                  std::vector<int>* matches) {
  const float max_l2_dist = kSqSiftDescriptorNorm * max_distance * max_distance;

  size_t num_matches = 0;
  matches->resize(indices.rows(), -1);

  for (int d1_idx = 0; d1_idx < indices.rows(); ++d1_idx) {
    int best_d2_idx = -1;
    float best_l2_dist = std::numeric_limits<float>::max();
    float second_best_l2_dist = std::numeric_limits<float>::max();
    for (int n_idx = 0; n_idx < indices.cols(); ++n_idx) {
      const int d2_idx = indices(d1_idx, n_idx);
      const float l2_dist = l2_dists(d1_idx, n_idx);
      if (l2_dist < best_l2_dist) {
        best_d2_idx = d2_idx;
        second_best_l2_dist = best_l2_dist;
        best_l2_dist = l2_dist;
      } else if (l2_dist < second_best_l2_dist) {
        second_best_l2_dist = l2_dist;
      }
    }

    // Check if any match found.
    if (best_d2_idx == -1) {
      continue;
    }

    // Check if match distance passes threshold.
    if (best_l2_dist > max_l2_dist) {
      continue;
    }

    // Check if match passes ratio test. Keep this comparison >= in order to
    // ensure that the case of best == second_best is detected.
    if (std::sqrt(best_l2_dist) >= max_ratio * std::sqrt(second_best_l2_dist)) {
      continue;
    }

    ++num_matches;
    (*matches)[d1_idx] = best_d2_idx;
  }

  return num_matches;
}

void FindBestMatchesIndex(const Eigen::RowMajorMatrixXi& indices_1to2,
                          const Eigen::RowMajorMatrixXf& l2_dists_1to2,
                          const Eigen::RowMajorMatrixXi& indices_2to1,
                          const Eigen::RowMajorMatrixXf& l2_dists_2to1,
                          const float max_ratio,
                          const float max_distance,
                          const bool cross_check,
                          FeatureMatches* matches) {
  matches->clear();

  std::vector<int> matches_1to2;
  const size_t num_matches_1to2 = FindBestMatchesOneWayIndex(
      indices_1to2, l2_dists_1to2, max_ratio, max_distance, &matches_1to2);

  if (cross_check && indices_2to1.rows()) {
    std::vector<int> matches_2to1;
    const size_t num_matches_2to1 = FindBestMatchesOneWayIndex(
        indices_2to1, l2_dists_2to1, max_ratio, max_distance, &matches_2to1);
    matches->reserve(std::min(num_matches_1to2, num_matches_2to1));
    for (size_t i1 = 0; i1 < matches_1to2.size(); ++i1) {
      if (matches_1to2[i1] != -1 && matches_2to1[matches_1to2[i1]] != -1 &&
          matches_2to1[matches_1to2[i1]] == static_cast<int>(i1)) {
        FeatureMatch match;
        match.point2D_idx1 = i1;
        match.point2D_idx2 = matches_1to2[i1];
        matches->push_back(match);
      }
    }
  } else {
    matches->reserve(num_matches_1to2);
    for (size_t i1 = 0; i1 < matches_1to2.size(); ++i1) {
      if (matches_1to2[i1] != -1) {
        FeatureMatch match;
        match.point2D_idx1 = i1;
        match.point2D_idx2 = matches_1to2[i1];
        matches->push_back(match);
      }
    }
  }
}

enum class DistanceType {
  L2,
  DOT_PRODUCT,
};

Eigen::RowMajorMatrixXf ComputeSiftDistanceMatrix(
    const DistanceType distance_type,
    const FeatureKeypoints* keypoints1,
    const FeatureKeypoints* keypoints2,
    const FeatureDescriptors& descriptors1,
    const FeatureDescriptors& descriptors2,
    const std::function<bool(float, float, float, float)>& guided_filter) {
  if (guided_filter != nullptr) {
    THROW_CHECK_NOTNULL(keypoints1);
    THROW_CHECK_NOTNULL(keypoints2);
    THROW_CHECK_EQ(keypoints1->size(), descriptors1.rows());
    THROW_CHECK_EQ(keypoints2->size(), descriptors2.rows());
  }

  const Eigen::Matrix<int, Eigen::Dynamic, 128> descriptors1_int =
      descriptors1.cast<int>();
  const Eigen::Matrix<int, Eigen::Dynamic, 128> descriptors2_int =
      descriptors2.cast<int>();

  Eigen::RowMajorMatrixXf distances(descriptors1.rows(), descriptors2.rows());
  for (FeatureDescriptors::Index i1 = 0; i1 < descriptors1.rows(); ++i1) {
    for (FeatureDescriptors::Index i2 = 0; i2 < descriptors2.rows(); ++i2) {
      if (guided_filter != nullptr && guided_filter((*keypoints1)[i1].x,
                                                    (*keypoints1)[i1].y,
                                                    (*keypoints2)[i2].x,
                                                    (*keypoints2)[i2].y)) {
        if (distance_type == DistanceType::L2) {
          distances(i1, i2) = kSqSiftDescriptorNorm;
        } else if (distance_type == DistanceType::DOT_PRODUCT) {
          distances(i1, i2) = 0;
        } else {
          LOG(FATAL_THROW) << "Distance type not supported";
        }
      } else {
        if (distance_type == DistanceType::L2) {
          distances(i1, i2) =
              (descriptors1_int.row(i1) - descriptors2_int.row(i2))
                  .squaredNorm();
        } else if (distance_type == DistanceType::DOT_PRODUCT) {
          distances(i1, i2) =
              descriptors1_int.row(i1).dot(descriptors2_int.row(i2));
        } else {
          LOG(FATAL_THROW) << "Distance type not supported";
        }
      }
    }
  }

  return distances;
}

class SiftCPUFeatureMatcher : public FeatureMatcher {
 public:
  explicit SiftCPUFeatureMatcher(const FeatureMatchingOptions& options)
      : options_(options) {
    THROW_CHECK(options_.Check());
  }

  static std::unique_ptr<FeatureMatcher> Create(
      const FeatureMatchingOptions& options) {
    return std::make_unique<SiftCPUFeatureMatcher>(options);
  }

  void Match(const Image& image1,
             const Image& image2,
             FeatureMatches* matches) override {
    THROW_CHECK_NOTNULL(matches);
    THROW_CHECK_NE(image1.image_id, kInvalidImageId);
    THROW_CHECK_NE(image2.image_id, kInvalidImageId);
    THROW_CHECK_NOTNULL(image1.descriptors);
    THROW_CHECK_NOTNULL(image2.descriptors);
    THROW_CHECK_EQ(image1.descriptors->cols(), 128);
    THROW_CHECK_EQ(image2.descriptors->cols(), 128);

    matches->clear();

    if (!options_.sift->cpu_brute_force_matcher &&
        (prev_image_id1_ == kInvalidImageId ||
         prev_image_id1_ != image1.image_id)) {
      index1_ = options_.sift->cpu_descriptor_index_cache->Get(image1.image_id);
      prev_image_id1_ = image1.image_id;
    }

    if (!options_.sift->cpu_brute_force_matcher &&
        (prev_image_id2_ == kInvalidImageId ||
         prev_image_id2_ != image2.image_id)) {
      index2_ = options_.sift->cpu_descriptor_index_cache->Get(image2.image_id);
      prev_image_id2_ = image2.image_id;
    }

    if (image1.descriptors->rows() == 0 || image2.descriptors->rows() == 0) {
      return;
    }

    if (options_.sift->cpu_brute_force_matcher) {
      const Eigen::RowMajorMatrixXf dot_products =
          ComputeSiftDistanceMatrix(DistanceType::DOT_PRODUCT,
                                    nullptr,
                                    nullptr,
                                    *image1.descriptors,
                                    *image2.descriptors,
                                    nullptr);
      FindBestMatchesBruteForce(dot_products,
                                options_.sift->max_ratio,
                                options_.sift->max_distance,
                                options_.sift->cross_check,
                                matches);
      return;
    }

    Eigen::RowMajorMatrixXi indices_1to2;
    Eigen::RowMajorMatrixXf l2_dists_1to2;
    Eigen::RowMajorMatrixXi indices_2to1;
    Eigen::RowMajorMatrixXf l2_dists_2to1;
    index2_->Search(
        /*num_neighbors=*/2,
        image1.descriptors->cast<float>(),
        indices_1to2,
        l2_dists_1to2);
    if (options_.sift->cross_check) {
      index1_->Search(/*num_neighbors=*/2,
                      image2.descriptors->cast<float>(),
                      indices_2to1,
                      l2_dists_2to1);
    }

    FindBestMatchesIndex(indices_1to2,
                         l2_dists_1to2,
                         indices_2to1,
                         l2_dists_2to1,
                         options_.sift->max_ratio,
                         options_.sift->max_distance,
                         options_.sift->cross_check,
                         matches);
  }

  void MatchGuided(const double max_error,
                   const Image& image1,
                   const Image& image2,
                   TwoViewGeometry* two_view_geometry) override {
    THROW_CHECK_NOTNULL(two_view_geometry);
    THROW_CHECK_NE(image1.image_id, kInvalidImageId);
    THROW_CHECK_NE(image2.image_id, kInvalidImageId);
    THROW_CHECK_NOTNULL(image1.descriptors);
    THROW_CHECK_NOTNULL(image1.keypoints);
    THROW_CHECK_NOTNULL(image2.descriptors);
    THROW_CHECK_NOTNULL(image2.keypoints);
    THROW_CHECK_EQ(image1.descriptors->rows(), image1.keypoints->size());
    THROW_CHECK_EQ(image2.descriptors->rows(), image2.keypoints->size());
    THROW_CHECK_EQ(image1.descriptors->cols(), 128);
    THROW_CHECK_EQ(image2.descriptors->cols(), 128);

    two_view_geometry->inlier_matches.clear();

    if (!options_.sift->cpu_brute_force_matcher &&
        (prev_image_id1_ == kInvalidImageId ||
         prev_image_id1_ != image1.image_id)) {
      index1_ = options_.sift->cpu_descriptor_index_cache->Get(image1.image_id);
      prev_image_id1_ = image1.image_id;
    }

    if (!options_.sift->cpu_brute_force_matcher &&
        (prev_image_id2_ == kInvalidImageId ||
         prev_image_id2_ != image2.image_id)) {
      index2_ = options_.sift->cpu_descriptor_index_cache->Get(image2.image_id);
      prev_image_id2_ = image2.image_id;
    }

    const float max_residual = max_error * max_error;

    const Eigen::Matrix3f F = two_view_geometry->F.cast<float>();
    const Eigen::Matrix3f H = two_view_geometry->H.cast<float>();

    std::function<bool(float, float, float, float)> guided_filter;
    if (two_view_geometry->config == TwoViewGeometry::CALIBRATED ||
        two_view_geometry->config == TwoViewGeometry::UNCALIBRATED) {
      guided_filter =
          [&](const float x1, const float y1, const float x2, const float y2) {
            const Eigen::Vector3f p1(x1, y1, 1.0f);
            const Eigen::Vector3f p2(x2, y2, 1.0f);
            const Eigen::Vector3f Fx1 = F * p1;
            const Eigen::Vector3f Ftx2 = F.transpose() * p2;
            const float x2tFx1 = p2.transpose() * Fx1;
            return x2tFx1 * x2tFx1 /
                       (Fx1(0) * Fx1(0) + Fx1(1) * Fx1(1) + Ftx2(0) * Ftx2(0) +
                        Ftx2(1) * Ftx2(1)) >
                   max_residual;
          };
    } else if (two_view_geometry->config == TwoViewGeometry::PLANAR ||
               two_view_geometry->config == TwoViewGeometry::PANORAMIC ||
               two_view_geometry->config ==
                   TwoViewGeometry::PLANAR_OR_PANORAMIC) {
      guided_filter =
          [&](const float x1, const float y1, const float x2, const float y2) {
            const Eigen::Vector3f p1(x1, y1, 1.0f);
            const Eigen::Vector2f p2(x2, y2);
            return ((H * p1).hnormalized() - p2).squaredNorm() > max_residual;
          };
    } else {
      return;
    }

    THROW_CHECK(guided_filter);

    const Eigen::RowMajorMatrixXf l2_dists_1to2 =
        ComputeSiftDistanceMatrix(DistanceType::L2,
                                  image1.keypoints.get(),
                                  image2.keypoints.get(),
                                  *image1.descriptors,
                                  *image2.descriptors,
                                  guided_filter);
    const Eigen::RowMajorMatrixXf l2_dists_2to1 = l2_dists_1to2.transpose();

    Eigen::RowMajorMatrixXi indices_1to2(l2_dists_1to2.rows(),
                                         l2_dists_1to2.cols());
    for (int i = 0; i < indices_1to2.rows(); ++i) {
      indices_1to2.row(i) = Eigen::VectorXi::LinSpaced(
          indices_1to2.cols(), 0, indices_1to2.cols() - 1);
    }
    Eigen::RowMajorMatrixXi indices_2to1(l2_dists_1to2.cols(),
                                         l2_dists_1to2.rows());
    for (int i = 0; i < indices_2to1.rows(); ++i) {
      indices_2to1.row(i) = Eigen::VectorXi::LinSpaced(
          indices_2to1.cols(), 0, indices_2to1.cols() - 1);
    }

    FindBestMatchesIndex(indices_1to2,
                         l2_dists_1to2,
                         indices_2to1,
                         l2_dists_2to1,
                         options_.sift->max_ratio,
                         options_.sift->max_distance,
                         options_.sift->cross_check,
                         &two_view_geometry->inlier_matches);
  }

 private:
  const FeatureMatchingOptions options_;
  image_t prev_image_id1_ = kInvalidImageId;
  image_t prev_image_id2_ = kInvalidImageId;
  std::shared_ptr<FeatureDescriptorIndex> index1_;
  std::shared_ptr<FeatureDescriptorIndex> index2_;
};

#if defined(COLMAP_GPU_ENABLED)
// Mutexes that ensure that only one thread extracts/matches on the same GPU
// at the same time, since SiftGPU internally uses static variables.
static std::map<int, std::unique_ptr<std::mutex>> sift_match_gpu_mutexes_;

class SiftGPUFeatureMatcher : public FeatureMatcher {
 public:
  explicit SiftGPUFeatureMatcher(const FeatureMatchingOptions& options)
      : options_(options) {
    THROW_CHECK(options_.sift->Check());
  }

  static std::unique_ptr<FeatureMatcher> Create(
      const FeatureMatchingOptions& options) {
    // SiftGPU uses many global static state variables and the initialization
    // must be thread-safe in order to work correctly. This is enforced here.
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    const std::vector<int> gpu_indices = CSVToVector<int>(options.gpu_index);
    THROW_CHECK_EQ(gpu_indices.size(), 1) << "SiftGPU can only run on one GPU";

    SiftGPU sift_gpu;
    sift_gpu.SetVerbose(0);

    auto matcher = std::make_unique<SiftGPUFeatureMatcher>(options);

    // Note that the SiftMatchGPU object is not movable (for whatever reason).
    // If we instead create the object here and move it to the constructor, the
    // program segfaults inside SiftMatchGPU.

    matcher->sift_match_gpu_ = SiftMatchGPU(options.max_num_matches);

#if defined(COLMAP_CUDA_ENABLED)
    if (gpu_indices[0] >= 0) {
      matcher->sift_match_gpu_.SetLanguage(
          SiftMatchGPU::SIFTMATCH_CUDA_DEVICE0 + gpu_indices[0]);
    } else {
      matcher->sift_match_gpu_.SetLanguage(SiftMatchGPU::SIFTMATCH_CUDA);
    }
#else   // COLMAP_CUDA_ENABLED
    matcher->sift_match_gpu_.SetLanguage(SiftMatchGPU::SIFTMATCH_GLSL);
#endif  // COLMAP_CUDA_ENABLED

    if (matcher->sift_match_gpu_.VerifyContextGL() == 0) {
      return nullptr;
    }

    if (!matcher->sift_match_gpu_.Allocate(options.max_num_matches,
                                           options.sift->cross_check)) {
      LOG(ERROR) << StringPrintf(
          "Not enough GPU memory to match %d features. "
          "Reduce the maximum number of matches.",
          options.max_num_matches);
      return nullptr;
    }

#if !defined(COLMAP_CUDA_ENABLED)
    if (matcher->sift_match_gpu_.GetMaxSift() < options.max_num_matches) {
      LOG(WARNING) << StringPrintf(
          "OpenGL version of SiftGPU only supports a "
          "maximum of %d matches - consider changing to CUDA-based "
          "feature matching to avoid this limitation.",
          matcher->sift_match_gpu_.GetMaxSift());
    }
#endif  // COLMAP_CUDA_ENABLED

    matcher->sift_match_gpu_.gpu_index = gpu_indices[0];
    if (sift_match_gpu_mutexes_.count(gpu_indices[0]) == 0) {
      sift_match_gpu_mutexes_.emplace(gpu_indices[0],
                                      std::make_unique<std::mutex>());
    }

    return matcher;
  }

  void Match(const Image& image1,
             const Image& image2,
             FeatureMatches* matches) override {
    THROW_CHECK_NOTNULL(matches);
    THROW_CHECK_NE(image1.image_id, kInvalidImageId);
    THROW_CHECK_NE(image2.image_id, kInvalidImageId);
    THROW_CHECK_NOTNULL(image1.descriptors);
    THROW_CHECK_NOTNULL(image2.descriptors);
    THROW_CHECK_EQ(image1.descriptors->cols(), 128);
    THROW_CHECK_EQ(image2.descriptors->cols(), 128);

    matches->clear();

    std::lock_guard<std::mutex> lock(
        *sift_match_gpu_mutexes_[sift_match_gpu_.gpu_index]);

    if (prev_image_id1_ == kInvalidImageId || prev_is_guided_ ||
        prev_image_id1_ != image1.image_id) {
      WarnIfMaxNumMatchesReachedGPU(*image1.descriptors);
      sift_match_gpu_.SetDescriptors(
          0, image1.descriptors->rows(), image1.descriptors->data());
      prev_image_id1_ = image1.image_id;
    }

    if (prev_image_id2_ == kInvalidImageId || prev_is_guided_ ||
        prev_image_id2_ != image2.image_id) {
      WarnIfMaxNumMatchesReachedGPU(*image2.descriptors);
      sift_match_gpu_.SetDescriptors(
          1, image2.descriptors->rows(), image2.descriptors->data());
      prev_image_id2_ = image2.image_id;
    }

    prev_is_guided_ = false;

    matches->resize(static_cast<size_t>(options_.max_num_matches));

    const int num_matches = sift_match_gpu_.GetSiftMatch(
        options_.max_num_matches,
        reinterpret_cast<uint32_t(*)[2]>(matches->data()),
        static_cast<float>(options_.sift->max_distance),
        static_cast<float>(options_.sift->max_ratio),
        options_.sift->cross_check);

    if (num_matches < 0) {
      LOG(ERROR) << "Feature matching failed. This is probably caused by "
                    "insufficient GPU memory. Consider reducing the maximum "
                    "number of features and/or matches.";
      matches->clear();
    } else {
      THROW_CHECK_LE(num_matches, matches->size());
      matches->resize(num_matches);
    }
  }

  void MatchGuided(const double max_error,
                   const Image& image1,
                   const Image& image2,
                   TwoViewGeometry* two_view_geometry) override {
    static_assert(offsetof(FeatureKeypoint, x) == 0 * sizeof(float),
                  "Invalid keypoint format");
    static_assert(offsetof(FeatureKeypoint, y) == 1 * sizeof(float),
                  "Invalid keypoint format");
    static_assert(sizeof(FeatureKeypoint) == 6 * sizeof(float),
                  "Invalid keypoint format");

    THROW_CHECK_NOTNULL(two_view_geometry);
    THROW_CHECK_NE(image1.image_id, kInvalidImageId);
    THROW_CHECK_NE(image2.image_id, kInvalidImageId);
    THROW_CHECK_NOTNULL(image1.descriptors);
    THROW_CHECK_NOTNULL(image1.keypoints);
    THROW_CHECK_NOTNULL(image2.descriptors);
    THROW_CHECK_NOTNULL(image2.keypoints);
    THROW_CHECK_EQ(image1.descriptors->rows(), image1.keypoints->size());
    THROW_CHECK_EQ(image2.descriptors->rows(), image2.keypoints->size());
    THROW_CHECK_EQ(image1.descriptors->cols(), 128);
    THROW_CHECK_EQ(image2.descriptors->cols(), 128);

    two_view_geometry->inlier_matches.clear();

    std::lock_guard<std::mutex> lock(
        *sift_match_gpu_mutexes_[sift_match_gpu_.gpu_index]);

    constexpr size_t kFeatureShapeNumElems = 4;

    if (prev_image_id1_ == kInvalidImageId || !prev_is_guided_ ||
        prev_image_id1_ != image1.image_id) {
      WarnIfMaxNumMatchesReachedGPU(*image1.descriptors);
      const size_t kIndex = 0;
      sift_match_gpu_.SetDescriptors(
          kIndex, image1.descriptors->rows(), image1.descriptors->data());
      sift_match_gpu_.SetFeautreLocation(
          kIndex,
          reinterpret_cast<const float*>(image1.keypoints->data()),
          kFeatureShapeNumElems);
      prev_image_id1_ = image1.image_id;
    }

    if (prev_image_id2_ == kInvalidImageId || !prev_is_guided_ ||
        prev_image_id2_ != image2.image_id) {
      WarnIfMaxNumMatchesReachedGPU(*image2.descriptors);
      const size_t kIndex = 1;
      sift_match_gpu_.SetDescriptors(
          kIndex, image2.descriptors->rows(), image2.descriptors->data());
      sift_match_gpu_.SetFeautreLocation(
          kIndex,
          reinterpret_cast<const float*>(image2.keypoints->data()),
          kFeatureShapeNumElems);
      prev_image_id2_ = image2.image_id;
    }

    prev_is_guided_ = true;

    Eigen::Matrix<float, 3, 3, Eigen::RowMajor> F;
    Eigen::Matrix<float, 3, 3, Eigen::RowMajor> H;
    float* F_ptr = nullptr;
    float* H_ptr = nullptr;
    if (two_view_geometry->config == TwoViewGeometry::CALIBRATED ||
        two_view_geometry->config == TwoViewGeometry::UNCALIBRATED) {
      F = two_view_geometry->F.cast<float>();
      F_ptr = F.data();
    } else if (two_view_geometry->config == TwoViewGeometry::PLANAR ||
               two_view_geometry->config == TwoViewGeometry::PANORAMIC ||
               two_view_geometry->config ==
                   TwoViewGeometry::PLANAR_OR_PANORAMIC) {
      H = two_view_geometry->H.cast<float>();
      H_ptr = H.data();
    } else {
      return;
    }

    THROW_CHECK(F_ptr != nullptr || H_ptr != nullptr);

    two_view_geometry->inlier_matches.resize(
        static_cast<size_t>(options_.max_num_matches));

    const float max_residual = static_cast<float>(max_error * max_error);

    const int num_matches = sift_match_gpu_.GetGuidedSiftMatch(
        options_.max_num_matches,
        reinterpret_cast<uint32_t(*)[2]>(
            two_view_geometry->inlier_matches.data()),
        H_ptr,
        F_ptr,
        static_cast<float>(options_.sift->max_distance),
        static_cast<float>(options_.sift->max_ratio),
        max_residual,
        max_residual,
        options_.sift->cross_check);

    if (num_matches < 0) {
      LOG(ERROR) << "Feature matching failed. This is probably caused by "
                    "insufficient GPU memory. Consider reducing the maximum "
                    "number of features.";
      two_view_geometry->inlier_matches.clear();
    } else {
      THROW_CHECK_LE(num_matches, two_view_geometry->inlier_matches.size());
      two_view_geometry->inlier_matches.resize(num_matches);
    }
  }

 private:
  void WarnIfMaxNumMatchesReachedGPU(const FeatureDescriptors& descriptors) {
    if (sift_match_gpu_.GetMaxSift() < descriptors.rows()) {
      LOG(WARNING) << StringPrintf(
          "Clamping features from %d to %d - consider "
          "increasing the maximum number of matches.",
          descriptors.rows(),
          sift_match_gpu_.GetMaxSift());
    }
  }

  const FeatureMatchingOptions options_;
  SiftMatchGPU sift_match_gpu_;
  bool prev_is_guided_ = false;
  image_t prev_image_id1_ = kInvalidImageId;
  image_t prev_image_id2_ = kInvalidImageId;
};
#endif  // COLMAP_GPU_ENABLED

}  // namespace

std::unique_ptr<FeatureMatcher> CreateSiftFeatureMatcher(
    const FeatureMatchingOptions& options) {
  THROW_CHECK_NOTNULL(options.sift);
  if (options.use_gpu) {
#ifdef COLMAP_GPU_ENABLED
    LOG(INFO) << "Creating SIFT GPU feature matcher";
    return SiftGPUFeatureMatcher::Create(options);
#else
    return nullptr;
#endif  // COLMAP_GPU_ENABLED
  } else {
    LOG(INFO) << "Creating SIFT CPU feature matcher";
    return SiftCPUFeatureMatcher::Create(options);
  }
}

void LoadSiftFeaturesFromTextFile(const std::string& path,
                                  FeatureKeypoints* keypoints,
                                  FeatureDescriptors* descriptors) {
  THROW_CHECK_NOTNULL(keypoints);
  THROW_CHECK_NOTNULL(descriptors);

  std::ifstream file(path.c_str());
  THROW_CHECK_FILE_OPEN(file, path);

  std::string line;
  std::string item;

  std::getline(file, line);
  std::stringstream header_line_stream(line);

  std::getline(header_line_stream >> std::ws, item, ' ');
  const point2D_t num_features = std::stoul(item);

  std::getline(header_line_stream >> std::ws, item, ' ');
  const size_t dim = std::stoul(item);

  THROW_CHECK_EQ(dim, 128) << "SIFT features must have 128 dimensions";

  keypoints->resize(num_features);
  descriptors->resize(num_features, dim);

  for (size_t i = 0; i < num_features; ++i) {
    std::getline(file, line);
    std::stringstream feature_line_stream(line);

    std::getline(feature_line_stream >> std::ws, item, ' ');
    const float x = std::stold(item);

    std::getline(feature_line_stream >> std::ws, item, ' ');
    const float y = std::stold(item);

    std::getline(feature_line_stream >> std::ws, item, ' ');
    const float scale = std::stold(item);

    std::getline(feature_line_stream >> std::ws, item, ' ');
    const float orientation = std::stold(item);

    (*keypoints)[i] = FeatureKeypoint(x, y, scale, orientation);

    // Descriptor
    for (size_t j = 0; j < dim; ++j) {
      std::getline(feature_line_stream >> std::ws, item, ' ');
      const float value = std::stod(item);
      THROW_CHECK_GE(value, 0);
      THROW_CHECK_LE(value, 255);
      (*descriptors)(i, j) = TruncateCast<float, uint8_t>(value);
    }
  }
}

}  //  namespace colmap
