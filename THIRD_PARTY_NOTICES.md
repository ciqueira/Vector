# Third-Party Notices

This file lists third-party material actually used by Vector. It does not
license the project-owned code, which is governed by `LICENSE.md`.

Audit date: 2026-06-26.

## OpenFX

Vector is built against the shared OpenFX SDK used by MC OFX. The API headers
and support library are provided by The Open Effects Association Ltd. under
their retained BSD-style licenses.

- Project: <https://github.com/AcademySoftwareFoundation/openfx>
- Version: `OFX_Release_1.5.1`
- Support-library license: `MCPlugins/third_party/openfx/Support/LICENSE`
- Individual API headers retain their original notices.

Binary distributions must reproduce the applicable OpenFX copyright,
conditions, and disclaimer. The MC OFX build wrapper copies the shared SDK
support license into the OFX bundle as `OPENFX-BSD-3-CLAUSE.txt` when legal
resources are packaged by the build.

## NVIDIA CUDA Runtime

Windows and Linux builds use the NVIDIA CUDA Runtime. The Windows build links
`cudart_static.lib`; the CUDA Toolkit EULA identifies the CUDA Runtime,
including the static runtime libraries, as distributable when incorporated
into an application that complies with NVIDIA's distribution requirements.

- CUDA Toolkit EULA:
  <https://docs.nvidia.com/cuda/eula/index.html>
- Used in: `src/CudaKernel.cu` and the Windows/Linux build configuration.

CUDA and NVIDIA are trademarks or registered trademarks of NVIDIA
Corporation. NVIDIA does not endorse Vector.

## Technical Specifications

Vector uses published camera and grading pipeline names for user-facing pivot
presets:

- ACES AP1 / ACEScct
- DaVinci Wide Gamut / Intermediate
- ARRI Wide Gamut 3 / LogC3
- ARRI Wide Gamut 4 / LogC4

The selected input-space control is used only as a split-tone pivot preset.
Vector does not include alternate color-space conversion code for those
presets.

Curves saturation, split-tone processing, preview overlays, RGB math, Metal
implementation, CUDA implementation, and OFX integration are project-owned
implementations and require no additional third-party attribution in this
notice.
