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

{
  const double* b = B.data();

  coeffs(0) = b[0] * b[17] * b[34] + b[26] * b[4] * b[21] -
              b[26] * b[17] * b[8] - b[13] * b[4] * b[34] -
              b[0] * b[21] * b[30] + b[13] * b[30] * b[8];
  coeffs(1) =
      b[26] * b[4] * b[22] + b[14] * b[30] * b[8] + b[13] * b[31] * b[8] +
      b[1] * b[17] * b[34] - b[13] * b[5] * b[34] + b[26] * b[5] * b[21] -
      b[0] * b[21] * b[31] - b[26] * b[17] * b[9] - b[1] * b[21] * b[30] +
      b[27] * b[4] * b[21] + b[0] * b[17] * b[35] - b[0] * b[22] * b[30] +
      b[13] * b[30] * b[9] + b[0] * b[18] * b[34] - b[27] * b[17] * b[8] -
      b[14] * b[4] * b[34] - b[13] * b[4] * b[35] - b[26] * b[18] * b[8];
  coeffs(2) =
      b[14] * b[30] * b[9] + b[14] * b[31] * b[8] + b[13] * b[31] * b[9] -
      b[13] * b[4] * b[36] - b[13] * b[5] * b[35] + b[15] * b[30] * b[8] -
      b[13] * b[6] * b[34] + b[13] * b[30] * b[10] + b[13] * b[32] * b[8] -
      b[14] * b[4] * b[35] - b[14] * b[5] * b[34] + b[26] * b[4] * b[23] +
      b[26] * b[5] * b[22] + b[26] * b[6] * b[21] - b[26] * b[17] * b[10] -
      b[15] * b[4] * b[34] - b[26] * b[18] * b[9] - b[26] * b[19] * b[8] +
      b[27] * b[4] * b[22] + b[27] * b[5] * b[21] - b[27] * b[17] * b[9] -
      b[27] * b[18] * b[8] - b[1] * b[21] * b[31] - b[0] * b[23] * b[30] -
      b[0] * b[21] * b[32] + b[28] * b[4] * b[21] - b[28] * b[17] * b[8] +
      b[2] * b[17] * b[34] + b[0] * b[18] * b[35] - b[0] * b[22] * b[31] +
      b[0] * b[17] * b[36] + b[0] * b[19] * b[34] - b[1] * b[22] * b[30] +
      b[1] * b[18] * b[34] + b[1] * b[17] * b[35] - b[2] * b[21] * b[30];
  coeffs(3) =
      b[14] * b[30] * b[10] + b[14] * b[32] * b[8] - b[3] * b[21] * b[30] +
      b[3] * b[17] * b[34] + b[13] * b[32] * b[9] + b[13] * b[33] * b[8] -
      b[13] * b[4] * b[37] - b[13] * b[5] * b[36] + b[15] * b[30] * b[9] +
      b[15] * b[31] * b[8] - b[16] * b[4] * b[34] - b[13] * b[6] * b[35] -
      b[13] * b[7] * b[34] + b[13] * b[30] * b[11] + b[13] * b[31] * b[10] +
      b[14] * b[31] * b[9] - b[14] * b[4] * b[36] - b[14] * b[5] * b[35] -
      b[14] * b[6] * b[34] + b[16] * b[30] * b[8] - b[26] * b[20] * b[8] +
      b[26] * b[4] * b[24] + b[26] * b[5] * b[23] + b[26] * b[6] * b[22] +
      b[26] * b[7] * b[21] - b[26] * b[17] * b[11] - b[15] * b[4] * b[35] -
      b[15] * b[5] * b[34] - b[26] * b[18] * b[10] - b[26] * b[19] * b[9] +
      b[27] * b[4] * b[23] + b[27] * b[5] * b[22] + b[27] * b[6] * b[21] -
      b[27] * b[17] * b[10] - b[27] * b[18] * b[9] - b[27] * b[19] * b[8] +
      b[0] * b[17] * b[37] - b[0] * b[23] * b[31] - b[0] * b[24] * b[30] -
      b[0] * b[21] * b[33] - b[29] * b[17] * b[8] + b[28] * b[4] * b[22] +
      b[28] * b[5] * b[21] - b[28] * b[17] * b[9] - b[28] * b[18] * b[8] +
      b[29] * b[4] * b[21] + b[1] * b[19] * b[34] - b[2] * b[21] * b[31] +
      b[0] * b[20] * b[34] + b[0] * b[19] * b[35] + b[0] * b[18] * b[36] -
      b[0] * b[22] * b[32] - b[1] * b[23] * b[30] - b[1] * b[21] * b[32] +
      b[1] * b[18] * b[35] - b[1] * b[22] * b[31] - b[2] * b[22] * b[30] +
      b[2] * b[17] * b[35] + b[1] * b[17] * b[36] + b[2] * b[18] * b[34];
  coeffs(4) =
      -b[14] * b[6] * b[35] - b[14] * b[7] * b[34] - b[3] * b[22] * b[30] -
      b[3] * b[21] * b[31] + b[3] * b[17] * b[35] + b[3] * b[18] * b[34] +
      b[13] * b[32] * b[10] + b[13] * b[33] * b[9] - b[13] * b[4] * b[38] -
      b[13] * b[5] * b[37] - b[15] * b[6] * b[34] + b[15] * b[30] * b[10] +
      b[15] * b[32] * b[8] - b[16] * b[4] * b[35] - b[13] * b[6] * b[36] -
      b[13] * b[7] * b[35] + b[13] * b[31] * b[11] + b[13] * b[30] * b[12] +
      b[14] * b[32] * b[9] + b[14] * b[33] * b[8] - b[14] * b[4] * b[37] -
      b[14] * b[5] * b[36] + b[16] * b[30] * b[9] + b[16] * b[31] * b[8] -
      b[26] * b[20] * b[9] + b[26] * b[4] * b[25] + b[26] * b[5] * b[24] +
      b[26] * b[6] * b[23] + b[26] * b[7] * b[22] - b[26] * b[17] * b[12] +
      b[14] * b[30] * b[11] + b[14] * b[31] * b[10] + b[15] * b[31] * b[9] -
      b[15] * b[4] * b[36] - b[15] * b[5] * b[35] - b[26] * b[18] * b[11] -
      b[26] * b[19] * b[10] - b[27] * b[20] * b[8] + b[27] * b[4] * b[24] +
      b[27] * b[5] * b[23] + b[27] * b[6] * b[22] + b[27] * b[7] * b[21] -
      b[27] * b[17] * b[11] - b[27] * b[18] * b[10] - b[27] * b[19] * b[9] -
      b[16] * b[5] * b[34] - b[29] * b[17] * b[9] - b[29] * b[18] * b[8] +
      b[28] * b[4] * b[23] + b[28] * b[5] * b[22] + b[28] * b[6] * b[21] -
      b[28] * b[17] * b[10] - b[28] * b[18] * b[9] - b[28] * b[19] * b[8] +
      b[29] * b[4] * b[22] + b[29] * b[5] * b[21] - b[2] * b[23] * b[30] +
      b[2] * b[18] * b[35] - b[1] * b[22] * b[32] - b[2] * b[21] * b[32] +
      b[2] * b[19] * b[34] + b[0] * b[19] * b[36] - b[0] * b[22] * b[33] +
      b[0] * b[20] * b[35] - b[0] * b[23] * b[32] - b[0] * b[25] * b[30] +
      b[0] * b[17] * b[38] + b[0] * b[18] * b[37] - b[0] * b[24] * b[31] +
      b[1] * b[17] * b[37] - b[1] * b[23] * b[31] - b[1] * b[24] * b[30] -
      b[1] * b[21] * b[33] + b[1] * b[20] * b[34] + b[1] * b[19] * b[35] +
      b[1] * b[18] * b[36] + b[2] * b[17] * b[36] - b[2] * b[22] * b[31];
  coeffs(5) =
      -b[14] * b[6] * b[36] - b[14] * b[7] * b[35] + b[14] * b[31] * b[11] -
      b[3] * b[23] * b[30] - b[3] * b[21] * b[32] + b[3] * b[18] * b[35] -
      b[3] * b[22] * b[31] + b[3] * b[17] * b[36] + b[3] * b[19] * b[34] +
      b[13] * b[32] * b[11] + b[13] * b[33] * b[10] - b[13] * b[5] * b[38] -
      b[15] * b[6] * b[35] - b[15] * b[7] * b[34] + b[15] * b[30] * b[11] +
      b[15] * b[31] * b[10] + b[16] * b[31] * b[9] - b[13] * b[6] * b[37] -
      b[13] * b[7] * b[36] + b[13] * b[31] * b[12] + b[14] * b[32] * b[10] +
      b[14] * b[33] * b[9] - b[14] * b[4] * b[38] - b[14] * b[5] * b[37] -
      b[16] * b[6] * b[34] + b[16] * b[30] * b[10] + b[16] * b[32] * b[8] -
      b[26] * b[20] * b[10] + b[26] * b[5] * b[25] + b[26] * b[6] * b[24] +
      b[26] * b[7] * b[23] + b[14] * b[30] * b[12] + b[15] * b[32] * b[9] +
      b[15] * b[33] * b[8] - b[15] * b[4] * b[37] - b[15] * b[5] * b[36] +
      b[29] * b[5] * b[22] + b[29] * b[6] * b[21] - b[26] * b[18] * b[12] -
      b[26] * b[19] * b[11] - b[27] * b[20] * b[9] + b[27] * b[4] * b[25] +
      b[27] * b[5] * b[24] + b[27] * b[6] * b[23] + b[27] * b[7] * b[22] -
      b[27] * b[17] * b[12] - b[27] * b[18] * b[11] - b[27] * b[19] * b[10] -
      b[28] * b[20] * b[8] - b[16] * b[4] * b[36] - b[16] * b[5] * b[35] -
      b[29] * b[17] * b[10] - b[29] * b[18] * b[9] - b[29] * b[19] * b[8] +
      b[28] * b[4] * b[24] + b[28] * b[5] * b[23] + b[28] * b[6] * b[22] +
      b[28] * b[7] * b[21] - b[28] * b[17] * b[11] - b[28] * b[18] * b[10] -
      b[28] * b[19] * b[9] + b[29] * b[4] * b[23] - b[2] * b[22] * b[32] -
      b[2] * b[21] * b[33] - b[1] * b[24] * b[31] + b[0] * b[18] * b[38] -
      b[0] * b[24] * b[32] + b[0] * b[19] * b[37] + b[0] * b[20] * b[36] -
      b[0] * b[25] * b[31] - b[0] * b[23] * b[33] + b[1] * b[19] * b[36] -
      b[1] * b[22] * b[33] + b[1] * b[20] * b[35] + b[2] * b[19] * b[35] -
      b[2] * b[24] * b[30] - b[2] * b[23] * b[31] + b[2] * b[20] * b[34] +
      b[2] * b[17] * b[37] - b[1] * b[25] * b[30] + b[1] * b[18] * b[37] +
      b[1] * b[17] * b[38] - b[1] * b[23] * b[32] + b[2] * b[18] * b[36];
  coeffs(6) =
      -b[14] * b[6] * b[37] - b[14] * b[7] * b[36] + b[14] * b[31] * b[12] +
      b[3] * b[17] * b[37] - b[3] * b[23] * b[31] - b[3] * b[24] * b[30] -
      b[3] * b[21] * b[33] + b[3] * b[20] * b[34] + b[3] * b[19] * b[35] +
      b[3] * b[18] * b[36] - b[3] * b[22] * b[32] + b[13] * b[32] * b[12] +
      b[13] * b[33] * b[11] - b[15] * b[6] * b[36] - b[15] * b[7] * b[35] +
      b[15] * b[31] * b[11] + b[15] * b[30] * b[12] + b[16] * b[32] * b[9] +
      b[16] * b[33] * b[8] - b[13] * b[6] * b[38] - b[13] * b[7] * b[37] +
      b[14] * b[32] * b[11] + b[14] * b[33] * b[10] - b[14] * b[5] * b[38] -
      b[16] * b[6] * b[35] - b[16] * b[7] * b[34] + b[16] * b[30] * b[11] +
      b[16] * b[31] * b[10] - b[26] * b[19] * b[12] - b[26] * b[20] * b[11] +
      b[26] * b[6] * b[25] + b[26] * b[7] * b[24] + b[15] * b[32] * b[10] +
      b[15] * b[33] * b[9] - b[15] * b[4] * b[38] - b[15] * b[5] * b[37] +
      b[29] * b[5] * b[23] + b[29] * b[6] * b[22] + b[29] * b[7] * b[21] -
      b[27] * b[20] * b[10] + b[27] * b[5] * b[25] + b[27] * b[6] * b[24] +
      b[27] * b[7] * b[23] - b[27] * b[18] * b[12] - b[27] * b[19] * b[11] -
      b[28] * b[20] * b[9] - b[16] * b[4] * b[37] - b[16] * b[5] * b[36] +
      b[0] * b[19] * b[38] - b[0] * b[24] * b[33] + b[0] * b[20] * b[37] -
      b[29] * b[17] * b[11] - b[29] * b[18] * b[10] - b[29] * b[19] * b[9] +
      b[28] * b[4] * b[25] + b[28] * b[5] * b[24] + b[28] * b[6] * b[23] +
      b[28] * b[7] * b[22] - b[28] * b[17] * b[12] - b[28] * b[18] * b[11] -
      b[28] * b[19] * b[10] - b[29] * b[20] * b[8] + b[29] * b[4] * b[24] +
      b[2] * b[18] * b[37] - b[0] * b[25] * b[32] + b[1] * b[18] * b[38] -
      b[1] * b[24] * b[32] + b[1] * b[19] * b[37] + b[1] * b[20] * b[36] -
      b[1] * b[25] * b[31] + b[2] * b[17] * b[38] + b[2] * b[19] * b[36] -
      b[2] * b[24] * b[31] - b[2] * b[22] * b[33] - b[2] * b[23] * b[32] +
      b[2] * b[20] * b[35] - b[1] * b[23] * b[33] - b[2] * b[25] * b[30];
  coeffs(7) =
      -b[14] * b[6] * b[38] - b[14] * b[7] * b[37] + b[3] * b[19] * b[36] -
      b[3] * b[22] * b[33] + b[3] * b[20] * b[35] - b[3] * b[23] * b[32] -
      b[3] * b[25] * b[30] + b[3] * b[17] * b[38] + b[3] * b[18] * b[37] -
      b[3] * b[24] * b[31] - b[15] * b[6] * b[37] - b[15] * b[7] * b[36] +
      b[15] * b[31] * b[12] + b[16] * b[32] * b[10] + b[16] * b[33] * b[9] +
      b[13] * b[33] * b[12] - b[13] * b[7] * b[38] + b[14] * b[32] * b[12] +
      b[14] * b[33] * b[11] - b[16] * b[6] * b[36] - b[16] * b[7] * b[35] +
      b[16] * b[31] * b[11] + b[16] * b[30] * b[12] + b[15] * b[32] * b[11] +
      b[15] * b[33] * b[10] - b[15] * b[5] * b[38] + b[29] * b[5] * b[24] +
      b[29] * b[6] * b[23] - b[26] * b[20] * b[12] + b[26] * b[7] * b[25] -
      b[27] * b[19] * b[12] - b[27] * b[20] * b[11] + b[27] * b[6] * b[25] +
      b[27] * b[7] * b[24] - b[28] * b[20] * b[10] - b[16] * b[4] * b[38] -
      b[16] * b[5] * b[37] + b[29] * b[7] * b[22] - b[29] * b[17] * b[12] -
      b[29] * b[18] * b[11] - b[29] * b[19] * b[10] + b[28] * b[5] * b[25] +
      b[28] * b[6] * b[24] + b[28] * b[7] * b[23] - b[28] * b[18] * b[12] -
      b[28] * b[19] * b[11] - b[29] * b[20] * b[9] + b[29] * b[4] * b[25] -
      b[2] * b[24] * b[32] + b[0] * b[20] * b[38] - b[0] * b[25] * b[33] +
      b[1] * b[19] * b[38] - b[1] * b[24] * b[33] + b[1] * b[20] * b[37] -
      b[2] * b[25] * b[31] + b[2] * b[20] * b[36] - b[1] * b[25] * b[32] +
      b[2] * b[19] * b[37] + b[2] * b[18] * b[38] - b[2] * b[23] * b[33];
  coeffs(8) =
      b[3] * b[18] * b[38] - b[3] * b[24] * b[32] + b[3] * b[19] * b[37] +
      b[3] * b[20] * b[36] - b[3] * b[25] * b[31] - b[3] * b[23] * b[33] -
      b[15] * b[6] * b[38] - b[15] * b[7] * b[37] + b[16] * b[32] * b[11] +
      b[16] * b[33] * b[10] - b[16] * b[5] * b[38] - b[16] * b[6] * b[37] -
      b[16] * b[7] * b[36] + b[16] * b[31] * b[12] + b[14] * b[33] * b[12] -
      b[14] * b[7] * b[38] + b[15] * b[32] * b[12] + b[15] * b[33] * b[11] +
      b[29] * b[5] * b[25] + b[29] * b[6] * b[24] - b[27] * b[20] * b[12] +
      b[27] * b[7] * b[25] - b[28] * b[19] * b[12] - b[28] * b[20] * b[11] +
      b[29] * b[7] * b[23] - b[29] * b[18] * b[12] - b[29] * b[19] * b[11] +
      b[28] * b[6] * b[25] + b[28] * b[7] * b[24] - b[29] * b[20] * b[10] +
      b[2] * b[19] * b[38] - b[1] * b[25] * b[33] + b[2] * b[20] * b[37] -
      b[2] * b[24] * b[33] - b[2] * b[25] * b[32] + b[1] * b[20] * b[38];
  coeffs(9) =
      b[29] * b[7] * b[24] - b[29] * b[20] * b[11] + b[2] * b[20] * b[38] -
      b[2] * b[25] * b[33] - b[28] * b[20] * b[12] + b[28] * b[7] * b[25] -
      b[29] * b[19] * b[12] - b[3] * b[24] * b[33] + b[15] * b[33] * b[12] +
      b[3] * b[19] * b[38] - b[16] * b[6] * b[38] + b[3] * b[20] * b[37] +
      b[16] * b[32] * b[12] + b[29] * b[6] * b[25] - b[16] * b[7] * b[37] -
      b[3] * b[25] * b[32] - b[15] * b[7] * b[38] + b[16] * b[33] * b[11];
  coeffs(10) = -b[29] * b[20] * b[12] + b[29] * b[7] * b[25] +
               b[16] * b[33] * b[12] - b[16] * b[7] * b[38] +
               b[3] * b[20] * b[38] - b[3] * b[25] * b[33];
}
