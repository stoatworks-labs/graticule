#pragma once

#include <cmath>

#include <FFGLSDK.h>

#include "StoatworksAboutParams.h"

/**
    Every parameter, and what its host-side value means.

    ## The two units

    FFGL hands a plugin floats. What a float *means* depends on the type the
    parameter was declared with, and the fleet has been bitten by conflating
    them often enough that the rule is written here rather than remembered:

    - **`FF_TYPE_STANDARD` is 0..1**, always. `SetParamInfo` clamps the default
      into that range before `SetParamRange` could widen it, and there is no
      `SetParamDefault`, so a standard parameter that wants to mean anything else
      is mapped here, in a named inline function, and nowhere else.
    - **`FF_TYPE_INTEGER` holds the real integer.** The clamp is guarded by
      `if( pType == FF_TYPE_STANDARD )`, so an integer passes through untouched
      and a pitch of 100 pixels can be declared as 100.0f with a range of
      2..1024. This is why nearly everything here that is a pixel count is an
      integer: an operator lining up an LED wall wants to type 192, not find
      0.0937 on a slider.
    - **`FF_TYPE_OPTION` holds the element VALUE**, which for every dropdown here
      is its index. `ToOption` in Graticule.cpp accepts either that or a
      normalised 0..1 from a host that does it the other way.
    - **`FF_TYPE_BOOLEAN` is 0 or 1**, read as `> 0.5f`.

    ## Order is load-bearing

    The host draws parameters in declaration order, and `SetParamGroup`
    collapses *runs* of the same group name into one fold. An id moved out of
    its run splits its group in two in the inspector. The enum below is the
    inspector, top to bottom.

    ## Names

    FFGL truncates a parameter name at 16 characters, in the host, silently.
    `gttest --names` lists any that are over.
*/
namespace graticule
{
enum ParamId : FFUInt32
{
	// -- Pattern ------------------------------------------------------------
	PT_PRESET,///< factory preset; element 0 is "Custom"
	PT_PATTERN,
	PT_LEVELS,///< full 0..255 or legal 16..235

	// -- Grid ---------------------------------------------------------------
	PT_GRID_MODE,///< pitch in pixels, or a division count
	PT_PITCH,
	PT_DIV_X,
	PT_DIV_Y,
	PT_LINE_W,
	PT_MAJOR_EVERY,
	PT_MAJOR_W,
	PT_DIAGONALS,
	PT_CENTRE,
	PT_ORIGIN_CENTRE,
	PT_CELL_LABELS,
	PT_LINE_R,
	PT_LINE_G,
	PT_LINE_B,
	PT_MAJOR_R,
	PT_MAJOR_G,
	PT_MAJOR_B,
	PT_BG_R,
	PT_BG_G,
	PT_BG_B,

	// -- LED Tiles ----------------------------------------------------------
	PT_TILE_W,
	PT_TILE_H,
	PT_MODULE_W,///< 0 = no module lines
	PT_MODULE_H,
	PT_ORIGIN_X,
	PT_ORIGIN_Y,
	PT_CHECKER,
	PT_TILE_LABELS,
	PT_BORDER_W,
	PT_TILE_A_R,
	PT_TILE_A_G,
	PT_TILE_A_B,
	PT_TILE_B_R,
	PT_TILE_B_G,
	PT_TILE_B_B,
	PT_BORDER_R,
	PT_BORDER_G,
	PT_BORDER_B,

	// -- Solid --------------------------------------------------------------
	PT_FIELD,
	PT_LEVEL,
	PT_CUSTOM_R,
	PT_CUSTOM_G,
	PT_CUSTOM_B,

	// -- Greyscale ----------------------------------------------------------
	PT_STEPS,///< 0 = continuous ramp
	PT_VERTICAL,
	PT_PER_CHANNEL,

	// -- Pixel Check --------------------------------------------------------
	PT_CELL,
	PT_BURSTS,

