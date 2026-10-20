/*
    The one GL entry point emscripten's WebGL2 library cannot take from the
    plugin as it is: glShaderSource. Every other GL call the plugin and the
    FFGL SDK make goes straight to emscripten's implementation, on the page's
    own WebGL2 context. zoetrope's shim, cut down: Graticule touches no
    desktop-only state (it disables GL_DEPTH_TEST and GL_BLEND and nothing
    else), so glEnable, glDisable and glIsEnabled pass through.

    glShaderSource
        The plugin hands GL desktop GLSL 4.10 (`#version 410 core`); WebGL2
        compiles GLSL ES 3.00. The page's `graticuleShaderSource` first
        REQUIRES the text to be byte for byte the page's own assembly of the
        plugin's shaders for that stage (demo/shaders.js, which
        demo/tools/check_shaders.py holds to source/Shaders.cpp and to the
        Compile() call in Render.cpp), then applies the demo kit's `port()`:
        the version line and the ES precision defaults, nothing else. A shader
        the plugin assembled differently from the page's copy is refused (an
        empty source, so the plugin's InitGL fails and logs it), never
        compiled quietly.

    Nothing else needed one. The font is a single-level GL_R8 texture
    uploaded with GL_RED / GL_UNSIGNED_BYTE, which ES 3.0 accepts as it is;
    the VAO is empty and the triangle comes from gl_VertexID, which ES 3.00
    has; and the draw goes into the framebuffer the page has bound.
*/
#include <GLES3/gl3.h>
#include <emscripten/emscripten.h>

#include <cstring>
#include <string>

namespace
{
// clang-format off
EM_JS( void, gtShaderSource, ( GLuint shader, const char* source ), {
	const object = GL.shaders[ shader ];
	const stage  = GLctx.getShaderParameter( object, 0x8B4F /* GL_SHADER_TYPE */ ) === 0x8B31 ? 'vertex' : 'fragment';
	GLctx.shaderSource( object, Module[ 'graticuleShaderSource' ]( stage, UTF8ToString( source ) ) );
} );
// clang-format on
} // namespace

extern "C"
{
void glShaderSource( GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length )
{
	std::string source;
	for( GLsizei i = 0; i < count; ++i )
	{
		if( string[ i ] == nullptr )
			continue;
		const size_t n = ( length != nullptr && length[ i ] >= 0 ) ? static_cast< size_t >( length[ i ] ) : std::strlen( string[ i ] );
		source.append( string[ i ], n );
	}
	gtShaderSource( shader, source.c_str() );
}
} // extern "C"
