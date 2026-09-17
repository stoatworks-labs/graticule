#include "Shaders.h"

namespace graticule
{
const char* const kVertexShader = R"(#version 410 core
// One triangle that covers the viewport. No vertex buffer: the corners come
// from gl_VertexID, so the only thing bound at draw time is the VAO.
void main()
{
	vec2 corner = vec2( ( gl_VertexID == 1 ) ? 3.0 : -1.0, ( gl_VertexID == 2 ) ? 3.0 : -1.0 );
	gl_Position = vec4( corner, 0.0, 1.0 );
}
)";

const char* const kFragmentShader = R"(#version 410 core
out vec4 fragColour;

uniform ivec2 uSize;
uniform int   uPattern;
uniform int   uLegal;

// Grid
uniform int   uGridMode;// 0 pitch, 1 divisions
uniform int   uPitch;
uniform ivec2 uDiv;
uniform int   uLineW;
uniform int   uMajorEvery;
uniform int   uMajorW;
uniform int   uDiagonals;
uniform int   uCentre;
uniform int   uOriginCentre;
uniform int   uCellLabels;
uniform vec3  uLineCol;
uniform vec3  uMajorCol;
uniform vec3  uBgCol;

// LED tiles
uniform ivec2 uTile;
uniform ivec2 uModule;
uniform ivec2 uOrigin;
uniform int   uChecker;
uniform int   uTileLabels;
uniform int   uBorderW;
uniform vec3  uTileA;
uniform vec3  uTileB;
uniform vec3  uBorderCol;

// Solid
uniform vec3  uSolid;// unit space: 0 = reference black, 1 = reference white

// Greyscale
uniform int   uSteps;
uniform int   uVertical;
uniform int   uPerChannel;

// Pixel check
uniform int   uCell;
uniform int   uBursts;

// Alignment
uniform int   uEdgeBorder;
uniform int   uCorners;
uniform int   uCornerSize;
uniform int   uTarget;
uniform int   uSafeA;
uniform int   uSafeB;
uniform int   uPatches;
uniform int   uAlignDiag;

// Colour bars: rectangles in top-down pixels, colours in unit space, .a = 1
// for the luma ramp.
uniform int   uBandCount;
uniform ivec4 uBandRect[ 48 ];
uniform vec4  uBandCol[ 48 ];

// Motion marker, top-down pixels; w = 0 means none.
uniform ivec4 uMarker;

// Burn-in
uniform sampler2D uFont;
uniform int   uTextScale;
uniform int   uLineCount;
uniform ivec4 uLine[ 3 ];// offset, length, x, y
uniform int   uText[ 96 ];
uniform ivec4 uPlate;// x, y, w, h; w = 0 means none

// ---------------------------------------------------------------------------
// Arithmetic that is safe on negative operands.
// ---------------------------------------------------------------------------
int idiv( int a, int b ) { return int( floor( float( a ) / float( b ) ) ); }
int imod( int a, int b ) { return a - b * idiv( a, b ); }
int iround( float v ) { return int( floor( v + 0.5 ) ); }

// The level range. Everything measured -- bars, ramps, fields, the pixel
// checker -- passes through here on its way out; the grid, tile and alignment
// colours are the operator's RGB and do not.
vec3 toOut( vec3 unit )
{
	if( uLegal == 1 )
		return clamp( ( 16.0 + unit * 219.0 ) / 255.0, 0.0, 1.0 );
	return clamp( unit, 0.0, 1.0 );
}

// A line of width w centred on coordinate v, as Test Card's crispLine lays it:
// an odd width covers v itself and the same number of pixels each side; an
// even width covers one more pixel below than above. At the raster's edges the
// line is pulled fully inside so a border is always visible.
bool lineHit( int p, int v, int w, int extent )
{
	int lo = v - w / 2;
	if( v <= 0 )
		lo = 0;
	else if( v >= extent )
		lo = extent - w;
	return p >= lo && p < lo + w;
}

