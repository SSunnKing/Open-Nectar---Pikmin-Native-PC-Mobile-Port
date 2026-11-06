/* GL probe for the scenario CI jobs (#60, #63).
 *
 * Creates a hidden SDL window with the same context profile the game requests
 * (pc_window.cpp: 4.1 core on macOS, 3.3 compatibility elsewhere), prints
 * GL_VERSION, GL_RENDERER and GLSL version, compiles and links a
 * "#version 330 core" program, reads back a cleared framebuffer and swaps 60
 * times. Exit 0 only if every step works at the requested version. Entry points
 * come from SDL_GL_GetProcAddress, so the probe links nothing but SDL2.
 *
 * Build: cc gl_probe.c $(sdl2-config --cflags --libs) -o gl_probe
 */
/* The game owns main() (SDL_MAIN_HANDLED); SDL_main.h would redefine it on Windows. */
#define SDL_MAIN_HANDLED
#include <SDL.h>
#undef main /* MinGW sdl2-config passes -Dmain=SDL_main */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef int GLsizei;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef float GLfloat;

#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401

static const GLubyte *(*p_glGetString)(GLenum);
static void (*p_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*p_glClear)(GLuint);
static void (*p_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
static GLuint (*p_glCreateShader)(GLenum);
static void (*p_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
static void (*p_glCompileShader)(GLuint);
static void (*p_glGetShaderiv)(GLuint, GLenum, GLint *);
static void (*p_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
static GLuint (*p_glCreateProgram)(void);
static void (*p_glAttachShader)(GLuint, GLuint);
static void (*p_glLinkProgram)(GLuint);
static void (*p_glGetProgramiv)(GLuint, GLenum, GLint *);

static int fail(const char *what) {
    fprintf(stderr, "GL_PROBE_FAIL: %s (SDL: %s)\n", what, SDL_GetError());
    return 1;
}

static int load(void) {
#define LOAD(n) \
    if (!(*(void **)&p_##n = SDL_GL_GetProcAddress(#n))) { fprintf(stderr, "missing entry point %s\n", #n); return 0; }
    LOAD(glGetString) LOAD(glClearColor) LOAD(glClear) LOAD(glReadPixels) LOAD(glCreateShader)
    LOAD(glShaderSource) LOAD(glCompileShader) LOAD(glGetShaderiv) LOAD(glGetShaderInfoLog)
    LOAD(glCreateProgram) LOAD(glAttachShader) LOAD(glLinkProgram) LOAD(glGetProgramiv)
#undef LOAD
    return 1;
}

static int compile(GLenum type, const char *src) {
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 1, &src, NULL);
    p_glCompileShader(s);
    GLint ok = 0;
    p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        p_glGetShaderInfoLog(s, sizeof log - 1, NULL, log);
        fprintf(stderr, "shader compile failed: %s\n", log);
        return 0;
    }
    return (int)s;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0); /* the CI step can kill a hung probe; keep what it printed */
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 0);
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return fail("SDL_Init");

    int wantMajor = 3, wantMinor = 3;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#ifdef __APPLE__
    wantMajor = 4; wantMinor = 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window *win = SDL_CreateWindow("gl_probe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 320, 240,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!win) return fail("SDL_CreateWindow");
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) return fail("SDL_GL_CreateContext");
    if (!load()) return 1;

    const char *ver = (const char *)p_glGetString(GL_VERSION);
    const char *ren = (const char *)p_glGetString(GL_RENDERER);
    const char *glsl = (const char *)p_glGetString(GL_SHADING_LANGUAGE_VERSION);
    printf("GL_VERSION: %s\nGL_RENDERER: %s\nGLSL: %s\nSDL_VIDEODRIVER: %s\n", ver ? ver : "(null)",
           ren ? ren : "(null)", glsl ? glsl : "(null)", SDL_GetCurrentVideoDriver());
    if (!ver) return fail("no GL_VERSION");

    int major = 0, minor = 0;
    if (sscanf(ver, "%d.%d", &major, &minor) != 2) return fail("unparsable GL_VERSION");
    if (major < wantMajor || (major == wantMajor && minor < wantMinor)) {
        fprintf(stderr, "GL_PROBE_FAIL: got %d.%d, need %d.%d\n", major, minor, wantMajor, wantMinor);
        return 1;
    }
#ifdef _WIN32
    if (ren && strstr(ren, "GDI Generic")) return fail("GDI Generic renderer");
#endif

    int vs = compile(GL_VERTEX_SHADER, "#version 330 core\nvoid main(){gl_Position=vec4(0.0);}\n");
    int fs = compile(GL_FRAGMENT_SHADER, "#version 330 core\nout vec4 c;\nvoid main(){c=vec4(1.0,0.0,0.0,1.0);}\n");
    if (!vs || !fs) return fail("#version 330 core shader");
    GLuint prog = p_glCreateProgram();
    p_glAttachShader(prog, (GLuint)vs);
    p_glAttachShader(prog, (GLuint)fs);
    p_glLinkProgram(prog);
    GLint linked = 0;
    p_glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (!linked) return fail("program link");

    unsigned char px[4] = {0, 0, 0, 0};
    p_glClearColor(0.f, 1.f, 0.f, 1.f);
    p_glClear(GL_COLOR_BUFFER_BIT);
    p_glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    if (px[0] != 0 || px[1] != 255 || px[2] != 0) {
        fprintf(stderr, "GL_PROBE_FAIL: readback %u,%u,%u,%u\n", px[0], px[1], px[2], px[3]);
        return 1;
    }

    SDL_GL_SetSwapInterval(0);
    Uint32 t0 = SDL_GetTicks();
    for (int i = 0; i < 60; i++) {
        p_glClear(GL_COLOR_BUFFER_BIT);
        SDL_GL_SwapWindow(win);
    }
    printf("60 swaps: %u ms\nGL_PROBE_OK\n", (unsigned)(SDL_GetTicks() - t0));
    fflush(stdout);
    /* No teardown: Mesa llvmpipe's context destruction hangs on Windows runners, and
       the probe has already said everything it needs to. */
    _Exit(0);
}
