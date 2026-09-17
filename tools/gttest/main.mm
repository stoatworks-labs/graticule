// Pixels, from the real plugin class, in a real GL context, with no host.
//
// oxbow proves the bundle registers and instantiates. This is where the
// patterns are actually checked: every check below drives GraticulePlugin the
// way a host does -- InitGL, SetFloatParameter, SetTime, ProcessOpenGL into an
// FBO -- reads the pixels back and asserts what the arithmetic says they are.
//
//   --font                  print every glyph; fail on a blank or duplicated one
//   --colours               the fifteen RP 219 colours land on the measured code values
//   --bars <fixture.json>   the layout matches ffmpeg's measured geometry at four sizes
//   --defaults              preset row 1 IS the constructor's defaults
//   --names                 no parameter name over FFGL's 16 characters
//   --list                  the fleet's parameter listing, for tools/sweep.py
//   --pixels                render each pattern and check specific pixels
//   --out f.png [--size WxH] [--frames N] [--set Name=value ...]
//                           render a frame, for eyes and for the sweep
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "Bars.h"
#include "Font.h"
#include "Graticule.h"
#include "Presets.h"

using namespace graticule;

static int failures = 0;
static void Check( bool ok, const std::string& what )
{
	std::printf( "  %s %s\n", ok ? "ok  " : "FAIL", what.c_str() );
	if( !ok )
		++failures;
}

