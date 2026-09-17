#pragma once

#include <vector>

#include "Controls.h"

/**
    Colour bars: the layout and the colours, on the CPU, as plain arithmetic.

    Ported from Test Card (stoatworks-labs/test-card, `src/patterns/rp219.ts`
    and `src/lib/colour.ts`), the static-PNG sibling of this plugin, so the two
    produce the same bars to the pixel and to the code value. What follows is
    that module's provenance, because it is the reason to trust the numbers.

    ## Where the geometry came from

    SMPTE RP 219 is a paywalled standard and every free description of it is a
    redrawing of a redrawing. Test Card instead rendered ffmpeg's `smptehdbars`
    at 1920x1080, 3840x2160, 1280x720 and 2048x1080, run-length analysed the
    rasters, and recovered the band boundaries and the Y'CbCr code values. All
    four sizes -- including the non-16:9 DCI one -- agreed on the same
    fractions, so this is a proportional model, not a 1920-specific one. The
    measurement is committed at `tools/fixtures/rp219-ffmpeg.json` and
    `gttest --bars` asserts against it.

      Rows, as fractions of the height:    7/12, 1/12, 1/12, 3/12
      Columns, as fractions of the width:  a = 1/8 (side blocks), b = 3/28 (a bar)
      2a + 7b = 1/4 + 3/4 = 1, exactly.

      Row A  40% grey | 75% white, yellow, cyan, green, magenta, red, blue | 40% grey
      Row B  100% cyan | +I | 75% white (6b) | 100% blue
      Row C  100% yellow | +Q | luma ramp (6b) | 100% red
      Row D  15% grey | black (3b/2) | 100% white (2b) | black (5b/6)
             | pluge -2%, 0%, +2%, 0%, +4% (b/3 each) | black (b) | 15% grey

    ## Where the colours came from

    Every value except +I and +Q is COMPUTED from its R'G'B' definition through
    the Rec.709 matrix. "75% yellow is (0.75, 0.75, 0)" is a statement anyone
    can check; "75% yellow is Y=168 Cb=44 Cr=136" is three numbers taken on
    faith. The computed values land exactly on the measured ffmpeg ones -- all
    fifteen, to the code value -- which is itself the evidence the measurement
    is sound. +I and +Q are the NTSC chroma reference signals, not colours, and
    are the one pair carried as measured constants.

    ## Two traps Test Card documents, kept here because they are easy to re-make

    - `ffmpeg -pix_fmt rgb24` converts HD bars with the BT.601 matrix and
      renders 75% yellow as (189, 202, 7). It looks like bars. It is not bars.
    - The Y'CbCr values are studio swing. Written straight into a full-range
      output, black is grey and white is dull; expanded when the chain expects
      studio swing, the pluge clips. So the level range is the operator's choice
      (`Levels`), and the -2% pluge patch is invisible at full range BY DESIGN.

    ## One deliberate difference from ffmpeg

    ffmpeg rounds each bar up and dumps the remainder into the last column, so
    its right-hand grey block is up to 6px narrower than its left. `LayOut`
    rounds each cumulative boundary to nearest, keeping the two blocks equal
    and every edge within half a pixel of nominal; an asymmetric test pattern is
    a defect. The conformance test therefore requires the ROW boundaries to
    match ffmpeg exactly and allows columns to differ by up to 8px, while
    separately asserting that the bands tile the raster with no gap.
*/
namespace graticule::bars
{
/// 8-bit Y'CbCr, studio swing: Y' 16..235, Cb/Cr 16..240 centred on 128.
struct Ycbcr
{
	double y, cb, cr;
};

/// Gamma-encoded R'G'B' normalised so 0 = reference black and 1 = reference
/// white. May sit outside 0..1: the pluge is below black on purpose.
struct Unit
{
	double r, g, b;
};

constexpr double kLumaBlack  = 16.0;
constexpr double kLumaWhite  = 235.0;
constexpr double kLumaSpan   = kLumaWhite - kLumaBlack;// 219
constexpr double kChromaSpan = 224.0;

/// Rec.709 forward matrix, R'G'B' 0..1 to studio-swing Y'CbCr.
Ycbcr RgbToYcbcr( double r, double g, double b );

/// Rec.709 inverse, to normalised R'G'B'.
Unit YcbcrToUnit( const Ycbcr& c );

/// A neutral grey at `pct` of the black-to-white span. -2 is legal.
Ycbcr GreyPct( double pct );

/// Normalised 0..1 (or beyond) to an 8-bit code for the chosen range. Full
/// range clamps to 0..1 first; legal range keeps footroom and headroom.
int UnitToCode( double v, Levels range );

enum class Fill
{
	Flat,
	Ramp///< horizontal luma ramp, black at the left edge to white at the right
};

struct Band
{
	char        row;
	const char* name;
	int         x, y, w, h;
	Fill        fill;
	Unit        colour;///< meaningful for Flat
};

struct Segment
{
	int start, size;
};

/// Lay proportional `weights` onto `total` pixels. Boundaries are accumulated
/// exactly and rounded once, so the segments always tile `total` with no gap
/// and no overlap and every edge is within half a pixel of nominal.
std::vector< Segment > LayOut( const std::vector< double >& weights, int total );

/// The full RP 219 band list for a width x height raster. Any aspect ratio is
/// accepted: the pattern is proportional and stretches rather than pillarboxes,
/// which is right for a media-server output where the raster IS the frame.
std::vector< Band > Rp219Layout( int width, int height );

/// Eight equal full-height bars -- white, yellow, cyan, green, magenta, red,
/// blue, black -- at `amplitudePct` of full scale. Not RP 219; the everyday
/// "colour bars", and they survive a narrow LED strip where four rows would not.
std::vector< Band > BarsLayout( int width, int height, double amplitudePct );

/// The largest band count either layout produces; sizes the shader's arrays.
constexpr int kMaxBands = 48;

/// The named colours, for the conformance test.
struct Palette
{
	Ycbcr grey40, grey15, black, white100, white75;
	Ycbcr yellow75, cyan75, green75, magenta75, red75, blue75;
	Ycbcr cyan100, blue100, yellow100, red100;
	Ycbcr plusI, plusQ;
	Ycbcr plugeMinus2, plugePlus2, plugePlus4;
};
const Palette& Colours();

} // namespace graticule::bars
