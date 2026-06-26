// Copyright (c) 2026 Magno Ciqueira. All rights reserved.
// SPDX-License-Identifier: LicenseRef-MCVector-Proprietary
// See LICENSE.md in the repository root for source-available terms.

#ifndef MCVECTOR_PARAMS_H
#define MCVECTOR_PARAMS_H

typedef struct {
  int pivotPreset; // 0=ACEScct, 1=DWG, 2=LogC3, 3=LogC4
  int enableSaturation;
  int enableSplitTone;
  int showSatCurve;
  int showToneCurve;
  int _pad0;
  int _pad1;
  int _pad2;

  float satLow;
  float satMid;
  float satHigh;
  float satGlobal;
  float satLumMask;

  float splitShadow;
  float shadowMix;
  float neutralBlack;
  float splitHighlight;
  float highlightMix;
  float neutralWhite;
  float pivotWidth;
  float pivotOffset;
} MCVectorParams;

#endif // MCVECTOR_PARAMS_H
