# Working on graticule

An FFGL **source** plugin (`GT01`) that generates test patterns and LED cabinet grids
live at the output's raster: SMPTE RP 219, plain bars, grid, alignment chart, LED tiles,
solid, greyscale and pixel check, with a burn-in, a motion marker and a frame counter.

`CLAUDE.md` is the command reference; this file is the *why*.

## Status

Built 2026-09-17. Every check in `tools/verify.sh` passes on macOS: the font, the RP 219
colours and geometry against ffmpeg's measurement, the pixel checks, the preset table, the
dead-control sweep, registration, lipo, plist, ad-hoc codesign and oxbow. The universal
bundle is installed in Arena 7.27.1's Extra Effects on the development machine. **It has
not been dropped into a clip inside Arena yet**, and the Windows build is CI-only and has
never been run.

No release tag, no website registration, no browser demo.

## Where the truth lives

- **The patterns are ported from Test Card** (`stoatworks-labs/test-card`,
  `src/patterns/draw.ts` and `rp219.ts`). The intent is that the two agree to the pixel.
  When one changes, the other should. Two deliberate differences: the alignment chart's
  corner brackets are drawn fully inside the raster (Test Card's stroke straddles the edge
  and loses a pixel), and its labels sit at the bottom-left of what they label so the
  burn-in, which defaults to top-left, does not cover them.
- **The RP 219 fixture** `tools/fixtures/rp219-ffmpeg.json` is copied from Test Card. It
  was measured from ffmpeg's `smptehdbars`, and `gttest --bars` asserts against it. Do not
  regenerate it here; regenerate it there with `scripts/extract-rp219-reference.py`.
- **The shader is the pattern.** `source/Shaders.cpp` decides every pixel; the C++ only
  converts parameters into the shader's units (integer pixels, top-down). If a pattern is
  wrong, the fix is in GLSL.

## Ground rules carried from the fleet

- Every ranged parameter is **0..1 host-side** unless it is `FF_TYPE_INTEGER`, which holds
  the real integer. `SetParamInfo` clamps a STANDARD default before `SetParamRange` can
  widen it, and there is no `SetParamDefault`. This plugin is mostly integers because pixel
  counts want typing, not sliding.
- Resolume's `SetTime` is in **milliseconds**. `Clock` measures the unit rather than
  assuming it; the harness forces seconds.
- Factory presets are an **override**, not a write — Resolume ignores value events.
- The four-character plugin id must be unique across the fleet. `GT01` is this one.
- **Every parameter name must be unique**, because `gttest --set` and the sweep find
  parameters by name. The alignment chart's switches are "Centre Marks" and "Diagonal
  Cross" precisely because the grid already had "Centre Target" and "Diagonals".
- `patch`, `sample`, `input`, `output`, `filter`, `common`, `active`, `half` are GLSL
  reserved words. `patch` cost a build here. Shader errors surface only at runtime, in the
  diagnostics log, and in `gttest --pixels` as `InitGL FAILED`.
- `oxbow selftest` is the only check that proves the bundle actually registers a plugin. A
  clean build and a green test run do not. Unlike gridiron, this source draws with no input
  and no file, so oxbow's PASS verdict is meaningful here and `verify.sh` requires it.
- Anything the release job does that can be done locally goes in `tools/verify.sh`.

## Do not trust

- **`ATTRIBUTIONS.md`** and **`source/StoatworksAbout.h`** are hand-written in the shape the
  fleet's sync scripts generate. Register the project in `stoatworks-website`'s
  `projects.json`, `stoatworks-backend`'s `sync-about.py` TARGETS and `attributions/names.json`
  and re-run the syncs before the first release. The About header's facts were chosen so
  the button count — and therefore the parameter count — does not change when it is
  regenerated; if the sync adds a guide link, `PT_ABOUT_BUTTON_n` and the static_assert in
  `Graticule.cpp` will tell you.
