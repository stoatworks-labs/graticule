/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call.

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Graticule.cpp` and everything it calls (Render, Shaders,
    Bars, Font, Clock, Diag), compiled UNMODIFIED into the same WebAssembly
    module as this file, with the FFGL SDK pieces it needs (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs a `GraticulePlugin`, as SourcePlugin.cpp's registration
        would through CFFGLPluginInfo, and calls InitGL / DeInitGL;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (GetParamName, GetParamType, GetParamGroup,
        GetParamDefault, the element and range getters -- the SDK compiled
        unmodified too), so the page's panel is built from the plugin's
        constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, SetTextParameter
        (the burn-in's Text), Resize when the composition's raster changes
        (FF_RESIZE, as Resolume sends it), and each frame SetTime and
        ProcessOpenGL with no input -- Graticule is a source and declares
        none -- in the order tools/gttest makes them.

    The clock is the page's: SetTime is handed the kit's seconds, and the
    plugin's own vote decides the unit (seconds, within four frames, as in a
    harness; Resolume's milliseconds would be found the same way). Before the
    vote settles the plugin runs on its wall clock, which here is the
    browser's performance.now(). Unlike gttest the page does NOT call
    ForceSecondsClock: a host cannot, so the page does not either.

    Two read-outs are the page's and are the plugin's own public functions:
    gt_convert is Controls.h's conversion for the two FF_TYPE_STANDARD
    controls, and gt_effective / gt_overridden are GraticulePlugin::Effective,
    the one place the plugin reads a preset's override. They report and
    change nothing (gt_overridden restores what it probes).

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the one entry point that cannot be passed
    straight through.
*/
#include "Controls.h"
#include "Diag.h"
#include "Graticule.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

using namespace graticule;

namespace
{
/// One plugin instance and the host-side struct it is handed.
struct Instance
{
	GraticulePlugin plugin;
	bool initialised = false;

	ProcessOpenGLStruct process {};

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

constexpr double kNaN = std::numeric_limits< double >::quiet_NaN();
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// The plugin's log. Diag.cpp opens it in InitGL (diag::init), so this must run
// before the first gt_init_gl: a directory in the page's in-memory file
// system, named through the override Diag.cpp reads. Its own `mkdir -p` goes
// through system(), which a browser does not have.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE void gt_prepare_log()
{
	setenv( "GRATICULE_LOG_DIR", "/graticule/logs", 1 );
	mkdir( "/graticule", 0755 );
	mkdir( "/graticule/logs", 0755 );
}

EMSCRIPTEN_KEEPALIVE const char* gt_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE Instance* gt_new()
{
	return new Instance();
}

EMSCRIPTEN_KEEPALIVE void gt_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int gt_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

/// FF_RESIZE: the composition's raster changed. The plugin does not override
/// Resize, so this is CFFGLPlugin's, which records the new viewport that
/// ProcessOpenGL draws at -- exactly what FFGL.cpp's plugMain does with it.
EMSCRIPTEN_KEEPALIVE void gt_resize( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width  = static_cast< GLuint >( width );
	viewport.height = static_cast< GLuint >( height );
	instance->plugin.Resize( &viewport );
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int gt_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* gt_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int gt_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* gt_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it).
EMSCRIPTEN_KEEPALIVE float gt_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a text parameter.
EMSCRIPTEN_KEEPALIVE const char* gt_param_default_text( Instance* instance, int index )
{
	const FFMixed mixed = instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) );
	return mixed.PointerValue ? static_cast< const char* >( mixed.PointerValue ) : "";
}

EMSCRIPTEN_KEEPALIVE int gt_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* gt_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float gt_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

EMSCRIPTEN_KEEPALIVE float gt_param_range_min( Instance* instance, int index )
{
	return instance->plugin.GetParamRange( static_cast< unsigned int >( index ) ).min;
}

EMSCRIPTEN_KEEPALIVE float gt_param_range_max( Instance* instance, int index )
{
	return instance->plugin.GetParamRange( static_cast< unsigned int >( index ) ).max;
}

EMSCRIPTEN_KEEPALIVE int gt_min_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMinInputs() );
}

EMSCRIPTEN_KEEPALIVE int gt_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int gt_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float gt_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

EMSCRIPTEN_KEEPALIVE int gt_set_text( Instance* instance, int index, const char* value )
{
	return instance->plugin.SetTextParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE const char* gt_get_text( Instance* instance, int index )
{
	const char* text = instance->plugin.GetTextParameter( static_cast< unsigned int >( index ) );
	return text ? text : "";
}

/// One frame, in the order tools/gttest makes the calls: SetTime, then
/// ProcessOpenGL with no input textures, drawing into whatever framebuffer the
/// page has bound -- the canvas's, name 0, as a host's HostFBO. 1 on success.
EMSCRIPTEN_KEEPALIVE int gt_process( Instance* instance, double seconds )
{
	instance->plugin.SetTime( seconds );
	instance->process.numInputTextures = 0;
	instance->process.inputTextures    = nullptr;
	instance->process.HostFBO          = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// The preset override, read through the plugin's own Effective(): what it
// draws with, which differs from the panel's value while Preset is on a
// factory row that sets this column.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE float gt_effective( Instance* instance, int index )
{
	return instance->plugin.Effective( static_cast< unsigned int >( index ) );
}

/// 1 when the current preset overrides this parameter: Effective() does not
/// follow the parameter's own value. Probed with two values the parameter is
/// then put back from; only for the plugin's own numeric controls (never the
/// About block, whose SetFloatParameter is a button press, and never a text
/// parameter).
EMSCRIPTEN_KEEPALIVE int gt_overridden( Instance* instance, int index )
{
	const unsigned int id = static_cast< unsigned int >( index );
	if( id >= PT_ABOUT_TEXT || id == PT_PRESET || id == PT_TEXT )
		return 0;
	GraticulePlugin& p = instance->plugin;
	const float kept   = p.GetFloatParameter( id );
	p.SetFloatParameter( id, kept + 1.0f );
	const float a = p.Effective( id );
	p.SetFloatParameter( id, kept + 2.0f );
	const float b = p.Effective( id );
	p.SetFloatParameter( id, kept );
	return a == b ? 1 : 0;
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.h's conversion for a parameter, the one
// BuildFrame applies to it. NaN for every control that is not one of the two
// FF_TYPE_STANDARD ones (the integers, options and booleans hold their real
// value already).
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double gt_convert( int index, float v )
{
	switch( index )
	{
	case PT_LEVEL: return FieldLevel( v );
	case PT_SPEED: return CrossingsPerSecond( v );
	default: return kNaN;
	}
}

//---------------------------------------------------------------------------
// The About block's buttons: the URL each one opens, from the
// StoatworksAboutLinks.h the plugin is built with. The host getters give only
// a button's label; the page needs the address the plugin's own press would
// have opened, because that press calls system(), which a browser does not
// have.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int gt_about_first()
{
	return static_cast< int >( PT_ABOUT_TEXT );
}

EMSCRIPTEN_KEEPALIVE const char* gt_about_button_label( int button )
{
	const auto& list = stoatworks::about::buttons();
	return button >= 0 && static_cast< size_t >( button ) < list.size() ? list[ static_cast< size_t >( button ) ].label : "";
}

EMSCRIPTEN_KEEPALIVE const char* gt_about_button_url( int button )
{
	const auto& list = stoatworks::about::buttons();
	return button >= 0 && static_cast< size_t >( button ) < list.size() ? list[ static_cast< size_t >( button ) ].url : "";
}

} // extern "C"
