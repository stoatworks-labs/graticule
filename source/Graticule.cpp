#include "Graticule.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Bars.h"
#include "Diag.h"
#include "Font.h"
#include "Presets.h"

namespace graticule
{
static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount,
			   "the About block's size changed with the generated header -- "
			   "add or remove a PT_ABOUT_BUTTON_n to match" );

namespace
{
/// Turn a normalised option parameter back into an index. Resolume hands option
/// parameters back as the element *value*, which for these is the index already,
/// but a host that normalises would give 0..1 -- so both are accepted and
/// clamped. Getting this wrong silently selects mode 0 for ever.
int ToOption( float v, int count )
{
	if( count <= 1 )
		return 0;
	int i = ( v <= 1.0f && count > 2 && v != std::floor( v ) ) ? static_cast< int >( v * static_cast< float >( count - 1 ) + 0.5f )
															   : static_cast< int >( v + 0.5f );
	return std::min( std::max( i, 0 ), count - 1 );
}

int Int( float v )
{
	return static_cast< int >( std::lround( v ) );
}

bool Bool( float v )
{
	return v > 0.5f;
}

/// Which ParamId each preset column drives.
constexpr ParamId kPresetTarget[ presets::kParamCount ] = {
	PT_PATTERN, PT_LEVELS, PT_GRID_MODE, PT_PITCH,  PT_DIV_X, PT_DIV_Y, PT_MAJOR_EVERY, PT_TILE_W,   PT_TILE_H,
	PT_MODULE_W, PT_MODULE_H, PT_STEPS,   PT_CELL,   PT_MOTION, PT_SPEED, PT_MARKER_SIZE, PT_FRAME_COUNTER,
};

/// A triangle wave in 0..1 with period 1.
double Tri( double u )
{
	const double f = u - std::floor( u );
	return 1.0 - std::fabs( 2.0 * f - 1.0 );
}

void AppendText( std::vector< int >& out, const std::string& s )
{
	for( unsigned char c : s )
		out.push_back( ( c >= 32 && c < 127 ) ? static_cast< int >( c ) : 63 );
}
} // namespace

