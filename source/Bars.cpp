#include "Bars.h"

#include <cmath>

namespace graticule::bars
{
namespace
{
constexpr double kKr = 0.2126;
constexpr double kKb = 0.0722;
constexpr double kKg = 1.0 - kKr - kKb;// 0.7152

// The derived Pb/Pr scale factors:
//   R = Y + 2(1-Kr)Pr                    -> 1.5748
//   B = Y + 2(1-Kb)Pb                    -> 1.8556
//   G = Y - (2Kr(1-Kr)/Kg)Pr - (2Kb(1-Kb)/Kg)Pb -> 0.4681, 0.1873
constexpr double kRPr = 1.5748;
constexpr double kBPb = 1.8556;
constexpr double kGPr = 0.4681;
constexpr double kGPb = 0.1873;

Ycbcr Bar( double r, double g, double b, double pct = 100.0 )
{
	const double a = pct / 100.0;
	return RgbToYcbcr( r * a, g * a, b * a );
}

struct ColSpec
{
	const char* name;
	double      w;
	Fill        fill;
	Ycbcr       colour;
};

ColSpec Flat( const char* name, double w, const Ycbcr& c )
{
	return { name, w, Fill::Flat, c };
}

constexpr double kA = 1.0 / 8.0;
constexpr double kB = 3.0 / 28.0;
} // namespace

Ycbcr RgbToYcbcr( double r, double g, double b )
{
	const double y = kKr * r + kKg * g + kKb * b;
	return { kLumaBlack + y * kLumaSpan,
			 128.0 + ( kChromaSpan * ( b - y ) ) / ( 2.0 * ( 1.0 - kKb ) ),
			 128.0 + ( kChromaSpan * ( r - y ) ) / ( 2.0 * ( 1.0 - kKr ) ) };
}

Unit YcbcrToUnit( const Ycbcr& c )
{
	const double y  = ( c.y - kLumaBlack ) / kLumaSpan;
	const double pb = ( c.cb - 128.0 ) / kChromaSpan;
	const double pr = ( c.cr - 128.0 ) / kChromaSpan;
	return { y + kRPr * pr, y - kGPb * pb - kGPr * pr, y + kBPb * pb };
}

Ycbcr GreyPct( double pct )
{
	return { kLumaBlack + ( pct / 100.0 ) * kLumaSpan, 128.0, 128.0 };
}

int UnitToCode( double v, Levels range )
{
	if( range == Levels::Legal )
	{
		// Studio swing keeps headroom and footroom, so do NOT clamp to 0..1
		// first -- sub-black and super-white are representable and are the
		// entire point of the pluge.
		const double code = kLumaBlack + v * kLumaSpan;
		return static_cast< int >( std::lround( std::fmin( 255.0, std::fmax( 0.0, code ) ) ) );
	}
	const double c = v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
	return static_cast< int >( std::lround( c * 255.0 ) );
}

const Palette& Colours()
{
	static const Palette p = [] {
		Palette c;
		c.grey40   = GreyPct( 40 ); // measured Y=104
		c.grey15   = GreyPct( 15 ); // measured Y=49
		c.black    = GreyPct( 0 );  // measured Y=16
		c.white100 = GreyPct( 100 );// measured Y=235
		c.white75  = GreyPct( 75 ); // measured Y=180

		c.yellow75  = Bar( 1, 1, 0, 75 );// measured 168, 44, 136
		c.cyan75    = Bar( 0, 1, 1, 75 );// measured 145, 147, 44
		c.green75   = Bar( 0, 1, 0, 75 );// measured 133, 63, 52
		c.magenta75 = Bar( 1, 0, 1, 75 );// measured 63, 193, 204
		c.red75     = Bar( 1, 0, 0, 75 );// measured 51, 109, 212
		c.blue75    = Bar( 0, 0, 1, 75 );// measured 28, 212, 120

		c.cyan100   = Bar( 0, 1, 1 );// measured 188, 154, 16
		c.blue100   = Bar( 0, 0, 1 );// measured 32, 240, 118
		c.yellow100 = Bar( 1, 1, 0 );// measured 219, 16, 138
		c.red100    = Bar( 1, 0, 0 );// measured 63, 102, 240

		// The +I and +Q chroma reference signals. Not derivable -- measured.
		c.plusI = { 57, 156, 97 };
		c.plusQ = { 44, 171, 147 };

		c.plugeMinus2 = GreyPct( -2 );// measured Y=12
		c.plugePlus2  = GreyPct( 2 ); // measured Y=20
		c.plugePlus4  = GreyPct( 4 ); // measured Y=25
		return c;
	}();
	return p;
}

std::vector< Segment > LayOut( const std::vector< double >& weights, int total )
{
	double sum = 0.0;
	for( double w : weights )
		sum += w;

	std::vector< Segment > out;
	out.reserve( weights.size() );
	double acc  = 0.0;
	int    prev = 0;
	for( size_t i = 0; i < weights.size(); ++i )
	{
		acc += weights[ i ];
		// The final boundary is pinned to `total` so the pattern always fills
		// the raster exactly, whatever the weights sum to.
		const int edge = i + 1 == weights.size() ? total : static_cast< int >( std::lround( ( acc / sum ) * total ) );
		out.push_back( { prev, edge - prev } );
		prev = edge;
	}
	return out;
}

std::vector< Band > Rp219Layout( int width, int height )
{
	const Palette& C = Colours();

	const std::vector< ColSpec > rowA = {
		Flat( "40% grey", kA, C.grey40 ),      Flat( "75% white", kB, C.white75 ),
		Flat( "75% yellow", kB, C.yellow75 ),  Flat( "75% cyan", kB, C.cyan75 ),
		Flat( "75% green", kB, C.green75 ),    Flat( "75% magenta", kB, C.magenta75 ),
		Flat( "75% red", kB, C.red75 ),        Flat( "75% blue", kB, C.blue75 ),
		Flat( "40% grey", kA, C.grey40 ),
	};
	const std::vector< ColSpec > rowB = {
		Flat( "100% cyan", kA, C.cyan100 ),
		Flat( "+I", kB, C.plusI ),
		Flat( "75% white", 6 * kB, C.white75 ),
		Flat( "100% blue", kA, C.blue100 ),
	};
	const std::vector< ColSpec > rowC = {
		Flat( "100% yellow", kA, C.yellow100 ),
		Flat( "+Q", kB, C.plusQ ),
		{ "luma ramp", 6 * kB, Fill::Ramp, C.black },
		Flat( "100% red", kA, C.red100 ),
	};
	const std::vector< ColSpec > rowD = {
		Flat( "15% grey", kA, C.grey15 ),
		Flat( "black", ( 3 * kB ) / 2, C.black ),
		Flat( "100% white", 2 * kB, C.white100 ),
		Flat( "black", ( 5 * kB ) / 6, C.black ),
		Flat( "pluge -2%", kB / 3, C.plugeMinus2 ),
		Flat( "pluge 0%", kB / 3, C.black ),
		Flat( "pluge +2%", kB / 3, C.plugePlus2 ),
		Flat( "pluge 0%", kB / 3, C.black ),
		Flat( "pluge +4%", kB / 3, C.plugePlus4 ),
		Flat( "black", kB, C.black ),
		Flat( "15% grey", kA, C.grey15 ),
	};

	struct Row
	{
		char                         key;
		double                       h;
		const std::vector< ColSpec >* cols;
	};
	const Row rows[] = { { 'A', 7.0 / 12.0, &rowA }, { 'B', 1.0 / 12.0, &rowB }, { 'C', 1.0 / 12.0, &rowC }, { 'D', 3.0 / 12.0, &rowD } };

	std::vector< double > rowWeights;
	for( const Row& r : rows )
		rowWeights.push_back( r.h );
	const std::vector< Segment > rowGeom = LayOut( rowWeights, height );

	std::vector< Band > bands;
	for( size_t i = 0; i < 4; ++i )
	{
		const Row& row = rows[ i ];
		std::vector< double > colWeights;
		for( const ColSpec& c : *row.cols )
			colWeights.push_back( c.w );
		const std::vector< Segment > cols = LayOut( colWeights, width );
		for( size_t j = 0; j < row.cols->size(); ++j )
		{
			const ColSpec& c = ( *row.cols )[ j ];
			bands.push_back( { row.key, c.name, cols[ j ].start, rowGeom[ i ].start, cols[ j ].size, rowGeom[ i ].size,
							   c.fill, YcbcrToUnit( c.colour ) } );
		}
	}
	return bands;
}

std::vector< Band > BarsLayout( int width, int height, double amplitudePct )
{
	const Palette& C     = Colours();
	const double   scale = amplitudePct / 100.0;
	auto at = [ scale ]( const Ycbcr& c ) {
		return Ycbcr{ 16.0 + ( c.y - 16.0 ) * scale, 128.0 + ( c.cb - 128.0 ) * scale, 128.0 + ( c.cr - 128.0 ) * scale };
	};
	struct Spec
	{
		const char* name;
		Ycbcr       colour;
	};
	const Spec specs[] = {
		{ "white", at( C.white100 ) },   { "yellow", at( C.yellow100 ) }, { "cyan", at( C.cyan100 ) },
		{ "green", at( Bar( 0, 1, 0 ) ) }, { "magenta", at( Bar( 1, 0, 1 ) ) }, { "red", at( C.red100 ) },
		{ "blue", at( C.blue100 ) },     { "black", GreyPct( 0 ) },
	};
	const std::vector< Segment > cols = LayOut( std::vector< double >( 8, 1.0 ), width );
	std::vector< Band >          bands;
	for( size_t i = 0; i < 8; ++i )
		bands.push_back( { 'A', specs[ i ].name, cols[ i ].start, 0, cols[ i ].size, height, Fill::Flat,
						   YcbcrToUnit( specs[ i ].colour ) } );
	return bands;
}

} // namespace graticule::bars
