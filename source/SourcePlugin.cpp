/**
	The FF_SOURCE registration, and nothing else.

	**This file is listed directly in the source target, not in the shared
	object library.** `CFFGLPluginInfo` registers itself from a file-scope
	constructor and nothing ever references it by name, so in a static archive
	the linker is entitled to drop the whole translation unit -- giving a bundle
	that loads, exports `plugMain`, and reports that it contains no plugins.

	    nm -gU Graticule.bundle/Contents/MacOS/Graticule | grep plugMain

	That is also why the shared code is an OBJECT library rather than a STATIC
	one, and it is why `oxbow selftest` is the only check that proves a bundle
	actually registers anything.

	`GT01`: four characters, unique across the fleet. `GR01` is gridiron's,
	`GF01` gaffer's.
*/
#include "Graticule.h"

static CFFGLPluginInfo PluginInfo(
	PluginFactory< graticule::GraticulePlugin >,             // Create method
	"GT01",                                                  // Plugin unique ID of maximum length 4
	"Graticule",                                             // Plugin name
	2,                                                       // API major version number
	1,                                                       // API minor version number
	0,                                                       // Plugin major version number
	1,                                                       // Plugin minor version number
	FF_SOURCE,                                               // Plugin type
	"Test patterns and LED grids, generated live at the output's own raster.\n\n"
	"SMPTE RP 219 bars with the geometry and colours measured from a real generator, plain 75% and 100% bars, "
	"a pixel-pitch or division grid with heavy lines and cell labels, an alignment chart with edge border, "
	"corner brackets, safe areas and single-pixel patches, an LED wall of lettered cabinets with module lines, "
	"solid fields, a greyscale ramp or stepped wedge, and a single-pixel checker beside line bursts.\n\n"
	"Every line is a whole number of pixels and nothing is anti-aliased, so a soft edge on the wall means a "
	"scaler in the chain. Levels switches between full range and legal 16-235. A motion marker and a frame "
	"counter turn any pattern into a latency and dropped-frame check. Start from a Preset.",// Plugin description
	"Graticule FFGL source"                                  // About
);

extern "C" const char* GraticuleSourceBuildStamp()
{
	return "graticule " GRATICULE_VERSION " source, built " __DATE__ " " __TIME__;
}
