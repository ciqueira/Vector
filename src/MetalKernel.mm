// Copyright (c) 2026 Magno Ciqueira. All rights reserved.
// SPDX-License-Identifier: LicenseRef-MCVector-Proprietary
// See LICENSE.md in the repository root for source-available terms.

#include "VectorParams.h"

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdio>
#include <mutex>
#include <unordered_map>

static const char *kMetalSource = R"METAL(
#include <metal_stdlib>
using namespace metal;

struct MCVectorParams {
  int pivotPreset;
  int enableSaturation;
  int enableZoneSaturation;
  int enableSplitTone;
  int showSatCurve;
  int showToneCurve;
  int showZoneCurve;
  int _pad2;

  float satLow;
  float satMid;
  float satHigh;
  float satGlobal;
  float satLumMask;
  float zoneShadowSaturation;
  float zoneHighlightSaturation;
  float zonePivot;
  float zoneSoftness;

  float splitShadow;
  float shadowMix;
  float neutralBlack;
  float splitHighlight;
  float highlightMix;
  float neutralWhite;
  float pivotWidth;
  float pivotOffset;
  float shadowCurveBias;
  float highlightCurveBias;
  float shadowRedBellyCenter;
  float shadowGreenBellyCenter;
  float shadowBlueBellyCenter;
};

float pivotFromPreset(int preset) {
  if (preset == 0) return 0.414f;
  if (preset == 1) return 0.336f;
  if (preset == 2) return 0.391f;
  if (preset == 3) return 0.278f;
  return 0.336f;
}

float applyBezier(float in, float point02, float point03, float point04) {
  float mappedMid = 1.0f + (point03 - 1.0f) * 0.150f;
  float mappedLow = 1.0f + (point02 - 1.0f) * 0.300f;
  float mappedHi = 1.0f + (point04 - 1.0f) * 0.300f;
  float diff = mappedMid - 1.0f;
  float autoLow = 1.0f + diff * 1.6f;
  float autoHi = 1.0f + diff * 0.4666f;
  float finalLow = autoLow * mappedLow;
  float finalHi = autoHi * mappedHi;
  float p1 = mappedMid * 0.90f + finalLow * 0.10f;
  float inv = 1.0f - in;
  return finalLow * inv * inv * inv + 3.0f * p1 * inv * inv * in +
         3.0f * mappedMid * inv * in * in + finalHi * in * in * in;
}

float rgbDirectSatValue(float3 rgb) {
  float neutral = (rgb.x + rgb.y + rgb.z) / 3.0f;
  float3 chroma = rgb - float3(neutral);
  float chromaMag = length(chroma);
  return clamp(chromaMag / (abs(neutral) + chromaMag + 1.0e-6f), 0.0f, 1.0f);
}

float3 applyRgbDirectSat(float3 rgb, float satMult) {
  float neutral = (rgb.x + rgb.y + rgb.z) / 3.0f;
  return float3(neutral) + (rgb - float3(neutral)) * satMult;
}

float3 applyCurvesSaturation(float3 in, constant MCVectorParams &p) {
  float satVal = rgbDirectSatValue(in);
  float effGlobalSat = p.satGlobal;
  if (effGlobalSat > 1.0f) {
    effGlobalSat = 1.0f + (effGlobalSat - 1.0f) * 0.5f;
  }
  float satMult = applyBezier(satVal, p.satLow, p.satMid, p.satHigh) *
                  effGlobalSat;
  satMult = 1.0f + (satMult - 1.0f) * p.satLumMask;
  return applyRgbDirectSat(in, satMult);
}

float zoneToneValue(float3 rgb) {
  float tone = max(rgb.x, max(rgb.y, rgb.z));
  return pow(clamp(tone, 0.0f, 1.0f), 0.4101205819200422f);
}

float zoneMask(float x, float pivot, float softness, bool highlights) {
  float width = mix(0.015f, 0.35f, clamp(softness, 0.0f, 1.0f));
  float arg = (x - pivot) / width;
  if (highlights) {
    arg = -arg;
  }
  arg = clamp(arg, -60.0f, 60.0f);
  return 1.0f / (1.0f + exp(arg));
}

