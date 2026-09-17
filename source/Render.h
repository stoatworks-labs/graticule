#pragma once

#include <string>
#include <vector>

#include "Bars.h"

#include <FFGLSDK.h>

/**
    The draw: one program, one triangle, one font texture, into the host's FBO.

    Everything that decides what a pixel is lives in the fragment shader; this
    class only carries the answer to "what are the settings this frame" across
    to it. `Frame` is that answer, built by the plugin from its parameters (and
    by the harness directly), in the units the shader wants -- integer pixels,
    top-down -- so the conversion from host-side floats happens in exactly one
    place, Graticule.cpp.

    ## No framebuffer of its own

    A test pattern is opaque and has no depth, so unlike gridiron there is
    nothing to composite: the triangle is drawn straight into `HostFBO` at the
    viewport size the host reports.

    ## Restoring state

    Every `ffglex::Scoped*` binding clears to 0 on scope exit rather than
    restoring what was there, so they are no help for handing the context back
    to a host that had its own state. The state this touches is put back by
    hand at the end of `Draw`.
*/
namespace graticule
{
struct Frame
{
	int width = 0, height = 0;
	int  pattern = 0;
	bool legal   = false;

	// Grid
	int   gridMode = 0, pitch = 100, divX = 16, divY = 9, lineW = 1, majorEvery = 5, majorW = 3;
	bool  diagonals = true, centre = true, originCentre = false, cellLabels = false;
	float lineCol[ 3 ] = { 0.24f, 0.49f, 1.0f }, majorCol[ 3 ] = { 1, 1, 1 }, bgCol[ 3 ] = { 0, 0, 0 };

	// LED tiles
	int   tileW = 192, tileH = 192, moduleW = 96, moduleH = 96, originX = 0, originY = 0, borderW = 1;
	bool  checker = true, tileLabels = true;
	float tileA[ 3 ] = { 0.063f, 0.063f, 0.063f }, tileB[ 3 ] = { 0.165f, 0.165f, 0.165f }, borderCol[ 3 ] = { 1, 1, 1 };

	// Solid, in unit space
	float solid[ 3 ] = { 1, 1, 1 };

	// Greyscale
	int  steps = 11;
	bool vertical = false, perChannel = false;

	// Pixel check
	int  cell   = 1;
	bool bursts = true;

	// Alignment
	bool edgeBorder = true, corners = true, target = true, patches = true, alignDiag = true;
	int  cornerSize = 100, safeA = 10, safeB = 20;

	// Colour bars, for patterns 0..2
	std::vector< bars::Band > bands;

	// Motion marker: x, y, w, h in top-down pixels; w = 0 for none.
	int marker[ 4 ] = { 0, 0, 0, 0 };

	// Burn-in
	struct Line
	{
		int offset, length, x, y;
	};
	int                 textScale = 1;
	std::vector< int >  text;
	std::vector< Line > lines;
	int                 plate[ 4 ] = { 0, 0, 0, 0 };
};

class Renderer
{
public:
	Renderer() = default;

	bool InitGL();
	void DeInitGL();
	bool Ready() const { return mReady; }

	void Draw( const Frame& frame, GLuint hostFBO );

	const std::string& Note() const { return mNote; }

private:
	ffglex::FFGLShader mProgram;
	GLuint             mVao  = 0;
	GLuint             mFont = 0;

	bool        mReady = false;
	std::string mNote;
};

} // namespace graticule
