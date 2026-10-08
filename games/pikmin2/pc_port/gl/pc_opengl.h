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
// Solo lo usan sondas de depuración de pc_gfx (volcar texturas): GLES 3.0 no
// tiene glGetTexImage ni las constantes de tamaño de nivel.
#  ifndef GL_TEXTURE_WIDTH
#    define GL_TEXTURE_WIDTH 0x1000
#  endif
#  ifndef GL_TEXTURE_HEIGHT
#    define GL_TEXTURE_HEIGHT 0x1001
#  endif
static inline void glGetTexImage(GLenum, GLint, GLenum, GLenum, void*) { }
#  if !defined(GL_ES_VERSION_3_1)
static inline void glGetTexLevelParameteriv(GLenum, GLint, GLenum, GLint* p) { if (p) *p = 0; }
#  endif
#  ifndef GL_TIME_ELAPSED
#    define GL_TIME_ELAPSED GL_TIME_ELAPSED_EXT
#  endif
#  ifndef APIENTRYP
#    define APIENTRYP GL_APIENTRYP
#  endif
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
