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

float3 applyShadowSplit(float3 in, float pivot, float strength, float colorMix,
                        float neutralBlack) {
  float3 out = in;
  if (pivot <= 1.0e-6f) return out;

  float blueStrength = ((1.0f - colorMix) * strength) / 6.0f;
  float greenStrength = (colorMix * strength) / 6.0f;

  if (in.x <= pivot) {
    float r = in.x / pivot;
    float inv = 1.0f - r;
    out.x = ((0.0f - blueStrength * neutralBlack -
              greenStrength * neutralBlack) * inv * inv * inv +
             3.0f * (0.333f - blueStrength * neutralBlack -
                      greenStrength * neutralBlack) * inv * inv * r +
             3.0f * (0.666f - blueStrength - greenStrength) * inv * r * r +
             r * r * r) * pivot;
  }

  if (in.y <= pivot) {
    float g = in.y / pivot;
    float inv = 1.0f - g;
    out.y = (greenStrength * neutralBlack * inv * inv * inv +
             3.0f * (greenStrength * neutralBlack + 0.333f) * inv * inv * g +
             3.0f * (greenStrength + 0.666f) * inv * g * g + g * g * g) *
            pivot;
  }

  if (in.z <= pivot) {
    float b = in.z / pivot;
    float inv = 1.0f - b;
    out.z = (blueStrength * neutralBlack * inv * inv * inv +
             3.0f * (blueStrength * neutralBlack + 0.333f) * inv * inv * b +
             3.0f * (blueStrength + 0.666f) * inv * b * b + b * b * b) *
            pivot;
  }
  return out;
}

float3 applyHighlightSplit(float3 in, float pivot, float strength,
                           float colorMix, float neutralWhite) {
  float3 invIn = 1.0f - in;
  float invPivot = 1.0f - pivot;
  if (invPivot <= 1.0e-6f) return in;

  float redStrength = ((1.0f - colorMix) * strength) / 8.0f;
  float greenStrength = (colorMix * strength) / 8.0f;
  float3 invOut = invIn;

  if (invIn.x <= invPivot) {
    float r = invIn.x / invPivot;
    float inv = 1.0f - r;
    invOut.x = ((1.0f - (neutralWhite * redStrength + 1.0f)) *
                    inv * inv * inv +
                3.0f * (1.0f - (neutralWhite * redStrength + 0.666f)) *
                    inv * inv * r +
                3.0f * (1.0f - (redStrength + 0.333f)) * inv * r * r +
                r * r * r) * invPivot;
  }

  if (invIn.y <= invPivot) {
    float g = invIn.y / invPivot;
    float inv = 1.0f - g;
    invOut.y = ((1.0f - (neutralWhite * greenStrength + 1.0f)) *
                    inv * inv * inv +
                3.0f * (1.0f - (neutralWhite * greenStrength + 0.666f)) *
                    inv * inv * g +
                3.0f * (1.0f - (greenStrength + 0.333f)) * inv * g * g +
                g * g * g) * invPivot;
  }

  if (invIn.z <= invPivot) {
    float b = invIn.z / invPivot;
    float inv = 1.0f - b;
    invOut.z = ((1.0f - (1.0f - greenStrength * neutralWhite -
                         redStrength * neutralWhite)) *
                    inv * inv * inv +
                3.0f * (1.0f - (0.666f - greenStrength * neutralWhite -
                                 redStrength * neutralWhite)) *
                    inv * inv * b +
                3.0f * (1.0f - (0.333f - greenStrength - redStrength)) *
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
  float shadowStrength = p.splitShadow * 2.0f;

  float3 out = applyShadowSplit(clamped, shadowPivot, shadowStrength, shadowMix,
                                neutralBlack);
  return applyHighlightSplit(out, highlightPivot, p.splitHighlight,
                             highlightMix, neutralWhite);
}

float3 applyVector(float3 in, constant MCVectorParams &p) {
  float3 sat = p.enableSaturation != 0 ? applyCurvesSaturation(in, p) : in;
  float3 split = p.enableSplitTone != 0 ? applySplitTone(in, p) : in;
  return in + (sat - in) + (split - in);
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
  float spacing = 2.0f / float(height);
  float thickness = 0.5f;
  float falloff = 1.0f;
  float alphaR = clamp(1.0f - (abs(yf - (baseCurve + spacing)) * float(height) -
                               thickness) / falloff, 0.0f, 1.0f);
  float alphaG = clamp(1.0f - (abs(yf - baseCurve) * float(height) -
                               thickness) / falloff, 0.0f, 1.0f);
  float alphaB = clamp(1.0f - (abs(yf - (baseCurve - spacing)) * float(height) -
                               thickness) / falloff, 0.0f, 1.0f);
  float combined = max(alphaR, max(alphaG, alphaB));
  return out * (1.0f - combined) + float3(alphaR, alphaG, alphaB);
}

float3 drawToneCurve(float3 out, uint x, uint y, uint width, uint height,
                     constant MCVectorParams &p) {
  if (p.enableSplitTone == 0 || p.showToneCurve == 0 || width == 0 ||
      height == 0) return out;
  float rv = float(x) / float(width);
  float screenY = float(y) / float(height);
  float3 ramp = applySplitTone(float3(rv), p);
  float halfThickness = 5.0f / float(height);
  if (abs(ramp.x - screenY) <= halfThickness) out.x = 1.0f;
  if (abs(ramp.y - screenY) <= halfThickness) out.y = 1.0f;
  if (abs(ramp.z - screenY) <= halfThickness) out.z = 1.0f;
  return out;
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
  out = drawSatCurve(out, id.x, id.y, uint(width), uint(height), params);
  out = drawToneCurve(out, id.x, id.y, uint(width), uint(height), params);

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