float zoneSatMultiplier(float x, constant MCVectorParams &p) {
  float pivot = pow(clamp(p.zonePivot, 0.0f, 1.0f), 0.4101205819200422f);
  float zone = clamp(p.zoneShadowSaturation, -1.0f, 1.0f);
  float strength = clamp(p.zoneHighlightSaturation, -1.0f, 1.0f);
  float shadows = zoneMask(x, pivot, p.zoneSoftness, false);
  float highlights = zoneMask(x, pivot, p.zoneSoftness, true);
  float shadowFocus = zone <= 0.0f ? 1.0f : 1.0f - zone;
  float highlightFocus = zone >= 0.0f ? 1.0f : 1.0f + zone;
  return max(0.0f, 1.0f + strength * highlights * highlightFocus -
                       strength * shadows * shadowFocus);
}

float3 applyZoneSaturation(float3 in, constant MCVectorParams &p) {
  float x = zoneToneValue(in);
  return applyRgbDirectSat(in, zoneSatMultiplier(x, p));
}

float splitCurveOffset(float curveBias) {
  return clamp(curveBias, -1.0f, 1.0f) * 0.25f;
}

float splitCurveLow(float curveBias) {
  return clamp(0.333f + splitCurveOffset(curveBias), 0.05f, 0.95f);
}

float splitCurveHigh(float curveBias) {
  return clamp(0.666f + splitCurveOffset(curveBias), 0.05f, 0.95f);
}

float bezierY(float t, float p1, float p2) {
  float inv = 1.0f - t;
  return 3.0f * p1 * inv * inv * t + 3.0f * p2 * inv * t * t +
         t * t * t;
}

