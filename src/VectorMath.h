// Copyright (c) 2026 Magno Ciqueira. All rights reserved.
// SPDX-License-Identifier: LicenseRef-MCVector-Proprietary
// See LICENSE.md in the repository root for source-available terms.

#ifndef MCVECTOR_MATH_H
#define MCVECTOR_MATH_H

#include "VectorParams.h"

#include <algorithm>
#include <cmath>

namespace mcvector {

struct float3 {
  float x;
  float y;
  float z;
};

inline float3 make_float3(float x, float y, float z) { return {x, y, z}; }

inline float clampf(float v, float lo, float hi) {
  return std::min(std::max(v, lo), hi);
}

inline float mixf(float a, float b, float t) { return a + (b - a) * t; }

inline float pivotFromPreset(int preset) {
  switch (preset) {
  case 0:
    return 0.414f; // ACES AP1 / ACEScct
  case 1:
    return 0.336f; // DaVinci Wide Gamut / Intermediate
  case 2:
    return 0.391f; // ARRI Wide Gamut 3 / LogC3
  case 3:
    return 0.278f; // ARRI Wide Gamut 4 / LogC4
  default:
    return 0.336f;
  }
}

inline float applyBezier(float in, float point02, float point03,
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

inline float rgbDirectSatValue(float3 rgb) {
  const float neutral = (rgb.x + rgb.y + rgb.z) / 3.0f;
  const float cx = rgb.x - neutral;
  const float cy = rgb.y - neutral;
  const float cz = rgb.z - neutral;
  const float chromaMag = std::sqrt(cx * cx + cy * cy + cz * cz);
  return clampf(chromaMag / (std::fabs(neutral) + chromaMag + 1.0e-6f), 0.0f,
                1.0f);
}

inline float3 applyRgbDirectSat(float3 rgb, float satMult) {
  const float neutral = (rgb.x + rgb.y + rgb.z) / 3.0f;
  return make_float3(neutral + (rgb.x - neutral) * satMult,
                     neutral + (rgb.y - neutral) * satMult,
                     neutral + (rgb.z - neutral) * satMult);
}

inline float3 applyCurvesSaturation(float3 in, const MCVectorParams &p) {
  const float satVal = rgbDirectSatValue(in);
  float effGlobalSat = p.satGlobal;
  if (effGlobalSat > 1.0f) {
    effGlobalSat = 1.0f + (effGlobalSat - 1.0f) * 0.5f;
  }

  float satMult = applyBezier(satVal, p.satLow, p.satMid, p.satHigh) *
                  effGlobalSat;
  satMult = 1.0f + (satMult - 1.0f) * p.satLumMask;
  return applyRgbDirectSat(in, satMult);
}

inline float3 applyShadowSplit(float3 in, float pivot, float strength,
                               float colorMix, float neutralBlack) {
  float3 out = in;
  if (pivot <= 1.0e-6f) {
    return out;
  }

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

inline float3 applyHighlightSplit(float3 in, float pivot, float strength,
                                  float colorMix, float neutralWhite) {
  float3 out = in;
  float3 invIn = make_float3(1.0f - in.x, 1.0f - in.y, 1.0f - in.z);
  const float invPivot = 1.0f - pivot;
  if (invPivot <= 1.0e-6f) {
    return out;
  }

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

  out.x = 1.0f - invOut.x;
  out.y = 1.0f - invOut.y;
  out.z = 1.0f - invOut.z;
  return out;
}

inline float3 applySplitTone(float3 in, const MCVectorParams &p) {
  float3 clamped = make_float3(clampf(in.x, 0.0f, 1.0f),
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
  out = applyHighlightSplit(out, highlightPivot, p.splitHighlight,
                            highlightMix, neutralWhite);
  return out;
}

inline float3 applyVector(float3 in, const MCVectorParams &p) {
  const float3 sat = p.enableSaturation ? applyCurvesSaturation(in, p) : in;
  const float3 split = p.enableSplitTone ? applySplitTone(in, p) : in;
  return make_float3(in.x + (sat.x - in.x) + (split.x - in.x),
                     in.y + (sat.y - in.y) + (split.y - in.y),
                     in.z + (sat.z - in.z) + (split.z - in.z));
}

inline float3 drawSatCurve(float3 out, int x, int y, int width, int height,
                           const MCVectorParams &p) {
  if (!p.enableSaturation || !p.showSatCurve || width <= 0 || height <= 0) {
    return out;
  }

  const float xf = static_cast<float>(x) / static_cast<float>(width);
  const float yf = static_cast<float>(y) / static_cast<float>(height);

  float effGlobalSat = p.satGlobal;
  if (effGlobalSat > 1.0f) {
    effGlobalSat = 1.0f + (effGlobalSat - 1.0f) * 0.5f;
  }

  const float sMult = applyBezier(xf, p.satLow, p.satMid, p.satHigh) *
                      effGlobalSat;
  const float baseCurve = (1.0f + (sMult - 1.0f) * p.satLumMask) * 0.5f;
  const float spacing = 2.0f / static_cast<float>(height);
  const float thickness = 0.5f;
  const float falloff = 1.0f;

  const float distR = std::fabs(yf - (baseCurve + spacing)) * height;
  const float distG = std::fabs(yf - baseCurve) * height;
  const float distB = std::fabs(yf - (baseCurve - spacing)) * height;
  const float alphaR = clampf(1.0f - (distR - thickness) / falloff, 0.0f, 1.0f);
  const float alphaG = clampf(1.0f - (distG - thickness) / falloff, 0.0f, 1.0f);
  const float alphaB = clampf(1.0f - (distB - thickness) / falloff, 0.0f, 1.0f);
  const float combined = std::max(alphaR, std::max(alphaG, alphaB));

  out.x = out.x * (1.0f - combined) + alphaR;
  out.y = out.y * (1.0f - combined) + alphaG;
  out.z = out.z * (1.0f - combined) + alphaB;
  return out;
}

inline float3 drawToneCurve(float3 out, int x, int y, int width, int height,
                            const MCVectorParams &p) {
  if (!p.enableSplitTone || !p.showToneCurve || width <= 0 || height <= 0) {
    return out;
  }

  const float rampValue = static_cast<float>(x) / static_cast<float>(width);
  const float screenY = static_cast<float>(y) / static_cast<float>(height);
  float3 ramp = make_float3(rampValue, rampValue, rampValue);
  ramp = applySplitTone(ramp, p);

  const float halfThickness = 5.0f / static_cast<float>(height);
  const float overlayR = std::fabs(ramp.x - screenY) <= halfThickness ? 1.0f : 0.0f;
  const float overlayG = std::fabs(ramp.y - screenY) <= halfThickness ? 1.0f : 0.0f;
  const float overlayB = std::fabs(ramp.z - screenY) <= halfThickness ? 1.0f : 0.0f;

  out.x = overlayR > 0.0f ? overlayR : out.x;
  out.y = overlayG > 0.0f ? overlayG : out.y;
  out.z = overlayB > 0.0f ? overlayB : out.z;
  return out;
}

} // namespace mcvector

#endif // MCVECTOR_MATH_H