bool inRect( ivec2 p, ivec4 r )
{
	return p.x >= r.x && p.y >= r.y && p.x < r.x + r.z && p.y < r.y + r.w;
}

// ---------------------------------------------------------------------------
// Text. A small buffer of character codes, filled per pixel for the labels
// that depend on where the pixel is.
// ---------------------------------------------------------------------------
int gBuf[ 16 ];
int gLen = 0;

void tClear() { gLen = 0; }

void tPush( int c )
{
	if( gLen < 16 )
	{
		gBuf[ gLen ] = c;
		gLen++;
	}
}

void tInt( int v )
{
	if( v < 0 )
	{
		tPush( 45 );
		v = -v;
	}
	int d[ 8 ];
	int n = 0;
	do
	{
		d[ n ] = v - 10 * ( v / 10 );
		v = v / 10;
		n++;
	} while( v > 0 && n < 8 );
	for( int i = n - 1; i >= 0; i-- )
		tPush( 48 + d[ i ] );
}

// Column letters, A..Z then AA, AB, ...: bijective base 26, as Test Card's
// columnRef and every spreadsheet.
void tCol( int index )
{
	int n = index;
	int tmp[ 4 ];
	int m = 0;
	do
	{
		tmp[ m ] = imod( n, 26 );
		n = idiv( n, 26 ) - 1;
		m++;
	} while( n >= 0 && m < 4 );
	for( int i = m - 1; i >= 0; i-- )
		tPush( 65 + tmp[ i ] );
}

int tWidth( int s ) { return gLen * 6 * s - s; }

bool glyphBit( int code, int lx, int ly )
{
	return texelFetch( uFont, ivec2( code * 5 + lx, ly ), 0 ).r > 0.5;
}

// Is pixel p lit by the buffered string drawn at origin, scale s?
bool tHit( ivec2 p, ivec2 origin, int s )
{
	int dx = p.x - origin.x;
	int dy = p.y - origin.y;
	if( dx < 0 || dy < 0 )
		return false;
	int ly = dy / s;
	if( ly >= 7 )
		return false;
	int adv = 6 * s;
	int ci = dx / adv;
	if( ci >= gLen )
		return false;
	int lx = ( dx - ci * adv ) / s;
	if( lx >= 5 )
		return false;
	return glyphBit( gBuf[ ci ], lx, ly );
}

// The same for a burn-in line living in the uniform array.
bool uHit( ivec2 p, int li )
{
	ivec4 L = uLine[ li ];
	int s = uTextScale;
	int dx = p.x - L.z;
	int dy = p.y - L.w;
	if( dx < 0 || dy < 0 )
		return false;
	int ly = dy / s;
	if( ly >= 7 )
		return false;
	int adv = 6 * s;
	int ci = dx / adv;
	if( ci >= L.y )
		return false;
	int lx = ( dx - ci * adv ) / s;
	if( lx >= 5 )
		return false;
	return glyphBit( uText[ L.x + ci ], lx, ly );
}

