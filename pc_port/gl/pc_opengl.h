#ifndef PIKMIN_PC_OPENGL_H
#define PIKMIN_PC_OPENGL_H

// Punto único de entrada a las cabeceras de OpenGL.
//
// En Windows, <GL/gl.h> y <GL/glext.h> necesitan las convenciones de llamada
// APIENTRY, WINGDIAPI y CALLBACK, que normalmente obtienen incluyendo
// <windows.h>. Aquí no podemos permitirlo: windows.h define macros con nombres
// muy comunes, y una de ellas es ERROR, que este proyecto usa como macro de
// registro propia (include/DebugLog.h) en 371 archivos. Incluir windows.h en la
// ruta gráfica la aplastaría.
//
// Tanto glext.h como gl.h condicionan su #include <windows.h> a que APIENTRY no
// esté ya definida, así que definiéndola nosotros primero se saltan windows.h
// por completo y obtenemos solo OpenGL. Las definiciones son las mismas que
// windows.h habría proporcionado.
//
// El juego carga las funciones modernas de GL con SDL_GL_GetProcAddress, así
// que no hace falta ni GLAD ni GLEW en ningún sistema.

#ifdef _WIN32
#  ifndef APIENTRY
#    define APIENTRY __stdcall
#  endif
#  ifndef WINGDIAPI
#    define WINGDIAPI __declspec(dllimport)
#  endif
#  ifndef CALLBACK
#    define CALLBACK __stdcall
#  endif
#endif

// Fuera de la build de Android PIKI_USE_GLES no está definido; darle valor
// evita que las expresiones `PIKI_USE_GLES == 0` fallen.
#ifndef PIKI_USE_GLES
#  define PIKI_USE_GLES 0
#endif
#if PIKI_USE_GLES
#  include <GLES3/gl3.h>
#  include <GLES3/gl3ext.h>
// Timer queries: en GLES viven en EXT_disjoint_timer_query (gl2ext.h) con
// sufijo EXT; el mismo nombre que en escritorio deja el resto del código
// igual. Si el driver no las expone, los punteros quedan nulos y se ignoran.
#  include <GLES2/gl2ext.h>
#  ifndef GL_TIME_ELAPSED
#    define GL_TIME_ELAPSED GL_TIME_ELAPSED_EXT
#  endif
#  ifndef APIENTRYP
#    define APIENTRYP GL_APIENTRYP
#  endif
#elif defined(__APPLE__)
// macOS keeps the desktop GL headers inside OpenGL.framework, as
// <OpenGL/gl.h>, and ships no <GL/gl.h> at all.
//
// Spelling it <GL/gl.h> here does not merely fail to find the system
// header -- it silently finds the wrong one. The include path carries
// -I<repo>/pc_port, and macOS's filesystem is case-insensitive, so
// <GL/gl.h> matches this directory's own pc_port/gl/gl.h, a one-line
// wrapper that includes *this* header right back. Its guard is already
// defined by then, so the whole thing expands to nothing, no error is
// raised, and every GL type stays undefined until <GL/glext.h> (which
// case-folds onto the bundled include/gl/glext.h) reports GLint,
// GLenum and GLsizei as unknown type names. On Linux and Windows the
// case-sensitive lookup never collides, which is why this only bites
// here.
//
// Apple's <OpenGL/glext.h> already covers everything the bundled
// include/gl/glext.h defines (GL_EXT_compiled_vertex_array and
// GL_ARB_multitexture, both from the late 90s), so that file is simply
// not used on this platform. Anything newer than Apple's headers know
// about is declared where it is used -- see the enum block near the top
// of pc_gfx.cpp -- because the port resolves every modern entry point
// through SDL_GL_GetProcAddress regardless of platform.
#  ifndef GL_SILENCE_DEPRECATION
// OpenGL has been formally deprecated on macOS since 10.14. It still
// works (and is the only portable option here -- the alternative is a
// Metal backend), but without this every GL call in the port emits a
// deprecation warning and buries real diagnostics.
#    define GL_SILENCE_DEPRECATION 1
#  endif
#  include <OpenGL/gl.h>
#  include <OpenGL/glext.h>
#  ifndef APIENTRY
#    define APIENTRY
#  endif
#  ifndef APIENTRYP
#    define APIENTRYP APIENTRY *
#  endif

