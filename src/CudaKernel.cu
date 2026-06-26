// Copyright (c) 2026 Magno Ciqueira. All rights reserved.
// SPDX-License-Identifier: LicenseRef-MCVector-Proprietary
// See LICENSE.md in the repository root for source-available terms.

#include "VectorParams.h"

#include <cuda_runtime.h>

__device__ inline float clampf(float v, float lo, float hi) {
  return fminf(fmaxf(v, lo), hi);
}

__device__ inline float3 f3(float x, float y, float z) {
  return make_float3(x, y, z);
}

__device__ inline float3 add3(float3 a, float3 b) {
  return f3(a.x + b.x, a.y + b.y, a.z + b.z);
}

__device__ inline float3 sub3(float3 a, float3 b) {
  return f3(a.x - b.x, a.y - b.y, a.z - b.z);
}

__device__ inline float3 scale3(float3 a, float s) {
  return f3(a.x * s, a.y * s, a.z * s);
}

__device__ inline float pivotFromPreset(int preset) {
  if (preset == 0)
    return 0.414f;
  if (preset == 1)
    return 0.336f;
  if (preset == 2)
    return 0.391f;
  if (preset == 3)
    return 0.278f;
  return 0.336f;
}

__device__ inline float applyBezier(float in, float point02, float point03,
                                    float point04) {
  const float mappedMid = 1.0f + (point03 - 1.0f) * 0.150f;
  const float mappedLow = 1.0f + (point02 - 1.0f) * 0.300f;
  const float mappedHi = 1.0f + (point04 - 1.0f) * 0.300f;
  const float diff = mappedMid - 1.0f;
  const float autoLow = 1.0f + diff * 1.6f;
  const float autoHi = 1.0f + diff * 0.4666f;
  const float finalLow = autoLow * mappedLow;
  const float finalHi = autoHi * mappedHi;
  const float p1 = mappedMid * 0.90f + finalLow * 0.10f;
  const float inv = 1.0f - in;

  return finalLow * inv * inv * inv + 3.0f * p1 * inv * inv * in +
         3.0f * mappedMid * inv * in * in + finalHi * in * in * in;
}

__device__ inline float rgbDirectSatValue(float3 rgb) {
  const float neutral = (rgb.x + rgb.y + rgb.z) / 3.0f;
  const float cx = rgb.x - neutral;
  const float cy = rgb.y - neutral;
  const float cz = rgb.z - neutral;
  const float chromaMag = sqrtf(cx * cx + cy * cy + cz * cz);
  return clampf(chromaMag / (fabsf(neutral) + chromaMag + 1.0e-6f), 0.0f,
                1.0f);
}

__device__ inline float3 applyRgbDirectSat(float3 rgb, float satMult) {
  const float neutral = (rgb.x + rgb.y + rgb.z) / 3.0f;
  return f3(neutral + (rgb.x - neutral) * satMult,
            neutral + (rgb.y - neutral) * satMult,
            neutral + (rgb.z - neutral) * satMult);
}

__device__ inline float3 applyCurvesSaturation(float3 in,
                                               MCVectorParams p) {
  const float satVal = rgbDirectSatValue(in);
  float effGlobalSat = p.satGlobal;
  if (effGlobalSat > 1.0f) {
    effGlobalSat = 1.0f + (effGlobalSat - 1.0f) * 0.5f;
  }

  float satMult =
      applyBezier(satVal, p.satLow, p.satMid, p.satHigh) * effGlobalSat;
  satMult = 1.0f + (satMult - 1.0f) * p.satLumMask;
  return applyRgbDirectSat(in, satMult);
}