// ---------------------------------------------------------------------------
// Grid lines along one axis.
//
// Returns 0 for no line, 1 for a minor line, 2 for a major line. `cell` comes
// back as the index of the cell the pixel is in, counted so the cell holding
// pixel 0 is cell 1 -- the number a label shows.
// ---------------------------------------------------------------------------
int gridAxis( int p, int extent, int div, out int cell )
{
	int hit = 0;
	if( uGridMode == 1 )
	{
		// Divisions: lines spread so the last one lands exactly on the far edge.
		int n = max( 1, div );
		int k0 = idiv( p * n, extent );
		cell = k0 + 1;
		for( int k = k0 - 1; k <= k0 + 1; k++ )
		{
			if( k < 0 || k > n )
				continue;
			int v = iround( float( extent ) * float( k ) / float( n ) );
			bool major = uMajorEvery > 0 && imod( k, uMajorEvery ) == 0;
			int w = major ? uMajorW : uLineW;
			if( lineHit( p, v, w, extent ) )
				hit = max( hit, major ? 2 : 1 );
		}
		// The cell index from the line positions, not the estimate above.
		int v0 = iround( float( extent ) * float( k0 ) / float( n ) );
		if( p < v0 )
			cell = k0;
		else if( p >= iround( float( extent ) * float( k0 + 1 ) / float( n ) ) )
			cell = k0 + 2;
		return hit;
	}

	int step = max( 1, uPitch );
	int origin = uOriginCentre == 1 ? extent / 2 : 0;
	int k0 = idiv( p - origin, step );
	// Cells are numbered from the first one that touches the raster.
	int kFirst = idiv( 0 - origin, step );
	cell = k0 - kFirst + 1;
	for( int k = k0 - 1; k <= k0 + 1; k++ )
	{
		int v = origin + k * step;
		if( v < 0 || v > extent )
			continue;
		bool major = uMajorEvery > 0 && imod( k, uMajorEvery ) == 0;
		int w = major ? uMajorW : uLineW;
		if( lineHit( p, v, w, extent ) )
			hit = max( hit, major ? 2 : 1 );
	}
	// The raster edge is always worth a line even when the pitch misses it.
	int lastOnPitch = origin + idiv( extent - origin, step ) * step;
	if( lastOnPitch != extent && lineHit( p, extent, uMajorW, extent ) )
		hit = 2;
	if( uOriginCentre == 1 )
	{
		int firstOnPitch = origin - idiv( origin, step ) * step;
		if( firstOnPitch != 0 && lineHit( p, 0, uMajorW, extent ) )
			hit = 2;
	}
	return hit;
}

// Extent of one cell along an axis, for sizing labels.
int cellSize( int extent, int div )
{
	if( uGridMode == 1 )
		return extent / max( 1, div );
	return max( 1, uPitch );
}

// The cell's rectangle along one axis, from its 1-based index.
ivec2 cellSpan( int cell, int extent, int div )
{
	if( uGridMode == 1 )
	{
		int n = max( 1, div );
		int a = iround( float( extent ) * float( cell - 1 ) / float( n ) );
		int b = iround( float( extent ) * float( cell ) / float( n ) );
		return ivec2( a, b );
	}
	int step = max( 1, uPitch );
	int origin = uOriginCentre == 1 ? extent / 2 : 0;
	int kFirst = idiv( 0 - origin, step );
	int k = cell - 1 + kFirst;
	return ivec2( origin + k * step, origin + ( k + 1 ) * step );
}

bool diagonalHit( ivec2 p, int w )
{
	vec2 c = vec2( p ) + 0.5;
	vec2 S = vec2( uSize );
	float len = length( S );
	float d1 = abs( S.y * c.x - S.x * c.y ) / len;
	float d2 = abs( S.y * c.x + S.x * c.y - S.x * S.y ) / len;
	return min( d1, d2 ) <= float( w ) * 0.5;
}

bool targetHit( ivec2 p, int w )
{
	vec2 c = vec2( uSize ) * 0.5;
	if( lineHit( p.x, iround( c.x ), w, uSize.x ) || lineHit( p.y, iround( c.y ), w, uSize.y ) )
		return true;
	float r = min( c.x, c.y );
	float d = length( vec2( p ) + 0.5 - c );
	for( int i = 1; i <= 4; i++ )
		if( abs( d - r * float( i ) * 0.25 ) <= float( w ) * 0.5 )
			return true;
	return false;
}

// A 1px checkerboard patch: fg on even parity, bg on odd.
bool checkerBit( ivec2 p, int cell )
{
	int c = max( 1, cell );
	return imod( idiv( p.x, c ) + idiv( p.y, c ), 2 ) == 0;
}