// Apple does not use the Khronos PFNGL<NAME>PROC convention anywhere: its
// headers declare function-pointer types in their own style instead, as
// glGenBuffersProcPtr and friends. There is not a single "PFN" in either
// <OpenGL/gl.h> or <OpenGL/glext.h>, so every one of the typedefs this port
// uses is absent here and has to be declared. Nothing below can collide with
// an Apple declaration, precisely because Apple declares none of them.
//
// These are generated from Apple's own ...ProcPtr typedefs, so the parameter
// lists are the ones this SDK actually ships rather than a transcription of
// the spec. The three exceptions are noted where they appear: GL 3.0 entry
// points Apple exposes only with an APPLE suffix, or (glMapBufferRange) not
// at all. The functions themselves are resolved at runtime through
// SDL_GL_GetProcAddress, so a type being missing from the header says nothing
// about whether the driver provides the entry point -- on a Core 3.3 context
// it does.
typedef void (APIENTRYP PFNGLACTIVETEXTUREARBPROC)(GLenum texture);
typedef void (APIENTRYP PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void (APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRYP PFNGLBINDATTRIBLOCATIONPROC)(GLuint program, GLuint index, const GLchar *name);
typedef void (APIENTRYP PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void (APIENTRYP PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);
typedef void (APIENTRYP PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (APIENTRYP PFNGLBLENDEQUATIONPROC)(GLenum mode);
typedef void (APIENTRYP PFNGLBLITFRAMEBUFFERPROC)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
typedef void (APIENTRYP PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
typedef void (APIENTRYP PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data);
typedef GLenum (APIENTRYP PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);
typedef void (APIENTRYP PFNGLCLIENTACTIVETEXTUREARBPROC)(GLenum texture);
typedef GLenum (APIENTRYP PFNGLCLIENTWAITSYNCPROC)(GLsync sync, GLbitfield flags, GLuint64 timeout);
typedef void (APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (APIENTRYP PFNGLCOMPRESSEDTEXIMAGE2DPROC)(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const GLvoid *data);
typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLDELETESHADERPROC)(GLuint shader);
typedef void (APIENTRYP PFNGLDELETESYNCPROC)(GLsync sync);
typedef void (APIENTRYP PFNGLDRAWBUFFERSPROC)(GLsizei n, const GLenum *bufs);
typedef void (APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef GLsync (APIENTRYP PFNGLFENCESYNCPROC)(GLenum condition, GLbitfield flags);
typedef void (APIENTRYP PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void (APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void (APIENTRYP PFNGLGENBUFFERSPROC)(GLsizei n, GLuint *buffers);
typedef void (APIENTRYP PFNGLGENERATEMIPMAPPROC)(GLenum target);
typedef void (APIENTRYP PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint *framebuffers);
typedef void (APIENTRYP PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint *renderbuffers);
typedef void (APIENTRYP PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
typedef void (APIENTRYP PFNGLGETACTIVEUNIFORMPROC)(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
typedef GLint (APIENTRYP PFNGLGETATTRIBLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void (APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint *params);
typedef void (APIENTRYP PFNGLGETUNIFORMIVPROC)(GLuint program, GLint location, GLint *params);
typedef GLint (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar *name);
typedef void (APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLLOCKARRAYSEXTPROC)(GLint first, GLsizei count);
typedef GLvoid *(APIENTRYP PFNGLMAPBUFFERRANGEPROC)(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);

// Enum constants renamed when the extension was promoted into core GL, where
// Apple's headers only carry the older spelling. Same tokens, same values --
// GL 3.0 simply renamed GL_COMPARE_R_TO_TEXTURE (which Apple has, at 0x884E)
// to GL_COMPARE_REF_TO_TEXTURE, and the port uses the core name.
#  ifndef GL_COMPARE_REF_TO_TEXTURE
#    ifdef GL_COMPARE_R_TO_TEXTURE
#      define GL_COMPARE_REF_TO_TEXTURE GL_COMPARE_R_TO_TEXTURE
#    else
#      define GL_COMPARE_REF_TO_TEXTURE 0x884E
#    endif
#  endif
typedef void (APIENTRYP PFNGLMULTITEXCOORD2FARBPROC)(GLenum target, GLfloat s, GLfloat t);
typedef void (APIENTRYP PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void (APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar* const *string, const GLint *length);
typedef void (APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void (APIENTRYP PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void (APIENTRYP PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void (APIENTRYP PFNGLUNIFORM2IPROC)(GLint location, GLint v0, GLint v1);
typedef void (APIENTRYP PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRYP PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRYP PFNGLUNIFORM4FVPROC)(GLint location, GLsizei count, const GLfloat *value);
typedef void (APIENTRYP PFNGLUNIFORM4IPROC)(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
typedef void (APIENTRYP PFNGLUNIFORMMATRIX3FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRYP PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRYP PFNGLUNLOCKARRAYSEXTPROC)(void);
typedef GLboolean (APIENTRYP PFNGLUNMAPBUFFERPROC)(GLenum target);
typedef void (APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const GLvoid *pointer);
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#endif

// Red de seguridad: si alguna cabecera del sistema acabara arrastrando
// windows.h de todos modos, estas macros suyas chocan con identificadores del
// juego. Retirarlas aquí hace que un conflicto se manifieste como un error de
// compilación claro en vez de como comportamiento extraño.
#ifdef _WIN32
#  undef near
#  undef far
#  undef small
#endif

#endif // PIKMIN_PC_OPENGL_H
