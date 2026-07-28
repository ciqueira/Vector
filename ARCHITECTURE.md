# Architecture

## Runtime Responsibilities

```text
Input RGBA
  -> OFX wrapper
  -> parameter snapshot
  -> CPU fallback, Metal, or CUDA backend
  -> split-tone plus parallel saturation processing
       split-tone serial base
       curves saturation branch from split RGB
       zone saturation branch from split RGB
  -> saturation deltas combined over split RGB
  -> optional curve overlays and split-tone color patches
  -> Output RGBA
```

RGB Direct, RGB Spherical, and Split Tone work in the incoming RGB signal.
When OKLCH saturation is selected, `Input Space` also selects the transfer
decode/encode and linear RGB gamut used by the Oklab conversion. It continues
to provide the split-tone pivot preset. Both saturation branches modify Oklab
chroma directly for every hue, including cyan, blue, and magenta; they do not
blend the result with RGB Spherical.

## Main Files

| File | Responsibility |
| --- | --- |
| `src/MCVector.cpp` | OFX registration, parameters, image setup, CPU fallback, and GPU dispatch |
| `src/VectorMath.h` | Host-side RGB processing and overlay reference |
| `src/MetalKernel.mm` | macOS Metal pixel-processing implementation |
| `src/CudaKernel.cu` | Windows/Linux CUDA pixel-processing implementation |
| `src/VectorParams.h` | Shared parameter layout |
| `../tests/` | Shared host-side OKLCH numerical and regression tests |

## Processing Model

Vector uses a serial/parallel processing model:

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

Identity saturation branches are bypassed before color-model conversion.
Near-neutral OKLCH corrections use a continuous chroma weight instead of
snapping Oklab `a` and `b` to zero. OKLCH remains a pure model across the full
hue circle; RGB Direct and RGB Spherical are independent user-selectable
models rather than fallback paths.

## GPU Parity

Metal and CUDA implement the same math and parameter layout as the host-side
fallback. macOS uses Metal; Windows and Linux use CUDA. All three paths
unpremultiply associated-alpha input before processing and premultiply the
result when required by the output clip.

The manual `Validate MCVector CUDA` workflow compiles the Windows/CUDA bundle
without uploading or publishing an artifact.

## CI and Release Gate

1. Resolve `src/VERSION` or a manually supplied version.
2. Validate that the target release tag does not already exist.
3. Build macOS and/or Windows artifacts from MC OFX.
4. Publish the release to `ciqueira/Vector` after successful platform builds.