namespace
{
//---------------------------------------------------------------------------
// A PNG writer. zlib ships with the OS, so this is a few chunk headers and a
// CRC rather than a dependency.
//---------------------------------------------------------------------------
void putU32( std::vector< unsigned char >& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( std::vector< unsigned char >& out, const char* type, const std::vector< unsigned char >& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

bool writePng( const std::string& path, int width, int height, const std::vector< unsigned char >& rgba )
{
	std::vector< unsigned char > raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = 0; y < height; ++y )
	{
		raw.push_back( 0 );
		const unsigned char* row = rgba.data() + static_cast< size_t >( y ) * width * 4;
		raw.insert( raw.end(), row, row + static_cast< size_t >( width ) * 4 );
	}
	uLongf                       compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	std::vector< unsigned char > compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	std::vector< unsigned char > png = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
	std::vector< unsigned char > header;
	putU32( header, static_cast< uint32_t >( width ) );
	putU32( header, static_cast< uint32_t >( height ) );
	header.push_back( 8 );
	header.push_back( 6 );
	header.push_back( 0 );
	header.push_back( 0 );
	header.push_back( 0 );
	putChunk( png, "IHDR", header );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );

	std::FILE* file = std::fopen( path.c_str(), "wb" );
	if( file == nullptr )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}

//---------------------------------------------------------------------------
// GL plumbing.
//---------------------------------------------------------------------------
CGLContextObj createContext()
{
	CGLPixelFormatAttribute attrs[] = { kCGLPFAOpenGLProfile,
										(CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
										kCGLPFAAccelerated,
										kCGLPFAColorSize,
										(CGLPixelFormatAttribute)24,
										(CGLPixelFormatAttribute)0 };
	CGLPixelFormatObj       pix  = nullptr;
	GLint                   npix = 0;
	if( CGLChoosePixelFormat( attrs, &pix, &npix ) != kCGLNoError || pix == nullptr )
	{
		// No accelerated context (a CI runner): take whatever there is.
		CGLPixelFormatAttribute soft[] = { kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core,
										   kCGLPFAColorSize, (CGLPixelFormatAttribute)24, (CGLPixelFormatAttribute)0 };
		if( CGLChoosePixelFormat( soft, &pix, &npix ) != kCGLNoError || pix == nullptr )
			return nullptr;
	}
	CGLContextObj ctx = nullptr;
	if( CGLCreateContext( pix, nullptr, &ctx ) != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( ctx );
	return ctx;
}

struct Target
{
	GLuint fbo = 0, colour = 0;
	int    w = 0, h = 0;

	void Create( int width, int height )
	{
		w = width;
		h = height;
		glGenTextures( 1, &colour );
		glBindTexture( GL_TEXTURE_2D, colour );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glGenFramebuffers( 1, &fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0 );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}
};

/// Top-down RGBA, so (x, y) indexes the way the patterns are described.
struct Image
{
	int                          w = 0, h = 0;
	std::vector< unsigned char > px;

	const unsigned char* at( int x, int y ) const { return px.data() + ( static_cast< size_t >( y ) * w + x ) * 4; }
	bool is( int x, int y, int r, int g, int b, int tol = 0 ) const
	{
		const unsigned char* p = at( x, y );
		return std::abs( p[ 0 ] - r ) <= tol && std::abs( p[ 1 ] - g ) <= tol && std::abs( p[ 2 ] - b ) <= tol;
	}
	std::string str( int x, int y ) const
	{
		const unsigned char* p = at( x, y );
		return "(" + std::to_string( p[ 0 ] ) + "," + std::to_string( p[ 1 ] ) + "," + std::to_string( p[ 2 ] ) + ")";
	}
};

Image readBack( const Target& t )
{
	std::vector< unsigned char > raw( static_cast< size_t >( t.w ) * t.h * 4 );
	glBindFramebuffer( GL_FRAMEBUFFER, t.fbo );
	glReadPixels( 0, 0, t.w, t.h, GL_RGBA, GL_UNSIGNED_BYTE, raw.data() );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	Image img;
	img.w = t.w;
	img.h = t.h;
	img.px.resize( raw.size() );
	for( int y = 0; y < t.h; ++y )
		std::memcpy( img.px.data() + static_cast< size_t >( y ) * t.w * 4,
					 raw.data() + static_cast< size_t >( t.h - 1 - y ) * t.w * 4, static_cast< size_t >( t.w ) * 4 );
	return img;
}

Image renderAt( GraticulePlugin& p, const Target& t, double seconds )
{
	ProcessOpenGLStruct gl = {};
	gl.HostFBO             = t.fbo;
	glBindFramebuffer( GL_FRAMEBUFFER, t.fbo );
	glViewport( 0, 0, t.w, t.h );
	glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	p.SetTime( seconds );
	p.ProcessOpenGL( &gl );
	return readBack( t );
}

struct Host
{
	CGLContextObj ctx = nullptr;
	Target        target;

	bool Open( int w, int h )
	{
		ctx = createContext();
		if( ctx == nullptr )
		{
			std::printf( "no GL context\n" );
			return false;
		}
		target.Create( w, h );
		return true;
	}
};

/// A plugin ready to render at the target's size.
struct Instance
{
	GraticulePlugin plugin;
	Instance( int w, int h )
	{
		plugin.ForceSecondsClock();
		FFGLViewportStruct vp = { 0, 0, static_cast< FFUInt32 >( w ), static_cast< FFUInt32 >( h ) };
		if( plugin.InitGL( &vp ) != FF_SUCCESS )
			std::printf( "  InitGL FAILED\n" );
	}
	~Instance() { plugin.DeInitGL(); }

	void set( unsigned int id, float v ) { plugin.SetFloatParameter( id, v ); }

	/// The burn-in is on by default and its plate sits over the top-left
	/// corner, which is where half the checks below probe.
	void quiet() { set( PT_BURNIN, 0.0f ); }
};

//---------------------------------------------------------------------------
// The smallest JSON reader that reads the fixture: objects, arrays, numbers,
// strings, null.
//---------------------------------------------------------------------------
struct Json
{
	enum Kind
	{
		Null,
		Number,
		String,
		Array,
		Object
	} kind = Null;
	double                     number = 0;
	std::string                str;
	std::vector< Json >        items;
	std::map< std::string, Json > fields;

	const Json& operator[]( const char* key ) const
	{
		static const Json none;
		auto it = fields.find( key );
		return it == fields.end() ? none : it->second;
	}
	bool has( const char* key ) const { return fields.count( key ) > 0 && fields.at( key ).kind != Null; }
};

struct JsonReader
{
	const std::string& s;
	size_t             i = 0;
	explicit JsonReader( const std::string& text ) : s( text ) {}

	void ws()
	{
		while( i < s.size() && std::isspace( static_cast< unsigned char >( s[ i ] ) ) )
			++i;
	}
	Json value()
	{
		ws();
		Json j;
		if( i >= s.size() )
			return j;
		const char c = s[ i ];
		if( c == '{' )
		{
			j.kind = Json::Object;
			++i;
			ws();
			if( s[ i ] == '}' )
			{
				++i;
				return j;
			}
			for( ;; )
			{
				ws();
				Json key = value();
				ws();
				++i;// ':'
				Json v = value();
				j.fields[ key.str ] = v;
				ws();
				if( s[ i ] == ',' )
				{
					++i;
					continue;
				}
				++i;// '}'
				return j;
			}
		}
		if( c == '[' )
		{
			j.kind = Json::Array;
			++i;
			ws();
			if( s[ i ] == ']' )
			{
				++i;
				return j;
			}
			for( ;; )
			{
				j.items.push_back( value() );
				ws();
				if( s[ i ] == ',' )
				{
					++i;
					continue;
				}
				++i;// ']'
				return j;
			}
		}
		if( c == '"' )
		{
			j.kind = Json::String;
			++i;
			while( i < s.size() && s[ i ] != '"' )
			{
				if( s[ i ] == '\\' )
					++i;
				j.str += s[ i++ ];
			}
			++i;
			return j;
		}
		if( c == 'n' )
		{
			i += 4;
			return j;
		}
		if( c == 't' )
		{
			i += 4;
			j.kind   = Json::Number;
			j.number = 1;
			return j;
		}
		if( c == 'f' )
		{
			i += 5;
			j.kind = Json::Number;
			return j;
		}
		j.kind        = Json::Number;
		const size_t b = i;
		while( i < s.size() && ( std::isdigit( static_cast< unsigned char >( s[ i ] ) ) || s[ i ] == '-' || s[ i ] == '.' ||
								 s[ i ] == 'e' || s[ i ] == 'E' || s[ i ] == '+' ) )
			++i;
		j.number = std::atof( s.substr( b, i - b ).c_str() );
		return j;
	}
};

int roundi( double v )
{
	return static_cast< int >( std::lround( v ) );
}
} // namespace

//---------------------------------------------------------------------------
// --font
//---------------------------------------------------------------------------
int runFont()
{
	std::printf( "the font\n\n" );
	std::map< std::string, int > seen;
	int blank = 0, dup = 0;
	for( int code = 32; code < 127; ++code )
	{
		const char* const* g = font::Glyph( code );
		std::string        key;
		bool               lit = false;
		for( int y = 0; y < font::kHeight; ++y )
		{
			key += g[ y ];
			for( int x = 0; x < font::kWidth; ++x )
				lit = lit || g[ y ][ x ] == '#';
		}
		if( code != 32 && !lit )
		{
			std::printf( "  glyph %d ('%c') is blank\n", code, code );
			++blank;
		}
		if( code != 32 && seen.count( key ) )
		{
			std::printf( "  glyph %d ('%c') duplicates %d ('%c')\n", code, code, seen[ key ], seen[ key ] );
			++dup;
		}
		seen[ key ] = code;
	}
	// Print them, sixteen to a row, for a human to read.
	for( int base = 32; base < 128; base += 16 )
	{
		for( int y = 0; y < font::kHeight; ++y )
		{
			std::string line;
			for( int code = base; code < base + 16; ++code )
			{
				line += font::Glyph( code )[ y ];
				line += ' ';
			}
			std::printf( "  %s\n", line.c_str() );
		}
		std::printf( "\n" );
	}
	Check( blank == 0, "no printable glyph is blank" );
	Check( dup == 0, "no two glyphs are the same picture" );
	Check( font::Bit( 'A', 2, 0 ) && !font::Bit( 'A', 0, 0 ), "'A' has its apex at the top centre" );
	Check( font::Bit( '1', 2, 0 ) && font::Bit( '1', 1, 6 ) && font::Bit( '1', 3, 6 ), "'1' has a base" );
	std::printf( "\n  %s\n", failures == 0 ? "PASS" : "FAIL" );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --colours
//---------------------------------------------------------------------------
int runColours()
{
	std::printf( "the RP 219 colours, computed through Rec.709, against the measurement\n\n" );
	const bars::Palette& C = bars::Colours();
	struct Row
	{
		const char*  name;
		bars::Ycbcr  got;
		int          y, cb, cr;
	};
	const Row rows[] = {
		{ "75% white", C.white75, 180, 128, 128 },       { "75% yellow", C.yellow75, 168, 44, 136 },
		{ "75% cyan", C.cyan75, 145, 147, 44 },          { "75% green", C.green75, 133, 63, 52 },
		{ "75% magenta", C.magenta75, 63, 193, 204 },    { "75% red", C.red75, 51, 109, 212 },
		{ "75% blue", C.blue75, 28, 212, 120 },          { "100% cyan", C.cyan100, 188, 154, 16 },
		{ "100% blue", C.blue100, 32, 240, 118 },        { "100% yellow", C.yellow100, 219, 16, 138 },
		{ "100% red", C.red100, 63, 102, 240 },          { "40% grey", C.grey40, 104, 128, 128 },
		{ "15% grey", C.grey15, 49, 128, 128 },          { "black", C.black, 16, 128, 128 },
		{ "white", C.white100, 235, 128, 128 },          { "pluge -2%", C.plugeMinus2, 12, 128, 128 },
		{ "pluge +2%", C.plugePlus2, 20, 128, 128 },     { "pluge +4%", C.plugePlus4, 25, 128, 128 },
		{ "+I", C.plusI, 57, 156, 97 },                  { "+Q", C.plusQ, 44, 171, 147 },
	};
	for( const Row& r : rows )
	{
		const bool ok = roundi( r.got.y ) == r.y && roundi( r.got.cb ) == r.cb && roundi( r.got.cr ) == r.cr;
		char       buf[ 128 ];
		std::snprintf( buf, sizeof( buf ), "%-12s computed %3d %3d %3d  measured %3d %3d %3d", r.name, roundi( r.got.y ),
					   roundi( r.got.cb ), roundi( r.got.cr ), r.y, r.cb, r.cr );
		Check( ok, buf );
	}

	// And out the other side: the value a full-range PNG carries.
	const bars::Unit y75 = bars::YcbcrToUnit( C.yellow75 );
	Check( bars::UnitToCode( y75.r, Levels::Full ) == 191 && bars::UnitToCode( y75.g, Levels::Full ) == 191 &&
			   bars::UnitToCode( y75.b, Levels::Full ) == 0,
		   "75% yellow at full range is (191, 191, 0), not ffmpeg's BT.601 (189, 202, 7)" );
	Check( bars::UnitToCode( y75.r, Levels::Legal ) == 180 && bars::UnitToCode( y75.b, Levels::Legal ) == 16,
		   "75% yellow at legal range is (180, 180, 16)" );
	const bars::Unit pl = bars::YcbcrToUnit( C.plugeMinus2 );
	Check( bars::UnitToCode( pl.r, Levels::Full ) == 0, "pluge -2% clips to 0 at full range" );
	Check( bars::UnitToCode( pl.r, Levels::Legal ) == 12, "pluge -2% survives as 12 at legal range" );

	std::printf( "\n  %s\n", failures == 0 ? "PASS" : "FAIL" );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --bars <fixture>
//---------------------------------------------------------------------------
int runBars( const std::string& fixturePath )
{
	std::printf( "the RP 219 layout against ffmpeg's measured geometry\n\n" );
	std::ifstream in( fixturePath );
	if( !in )
	{
		std::printf( "  cannot read %s\n", fixturePath.c_str() );
		return 1;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string text = ss.str();
	JsonReader        reader( text );
	const Json        doc = reader.value();

	const Json& sizes = doc[ "sizes" ];
	Check( sizes.items.size() >= 4, "fixture carries at least four sizes" );

	for( const Json& size : sizes.items )
	{
		const int w = static_cast< int >( size[ "width" ].number );
		const int h = static_cast< int >( size[ "height" ].number );
		std::printf( "\n  %dx%d\n", w, h );
		const std::vector< bars::Band > bands = bars::Rp219Layout( w, h );

		// Rows: exact.
		int    bandIndex = 0;
		size_t rowIndex  = 0;
		for( const Json& row : size[ "rows" ].items )
		{
			const int    ry   = static_cast< int >( row[ "y" ].number );
			const int    rh   = static_cast< int >( row[ "h" ].number );
			const size_t cols = row[ "columns" ].items.size();
			bool         rowOk = true, colOk = true, colourOk = true;
			int          worstCol = 0;
			for( size_t c = 0; c < cols; ++c, ++bandIndex )
			{
				if( bandIndex >= static_cast< int >( bands.size() ) )
				{
					rowOk = false;
					break;
				}
				const bars::Band& b   = bands[ static_cast< size_t >( bandIndex ) ];
				const Json&       col = row[ "columns" ].items[ c ];
				if( b.y != ry || b.h != rh )
					rowOk = false;
				const int dx = std::abs( b.x - static_cast< int >( col[ "x" ].number ) );
				worstCol     = std::max( worstCol, dx );
				if( dx > 8 )
					colOk = false;
				if( col.has( "ycbcr" ) )
				{
					// Round-trip our unit colour back to Y'CbCr and compare codes.
					const bars::Ycbcr got = bars::RgbToYcbcr( b.colour.r, b.colour.g, b.colour.b );
					const Json&       m   = col[ "ycbcr" ];
					if( b.fill == bars::Fill::Ramp || roundi( got.y ) != roundi( m.items[ 0 ].number ) ||
						roundi( got.cb ) != roundi( m.items[ 1 ].number ) || roundi( got.cr ) != roundi( m.items[ 2 ].number ) )
						colourOk = false;
				}
				else if( b.fill != bars::Fill::Ramp )
					colourOk = false;
			}
			const std::string label = "row " + std::string( 1, 'A' + static_cast< char >( rowIndex ) );
			Check( rowOk, label + " boundaries match exactly (y=" + std::to_string( ry ) + " h=" + std::to_string( rh ) + ")" );
			Check( colOk, label + " column boundaries within 8px (worst " + std::to_string( worstCol ) + ")" );
			Check( colourOk, label + " every band is the measured colour, or the ramp" );
			++rowIndex;
		}
		Check( bandIndex == static_cast< int >( bands.size() ), "same number of bands as the measurement" );

		// Tiling: every pixel covered exactly once.
		std::vector< int > cover( static_cast< size_t >( w ) * h, 0 );
		for( const bars::Band& b : bands )
			for( int y = b.y; y < b.y + b.h; ++y )
				for( int x = b.x; x < b.x + b.w; ++x )
					if( x >= 0 && x < w && y >= 0 && y < h )
						cover[ static_cast< size_t >( y ) * w + x ]++;
		bool tiled = true;
		for( int v : cover )
			tiled = tiled && v == 1;
		Check( tiled, "the bands tile the raster with no gap and no overlap" );

		// Symmetry: the two grey side blocks are equal, which is the one
		// deliberate difference from ffmpeg.
		Check( bands.front().w == bands[ 8 ].w, "the two side blocks are equal (" + std::to_string( bands.front().w ) + "px)" );
	}

	std::printf( "\n  %s\n", failures == 0 ? "PASS" : "FAIL" );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --defaults
//---------------------------------------------------------------------------
int runDefaults()
{
	std::printf( "preset 1 is the constructor's defaults\n\n" );
	GraticulePlugin plugin;
	const ParamId   target[ presets::kParamCount ] = {
        PT_PATTERN,  PT_LEVELS,   PT_GRID_MODE, PT_PITCH, PT_DIV_X,  PT_DIV_Y, PT_MAJOR_EVERY, PT_TILE_W,      PT_TILE_H,
        PT_MODULE_W, PT_MODULE_H, PT_STEPS,     PT_CELL,  PT_MOTION, PT_SPEED, PT_MARKER_SIZE, PT_FRAME_COUNTER,
	};
	const presets::Preset& row = presets::kPresets[ 0 ];
	for( int c = 0; c < presets::kParamCount; ++c )
	{
		const float d = plugin.GetFloatParameter( target[ c ] );
		char        buf[ 96 ];
		std::snprintf( buf, sizeof( buf ), "%-16s default %g  preset %g", plugin.GetParamName( target[ c ] ), d, row.v[ c ] );
		Check( std::fabs( d - row.v[ c ] ) < 1e-6f, buf );
	}
	// And the preset machinery: selecting a row overrides, deselecting restores.
	plugin.SetFloatParameter( PT_PATTERN, 3.0f );
	plugin.SetFloatParameter( PT_PRESET, 7.0f );// LED 500 mm P2.6
	Check( plugin.Effective( PT_PATTERN ) == 5.0f && plugin.Effective( PT_TILE_W ) == 192.0f,
		   "a chosen preset overrides the pattern and the tile size" );
	plugin.SetFloatParameter( PT_PRESET, 0.0f );
	Check( plugin.Effective( PT_PATTERN ) == 3.0f, "Custom hands the controls back" );
	std::printf( "\n  %s\n", failures == 0 ? "PASS" : "FAIL" );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --names, --list
//---------------------------------------------------------------------------
int runNames()
{
	GraticulePlugin plugin;
	std::printf( "names longer than FFGL's 16 characters:\n\n" );
	int over = 0;
	for( unsigned int id = 0; id < PT_COUNT; ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( name != nullptr && std::strlen( name ) > 16 )
		{
			std::printf( "  %-3u  %-28s %zu\n", id, name, std::strlen( name ) );
			++over;
		}
		for( unsigned int e = 0; e < plugin.GetNumParamElements( id ); ++e )
		{
			const char* el = plugin.GetParamElementName( id, e );
			if( el != nullptr && std::strlen( el ) > 16 )
			{
				std::printf( "  %-3u  %-28s element %u: %s (%zu)\n", id, name, e, el, std::strlen( el ) );
				++over;
			}
		}
	}
	std::printf( "\n  %d over the limit\n", over );
	return over == 0 ? 0 : 1;
}

int runList()
{
	GraticulePlugin plugin;
	std::printf( "%-4s %-22s %-9s %10s   %-16s %s\n", "id", "name", "kind", "value", "range", "means" );
	for( unsigned int id = 0; id < PT_COUNT; ++id )
	{
		const char* name = plugin.GetParamName( id );
		if( id >= PT_ABOUT_TEXT )
		{
			std::printf( "%-4u %-22s %-9s %10s   %-16s %s\n", id, name ? name : "", "about", "-", "-",
						 "the Stoatworks About block; not swept" );
			continue;
		}
		const char* kind = "standard";
		switch( plugin.GetParamType( id ) )
		{
		case FF_TYPE_BOOLEAN: kind = "boolean"; break;
		case FF_TYPE_EVENT: kind = "event"; break;
		case FF_TYPE_INTEGER: kind = "integer"; break;
		case FF_TYPE_OPTION: kind = "option"; break;
		case FF_TYPE_TEXT: kind = "text"; break;
		case FF_TYPE_RED:
		case FF_TYPE_GREEN:
		case FF_TYPE_BLUE: kind = "colour"; break;
		default: break;
		}
		RangeStruct range = plugin.GetParamRange( id );
		if( plugin.GetParamType( id ) == FF_TYPE_OPTION )
		{
			range.min = 0.0f;
			range.max = static_cast< float >( std::max( 1u, plugin.GetNumParamElements( id ) ) ) - 1.0f;
		}
		if( plugin.GetParamType( id ) == FF_TYPE_TEXT )
		{
			std::printf( "%-4u %-22s %-9s %10s   %-16s %s\n", id, name ? name : "", kind, "-", "-", "free text" );
			continue;
		}
		char rangeText[ 32 ] = {};
		std::snprintf( rangeText, sizeof( rangeText ), "[%g .. %g]", range.min, range.max );
		std::printf( "%-4u %-22s %-9s %10.4f   %-16s\n", id, name ? name : "", kind, plugin.GetFloatParameter( id ), rangeText );
	}
	return 0;
}

//---------------------------------------------------------------------------
// --pixels
//---------------------------------------------------------------------------
int runPixels()
{
	std::printf( "pixels\n\n" );
	Host host;
	if( !host.Open( 640, 360 ) )
		return 1;
	std::printf( "  GL %s\n\n", glGetString( GL_VERSION ) );

	Target hd;
	hd.Create( 1920, 1080 );

	{
		std::printf( "SMPTE RP 219 at 1920x1080\n" );
		Instance i( 1920, 1080 );
		i.quiet();
		Image    img = renderAt( i.plugin, hd, 0.0 );
		Check( img.is( 549, 300, 191, 191, 0 ), "75% yellow bar is (191, 191, 0), got " + img.str( 549, 300 ) );
		Check( img.is( 100, 300, 102, 102, 102 ), "40% grey block is (102, 102, 102), got " + img.str( 100, 300 ) );
		Check( img.is( 1000, 700, 191, 191, 191 ), "row B 75% white is 191, got " + img.str( 1000, 700 ) );
		Check( img.is( 1000, 1000, 0, 0, 0 ), "row D black is 0, got " + img.str( 1000, 1000 ) );
		Check( img.is( 700, 1000, 255, 255, 255 ), "row D 100% white is 255, got " + img.str( 700, 1000 ) );
		// The ramp: black at its left, white at its right, monotonic between.
		const std::vector< bars::Band > bands = bars::Rp219Layout( 1920, 1080 );
		const bars::Band*               ramp  = nullptr;
		for( const bars::Band& b : bands )
			if( b.fill == bars::Fill::Ramp )
				ramp = &b;
		bool mono = ramp != nullptr;
		if( ramp )
		{
			int last = -1;
			for( int x = ramp->x; x < ramp->x + ramp->w; ++x )
			{
				const int v = img.at( x, ramp->y + 5 )[ 0 ];
				mono        = mono && v >= last;
				last        = v;
			}
			Check( img.at( ramp->x, ramp->y + 5 )[ 0 ] == 0 && img.at( ramp->x + ramp->w - 1, ramp->y + 5 )[ 0 ] == 255,
				   "the luma ramp runs 0 to 255" );
		}
		Check( mono, "the luma ramp is monotonic" );

		i.set( PT_LEVELS, 1.0f );
		img = renderAt( i.plugin, hd, 0.0 );
		Check( img.is( 549, 300, 180, 180, 16 ), "legal range: 75% yellow is (180, 180, 16), got " + img.str( 549, 300 ) );
		Check( img.is( 1000, 1000, 16, 16, 16 ), "legal range: black is 16, got " + img.str( 1000, 1000 ) );
		// The pluge: at legal range the -2% patch is 12, distinguishable from
		// the 16 either side of it.
		const bars::Band* pluge = nullptr;
		for( const bars::Band& b : bands )
			if( std::string( b.name ) == "pluge -2%" )
				pluge = &b;
		Check( pluge != nullptr && img.is( pluge->x + 2, pluge->y + 10, 12, 12, 12 ), "legal range: pluge -2% is 12" );
	}

	{
		std::printf( "\n75% bars at 640x360\n" );
		Instance i( 640, 360 );
		i.quiet();
		i.set( PT_PATTERN, 1.0f );
		Image img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 40, 180, 191, 191, 191 ), "bar 1 is 75% white, got " + img.str( 40, 180 ) );
		Check( img.is( 120, 180, 191, 191, 0 ), "bar 2 is 75% yellow, got " + img.str( 120, 180 ) );
		Check( img.is( 600, 180, 0, 0, 0 ), "bar 8 is black, got " + img.str( 600, 180 ) );
		i.set( PT_PATTERN, 2.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 120, 180, 255, 255, 0 ), "100% bars: bar 2 is (255, 255, 0), got " + img.str( 120, 180 ) );
	}

	{
		std::printf( "\ngrid at 640x360, pitch 100, major every 5\n" );
		Instance i( 640, 360 );
		i.quiet();
		i.set( PT_PATTERN, 3.0f );
		i.set( PT_DIAGONALS, 0.0f );
		i.set( PT_CENTRE, 0.0f );
		Image img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 100, 50, 61, 125, 255 ), "x=100 is a minor line in the line colour, got " + img.str( 100, 50 ) );
		Check( img.is( 99, 50, 0, 0, 0 ) && img.is( 101, 50, 0, 0, 0 ), "a 1px line is one pixel wide" );
		Check( img.is( 500, 50, 255, 255, 255 ) && img.is( 499, 50, 255, 255, 255 ) && img.is( 501, 50, 255, 255, 255 ) &&
				   img.is( 498, 50, 0, 0, 0 ) && img.is( 502, 50, 0, 0, 0 ),
			   "x=500 is a major line, three pixels wide" );
		Check( img.is( 0, 50, 255, 255, 255 ) && img.is( 1, 50, 255, 255, 255 ) && img.is( 2, 50, 255, 255, 255 ),
			   "the left edge is a major line pulled fully inside" );
		Check( img.is( 639, 50, 255, 255, 255 ) && img.is( 636, 50, 0, 0, 0 ), "the right edge is a line even though the pitch misses it" );
		Check( img.is( 50, 100, 61, 125, 255 ), "y=100 is a minor line" );
		Check( img.is( 50, 50, 0, 0, 0 ), "a cell interior is the background" );

		i.set( PT_DIAGONALS, 1.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 320, 180, 61, 125, 255 ), "the diagonals cross at the centre" );

		i.set( PT_CENTRE, 1.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 320, 10, 255, 255, 255 ) && img.is( 10, 180, 255, 255, 255 ), "the centre target's cross is drawn in the major colour" );

		i.set( PT_CELL_LABELS, 1.0f );
		i.set( PT_DIAGONALS, 0.0f );
		i.set( PT_CENTRE, 0.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		int lit = 0;
		for( int y = 1; y < 100; ++y )
			for( int x = 1; x < 100; ++x )
				if( img.is( x, y, 255, 255, 255 ) )
					++lit;
		Check( lit > 20, "cell 1,1 carries a label (" + std::to_string( lit ) + " white pixels inside it)" );

		i.set( PT_GRID_MODE, 1.0f );
		i.set( PT_CELL_LABELS, 0.0f );
		i.set( PT_MAJOR_EVERY, 0.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 40, 50, 61, 125, 255 ) && img.is( 39, 50, 0, 0, 0 ), "16 divisions of 640: a line at x=40" );
		Check( img.is( 639, 50, 61, 125, 255 ), "the last division line lands on the far edge" );
	}

