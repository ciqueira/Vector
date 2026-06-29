# Architecture

## Runtime Responsibilities

```text
Input RGBA
  -> OFX wrapper
  -> parameter snapshot
  -> CPU fallback, Metal, or CUDA backend
  -> hybrid RGB processing
       split-tone serial base
       curves saturation branch from split RGB
       zone saturation branch from split RGB
  -> saturation deltas combined over split RGB
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

Vector uses a hybrid serial/parallel model:

```text
base RGB -> Split Tone -> split RGB
split RGB -> Curves Saturation -> saturation delta
split RGB -> Zone Saturation   -> zone saturation delta
split RGB + saturation delta + zone delta -> output RGB
```

Split tone establishes the color-separated base first. Curves Saturation and
Zone Saturation then run in parallel from that same split RGB base, so the two
saturation branches remain independent from each other while responding to the
color separation created by split tone.

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
