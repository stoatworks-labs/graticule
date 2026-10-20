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

## The browser demo (2026-10-20)

`demo/` is <https://graticule-demo.stoatworks-labs.com>, built to the fleet's
`resolume-demo` kit rules, zoetrope's way (fiche's and polyhedral's before it): it **runs
the plugin rather than a port of it**. Built and tested locally on 2026-10-20 for v0.1.0;
the DNS record, the repo secret and variable, the first deploy and the website's `demo`
field were left to the release.

- **`graticule-core.wasm` is the plugin's C++, unmodified**: Graticule.cpp, Render,
  Shaders, Bars, Font, Clock and Diag, with the SDK's CFFGLPluginManager / CFFGLPlugin,
  FFGLLog, FFGLShader and FFGLUtilities. Left out: SourcePlugin.cpp (the registration and
  build stamp), so also FFGL.cpp (plugMain) and CFFGLPluginInfo. `-D__linux__` goes only
  to the files that include the SDK (Graticule, Render, Bars through Controls.h, the
  SDK's own, glue.cpp). Committed, with `demo/wasm/inputs.sha256` pinning every input;
  rebuild with `demo/tools/build-wasm.sh`. **Nothing is ported to JavaScript.**
- **The host** (`demo/wasm/glue.cpp`) constructs the plugin, reads its declarations back
  through the SDK's host getters (the panel, About block included, is built from them) and
  forwards SetFloatParameter, SetTextParameter (Text), Resize on a raster change (FF_RESIZE,
  CFFGLPlugin's own, which ProcessOpenGL reads) and each frame SetTime and ProcessOpenGL
  with no inputs, in gttest's order. It does NOT call ForceSecondsClock: the plugin votes
  on the unit as it would in a host. `GRATICULE_LOG_DIR` must be set before the first
  InitGL (Diag opens its log there). gt_convert is Controls.h's FieldLevel and
  CrossingsPerSecond; gt_effective / gt_overridden are `Effective()`, the overridden
  probe setting a control to two values and putting it back.
- **The shim** (`demo/wasm/gl_shim.cpp`) is one entry point: glShaderSource (the text must
  equal the page's assembly of `shaders.js`, then the kit's `port()`). Graticule touches no
  desktop-only state, its font is a single-level GL_R8 upload ES 3.0 takes as it is, and
  the triangle comes from gl_VertexID, so nothing else needed translating.
- **Pixels.** The kit draws its canvas at `width: 100%`, a CSS scale, which would draw a
  soft grid and a grey pixel check and blame the plugin. `plugin.js` adds a "This screen,
  1:1" composition (the default without `?size=`): the canvas's own box in device pixels
  from a ResizeObserver on `device-pixel-content-box`, re-measured on resize, zoom and
  screen change, so the raster is the device raster by construction; in embed mode that
  is the viewport. A fixed composition is shown at raster ÷ devicePixelRatio CSS pixels
  and scrolls rather than shrinks. "Pixels: Scaled to fit" scales on purpose. The line
  under the picture reports the device box the browser gave the canvas. The stage's
  rounded corners are squared: kit.css's 8 px radius hid the corners of the raster, where
  the alignment chart's border and brackets live. DevTools' device emulation reports CSS
  pixels as device pixels while painting at the emulated ratio, so a report that
  disagrees with CSS × devicePixelRatio by more than 2 is replaced by that estimate and
  labelled as one; test with Chrome's `--force-device-scale-factor`, not emulation.
- **Checked**: `demo/tools/check_shaders.py` (verify.sh's `demo` step) holds the vertex
  shader and the fragment shader's three raw strings to Shaders.cpp, Render.cpp's single
  Compile() to the ASSEMBLY order, Shaders.h's declarations, and every input of the
  .wasm to its manifest (a new source file fails too). 9 of 9 negative controls caught:
  one character of the GLSL in Shaders.cpp; a space typed into shaders.js; a comment in
  Graticule.cpp; a new header; a new .cpp; a new shader in Shaders.cpp; Compile()'s stages
  swapped; a byte on the .wasm; a line in glue.cpp. At run time a shaders.js with one
  space added to a comment was refused, and the page said why rather than drawing.
- **Compared with gttest, 2026-10-20** (M4 Max, Chrome 155 headless, ANGLE on Metal; the
  page's `?size=` frames read back with readPixels against `gttest --out` for the same
  `--set`s): 20 settings, all identical to the pixel, 0 of 32.2 M pixels differing: RP 219
  full and legal, 75% and 100% bars, the grid by pitch (labels, origin centre, widths 2
  and 5) and by divisions, the alignment chart at 1920 × 1080 and 1366 × 768, two LED
  presets (one with an offset origin, one at 3840 × 2160), solid fields, the ramp and the
  wedge, the pixel check, a centred size-4 burn-in with typed text, and the marker
  (Sweep Across at 0.5 s and Bounce at 1.25 s, by Pause, Restart and Steps against
  `--frames`). On SwiftShader 18 of 20 were identical and two differed by one code value
  where a level lands exactly half-way (the wedge's 0.3 step at 76.5, the burn-in plate's
  25.5): GL leaves the float-to-8-bit rounding there to the implementation. Negative
  controls, all caught: Pitch 37 against 38, HELLO against HELLP, legal against full, the
  marker one frame early, Level 0.37 against 0.371.
- **Compared on screen**: a screenshot of the composited page matched the canvas's
  pixels exactly over the visible stage at device pixel ratios 1, 1.25, 1.5 and 2 (real
  ratios, `--force-device-scale-factor`), for "This screen" and for fixed 1920 × 1080 and
  1280 × 720; with Scaled to fit the same comparer fails, as it should. Embed mode at 1 and
  2: the screenshot is the canvas. Every control kind moved through the DOM changes the
  picture; a window resize re-rasters and the plugin logs the new size. No console
  messages on Metal; on SwiftShader Chrome logs four "GPU stall due to ReadPixels"
  performance warnings, as zoetrope's and fiche's pages do.
- **Gaps, all said on the page**: the browser's clock (the Frame Counter counts this
  browser's frames, so the latency use needs the real output); colours through an 8-bit
  swatch; integers as number fields; Text sent per keystroke; the About buttons'
  `std::system()` does nothing in a browser, so the page opens the plugin's own URLs; a
  desktop scaled resolution resamples after the browser, which no page can see.
- **Left to the release**: the proxied AAAA 100:: record for `graticule-demo` (the zone's
  custom domains are full, so `wrangler.toml` uses a route), `CLOUDFLARE_API_TOKEN` and
  `CLOUDFLARE_ACCOUNT_ID` on the repo, the first deploy by hand, `graticule` on
  `resolume-demo/sync.sh`'s list, and the website's `demo` field.

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
  Resolume saves, and OSC addresses, parameters by name too (lower-cased, spaces
  removed), so a rename after a release breaks saved compositions.
- **A colour's red component carries the swatch's name.** Resolume draws a RED/GREEN/BLUE
  run as one swatch labelled with the red parameter's name and hides the other two, so
  the red one is "Line Colour" (or plain "Background", which would be 17 characters
  with " Colour") and never "Line Red". Renamed before v0.1.0, when it was still free.
- `patch`, `sample`, `input`, `output`, `filter`, `common`, `active`, `half` are GLSL
  reserved words. `patch` cost a build here. Shader errors surface only at runtime, in the
  diagnostics log, and in `gttest --pixels` as `InitGL FAILED`.
- `oxbow selftest` is the only check that proves the bundle actually registers a plugin. A
  clean build and a green test run do not. Unlike gridiron, this source draws with no input
  and no file, so oxbow's PASS verdict is meaningful here and `verify.sh` requires it.
- Anything the release job does that can be done locally goes in `tools/verify.sh`.
- **CI has no GPU, and a new GL program is a compile.** Under the runner's software
  renderer the first draw of every program compiles the whole pattern shader, over 20 s,
  while a 160x90 frame after it costs hundredths of a second. The sweep once started a
  `gttest` per render and timed the CI job out on every push from 2026-09-17 to
  2026-10-12. Anything that renders many frames shares one program (`gttest --batch`);
  `GTTEST_SOFTWARE=1` reproduces the runner locally.

## Generated, do not edit

- **`ATTRIBUTIONS.md`** and the README block between the `attributions` markers come from
  `stoatworks-backend/scripts/sync-attributions.py`; **`source/StoatworksAbout.h`** from
  `sync-about.py`; the README block between the `downloads` markers from
  `release/gen-downloads.py`. Change the masters there and re-run the sync.
- The About block's size follows the header: when a sync adds or drops a link,
  `PT_ABOUT_BUTTON_n` in `Controls.h` has to follow, and the static_assert in
  `Graticule.cpp` says so. The guide link added the fourth button before v0.1.0.