	// -- Alignment ----------------------------------------------------------
	PT_EDGE_BORDER,
	PT_CORNERS,
	PT_CORNER_SIZE,
	PT_TARGET,
	PT_SAFE_A,///< percent inset, 0 = off
	PT_SAFE_B,
	PT_PATCHES,
	PT_ALIGN_DIAG,

	// -- Motion -------------------------------------------------------------
	PT_MOTION,
	PT_SPEED,
	PT_MARKER_SIZE,
	PT_FRAME_COUNTER,

	// -- Burn-in ------------------------------------------------------------
	PT_BURNIN,
	PT_SHOW_RES,
	PT_SHOW_PATTERN,
	PT_TEXT,
	PT_TEXT_POS,
	PT_TEXT_SIZE,///< glyph scale; 0 = from the raster
	PT_PLATE,

	// -- About --------------------------------------------------------------
	// The Stoatworks About block: one text line and one button per link. Its
	// size is decided by StoatworksAbout.h at compile time, so Graticule.cpp
	// static_asserts this run against `about::kParamCount` -- add or remove a
	// PT_ABOUT_BUTTON_n when the generated header gains or loses a link.
	PT_ABOUT_TEXT,
	PT_ABOUT_BUTTON_1,
	PT_ABOUT_BUTTON_2,
	PT_ABOUT_BUTTON_3,

	PT_COUNT
};

enum class Pattern : int
{
	Smpte = 0,///< SMPTE RP 219
	Bars75,
	Bars100,
	Grid,
	Alignment,
	LedTiles,
	Solid,
	Greyscale,
	PixelCheck,
	Count
};
constexpr int kPatternCount = static_cast< int >( Pattern::Count );

/// The names the dropdown shows, and the burn-in prints. Indexed by Pattern.
inline const char* PatternName( Pattern p )
{
	switch( p )
	{
	case Pattern::Smpte: return "SMPTE RP 219";
	case Pattern::Bars75: return "75% BARS";
	case Pattern::Bars100: return "100% BARS";
	case Pattern::Grid: return "GRID";
	case Pattern::Alignment: return "ALIGNMENT";
	case Pattern::LedTiles: return "LED TILES";
	case Pattern::Solid: return "SOLID";
	case Pattern::Greyscale: return "GREYSCALE";
	case Pattern::PixelCheck: return "PIXEL CHECK";
	default: return "";
	}
}

enum class Levels : int
{
	Full = 0,
	Legal
};

enum class GridMode : int
{
	Pitch = 0,
	Divisions
};

enum class Field : int
{
	White = 0,
	Black,
	Red,
	Green,
	Blue,
	Cyan,
	Magenta,
	Yellow,
	Grey50,
	Grey18,
	Custom,
	Count
};
constexpr int kFieldCount = static_cast< int >( Field::Count );

enum class Motion : int
{
	Off = 0,
	SweepAcross,
	SweepDown,
	Bounce,
	Count
};
constexpr int kMotionCount = static_cast< int >( Motion::Count );

enum class TextPos : int
{
	TopLeft = 0,
	TopCentre,
	Centre,
	BottomCentre,
	BottomLeft,
	Count
};
constexpr int kTextPosCount = static_cast< int >( TextPos::Count );

// ---------------------------------------------------------------------------
// The 0..1 controls, in engineering units.
// ---------------------------------------------------------------------------

/// Marker speed in raster crossings per second. Centred so the default of 0.5
/// is exactly one crossing a second -- the number a latency measurement wants
/// to be able to count against a clock -- with an eighth at one end and eight
/// at the other.
inline float CrossingsPerSecond( float v )
{
	return static_cast< float >( std::pow( 2.0, ( static_cast< double >( v ) - 0.5 ) * 6.0 ) );
}

/// Solid field level as a fraction of full scale; the slider is the percentage.
inline float FieldLevel( float v )
{
	return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

} // namespace graticule