// ---------------------------------------------------------------------------
// The patterns.
// ---------------------------------------------------------------------------
vec3 bars( ivec2 p )
{
	for( int i = 0; i < uBandCount; i++ )
	{
		ivec4 r = uBandRect[ i ];
		if( inRect( p, r ) )
		{
			vec4 c = uBandCol[ i ];
			if( c.a > 0.5 )
			{
				float t = r.z <= 1 ? 0.0 : float( p.x - r.x ) / float( r.z - 1 );
				return toOut( vec3( t ) );
			}
			return toOut( c.rgb );
		}
	}
	return toOut( vec3( 0.0 ) );
}

vec3 grid( ivec2 p )
{
	vec3 col = uBgCol;
	if( uDiagonals == 1 && diagonalHit( p, uLineW ) )
		col = uLineCol;

	int cx, cy;
	int hx = gridAxis( p.x, uSize.x, uDiv.x, cx );
	int hy = gridAxis( p.y, uSize.y, uDiv.y, cy );
	int hit = max( hx, hy );
	if( hit == 1 )
		col = uLineCol;
	else if( hit == 2 )
		col = uMajorCol;

	if( uCellLabels == 1 )
	{
		int cw = cellSize( uSize.x, uDiv.x );
		int ch = cellSize( uSize.y, uDiv.y );
		// Below about eight pixels of cap height the labels are an unreadable
		// smear that just dirties the pattern, so skip them rather than draw
		// noise.
		float size = float( min( cw, ch ) ) / 5.0;
		if( size >= 8.0 )
		{
			int s = max( 1, int( size / 7.0 ) );
			tClear();
			tInt( cx );
			tPush( 44 );
			tInt( cy );
			ivec2 sx = cellSpan( cx, uSize.x, uDiv.x );
			ivec2 sy = cellSpan( cy, uSize.y, uDiv.y );
			ivec2 centre = ivec2( ( sx.x + sx.y ) / 2, ( sy.x + sy.y ) / 2 );
			ivec2 origin = centre - ivec2( tWidth( s ) / 2, ( 7 * s ) / 2 );
			if( tHit( p, origin, s ) )
				col = uMajorCol;
		}
	}

	if( uCentre == 1 && targetHit( p, uMajorW ) )
		col = uMajorCol;
	return col;
}

