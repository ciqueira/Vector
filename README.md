# Vector

Vector brings look-development tools for split-tone and saturation operations
into a single OFX. It keeps related adjustments together, uses the incoming RGB
signal as the reference, and reduces the need to stack several small tools for
the same kind of correction.

Processing combines a serial split-tone stage with two parallel saturation
branches. The tool stays direct, with separate controls for chroma saturation,
tonal-zone saturation, and curve previews.

Vector is distributed through [MCNexus](https://mcnexus.app).
Nexus provides distribution, licensing, update delivery, and product support.
MCNexus is the desktop application used to activate, install, update, and manage
the plugin.

## Included Plugins

| Plugin | Version | Distribution | Free Key | Support Project |
| --- | --- | --- | --- | --- |
| Vector | Current | OpenKey | [Get Key](https://bridge.mcnexus.app/github/claim?t=vector-oss&tmpl=363f9ad5-7dec-4e29-86e6-b5923dbfb2d4&sig=ea36f997f27e41cd) | [Become a Supporter](https://bridge.mcnexus.app/commerce/start?t=vector-oss&offer=vector-supporter) |

## Vector

Vector keeps the main processing in the incoming RGB signal. `Input Space`
defines the preset used as a reference for pivots and curves, but it does not
turn the plugin into a full color-space converter.

Available input presets:

- ACES AP1 / ACEScct
- DaVinci Wide Gamut / Intermediate
- ARRI Wide Gamut 3 / LogC3
- ARRI Wide Gamut 4 / LogC4

`Model/Space Type` defines how the saturation branches interpret the signal:

- `RGB Direct`: measures the spread between RGB channels.
- `RGB Spherical`: uses a spherical reading around the neutral axis.
- `OKLCH`: uses a perceptual reading based on Oklab.

## Processing Model

Processing starts with split tone:

```text
Input RGB -> Split Tone -> Split RGB

Split RGB -> Curves Saturation -> saturation delta
Split RGB -> Zone Saturation   -> zone saturation delta

Split RGB + saturation deltas -> Output RGB
```

Split tone is serial and first creates the base separation between shadows and
highlights. Curves Saturation and Zone Saturation then run in parallel from the
same `Split RGB` source. One saturation branch does not feed the other; both
contribute to the final result from the same image state.

## Controls

`Split Tone` creates color separation between shadows and highlights. Controls
include shadow and highlight strength, neutral black and white protection,
pivot width and offset, curve bias, and curve preview.

`Curves Saturation` adjusts low, mid, and high saturation. `Global Sat` changes
the overall response, while `Curve Amount` blends the curve back toward a
neutral response.

`Zone Saturation` changes saturation by tonal region. `Zone Focus` directs the
operation toward shadows, highlights, or both sides of the pivot. `Pivot`,
`Pivot Width`, and `Sat Strength` define the transition and intensity.

## Platform Support

Current builds support:

- macOS, Apple Silicon and compatible Intel Macs
- Windows x64

Supported processing backends:

- Metal on macOS
- CUDA on Windows

## Installation

1. Use the `Get Key` link above to generate the OpenKey license with a GitHub
   account.
2. Open MCNexus.
3. Activate Vector with the issued key.
4. Install or update the plugin through MCNexus.

Lost key: open the same claim link with the same GitHub account to recover the
issued license.

## Support the Project

Vector remains available free of charge with all currently published plugin
features. If it is useful in your work, you can optionally support its
maintenance and continued development.

The Vector Supporter benefit includes:

- priority private email support for 12 months; and
- operational email notices about Vector releases, compatibility, maintenance,
  security, and material service changes.

The purchase does not add exclusive plugin features. To deliver and associate
the Supporter benefit, Nexus may create a new technical license or associate
and update an existing license. You do not need to obtain the free key before
checkout; existing users should use the same GitHub account and verified
email.

[Purchase Vector Supporter](https://bridge.mcnexus.app/commerce/start?t=vector-oss&offer=vector-supporter)

Before purchasing, read the
[Supporter Terms](https://legal.magnociqueira.com.br/products/vector/terms/),
[Refund Policy](https://legal.magnociqueira.com.br/products/vector/refunds/),
[Privacy Policy](https://legal.magnociqueira.com.br/products/vector/privacy/),
and [Support Policy](https://legal.magnociqueira.com.br/products/vector/support/).
Markdown copies remain under [`legal/`](legal/README.md) for repository
discoverability. Messages about unrelated products are not included
automatically and require a separate marketing choice if one is offered in the
future.

## License

Vector is source-available for review, documentation, and technical
transparency. Public access to this repository does not make the project
open-source software.

See:

- [LICENSE.md](LICENSE.md)
- [BINARY_LICENSE.md](BINARY_LICENSE.md)
- [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
- [Product legal documents](legal/README.md)

## Binary Releases

Official binary releases are distributed through Nexus and installed with
MCNexus. Use only official MCNexus or project release channels for binaries,
updates, and activation.