__device__ inline float3 applyShadowSplit(float3 in, float pivot,
                                          float strength, float colorMix,
                                          float neutralBlack) {
  float3 out = in;
  if (pivot <= 1.0e-6f)
    return out;

  const float blueStrength = ((1.0f - colorMix) * strength) / 6.0f;
  const float greenStrength = (colorMix * strength) / 6.0f;

  if (in.x <= pivot) {
    const float r = in.x / pivot;
    const float inv = 1.0f - r;
    out.x = ((0.0f - blueStrength * neutralBlack -
              greenStrength * neutralBlack) *
                 inv * inv * inv +
             3.0f * (0.333f - blueStrength * neutralBlack -
                      greenStrength * neutralBlack) *
                 inv * inv * r +
             3.0f * (0.666f - blueStrength - greenStrength) * inv * r * r +
             r * r * r) *
            pivot;
  }

  if (in.y <= pivot) {
    const float g = in.y / pivot;
    const float inv = 1.0f - g;
    out.y = (greenStrength * neutralBlack * inv * inv * inv +
             3.0f * (greenStrength * neutralBlack + 0.333f) * inv * inv * g +
             3.0f * (greenStrength + 0.666f) * inv * g * g + g * g * g) *
            pivot;
  }

  if (in.z <= pivot) {
    const float b = in.z / pivot;
    const float inv = 1.0f - b;
    out.z = (blueStrength * neutralBlack * inv * inv * inv +
             3.0f * (blueStrength * neutralBlack + 0.333f) * inv * inv * b +
             3.0f * (blueStrength + 0.666f) * inv * b * b + b * b * b) *
            pivot;
  }

  return out;
}

__device__ inline float3 applyHighlightSplit(float3 in, float pivot,
                                             float strength, float colorMix,
                                             float neutralWhite) {
  float3 invIn = f3(1.0f - in.x, 1.0f - in.y, 1.0f - in.z);
  const float invPivot = 1.0f - pivot;
  if (invPivot <= 1.0e-6f)
    return in;

  const float redStrength = ((1.0f - colorMix) * strength) / 8.0f;
  const float greenStrength = (colorMix * strength) / 8.0f;
  float3 invOut = invIn;

  if (invIn.x <= invPivot) {
    const float r = invIn.x / invPivot;
    const float inv = 1.0f - r;
    invOut.x = ((1.0f - (neutralWhite * redStrength + 1.0f)) *
                    inv * inv * inv +
                3.0f * (1.0f - (neutralWhite * redStrength + 0.666f)) *
                    inv * inv * r +
                3.0f * (1.0f - (redStrength + 0.333f)) * inv * r * r +
                r * r * r) *
               invPivot;
  }

  if (invIn.y <= invPivot) {
    const float g = invIn.y / invPivot;
    const float inv = 1.0f - g;
    invOut.y = ((1.0f - (neutralWhite * greenStrength + 1.0f)) *
                    inv * inv * inv +
                3.0f * (1.0f - (neutralWhite * greenStrength + 0.666f)) *
                    inv * inv * g +
                3.0f * (1.0f - (greenStrength + 0.333f)) * inv * g * g +
                g * g * g) *
               invPivot;
  }

  if (invIn.z <= invPivot) {
    const float b = invIn.z / invPivot;
    const float inv = 1.0f - b;
    invOut.z = ((1.0f - (1.0f - greenStrength * neutralWhite -
                         redStrength * neutralWhite)) *
                    inv * inv * inv +
                3.0f * (1.0f - (0.666f - greenStrength * neutralWhite -
                                 redStrength * neutralWhite)) *
                    inv * inv * b +
                3.0f * (1.0f - (0.333f - greenStrength - redStrength)) *
                    inv * b * b +
                b * b * b) *
               invPivot;
  }

  return f3(1.0f - invOut.x, 1.0f - invOut.y, 1.0f - invOut.z);
}

__device__ inline float3 applySplitTone(float3 in, MCVectorParams p) {
  float3 clamped = f3(clampf(in.x, 0.0f, 1.0f),
                      clampf(in.y, 0.0f, 1.0f),
                      clampf(in.z, 0.0f, 1.0f));
  const float shadowMix = p.shadowMix * 0.6f;
  const float highlightMix = p.highlightMix * 0.6f;
  const float neutralBlack = 1.0f - p.neutralBlack;
  const float neutralWhite = 1.0f - p.neutralWhite;
  const float pivot = clampf(pivotFromPreset(p.pivotPreset) + p.pivotOffset,
                             0.0f, 1.0f);
  const float pivotWidth = p.pivotWidth * (pivot + 0.001f);
  const float shadowPivot = clampf(pivot - pivotWidth, 0.0f, 1.0f);
  const float highlightPivot = clampf(pivot + pivotWidth, 0.0f, 1.0f);
  const float shadowStrength = p.splitShadow * 2.0f;

  float3 out = applyShadowSplit(clamped, shadowPivot, shadowStrength, shadowMix,
                                neutralBlack);
  return applyHighlightSplit(out, highlightPivot, p.splitHighlight,
                             highlightMix, neutralWhite);
}

