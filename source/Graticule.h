#pragma once

#include <string>

#include "Clock.h"
#include "Controls.h"
#include "Render.h"

#include <FFGLSDK.h>

/**
    The plugin: test patterns and LED grids, generated live at the output's
    own raster.

    This class is the wiring. The parts that decide anything are elsewhere and
    are testable without a host:

      `Bars`     the RP 219 geometry and the Rec.709 colour maths
      `Font`     the 5x7 glyphs every label is drawn from
      `Shaders`  the pixel function -- every pattern, every line, every label
      `Render`   one triangle into the host's framebuffer

    ## Per frame

    `BuildFrame` turns the parameter cache into a `Frame` in the shader's
    units: it lays out the bars for the current raster, places the motion
    marker from the clock, and composes the burn-in text. All of it is a few
    hundred integer operations; nothing is cached because nothing is expensive.
*/
namespace graticule
{
class GraticulePlugin : public CFFGLPlugin
{
public:
	GraticulePlugin();
	~GraticulePlugin() override = default;

	FFResult InitGL( const FFGLViewportStruct* vp ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* pGL ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float    GetFloatParameter( unsigned int index ) override;

	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	char*    GetTextParameter( unsigned int index ) override;

	FFResult SetTime( double time ) override;

	/// The Frame the next ProcessOpenGL would draw at this size, for the
	/// harness: the same code path, without a context.
	Frame BuildFrame( int width, int height, double seconds, long frameIndex ) const;

	/// The effective value of a parameter: the operator's, or the preset's
	/// where the preset dropdown is on anything but Custom.
	float Effective( unsigned int index ) const;

	/// For the harness, which sends seconds and wants the marker where the
	/// arithmetic says it is on the first frame rather than after the clock
	/// has voted.
	void ForceSecondsClock() { mClock.ForceSeconds(); }

private:
	int OptionIndex( unsigned int param, int count ) const;

	float mParams[ PT_COUNT ] = {};

	std::string mText;///< the burn-in's custom text

	Renderer mRenderer;
	Clock    mClock;

	/// Which instance this is within the host process; several clips of this
	/// plugin can live in one composition and their log lines would otherwise
	/// interleave into one account of a plugin that contradicts itself.
	int         mInstanceId = 0;
	std::string mTag;

	long   mFrame        = 0;
	bool   mGlReady      = false;
	bool   mHostTimeSeen = false;
	double mHostTime     = 0.0;
};

} // namespace graticule
