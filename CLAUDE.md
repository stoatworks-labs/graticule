# graticule

Test patterns and LED cabinet grids as an FFGL **source** for Resolume Arena/Avenue.
C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. Public MIT repo.

Read `AGENTS.md` before changing a pattern, the presets or the parameter list.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install into Arena: `cmake --install build`
- Render a frame offline: `./build/gttest --out /tmp/f.png --size 1920x1080 --set "Preset=7"`
- Set anything by name: `--set "Pattern=3" --set "Pitch=120" --set "Text=HELLO"`
- List parameters: `./build/gttest --list`

## Verify
- Everything: `tools/verify.sh` (fresh universal build + every check, ~30 s)
- The glyphs, printed: `./build/gttest --font`
- The RP 219 colours land on the measured code values: `./build/gttest --colours`
- The RP 219 layout matches ffmpeg at four rasters: `./build/gttest --bars tools/fixtures/rp219-ffmpeg.json`
- Preset 1 IS the constructor's defaults: `./build/gttest --defaults`
- No name over 16 characters: `./build/gttest --names`
- Specific pixels of every pattern: `./build/gttest --pixels`
- No dead controls: `python3 tools/sweep.py` (`--size WxH`, `--jobs N`)
- Preset rows the right width and kind: `python3 tools/check_presets.py`

## Notes
- **The shader decides every pixel**; `Graticule.cpp` only converts parameters into the
  shader's units (integer pixels, y = 0 at the TOP). A wrong pattern is a GLSL fix.
- **Nothing is anti-aliased**, on purpose. A 1px line is one column of pixels.
- **GLSL `%` and `/` are undefined on negative operands.** Use `imod`/`idiv` for anything
  that can go negative (centre-origin grids, offset tile origins).
- **`patch` is a GLSL reserved word.** So are `sample`, `input`, `output`, `filter`,
  `common`, `active`, `half`. A shader that will not compile is `InitGL FAILED` in
  `gttest --pixels` and a black clip in the host.
- **Parameter names must be unique** — `--set` and the sweep find them by name.
- **Bars are laid out on the CPU** (`Bars.cpp`, ported from Test Card) and handed to the
  shader as up to 48 rectangles; the ramp is a flag on its band.
- **Colour bars, fields, wedges and the checker go through `toOut()`** for the level
  range; grid, tile and alignment colours are the operator's RGB and do not.
- **The burn-in plate sits top-left by default**, so pixel checks set `Burn-in=0`
  (`Instance::quiet()`) before probing the top-left corner.
- **Presets are an OVERRIDE, not a write** — Resolume does not consume value events.
  `Effective()` is the one place that reads them.
- `SetParamInfo` clamps a STANDARD default into 0..1; `FF_TYPE_INTEGER` is exempt, which
  is why pixel counts are declared as real integers with real ranges.
- Override `SetTextParameter` to return FF_SUCCESS for the About block, or no host can
  instantiate the plugin at all.
- `graticule_core` is an OBJECT library, not STATIC — the plugin registers itself from a
  file-scope constructor nothing references by name.
- macOS build must be universal. Verify with `lipo`, never the build log.
- FFGL id is `GT01`.
- Public repo. "Commit" = commit **and** push.

## Not done yet
- Not yet instantiated inside Arena (installed into Extra Effects 2026-09-17).
- No release tag; not registered on the website; `StoatworksAbout.h` and `ATTRIBUTIONS.md`
  are provisional hand copies.
- No browser demo.

## Diagnostics

`source/Diag.{h,cpp}` — log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/graticule/graticule.YYYY-MM-DD.log
