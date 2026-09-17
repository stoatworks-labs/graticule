#include "Render.h"

#include <algorithm>

#include "Font.h"
#include "Shaders.h"

namespace graticule
{
namespace
{
void SetI( GLuint program, const char* name, int v )
{
	glUniform1i( glGetUniformLocation( program, name ), v );
}
void SetI2( GLuint program, const char* name, int a, int b )
{
	glUniform2i( glGetUniformLocation( program, name ), a, b );
}
void SetI4( GLuint program, const char* name, const int* v )
{
	glUniform4i( glGetUniformLocation( program, name ), v[ 0 ], v[ 1 ], v[ 2 ], v[ 3 ] );
}
void SetF3( GLuint program, const char* name, const float* v )
{
	glUniform3f( glGetUniformLocation( program, name ), v[ 0 ], v[ 1 ], v[ 2 ] );
}
} // namespace

bool Renderer::InitGL()
{
	if( mReady )
		return true;

	mNote.clear();

	if( !mProgram.Compile( kVertexShader, kFragmentShader ) )
	{
		mNote = "the pattern shader would not compile";
		return false;
	}

	glGenVertexArrays( 1, &mVao );

	// The font, as a single-channel texture indexed by ASCII code.
	//
	// The queue is drained first. A plugin that reads a GL error the HOST left
	// behind as its own failure abandons a perfectly good upload -- gridiron#1.
	while( glGetError() != GL_NO_ERROR )
	{
	}
	const std::vector< uint8_t > pixels = font::Texture();
	glGenTextures( 1, &mFont );
	glBindTexture( GL_TEXTURE_2D, mFont );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_R8, font::kTextureWidth, font::kTextureHeight, 0, GL_RED, GL_UNSIGNED_BYTE,
				  pixels.data() );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );

	const GLenum err = glGetError();
	if( err != GL_NO_ERROR )
	{
		mNote = "the font texture would not upload (GL error 0x" + std::to_string( static_cast< int >( err ) ) + ")";
		DeInitGL();
		return false;
	}

	mReady = true;
	return true;
}

void Renderer::DeInitGL()
{
	if( mFont != 0 )
	{
		glDeleteTextures( 1, &mFont );
		mFont = 0;
	}
	if( mVao != 0 )
	{
		glDeleteVertexArrays( 1, &mVao );
		mVao = 0;
	}
	mProgram.FreeGLResources();
	mReady = false;
}