vec3 alignment( ivec2 p )
{
	vec3 col = uBgCol;
	vec3 fg = uMajorCol;
	int W = uSize.x;
	int H = uSize.y;
	int mn = min( W, H );

	if( uAlignDiag == 1 && diagonalHit( p, 1 ) )
		col = fg;
	if( uTarget == 1 && targetHit( p, 1 ) )
		col = fg;

	// Safe areas: nested rectangles at a percentage inset, each labelled with
	// the area it encloses.
	for( int i = 0; i < 2; i++ )
	{
		int pct = i == 0 ? uSafeA : uSafeB;
		if( pct <= 0 )
			continue;
		int ix = iround( float( W * pct ) / 200.0 );
		int iy = iround( float( H * pct ) / 200.0 );
		int rw = W - ix * 2;
		int rh = H - iy * 2;
		bool onX = ( p.x == ix || p.x == ix + rw - 1 ) && p.y >= iy && p.y < iy + rh;
		bool onY = ( p.y == iy || p.y == iy + rh - 1 ) && p.x >= ix && p.x < ix + rw;
		if( onX || onY )
			col = fg;
		// Labelled inside its bottom-left corner, clear of the burn-in, which
		// sits top-left by default.
		int s = max( 1, int( max( 10.0, float( mn ) / 60.0 ) / 7.0 ) );
		tClear();
		tInt( 100 - pct );
		tPush( 37 );
		if( tHit( p, ivec2( ix + 4, iy + rh - 4 - 7 * s ), s ) )
			col = fg;
	}

	if( uCorners == 1 )
	{
		int sz = max( 8, uCornerSize );
		// L-brackets, three pixels wide, fully inside the raster.
		bool nearX = p.x < 3 || p.x >= W - 3;
		bool nearY = p.y < 3 || p.y >= H - 3;
		bool armX = nearY && ( p.x < sz || p.x >= W - sz );
		bool armY = nearX && ( p.y < sz || p.y >= H - sz );
		if( armX || armY )
			col = fg;
		// The pixel count sits beside the bottom-left bracket, clear of the
		// burn-in.
		int s = max( 1, int( max( 10.0, float( mn ) / 50.0 ) / 7.0 ) );
		tClear();
		tInt( sz );
		tPush( 112 );
		tPush( 120 );
		if( tHit( p, ivec2( sz + 6, H - 6 - 7 * s ), s ) )
			col = fg;
	}

	if( uPatches == 1 )
	{
		// Single-pixel checkerboards at four spots. If any scaling is
		// happening anywhere in the chain these turn into flat grey.
		int patchPx = max( 16, iround( float( mn ) / 20.0 ) );
		ivec2 spots[ 4 ];
		spots[ 0 ] = ivec2( iround( float( W ) * 0.25 ), iround( float( H ) * 0.25 ) );
		spots[ 1 ] = ivec2( iround( float( W ) * 0.75 ) - patchPx, iround( float( H ) * 0.25 ) );
		spots[ 2 ] = ivec2( iround( float( W ) * 0.25 ), iround( float( H ) * 0.75 ) - patchPx );
		spots[ 3 ] = ivec2( iround( float( W ) * 0.75 ) - patchPx, iround( float( H ) * 0.75 ) - patchPx );
		for( int i = 0; i < 4; i++ )
			if( inRect( p, ivec4( spots[ i ], patchPx, patchPx ) ) )
				col = checkerBit( p - spots[ i ], 1 ) ? fg : uBgCol;
	}

	// Drawn last so nothing overlaps it: this is the line that proves the
	// raster is not being cropped or overscanned.
	if( uEdgeBorder == 1 && ( p.x == 0 || p.y == 0 || p.x == W - 1 || p.y == H - 1 ) )
		col = fg;
	return col;
}

vec3 ledTiles( ivec2 p )
{
	int tw = max( 1, uTile.x );
	int th = max( 1, uTile.y );
	// Start at or before the origin so a wall with an offset origin still gets
	// its partial first row and column drawn rather than silently skipped.
	int startX = uOrigin.x - idiv( max( 0, uOrigin.x ) + tw - 1, tw ) * tw;
	int startY = uOrigin.y - idiv( max( 0, uOrigin.y ) + th - 1, th ) * th;
	int col = idiv( p.x - startX, tw );
	int row = idiv( p.y - startY, th );
	int x0 = startX + col * tw;
	int y0 = startY + row * th;

	bool parity = imod( col + row, 2 ) == 0;
	vec3 fill = ( uChecker == 1 && !parity ) ? uTileB : uTileA;
	vec3 out_ = fill;

	// Module lines inside the tile, one pixel, half-strength.
	if( uModule.x > 0 )
	{
		int lx = p.x - x0;
		if( lx > 0 && imod( lx, uModule.x ) == 0 )
			out_ = mix( fill, uBorderCol, 0.5 );
	}
	if( uModule.y > 0 )
	{
		int ly = p.y - y0;
		if( ly > 0 && imod( ly, uModule.y ) == 0 )
			out_ = mix( fill, uBorderCol, 0.5 );
	}

	// Cabinet borders at every tile boundary.
	if( uBorderW > 0 )
	{
		bool hit = false;
		for( int k = 0; k <= 1; k++ )
		{
			int vx = x0 + k * tw;
			int vy = y0 + k * th;
			if( vx >= 0 && vx <= uSize.x && lineHit( p.x, vx, uBorderW, uSize.x ) )
				hit = true;
			if( vy >= 0 && vy <= uSize.y && lineHit( p.y, vy, uBorderW, uSize.y ) )
				hit = true;
		}
		if( hit )
			out_ = uBorderCol;
	}

	if( uTileLabels == 1 )
	{
		tClear();
		tCol( col );
		tInt( row + 1 );
		// Fit the label to about 70% of the tile width and half its height.
		int s = min( ( tw * 7 ) / ( 10 * ( 6 * gLen - 1 ) ), th / 14 );
		if( s >= 1 )
		{
			ivec2 centre = ivec2( x0 + tw / 2, y0 + th / 2 );
			ivec2 origin = centre - ivec2( tWidth( s ) / 2, ( 7 * s ) / 2 );
			if( tHit( p, origin, s ) )
				out_ = uBorderCol;
		}
	}
	return out_;
}