float smoothBell(float x, float center) {
  if (x <= 0.0f || x >= 1.0f) return 0.0f;

  if (x <= center) {
    float t = clamp(x / center, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
  }

  float t = clamp((1.0f - x) / (1.0f - center), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float shadowBellyCenterValue(float bellyCenter) {
  float position = clamp(bellyCenter, -1.0f, 1.0f);
  return clamp(0.666f + position * 0.55f, 0.08f, 0.92f);
}

float shadowBiasedBezierY(float x, float curveBias, float bellyCenter) {
  float offset = splitCurveOffset(curveBias);
  if (abs(offset) <= 1.0e-6f) return x;

  float biased = bezierY(x, 0.333f + offset, 0.666f + offset);
  float center = shadowBellyCenterValue(bellyCenter);
  float focus = smoothBell(x, center);
  return clamp(x + (biased - x) * focus, 0.0f, 1.0f);
}

float shadowBellyEnvelope(float x, float bellyCenter) {
  float position = clamp(bellyCenter, -1.0f, 1.0f);
  if (abs(position) <= 1.0e-6f) {
    return 3.0f * (1.0f - x) * x * x;
  }

  float center = shadowBellyCenterValue(bellyCenter);
  return (4.0f / 9.0f) * smoothBell(x, center);
}

float shadowBlackEnvelopeBase(float x) {
  float inv = 1.0f - x;
  return inv * inv * inv + 3.0f * inv * inv * x;
}

float shadowBlackEnvelope(float x, float bellyCenter) {
  float position = clamp(bellyCenter, -1.0f, 1.0f);
  if (abs(position) <= 1.0e-6f) {
    return shadowBlackEnvelopeBase(x);
  }

  float center = shadowBellyCenterValue(bellyCenter);
  const float defaultCenter = 0.666f;
  float mapped = x <= center
                     ? x * (defaultCenter / center)
                     : defaultCenter +
                           (x - center) * ((1.0f - defaultCenter) /
                                           (1.0f - center));
  return shadowBlackEnvelopeBase(clamp(mapped, 0.0f, 1.0f));
}

float3 applyShadowSplit(float3 in, float pivot, float strength, float colorMix,
                        float neutralBlack, float curveBias,
                        float redBellyCenter, float greenBellyCenter,
                        float blueBellyCenter) {
  float3 out = in;
  if (pivot <= 1.0e-6f) return out;

  float blueStrength = ((1.0f - colorMix) * strength) / 6.0f;
  float greenStrength = (colorMix * strength) / 6.0f;

  if (in.x <= pivot) {
    float r = in.x / pivot;
    float belly = shadowBellyEnvelope(r, redBellyCenter);
    float black = shadowBlackEnvelope(r, redBellyCenter) * neutralBlack;
    float base = shadowBiasedBezierY(r, curveBias, redBellyCenter);
    out.x = (base - (blueStrength + greenStrength) * (black + belly)) *
            pivot;
  }

  if (in.y <= pivot) {
    float g = in.y / pivot;
    float belly = shadowBellyEnvelope(g, greenBellyCenter);
    float black = shadowBlackEnvelope(g, greenBellyCenter) * neutralBlack;
    float base = shadowBiasedBezierY(g, curveBias, greenBellyCenter);
    out.y = (base + greenStrength * (black + belly)) * pivot;
  }

  if (in.z <= pivot) {
    float b = in.z / pivot;
    float belly = shadowBellyEnvelope(b, blueBellyCenter);
    float black = shadowBlackEnvelope(b, blueBellyCenter) * neutralBlack;
    float base = shadowBiasedBezierY(b, curveBias, blueBellyCenter);
    out.z = (base + blueStrength * (black + belly)) * pivot;
  }
  return out;
}

float3 applyHighlightSplit(float3 in, float pivot, float strength,
                           float colorMix, float neutralWhite,
                           float curveBias) {
  float3 invIn = 1.0f - in;
  float invPivot = 1.0f - pivot;
  if (invPivot <= 1.0e-6f) return in;

  float redStrength = ((1.0f - colorMix) * strength) / 8.0f;
  float greenStrength = (colorMix * strength) / 8.0f;
  float curveLow = splitCurveLow(curveBias);
  float curveHigh = splitCurveHigh(curveBias);
  float3 invOut = invIn;

  if (invIn.x <= invPivot) {
    float r = invIn.x / invPivot;
    float inv = 1.0f - r;
    invOut.x = ((1.0f - (neutralWhite * redStrength + 1.0f)) *
                    inv * inv * inv +
                3.0f * (1.0f - (neutralWhite * redStrength + curveHigh)) *
                    inv * inv * r +
                3.0f * (1.0f - (redStrength + curveLow)) * inv * r * r +
                r * r * r) * invPivot;
  }

  if (invIn.y <= invPivot) {
    float g = invIn.y / invPivot;
    float inv = 1.0f - g;
    invOut.y = ((1.0f - (neutralWhite * greenStrength + 1.0f)) *
                    inv * inv * inv +
                3.0f * (1.0f - (neutralWhite * greenStrength + curveHigh)) *
                    inv * inv * g +
                3.0f * (1.0f - (greenStrength + curveLow)) * inv * g * g +
                g * g * g) * invPivot;
  }

  if (invIn.z <= invPivot) {
    float b = invIn.z / invPivot;
    float inv = 1.0f - b;
    invOut.z = ((1.0f - (1.0f - greenStrength * neutralWhite -
                         redStrength * neutralWhite)) *
                    inv * inv * inv +
                3.0f * (1.0f - (curveHigh - greenStrength * neutralWhite -
                                 redStrength * neutralWhite)) *
                    inv * inv * b +
                3.0f * (1.0f - (curveLow - greenStrength - redStrength)) *
                    inv * b * b +
                b * b * b) * invPivot;
  }

  return 1.0f - invOut;
}

float3 applySplitTone(float3 in, constant MCVectorParams &p) {
  float3 clamped = clamp(in, float3(0.0f), float3(1.0f));
  float shadowMix = p.shadowMix * 0.6f;
  float highlightMix = p.highlightMix * 0.6f;
  float neutralBlack = 1.0f - p.neutralBlack;
  float neutralWhite = 1.0f - p.neutralWhite;
  float pivot = clamp(pivotFromPreset(p.pivotPreset) + p.pivotOffset, 0.0f, 1.0f);
  float pivotWidth = p.pivotWidth * (pivot + 0.001f);
  float shadowPivot = clamp(pivot - pivotWidth, 0.0f, 1.0f);
  float highlightPivot = clamp(pivot + pivotWidth, 0.0f, 1.0f);
  float shadowStrength = p.splitShadow * 2.5f;

  float3 out = applyShadowSplit(clamped, shadowPivot, shadowStrength, shadowMix,
                                neutralBlack, p.shadowCurveBias,
                                p.shadowRedBellyCenter,
                                p.shadowGreenBellyCenter,
                                p.shadowBlueBellyCenter);
  return applyHighlightSplit(out, highlightPivot, p.splitHighlight,
                             highlightMix, neutralWhite,
                             p.highlightCurveBias);
}

float3 applyVector(float3 in, constant MCVectorParams &p) {
  float3 split = p.enableSplitTone != 0 ? applySplitTone(in, p) : in;
  float3 sat = p.enableSaturation != 0 ? applyCurvesSaturation(in, p) : in;
  float3 zone =
      p.enableZoneSaturation != 0 ? applyZoneSaturation(in, p) : in;
  return in + (split - in) + (sat - in) + (zone - in);
}

float3 normalizePatchDelta(float3 delta) {
  float minv = min(delta.x, min(delta.y, delta.z));
  float3 color = delta - float3(minv);
  float maxv = max(color.x, max(color.y, color.z));

  if (maxv <= 0.000001f) return float3(0.18f);

  return color / maxv;
}

float encodeDavinciIntermediate(float x) {
  float a = 0.0075f;
  float b = 7.0f;
  float c = 0.07329248f;
  float m = 10.44426855f;
  float linCut = 0.00262409f;
  return x > linCut ? (log2(x + a) + b) * c : x * m;
}

float encodeAcescct(float x) {
  float a = 10.5402377416545f;
  float b = 0.0729055341958355f;
  float c = 9.72f;
  float d = 17.52f;
  float e = 0.0078125f;
  return x <= e ? a * x + b : (log2(x) + c) / d;
}

float encodeLogC3(float x) {
  float cut = 0.010591f;
  float a = 5.555556f;
  float b = 0.052272f;
  float c = 0.247190f;
  float d = 0.385537f;
  float e = 5.367655f;
  float f = 0.092809f;
  return x > cut ? c * log10(a * x + b) + d : e * x + f;
}

float encodeLogC4(float x) {
  float a = (pow(2.0f, 18.0f) - 16.0f) / 117.45f;
  float b = (1023.0f - 95.0f) / 1023.0f;
  float c = 95.0f / 1023.0f;
  float s = (7.0f * log(2.0f) * pow(2.0f, 7.0f - 14.0f * c / b)) / (a * b);
  float t = (pow(2.0f, 14.0f * (-c / b) + 6.0f) - 64.0f) / a;
  return x < t ? (x - t) / s
               : (log2(a * x + 64.0f) - 6.0f) / 14.0f * b + c;
}

float3 encodePatchTransfer(float3 color, int pivotPreset) {
  color = max(float3(0.0f), color);

  if (pivotPreset == 0) {
    return float3(encodeAcescct(color.x), encodeAcescct(color.y),
                  encodeAcescct(color.z));
  }
  if (pivotPreset == 1) {
    return float3(encodeDavinciIntermediate(color.x),
                  encodeDavinciIntermediate(color.y),
                  encodeDavinciIntermediate(color.z));
  }
  if (pivotPreset == 2) {
    return float3(encodeLogC3(color.x), encodeLogC3(color.y),
                  encodeLogC3(color.z));
  }
  if (pivotPreset == 3) {
    return float3(encodeLogC4(color.x), encodeLogC4(color.y),
                  encodeLogC4(color.z));
  }
  return color;
}

float3 getShadowPatchColor(float pivot, float colorMix, float neutralBlack,
                           float curveBias, float redBellyCenter,
                           float greenBellyCenter, float blueBellyCenter) {
  if (pivot <= 0.0f) return float3(0.18f);

  float sample = clamp(pivot * 0.5f, 0.0f, 1.0f);
  float3 base = float3(sample);
  float3 toned =
      applyShadowSplit(base, pivot, 1.0f, colorMix, neutralBlack,
                       curveBias, redBellyCenter, greenBellyCenter,
                       blueBellyCenter);
  return normalizePatchDelta(toned - base);
}

float3 getHighlightPatchColor(float pivot, float colorMix, float neutralWhite,
                              float curveBias) {
  if (pivot >= 1.0f) return float3(0.82f);

  float sample = clamp(pivot + ((1.0f - pivot) * 0.5f), 0.0f, 1.0f);
  float3 base = float3(sample);
  float3 toned = applyHighlightSplit(base, pivot, 1.0f, colorMix,
                                     neutralWhite, curveBias);
  return normalizePatchDelta(toned - base);
}

float3 drawColorPatches(float3 out, uint x, uint y, uint width, uint height,
                        float3 shadowColor, float3 highlightColor) {
  float minDim = min(float(width), float(height));
  float patch = max(140.0f, min(360.0f, minDim * 0.2315f));
  float gap = max(5.0f, patch * 0.12f);
  float margin = max(12.0f, minDim * 0.025f);
  float left = float(width) - margin - patch;
  float shadowTop = float(height) - margin - patch;
  float highlightTop = shadowTop - gap - patch;
  float xf = float(x);
  float yf = float(height - 1 - y);

  if (xf >= left && xf <= left + patch && yf >= highlightTop &&
      yf <= highlightTop + patch) return highlightColor;

  if (xf >= left && xf <= left + patch && yf >= shadowTop &&
      yf <= shadowTop + patch) return shadowColor;

  return out;
}

float lineAlpha(float curveY, float screenY, float halfThickness,
                float falloff) {
  float dist = abs(curveY - screenY);
  return clamp(1.0f - (dist - halfThickness) / falloff, 0.0f, 1.0f);
}

float3 overlayRgbLine(float3 current, float curveY, float screenY,
                      float3 color, float halfThickness, float falloff) {
  float alpha = lineAlpha(curveY, screenY, halfThickness, falloff);
  return current * (1.0f - alpha) + color * alpha;
}

float3 drawSatCurve(float3 out, uint x, uint y, uint width, uint height,
                    constant MCVectorParams &p) {
  if (p.enableSaturation == 0 || p.showSatCurve == 0 || width == 0 ||
      height == 0) return out;
  float xf = float(x) / float(width);
  float yf = float(y) / float(height);
  float effGlobalSat = p.satGlobal;
  if (effGlobalSat > 1.0f) {
    effGlobalSat = 1.0f + (effGlobalSat - 1.0f) * 0.5f;
  }
  float sMult = applyBezier(xf, p.satLow, p.satMid, p.satHigh) * effGlobalSat;
  float baseCurve = (1.0f + (sMult - 1.0f) * p.satLumMask) * 0.5f;
  float halfThickness = 5.0f / float(height);
  float spacing = halfThickness * 2.0f;
  float falloff = 1.0f / float(height);

  out = overlayRgbLine(out, baseCurve + spacing, yf,
                       float3(1.0f, 0.0f, 0.0f), halfThickness, falloff);
  out = overlayRgbLine(out, baseCurve, yf, float3(0.0f, 1.0f, 0.0f),
                       halfThickness, falloff);
  out = overlayRgbLine(out, baseCurve - spacing, yf,
                       float3(0.0f, 0.0f, 1.0f), halfThickness, falloff);
  return out;
}

float3 drawZoneCurve(float3 out, uint x, uint y, uint width, uint height,
                     constant MCVectorParams &p) {
  if (p.enableZoneSaturation == 0 || p.showZoneCurve == 0 || width == 0 ||
      height == 0) return out;

  float xf = float(x) / float(width);
  float yf = float(y) / float(height);
  float gamma = 0.4101205819200422f;
  float xTone = pow(clamp(xf, 0.0f, 1.0f), gamma);
  float halfThickness = 4.0f / float(height);
  float falloff = 1.0f / float(height);

  float pivotX = clamp(p.zonePivot, 0.0f, 1.0f);
  float pivotDx = abs(xf - pivotX) * float(width);
  float pivotDy = abs(yf - 0.5f) * float(height);
  float pivotDot = clamp(1.0f - (sqrt(pivotDx * pivotDx + pivotDy * pivotDy) -
                                 3.0f) /
                                    2.0f,
                         0.0f, 1.0f);
  out = out * (1.0f - pivotDot) + float3(0.72f) * pivotDot;

  float satMult = zoneSatMultiplier(xTone, p);
  float curve = clamp(0.5f + (satMult - 1.0f) * 0.5f, 0.0f, 1.0f);
  return overlayRgbLine(out, curve, yf, float3(0.35f, 1.0f, 0.35f),
                        halfThickness, falloff);
}

float3 drawToneCurve(float3 out, uint x, uint y, uint width, uint height,
                     constant MCVectorParams &p) {
  if (p.enableSplitTone == 0 || p.showToneCurve == 0 || width == 0 ||
      height == 0) return out;
  float rv = float(x) / float(width);
  float screenY = float(y) / float(height);
  float3 ramp = applySplitTone(float3(rv), p);
  float halfThickness = 5.0f / float(height);
  float falloff = 1.0f / float(height);
  out = overlayRgbLine(out, ramp.x, screenY, float3(1.0f, 0.0f, 0.0f),
                       halfThickness, falloff);
  out = overlayRgbLine(out, ramp.y, screenY, float3(0.0f, 1.0f, 0.0f),
                       halfThickness, falloff);
  out = overlayRgbLine(out, ramp.z, screenY, float3(0.0f, 0.0f, 1.0f),
                       halfThickness, falloff);

  float shadowMix = p.shadowMix * 0.6f;
  float highlightMix = p.highlightMix * 0.6f;
  float neutralBlack = 1.0f - p.neutralBlack;
  float neutralWhite = 1.0f - p.neutralWhite;
  float pivot = clamp(pivotFromPreset(p.pivotPreset) + p.pivotOffset, 0.0f, 1.0f);
  float pivotWidth = p.pivotWidth * (pivot + 0.001f);
  float shadowPivot = clamp(pivot - pivotWidth, 0.0f, 1.0f);
  float highlightPivot = clamp(pivot + pivotWidth, 0.0f, 1.0f);
  float3 shadowColor =
      encodePatchTransfer(getShadowPatchColor(shadowPivot, shadowMix,
                                              neutralBlack,
                                              p.shadowCurveBias,
                                              p.shadowRedBellyCenter,
                                              p.shadowGreenBellyCenter,
                                              p.shadowBlueBellyCenter),
                          p.pivotPreset);
  float3 highlightColor =
      encodePatchTransfer(getHighlightPatchColor(highlightPivot, highlightMix,
                                                 neutralWhite,
                                                 p.highlightCurveBias),
                          p.pivotPreset);
  return drawColorPatches(out, x, y, width, height, shadowColor,
                          highlightColor);
}

kernel void MCVectorKernel(constant int &width [[ buffer(0) ]],
                           constant int &height [[ buffer(1) ]],
                           constant int &rowBytes [[ buffer(2) ]],
                           constant MCVectorParams &params [[ buffer(3) ]],
                           const device float *input [[ buffer(4) ]],
                           device float *output [[ buffer(5) ]],
                           uint2 id [[ thread_position_in_grid ]]) {
  if (id.x >= (uint)width || id.y >= (uint)height) return;
  int fpr = rowBytes / int(sizeof(float));
  int idx = int(id.y) * fpr + int(id.x) * 4;

  float3 in = float3(input[idx + 0], input[idx + 1], input[idx + 2]);
  float3 out = applyVector(in, params);
  out = drawToneCurve(out, id.x, id.y, uint(width), uint(height), params);
  out = drawSatCurve(out, id.x, id.y, uint(width), uint(height), params);
  out = drawZoneCurve(out, id.x, id.y, uint(width), uint(height), params);

  output[idx + 0] = out.x;
  output[idx + 1] = out.y;
  output[idx + 2] = out.z;
  output[idx + 3] = input[idx + 3];
}
)METAL";

static std::mutex s_Mutex;
static std::unordered_map<id<MTLCommandQueue>, id<MTLComputePipelineState>>
    s_PipelineMap;

extern "C" void RunMetalKernel(void *commandQueue, int width, int height,
                               int rowBytes, const float *input,
                               float *output, MCVectorParams params) {
  @autoreleasepool {
    id<MTLCommandQueue> queue = static_cast<id<MTLCommandQueue>>(commandQueue);
    if (!queue) {
      return;
    }

    id<MTLDevice> device = queue.device;
    id<MTLComputePipelineState> pipelineState = nil;
    {
      std::lock_guard<std::mutex> lock(s_Mutex);
      auto it = s_PipelineMap.find(queue);
      if (it != s_PipelineMap.end()) {
        pipelineState = it->second;
      } else {
        NSError *err = nil;
        MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000
        options.mathMode = MTLMathModeFast;
#else
        options.fastMathEnabled = YES;
#endif

        id<MTLLibrary> library =
            [device newLibraryWithSource:@(kMetalSource)
                                  options:options
                                    error:&err];
        [options release];
        if (!library) {
          fprintf(stderr, "[MCVector] Metal compile error: %s\n",
                  err.localizedDescription.UTF8String);
          return;
        }

        id<MTLFunction> fn = [library newFunctionWithName:@"MCVectorKernel"];
        [library release];
        if (!fn) {
          fprintf(stderr, "[MCVector] Metal function MCVectorKernel not found\n");
          return;
        }

        pipelineState = [device newComputePipelineStateWithFunction:fn
                                                              error:&err];
        [fn release];
        if (!pipelineState) {
          fprintf(stderr, "[MCVector] Metal pipeline error: %s\n",
                  err.localizedDescription.UTF8String);
          return;
        }
        s_PipelineMap[queue] = pipelineState;
      }
    }

    id<MTLBuffer> srcBuf =
        reinterpret_cast<id<MTLBuffer>>(const_cast<float *>(input));
    id<MTLBuffer> dstBuf = reinterpret_cast<id<MTLBuffer>>(output);

    id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [cmdBuf computeCommandEncoder];
    [encoder setComputePipelineState:pipelineState];
    [encoder setBytes:&width length:sizeof(int) atIndex:0];
    [encoder setBytes:&height length:sizeof(int) atIndex:1];
    [encoder setBytes:&rowBytes length:sizeof(int) atIndex:2];
    [encoder setBytes:&params length:sizeof(MCVectorParams) atIndex:3];
    [encoder setBuffer:srcBuf offset:0 atIndex:4];
    [encoder setBuffer:dstBuf offset:0 atIndex:5];

    NSUInteger exeWidth = pipelineState.threadExecutionWidth;
    NSUInteger maxHeight = pipelineState.maxTotalThreadsPerThreadgroup / exeWidth;
    MTLSize threadsPerGroup = MTLSizeMake(exeWidth, maxHeight, 1);
    MTLSize threadgroups =
        MTLSizeMake(((NSUInteger)width + exeWidth - 1) / exeWidth,
                    ((NSUInteger)height + maxHeight - 1) / maxHeight, 1);

    [encoder dispatchThreadgroups:threadgroups
            threadsPerThreadgroup:threadsPerGroup];
    [encoder endEncoding];
    [cmdBuf commit];
  }
}
