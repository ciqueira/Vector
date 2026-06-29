# Architecture

## Runtime Responsibilities

```text
Input RGBA
  -> OFX wrapper
  -> parameter snapshot
  -> CPU fallback, Metal, or CUDA backend
  -> parallel RGB processing
       split-tone branch
       curves saturation branch
       zone saturation branch
  -> branch deltas combined over original RGB
  -> optional curve overlays and split-tone color patches
  -> Output RGBA
```

Vector intentionally keeps processing in the incoming RGB signal. The
`Input Space` parameter is a pivot preset for split tone, not a color
conversion selector.

## Main Files

| File | Responsibility |
| --- | --- |
| `src/MCVector.cpp` | OFX registration, parameters, image setup, CPU fallback, and GPU dispatch |
| `src/VectorMath.h` | Host-side RGB processing and overlay reference |
| `src/MetalKernel.mm` | macOS Metal pixel-processing implementation |
| `src/CudaKernel.cu` | Windows/Linux CUDA pixel-processing implementation |
| `src/VectorParams.h` | Shared parameter layout |

## Processing Model

Vector combines three branches in parallel:

```text
base RGB -> Split Tone        -> split-tone delta
base RGB -> Curves Saturation -> saturation delta
base RGB -> Zone Saturation   -> zone saturation delta
base RGB + split-tone delta + saturation delta + zone delta -> output RGB
```

This avoids a serial dependency where saturation changes would alter the
split-tone input or split tone would alter the saturation curve response.
Zone saturation follows the same rule: it reads the original RGB signal and
contributes only its own delta to the final result.

## GPU Parity

Metal and CUDA implement the same math and parameter layout as the host-side
fallback. macOS uses Metal; Windows and Linux use CUDA.

The manual `Validate MCVector CUDA` workflow compiles the Windows/CUDA bundle
without uploading or publishing an artifact.

## CI and Release Gate

1. Resolve `src/VERSION` or a manually supplied version.
2. Validate that the target release tag does not already exist.
3. Build macOS and/or Windows artifacts from MC OFX.
4. Publish the release to `ciqueira/Vector` after successful platform builds.
