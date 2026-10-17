#pragma once

// OpenGL 3.3 core con las funciones cargadas por SDL_GL_GetProcAddress (como
// el juego: sin GLAD ni GLEW). Solo las que usa el editor.

#ifdef _WIN32
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
#ifndef WINGDIAPI
#define WINGDIAPI __declspec(dllimport)
#endif
#ifndef CALLBACK
#define CALLBACK __stdcall
#endif
#endif
#ifdef __APPLE__
// macOS no tiene <GL/gl.h>: gl3.h (core) trae las funciones, enums y PFNGL*PROC que usa este archivo.
#ifndef GL_SILENCE_DEPRECATION
#define GL_SILENCE_DEPRECATION
#endif
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#if defined(_WIN32)
// Windows: the SDK's <GL/gl.h> stops at OpenGL 1.1 and ships no Khronos
// <GL/glext.h>, so the modern entry points this file needs (buffers, VAOs,
// shaders, FBOs) come from SDL's copy of the Khronos header, which is on the
// include path on every platform. APIENTRY is defined above, so
// SDL_opengl_glext.h does not pull in <windows.h> by itself.
// Not games/pikmin1/include/GL/glext.h: that is a deliberately minimal stub for
// the game's GX translator and does not carry these types.
#include <SDL_opengl_glext.h>
#else
#include <GL/glext.h>
#endif
#endif

#define NM_GL_FUNCS(X)                                                                                                                     \
	X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                                                                   \
	X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                                                                             \
	X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                                                                   \
	X(PFNGLBUFFERDATAPROC, glBufferData)                                                                                                   \
	X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                                                                         \
	X(PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays)                                                                                   \
	X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                                                                         \
	X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                                                                         \
	X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                                                                                 \
	X(PFNGLCREATESHADERPROC, glCreateShader)                                                                                               \
	X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                                                               \
	X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                                                             \
	X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                                                                 \
	X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                                                                       \
	X(PFNGLDELETESHADERPROC, glDeleteShader)                                                                                               \
	X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                                                             \
	X(PFNGLATTACHSHADERPROC, glAttachShader)                                                                                               \
	X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                                                                 \
	X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                                                               \
	X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                                                                     \
	X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                                                                   \
	X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                                                                                   \
	X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                                                                   \
	X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                                                                                       \
	X(PFNGLUNIFORM4FPROC, glUniform4f)                                                                                                     \
	X(PFNGLUNIFORM3FPROC, glUniform3f)                                                                                                     \
	X(PFNGLUNIFORM1FPROC, glUniform1f)                                                                                                     \
	X(PFNGLUNIFORM1IPROC, glUniform1i)                                                                                                     \
	X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                                                                         \
	X(PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers)                                                                                   \
	X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                                                                         \
	X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                                                                               \
	X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                                                                                       \
	X(PFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers)                                                                                 \
	X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                                                                                       \
	X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                                                                                 \
	X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)                                                                         \
	X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                                                                           \
	X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                                                                                           \
	X(PFNGLACTIVETEXTUREPROC, glActiveTexture)

#define NM_GL_DECLARE(type, name) extern type nm_##name;
NM_GL_FUNCS(NM_GL_DECLARE)
#undef NM_GL_DECLARE

// Nombres de siempre en el código del editor.
#define glGenBuffers               nm_glGenBuffers
#define glDeleteBuffers            nm_glDeleteBuffers
#define glBindBuffer               nm_glBindBuffer
#define glBufferData               nm_glBufferData
#define glGenVertexArrays          nm_glGenVertexArrays
#define glDeleteVertexArrays       nm_glDeleteVertexArrays
#define glBindVertexArray          nm_glBindVertexArray
#define glEnableVertexAttribArray  nm_glEnableVertexAttribArray
#define glVertexAttribPointer      nm_glVertexAttribPointer
#define glCreateShader             nm_glCreateShader
#define glShaderSource             nm_glShaderSource
#define glCompileShader            nm_glCompileShader
#define glGetShaderiv              nm_glGetShaderiv
#define glGetShaderInfoLog         nm_glGetShaderInfoLog
#define glDeleteShader             nm_glDeleteShader
#define glCreateProgram            nm_glCreateProgram
#define glAttachShader             nm_glAttachShader
#define glLinkProgram              nm_glLinkProgram
#define glGetProgramiv             nm_glGetProgramiv
#define glGetProgramInfoLog        nm_glGetProgramInfoLog
#define glUseProgram               nm_glUseProgram
#define glBindAttribLocation       nm_glBindAttribLocation
#define glGetUniformLocation       nm_glGetUniformLocation
#define glUniformMatrix4fv         nm_glUniformMatrix4fv
#define glUniform4f                nm_glUniform4f
#define glUniform3f                nm_glUniform3f
#define glUniform1f                nm_glUniform1f
#define glUniform1i                nm_glUniform1i
#define glGenFramebuffers          nm_glGenFramebuffers
#define glDeleteFramebuffers       nm_glDeleteFramebuffers
#define glBindFramebuffer          nm_glBindFramebuffer
#define glFramebufferTexture2D     nm_glFramebufferTexture2D
#define glGenRenderbuffers         nm_glGenRenderbuffers
#define glDeleteRenderbuffers      nm_glDeleteRenderbuffers
#define glBindRenderbuffer         nm_glBindRenderbuffer
#define glRenderbufferStorage      nm_glRenderbufferStorage
#define glFramebufferRenderbuffer  nm_glFramebufferRenderbuffer
#define glCheckFramebufferStatus   nm_glCheckFramebufferStatus
#define glGenerateMipmap           nm_glGenerateMipmap
#define glActiveTexture            nm_glActiveTexture

/// Carga las funciones (con el contexto ya creado). false si falta alguna.
bool nmLoadGl();