GraticulePlugin::GraticulePlugin()
{
	static std::atomic< int > sNextInstance{ 1 };
	mInstanceId = sNextInstance.fetch_add( 1 );
	mTag        = "[" + std::to_string( mInstanceId ) + "] ";

	SetMinInputs( 0 );
	SetMaxInputs( 0 );

	auto group = [ this ]( unsigned int from, unsigned int to, const char* name ) {
		for( unsigned int i = from; i <= to; ++i )
			SetParamGroup( i, name );
	};
	auto option = [ this ]( unsigned int id, const char* name, std::initializer_list< const char* > elements, float def ) {
		SetOptionParamInfo( id, name, static_cast< unsigned int >( elements.size() ), def );
		unsigned int i = 0;
		for( const char* e : elements )
		{
			SetParamElementInfo( id, i, e, static_cast< float >( i ) );
			++i;
		}
	};
	auto integer = [ this ]( unsigned int id, const char* name, float def, float lo, float hi ) {
		// Only FF_TYPE_STANDARD gets its default clamped into 0..1 -- see
		// Controls.h -- so an integer can be declared with its real default.
		SetParamInfo( id, name, FF_TYPE_INTEGER, def );
		SetParamRange( id, lo, hi );
		mParams[ id ] = def;
	};
	auto boolean = [ this ]( unsigned int id, const char* name, bool def ) {
		SetParamInfo( id, name, FF_TYPE_BOOLEAN, def );
		mParams[ id ] = def ? 1.0f : 0.0f;
	};
	auto standard = [ this ]( unsigned int id, const char* name, float def ) {
		SetParamInfo( id, name, FF_TYPE_STANDARD, def );
		mParams[ id ] = def;
	};
	auto colour = [ this ]( unsigned int first, const char* label, float r, float g, float b ) {
		// SetParamInfo copies the name into a std::string, so temporaries are fine.
		const std::string base = label;
		SetParamInfo( first + 0, ( base + " Red" ).c_str(), FF_TYPE_RED, r );
		SetParamInfo( first + 1, ( base + " Green" ).c_str(), FF_TYPE_GREEN, g );
		SetParamInfo( first + 2, ( base + " Blue" ).c_str(), FF_TYPE_BLUE, b );
		mParams[ first + 0 ] = r;
		mParams[ first + 1 ] = g;
		mParams[ first + 2 ] = b;
	};

	// Declaration order is the order the host draws these, and SetParamGroup
	// collapses runs -- so an id moved out of its run splits its group in two.

	// -- Pattern ---------------------------------------------------------------
	{
		std::vector< const char* > names;
		names.push_back( "Custom" );
		for( int i = 0; i < presets::kCount; ++i )
			names.push_back( presets::kPresets[ i ].name );
		SetOptionParamInfo( PT_PRESET, "Preset", static_cast< unsigned int >( names.size() ), 0.0f );
		for( unsigned int i = 0; i < names.size(); ++i )
			SetParamElementInfo( PT_PRESET, i, names[ i ], static_cast< float >( i ) );
	}
	option( PT_PATTERN, "Pattern",
			{ "SMPTE RP 219", "75% Bars", "100% Bars", "Grid", "Alignment", "LED Tiles", "Solid", "Greyscale", "Pixel Check" },
			0.0f );
	option( PT_LEVELS, "Levels", { "Full 0-255", "Legal 16-235" }, 0.0f );
	group( PT_PRESET, PT_LEVELS, "Pattern" );

	// -- Grid ------------------------------------------------------------------
	option( PT_GRID_MODE, "Grid By", { "Pitch", "Divisions" }, 0.0f );
	integer( PT_PITCH, "Pitch", 100.0f, 2.0f, 1024.0f );
	integer( PT_DIV_X, "Divisions X", 16.0f, 1.0f, 128.0f );
	integer( PT_DIV_Y, "Divisions Y", 9.0f, 1.0f, 128.0f );
	integer( PT_LINE_W, "Line Width", 1.0f, 1.0f, 16.0f );
	integer( PT_MAJOR_EVERY, "Major Every", 5.0f, 0.0f, 32.0f );
	integer( PT_MAJOR_W, "Major Width", 3.0f, 1.0f, 16.0f );
	boolean( PT_DIAGONALS, "Diagonals", true );
	boolean( PT_CENTRE, "Centre Target", true );
	boolean( PT_ORIGIN_CENTRE, "Origin Centre", false );
	boolean( PT_CELL_LABELS, "Cell Labels", false );
	colour( PT_LINE_R, "Line", 0.24f, 0.49f, 1.0f );
	colour( PT_MAJOR_R, "Major", 1.0f, 1.0f, 1.0f );
	colour( PT_BG_R, "Background", 0.0f, 0.0f, 0.0f );
	group( PT_GRID_MODE, PT_BG_B, "Grid" );

	// -- LED Tiles -------------------------------------------------------------
	integer( PT_TILE_W, "Tile Width", 192.0f, 4.0f, 2048.0f );
	integer( PT_TILE_H, "Tile Height", 192.0f, 4.0f, 2048.0f );
	integer( PT_MODULE_W, "Module Width", 96.0f, 0.0f, 1024.0f );
	integer( PT_MODULE_H, "Module Height", 96.0f, 0.0f, 1024.0f );
	integer( PT_ORIGIN_X, "Origin X", 0.0f, 0.0f, 4096.0f );
	integer( PT_ORIGIN_Y, "Origin Y", 0.0f, 0.0f, 4096.0f );
	boolean( PT_CHECKER, "Checker", true );
	boolean( PT_TILE_LABELS, "Tile Labels", true );
	integer( PT_BORDER_W, "Border Width", 1.0f, 0.0f, 16.0f );
	colour( PT_TILE_A_R, "Tile A", 0.063f, 0.063f, 0.063f );
	colour( PT_TILE_B_R, "Tile B", 0.165f, 0.165f, 0.165f );
	colour( PT_BORDER_R, "Border", 1.0f, 1.0f, 1.0f );
	group( PT_TILE_W, PT_BORDER_B, "LED Tiles" );

	// -- Solid -----------------------------------------------------------------
	option( PT_FIELD, "Field",
			{ "White", "Black", "Red", "Green", "Blue", "Cyan", "Magenta", "Yellow", "50% Grey", "18% Grey", "Custom" }, 0.0f );
	standard( PT_LEVEL, "Level", 1.0f );
	colour( PT_CUSTOM_R, "Custom", 1.0f, 1.0f, 1.0f );
	group( PT_FIELD, PT_CUSTOM_B, "Solid" );

	// -- Greyscale -------------------------------------------------------------
	integer( PT_STEPS, "Steps", 11.0f, 0.0f, 64.0f );
	boolean( PT_VERTICAL, "Vertical", false );
	boolean( PT_PER_CHANNEL, "Per Channel", false );
	group( PT_STEPS, PT_PER_CHANNEL, "Greyscale" );

	// -- Pixel Check -----------------------------------------------------------
	integer( PT_CELL, "Cell", 1.0f, 1.0f, 16.0f );
	boolean( PT_BURSTS, "Line Bursts", true );
	group( PT_CELL, PT_BURSTS, "Pixel Check" );

	// -- Alignment -------------------------------------------------------------
	boolean( PT_EDGE_BORDER, "Edge Border", true );
	boolean( PT_CORNERS, "Corner Markers", true );
	integer( PT_CORNER_SIZE, "Corner Size", 100.0f, 8.0f, 1000.0f );
	boolean( PT_TARGET, "Centre Marks", true );
	integer( PT_SAFE_A, "Safe Area A %", 10.0f, 0.0f, 49.0f );
	integer( PT_SAFE_B, "Safe Area B %", 20.0f, 0.0f, 49.0f );
	boolean( PT_PATCHES, "Pixel Patches", true );
	boolean( PT_ALIGN_DIAG, "Diagonal Cross", true );
	group( PT_EDGE_BORDER, PT_ALIGN_DIAG, "Alignment" );

	// -- Motion ----------------------------------------------------------------
	option( PT_MOTION, "Marker", { "Off", "Sweep Across", "Sweep Down", "Bounce" }, 0.0f );
	standard( PT_SPEED, "Speed", 0.5f );
	integer( PT_MARKER_SIZE, "Marker Size", 16.0f, 1.0f, 256.0f );
	boolean( PT_FRAME_COUNTER, "Frame Counter", false );
	group( PT_MOTION, PT_FRAME_COUNTER, "Motion" );

	// -- Burn-in ---------------------------------------------------------------
	boolean( PT_BURNIN, "Burn-in", true );
	boolean( PT_SHOW_RES, "Show Resolution", true );
	boolean( PT_SHOW_PATTERN, "Show Pattern", false );
	SetParamInfo( PT_TEXT, "Text", FF_TYPE_TEXT, "" );
	option( PT_TEXT_POS, "Position", { "Top Left", "Top Centre", "Centre", "Bottom Centre", "Bottom Left" }, 0.0f );
	integer( PT_TEXT_SIZE, "Text Size", 0.0f, 0.0f, 16.0f );
	boolean( PT_PLATE, "Plate", true );
	group( PT_BURNIN, PT_PLATE, "Burn-in" );

	// -- About -----------------------------------------------------------------
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	group( PT_ABOUT_TEXT, PT_COUNT - 1, "About" );
}

