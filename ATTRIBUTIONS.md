# Attributions

Graticule is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### Test patterns, Rec. 709 colour maths and the RP 219 layout — Stoatworks test-card

<https://github.com/stoatworks-labs/test-card>  
Licence: MIT  
Copyright: Stoatworks Labs

The pattern definitions, the Rec. 709 colour maths and the SMPTE RP 219 layout are ported from Test Card's TypeScript, and tools/fixtures/rp219-ffmpeg.json is its measurement of ffmpeg's smptehdbars. Graticule is the live version of the same patterns.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### FFmpeg smptehdbars

<https://ffmpeg.org>  
Licence: LGPL-2.1-or-later / GPL

Not linked, not shipped. Its output was measured once, in Test Card, to pin the RP 219 geometry and code values; the measurement is what this repo tests against.

## Standards and published specifications

What the implementation is measured against.

- **SMPTE RP 219** — The colour bar layout, ported from Test Card, with its geometry and code values pinned by Test Card's measurement of FFmpeg's smptehdbars.
- **ITU-R BT.709** — The Rec. 709 colour maths, ported from Test Card.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