void Renderer::Draw( const Frame& f, GLuint hostFBO )
{
	if( !mReady || f.width <= 0 || f.height <= 0 )
		return;

	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( 0, 0, f.width, f.height );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_BLEND );

	const GLuint prog = mProgram.GetGLID();
	glUseProgram( prog );

	SetI2( prog, "uSize", f.width, f.height );
	SetI( prog, "uPattern", f.pattern );
	SetI( prog, "uLegal", f.legal ? 1 : 0 );

	SetI( prog, "uGridMode", f.gridMode );
	SetI( prog, "uPitch", f.pitch );
	SetI2( prog, "uDiv", f.divX, f.divY );
	SetI( prog, "uLineW", f.lineW );
	SetI( prog, "uMajorEvery", f.majorEvery );
	SetI( prog, "uMajorW", f.majorW );
	SetI( prog, "uDiagonals", f.diagonals ? 1 : 0 );
	SetI( prog, "uCentre", f.centre ? 1 : 0 );
	SetI( prog, "uOriginCentre", f.originCentre ? 1 : 0 );
	SetI( prog, "uCellLabels", f.cellLabels ? 1 : 0 );
	SetF3( prog, "uLineCol", f.lineCol );
	SetF3( prog, "uMajorCol", f.majorCol );
	SetF3( prog, "uBgCol", f.bgCol );

	SetI2( prog, "uTile", f.tileW, f.tileH );
	SetI2( prog, "uModule", f.moduleW, f.moduleH );
	SetI2( prog, "uOrigin", f.originX, f.originY );
	SetI( prog, "uChecker", f.checker ? 1 : 0 );
	SetI( prog, "uTileLabels", f.tileLabels ? 1 : 0 );
	SetI( prog, "uBorderW", f.borderW );
	SetF3( prog, "uTileA", f.tileA );
	SetF3( prog, "uTileB", f.tileB );
	SetF3( prog, "uBorderCol", f.borderCol );

	SetF3( prog, "uSolid", f.solid );

	SetI( prog, "uSteps", f.steps );
	SetI( prog, "uVertical", f.vertical ? 1 : 0 );
	SetI( prog, "uPerChannel", f.perChannel ? 1 : 0 );

	SetI( prog, "uCell", f.cell );
	SetI( prog, "uBursts", f.bursts ? 1 : 0 );

	SetI( prog, "uEdgeBorder", f.edgeBorder ? 1 : 0 );
	SetI( prog, "uCorners", f.corners ? 1 : 0 );
	SetI( prog, "uCornerSize", f.cornerSize );
	SetI( prog, "uTarget", f.target ? 1 : 0 );
	SetI( prog, "uSafeA", f.safeA );
	SetI( prog, "uSafeB", f.safeB );
	SetI( prog, "uPatches", f.patches ? 1 : 0 );
	SetI( prog, "uAlignDiag", f.alignDiag ? 1 : 0 );

	// The bars, packed for the arrays.
	{
		const int n = std::min( static_cast< int >( f.bands.size() ), bars::kMaxBands );
		std::vector< GLint >   rects( static_cast< size_t >( bars::kMaxBands ) * 4, 0 );
		std::vector< GLfloat > cols( static_cast< size_t >( bars::kMaxBands ) * 4, 0.0f );
		for( int i = 0; i < n; ++i )
		{
			const bars::Band& b = f.bands[ static_cast< size_t >( i ) ];
			rects[ i * 4 + 0 ]  = b.x;
			rects[ i * 4 + 1 ]  = b.y;
			rects[ i * 4 + 2 ]  = b.w;
			rects[ i * 4 + 3 ]  = b.h;
			cols[ i * 4 + 0 ]   = static_cast< float >( b.colour.r );
			cols[ i * 4 + 1 ]   = static_cast< float >( b.colour.g );
			cols[ i * 4 + 2 ]   = static_cast< float >( b.colour.b );
			cols[ i * 4 + 3 ]   = b.fill == bars::Fill::Ramp ? 1.0f : 0.0f;
		}
		SetI( prog, "uBandCount", n );
		glUniform4iv( glGetUniformLocation( prog, "uBandRect" ), bars::kMaxBands, rects.data() );
		glUniform4fv( glGetUniformLocation( prog, "uBandCol" ), bars::kMaxBands, cols.data() );
	}

	SetI4( prog, "uMarker", f.marker );

	// The burn-in.
	{
		std::vector< GLint > text( 96, 32 );
		for( size_t i = 0; i < f.text.size() && i < 96; ++i )
			text[ i ] = f.text[ i ];
		glUniform1iv( glGetUniformLocation( prog, "uText" ), 96, text.data() );

		std::vector< GLint > lines( 12, 0 );
		const int            count = std::min( static_cast< int >( f.lines.size() ), 3 );
		for( int i = 0; i < count; ++i )
		{
			lines[ i * 4 + 0 ] = f.lines[ static_cast< size_t >( i ) ].offset;
			lines[ i * 4 + 1 ] = f.lines[ static_cast< size_t >( i ) ].length;
			lines[ i * 4 + 2 ] = f.lines[ static_cast< size_t >( i ) ].x;
			lines[ i * 4 + 3 ] = f.lines[ static_cast< size_t >( i ) ].y;
		}
		glUniform4iv( glGetUniformLocation( prog, "uLine" ), 3, lines.data() );
		SetI( prog, "uLineCount", count );
		SetI( prog, "uTextScale", std::max( 1, f.textScale ) );
		SetI4( prog, "uPlate", f.plate );
	}

	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, mFont );
	SetI( prog, "uFont", 0 );

	glBindVertexArray( mVao );
	glDrawArrays( GL_TRIANGLES, 0, 3 );

	// ---- hand the context back -------------------------------------------
	glBindVertexArray( 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glUseProgram( 0 );
}

} // namespace graticule