FFResult GraticulePlugin::InitGL( const FFGLViewportStruct* vp )
{
	// Idempotent: a host may call this again, and the offline harness does.
	if( mGlReady )
		return CFFGLPlugin::InitGL( vp );

	diag::init();

	auto glString = []( GLenum name ) {
		const GLubyte* s = glGetString( name );
		return s != nullptr ? std::string( reinterpret_cast< const char* >( s ) ) : std::string( "?" );
	};
	diag::info( mTag + "GL vendor=" + glString( GL_VENDOR ) + " renderer=" + glString( GL_RENDERER ) +
				" version=" + glString( GL_VERSION ) );

	if( !mRenderer.InitGL() )
	{
		diag::error( mTag + "InitGL failed: " + mRenderer.Note() );
		return FF_FAIL;
	}
	mGlReady = true;
	return CFFGLPlugin::InitGL( vp );
}

FFResult GraticulePlugin::DeInitGL()
{
	mRenderer.DeInitGL();
	mGlReady = false;
	return FF_SUCCESS;
}

FFResult GraticulePlugin::SetTime( double time )
{
	mHostTimeSeen = true;
	mHostTime     = time;
	return CFFGLPlugin::SetTime( time );
}

FFResult GraticulePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;
	mParams[ index ] = value;
	return FF_SUCCESS;
}

float GraticulePlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? mParams[ index ] : 0.0f;
}

FFResult GraticulePlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_TEXT )
	{
		mText = value != nullptr ? value : "";
		return FF_SUCCESS;
	}
	// Must return FF_SUCCESS for the About block, or no host can instantiate
	// the plugin at all: the base class fails an unknown text parameter, and a
	// host that sets one during instantiation treats that as the plugin
	// refusing to load. What the operator typed into it is dropped.
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return FF_FAIL;
}

char* GraticulePlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_TEXT )
		return const_cast< char* >( mText.c_str() );
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return const_cast< char* >( "" );
}

float GraticulePlugin::Effective( unsigned int index ) const
{
	const int preset = ToOption( mParams[ PT_PRESET ], presets::kCount + 1 );
	if( preset > 0 )
	{
		const presets::Preset& row = presets::kPresets[ preset - 1 ];
		for( int c = 0; c < presets::kParamCount; ++c )
			if( kPresetTarget[ c ] == index )
				return row.v[ c ];
	}
	return index < PT_COUNT ? mParams[ index ] : 0.0f;
}

int GraticulePlugin::OptionIndex( unsigned int param, int count ) const
{
	return ToOption( Effective( param ), count );
}

Frame GraticulePlugin::BuildFrame( int width, int height, double seconds, long frameIndex ) const
{
	Frame f;
	f.width  = width;
	f.height = height;

	const Pattern pattern = static_cast< Pattern >( OptionIndex( PT_PATTERN, kPatternCount ) );
	f.pattern             = static_cast< int >( pattern );
	f.legal               = OptionIndex( PT_LEVELS, 2 ) == 1;

	auto E = [ this ]( unsigned int id ) { return Effective( id ); };
	auto C = [ this ]( unsigned int first, float* out ) {
		out[ 0 ] = Effective( first );
		out[ 1 ] = Effective( first + 1 );
		out[ 2 ] = Effective( first + 2 );
	};

	f.gridMode     = OptionIndex( PT_GRID_MODE, 2 );
	f.pitch        = std::max( 2, Int( E( PT_PITCH ) ) );
	f.divX         = std::max( 1, Int( E( PT_DIV_X ) ) );
	f.divY         = std::max( 1, Int( E( PT_DIV_Y ) ) );
	f.lineW        = std::max( 1, Int( E( PT_LINE_W ) ) );
	f.majorEvery   = std::max( 0, Int( E( PT_MAJOR_EVERY ) ) );
	f.majorW       = std::max( 1, Int( E( PT_MAJOR_W ) ) );
	f.diagonals    = Bool( E( PT_DIAGONALS ) );
	f.centre       = Bool( E( PT_CENTRE ) );
	f.originCentre = Bool( E( PT_ORIGIN_CENTRE ) );
	f.cellLabels   = Bool( E( PT_CELL_LABELS ) );
	C( PT_LINE_R, f.lineCol );
	C( PT_MAJOR_R, f.majorCol );
	C( PT_BG_R, f.bgCol );

	f.tileW      = std::max( 1, Int( E( PT_TILE_W ) ) );
	f.tileH      = std::max( 1, Int( E( PT_TILE_H ) ) );
	f.moduleW    = std::max( 0, Int( E( PT_MODULE_W ) ) );
	f.moduleH    = std::max( 0, Int( E( PT_MODULE_H ) ) );
	f.originX    = Int( E( PT_ORIGIN_X ) );
	f.originY    = Int( E( PT_ORIGIN_Y ) );
	f.checker    = Bool( E( PT_CHECKER ) );
	f.tileLabels = Bool( E( PT_TILE_LABELS ) );
	f.borderW    = std::max( 0, Int( E( PT_BORDER_W ) ) );
	C( PT_TILE_A_R, f.tileA );
	C( PT_TILE_B_R, f.tileB );
	C( PT_BORDER_R, f.borderCol );

	{
		const Field field = static_cast< Field >( OptionIndex( PT_FIELD, kFieldCount ) );
		const float level = FieldLevel( E( PT_LEVEL ) );
		float       base[ 3 ] = { 1, 1, 1 };
		switch( field )
		{
		case Field::White: break;
		case Field::Black: base[ 0 ] = base[ 1 ] = base[ 2 ] = 0; break;
		case Field::Red: base[ 1 ] = base[ 2 ] = 0; break;
		case Field::Green: base[ 0 ] = base[ 2 ] = 0; break;
		case Field::Blue: base[ 0 ] = base[ 1 ] = 0; break;
		case Field::Cyan: base[ 0 ] = 0; break;
		case Field::Magenta: base[ 1 ] = 0; break;
		case Field::Yellow: base[ 2 ] = 0; break;
		case Field::Grey50: base[ 0 ] = base[ 1 ] = base[ 2 ] = 0.5f; break;
		case Field::Grey18: base[ 0 ] = base[ 1 ] = base[ 2 ] = 0.18f; break;
		case Field::Custom: C( PT_CUSTOM_R, base ); break;
		default: break;
		}
		for( int i = 0; i < 3; ++i )
			f.solid[ i ] = base[ i ] * level;
	}

	f.steps      = std::max( 0, Int( E( PT_STEPS ) ) );
	f.vertical   = Bool( E( PT_VERTICAL ) );
	f.perChannel = Bool( E( PT_PER_CHANNEL ) );

	f.cell   = std::max( 1, Int( E( PT_CELL ) ) );
	f.bursts = Bool( E( PT_BURSTS ) );

	f.edgeBorder = Bool( E( PT_EDGE_BORDER ) );
	f.corners    = Bool( E( PT_CORNERS ) );
	f.cornerSize = std::max( 8, Int( E( PT_CORNER_SIZE ) ) );
	f.target     = Bool( E( PT_TARGET ) );
	f.safeA      = std::max( 0, Int( E( PT_SAFE_A ) ) );
	f.safeB      = std::max( 0, Int( E( PT_SAFE_B ) ) );
	f.patches    = Bool( E( PT_PATCHES ) );
	f.alignDiag  = Bool( E( PT_ALIGN_DIAG ) );

	if( pattern == Pattern::Smpte )
		f.bands = bars::Rp219Layout( width, height );
	else if( pattern == Pattern::Bars75 )
		f.bands = bars::BarsLayout( width, height, 75.0 );
	else if( pattern == Pattern::Bars100 )
		f.bands = bars::BarsLayout( width, height, 100.0 );

	// -- the marker ------------------------------------------------------------
	{
		const Motion motion = static_cast< Motion >( OptionIndex( PT_MOTION, kMotionCount ) );
		const double rate   = CrossingsPerSecond( E( PT_SPEED ) );
		const int    size   = std::max( 1, Int( E( PT_MARKER_SIZE ) ) );
		const double u      = seconds * rate;
		const double phase  = u - std::floor( u );
		switch( motion )
		{
		case Motion::SweepAcross:
			f.marker[ 0 ] = static_cast< int >( std::lround( phase * ( width + size ) ) ) - size;
			f.marker[ 1 ] = 0;
			f.marker[ 2 ] = size;
			f.marker[ 3 ] = height;
			break;
		case Motion::SweepDown:
			f.marker[ 0 ] = 0;
			f.marker[ 1 ] = static_cast< int >( std::lround( phase * ( height + size ) ) ) - size;
			f.marker[ 2 ] = width;
			f.marker[ 3 ] = size;
			break;
		case Motion::Bounce:
			// Two incommensurate triangle waves, so the box visits the whole
			// raster rather than retracing one diagonal.
			f.marker[ 0 ] = static_cast< int >( std::lround( Tri( u * 0.5 ) * std::max( 0, width - size ) ) );
			f.marker[ 1 ] = static_cast< int >( std::lround( Tri( u * 0.5 * 0.7 ) * std::max( 0, height - size ) ) );
			f.marker[ 2 ] = size;
			f.marker[ 3 ] = size;
			break;
		default: break;
		}
	}

	// -- the burn-in -----------------------------------------------------------
	{
		std::vector< std::string > lines;
		if( Bool( E( PT_BURNIN ) ) )
		{
			std::string line;
			auto add = [ &line ]( const std::string& s ) {
				if( s.empty() )
					return;
				if( !line.empty() )
					line += "  ";
				line += s;
			};
			if( Bool( E( PT_SHOW_RES ) ) )
				add( std::to_string( width ) + "x" + std::to_string( height ) );
			if( Bool( E( PT_SHOW_PATTERN ) ) )
				add( PatternName( pattern ) );
			add( mText );
			if( !line.empty() )
				lines.push_back( line );
		}
		if( Bool( E( PT_FRAME_COUNTER ) ) )
		{
			char buffer[ 64 ];
			std::snprintf( buffer, sizeof( buffer ), "F%06ld  %.2fs", frameIndex % 1000000L, seconds );
			lines.emplace_back( buffer );
		}

		if( !lines.empty() )
		{
			const int sizeParam = Int( E( PT_TEXT_SIZE ) );
			const int s = sizeParam > 0 ? sizeParam : std::max( 1, static_cast< int >( std::lround( std::min( width, height ) / 180.0 ) ) );
			f.textScale = s;

			int blockW = 0;
			for( std::string& l : lines )
			{
				if( l.size() > 40 )
					l.resize( 40 );
				blockW = std::max( blockW, static_cast< int >( l.size() ) * font::kAdvance * s - s );
			}
			const int blockH = static_cast< int >( lines.size() ) * 8 * s - s;
			const int margin = 2 * s + 4;

			int x = margin, y = margin;
			switch( static_cast< TextPos >( OptionIndex( PT_TEXT_POS, kTextPosCount ) ) )
			{
			case TextPos::TopCentre: x = ( width - blockW ) / 2; break;
			case TextPos::Centre: x = ( width - blockW ) / 2; y = ( height - blockH ) / 2; break;
			case TextPos::BottomCentre: x = ( width - blockW ) / 2; y = height - margin - blockH; break;
			case TextPos::BottomLeft: y = height - margin - blockH; break;
			default: break;
			}

			int offset = 0;
			for( size_t i = 0; i < lines.size(); ++i )
			{
				Frame::Line L;
				L.offset = offset;
				L.length = static_cast< int >( lines[ i ].size() );
				L.x      = x;
				L.y      = y + static_cast< int >( i ) * 8 * s;
				AppendText( f.text, lines[ i ] );
				offset += L.length;
				f.lines.push_back( L );
			}

			if( Bool( E( PT_PLATE ) ) )
			{
				f.plate[ 0 ] = x - 2 * s;
				f.plate[ 1 ] = y - 2 * s;
				f.plate[ 2 ] = blockW + 4 * s;
				f.plate[ 3 ] = blockH + 4 * s;
			}
		}
	}

	return f;
}

FFResult GraticulePlugin::ProcessOpenGL( ProcessOpenGLStruct* pGL )
{
	if( pGL == nullptr || !mGlReady )
		return FF_FAIL;

	mClock.Tick( mHostTime, mHostTimeSeen );
	diag::stateChanged( mTag + "clock", mTag + "host clock is " + mClock.Unit() );

	const int width  = static_cast< int >( currentViewport.width );
	const int height = static_cast< int >( currentViewport.height );
	if( width <= 0 || height <= 0 )
		return FF_SUCCESS;

	const Frame frame = BuildFrame( width, height, mClock.Seconds(), mFrame );
	++mFrame;

	diag::stateChanged( mTag + "draw", mTag + "drawing " + PatternName( static_cast< Pattern >( frame.pattern ) ) + " at " +
										   std::to_string( width ) + "x" + std::to_string( height ) +
										   ( frame.legal ? " legal" : " full" ) + " range" );

	mRenderer.Draw( frame, pGL->HostFBO );
	return FF_SUCCESS;
}

} // namespace graticule