__device__ inline float3 applyVector(float3 in, MCVectorParams p) {
  const float3 sat = p.enableSaturation ? applyCurvesSaturation(in, p) : in;
  const float3 split = p.enableSplitTone ? applySplitTone(in, p) : in;
  return add3(in, add3(sub3(sat, in), sub3(split, in)));
}

__device__ inline float3 drawSatCurve(float3 out, int x, int y, int width,
                                      int height, MCVectorParams p) {
  if (!p.enableSaturation || !p.showSatCurve || width <= 0 || height <= 0)
    return out;

  const float xf = (float)x / (float)width;
  const float yf = (float)y / (float)height;
  float effGlobalSat = p.satGlobal;
  if (effGlobalSat > 1.0f) {
    effGlobalSat = 1.0f + (effGlobalSat - 1.0f) * 0.5f;
  }

  const float sMult =
      applyBezier(xf, p.satLow, p.satMid, p.satHigh) * effGlobalSat;
  const float baseCurve = (1.0f + (sMult - 1.0f) * p.satLumMask) * 0.5f;
  const float spacing = 2.0f / (float)height;
  const float thickness = 0.5f;
  const float falloff = 1.0f;
  const float alphaR =
      clampf(1.0f - (fabsf(yf - (baseCurve + spacing)) * height - thickness) /
                         falloff,
             0.0f, 1.0f);
  const float alphaG =
      clampf(1.0f - (fabsf(yf - baseCurve) * height - thickness) / falloff,
             0.0f, 1.0f);
  const float alphaB =
      clampf(1.0f - (fabsf(yf - (baseCurve - spacing)) * height - thickness) /
                         falloff,
             0.0f, 1.0f);
  const float combined = fmaxf(alphaR, fmaxf(alphaG, alphaB));
  return f3(out.x * (1.0f - combined) + alphaR,
            out.y * (1.0f - combined) + alphaG,
            out.z * (1.0f - combined) + alphaB);
}

__device__ inline float3 drawToneCurve(float3 out, int x, int y, int width,
                                       int height, MCVectorParams p) {
  if (!p.enableSplitTone || !p.showToneCurve || width <= 0 || height <= 0)
    return out;

  const float rv = (float)x / (float)width;
  const float screenY = (float)y / (float)height;
  const float3 ramp = applySplitTone(f3(rv, rv, rv), p);
  const float halfThickness = 5.0f / (float)height;

  if (fabsf(ramp.x - screenY) <= halfThickness)
    out.x = 1.0f;
  if (fabsf(ramp.y - screenY) <= halfThickness)
    out.y = 1.0f;
  if (fabsf(ramp.z - screenY) <= halfThickness)
    out.z = 1.0f;
  return out;
}

__global__ void MCVectorKernel(int width, int height, int rowBytes,
                               const float *input, float *output,
                               MCVectorParams params) {
  const int x = blockIdx.x * blockDim.x + threadIdx.x;
  const int y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x >= width || y >= height)
    return;

  const int fpr = rowBytes / (int)sizeof(float);
  const int idx = y * fpr + x * 4;
  const float3 in = f3(input[idx + 0], input[idx + 1], input[idx + 2]);
  float3 out = applyVector(in, params);
  out = drawSatCurve(out, x, y, width, height, params);
  out = drawToneCurve(out, x, y, width, height, params);

  output[idx + 0] = out.x;
  output[idx + 1] = out.y;
  output[idx + 2] = out.z;
  output[idx + 3] = input[idx + 3];
}

extern "C" void RunCudaKernel(void *stream, int width, int height,
                              int rowBytes, const float *input, float *output,
                              MCVectorParams params) {
  cudaStream_t cudaStream = (cudaStream_t)stream;
  dim3 block(16, 16);
  dim3 grid((width + block.x - 1) / block.x,
            (height + block.y - 1) / block.y);
  MCVectorKernel<<<grid, block, 0, cudaStream>>>(width, height, rowBytes, input,
                                                 output, params);
}