vec3 greyscale( ivec2 p )
{
	int strips = uPerChannel == 1 ? 4 : 1;
	int along = uVertical == 1 ? uSize.y : uSize.x;
	int across = uVertical == 1 ? uSize.x : uSize.y;
	int i = uVertical == 1 ? p.y : p.x;
	int pos = uVertical == 1 ? p.x : p.y;

	float raw = along <= 1 ? 0.0 : float( i ) / float( along - 1 );
	// A stepped wedge quantises the position, not the colour, so each step is
	// a flat patch of one exact code value.
	float t = raw;
	if( uSteps > 0 )
		t = min( 1.0, floor( raw * float( uSteps ) ) / float( max( 1, uSteps - 1 ) ) );

	float ss = float( across ) / float( strips );
	int si = int( float( pos ) / ss );
	if( pos < iround( float( si ) * ss ) )
		si--;
	else if( pos >= iround( float( si + 1 ) * ss ) )
		si++;
	si = clamp( si, 0, strips - 1 );

	vec3 mult = vec3( 1.0 );
	if( strips == 4 )
		mult = si == 0 ? vec3( 1.0 ) : si == 1 ? vec3( 1.0, 0.0, 0.0 ) : si == 2 ? vec3( 0.0, 1.0, 0.0 ) : vec3( 0.0, 0.0, 1.0 );
	return toOut( t * mult );
}

vec3 pixelCheck( ivec2 p )
{
	vec3 white = toOut( vec3( 1.0 ) );
	vec3 black = toOut( vec3( 0.0 ) );
	int c = max( 1, uCell );
	if( uBursts == 0 )
		return checkerBit( p, c ) ? white : black;

	// Left half checkerboard, right half split into horizontal and vertical
	// bursts. On LED these three behave differently under a scan-rate problem.
	int half_ = iround( float( uSize.x ) / 2.0 );
	if( p.x < half_ )
		return checkerBit( p, c ) ? white : black;
	int mid = iround( float( uSize.y ) / 2.0 );
	if( p.y < mid )
		return imod( p.y, 2 * c ) < c ? white : black;
	return imod( p.x - half_, 2 * c ) < c ? white : black;
}

void main()
{
	ivec2 p = ivec2( int( gl_FragCoord.x ), uSize.y - 1 - int( gl_FragCoord.y ) );

	vec3 col;
	if( uPattern <= 2 )
		col = bars( p );
	else if( uPattern == 3 )
		col = grid( p );
	else if( uPattern == 4 )
		col = alignment( p );
	else if( uPattern == 5 )
		col = ledTiles( p );
	else if( uPattern == 6 )
		col = toOut( uSolid );
	else if( uPattern == 7 )
		col = greyscale( p );
	else
		col = pixelCheck( p );

	// The motion marker inverts whatever is under it, so it is visible on
	// every pattern and every field.
	if( uMarker.z > 0 && inRect( p, uMarker ) )
		col = vec3( 1.0 ) - col;

	if( uLineCount > 0 )
	{
		if( uPlate.z > 0 && inRect( p, uPlate ) )
			col = mix( col, vec3( 0.0 ), 0.75 );
		for( int i = 0; i < uLineCount; i++ )
			if( uHit( p, i ) )
				col = vec3( 1.0 );
	}

	fragColour = vec4( col, 1.0 );
}
)";

} // namespace graticule
