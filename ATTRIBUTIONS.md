# Attributions

Graticule is built on other people's work. This file lists what that work is, who
did it, and what it is doing here.

> **Provisional.** Across the fleet this file is generated from master lists in
> `stoatworks-backend` by `scripts/sync-attributions.py`. Graticule is not
> registered there yet, so this copy is hand-written. Register it before release
> — and note that the script's `--only` flag truncates the file rather than
> filtering it.

## Third-party code this project uses

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>
Licence: BSD-3-Clause
Copyright: FreeFrame

Vendored as a git submodule at `external/ffgl`, pinned to `b1afaf9`.

The plugin ABI itself. An FFGL source is defined by this SDK's headers — there is
no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Windows only, from vcpkg, statically linked. The SDK's headers pull it in for the
OpenGL function pointers; macOS uses the system OpenGL framework instead.

## Work from elsewhere in the fleet

### Test Card

<https://github.com/stoatworks-labs/test-card>
Licence: MIT
Copyright: Stoatworks Labs

The pattern definitions, the Rec. 709 colour maths and the SMPTE RP 219 layout
are ported from Test Card's TypeScript, and `tools/fixtures/rp219-ffmpeg.json` is
its measurement of ffmpeg's `smptehdbars`. Graticule is the live version of the
same patterns.

## Measurement reference

### FFmpeg `smptehdbars`

<https://ffmpeg.org>
Licence: LGPL-2.1-or-later / GPL

Not linked, not shipped. Its output was measured once, in Test Card, to pin the
RP 219 geometry and code values; the measurement is what this repo tests against.
