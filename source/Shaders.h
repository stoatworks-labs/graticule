#pragma once

/**
    The GLSL, as string literals.

    `#version 410 core` because that is macOS's ceiling and the rest of the
    fleet already sits there.

    ## One fragment shader, one full-screen triangle

    A test pattern is a function from a pixel coordinate to a colour, and that
    is exactly what a fragment shader is. There is no geometry to speak of: the
    vertex shader emits one triangle that covers the viewport from
    `gl_VertexID`, so nothing is bound at draw time but a VAO and the font
    texture. Every pattern branches on `uPattern` inside `main`; the cost of a
    switch per pixel is nothing next to the cost of maintaining nine programs.

    ## Integer pixels, top-down

    The first thing the fragment shader does is turn `gl_FragCoord` into an
    integer pixel with y = 0 at the TOP, matching Test Card's canvas and the
    way every test-pattern description is written ("the first row is 7/12 of
    the height"). Everything after that is integer arithmetic on whole pixels:
    a line of width 1 is exactly one column of pixels, and a 1px checkerboard is
    exactly alternating pixels. There is no anti-aliasing anywhere, on purpose --
    a blurred edge on a pixel-mapping pattern must mean a scaler in the chain,
    never the pattern.

    ## Negative operands

    GLSL leaves `%` and `/` undefined when either operand is negative, and the
    grid with its origin at the centre and the LED wall with an offset origin
    both index negative cells. `idiv` and `imod` below floor through float, which
    is exact for the magnitudes a raster reaches.

    ## Text

    All text is drawn here from a 5x7 bitmap texture (Font.cpp). Labels that
    depend on the pixel -- a cell's coordinates, a tile's reference -- are built
    into a small local buffer by `tInt` and `tCol`; the burn-in arrives as a
    uniform int array the CPU filled from the operator's text.
*/
namespace graticule
{
extern const char* const kVertexShader;
extern const char* const kFragmentShader;

} // namespace graticule
