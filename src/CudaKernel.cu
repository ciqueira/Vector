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

__device__ inline float splitCurveOffset(float curveBias) {
  return clampf(curveBias, -1.0f, 1.0f) * 0.25f;
}

__device__ inline float splitCurveLow(float curveBias) {
  return clampf(0.333f + splitCurveOffset(curveBias), 0.05f, 0.95f);
}

__device__ inline float splitCurveHigh(float curveBias) {
  return clampf(0.666f + splitCurveOffset(curveBias), 0.05f, 0.95f);
}

__device__ inline float bezierY(float t, float p1, float p2) {
  const float inv = 1.0f - t;
  return 3.0f * p1 * inv * inv * t + 3.0f * p2 * inv * t * t +
         t * t * t;
}

__device__ inline float smoothBell(float x, float center) {
  if (x <= 0.0f || x >= 1.0f)
    return 0.0f;

  if (x <= center) {
    const float t = clampf(x / center, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  const float t = clampf((1.0f - x) / (1.0f - center), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

__device__ inline float shadowBellyCenterValue(float bellyCenter) {
  const float position = clampf(bellyCenter, -1.0f, 1.0f);
  return clampf(0.666f + position * 0.55f, 0.08f, 0.92f);
}

__device__ inline float shadowBiasedBezierY(float x, float curveBias,
                                            float bellyCenter) {
  const float offset = splitCurveOffset(curveBias);
  if (fabsf(offset) <= 1.0e-6f)
    return x;

  const float biased = bezierY(x, 0.333f + offset, 0.666f + offset);
  const float center = shadowBellyCenterValue(bellyCenter);
  const float focus = smoothBell(x, center);
  return clampf(x + (biased - x) * focus, 0.0f, 1.0f);
}

__device__ inline float shadowBellyEnvelope(float x, float bellyCenter) {
  const float position = clampf(bellyCenter, -1.0f, 1.0f);
  if (fabsf(position) <= 1.0e-6f)
    return 3.0f * (1.0f - x) * x * x;

  const float center = shadowBellyCenterValue(bellyCenter);
  return (4.0f / 9.0f) * smoothBell(x, center);
}

__device__ inline float shadowBlackEnvelopeBase(float x) {
  const float inv = 1.0f - x;
  return inv * inv * inv + 3.0f * inv * inv * x;
}

__device__ inline float shadowBlackEnvelope(float x, float bellyCenter) {
  const float position = clampf(bellyCenter, -1.0f, 1.0f);
  if (fabsf(position) <= 1.0e-6f)
    return shadowBlackEnvelopeBase(x);

  const float center = shadowBellyCenterValue(bellyCenter);
  const float defaultCenter = 0.666f;
  const float mapped =
      x <= center
          ? x * (defaultCenter / center)
          : defaultCenter +
                (x - center) * ((1.0f - defaultCenter) / (1.0f - center));
  return shadowBlackEnvelopeBase(clampf(mapped, 0.0f, 1.0f));
}

__device__ inline float3 applyShadowSplit(float3 in, float pivot,
                                          float strength, float colorMix,
                                          float neutralBlack,
                                          float curveBias,
                                          float redBellyCenter,
                                          float greenBellyCenter,
                                          float blueBellyCenter) {
  float3 out = in;
  if (pivot <= 1.0e-6f)
    return out;

  const float blueStrength = ((1.0f - colorMix) * strength) / 6.0f;
  const float greenStrength = (colorMix * strength) / 6.0f;

  if (in.x <= pivot) {
    const float r = in.x / pivot;
    const float belly = shadowBellyEnvelope(r, redBellyCenter);
    const float black = shadowBlackEnvelope(r, redBellyCenter) * neutralBlack;
    const float base = shadowBiasedBezierY(r, curveBias, redBellyCenter);
    out.x = (base - (blueStrength + greenStrength) * (black + belly)) *
            pivot;
  }

  if (in.y <= pivot) {
    const float g = in.y / pivot;
    const float belly = shadowBellyEnvelope(g, greenBellyCenter);
    const float black =
        shadowBlackEnvelope(g, greenBellyCenter) * neutralBlack;
    const float base = shadowBiasedBezierY(g, curveBias, greenBellyCenter);
    out.y = (base + greenStrength * (black + belly)) *
            pivot;
  }

  if (in.z <= pivot) {
    const float b = in.z / pivot;
    const float belly = shadowBellyEnvelope(b, blueBellyCenter);
    const float black = shadowBlackEnvelope(b, blueBellyCenter) * neutralBlack;
    const float base = shadowBiasedBezierY(b, curveBias, blueBellyCenter);
    out.z = (base + blueStrength * (black + belly)) *
            pivot;
  }

  return out;
}

__device__ inline float3 applyHighlightSplit(float3 in, float pivot,
                                             float strength, float colorMix,
                                             float neutralWhite,
                                             float curveBias) {
  float3 invIn = f3(1.0f - in.x, 1.0f - in.y, 1.0f - in.z);
  const float invPivot = 1.0f - pivot;
  if (invPivot <= 1.0e-6f)
    return in;

  const float redStrength = ((1.0f - colorMix) * strength) / 8.0f;
  const float greenStrength = (colorMix * strength) / 8.0f;
  const float curveLow = splitCurveLow(curveBias);
  const float curveHigh = splitCurveHigh(curveBias);
  float3 invOut = invIn;

  if (invIn.x <= invPivot) {
    const float r = invIn.x / invPivot;
    const float inv = 1.0f - r;
    invOut.x = ((1.0f - (neutralWhite * redStrength + 1.0f)) *
                    inv * inv * inv +
                3.0f * (1.0f - (neutralWhite * redStrength + curveHigh)) *
                    inv * inv * r +
                3.0f * (1.0f - (redStrength + curveLow)) * inv * r * r +
                r * r * r) *
               invPivot;
  }

  if (invIn.y <= invPivot) {
    const float g = invIn.y / invPivot;
    const float inv = 1.0f - g;
    invOut.y = ((1.0f - (neutralWhite * greenStrength + 1.0f)) *
                    inv * inv * inv +
                3.0f * (1.0f - (neutralWhite * greenStrength + curveHigh)) *
                    inv * inv * g +
                3.0f * (1.0f - (greenStrength + curveLow)) * inv * g * g +
                g * g * g) *
               invPivot;
  }

  if (invIn.z <= invPivot) {
    const float b = invIn.z / invPivot;
    const float inv = 1.0f - b;
    invOut.z = ((1.0f - (1.0f - greenStrength * neutralWhite -
                         redStrength * neutralWhite)) *
                    inv * inv * inv +
                3.0f * (1.0f - (curveHigh - greenStrength * neutralWhite -
                                 redStrength * neutralWhite)) *
                    inv * inv * b +
                3.0f * (1.0f - (curveLow - greenStrength - redStrength)) *
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
  const float shadowStrength = p.splitShadow * 2.5f;

  float3 out = applyShadowSplit(clamped, shadowPivot, shadowStrength, shadowMix,
                                neutralBlack, p.shadowCurveBias,
                                p.shadowRedBellyCenter,
                                p.shadowGreenBellyCenter,
                                p.shadowBlueBellyCenter);
  return applyHighlightSplit(out, highlightPivot, p.splitHighlight,
                             highlightMix, neutralWhite, p.highlightCurveBias);
}

__device__ inline float3 applyVector(float3 in, MCVectorParams p) {
  const float3 sat = p.enableSaturation ? applyCurvesSaturation(in, p) : in;
  const float3 split = p.enableSplitTone ? applySplitTone(in, p) : in;
  return add3(in, add3(sub3(sat, in), sub3(split, in)));
}

__device__ inline float3 normalizePatchDelta(float3 delta) {
  const float minv = fminf(delta.x, fminf(delta.y, delta.z));
  float3 color = f3(delta.x - minv, delta.y - minv, delta.z - minv);
  const float maxv = fmaxf(color.x, fmaxf(color.y, color.z));

  if (maxv <= 0.000001f)
    return f3(0.18f, 0.18f, 0.18f);

  return f3(color.x / maxv, color.y / maxv, color.z / maxv);
}

__device__ inline float encodeDavinciIntermediate(float x) {
  const float a = 0.0075f;
  const float b = 7.0f;
  const float c = 0.07329248f;
  const float m = 10.44426855f;
  const float linCut = 0.00262409f;
  return x > linCut ? (log2f(x + a) + b) * c : x * m;
}

__device__ inline float encodeAcescct(float x) {
  const float a = 10.5402377416545f;
  const float b = 0.0729055341958355f;
  const float c = 9.72f;
  const float d = 17.52f;
  const float e = 0.0078125f;
  return x <= e ? a * x + b : (log2f(x) + c) / d;
}

__device__ inline float encodeLogC3(float x) {
  const float cut = 0.010591f;
  const float a = 5.555556f;
  const float b = 0.052272f;
  const float c = 0.247190f;
  const float d = 0.385537f;
  const float e = 5.367655f;
  const float f = 0.092809f;
  return x > cut ? c * log10f(a * x + b) + d : e * x + f;
}

__device__ inline float encodeLogC4(float x) {
  const float a = (powf(2.0f, 18.0f) - 16.0f) / 117.45f;
  const float b = (1023.0f - 95.0f) / 1023.0f;
  const float c = 95.0f / 1023.0f;
  const float s =
      (7.0f * logf(2.0f) * powf(2.0f, 7.0f - 14.0f * c / b)) /
      (a * b);
  const float t = (powf(2.0f, 14.0f * (-c / b) + 6.0f) - 64.0f) / a;
  return x < t ? (x - t) / s
               : (log2f(a * x + 64.0f) - 6.0f) / 14.0f * b + c;
}

__device__ inline float3 encodePatchTransfer(float3 color, int pivotPreset) {
  color.x = fmaxf(0.0f, color.x);
  color.y = fmaxf(0.0f, color.y);
  color.z = fmaxf(0.0f, color.z);

  if (pivotPreset == 0)
    return f3(encodeAcescct(color.x), encodeAcescct(color.y),
              encodeAcescct(color.z));
  if (pivotPreset == 1)
    return f3(encodeDavinciIntermediate(color.x),
              encodeDavinciIntermediate(color.y),
              encodeDavinciIntermediate(color.z));
  if (pivotPreset == 2)
    return f3(encodeLogC3(color.x), encodeLogC3(color.y),
              encodeLogC3(color.z));
  if (pivotPreset == 3)
    return f3(encodeLogC4(color.x), encodeLogC4(color.y),
              encodeLogC4(color.z));
  return color;
}

__device__ inline float3 getShadowPatchColor(float pivot, float colorMix,
                                             float neutralBlack,
                                             float curveBias,
                                             float redBellyCenter,
                                             float greenBellyCenter,
                                             float blueBellyCenter) {
  if (pivot <= 0.0f)
    return f3(0.18f, 0.18f, 0.18f);

  const float sample = clampf(pivot * 0.5f, 0.0f, 1.0f);
  const float3 base = f3(sample, sample, sample);
  const float3 toned =
      applyShadowSplit(base, pivot, 1.0f, colorMix, neutralBlack, curveBias,
                       redBellyCenter, greenBellyCenter, blueBellyCenter);
  return normalizePatchDelta(f3(toned.x - base.x, toned.y - base.y,
                                toned.z - base.z));
}

__device__ inline float3 getHighlightPatchColor(float pivot, float colorMix,
                                                float neutralWhite,
                                                float curveBias) {
  if (pivot >= 1.0f)
    return f3(0.82f, 0.82f, 0.82f);

  const float sample = clampf(pivot + ((1.0f - pivot) * 0.5f), 0.0f, 1.0f);
  const float3 base = f3(sample, sample, sample);
  const float3 toned = applyHighlightSplit(base, pivot, 1.0f, colorMix,
                                           neutralWhite, curveBias);
  return normalizePatchDelta(f3(toned.x - base.x, toned.y - base.y,
                                toned.z - base.z));
}

__device__ inline float3 drawColorPatches(float3 out, int x, int y, int width,
                                          int height, float3 shadowColor,
                                          float3 highlightColor) {
  const float minDim = fminf((float)width, (float)height);
  const float patch = fmaxf(140.0f, fminf(360.0f, minDim * 0.2315f));
  const float gap = fmaxf(5.0f, patch * 0.12f);
  const float margin = fmaxf(12.0f, minDim * 0.025f);
  const float left = (float)width - margin - patch;
  const float shadowTop = (float)height - margin - patch;
  const float highlightTop = shadowTop - gap - patch;
  const float xf = (float)x;
  const float yf = (float)(height - 1 - y);

  if (xf >= left && xf <= left + patch && yf >= highlightTop &&
      yf <= highlightTop + patch)
    return highlightColor;

  if (xf >= left && xf <= left + patch && yf >= shadowTop &&
      yf <= shadowTop + patch)
    return shadowColor;

  return out;
}

__device__ inline float lineAlpha(float curveY, float screenY,
                                  float halfThickness, float falloff) {
  const float dist = fabsf(curveY - screenY);
  return clampf(1.0f - (dist - halfThickness) / falloff, 0.0f, 1.0f);
}

__device__ inline float3 overlayRgbLine(float3 current, float curveY,
                                        float screenY, float3 color,
                                        float halfThickness, float falloff) {
  const float alpha = lineAlpha(curveY, screenY, halfThickness, falloff);
  return f3(current.x * (1.0f - alpha) + color.x * alpha,
            current.y * (1.0f - alpha) + color.y * alpha,
            current.z * (1.0f - alpha) + color.z * alpha);
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
  const float halfThickness = 5.0f / (float)height;
  const float spacing = halfThickness * 2.0f;
  const float falloff = 1.0f / (float)height;

  out = overlayRgbLine(out, baseCurve + spacing, yf, f3(1.0f, 0.0f, 0.0f),
                       halfThickness, falloff);
  out = overlayRgbLine(out, baseCurve, yf, f3(0.0f, 1.0f, 0.0f),
                       halfThickness, falloff);
  out = overlayRgbLine(out, baseCurve - spacing, yf, f3(0.0f, 0.0f, 1.0f),
                       halfThickness, falloff);
  return out;
}

__device__ inline float3 drawToneCurve(float3 out, int x, int y, int width,
                                       int height, MCVectorParams p) {
  if (!p.enableSplitTone || !p.showToneCurve || width <= 0 || height <= 0)
    return out;

  const float rv = (float)x / (float)width;
  const float screenY = (float)y / (float)height;
  const float3 ramp = applySplitTone(f3(rv, rv, rv), p);
  const float halfThickness = 5.0f / (float)height;
  const float falloff = 1.0f / (float)height;

  out = overlayRgbLine(out, ramp.x, screenY, f3(1.0f, 0.0f, 0.0f),
                       halfThickness, falloff);
  out = overlayRgbLine(out, ramp.y, screenY, f3(0.0f, 1.0f, 0.0f),
                       halfThickness, falloff);
  out = overlayRgbLine(out, ramp.z, screenY, f3(0.0f, 0.0f, 1.0f),
                       halfThickness, falloff);

  const float shadowMix = p.shadowMix * 0.6f;
  const float highlightMix = p.highlightMix * 0.6f;
  const float neutralBlack = 1.0f - p.neutralBlack;
  const float neutralWhite = 1.0f - p.neutralWhite;
  const float pivot = clampf(pivotFromPreset(p.pivotPreset) + p.pivotOffset,
                             0.0f, 1.0f);
  const float pivotWidth = p.pivotWidth * (pivot + 0.001f);
  const float shadowPivot = clampf(pivot - pivotWidth, 0.0f, 1.0f);
  const float highlightPivot = clampf(pivot + pivotWidth, 0.0f, 1.0f);
  const float3 shadowColor =
      encodePatchTransfer(getShadowPatchColor(shadowPivot, shadowMix,
                                              neutralBlack,
                                              p.shadowCurveBias,
                                              p.shadowRedBellyCenter,
                                              p.shadowGreenBellyCenter,
                                              p.shadowBlueBellyCenter),
                          p.pivotPreset);
  const float3 highlightColor =
      encodePatchTransfer(getHighlightPatchColor(highlightPivot, highlightMix,
                                                 neutralWhite,
                                                 p.highlightCurveBias),
                          p.pivotPreset);
  return drawColorPatches(out, x, y, width, height, shadowColor,
                          highlightColor);
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
