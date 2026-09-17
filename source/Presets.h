#pragma once

/**
    Factory presets: a whole pattern an operator can reach in one gesture.

    A preset is a *job* -- "the bars, legal range, for the SDI chain", "a wall
    of 500 mm P2.6 cabinets", "the latency sweep" -- rather than a set of
    slider positions. Each row sets the pattern and every control that decides
    the geometry of that pattern, and leaves the colours, the burn-in and the
    per-pattern decorations to the operator.

    **Presets are an OVERRIDE, not a write.** Resolume does not consume value
    events, so a plugin cannot push a preset's values back into the inspector;
    if it changes its own parameters the sliders keep showing the old numbers.
    So while the dropdown is on anything but Custom, the row's values are laid
    over the operator's every frame at read time, and the inspector is, for
    those columns, not the truth. Element 0 of the dropdown is Custom and is
    not in this table: it means "the controls are the truth".

    **Standard parameters hold the host-facing 0..1**; **integer, option and
    boolean parameters hold their real value**, because `SetParamInfo`'s 0..1
    clamp is guarded by the parameter type. `tools/check_presets.py` fails a
    row with a fraction in a discrete column, which would otherwise round to
    the column's floor without a word.

    ## The first row is also the constructor's defaults

    `SMPTE RP 219` and the defaults in `Graticule.cpp` are the same pattern,
    written twice, and `gttest --defaults` is the test that fails when they go
    out of step. The fleet learned why on escapement: a build shipped whose
    defaults had been left behind by a retuned preset, and nothing in the test
    suite noticed because every test set its own parameters first.

    ## The LED rows

    Cabinet pixel counts are pitch arithmetic: a 500 mm cabinet at P2.6 is
    192 pixels (500 / 2.604), at P2.9 is 168 (500 / 2.976), at P3.9 is 128
    (500 / 3.906). The module lines split each of those into 250 mm quarters,
    which is the common module for those pitches. They are starting points; a
    real wall's numbers come off the processor's cabinet map, and Tile Width
    and Tile Height are free integers precisely so that they can be typed in.
*/

namespace graticule
{
namespace presets
{
enum Param
{
	kPattern,
	kLevels,
	kGridMode,
	kPitch,
	kDivX,
	kDivY,
	kMajorEvery,
	kTileW,
	kTileH,
	kModuleW,
	kModuleH,
	kSteps,
	kCell,
	kMotion,
	kSpeed,
	kMarkerSize,
	kFrameCounter,
	kParamCount
};

struct Preset
{
	const char* name;
	float       v[ kParamCount ];
};

inline constexpr Preset kPresets[] = {
	//   name                   pat lev gm  pitch dx  dy  maj  tw   th   mw  mh  steps cell mot speed  mark fc
	{ "SMPTE RP 219",        {   0,  0,  0, 100, 16,  9,  5, 192, 192, 96, 96, 11,   1,   0, 0.5f, 16,  0 } },
	//The defaults, and the pattern most people want first.

	{ "SMPTE legal",         {   0,  1,  0, 100, 16,  9,  5, 192, 192, 96, 96, 11,   1,   0, 0.5f, 16,  0 } },
	//The same bars at 16..235, for an SDI chain. Sub-black survives, so the
	//pluge means something. On a full-range display it looks washed out,
	//correctly.

	{ "75% bars",            {   1,  0,  0, 100, 16,  9,  5, 192, 192, 96, 96, 11,   1,   0, 0.5f, 16,  0 } },
	//Eight equal bars, full height. Survives being squeezed onto a narrow LED
	//strip where RP 219's four rows would be unreadable.

	{ "Grid 100 px",         {   3,  0,  0, 100, 16,  9,  5, 192, 192, 96, 96, 11,   1,   0, 0.5f, 16,  0 } },
	//A hundred-pixel pitch with every fifth line heavy: count the heavy lines
	//and you have the raster in five hundreds.

	{ "Grid 16 x 9",         {   3,  0,  1, 100, 16,  9,  0, 192, 192, 96, 96, 11,   1,   0, 0.5f, 16,  0 } },
	//Divisions, so the last line lands exactly on the far edge whatever the
	//raster. A grid whose right-hand column is a few pixels narrower than the
	//rest is the classic sign of rounding, and this one cannot show it.

	{ "Alignment",           {   4,  0,  0, 100, 16,  9,  5, 192, 192, 96, 96, 11,   1,   0, 0.5f, 16,  0 } },
	//Edge border, corner brackets with a pixel count, centre target, safe
	//areas, and single-pixel checker patches that go flat grey under a scaler.

	{ "LED 500 mm P2.6",     {   5,  0,  0, 100, 16,  9,  5, 192, 192, 96, 96, 11,   1,   0, 0.5f, 16,  0 } },
	{ "LED 500 mm P2.9",     {   5,  0,  0, 100, 16,  9,  5, 168, 168, 84, 84, 11,   1,   0, 0.5f, 16,  0 } },
	{ "LED 500 mm P3.9",     {   5,  0,  0, 100, 16,  9,  5, 128, 128, 64, 64, 11,   1,   0, 0.5f, 16,  0 } },
	{ "LED 1000 mm P2.6",    {   5,  0,  0, 100, 16,  9,  5, 192, 384, 96, 96, 11,   1,   0, 0.5f, 16,  0 } },
	//One cell per cabinet, lettered and numbered. Set Origin X/Y to where the
	//wall's top-left cabinet sits in the raster.

	{ "Grey 11-step",        {   7,  0,  0, 100, 16,  9,  5, 192, 192, 96, 96, 11,   1,   0, 0.5f, 16,  0 } },
	//Eleven flat patches from reference black to reference white. Steps = 0
	//is a continuous ramp.

	{ "Pixel check",         {   8,  0,  0, 100, 16,  9,  5, 192, 192, 96, 96, 11,   1,   0, 0.5f, 16,  0 } },
	//A single-pixel checkerboard beside single-pixel line bursts. If any of
	//the three renders as flat grey on the real output, something in the
	//chain is scaling.

	{ "Latency sweep",       {   3,  0,  0, 100, 16,  9,  5, 192, 192, 96, 96, 11,   1,   1, 0.5f, 16,  1 } },
	//The grid with a bar crossing it once a second and the frame counter on.
	//Film the wall and the monitor together and the offset between the two
	//counters is the latency, to the frame.
};

inline constexpr int kCount = static_cast< int >( sizeof( kPresets ) / sizeof( kPresets[ 0 ] ) );

} // namespace presets
} // namespace graticule
