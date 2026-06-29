// Copyright (c) 2026 Magno Ciqueira. All rights reserved.
// SPDX-License-Identifier: LicenseRef-MCVector-Proprietary
// See LICENSE.md in the repository root for source-available terms.

#ifndef MCVECTOR_PARAMS_H
#define MCVECTOR_PARAMS_H

typedef struct {
  int pivotPreset; // 0=ACEScct, 1=DWG, 2=LogC3, 3=LogC4
  int enableSaturation;
  int enableZoneSaturation;
  int enableSplitTone;
  int showSatCurve;
  int showToneCurve;
  int showZoneCurve;
  int saturationModelSpace; // 0=RGB Direct, 1=RGB Spherical, 2=OKLCH

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
} MCVectorParams;

#endif // MCVECTOR_PARAMS_H
