#include "colmap/feature/types.h"

#include "colmap/util/logging.h"

#include "pycolmap/feature/types.h"
#include "pycolmap/helpers.h"
#include "pycolmap/pybind11_extension.h"

#include <pybind11/eigen.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

using namespace colmap;
using namespace pybind11::literals;
namespace py = pybind11;

void BindFeatureTypes(py::module& m) {
  auto PyFeatureKeypoint =
      py::class_<FeatureKeypoint>(m, "FeatureKeypoint")
          .def(py::init<>())
          .def_readwrite("x", &FeatureKeypoint::x)
          .def_readwrite("y", &FeatureKeypoint::y)
          .def_readwrite("a11", &FeatureKeypoint::a11)
          .def_readwrite("a12", &FeatureKeypoint::a12)
          .def_readwrite("a21", &FeatureKeypoint::a21)
          .def_readwrite("a22", &FeatureKeypoint::a22)
          .def_static("from_shape_parameters",
                      &FeatureKeypoint::FromShapeParameters)
          .def("rescale", py::overload_cast<float>(&FeatureKeypoint::Rescale))
          .def("rescale",
               py::overload_cast<float, float>(&FeatureKeypoint::Rescale))
          .def("compute_scale", &FeatureKeypoint::ComputeScale)
          .def("compute_scale_x", &FeatureKeypoint::ComputeScaleX)
          .def("compute_scale_y", &FeatureKeypoint::ComputeScaleY)
          .def("compute_orientation", &FeatureKeypoint::ComputeOrientation)
          .def("compute_shear", &FeatureKeypoint::ComputeShear)
          .def("__repr__", [](const FeatureKeypoint& keypoint) {
            std::ostringstream ss;
            ss << "FeatureKeypoint(x=" << keypoint.x << ", y=" << keypoint.y
               << ")";
            return ss.str();
          });
  MakeDataclass(PyFeatureKeypoint);
  py::bind_vector<FeatureKeypoints>(m, "FeatureKeypoints");
  py::implicitly_convertible<py::iterable, FeatureKeypoints>();

  auto PyFeatureMatch =
      py::class_<FeatureMatch>(m, "FeatureMatch")
          .def(py::init<>())
          .def(py::init<const point2D_t, const point2D_t>())
          .def_readwrite("point2D_idx1", &FeatureMatch::point2D_idx1)
          .def_readwrite("point2D_idx2", &FeatureMatch::point2D_idx2)
          .def("__repr__", [](const FeatureMatch& match) {
            std::ostringstream ss;
            ss << "FeatureMatch(idx1=" << match.point2D_idx1
               << ", idx2=" << match.point2D_idx2 << ")";
            return ss.str();
          });
  MakeDataclass(PyFeatureMatch);
  py::bind_vector<FeatureMatches>(m, "FeatureMatches");
  py::implicitly_convertible<py::iterable, FeatureMatches>();
}
