# Graticule

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. The claims are *measured* rather
> than taken on trust. The SMPTE bars are checked against a run-length analysis of ffmpeg's
> `smptehdbars` at four rasters (`gttest --bars`), and their fifteen colours are computed
> through Rec. 709 and land on the measured code values (`gttest --colours`). Every other
> pattern is rendered by the real plugin class in a real GL context and specific pixels are
> checked (`gttest --pixels`), all 69 controls are proven to change the picture
> (`tools/sweep.py`), and the bundle registers, instantiates and lights pixels in the
> fleet's oxbow host. **It's been installed into Resolume Arena 7.27.1's plugin folder but
> hasn't been dropped into a clip there yet**, so the last step of proof is a screenshot
> nobody has taken.

Test patterns and LED grids for Resolume, generated live at the output's own raster.

The static-image version of this is [Test Card](https://github.com/stoatworks-labs/test-card),
which renders a PNG per output from an imported output map. Graticule is the same patterns
as a Resolume source: drop it on a layer and it draws at whatever size the composition or
the slice gives it, with a text burn-in, a moving marker and a frame counter, none of which
a PNG can manage.

It's an FFGL source plugin for Resolume Arena and Avenue.

<!-- downloads:start -->

<!-- downloads:end -->

## Patterns

| Pattern | What it is for |
|---|---|
| **SMPTE RP 219** | Standard bars, +I/+Q, luma ramp and pluge. Geometry and colours measured from a real generator — see [Bars.h](source/Bars.h) |
| **75% / 100% bars** | Eight equal full-height bars. Survives being squeezed onto a narrow LED strip where RP 219's four rows would be unreadable |
| **Grid** | Your own pitch in pixels or a division count, heavy lines every N, cell labels, diagonals, centre target, origin at the corner or the centre |
| **Alignment** | 1px edge border, corner brackets with the pixel count, safe areas, centre target and circles, 1px checker patches |
| **LED Tiles** | One lettered cell per cabinet, with module lines inside each cabinet. Tile size, module size and origin are free integers, so a real wall's numbers go straight in |
| **Solid** | Flat colour at a chosen level, for dead pixels and uniformity |
| **Greyscale** | Continuous ramp or stepped wedge, optionally split into R, G, B and neutral |
| **Pixel Check** | 1px checkerboard beside 1px line bursts. If any of the three renders as flat grey on the real output, something in the chain is scaling |

Every line is a whole number of pixels and nothing is anti-aliased, on purpose: a soft edge
on the wall means a scaler in the chain, never the pattern.

## Beyond the PNG

- **Levels**: full range 0–255 (media server, GPU, HDMI full) or legal 16–235 (an SDI
  chain). At full range the −2% pluge patch clips to black and can't be judged; that's
  what full range means, and it's why the pluge is only meaningful on legal.
- **Motion**: a bar sweeping across or down, or a bouncing box, inverting whatever is under
  it so it's visible on every pattern. The default speed is exactly one crossing a second.
- **Frame Counter**: frame number and seconds in the burn-in. Film the wall and the monitor
  together and the offset between the two counters is the latency, to the frame.
- **Burn-in**: resolution, pattern name and your own text, in a 5×7 bitmap font drawn by
  the shader at an integer scale, so the text is as pixel-exact as the pattern under it.
  Position, size and a contrasting plate are controls.
- **Presets**: thirteen whole jobs in one dropdown: the bars at each range, grids by pitch
  and by division, the alignment chart, 500 mm cabinets at P2.6 / P2.9 / P3.9 and a
  500×1000 at P2.6, the wedge, the pixel check, and a latency sweep. Presets are an
  override: while one is selected the inspector's numbers for the columns it sets aren't
  the truth, because Resolume doesn't accept value events from a plugin.

## Installing

Copy `Graticule.bundle` (macOS) or `Graticule.dll` (Windows) into

    ~/Documents/Resolume Arena/Extra Effects        (or "Resolume Avenue")
    Documents\Resolume Arena\Extra Effects           (Windows)

then restart Resolume. It shows up under **Sources**.

## Building

    git submodule update --init --recursive
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build
    ctest --test-dir build --output-on-failure
    cmake --install build          # into Arena's Extra Effects, macOS

The macOS build is universal (arm64 + x86_64) by default; add
`-DCMAKE_OSX_ARCHITECTURES=arm64` for a quicker development build. Windows needs GLEW from
vcpkg, and `.github/workflows/release.yml` has the full configure line.

`tools/verify.sh` runs everything the release job checks, locally, in a few seconds.

## Diagnostics

Graticule writes a plain-text log every time it runs:

    ~/Library/Logs/graticule/graticule.YYYY-MM-DD.log                     (macOS)
    %LOCALAPPDATA%\graticule\logs\graticule.YYYY-MM-DD.log                (Windows)
    ${XDG_STATE_HOME:-~/.local/state}/graticule/logs/graticule.YYYY-MM-DD.log

It records the build, the GL driver, what unit the host's clock turned out to be, which
pattern is drawing at what size, and any shader or texture failure. If you're filing a bug,
it's the single most useful thing to attach.

## Status

- Every pattern verified pixel by pixel through the real plugin class on macOS
  (`gttest --pixels`); the bars verified against ffmpeg's measured geometry at 1920×1080,
  3840×2160, 1280×720 and 2048×1080.
- The bundle registers, instantiates and renders 120 frames under oxbow.
- Installed into Arena 7.27.1's Extra Effects on the development machine; **not yet
  instantiated inside Arena**.
- Windows is built by CI only and has never been run by anyone.
- No browser demo yet.

## Licence

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