	{
		std::printf( "\nalignment at 640x360\n" );
		Instance i( 640, 360 );
		i.quiet();
		i.set( PT_PATTERN, 4.0f );
		Image img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 0, 0, 255, 255, 255 ) && img.is( 639, 359, 255, 255, 255 ) && img.is( 300, 0, 255, 255, 255 ) &&
				   img.is( 0, 200, 255, 255, 255 ),
			   "the edge border is on the outermost pixels" );
		Check( img.is( 200, 1, 0, 0, 0 ), "and only one pixel wide" );
		Check( img.is( 50, 1, 255, 255, 255 ) && img.is( 50, 2, 255, 255, 255 ) && img.is( 50, 3, 0, 0, 0 ),
			   "a corner arm is three pixels wide" );
		Check( img.is( 320, 180, 255, 255, 255 ), "the centre target crosses at the centre" );
		Check( img.is( 32, 100, 255, 255, 255 ), "the 10% safe area's left edge is at x=32" );
		Check( img.is( 64, 100, 255, 255, 255 ), "the 20% safe area's left edge is at x=64" );
		const int patch = std::max( 16, roundi( 360 / 20.0 ) );
		const int px = roundi( 640 * 0.25 ), py = roundi( 360 * 0.25 );
		Check( img.is( px, py, 255, 255, 255 ) && img.is( px + 1, py, 0, 0, 0 ) && img.is( px, py + 1, 0, 0, 0 ) &&
				   img.is( px + 1, py + 1, 255, 255, 255 ),
			   "a pixel patch is a single-pixel checkerboard (" + std::to_string( patch ) + "px)" );
	}

	{
		std::printf( "\nLED tiles at 640x360, 100x100 tiles, 50px modules\n" );
		Instance i( 640, 360 );
		i.quiet();
		i.set( PT_PATTERN, 5.0f );
		i.set( PT_TILE_W, 100.0f );
		i.set( PT_TILE_H, 100.0f );
		i.set( PT_MODULE_W, 50.0f );
		i.set( PT_MODULE_H, 50.0f );
		i.set( PT_TILE_LABELS, 0.0f );
		Image img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 0, 5, 255, 255, 255 ) && img.is( 100, 5, 255, 255, 255 ) && img.is( 200, 5, 255, 255, 255 ),
			   "cabinet borders at every 100px" );
		Check( img.is( 99, 5, 16, 16, 16, 1 ) && img.is( 101, 5, 42, 42, 42, 1 ), "the checker alternates across a border" );
		Check( img.is( 5, 5, 16, 16, 16, 1 ) && img.is( 105, 105, 16, 16, 16, 1 ), "and is the same colour diagonally" );
		Check( img.is( 50, 5, 135, 135, 135, 2 ), "a module line at half strength, got " + img.str( 50, 5 ) );
		Check( img.is( 49, 5, 16, 16, 16, 1 ) && img.is( 51, 5, 16, 16, 16, 1 ), "and one pixel wide" );

		i.set( PT_TILE_LABELS, 1.0f );
		i.set( PT_MODULE_W, 0.0f );
		i.set( PT_MODULE_H, 0.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		int lit = 0;
		for( int y = 2; y < 98; ++y )
			for( int x = 2; x < 98; ++x )
				if( img.is( x, y, 255, 255, 255 ) )
					++lit;
		Check( lit > 50, "tile A1 carries a label (" + std::to_string( lit ) + " white pixels inside it)" );

		i.set( PT_ORIGIN_X, 30.0f );
		i.set( PT_TILE_LABELS, 0.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 30, 5, 255, 255, 255 ) && img.is( 130, 5, 255, 255, 255 ) && img.is( 100, 5, 42, 42, 42, 1 ) && img.is( 5, 5, 16, 16, 16, 1 ),
			   "an origin offset moves every border, and the partial first column is still tile A" );
	}

	{
		std::printf( "\nsolid, greyscale, pixel check at 640x360\n" );
		Instance i( 640, 360 );
		i.quiet();
		i.set( PT_PATTERN, 6.0f );
		i.set( PT_FIELD, 2.0f );
		Image img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 320, 180, 255, 0, 0 ), "a red field is (255, 0, 0)" );
		i.set( PT_LEVEL, 0.5f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 320, 180, 128, 0, 0, 1 ), "at Level 0.5 it is (128, 0, 0), got " + img.str( 320, 180 ) );
		i.set( PT_FIELD, 10.0f );
		i.set( PT_LEVEL, 1.0f );
		i.set( PT_CUSTOM_G, 0.5f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 320, 180, 255, 128, 255, 1 ), "the custom field is the custom colour, got " + img.str( 320, 180 ) );

		i.set( PT_PATTERN, 7.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 0, 180, 0, 0, 0 ) && img.is( 639, 180, 255, 255, 255 ), "an 11-step wedge runs black to white" );
		Check( img.is( 100, 180, 26, 26, 26, 1 ) && img.is( 300, 180, 128, 128, 128, 1 ), "with flat steps at tenths (0.1 and 0.5)" );
		i.set( PT_STEPS, 0.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		bool mono = true;
		for( int x = 1; x < 640; ++x )
			mono = mono && img.at( x, 180 )[ 0 ] >= img.at( x - 1, 180 )[ 0 ];
		Check( mono, "steps 0 is a continuous monotonic ramp" );
		i.set( PT_PER_CHANNEL, 1.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 639, 135, 255, 0, 0 ) && img.is( 639, 225, 0, 255, 0 ) && img.is( 639, 315, 0, 0, 255 ),
			   "per channel: red, green and blue strips" );

		i.set( PT_PATTERN, 8.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 0, 0, 255, 255, 255 ) && img.is( 1, 0, 0, 0, 0 ) && img.is( 0, 1, 0, 0, 0 ) && img.is( 1, 1, 255, 255, 255 ),
			   "the left half is a single-pixel checkerboard" );
		Check( img.is( 400, 0, 255, 255, 255 ) && img.is( 400, 1, 0, 0, 0 ) && img.is( 500, 1, 0, 0, 0 ),
			   "the top-right is single-pixel horizontal lines" );
		Check( img.is( 320, 300, 255, 255, 255 ) && img.is( 321, 300, 0, 0, 0 ) && img.is( 321, 350, 0, 0, 0 ),
			   "the bottom-right is single-pixel vertical lines" );
		i.set( PT_CELL, 4.0f );
		i.set( PT_BURSTS, 0.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		Check( img.is( 3, 3, 255, 255, 255 ) && img.is( 4, 3, 0, 0, 0 ), "a 4px checker" );
	}

	{
		std::printf( "\nthe marker and the burn-in at 640x360\n" );
		Instance i( 640, 360 );
		i.set( PT_PATTERN, 6.0f );// white field
		i.set( PT_BURNIN, 0.0f );
		i.set( PT_MOTION, 1.0f );
		Image img = renderAt( i.plugin, host.target, 0.25 );
		// One crossing a second, so at t=0.25 the bar's left edge is at
		// round(0.25 * (640 + 16)) - 16 = 148.
		Check( img.is( 148, 180, 0, 0, 0 ) && img.is( 163, 180, 0, 0, 0 ) && img.is( 147, 180, 255, 255, 255 ) &&
				   img.is( 164, 180, 255, 255, 255 ),
			   "sweep across: the bar inverts x=148..163 at t=0.25, got " + img.str( 148, 180 ) + " " + img.str( 147, 180 ) );
		Image later = renderAt( i.plugin, host.target, 0.5 );
		Check( later.is( 148, 180, 255, 255, 255 ) && later.is( 312, 180, 0, 0, 0 ), "and has moved on at t=0.5" );
		i.set( PT_SPEED, 1.0f );
		later = renderAt( i.plugin, host.target, 0.25 );
		Check( !later.is( 148, 180, 0, 0, 0 ), "Speed changes where it is" );
		i.set( PT_MOTION, 3.0f );
		i.set( PT_MARKER_SIZE, 32.0f );
		later = renderAt( i.plugin, host.target, 0.0 );
		int dark = 0;
		for( int y = 0; y < 360; ++y )
			for( int x = 0; x < 640; ++x )
				if( later.is( x, y, 0, 0, 0 ) )
					++dark;
		Check( dark == 32 * 32, "bounce: a 32px box (" + std::to_string( dark ) + " inverted pixels)" );

		i.set( PT_MOTION, 0.0f );
		i.set( PT_BURNIN, 1.0f );
		i.set( PT_PLATE, 1.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		int white = 0, plate = 0;
		for( int y = 0; y < 60; ++y )
			for( int x = 0; x < 300; ++x )
			{
				if( img.is( x, y, 255, 255, 255 ) )
					++white;
				if( img.is( x, y, 64, 64, 64, 2 ) )
					++plate;
			}
		Check( plate > 200, "the plate darkens the field behind the burn-in (" + std::to_string( plate ) + " plate pixels)" );
		Check( white > 50, "the resolution is written on it (" + std::to_string( white ) + " white pixels)" );
		i.set( PT_PLATE, 0.0f );
		i.set( PT_SHOW_RES, 0.0f );
		i.plugin.SetTextParameter( PT_TEXT, "" );
		img = renderAt( i.plugin, host.target, 0.0 );
		white = 0;
		for( int y = 0; y < 60; ++y )
			for( int x = 0; x < 300; ++x )
				if( !img.is( x, y, 255, 255, 255 ) )
					++white;
		Check( white == 0, "with nothing to say the burn-in draws nothing" );
		i.plugin.SetTextParameter( PT_TEXT, "HELLO" );
		img = renderAt( i.plugin, host.target, 0.0 );
		int ink = 0;
		for( int y = 0; y < 60; ++y )
			for( int x = 0; x < 300; ++x )
				if( !img.is( x, y, 255, 255, 255 ) )
					++ink;
		// Without a plate the white text is invisible on a white field, so
		// switch to a black one and count.
		i.set( PT_FIELD, 1.0f );
		img = renderAt( i.plugin, host.target, 0.0 );
		ink = 0;
		for( int y = 0; y < 60; ++y )
			for( int x = 0; x < 300; ++x )
				if( img.is( x, y, 255, 255, 255 ) )
					++ink;
		Check( ink > 50, "custom text is drawn (" + std::to_string( ink ) + " pixels)" );
		i.set( PT_TEXT_POS, 3.0f );// bottom centre
		img = renderAt( i.plugin, host.target, 0.0 );
		int top = 0, bottom = 0;
		for( int y = 0; y < 360; ++y )
			for( int x = 0; x < 640; ++x )
				if( img.is( x, y, 255, 255, 255 ) )
					( y < 180 ? top : bottom )++;
		Check( top == 0 && bottom > 50, "Position moves it to the bottom" );
		i.set( PT_FRAME_COUNTER, 1.0f );
		Image a = renderAt( i.plugin, host.target, 0.0 );
		Image b = renderAt( i.plugin, host.target, 1.0 / 60.0 );
		Check( a.px != b.px, "the frame counter changes every frame" );
	}

	std::printf( "\n  %s\n", failures == 0 ? "PASS" : "FAIL" );
	return failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// --out
//---------------------------------------------------------------------------
int runOut( const std::string& path, int w, int h, int frames, const std::vector< std::pair< std::string, std::string > >& sets )
{
	Host host;
	if( !host.Open( w, h ) )
		return 1;
	Instance i( w, h );
	for( const auto& kv : sets )
	{
		bool found = false;
		for( unsigned int id = 0; id < PT_COUNT; ++id )
		{
			const char* name = i.plugin.GetParamName( id );
			if( name != nullptr && kv.first == name )
			{
				if( i.plugin.GetParamType( id ) == FF_TYPE_TEXT )
					i.plugin.SetTextParameter( id, kv.second.c_str() );
				else
					i.plugin.SetFloatParameter( id, static_cast< float >( std::atof( kv.second.c_str() ) ) );
				found = true;
				break;
			}
		}
		if( !found )
		{
			std::printf( "no parameter named '%s'\n", kv.first.c_str() );
			return 1;
		}
	}
	Image img;
	for( int f = 0; f < std::max( 1, frames ); ++f )
		img = renderAt( i.plugin, host.target, f / 60.0 );
	if( !writePng( path, w, h, img.px ) )
	{
		std::printf( "could not write %s\n", path.c_str() );
		return 1;
	}
	return 0;
}

int main( int argc, char** argv )
{
	std::string                                            out;
	int                                                    w = 640, h = 360, frames = 1;
	std::vector< std::pair< std::string, std::string > >   sets;
	for( int a = 1; a < argc; ++a )
	{
		const std::string arg = argv[ a ];
		if( arg == "--font" )
			return runFont();
		if( arg == "--colours" )
			return runColours();
		if( arg == "--bars" && a + 1 < argc )
			return runBars( argv[ a + 1 ] );
		if( arg == "--defaults" )
			return runDefaults();
		if( arg == "--names" )
			return runNames();
		if( arg == "--list" )
			return runList();
		if( arg == "--pixels" )
			return runPixels();
		if( arg == "--out" && a + 1 < argc )
			out = argv[ ++a ];
		else if( arg == "--size" && a + 1 < argc )
			std::sscanf( argv[ ++a ], "%dx%d", &w, &h );
		else if( arg == "--frames" && a + 1 < argc )
			frames = std::atoi( argv[ ++a ] );
		else if( arg == "--set" && a + 1 < argc )
		{
			const std::string kv = argv[ ++a ];
			const size_t      eq = kv.find( '=' );
			if( eq == std::string::npos )
			{
				std::printf( "--set wants Name=value\n" );
				return 1;
			}
			sets.emplace_back( kv.substr( 0, eq ), kv.substr( eq + 1 ) );
		}
	}
	if( !out.empty() )
		return runOut( out, w, h, frames, sets );
	std::printf( "usage: gttest --font | --colours | --bars fixture.json | --defaults | --names | --list | --pixels\n"
				 "       gttest --out f.png [--size WxH] [--frames N] [--set Name=value ...]\n" );
	return 2;
}
