#include "pc_gfx.h"
#include "pc_gx_vertex_decode.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <cmath>
#if defined(_WIN32) || defined(__ANDROID__)
// glibc's backtrace() has no Windows equivalent worth pulling a dependency in
// for, and bionic has no execinfo.h; the wild-vertex diagnostic below degrades
// to its raw dump instead.
#else
#include <execinfo.h>
#endif
extern "C" int pc_movie_active(void);
#include <chrono>
#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <mutex>
#include <new>

bool pc_host_alloc_active();
void pc_host_alloc_set(bool active);

// Routes the global operator new to malloc for the enclosing scope (see
// JKRHeap.cpp); for renderer state built by code that uses the default STL
// allocator but must survive the current section heap.
struct PcGfxHostAllocScope {
    bool prev = pc_host_alloc_active();
    PcGfxHostAllocScope() { pc_host_alloc_set(true); }
    ~PcGfxHostAllocScope() { pc_host_alloc_set(prev); }
};

// Renderer caches outlive the game's section heaps. The global operator new
// intentionally follows JKRHeap::sCurrentHeap, so using the default STL
// allocator here leaves map nodes dangling when BootSection destroys its
// heap. Keep all persistent renderer hash storage in the host heap instead.
template <typename T> struct PcGfxHostAllocator {
    using value_type = T;
    PcGfxHostAllocator() noexcept = default;
    template <typename U> PcGfxHostAllocator(const PcGfxHostAllocator<U>&) noexcept {}
    T* allocate(std::size_t count) {
        if (count > static_cast<std::size_t>(-1) / sizeof(T)) throw std::bad_alloc();
        void* memory = std::malloc(count * sizeof(T));
        if (!memory) throw std::bad_alloc();
        return static_cast<T*>(memory);
    }
    void deallocate(T* memory, std::size_t) noexcept { std::free(memory); }
    template <typename U> bool operator==(const PcGfxHostAllocator<U>&) const noexcept { return true; }
    template <typename U> bool operator!=(const PcGfxHostAllocator<U>&) const noexcept { return false; }
};

template <typename Key, typename Value>
using PcGfxHostMap = std::unordered_map<Key, Value, std::hash<Key>, std::equal_to<Key>,
                                        PcGfxHostAllocator<std::pair<const Key, Value>>>;
template <typename Key>
using PcGfxHostSet = std::unordered_set<Key, std::hash<Key>, std::equal_to<Key>, PcGfxHostAllocator<Key>>;
template <typename Value>
using PcGfxHostVector = std::vector<Value, PcGfxHostAllocator<Value>>;

// xxHash en un solo header con la implementación inline: no añade nada que
// compilar. Lo usa el volcado de nombres de textura (--dump-texture-names,
// PLAN_TEXTURAS_HD fase 0), que reproduce el esquema tex1_* de Dolphin.
#define XXH_INLINE_ALL
#include "xxhash.h"

#include "../timing/pc_render_packet.h"
#include "pc_tev_shader.h"
#include "pc_gx_lighting_glsl.h"
#include "pc_postprocess.h"
#include "pc_texpack.h"
#include "../timing/pc_render_phase.h"
#include "../timing/pc_tick_profiler.h"

#include "pc_opengl.h"
#ifdef __ANDROID__
#include "../android/pc_android.h"
#endif

// ── GL Function Pointers (Loaded via SDL_GL_GetProcAddress) ──
typedef void (APIENTRYP PFNGLGENBUFFERSPROC) (GLsizei n, GLuint *buffers);
typedef void (APIENTRYP PFNGLBINDBUFFERPROC) (GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNGLBUFFERDATAPROC) (GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void (APIENTRYP PFNGLBUFFERSUBDATAPROC) (GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
typedef void* (APIENTRYP PFNGLMAPBUFFERRANGEPROC) (GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
typedef GLboolean (APIENTRYP PFNGLUNMAPBUFFERPROC) (GLenum target);
typedef GLsync (APIENTRYP PFNGLFENCESYNCPROC) (GLenum condition, GLbitfield flags);
typedef GLenum (APIENTRYP PFNGLCLIENTWAITSYNCPROC) (GLsync sync, GLbitfield flags, GLuint64 timeout);
typedef void (APIENTRYP PFNGLDELETESYNCPROC) (GLsync sync);
typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC) (GLenum type);
typedef void (APIENTRYP PFNGLSHADERSOURCEPROC) (GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
typedef void (APIENTRYP PFNGLCOMPILESHADERPROC) (GLuint shader);
typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC) (void);
typedef void (APIENTRYP PFNGLATTACHSHADERPROC) (GLuint program, GLuint shader);
typedef void (APIENTRYP PFNGLLINKPROGRAMPROC) (GLuint program);
typedef void (APIENTRYP PFNGLUSEPROGRAMPROC) (GLuint program);
typedef GLint (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC) (GLuint program, const GLchar *name);
typedef GLint (APIENTRYP PFNGLGETATTRIBLOCATIONPROC) (GLuint program, const GLchar *name);
typedef void (APIENTRYP PFNGLUNIFORMMATRIX4FVPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
typedef void (APIENTRYP PFNGLUNIFORM1IPROC) (GLint location, GLint v0);
typedef void (APIENTRYP PFNGLUNIFORM1FPROC) (GLint location, GLfloat v0);
typedef void (APIENTRYP PFNGLUNIFORM4FPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void (APIENTRYP PFNGLUNIFORM3FPROC) (GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void (APIENTRYP PFNGLUNIFORM2IPROC) (GLint location, GLint v0, GLint v1);
typedef void (APIENTRYP PFNGLUNIFORM4IPROC) (GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
typedef void (APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC) (GLuint index);
typedef void (APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC) (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef void (APIENTRYP PCGLGENQUERIESPROC) (GLsizei n, GLuint* ids);
typedef void (APIENTRYP PCGLBEGINQUERYPROC) (GLenum target, GLuint id);
typedef void (APIENTRYP PCGLENDQUERYPROC) (GLenum target);
typedef void (APIENTRYP PCGLGETQUERYOBJECTIVPROC) (GLuint id, GLenum pname, GLint* params);
typedef void (APIENTRYP PCGLGETQUERYOBJECTUI64VPROC) (GLuint id, GLenum pname, GLuint64* params);

// Windows' opengl32 exports only OpenGL 1.1, so anything newer has to come
// through SDL_GL_GetProcAddress like the rest. On Linux these two happen to be
// declared by the system header, which is why they were called directly.
static PFNGLACTIVETEXTUREPROC glActiveTexture_ptr = nullptr;
static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays_ptr = nullptr;
static PFNGLGENERATEMIPMAPPROC glGenerateMipmap_ptr = nullptr;
typedef void (APIENTRYP PCGLINVALIDATEFRAMEBUFFERPROC) (GLenum target, GLsizei numAttachments, const GLenum* attachments);
static PCGLINVALIDATEFRAMEBUFFERPROC glInvalidateFramebuffer_ptr = nullptr;

// Not in the port's glext.h: anisotropic filtering is an extension everywhere
// except in core GL 4.6, and the port targets 3.3.
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#endif
#ifndef GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

static int sAnisotropyRequested = 0;
static float sAnisotropyMax = 1.0f;
static bool sAnisotropySupported = false;
static bool sFilteringReported = false;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray_ptr = nullptr;
static PFNGLBLENDEQUATIONPROC glBlendEquation_ptr = nullptr;
static PFNGLGENBUFFERSPROC glGenBuffers_ptr = nullptr;
static PFNGLDRAWBUFFERSPROC glDrawBuffers_ptr = nullptr;
static PFNGLBINDBUFFERPROC glBindBuffer_ptr = nullptr;
static PFNGLBUFFERDATAPROC glBufferData_ptr = nullptr;
static PFNGLBUFFERSUBDATAPROC glBufferSubData_ptr = nullptr;
static PFNGLMAPBUFFERRANGEPROC glMapBufferRange_ptr = nullptr;
static PFNGLUNMAPBUFFERPROC glUnmapBuffer_ptr = nullptr;
static PFNGLFENCESYNCPROC glFenceSync_ptr = nullptr;
static PFNGLCLIENTWAITSYNCPROC glClientWaitSync_ptr = nullptr;
static PFNGLDELETESYNCPROC glDeleteSync_ptr = nullptr;
static PFNGLCREATESHADERPROC glCreateShader_ptr = nullptr;
static PFNGLSHADERSOURCEPROC glShaderSource_ptr = nullptr;
static PFNGLCOMPILESHADERPROC glCompileShader_ptr = nullptr;
static PFNGLCREATEPROGRAMPROC glCreateProgram_ptr = nullptr;
static PFNGLATTACHSHADERPROC glAttachShader_ptr = nullptr;
static PFNGLLINKPROGRAMPROC glLinkProgram_ptr = nullptr;
static PFNGLUSEPROGRAMPROC glUseProgram_ptr = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_ptr = nullptr;
static PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation_ptr = nullptr;
static PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation_ptr = nullptr;
static PFNGLDELETESHADERPROC glDeleteShader_ptr = nullptr;
static PFNGLDELETEPROGRAMPROC glDeleteProgram_ptr = nullptr;
static PFNGLGETUNIFORMIVPROC glGetUniformiv_ptr = nullptr;
static PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_ptr = nullptr;
typedef void (APIENTRYP PFNGLUNIFORMMATRIX3FVPROC) (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
static PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv_ptr = nullptr;
static PFNGLUNIFORM1IPROC glUniform1i_ptr = nullptr;
static PFNGLUNIFORM1FPROC glUniform1f_ptr = nullptr;
static PFNGLUNIFORM4FPROC glUniform4f_ptr = nullptr;
static PFNGLUNIFORM4FVPROC glUniform4fv_ptr = nullptr;
static PFNGLUNIFORM3FPROC glUniform3f_ptr = nullptr;
static PFNGLUNIFORM3FVPROC glUniform3fv_ptr = nullptr;
static PFNGLUNIFORM1FVPROC glUniform1fv_ptr = nullptr;
static PFNGLUNIFORM2FPROC glUniform2f_ptr = nullptr;
static PFNGLUNIFORM2IPROC glUniform2i_ptr = nullptr;
static PFNGLUNIFORM4IPROC glUniform4i_ptr = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_ptr = nullptr;
static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_ptr = nullptr;
static PFNGLGETSHADERIVPROC glGetShaderiv_ptr = nullptr;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_ptr = nullptr;
static PFNGLGETPROGRAMIVPROC glGetProgramiv_ptr = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ptr = nullptr;
static PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform_ptr = nullptr;
static PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers_ptr = nullptr;
static PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer_ptr = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D_ptr = nullptr;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus_ptr = nullptr;
static PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer_ptr = nullptr;
static PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers_ptr = nullptr;
static PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer_ptr = nullptr;
static PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage_ptr = nullptr;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer_ptr = nullptr;
static PCGLGENQUERIESPROC glGenQueries_ptr = nullptr;
static PCGLBEGINQUERYPROC glBeginQuery_ptr = nullptr;
static PCGLENDQUERYPROC glEndQuery_ptr = nullptr;
static PCGLGETQUERYOBJECTIVPROC glGetQueryObjectiv_ptr = nullptr;
static PCGLGETQUERYOBJECTUI64VPROC glGetQueryObjectui64v_ptr = nullptr;

#include <unistd.h>
#include <pthread.h>
#include <execinfo.h> // Windows: sustituto vacío (pikmin2-decomp-adapter/win32)
#include <SDL2/SDL.h>

// Trazas de diagnostico del renderer: solo con PIKMIN_VERBOSE=1.
static bool pc_gfx_verbose()
{
    static const bool on = getenv("PIKMIN_VERBOSE") != nullptr;
    return on;
}

static void load_gl_functions() {
    glActiveTexture_ptr = (PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");
    glGenVertexArrays_ptr = (PFNGLGENVERTEXARRAYSPROC)SDL_GL_GetProcAddress("glGenVertexArrays");
    glGenerateMipmap_ptr = (PFNGLGENERATEMIPMAPPROC)SDL_GL_GetProcAddress("glGenerateMipmap");
    glInvalidateFramebuffer_ptr = (PCGLINVALIDATEFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glInvalidateFramebuffer");
    // Asked once. The maximum is a driver property, and requesting more than it
    // offers is an error rather than a request that gets clamped for us.
    if (SDL_GL_ExtensionSupported("GL_EXT_texture_filter_anisotropic")
        || SDL_GL_ExtensionSupported("GL_ARB_texture_filter_anisotropic")) {
        GLfloat maxAniso = 1.0f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
        if (maxAniso > 1.0f) {
            sAnisotropySupported = true;
            sAnisotropyMax = maxAniso;
        }
    }
    glBindVertexArray_ptr = (PFNGLBINDVERTEXARRAYPROC)SDL_GL_GetProcAddress("glBindVertexArray");
    glBlendEquation_ptr = (PFNGLBLENDEQUATIONPROC)SDL_GL_GetProcAddress("glBlendEquation");
    glGenBuffers_ptr = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
    glDrawBuffers_ptr = (PFNGLDRAWBUFFERSPROC)SDL_GL_GetProcAddress("glDrawBuffers");
    glBindBuffer_ptr = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
    glBufferData_ptr = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
    glBufferSubData_ptr = (PFNGLBUFFERSUBDATAPROC)SDL_GL_GetProcAddress("glBufferSubData");
    glMapBufferRange_ptr = (PFNGLMAPBUFFERRANGEPROC)SDL_GL_GetProcAddress("glMapBufferRange");
    glUnmapBuffer_ptr = (PFNGLUNMAPBUFFERPROC)SDL_GL_GetProcAddress("glUnmapBuffer");
    glFenceSync_ptr = (PFNGLFENCESYNCPROC)SDL_GL_GetProcAddress("glFenceSync");
    glClientWaitSync_ptr = (PFNGLCLIENTWAITSYNCPROC)SDL_GL_GetProcAddress("glClientWaitSync");
    glDeleteSync_ptr = (PFNGLDELETESYNCPROC)SDL_GL_GetProcAddress("glDeleteSync");
    glCreateShader_ptr = (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
    glShaderSource_ptr = (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
    glCompileShader_ptr = (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
    glCreateProgram_ptr = (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
    glAttachShader_ptr = (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
    glLinkProgram_ptr = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
    glUseProgram_ptr = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
    glGetUniformLocation_ptr = (PFNGLGETUNIFORMLOCATIONPROC)SDL_GL_GetProcAddress("glGetUniformLocation");
    glGetAttribLocation_ptr = (PFNGLGETATTRIBLOCATIONPROC)SDL_GL_GetProcAddress("glGetAttribLocation");
    glBindAttribLocation_ptr = (PFNGLBINDATTRIBLOCATIONPROC)SDL_GL_GetProcAddress("glBindAttribLocation");
    glDeleteShader_ptr = (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
    glDeleteProgram_ptr = (PFNGLDELETEPROGRAMPROC)SDL_GL_GetProcAddress("glDeleteProgram");
    glGetUniformiv_ptr = (PFNGLGETUNIFORMIVPROC)SDL_GL_GetProcAddress("glGetUniformiv");
    glUniformMatrix4fv_ptr = (PFNGLUNIFORMMATRIX4FVPROC)SDL_GL_GetProcAddress("glUniformMatrix4fv");
    glUniformMatrix3fv_ptr = (PFNGLUNIFORMMATRIX3FVPROC)SDL_GL_GetProcAddress("glUniformMatrix3fv");
    glUniform1i_ptr = (PFNGLUNIFORM1IPROC)SDL_GL_GetProcAddress("glUniform1i");
    glUniform1f_ptr = (PFNGLUNIFORM1FPROC)SDL_GL_GetProcAddress("glUniform1f");
    glUniform4f_ptr = (PFNGLUNIFORM4FPROC)SDL_GL_GetProcAddress("glUniform4f");
    glUniform4fv_ptr = (PFNGLUNIFORM4FVPROC)SDL_GL_GetProcAddress("glUniform4fv");
    glUniform3f_ptr = (PFNGLUNIFORM3FPROC)SDL_GL_GetProcAddress("glUniform3f");
    glUniform3fv_ptr = (PFNGLUNIFORM3FVPROC)SDL_GL_GetProcAddress("glUniform3fv");
    glUniform1fv_ptr = (PFNGLUNIFORM1FVPROC)SDL_GL_GetProcAddress("glUniform1fv");
    glUniform2f_ptr = (PFNGLUNIFORM2FPROC)SDL_GL_GetProcAddress("glUniform2f");
    glUniform2i_ptr = (PFNGLUNIFORM2IPROC)SDL_GL_GetProcAddress("glUniform2i");
    glUniform4i_ptr = (PFNGLUNIFORM4IPROC)SDL_GL_GetProcAddress("glUniform4i");
    glEnableVertexAttribArray_ptr = (PFNGLENABLEVERTEXATTRIBARRAYPROC)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
    glVertexAttribPointer_ptr = (PFNGLVERTEXATTRIBPOINTERPROC)SDL_GL_GetProcAddress("glVertexAttribPointer");
    glGetShaderiv_ptr = (PFNGLGETSHADERIVPROC)SDL_GL_GetProcAddress("glGetShaderiv");
    glGetShaderInfoLog_ptr = (PFNGLGETSHADERINFOLOGPROC)SDL_GL_GetProcAddress("glGetShaderInfoLog");
    glGetProgramiv_ptr = (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
    glGetProgramInfoLog_ptr = (PFNGLGETPROGRAMINFOLOGPROC)SDL_GL_GetProcAddress("glGetProgramInfoLog");
    glGetActiveUniform_ptr = (PFNGLGETACTIVEUNIFORMPROC)SDL_GL_GetProcAddress("glGetActiveUniform");
    glGenFramebuffers_ptr = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
    glBindFramebuffer_ptr = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    glFramebufferTexture2D_ptr = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    glCheckFramebufferStatus_ptr = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
    glBlitFramebuffer_ptr = (PFNGLBLITFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBlitFramebuffer");
    glGenRenderbuffers_ptr = (PFNGLGENRENDERBUFFERSPROC)SDL_GL_GetProcAddress("glGenRenderbuffers");
    glBindRenderbuffer_ptr = (PFNGLBINDRENDERBUFFERPROC)SDL_GL_GetProcAddress("glBindRenderbuffer");
    glRenderbufferStorage_ptr = (PFNGLRENDERBUFFERSTORAGEPROC)SDL_GL_GetProcAddress("glRenderbufferStorage");
    glFramebufferRenderbuffer_ptr = (PFNGLFRAMEBUFFERRENDERBUFFERPROC)SDL_GL_GetProcAddress("glFramebufferRenderbuffer");
    glGenQueries_ptr = (PCGLGENQUERIESPROC)SDL_GL_GetProcAddress("glGenQueries");
    glBeginQuery_ptr = (PCGLBEGINQUERYPROC)SDL_GL_GetProcAddress("glBeginQuery");
    glEndQuery_ptr = (PCGLENDQUERYPROC)SDL_GL_GetProcAddress("glEndQuery");
    glGetQueryObjectiv_ptr = (PCGLGETQUERYOBJECTIVPROC)SDL_GL_GetProcAddress("glGetQueryObjectiv");
    glGetQueryObjectui64v_ptr = (PCGLGETQUERYOBJECTUI64VPROC)SDL_GL_GetProcAddress("glGetQueryObjectui64v");
    // GLES: GL_TIME_ELAPSED y el resultado de 64 bits son de
    // EXT_disjoint_timer_query; glGetQueryObjectiv no existe en el núcleo de
    // ES 3 (solo la variante uiv), así que las cuatro entradas que faltan se
    // buscan con sufijo EXT.
    if (!glGenQueries_ptr) glGenQueries_ptr = (PCGLGENQUERIESPROC)SDL_GL_GetProcAddress("glGenQueriesEXT");
    if (!glBeginQuery_ptr) glBeginQuery_ptr = (PCGLBEGINQUERYPROC)SDL_GL_GetProcAddress("glBeginQueryEXT");
    if (!glEndQuery_ptr) glEndQuery_ptr = (PCGLENDQUERYPROC)SDL_GL_GetProcAddress("glEndQueryEXT");
    if (!glGetQueryObjectiv_ptr) glGetQueryObjectiv_ptr = (PCGLGETQUERYOBJECTIVPROC)SDL_GL_GetProcAddress("glGetQueryObjectivEXT");
    if (!glGetQueryObjectui64v_ptr) glGetQueryObjectui64v_ptr = (PCGLGETQUERYOBJECTUI64VPROC)SDL_GL_GetProcAddress("glGetQueryObjectui64vEXT");

    // Every pointer above is called without a null check, so a missing entry
    // point crashes the moment that feature is first used -- which can be deep
    // into a level rather than at startup. Windows' opengl32 exports only
    // OpenGL 1.1, so this is where a driver too old for the port shows up.
    // Name what is missing instead of leaving a silent landmine.
    {
        struct Entry { const char* name; const void* ptr; };
        const Entry entries[] = {
            { "glActiveTexture", (const void*)glActiveTexture_ptr },
            { "glBlendEquation", (const void*)glBlendEquation_ptr },
            { "glGenBuffers", (const void*)glGenBuffers_ptr },
            { "glBindBuffer", (const void*)glBindBuffer_ptr },
            { "glBufferData", (const void*)glBufferData_ptr },
            { "glCreateShader", (const void*)glCreateShader_ptr },
            { "glCreateProgram", (const void*)glCreateProgram_ptr },
            { "glUseProgram", (const void*)glUseProgram_ptr },
            { "glUniformMatrix4fv", (const void*)glUniformMatrix4fv_ptr },
        };
        int missing = 0;
        for (const Entry& entry : entries) {
            if (entry.ptr == nullptr) {
                fprintf(stderr, "[PC GX] OpenGL entry point missing: %s\n", entry.name);
                ++missing;
            }
        }
        if (missing != 0) {
            fprintf(stderr, "[PC GX] %d OpenGL entry point(s) unavailable. The driver is "
                            "too old for this port; expect a crash when they are used.\n",
                    missing);
        }
    }
}

// OpenGL uniform calls carry non-trivial validation/dispatch overhead. GX code
// commonly emits the same material state before many small primitives, so keep
// the last value for every location and only cross the driver boundary when it
// actually changes. Locations outside this deliberately generous range fall
// back to the raw call.
constexpr int PC_UNIFORM_CACHE_SIZE = 512;
// A location number only identifies a uniform within one program, and shader
// specialisation means several programs are now in play. Entries therefore
// carry the generation they were written in, and binding a different program
// bumps the generation: stale values are rejected in O(1) instead of clearing
// every table on each switch.
static uint32_t sUniformGeneration = 1;
// Diagnostic escape hatch: PIKMIN_NO_UNIFORM_CACHE=1 sends every write straight
// to the driver. If a symptom disappears with it set, the cache is at fault and
// not the generated shader.
static bool uniform_cache_disabled() {
    static const bool disabled = std::getenv("PIKMIN_NO_UNIFORM_CACHE") != nullptr;
    return disabled;
}
// Una textura creada con glTexImage2D(..., nullptr) trae lo que hubiera en esa
// VRAM: en NVIDIA, restos de fotogramas anteriores troceados en los bloques de
// la GPU (la selección de partida con píxeles y colores raros antes del vídeo
// de apertura). Todo destino de render se deja en negro transparente al crearlo.
static void invalidate_uniform_cache() {
    if (++sUniformGeneration == 0) sUniformGeneration = 1;
}

// Post-process programs share location indices (uDepth is often loc 1). The
// cache does not know which program is bound, so a CoC upload of uDepth=1
// makes the composite skip its own uDepth and leave it at the default unit
// 0 -- the scene colour. Dark ground then reads as "near camera" and DoF
// alone paints a noisy overlay. Bloom/AO change the shader and mask it.
static void post_bind_program(GLuint program)
{
    glUseProgram_ptr(program);
    invalidate_uniform_cache();
}
template <typename T> struct UniformCacheEntry {
    uint32_t generation = 0;
    T value {};
};
struct Uniform2iValue { GLint x, y; };
struct Uniform4iValue { GLint x, y, z, w; };
struct Uniform4fValue { GLfloat x, y, z, w; };
struct UniformMat4Value { GLfloat value[16]; };
struct UniformMat3Value { GLfloat value[9]; };
static UniformCacheEntry<GLint> sUniform1iCache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<GLfloat> sUniform1fCache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<Uniform2iValue> sUniform2iCache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<Uniform4iValue> sUniform4iCache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<Uniform4fValue> sUniform4fCache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<UniformMat4Value> sUniformMat4Cache[PC_UNIFORM_CACHE_SIZE];
static UniformCacheEntry<UniformMat3Value> sUniformMat3Cache[PC_UNIFORM_CACHE_SIZE];


// Names the exact uniform write that OpenGL rejects. A rejected write leaves
// the shader reading a stale value, which is indistinguishable from a wrong
// shader by looking at the picture alone. Enabled by PIKMIN_GL_CHECK.
static GLuint sCheckProgram = 0;
static bool uniform_check_enabled() {
    static const bool enabled = std::getenv("PIKMIN_GL_CHECK") != nullptr;
    return enabled;
}
static void check_uniform_write(GLint loc, const char* kind) {
    if (!uniform_check_enabled()) return;
    const GLenum error = glGetError();
    if (error == GL_NO_ERROR) return;
    static int reported = 0;
    if (reported++ > 40) return;
    char name[128] = "?";
    GLint size = 0; GLenum type = 0; GLsizei length = 0;
    // Locations are not indices, so find the active uniform that owns this one.
    GLint count = 0;
    if (glGetProgramiv_ptr) glGetProgramiv_ptr(sCheckProgram, GL_ACTIVE_UNIFORMS, &count);
    for (GLint i = 0; i < count; ++i) {
        char probe[128];
        if (!glGetActiveUniform_ptr) break;
        glGetActiveUniform_ptr(sCheckProgram, i, sizeof(probe), &length, &size, &type, probe);
        if (glGetUniformLocation_ptr(sCheckProgram, probe) == loc) {
            snprintf(name, sizeof(name), "%s", probe);
            break;
        }
    }
    printf("[PC GX uniform] %s write to location %d rejected (0x%04x) in program %u; that location holds '%s'\n",
           kind, loc, unsigned(error), unsigned(sCheckProgram), name);
    fflush(stdout);
}

static void cached_uniform1i(GLint loc, GLint value) {
    if (loc < 0) return;
    if (uniform_cache_disabled()) { glUniform1i_ptr(loc, value); return; }
    if (loc >= PC_UNIFORM_CACHE_SIZE) { glUniform1i_ptr(loc, value); return; }
    auto& entry = sUniform1iCache[loc];
    if (entry.generation == sUniformGeneration && entry.value == value) return;
    entry.generation = sUniformGeneration; entry.value = value;
    glUniform1i_ptr(loc, value);
    check_uniform_write(loc, "1i");
}
static void cached_uniform1f(GLint loc, GLfloat value) {
    if (loc < 0) return;
    if (uniform_cache_disabled()) { glUniform1f_ptr(loc, value); return; }
    if (loc >= PC_UNIFORM_CACHE_SIZE) { glUniform1f_ptr(loc, value); return; }
    auto& entry = sUniform1fCache[loc];
    if (entry.generation == sUniformGeneration && entry.value == value) return;
    entry.generation = sUniformGeneration; entry.value = value;
    glUniform1f_ptr(loc, value);
    check_uniform_write(loc, "1f");
}
static void cached_uniform2i(GLint loc, GLint x, GLint y) {
    if (loc < 0) return;
    if (uniform_cache_disabled()) { glUniform2i_ptr(loc, x, y); return; }
    if (loc >= PC_UNIFORM_CACHE_SIZE) { glUniform2i_ptr(loc, x, y); return; }
    auto& entry = sUniform2iCache[loc];
    const Uniform2iValue value { x, y };
    if (entry.generation == sUniformGeneration && memcmp(&entry.value, &value, sizeof(value)) == 0) return;
    entry.generation = sUniformGeneration; entry.value = value;
    glUniform2i_ptr(loc, x, y);
    check_uniform_write(loc, "2i");
}
static void cached_uniform4i(GLint loc, GLint x, GLint y, GLint z, GLint w) {
    if (loc < 0) return;
    if (uniform_cache_disabled()) { glUniform4i_ptr(loc, x, y, z, w); return; }
    if (loc >= PC_UNIFORM_CACHE_SIZE) { glUniform4i_ptr(loc, x, y, z, w); return; }
    auto& entry = sUniform4iCache[loc];
    const Uniform4iValue value { x, y, z, w };
    if (entry.generation == sUniformGeneration && memcmp(&entry.value, &value, sizeof(value)) == 0) return;
    entry.generation = sUniformGeneration; entry.value = value;
    glUniform4i_ptr(loc, x, y, z, w);
    check_uniform_write(loc, "4i");
}
static void cached_uniform4f(GLint loc, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    if (loc < 0) return;
    if (uniform_cache_disabled()) { glUniform4f_ptr(loc, x, y, z, w); return; }
    if (loc >= PC_UNIFORM_CACHE_SIZE) { glUniform4f_ptr(loc, x, y, z, w); return; }
    auto& entry = sUniform4fCache[loc];
    const Uniform4fValue value { x, y, z, w };
    if (entry.generation == sUniformGeneration && memcmp(&entry.value, &value, sizeof(value)) == 0) return;
    entry.generation = sUniformGeneration; entry.value = value;
    glUniform4f_ptr(loc, x, y, z, w);
    check_uniform_write(loc, "4f");
}
static void cached_uniform_mat4(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (loc < 0 || !value) return;
    if (uniform_cache_disabled()) { glUniformMatrix4fv_ptr(loc, count, transpose, value); return; }
    if (count != 1 || transpose != GL_FALSE || loc >= PC_UNIFORM_CACHE_SIZE) {
        glUniformMatrix4fv_ptr(loc, count, transpose, value); return;
    }
    auto& entry = sUniformMat4Cache[loc];
    if (entry.generation == sUniformGeneration && memcmp(entry.value.value, value, sizeof(entry.value.value)) == 0) return;
    entry.generation = sUniformGeneration; memcpy(entry.value.value, value, sizeof(entry.value.value));
    glUniformMatrix4fv_ptr(loc, count, transpose, value);
    check_uniform_write(loc, "mat4");
}
static void cached_uniform_mat3(GLint loc, GLsizei count, GLboolean transpose, const GLfloat* value) {
    if (loc < 0 || !value) return;
    if (uniform_cache_disabled()) { glUniformMatrix3fv_ptr(loc, count, transpose, value); return; }
    if (count != 1 || transpose != GL_FALSE || loc >= PC_UNIFORM_CACHE_SIZE) {
        glUniformMatrix3fv_ptr(loc, count, transpose, value); return;
    }
    auto& entry = sUniformMat3Cache[loc];
    if (entry.generation == sUniformGeneration && memcmp(entry.value.value, value, sizeof(entry.value.value)) == 0) return;
    entry.generation = sUniformGeneration; memcpy(entry.value.value, value, sizeof(entry.value.value));
    glUniformMatrix3fv_ptr(loc, count, transpose, value);
    check_uniform_write(loc, "mat3");
}

#define glUniform1i_ptr cached_uniform1i
#define glUniform1f_ptr cached_uniform1f
#define glUniform2i_ptr cached_uniform2i
#define glUniform4i_ptr cached_uniform4i
#define glUniform4f_ptr cached_uniform4f
#define glUniformMatrix4fv_ptr cached_uniform_mat4
#define glUniformMatrix3fv_ptr cached_uniform_mat3

// ── State Storage ──
struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b, a;
    // The current GLSL backend exposes four texture-coordinate varyings.
    // TEX4-TEX7 still have to be consumed from GX display lists, but retaining
    // them in every streamed vertex only inflated the upload by 32 bytes.
    float tex[4][2];
    float matrixSlot;
    // TEXnMTXIDX de las generaciones 0-2: (idx0+1) + (idx1+1)*128 + (idx2+1)*16384,
    // 0 = el vértice no elige matriz de textura.
    float texMtxPacked;
};
static_assert(sizeof(Vertex) == 80, "Keep the streamed GX vertex compact");

// Decoded display-list geometry is immutable for the model paths that use a
// PNMTXIDX palette. Keep raw vertices so repeated draws can skip GX parsing.

static float sProjMatrix[16];
static float sPosMatrix[64][16];
static u32 sCurrentPosMtxId = 0;
// Generation of the material/lighting/texture state the batch key hashes.
// Every function that writes any of it calls state_touched(); the key is then
// recomputed only when this differs from the generation it was last computed
// for (compute_batch_state_key). PIKMIN_STATEKEY_CHECK=1 recomputes the full
// hash on every primitive and reports a setter this bookkeeping missed.
static uint32_t sStateGen = 1;
static inline void state_touched(); // defined with the primitive state below

// Normal matrices (3x3 rotation part, stored column-major)
static float sNrmMatrix[64][9];

// Texture coordinate generation
struct TexCoordGen {
    bool active = false;
    int type = 1;      // GX_TG_MTX3X4
    int src = 4;       // GX_TG_TEX0
    u32 mtxIdx = 0;
    // Segunda transformación (dual tex): GX_PTTEXMTX0..19 = 64..121,
    // GX_PTIDENTITY = 125. normalize: normaliza antes de aplicarla.
    u32 postIdx = 125;
    bool normalize = false;
};
static TexCoordGen sTexCoordGen[8];
static float sTexMatrices[64][16];
// Memoria de matrices de post-transformación: 64 filas de 4 floats (XF
// 0x500-0x5FF). La matriz con id N ocupa las filas N-64..N-62; las filas
// 61-63 (GX_PTIDENTITY) son identidad.
static float sPostRows[64][4];
static uint32_t sPostMtxGen = 0;
static bool sPostRowsInit = [] {
    for (int r = 61; r < 64; ++r)
        for (int c = 0; c < 4; ++c) sPostRows[r][c] = (c == r - 61) ? 1.0f : 0.0f;
    return true;
}();
// Revisión conjunta de las matrices de textura 30..59, que los vértices pueden
// elegir uno a uno con TEXnMTXIDX (reflejos de modelos con esqueleto).
static uint32_t sTexPaletteGen = 0;
// Generaciones (0-3) cuya coordenada con fuente NRM ya calculó el decodificador
// de listas de display en la CPU (ver bake_nrm_texgen): el shader la usa tal cual.
static int sTexBakedMask = 0;
// Matrix contents change far less often than they are drawn with, so the batch
// key hashes a revision number per slot rather than the floats themselves.
// That removes ~600 of the ~2 KB the key used to walk on every primitive.
static uint32_t sPosMtxGen[64] = {};
static uint32_t sNrmMtxGen[64] = {};
static uint32_t sTexMtxGen[64] = {};
static uint32_t sProjMtxGen = 0;
static bool sTexMtxLoaded[64] = {};

// Lighting state
struct GfxLight {
    float pos[3];    // world position (or half-vector for specular light 7)
    float dir[3];    // direction / half-vector
    float a[3];      // specular attenuation a0,a1,a2 (N.H quadratic curve)
    float color[4];  // RGBA 0-1
    float k[3];      // distance attenuation k0,k1,k2
    bool active;
};
static GfxLight sLights[8] = {};
// Bumped whenever a light is (re)loaded. The batch key hashes this instead of
// the 68-byte light itself: lights are the largest part of the state and the
// only place they change is pc_gfx_load_light.
static uint32_t sLightGen[8] = {};

// TEV swap mode state
struct TevSwapMode {
    GXTevColorChan red;
    GXTevColorChan green;
    GXTevColorChan blue;
    GXTevColorChan alpha;
};
static TevSwapMode sTevSwapModes[4] = {
    { GX_CH_RED,   GX_CH_GREEN, GX_CH_BLUE,  GX_CH_ALPHA },
    { GX_CH_RED,   GX_CH_RED,   GX_CH_RED,   GX_CH_ALPHA },
    { GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA },
    { GX_CH_BLUE,  GX_CH_BLUE,  GX_CH_BLUE,  GX_CH_ALPHA },
};
static GXTevSwapSel sTevRasSwapSel[GX_MAXTEVSTAGE] = {};
static GXTevSwapSel sTevTexSwapSel[GX_MAXTEVSTAGE] = {};

struct GfxChannel {
    bool enabled = false;
    GXColorSrc matSrc = GX_SRC_VTX;      // Hardware GXInit default: vertex colors
    GXColorSrc ambSrc = GX_SRC_REG;
    u32 lightMask = 0;
    GXDiffuseFn diffFn = GX_DF_NONE;
    GXAttnFn attnFn = GX_AF_NONE;
    bool alphaEnabled = false;
    GXColorSrc alphaMatSrc = GX_SRC_VTX;
    GXColorSrc alphaAmbSrc = GX_SRC_REG;
    u32 alphaLightMask = 0;
    GXDiffuseFn alphaDiffFn = GX_DF_NONE;
    GXAttnFn alphaAttnFn = GX_AF_NONE;
    float matColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };  // White: multiply-through no-op
    // GXInit initializes both ambient channel registers to black.  In
    // particular COLOR1 is normally specular; a white default makes every
    // specular material fully white even before a light contributes.
    float ambColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};
static GfxChannel sChannels[2] = {}; // COLOR0/ALPHA0, COLOR1/ALPHA1

static constexpr GXAttnFn decode_xf_attn_fn(u32 control) {
    // GXSetChanCtrl: bit 9 = attenuation enabled (attn != NONE), bit 10 =
    // attenuation select (attn != SPEC). So SPEC is 1/0, SPOT 1/1, NONE 0/1.
    const bool attnEnable = (control & (1u << 9)) != 0;
    const bool attnSelect = (control & (1u << 10)) != 0;
    if (!attnEnable) return GX_AF_NONE;
    return attnSelect ? GX_AF_SPOT : GX_AF_SPEC;
}

static constexpr u32 decode_xf_light_mask(u32 control) {
    return ((control >> 2) & 0x0Fu) | ((control >> 7) & 0xF0u);
}

static void decode_xf_channel_control(GfxChannel& ch, u32 control, bool alpha) {
    const GXColorSrc matSrc = (control & (1u << 0)) ? GX_SRC_VTX : GX_SRC_REG;
    const bool enabled = (control & (1u << 1)) != 0;
    const GXColorSrc ambSrc = (control & (1u << 6)) ? GX_SRC_VTX : GX_SRC_REG;
    const u32 lightMask = decode_xf_light_mask(control);
    const GXDiffuseFn diffFn = GXDiffuseFn((control >> 7) & 0x3);
    const GXAttnFn attnFn = decode_xf_attn_fn(control);
    if (alpha) {
        ch.alphaMatSrc = matSrc;
        ch.alphaEnabled = enabled;
        ch.alphaAmbSrc = ambSrc;
        ch.alphaLightMask = lightMask;
        ch.alphaDiffFn = diffFn;
        ch.alphaAttnFn = attnFn;
    } else {
        ch.matSrc = matSrc;
        ch.enabled = enabled;
        ch.ambSrc = ambSrc;
        ch.lightMask = lightMask;
        ch.diffFn = diffFn;
        ch.attnFn = attnFn;
    }
}

static_assert(decode_xf_light_mask((1u << 2) | (1u << 14)) == 0x81,
              "XF channel light-mask decoding must preserve lights 0 and 7");

static GLuint sVBO = 0;
// En GLES el VBO es un anillo con fences (ver vbo_ring_*): necesita sitio
// para los frames que la GPU aún no ha consumido. Antes de las mallas
// residentes (fase 1) la pantalla de título movía ~21 MB de vértices por
// frame y el anillo era de 64 MB; ahora por él solo pasa la geometría
// inmediata (partículas, HUD, texto: ~60 KB/frame en el título, medido) y
// el primer frame de cada escena, que da la vuelta al anillo esperando a
// la GPU y no se nota porque es un frame de carga. El driver hace residente
// todo el anillo, así que 8 MB en vez de 64 son 56 MB de PSS menos.
#if PIKI_USE_GLES
static size_t sVboCapacity = 8 * 1024 * 1024;
#else
static size_t sVboCapacity = 16 * 1024 * 1024;
#endif
static size_t sVboWriteOffset = 0;
static bool vbo_upload_by_mapping();
static void vbo_ring_frame_begin();
static size_t sVboBytesLastFrame = 0;   // streamed vertex bytes of the previous frame
static size_t sVboBytesPeakFrame = 0;
static GLuint sShaderProgram = 0;
static GLuint sNativeFramebuffer = 0;
static GLuint sNativeColorTexture = 0;
static GLuint sNativeDepthStencil = 0;   // renderbuffer, the fallback
static GLuint sNativeDepthTexture = 0;   // sampled by post-process passes
static bool sDepthIsTexture       = false;
static bool sNativeFramebufferReady = false;
static int sRenderWidth = 640;
static int sRenderHeight = 480;
static float sRenderScale = 2.0f / 3.0f;
static bool sRenderScaleSet = false; // true once set by the settings module

// Aspect ratio support.
enum AspectRatioMode {
    ASPECT_AUTO = 0,    // Detect from window
    ASPECT_4_3 = 1,
    ASPECT_16_10 = 2,
    ASPECT_16_9 = 3,
    ASPECT_21_9 = 4,
    ASPECT_COUNT
};
static int sAspectRatioMode = ASPECT_AUTO;
static float sCurrentAspectRatio = 4.0f / 3.0f; // Current active aspect ratio
static GLuint sBoundTextures[8] = {};
static uint64_t sPerfDraws = 0;

// Instrumentation for the projection sequence. Enabled with PIKMIN_PROJ_DEBUG=1.
// The most recent perspective's terms, held until the world is known to have
// finished so the right ones can be committed. See pc_gfx_set_projection.
static float sPendingInvP00 = 1.0f, sPendingInvP11 = 1.0f;
static float sPendingNear = 1.0f, sPendingFar = 15000.0f;
static bool sPendingValid = false;
// Pikmin 2 pone despues del mundo otra perspectiva sin apenas draws (near=10
// far=10000) y el mundo reutiliza la perspectiva de frames anteriores; se
// confirmaba la equivocada y DOF desenfocaba todo y las sombras no casaban.
// Se atribuyen los draws a la perspectiva activa y se confirma la que mas dibujo.
#if PIKI_P2_HOST
static constexpr bool kP2Host = true;
#else
static constexpr bool kP2Host = false;
#endif
static bool sCurPerspValid = false;
static float sCurInvP00, sCurInvP11, sCurNear, sCurFar;
static uint64_t sCurDrawStart = 0;
static float sBestInvP00, sBestInvP11, sBestNear, sBestFar;
static int sBestDraws = -1;
static uint64_t sDrawsAtFrameStart = 0;
// Inicio real del frame (tras el present). pc_gfx_begin_frame no sirve: en
// Pikmin 2 lo dispara GXInvalidateVtxCache, que j3dSys.drawInit() llama varias
// veces en mitad del mundo y reiniciaba el recuento.
static uint64_t sPresentDrawStart = 0;
static void persp_account_draws() {
    if (!sCurPerspValid) return;
    const uint64_t from = sCurDrawStart > sPresentDrawStart ? sCurDrawStart : sPresentDrawStart;
    const int drawn = int(sPerfDraws - from);
    if (drawn > 0 && drawn > sBestDraws) {
        sBestDraws = drawn;
        sBestInvP00 = sCurInvP00; sBestInvP11 = sCurInvP11;
        sBestNear = sCurNear; sBestFar = sCurFar;
    }
}
static bool sProjDebug = false;
static int sProjSeenThisFrame = 0;
static uint64_t sDrawsAtProjection = 0;
static uint64_t sPerfVertices = 0;
static uint64_t sPerfDisplayLists = 0;
static uint64_t sPerfDisplayListBytes = 0;
static uint64_t sPerfTextureUploads = 0;
static uint64_t sPerfFastDraws = 0;
static uint64_t sPerfSourcePrimitives = 0;
static uint64_t sPerfPnMtxMask = 0;
static uint8_t sPerfPnMtxMax = 0;
static uint64_t sPerfFastPathDraws[4] = {};
static uint64_t sPerfFastPathVertices[4] = {};
static uint64_t sPerfPrimitiveDraws[7] = {};
static bool sPerfStatsEnabled = false;
// Timer queries de GPU: las quiere PIKMIN_PERF_STATS (media por segundo en
// stderr) y también el tick profiler (p50/p99 por frame, y el HUD). Son
// asíncronas: no cuestan nada al frame salvo dos glBeginQuery/glEndQuery.
static bool sGpuTimingEnabled = false;
static constexpr unsigned PC_GPU_QUERY_RING_SIZE = 8;
struct PerfGpuQuery {
    GLuint scene = 0;
    GLuint blit = 0;
    bool pending = false;
};
static PerfGpuQuery sPerfGpuQueries[PC_GPU_QUERY_RING_SIZE] = {};
static unsigned sPerfGpuQueryWrite = 0;
static bool sPerfGpuQueriesReady = false;
static bool sPerfGpuSceneActive = false;
static uint64_t sPerfGpuSceneNs = 0;
static uint64_t sPerfGpuBlitNs = 0;
static uint64_t sPerfGpuSamples = 0;
struct PerfScope {
    const char* name = nullptr;
    uint64_t draws = 0;
    uint64_t vertices = 0;
    u8 pipeline5ReportCount = 0;
    u8 pipeline7ReportCount = 0;
};
static PerfScope sPerfScopes[8] = {};
static int sPerfCurrentScope = -1;
static uint64_t sPerfTevStageDraws[GX_MAXTEVSTAGE + 1] = {};
struct PerfTevPattern { uint64_t key = 0; uint64_t count = 0; };
static PerfTevPattern sPerfTevPatterns[128] = {};
struct PerfTevMultiPattern { uint64_t key = 0; uint64_t count = 0; u8 stages = 0; };
static PerfTevMultiPattern sPerfTevMultiPatterns[128] = {};
// Uniform locations belong to a program, and specialisation means there is
// now one program per TEV configuration instead of a single ubershader. The
// active program's locations live here; switching programs assigns a cached
// set rather than re-querying the driver.
struct ProgramLocations {
	GLint projMtx = -1;
	GLint posMtx = -1;
	GLint usePalette = -1;
	GLint posPalette = -1;
	GLint nrmPalette = -1;
	GLint materialColor = -1;
	GLint useMaterialRgb = -1;
	GLint useMaterialAlpha = -1;
	GLint alphaComp0 = -1;
	GLint alphaComp1 = -1;
	GLint alphaOp = -1;
	GLint alphaRef0 = -1;
	GLint alphaRef1 = -1;
	GLint outTint = -1;
	GLint perPixel = -1;
	GLint numStages = -1;
	GLint fastPath = -1;
	GLint tevPrev = -1;
	GLint tevReg0 = -1;
	GLint tevReg1 = -1;
	GLint tevReg2 = -1;
	GLint konst[4] = {-1, -1, -1, -1};
	GLint tevKonst[GX_MAXTEVSTAGE] = {};
	GLint indMtx = -1;   // uIndMtx[0] (vec3[6])
	GLint indScale = -1; // uIndScale[0] (float[3])
	GLint tevCSel[GX_MAXTEVSTAGE] = {};
	GLint tevASel[GX_MAXTEVSTAGE] = {};
	GLint tevCOps[GX_MAXTEVSTAGE] = {};
	GLint tevAOps[GX_MAXTEVSTAGE] = {};
	GLint tevTexInfo[GX_MAXTEVSTAGE] = {};
	GLint tevSwapTable[4] = {};
	GLint tevSwapSel[GX_MAXTEVSTAGE] = {};
	GLint tex[8] = {};
	GLint nrmMtx = -1;
	GLint numLights = -1;
	GLint lightPos[4] = {};
	GLint lightColor[4] = {};
	GLint lightK[4] = {};
	GLint lightDir[4] = {};
	GLint lightA[4] = {};
	GLint lightDir1[4] = {};
	GLint lightA1[4] = {};
	GLint ambColor = -1;
	GLint chan0En = -1;
	GLint chan1En = -1;
	GLint chan0AttnFn = -1;
	GLint chan1AttnFn = -1;
	GLint numLights1 = -1;
	GLint lightPos1[4] = {};
	GLint lightColor1[4] = {};
	GLint lightK1[4] = {};
	GLint ambColor1 = -1;
	GLint materialColor1 = -1;
	GLint useMaterialRgb1 = -1;
	GLint specHalf1 = -1;   // specular half-vector for channel 1
	GLint specAttn1 = -1;   // specular a[] coefficients
	// ALPHA0 channel lighting (GXSetChanCtrl on GX_ALPHA0): its own light set.
	GLint numLightsA = -1;
	GLint chanA0En = -1;
	GLint chanA0AttnFn = -1;
	GLint ambAlpha0 = -1;
	GLint lightPosA[4] = {};
	GLint lightAlphaA = -1;   // the four lights' alpha in one vec4
	GLint lightKA[4] = {};
	GLint lightDirA[4] = {};
	GLint lightAA[4] = {};
	GLint fogParams = -1;
	GLint fogColour = -1;
	GLint tevChan[GX_MAXTEVSTAGE] = {};
	GLint tcMode[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
	GLint tcMtx[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
	GLint tcProj = -1;
	GLint tcPost = -1;     // uTcPost[0] (vec4[12])
	GLint tcPostOn = -1;
	GLint tcNorm = -1;
	GLint tc2x4 = -1;
	GLint texPalette = -1; // uTexPalette[0] (vec4[30])
};

static ProgramLocations sLoc;
// Fixed vertex attribute slots, forced with glBindAttribLocation on every
// program so a single vertex array object describes them all.
enum : GLuint {
    kAttrPosIndex       = 0,
    kAttrColorIndex     = 1,
    kAttrNormalIndex    = 2,
    kAttrTexCoord0Index = 3, // through 6; the Vertex carries four of them
    kAttrMatrixSlotIndex = 7,
    kAttrTexMtxIndex     = 8,
};
static constexpr int kVertexTexCoordCount = 4;
static GLint sAttrNormal = GLint(kAttrNormalIndex);
// Only the four coordinates the streamed Vertex actually carries get a slot;
// the rest stay -1 so nothing enables an array that reads past the struct.
static GLint sAttrTexCoord[8] = {
    GLint(kAttrTexCoord0Index + 0), GLint(kAttrTexCoord0Index + 1),
    GLint(kAttrTexCoord0Index + 2), GLint(kAttrTexCoord0Index + 3),
    -1, -1, -1, -1,
};
static GLint sAttrPos = GLint(kAttrPosIndex);
static GLint sAttrColor = GLint(kAttrColorIndex);

// Ubershader locations, kept so the fallback path can be restored, plus the
// program currently bound and the vertex shader shared by every program.
static ProgramLocations sUberLocations;
static GLuint sCurrentProgram = 0;
static GLuint sSharedVertexShader = 0;
// One generated program per TEV configuration instead of the interpreting
// ubershader. PIKMIN_TEV_SPECIALIZE=0 falls back to the ubershader for the
// whole session; PIKMIN_TEV_MAX_STAGES=N restricts specialisation to materials
// of at most N stages, which is useful for bisecting a suspected bad material.
static bool sSpecialiseShaders = true;
static int sSpecialiseMaxStages = 16;
static bool sSpecialiseFailed = false;
static uint64_t sPerfShaderCompiles = 0;

static PcGfxHostVector<Vertex> sVertexStream;
static GXPrimitive sCurrentPrimType;
static size_t sExpectedVerts = 0;
static bool sInPrimitive = false;
static bool sHaveVertex = false;
static bool sVerticesPretransformed = false;
static bool sVertexUsesPalette = false;
static bool sGpuSkinningEnabled = false;

// The FIFO consumes vertex data as it is written, so a register write that
// arrives after a primitive's vertices applies to the *next* primitive. The
// port only learns that a primitive is over at GXEnd or the next GXBegin,
// and Pikmin 2's JSystem seldom calls GXEnd (J2DPictureEx::drawTexCoord,
// J2DGrafContext, JUTFont...): without this, every 2D pane was drawn with
// the following pane's material, and the last one of the frame with whatever
// the next frame set up first (the title's clear box: a white slab with the
// perspective port). So a state change closes an open primitive whose
// vertices have started arriving. Between GXBegin and its last vertex GX
// allows no state writes, so nothing legitimate is cut short.
static bool sEndingPrimitive = false; // pc_gfx_end itself touches state (program selection)
static inline void state_touched() {
    ++sStateGen;
    if (sInPrimitive && !sEndingPrimitive && (sHaveVertex || !sVertexStream.empty())) {
        sEndingPrimitive = true;
        pc_gfx_end();
        sEndingPrimitive = false;
    }
}

// PERF-NATIVE-002 step 1: GL pipeline state that lives outside pc_gfx_end
// (blend, depth, cull, color mask, viewport, scissor, framebuffer, clears) is
// not mirrored in any variable here, so the probe cannot hash it. Each setter
// bumps this instead: a change of epoch is a change of state, which is exactly
// what a batch would have to flush on. The same list is the flush-point
// inventory step 5 needs, so it is worth getting complete now rather than
// rediscovering it as visual corruption later.
static uint64_t sGlStateEpoch = 0;
void pc_gfx_flush_batch(void);
// Called from each GL-state setter *after* its redundancy guard and *before*
// it touches GL, so the pending batch is drawn under the state it was built
// with. Placing it after the guard matters: most of the game's state
// programming is redundant, and flushing on those would defeat batching.
static inline void pc_gfx_note_gl_state_change() {
    pc_gfx_flush_batch();
    ++sGlStateEpoch;
}

// The pipeline setters (blend, depth, cull, viewport, scissor) keep a
// redundancy guard so a repeated GX call does not touch GL. post_apply()
// writes those bits of GL itself -- it disables blend, depth and cull --
// and present() does not put them back. A guard that still believes the
// last GX blend is bound then skips the glEnable that would have corrected
// it. On the file-select screen that is fatal: the whole frame is
// perspective, so the pass runs at present() rather than at an ortho
// boundary, and the next frame's pictures (IA4 stars, I8 sparkles) draw
// with blending left off. Their dark texels overwrite the background as
// opaque black squares.
static uint32_t sGlPipelineGuardSerial = 1;
static void invalidate_gl_pipeline_guards() {
    ++sGlPipelineGuardSerial;
}

static bool sFileSelDebugReport = false;
static bool sFileSelFxWindow = false;
static uint64_t sGfxFrameSerial = 0;
static void filesel_debug_probe_now(const char* tag, GLuint fbo);
static void filesel_debug_log_draw();
static void filesel_debug_on_present(bool latePost, GLuint produced);
static void filesel_debug_note_ortho_post();
static void filesel_debug_print_post_ortho();

// ── Render Packet Capture System ──
// Captures immutable render packets at the GX -> OpenGL boundary.
// These packets can be replayed without re-entering game code.

static PcRenderPacketStore sPacketStore;
static bool sCaptureEnabled = false;
static bool sCaptureActive = false;
static uint64_t sCurrentCaptureSerial = 0;
static PcGfxHostVector<uint8_t> sCaptureBuffer;
static float sReplayClearColor[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
static float sReplayClearDepth = 1.0f;

void pc_gfx_begin_capture(uint64_t serial) {
    if (!sCaptureEnabled) return;
    // The packet store (make_unique/std::vector) outlives the section heap.
    PcGfxHostAllocScope hostAllocScope;
    sCaptureActive = true;
    sCurrentCaptureSerial = serial;
    sCaptureBuffer.clear();
    sPacketStore.beginAuthoritativeTick(serial);
}

void pc_gfx_end_capture() {
    PcGfxHostAllocScope hostAllocScope;
    sCaptureActive = false;
    sPacketStore.endAuthoritativeTick();
}

void pc_gfx_enable_capture(bool enabled) {
    sCaptureEnabled = enabled;
}

bool pc_gfx_is_capture_enabled() {
    return sCaptureEnabled;
}

PcRenderPacketStore& pc_gfx_get_packet_store() {
    return sPacketStore;
}

// Replay captured display list without re-entering game code
void pc_gfx_replay_display_list(const void* list, u32 nbytes) {
    if (!list || nbytes == 0) return;
    // Temporarily disable capture during replay to avoid infinite recursion
    bool wasCapturing = sCaptureActive;
    sCaptureActive = false;
    pc_gfx_call_display_list(list, nbytes);
    sCaptureActive = wasCapturing;
}

// Replay the entire authoritative tick's captured display lists into a cleared
// internal framebuffer and present it, WITHOUT re-entering any game code.
// This demonstrates that replaying the same immutable packet twice is visually
// identical. Returns false if there is nothing to replay.
bool pc_gfx_replay_captured_frame(void) {
    PcRenderPacketStore& store = sPacketStore;
    if (!sCaptureEnabled) return false;
    PcGfxHostAllocScope hostAllocScope; // preparePresentation copies packets

    const uint64_t targetSerial = pc_render_tick_serial();
    if (!store.preparePresentation(targetSerial, 1.0)) return false;

    const PcRenderFrame* frame = store.getPresentationFrame();
    if (!frame || frame->packets.empty()) {
        store.clearPresentation();
        return false;
    }

    if (!sNativeFramebufferReady || !glBindFramebuffer_ptr) return false;

    // Bind the internal framebuffer and clear to the authoritative clear color.
    pc_gfx_flush_batch();
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    glClearColor(sReplayClearColor[0], sReplayClearColor[1], sReplayClearColor[2], sReplayClearColor[3]);
#if PIKI_USE_GLES
    glClearDepthf(sReplayClearDepth);
#else
    glClearDepth(sReplayClearDepth);
#endif
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Replay all captured display lists without touching game state.
    bool wasCapturing = sCaptureActive;
    sCaptureActive = false;
    for (const auto& packet : frame->packets) {
        if (packet->valid && !packet->displayListPayload.empty()) {
            pc_gfx_call_display_list(packet->displayListPayload.data(),
                                     static_cast<u32>(packet->displayListPayload.size()));
        }
    }
    sCaptureActive = wasCapturing;

    store.clearPresentation();

    // Present the replayed frame to the default framebuffer.
    pc_gfx_present();
    return true;
}

struct VertexArrayState {
    const u8* base = nullptr;
    u8 stride = 0;
};struct VertexFormatState {
    GXCompCnt count = GX_COMPCNT_NULL;
    GXCompType type = GX_F32;
    u8 frac = 0;
};
static GXAttrType sVtxDesc[GX_VA_MAX_ATTR] = {};
static VertexArrayState sVtxArrays[GX_VA_MAX_ATTR];
static VertexFormatState sVtxFormats[GX_MAX_VTXFMT][GX_VA_MAX_ATTR];

// ── Resident meshes (PLAN_RENDIMIENTO fase 1) ───────────────────────────────
//
// A static display list -- one with no BP/XF/CP commands inside, which is
// every model mesh -- is parsed once into triangle-expanded, model-space
// vertices and uploaded to a static VBO arena. Every later call draws it from
// there with one glDrawArrays: no parsing, no CPU transform, no upload. The
// matrix palette (aMatrixSlot per vertex, uPosPalette/uNrmPalette) does the
// skinning the CPU did, exactly like the GameCube's XF unit.
//
// Cache identity: the list pointer, plus a signature of everything the parse
// depended on (vertex descriptor, the formats and arrays actually used).
// Invalidation follows the hardware contract: the game calls DCFlushRange /
// DCStoreRange over any vertex data it rewrites (dgxGraphics.cpp initMesh),
// which drops every mesh whose source range intersects; a heap reset drops
// them all. PIKMIN_MESH_CACHE=0 disables the whole path for A/B comparison.
struct ResidentMesh {
    const void* list = nullptr;
    u32 nbytes = 0;
    GLint firstVertex = 0;
    GLsizei vertexCount = 0;
    bool skinned = false;   // carries PNMTXIDX: draw with the palette
    int paletteSlots = 0;   // slots the palette upload must cover
    uintptr_t lo = 0, hi = 0; // range of every CPU byte the parse read
    // Signature of the parse inputs.
    GXAttrType desc[GX_VA_MAX_ATTR] = {};
    u8 fmtMask = 0;
    VertexFormatState fmt[GX_MAX_VTXFMT][GX_VA_MAX_ATTR];
    VertexArrayState arrays[GX_VA_MAX_ATTR];
    uint64_t lastUsedFrame = 0;
};
static PcGfxHostMap<const void*, ResidentMesh> sResidentMeshes;
static bool sMeshDecodeCacheEnabled = true;
static GLuint sMeshArena = 0;
static GLuint sMeshVAO = 0;
static GLint  sStreamVAO = 0;
// Los dibujos de GX en streaming dan por hecho que su VAO y su VBO siguen
// enlazados (ver mesh_arena_init). Las pasadas propias (postproceso, dim, UI)
// vuelven a ellos al terminar en vez de dejar el VAO 0: en pantallas solo 2D
// (seleccion de archivo, videos THP) no hay malla residente que los
// re-enlace, y todo lo siguiente se dibujaba en el VAO 0, es decir, nada.
static void restore_stream_vao() {
    if (glBindVertexArray_ptr) glBindVertexArray_ptr(GLuint(sStreamVAO));
    if (glBindBuffer_ptr) glBindBuffer_ptr(GL_ARRAY_BUFFER, sVBO);
}
static size_t sMeshArenaCapacity = size_t(48) << 20;
static size_t sMeshArenaUsed = 0;
static bool   sMeshArenaReady = false;
static GLsync sMeshArenaResetFence = nullptr;
static PcGfxHostVector<Vertex> sMeshBuild;      // the list being built, triangle-expanded
static PcGfxHostVector<Vertex> sMeshPrimModel;  // model-space copy of the primitive in flight
static uint32_t sMeshDrawsThisFrame = 0;
static uint64_t sMeshVertsThisFrame = 0;
static uint32_t sMeshResets = 0;
static double   sMeshBuildMsThisFrame = 0.0;   // parse-side extra + upload of new meshes
static uint32_t sMeshBuildsThisFrame = 0;
static uint32_t sMeshBuildBytesThisFrame = 0;

// Active texture binding
// A stale vertex array -- one the game set for an earlier model and never
// reprogrammed -- reads plausible indices out of memory that now belongs to
// something else. Recording when each array was last set distinguishes that
// from a genuinely misaligned index, which is the other way to get garbage.
static uint64_t sFrameSerial = 0;
static uint64_t sArraySetFrame[GX_VA_MAX_ATTR] = {};
static uint64_t sArraySetSerial[GX_VA_MAX_ATTR] = {};
static uint64_t sArraySetCounter = 0;

static GLuint sActiveGLTextures[GX_MAX_TEXMAP] = {};
static bool sHasActiveTextures[GX_MAX_TEXMAP] = {};
static bool sUseMaterialRgb = false;
static bool sUseMaterialAlpha = false;
static float sMaterialColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static GXCompare sAlphaComp0 = GX_ALWAYS;
static GXCompare sAlphaComp1 = GX_ALWAYS;
static GXAlphaOp sAlphaOp = GX_AOP_AND;
static float sAlphaRef0 = 0.0f;
static float sAlphaRef1 = 0.0f;
static bool sColorUpdate = true;
static bool sAlphaUpdate = true;
static float sCopyClearColor[4] = { 0.1f, 0.1f, 0.15f, 1.0f };
static float sCopyClearDepth = 1.0f;
static u8 sNumTevStages = 1;

// Per-stage TEV state. Defaults mirror the GX hardware reset state: pass
// rasterized color through to PREV, so nothing renders black before the game
// programs its first material.
struct TevStageState {
	GXTevColorArg colorIn[4] = { GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC };
	GXTevAlphaArg alphaIn[4] = { GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA };
	GXTevOp colorOp = GX_TEV_ADD;
	GXTevBias colorBias = GX_TB_ZERO;
	GXTevScale colorScale = GX_CS_SCALE_1;
	GXBool colorClamp = GX_TRUE;
	GXTevRegID colorOutReg = GX_TEVPREV;
	GXTevOp alphaOp = GX_TEV_ADD;
	GXTevBias alphaBias = GX_TB_ZERO;
	GXTevScale alphaScale = GX_CS_SCALE_1;
	GXBool alphaClamp = GX_TRUE;
	GXTevRegID alphaOutReg = GX_TEVPREV;
	GXTexMapID texMap = GX_TEXMAP_NULL;
	GXTexCoordID texCoord = GX_TEXCOORD_NULL;
	bool textureEnabled = false;
	int rasChannel = 0; // 0/1 select a lit color channel; -1 means COLOR_NULL.
};

// BP TEV combiner register layout. Keep these helpers as the single source of
// truth for display-list decoding; the static assertions guard against the
// operand reversal that previously made the shader compensate for bad state.
static constexpr GXTevColorArg decode_tev_color_a(u32 value) { return GXTevColorArg((value >> 12) & 0xF); }
static constexpr GXTevColorArg decode_tev_color_b(u32 value) { return GXTevColorArg((value >> 8) & 0xF); }
static constexpr GXTevColorArg decode_tev_color_c(u32 value) { return GXTevColorArg((value >> 4) & 0xF); }
static constexpr GXTevColorArg decode_tev_color_d(u32 value) { return GXTevColorArg(value & 0xF); }
static constexpr GXTevAlphaArg decode_tev_alpha_a(u32 value) { return GXTevAlphaArg((value >> 13) & 0x7); }
static constexpr GXTevAlphaArg decode_tev_alpha_b(u32 value) { return GXTevAlphaArg((value >> 10) & 0x7); }
static constexpr GXTevAlphaArg decode_tev_alpha_c(u32 value) { return GXTevAlphaArg((value >> 7) & 0x7); }
static constexpr GXTevAlphaArg decode_tev_alpha_d(u32 value) { return GXTevAlphaArg((value >> 4) & 0x7); }

static_assert(decode_tev_color_a(0x1234) == GXTevColorArg(1));
static_assert(decode_tev_color_b(0x1234) == GXTevColorArg(2));
static_assert(decode_tev_color_c(0x1234) == GXTevColorArg(3));
static_assert(decode_tev_color_d(0x1234) == GXTevColorArg(4));
static_assert(decode_tev_alpha_a(0x29CF) == GXTevAlphaArg(1));
static_assert(decode_tev_alpha_b(0x29CF) == GXTevAlphaArg(2));
static_assert(decode_tev_alpha_c(0x29CF) == GXTevAlphaArg(3));
static_assert(decode_tev_alpha_d(0x29CF) == GXTevAlphaArg(4));

static TevStageState sTevStages[GX_MAXTEVSTAGE];

// Texturas indirectas (GXBump): estado por etapa TEV, etapas indirectas y las
// tres matrices. Se alimenta igual desde la API GX y desde los registros BP de
// las listas de display (IND_MTX, IND_CMD, RAS1_SS, RAS1_IREF, GEN_MODE).
struct TevIndState {
	u8 indStage = 0, fmt = 0, bias = 0, mtx = 0, wrapS = 0, wrapT = 0, addPrev = 0;
};
struct IndStageState {
	u8 texCoord = 0, texMap = 0, scaleS = 0, scaleT = 0;
};
static TevIndState sTevInd[GX_MAXTEVSTAGE];
static IndStageState sIndStages[4];
static u8 sNumIndStages = 0;
static float sIndMtx[3][2][3] = {};
static int sIndMtxScale[3] = { 0, 0, 0 };
static float sTevRegisters[4][4] = {
	{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}
};
static float sKonstColors[4][4] = {
	{1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}, {1, 1, 1, 1}
};
static GXTevKColorSel sKonstColorSel[GX_MAXTEVSTAGE] = {};
static GXTevKAlphaSel sKonstAlphaSel[GX_MAXTEVSTAGE] = {};

static float tev_konst_fraction(int selector) {
    static const float fractions[8] = { 1.0f, 7.0f / 8.0f, 3.0f / 4.0f, 5.0f / 8.0f,
                                        1.0f / 2.0f, 3.0f / 8.0f, 1.0f / 4.0f, 1.0f / 8.0f };
    return selector >= 0 && selector < 8 ? fractions[selector] : 1.0f;
}

static void resolve_tev_konst(u8 stage, float out[4]) {
    const int colorSel = static_cast<int>(sKonstColorSel[stage]);
    if (colorSel < 8) {
        out[0] = out[1] = out[2] = tev_konst_fraction(colorSel);
    } else if (colorSel >= 12 && colorSel < 16) {
        const float* color = sKonstColors[colorSel - 12];
        out[0] = color[0]; out[1] = color[1]; out[2] = color[2];
    } else if (colorSel >= 16 && colorSel < 32) {
        const int component = (colorSel - 16) >> 2;
        const int colorIndex = (colorSel - 16) & 3;
        const float value = sKonstColors[colorIndex][component];
        out[0] = out[1] = out[2] = value;
    } else {
        out[0] = out[1] = out[2] = 1.0f;
    }

    const int alphaSel = static_cast<int>(sKonstAlphaSel[stage]);
    if (alphaSel < 8) {
        out[3] = tev_konst_fraction(alphaSel);
    } else if (alphaSel >= 16 && alphaSel < 32) {
        const int component = (alphaSel - 16) >> 2;
        const int colorIndex = (alphaSel - 16) & 3;
        out[3] = sKonstColors[colorIndex][component];
    } else {
        out[3] = 1.0f;
    }
}

// Map of GXTexObj pointers to OpenGL Texture IDs
// Modo de repetición de GX en OpenGL (de Open Nectar): GX_MIRROR se trataba
// como CLAMP.
#ifndef GL_MIRRORED_REPEAT
#define GL_MIRRORED_REPEAT 0x8370
#endif
static GLint gxWrapToGl(GXTexWrapMode wrap) {
    switch (wrap) {
    case GX_REPEAT: return GL_REPEAT;
    case GX_MIRROR: return GL_MIRRORED_REPEAT;
    default: return GL_CLAMP_TO_EDGE;
    }
}
static PcGfxHostMap<uintptr_t, GLuint> sTextureCache;
static std::mutex sTextureRegistryMutex;

// GXInitTexObjLOD filters, keyed like sTextureCache. `appliedTo` is the GL
// texture the filter was last written to, so re-uploads pick it up again.
struct TexObjFilter {
    GXTexFilter minFilt = GX_LINEAR;
    GXTexFilter magFilt = GX_LINEAR;
    GLuint appliedTo = 0;
};
static PcGfxHostMap<uintptr_t, TexObjFilter> sTexObjFilters;

static GLenum gx_filter_to_gl(GXTexFilter f, bool hasMips) {
    switch (f) {
    case GX_NEAR:          return GL_NEAREST;
    case GX_LINEAR:        return GL_LINEAR;
    case GX_NEAR_MIP_NEAR: return hasMips ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
    case GX_LIN_MIP_NEAR:  return hasMips ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR;
    case GX_NEAR_MIP_LIN:  return hasMips ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST;
    case GX_LIN_MIP_LIN:   return hasMips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
    default:               return GL_LINEAR;
    }
}

static void apply_tex_obj_filter(uintptr_t key, GLuint tex) {
    std::lock_guard<std::mutex> lock(sTextureRegistryMutex);
    auto f = sTexObjFilters.find(key);
    if (f == sTexObjFilters.end() || f->second.appliedTo == tex) return;
    GLint prev = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLint maxLevel = 0;
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, &maxLevel);
    // Magnification never uses mipmaps; GX only allows NEAR/LINEAR there.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gx_filter_to_gl(f->second.minFilt, maxLevel > 0));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f->second.magFilt == GX_NEAR ? GL_NEAREST : GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prev));
    f->second.appliedTo = tex;
}

void pc_gfx_set_tex_obj_filter(GXTexObj* obj, GXTexFilter minFilt, GXTexFilter magFilt) {
    if (!obj) return;
    std::lock_guard<std::mutex> lock(sTextureRegistryMutex);
    TexObjFilter& f = sTexObjFilters[(uintptr_t)obj];
    if (f.minFilt != minFilt || f.magFilt != magFilt) f.appliedTo = 0;
    f.minFilt = minFilt;
    f.magFilt = magFilt;
}
// What the cache is costing. Every GX texture is expanded to RGBA8 -- the
// console's formats are 4 and 8 bits per pixel, so this is four to eight times
// the original -- and nothing ever gave one back until now.
static PcGfxHostMap<uintptr_t, size_t> sTextureBytes;
static size_t sTextureBytesLive = 0;
static size_t sTextureBytesPeak = 0;
static size_t sTexturesCreated = 0;
static size_t sTexturesReleased = 0;

struct PcTextureSignature {
    const void* image = nullptr;
    u16 width = 0;
    u16 height = 0;
    u32 format = 0;
    GXTexWrapMode wrapS = GX_CLAMP;
    GXTexWrapMode wrapT = GX_CLAMP;
    bool indexed = false;
    u32 tlutName = 0;

    bool operator==(const PcTextureSignature& other) const {
        return image == other.image && width == other.width && height == other.height
            && format == other.format && wrapS == other.wrapS && wrapT == other.wrapT
            && indexed == other.indexed && tlutName == other.tlutName;
    }
};
static PcGfxHostMap<uintptr_t, PcTextureSignature> sTextureSignatures;

struct PcTlut {
    PcGfxHostVector<u8> rgba;
    // Bytes GX crudos (big-endian), conservados sólo con el volcado de nombres
    // activo: el hash de paleta de Dolphin se calcula sobre ellos.
    PcGfxHostVector<u8> raw;
};
// CPU-owned description of a GX texture. GXInitTexObj is allowed to run on a
// loader thread, so this record deliberately owns the encoded bytes instead of
// retaining a pointer into a section heap. OpenGL materialisation is moved to
// GXLoadTexObj in the following step.
struct PcTextureSource {
    PcGfxHostVector<u8> image;
    u16 width = 0;
    u16 height = 0;
    u32 format = GX_TF_I4;
    GXTexWrapMode wrapS = GX_CLAMP;
    GXTexWrapMode wrapT = GX_CLAMP;
    bool indexed = false;
    u32 tlutName = 0;
    bool mipmap = false;
    u64 generation = 0;
    u64 uploadedGeneration = 0;
    uintptr_t srcAddr = 0; // imagen original: para encontrar copias de GXCopyTex
};
// GXCopyTex: direccion de destino -> textura GL con la copia del framebuffer.
struct PcEfbCopy { GLuint tex = 0; int w = 0, h = 0; };
static PcGfxHostMap<uintptr_t, PcEfbCopy> sEfbCopies;
static PcGfxHostMap<uintptr_t, PcTlut> sTlutObjects;
static PcGfxHostMap<u32, PcTlut> sLoadedTluts;
static PcGfxHostMap<uintptr_t, PcTextureSource> sTextureSources;
// Texturas con reemplazo HD del pack (PLAN_TEXTURAS_HD fase 1): texId -> si
// trae cadena de mips propia. Sobre estas hay que saltarse glGenerateMipmap:
// sobre un bloque comprimido es GL_INVALID_OPERATION, y además la cadena ya
// viene con el pack.
static PcGfxHostMap<GLuint, bool> sExternalMipChain;

// ── Volcado de nombres de textura (--dump-texture-names, PLAN_TEXTURAS_HD fase 0) ──
// Reproduce TextureInfo::CalculateTextureName de Dolphin para poder cotejar las
// texturas del juego con packs tex1_* existentes antes de escribir el cargador.
static bool sDumpTextureNames = false;
static FILE* sTextureNamesLog = nullptr;
static PcGfxHostSet<std::string> sDumpedTextureNames;

void pc_gfx_set_dump_texture_names(int enabled)
{
    sDumpTextureNames = enabled != 0;
    if (sDumpTextureNames && !sTextureNamesLog) {
        sTextureNamesLog = fopen("texture_names.log", "w");
        if (!sTextureNamesLog) {
            printf("[PC Port] --dump-texture-names: no se pudo crear texture_names.log\n");
            sDumpTextureNames = false;
        } else {
            printf("[PC Port] --dump-texture-names: escribiendo texture_names.log\n");
        }
    }
}

// Tamaño del nivel base en bytes GX crudos, con padding al tile completo. Es lo
// que hashea Dolphin (TexDecoder_GetTextureSizeInBytes sobre ancho/alto
// alineados al bloque): nunca la cadena de mipmaps.
static size_t gx_base_level_size(u16 width, u16 height, u32 format)
{
    int tileWidth = 0, tileHeight = 0, tileBytes = 0;
    switch (format) {
        case GX_TF_I4: case GX_TF_CMPR: case GX_TF_C4:
            tileWidth = 8; tileHeight = 8; tileBytes = 32; break;
        case GX_TF_I8: case GX_TF_IA4: case GX_TF_C8: case GX_TF_Z8:
            tileWidth = 8; tileHeight = 4; tileBytes = 32; break;
        case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3:
        case GX_TF_C14X2: case GX_TF_Z16:
            tileWidth = 4; tileHeight = 4; tileBytes = 32; break;
        case GX_TF_RGBA8: case GX_TF_Z24X8:
            tileWidth = 4; tileHeight = 4; tileBytes = 64; break;
        default: return 0;
    }
    const size_t tilesX = (width + tileWidth - 1) / tileWidth;
    const size_t tilesY = (height + tileHeight - 1) / tileHeight;
    return tilesX * tilesY * size_t(tileBytes);
}

// Nombre tex1_* de Dolphin para una textura, con los hashes de nivel base y
// TLUT. Devuelve false si no se puede nombrar (formato desconocido, o textura
// indexada sin paleta cruda). *outBytes devuelve el tamaño hasheado (el del
// nivel base con padding), útil para el volcado.
static bool compute_dolphin_name(const u8* image, u16 width, u16 height, u32 format,
                                 bool hasMipmaps, const u8* tlutRaw, size_t tlutEntries,
                                 char* out, size_t outSize, size_t* outBytes)
{
    if (!image) return false;
    const size_t textureSize = gx_base_level_size(width, height, format);
    if (textureSize == 0) return false;

    const bool indexed = format == GX_TF_C4 || format == GX_TF_C8 || format == GX_TF_C14X2;
    // Sin la paleta cruda el nombre quedaría sin hash de TLUT y no casaría
    // con nada: mejor no pedir el nombre.
    if (indexed && (!tlutRaw || tlutEntries == 0)) return false;

    bool hasTlutHash = false;
    u64 tlutHash = 0;
    if (indexed) {
        // Dolphin recorta la paleta al rango de índices usado en el nivel base
        // (padding incluido) y hashea sólo ese tramo. El caso C14X2 reproduce
        // su lectura tal cual: swap16 de un byte promovido, así que sólo mira
        // el primer byte de cada par, (byte & 0x3F) << 8.
        u32 minIndex = 0xFFFF, maxIndex = 0;
        if (format == GX_TF_C4) {
            for (size_t i = 0; i < textureSize; ++i) {
                const u32 low = image[i] & 0xF;
                const u32 high = image[i] >> 4;
                minIndex = std::min({minIndex, low, high});
                maxIndex = std::max({maxIndex, low, high});
            }
        } else if (format == GX_TF_C8) {
            for (size_t i = 0; i < textureSize; ++i) {
                minIndex = std::min(minIndex, u32(image[i]));
                maxIndex = std::max(maxIndex, u32(image[i]));
            }
        } else {
            for (size_t i = 0; i + 1 < textureSize; i += 2) {
                const u32 index = u32(image[i] & 0x3F) << 8;
                minIndex = std::min(minIndex, index);
                maxIndex = std::max(maxIndex, index);
            }
        }
        if (minIndex <= maxIndex && minIndex < tlutEntries) {
            if (maxIndex >= tlutEntries) maxIndex = u32(tlutEntries) - 1;
            tlutHash = XXH64(tlutRaw + size_t(minIndex) * 2, (maxIndex + 1 - minIndex) * 2, 0);
            hasTlutHash = true;
        }
    }

    const u64 textureHash = XXH64(image, textureSize, 0);
    if (hasTlutHash) {
        snprintf(out, outSize, "tex1_%ux%u%s_%016llx_%016llx_%u",
                 unsigned(width), unsigned(height), hasMipmaps ? "_m" : "",
                 (unsigned long long)textureHash, (unsigned long long)tlutHash, format);
    } else {
        snprintf(out, outSize, "tex1_%ux%u%s_%016llx_%u",
                 unsigned(width), unsigned(height), hasMipmaps ? "_m" : "",
                 (unsigned long long)textureHash, format);
    }
    if (outBytes) *outBytes = textureSize;
    return true;
}

static void dump_dolphin_texture_name(const u8* image, u16 width, u16 height, u32 format,
                                      bool hasMipmaps, const u8* tlutRaw, size_t tlutEntries)
{
    if (!sDumpTextureNames || !sTextureNamesLog) return;
    char name[96];
    size_t textureSize = 0;
    if (!compute_dolphin_name(image, width, height, format, hasMipmaps, tlutRaw, tlutEntries,
                              name, sizeof(name), &textureSize)) {
        return;
    }
    // The set's nodes are host memory but each std::string buffer uses the
    // default allocator; names are longer than the SSO buffer.
    PcGfxHostAllocScope hostAllocScope;
    if (sDumpedTextureNames.insert(name).second) {
        fprintf(sTextureNamesLog, "%s\t%u\t%u\t%u\t%zu\n",
                name, unsigned(width), unsigned(height), format, textureSize);
        fflush(sTextureNamesLog);
    }
}
static int sDrawableWidth = 640;
static int sDrawableHeight = 480;
static bool sUi43 = false;
static bool sHudWide = false;

// Pantalla partida: sub-rectángulo (normalizado, origen abajo-izquierda como
// GL) del destino sobre el que se mapea el espacio GX 640x480. Con él, el
// HUD de un jugador se dibuja "a pantalla completa" dentro de su mitad.
static bool  sViewSubrectOn = false;
static float sViewSubX0 = 0.0f, sViewSubY0 = 0.0f, sViewSubX1 = 1.0f, sViewSubY1 = 1.0f;
float pc_gfx_get_current_aspect_ratio(void);

// Tamaño virtual del HUD forzado (pantalla partida): 0 = automático
// (480*aspecto x 480). Con él, una vista más estrecha que 640 usa 640 x
// (640/aspecto) y el HUD se dibuja reducido y anclado abajo.
static int sHudVirtWOverride = 0, sHudVirtHOverride = 0;

static int hud_virtual_height() {
    return sHudVirtHOverride > 0 ? sHudVirtHOverride : 480;
}

static int hud_virtual_width() {
    if (sHudVirtWOverride > 0) return sHudVirtWOverride;
    const int v = int(lroundf(480.0f * pc_gfx_get_current_aspect_ratio()));
    return v < 640 ? 640 : v;
}

int pc_gfx_get_hud_virtual_height(void) { return hud_virtual_height(); }

void pc_gfx_set_hud_virtual_size(int w, int h) {
    sHudVirtWOverride = w > 0 ? w : 0;
    sHudVirtHOverride = h > 0 ? h : 0;
    invalidate_gl_pipeline_guards();
}

// GX 640x480 → GL. World/HUD stretch X to the window aspect. Menu UI 4:3 uses
// a uniform scale and centres the 640x480 rect (pillarbox). Viewport and
// scissor both call this so they cannot drift.
static void gx_rect_params_inner(float targetWidth, float targetHeight,
                                 float& scaleX, float& scaleY, float& offsetX, float& offsetY);

// Espacio de coordenadas GX (el EFB del modo de video). Pikmin 1 usa 640x480;
// Pikmin 2 PAL/NTSC dibuja en 608x448 y la salida de video de la consola lo
// estiraba a toda la pantalla. Con 640x480 fijos quedaban barras negras a la
// derecha y abajo (608/640, 448/480 del area).
static float sGxW = 640.0f, sGxH = 480.0f;
void pc_gfx_set_gx_space(int width, int height) {
    if (width > 0 && height > 0) {
        sGxW = float(width);
        sGxH = float(height);
    }
}

static void gx_rect_params(float& scaleX, float& scaleY, float& offsetX, float& offsetY) {
    const float fullWidth = sNativeFramebufferReady ? float(sRenderWidth) : float(sDrawableWidth);
    const float fullHeight = sNativeFramebufferReady ? float(sRenderHeight) : float(sDrawableHeight);
    if (!sViewSubrectOn) {
        gx_rect_params_inner(fullWidth, fullHeight, scaleX, scaleY, offsetX, offsetY);
        return;
    }
    const float subW = fullWidth * (sViewSubX1 - sViewSubX0);
    const float subH = fullHeight * (sViewSubY1 - sViewSubY0);
    gx_rect_params_inner(subW, subH, scaleX, scaleY, offsetX, offsetY);
    offsetX += fullWidth * sViewSubX0;
    offsetY += fullHeight * sViewSubY0;
}

static void gx_rect_params_inner(float targetWidth, float targetHeight,
                                 float& scaleX, float& scaleY, float& offsetX, float& offsetY) {
    if (sUi43) {
        // Caja 4:3 centrada; el EFB (sGxW x sGxH) la llena entera, como en la tele.
        const float scale = fminf(targetWidth / 640.0f, targetHeight / 480.0f);
        scaleX = 640.0f * scale / sGxW;
        scaleY = 480.0f * scale / sGxH;
        offsetX = (targetWidth - 640.0f * scale) * 0.5f;
        offsetY = (targetHeight - 480.0f * scale) * 0.5f;
        return;
    }
    if (sHudWide) {
        const float virtW = float(hud_virtual_width());
        const float virtH = float(hud_virtual_height());
        const float scale = fminf(targetWidth / virtW, targetHeight / virtH);
        scaleX = scale;
        scaleY = scale;
        offsetX = (targetWidth - virtW * scale) * 0.5f;
        offsetY = (targetHeight - virtH * scale) * 0.5f;
        return;
    }
    const int baseWidth = int(lroundf(480.0f * sCurrentAspectRatio));
    const int baseHeight = 480;
    const float scale = fminf(targetWidth / float(baseWidth), targetHeight / float(baseHeight));
    offsetX = (targetWidth - float(baseWidth) * scale) * 0.5f;
    offsetY = (targetHeight - float(baseHeight) * scale) * 0.5f;
    scaleX = (float(baseWidth) / sGxW) * scale;
    scaleY = (float(baseHeight) / sGxH) * scale;
}

static void calculate_output_area(int drawableWidth, int drawableHeight, float aspectRatio, GLint& outX, GLint& outY, GLint& outWidth, GLint& outHeight);
// ── Toques sobre menús 2D ────────────────────────────────────────────────────
// Cada pantalla de menú dibuja con una proyección distinta (640 estirado,
// 4:3 centrado, ancho virtual). La pantalla anota aquí su espacio al
// dibujar y la capa táctil invierte el toque (normalizado sobre la ventana)
// a coordenadas de ese espacio, las mismas de getGlobalBounds() de sus paneles.
static float sTapScaleX = 1.0f, sTapScaleY = 1.0f, sTapOffsetX = 0.0f, sTapOffsetY = 0.0f;
static float sTapTargetW = 640.0f, sTapTargetH = 480.0f;
static int sTapGraphW = 640, sTapGraphH = 480;
static bool sTapSpaceValid = false;

void pc_gfx_note_menu_tap_space(int graphWidth, int graphHeight) {
    gx_rect_params(sTapScaleX, sTapScaleY, sTapOffsetX, sTapOffsetY);
    sTapTargetW = sNativeFramebufferReady ? float(sRenderWidth) : float(sDrawableWidth);
    sTapTargetH = sNativeFramebufferReady ? float(sRenderHeight) : float(sDrawableHeight);
    sTapGraphW = graphWidth > 0 ? graphWidth : 640;
    sTapGraphH = graphHeight > 0 ? graphHeight : 480;
    sTapSpaceValid = sTapScaleX > 0.0f && sTapScaleY > 0.0f;
}

bool pc_gfx_menu_tap_to_graph(float nx, float ny, float* x, float* y) {
    if (!sTapSpaceValid) return false;
    // El render target ocupa el área de salida de la ventana (blit del
    // present); el toque normalizado sobre la ventana se lleva primero ahí.
    GLint outX, outY, outW, outH;
    calculate_output_area(sDrawableWidth, sDrawableHeight, sCurrentAspectRatio, outX, outY, outW, outH);
    if (outW <= 0 || outH <= 0) return false;
    const float winX = nx * float(sDrawableWidth);
    const float winY = ny * float(sDrawableHeight);
    const float rtX = (winX - outX) / float(outW) * sTapTargetW;
    const float rtYUp = (1.0f - (winY - outY) / float(outH)) * sTapTargetH; // GL: origen abajo
    const float gxX = (rtX - sTapOffsetX) / sTapScaleX;
    const float gxY = 480.0f - (rtYUp - sTapOffsetY) / sTapScaleY;            // GX: origen arriba
    if (x) *x = gxX * float(sTapGraphW) / 640.0f;
    if (y) *y = gxY * float(sTapGraphH) / 480.0f;
    return true;
}

static void map_gx_rect(float x, float y, float width, float height,
                        GLint& glX, GLint& glY, GLsizei& glWidth, GLsizei& glHeight) {
    float scaleX, scaleY, offsetX, offsetY;
    gx_rect_params(scaleX, scaleY, offsetX, offsetY);
    glX = (GLint)lroundf(offsetX + x * scaleX);
    const float virtH = sHudWide ? float(hud_virtual_height()) : sGxH;
    glY = (GLint)lroundf(offsetY + (virtH - y - height) * scaleY);
    glWidth = (GLsizei)std::max(0L, lroundf(width * scaleX));
    glHeight = (GLsizei)std::max(0L, lroundf(height * scaleY));
}

static void fill_ui_43_bars() {
    const float targetWidth = sNativeFramebufferReady ? float(sRenderWidth) : float(sDrawableWidth);
    const float targetHeight = sNativeFramebufferReady ? float(sRenderHeight) : float(sDrawableHeight);
    GLint ix, iy;
    GLsizei iw, ih;
    map_gx_rect(0.0f, 0.0f, sGxW, sGxH, ix, iy, iw, ih);
    const GLint tw = (GLint)lroundf(targetWidth);
    const GLint th = (GLint)lroundf(targetHeight);
    const GLint leftW = ix;
    const GLint rightX = ix + iw;
    const GLint rightW = tw - rightX;
    const GLint botH = iy;
    const GLint topY = iy + ih;
    const GLint topH = th - topY;
    if (leftW <= 0 && rightW <= 0 && botH <= 0 && topH <= 0) {
        return;
    }
    pc_gfx_note_gl_state_change();
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    if (leftW > 0) {
        glScissor(0, 0, leftW, th);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (rightW > 0) {
        glScissor(rightX, 0, rightW, th);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (botH > 0) {
        glScissor(0, 0, tw, botH);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    if (topH > 0) {
        glScissor(0, topY, tw, topH);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glClearColor(sCopyClearColor[0], sCopyClearColor[1], sCopyClearColor[2], sCopyClearColor[3]);
    invalidate_gl_pipeline_guards();
}

void pc_gfx_set_ui_43(int enabled) {
    const bool want = enabled != 0;
    if (want != sUi43) {
        sUi43 = want;
        invalidate_gl_pipeline_guards();
    }
    if (want) {
        fill_ui_43_bars();
    }
}

void pc_gfx_set_ui_43_no_bars(int enabled) {
    const bool want = enabled != 0;
    sUi43 = want;
    invalidate_gl_pipeline_guards();
}

static void gl_program_cache_invalidate();

static GLuint sDimProgram = 0;
static GLuint sDimVAO = 0;
static GLint sDimColorLoc = -1;
static bool sDimWindowAfterBlit = false;
static unsigned char sDimWindowAlpha = 160;

static bool ensure_dim_program()
{
    if (sDimProgram && sDimVAO) return true;
    if (!glCreateShader_ptr || !glCreateProgram_ptr || !glGenVertexArrays_ptr) return false;

    static const char* kFrag =
#if PIKI_USE_GLES
        "#version 300 es\n"
        "precision highp float;\n"
#else
        "#version 330 core\n"
#endif
        "uniform vec4 uColor;\n"
        "out vec4 oColour;\n"
        "void main() { oColour = uColor; }\n";

    GLuint vs = glCreateShader_ptr(GL_VERTEX_SHADER);
    const char* vert = pc_post_vertex_shader();
    glShaderSource_ptr(vs, 1, &vert, nullptr);
    glCompileShader_ptr(vs);
    GLuint fs = glCreateShader_ptr(GL_FRAGMENT_SHADER);
    glShaderSource_ptr(fs, 1, &kFrag, nullptr);
    glCompileShader_ptr(fs);
    sDimProgram = glCreateProgram_ptr();
    glAttachShader_ptr(sDimProgram, vs);
    glAttachShader_ptr(sDimProgram, fs);
    glLinkProgram_ptr(sDimProgram);
    glDeleteShader_ptr(vs);
    glDeleteShader_ptr(fs);
    GLint ok = 0;
    if (glGetProgramiv_ptr) glGetProgramiv_ptr(sDimProgram, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        glDeleteProgram_ptr(sDimProgram);
        sDimProgram = 0;
        return false;
    }
    sDimColorLoc = glGetUniformLocation_ptr(sDimProgram, "uColor");
    glGenVertexArrays_ptr(1, &sDimVAO);
    return sDimVAO != 0;
}

static void dim_draw(unsigned char alpha)
{
    if (!ensure_dim_program() || !glUseProgram_ptr || !glBindVertexArray_ptr) return;
    invalidate_uniform_cache();
    glUseProgram_ptr(sDimProgram);
    glUniform4f_ptr(sDimColorLoc, 0.0f, 0.0f, 0.0f, float(alpha) / 255.0f);
    glBindVertexArray_ptr(sDimVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    restore_stream_vao();
    glUseProgram_ptr(0);
    // Same trap as post_apply(): GX thinks the TEV program is still bound
    // and skips glUseProgram. P2D plates then draw with program 0 and show
    // up as an opaque black rectangle over the field.
    for (int i = 0; i < 8; i++) sBoundTextures[i] = 0;
    gl_program_cache_invalidate();
    invalidate_gl_pipeline_guards();
}

// ── Capa de sprites (interfaz táctil) ───────────────────────────────────────
static GLuint sOverlayProgram = 0;
static GLuint sOverlayVAO = 0;
static GLint sOverlayRectLoc = -1, sOverlayColorLoc = -1, sOverlayRotLoc = -1, sOverlayTexLoc = -1;

static bool ensure_overlay_program()
{
    if (sOverlayProgram && sOverlayVAO) return true;
    if (!glCreateShader_ptr || !glCreateProgram_ptr || !glGenVertexArrays_ptr) return false;
    static const char* kVert =
#if PIKI_USE_GLES
        "#version 300 es\n"
        "precision highp float;\n"
#else
        "#version 330 core\n"
#endif
        // uRect: centro (x,y) y medio tamaño (w,h) en NDC; uRot: cos, sin.
        "uniform vec4 uRect;\n"
        "uniform vec3 uRot;\n" // cos, sin, aspecto (ancho/alto) para girar en píxeles
        "out vec2 vUV;\n"
        "void main() {\n"
        "  vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
        "  vUV = vec2(corner.x, 1.0 - corner.y);\n"
        "  vec2 local = (corner * 2.0 - 1.0) * uRect.zw;\n"
        "  vec2 p = vec2(local.x * uRot.z, local.y);\n"
        "  vec2 q = vec2(p.x * uRot.x - p.y * uRot.y, p.x * uRot.y + p.y * uRot.x);\n"
        "  gl_Position = vec4(uRect.xy + vec2(q.x / uRot.z, q.y), 0.0, 1.0);\n"
        "}\n";
    static const char* kFrag =
#if PIKI_USE_GLES
        "#version 300 es\n"
        "precision highp float;\n"
#else
        "#version 330 core\n"
#endif
        "uniform sampler2D uTex;\n"
        "uniform vec4 uColor;\n"
        "in vec2 vUV;\n"
        "out vec4 oColour;\n"
        "void main() { oColour = texture(uTex, vUV) * uColor; }\n";
    GLuint vs = glCreateShader_ptr(GL_VERTEX_SHADER);
    glShaderSource_ptr(vs, 1, &kVert, nullptr);
    glCompileShader_ptr(vs);
    GLuint fs = glCreateShader_ptr(GL_FRAGMENT_SHADER);
    glShaderSource_ptr(fs, 1, &kFrag, nullptr);
    glCompileShader_ptr(fs);
    sOverlayProgram = glCreateProgram_ptr();
    glAttachShader_ptr(sOverlayProgram, vs);
    glAttachShader_ptr(sOverlayProgram, fs);
    glLinkProgram_ptr(sOverlayProgram);
    glDeleteShader_ptr(vs);
    glDeleteShader_ptr(fs);
    GLint ok = 0;
    if (glGetProgramiv_ptr) glGetProgramiv_ptr(sOverlayProgram, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024] = { 0 };
        if (glGetProgramInfoLog_ptr) glGetProgramInfoLog_ptr(sOverlayProgram, sizeof log, nullptr, log);
        printf("[PC Port] overlay program failed to link:\n%s\n", log);
        glDeleteProgram_ptr(sOverlayProgram);
        sOverlayProgram = 0;
        return false;
    }
    sOverlayRectLoc = glGetUniformLocation_ptr(sOverlayProgram, "uRect");
    sOverlayRotLoc = glGetUniformLocation_ptr(sOverlayProgram, "uRot");
    sOverlayColorLoc = glGetUniformLocation_ptr(sOverlayProgram, "uColor");
    sOverlayTexLoc = glGetUniformLocation_ptr(sOverlayProgram, "uTex");
    glGenVertexArrays_ptr(1, &sOverlayVAO);
    return sOverlayVAO != 0;
}

unsigned pc_gfx_overlay_texture_create(int width, int height, const unsigned char* rgba)
{
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (glGenerateMipmap_ptr) glGenerateMipmap_ptr(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
    sBoundTextures[0] = 0;
    return tex;
}

void pc_gfx_overlay_texture_destroy(unsigned texture)
{
    GLuint tex = texture;
    if (tex) glDeleteTextures(1, &tex);
}

void pc_gfx_get_drawable_size(int* width, int* height)
{
    if (width) *width = sDrawableWidth;
    if (height) *height = sDrawableHeight;
}

void pc_gfx_overlay_begin(void)
{
    if (!ensure_overlay_program() || sDrawableWidth <= 0 || sDrawableHeight <= 0) return;
    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    invalidate_uniform_cache();
    if (glBindFramebuffer_ptr) glBindFramebuffer_ptr(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    // Las texturas de la capa llevan alfa recto; el color ya viene sin
    // premultiplicar.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, sDrawableWidth, sDrawableHeight);
    glUseProgram_ptr(sOverlayProgram);
    glBindVertexArray_ptr(sOverlayVAO);
    glActiveTexture_ptr(GL_TEXTURE0);
    glUniform1i_ptr(sOverlayTexLoc, 0);
}

void pc_gfx_overlay_sprite(unsigned texture, float x, float y, float w, float h,
                           float r, float g, float b, float a, float angleRadians)
{
    if (!sOverlayProgram || sDrawableWidth <= 0 || sDrawableHeight <= 0) return;
    const float cx = (x + w * 0.5f) / float(sDrawableWidth) * 2.0f - 1.0f;
    const float cy = 1.0f - (y + h * 0.5f) / float(sDrawableHeight) * 2.0f;
    const float hw = w / float(sDrawableWidth);
    const float hh = h / float(sDrawableHeight);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform4f_ptr(sOverlayRectLoc, cx, cy, hw, hh);
    // El giro se aplica en NDC, que no es isótropo: el shader corrige con el aspecto.
    glUniform3f_ptr(sOverlayRotLoc, cosf(angleRadians), sinf(angleRadians),
                    float(sDrawableWidth) / float(sDrawableHeight));
    glUniform4f_ptr(sOverlayColorLoc, r, g, b, a);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void pc_gfx_overlay_end(void)
{
    if (!sOverlayProgram) return;
    glBindTexture(GL_TEXTURE_2D, 0);
    restore_stream_vao();
    glUseProgram_ptr(0);
    for (int i = 0; i < 8; i++) sBoundTextures[i] = 0;
    gl_program_cache_invalidate();
    invalidate_uniform_cache();
    invalidate_gl_pipeline_guards();
    if (sNativeFramebufferReady && glBindFramebuffer_ptr) glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    glEnable(GL_SCISSOR_TEST);
}

void pc_gfx_dim_full_target(unsigned char alpha)
{
    const GLint w = sNativeFramebufferReady ? sRenderWidth : sDrawableWidth;
    const GLint h = sNativeFramebufferReady ? sRenderHeight : sDrawableHeight;
    if (w <= 0 || h <= 0 || alpha == 0) return;

    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    if (sNativeFramebufferReady && glBindFramebuffer_ptr) {
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    }
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, w, h);
    dim_draw(alpha);
    glEnable(GL_SCISSOR_TEST);
    sDimWindowAfterBlit = true;
    sDimWindowAlpha = alpha;
}

// ── Lote de UI del host (menu F1 de Pikmin 2) ───────────────────────────────
// En Pikmin 2 las llamadas GX hechas despues del frame del juego no llegan a
// pintarse, asi que el menu se dibuja aqui con GL directo, igual que el dim.
static GLuint sUiProgram = 0, sUiVAO = 0, sUiVBO = 0, sUiFontTex = 0;
static const unsigned char* sUiFontSrc = nullptr;

static bool ensure_ui_program()
{
    if (sUiProgram && sUiVAO && sUiVBO) return true;
    if (!glCreateShader_ptr || !glCreateProgram_ptr || !glGenVertexArrays_ptr || !glGenBuffers_ptr) return false;
    static const char* kVert =
#if PIKI_USE_GLES
        "#version 300 es\nprecision highp float;\n"
#else
        "#version 330 core\n"
#endif
        "layout(location=0) in vec2 aPos;\n"
        "layout(location=1) in vec2 aUV;\n"
        "layout(location=2) in vec4 aCol;\n"
        "layout(location=3) in float aTex;\n"
        "out vec2 vUV; out vec4 vCol; out float vTex;\n"
        "void main() { vUV = aUV; vCol = aCol; vTex = aTex; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    static const char* kFrag =
#if PIKI_USE_GLES
        "#version 300 es\nprecision highp float;\n"
#else
        "#version 330 core\n"
#endif
        "uniform sampler2D uTex;\n"
        "in vec2 vUV; in vec4 vCol; in float vTex;\n"
        "out vec4 oColour;\n"
        "uniform sampler2D uAtlas;\n"
        "void main() {\n"
        "  if (vTex > 1.5) { oColour = texture(uAtlas, vUV) * vCol; return; }\n"
        "  float t = texture(uTex, vUV).r; oColour = vec4(vCol.rgb, vCol.a * mix(1.0, t, vTex));\n"
        "}\n";
    GLuint vs = glCreateShader_ptr(GL_VERTEX_SHADER);
    glShaderSource_ptr(vs, 1, &kVert, nullptr);
    glCompileShader_ptr(vs);
    GLuint fs = glCreateShader_ptr(GL_FRAGMENT_SHADER);
    glShaderSource_ptr(fs, 1, &kFrag, nullptr);
    glCompileShader_ptr(fs);
    sUiProgram = glCreateProgram_ptr();
    glAttachShader_ptr(sUiProgram, vs);
    glAttachShader_ptr(sUiProgram, fs);
    glLinkProgram_ptr(sUiProgram);
    glDeleteShader_ptr(vs);
    glDeleteShader_ptr(fs);
    GLint ok = 0;
    if (glGetProgramiv_ptr) glGetProgramiv_ptr(sUiProgram, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {0};
        if (glGetProgramInfoLog_ptr) glGetProgramInfoLog_ptr(sUiProgram, sizeof log, nullptr, log);
        printf("[PC GX] UI overlay program failed: %s\n", log);
        glDeleteProgram_ptr(sUiProgram);
        sUiProgram = 0;
        return false;
    }
    glGenVertexArrays_ptr(1, &sUiVAO);
    glGenBuffers_ptr(1, &sUiVBO);
    glBindVertexArray_ptr(sUiVAO);
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sUiVBO);
    const GLsizei stride = sizeof(PcUiVertex);
    glEnableVertexAttribArray_ptr(0);
    glVertexAttribPointer_ptr(0, 2, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(PcUiVertex, x));
    glEnableVertexAttribArray_ptr(1);
    glVertexAttribPointer_ptr(1, 2, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(PcUiVertex, u));
    glEnableVertexAttribArray_ptr(2);
    glVertexAttribPointer_ptr(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void*)offsetof(PcUiVertex, r));
    glEnableVertexAttribArray_ptr(3);
    glVertexAttribPointer_ptr(3, 1, GL_FLOAT, GL_FALSE, stride, (const void*)offsetof(PcUiVertex, tex));
    restore_stream_vao();
    return true;
}

static const unsigned char* sUiAtlasPixels = nullptr;
static int sUiAtlasW = 0, sUiAtlasH = 0;
static unsigned sUiAtlasGen = 0, sUiAtlasUploadedGen = ~0u;
static GLuint sUiAtlasTex = 0;

void pc_gfx_ui_set_atlas(const unsigned char* rgba, int w, int h, unsigned generation)
{
    sUiAtlasPixels = rgba;
    sUiAtlasW      = w;
    sUiAtlasH      = h;
    sUiAtlasGen    = generation;
}

void pc_gfx_ui_draw(const PcUiVertex* verts, int count, int virtW, int virtH,
                    const unsigned char* fontI8, int fontW, int fontH)
{
    if (!verts || count <= 0 || virtW <= 0 || virtH <= 0) return;
    const GLint w = sNativeFramebufferReady ? sRenderWidth : sDrawableWidth;
    const GLint h = sNativeFramebufferReady ? sRenderHeight : sDrawableHeight;
    if (w <= 0 || h <= 0 || !ensure_ui_program()) return;

    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    if (sNativeFramebufferReady && glBindFramebuffer_ptr) {
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    }
    if (fontI8 && (sUiFontSrc != fontI8 || !sUiFontTex)) {
        if (!sUiFontTex) glGenTextures(1, &sUiFontTex);
        if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sUiFontTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, fontW, fontH, 0, GL_RED, GL_UNSIGNED_BYTE, fontI8);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        sUiFontSrc = fontI8;
    }

    // Area virtual centrada y sin deformar dentro del render (como la UI 4:3).
    const float scale = std::min(float(w) / virtW, float(h) / virtH);
    const float ox = (w - virtW * scale) * 0.5f, oy = (h - virtH * scale) * 0.5f;
    static std::vector<PcUiVertex, PcGfxHostAllocator<PcUiVertex>> ndc;
    ndc.assign(verts, verts + count);
    for (PcUiVertex& v : ndc) {
        v.x = (ox + v.x * scale) / w * 2.0f - 1.0f;
        v.y = 1.0f - (oy + v.y * scale) / h * 2.0f;
    }

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glViewport(0, 0, w, h);
    invalidate_uniform_cache();
    glUseProgram_ptr(sUiProgram);
    if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sUiFontTex);
    glUniform1i_ptr(glGetUniformLocation_ptr(sUiProgram, "uTex"), 0);
    // Atlas RGBA (texturas del juego y glifos del menú de cristal), unidad 1.
    if (sUiAtlasPixels && (sUiAtlasGen != sUiAtlasUploadedGen || !sUiAtlasTex)) {
        if (!sUiAtlasTex) glGenTextures(1, &sUiAtlasTex);
        if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sUiAtlasTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sUiAtlasW, sUiAtlasH, 0, GL_RGBA, GL_UNSIGNED_BYTE, sUiAtlasPixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        sUiAtlasUploadedGen = sUiAtlasGen;
    }
    if (sUiAtlasTex) {
        if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sUiAtlasTex);
        if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    }
    glUniform1i_ptr(glGetUniformLocation_ptr(sUiProgram, "uAtlas"), 1);
    glBindVertexArray_ptr(sUiVAO);
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sUiVBO);
    glBufferData_ptr(GL_ARRAY_BUFFER, GLsizeiptr(ndc.size() * sizeof(PcUiVertex)), ndc.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, count);
    restore_stream_vao();
    glUseProgram_ptr(0);
    glEnable(GL_SCISSOR_TEST);
    // Como dim_draw: el estado GL cacheado por GX ya no es valido.
    for (int i = 0; i < 8; i++) sBoundTextures[i] = 0;
    gl_program_cache_invalidate();
    invalidate_gl_pipeline_guards();
}

// Desenfoque de una región del render target (coordenadas GX con el mapeo
// vigente: 4:3 centrado, ancho virtual...). Se hace con blits filtrados
// hacia abajo (1/4 y 1/8) y de vuelta: barato y suficiente para que el texto
// de un panel de cristal se lea sobre el fondo del título.
static GLuint sBlurFbo[2] = { 0, 0 }, sBlurTex[2] = { 0, 0 };
static int sBlurTexW[2] = { 0, 0 }, sBlurTexH[2] = { 0, 0 };

static bool blur_target(int i, int w, int h)
{
    if (!glGenFramebuffers_ptr || !glFramebufferTexture2D_ptr) return false;
    if (!sBlurFbo[i]) glGenFramebuffers_ptr(1, &sBlurFbo[i]);
    if (!sBlurTex[i]) glGenTextures(1, &sBlurTex[i]);
    if (sBlurTexW[i] != w || sBlurTexH[i] != h) {
        glBindTexture(GL_TEXTURE_2D, sBlurTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sBlurFbo[i]);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sBlurTex[i], 0);
        sBlurTexW[i] = w;
        sBlurTexH[i] = h;
    }
    return true;
}

void pc_gfx_blur_gx_rect(int gxX, int gxY, int gxW, int gxH, int passes)
{
    if (!sNativeFramebufferReady || !glBlitFramebuffer_ptr || !glBindFramebuffer_ptr) return;
    float sx, sy, ox, oy;
    gx_rect_params(sx, sy, ox, oy);
    // GX tiene el origen arriba; el framebuffer GL abajo.
    const int x0 = int(ox + gxX * sx), x1 = int(ox + (gxX + gxW) * sx);
    const int yTop = int(oy + gxY * sy), yBottom = int(oy + (gxY + gxH) * sy);
    const int y0 = sRenderHeight - yBottom, y1 = sRenderHeight - yTop;
    const int w = x1 - x0, h = y1 - y0;
    if (w <= 8 || h <= 8) return;
    const int wA = w / 4 < 4 ? 4 : w / 4, hA = h / 4 < 4 ? 4 : h / 4;
    const int wB = wA / 2 < 2 ? 2 : wA / 2, hB = hA / 2 < 2 ? 2 : hA / 2;
    if (!blur_target(0, wA, hA) || !blur_target(1, wB, hB)) return;

    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    glDisable(GL_SCISSOR_TEST);
    // nativo -> A (1/4)
    glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sNativeFramebuffer);
    glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, sBlurFbo[0]);
    glBlitFramebuffer_ptr(x0, y0, x1, y1, 0, 0, wA, hA, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    for (int p = 0; p < (passes < 1 ? 1 : passes); p++) {
        // A -> B (1/8) -> A: cada ida y vuelta filtrada ensancha el desenfoque.
        glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sBlurFbo[0]);
        glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, sBlurFbo[1]);
        glBlitFramebuffer_ptr(0, 0, wA, hA, 0, 0, wB, hB, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sBlurFbo[1]);
        glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, sBlurFbo[0]);
        glBlitFramebuffer_ptr(0, 0, wB, hB, 0, 0, wA, hA, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    // A -> nativo (misma región)
    glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sBlurFbo[0]);
    glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, sNativeFramebuffer);
    glBlitFramebuffer_ptr(0, 0, wA, hA, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    glEnable(GL_SCISSOR_TEST);
}

static void dim_window_letterbox(GLint outX, GLint outY, GLint outW, GLint outH)
{
    if (!sDimWindowAfterBlit) return;
    sDimWindowAfterBlit = false;
    if (sDrawableWidth <= 0 || sDrawableHeight <= 0) return;

    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glViewport(0, 0, sDrawableWidth, sDrawableHeight);

    const GLint dw = sDrawableWidth;
    const GLint dh = sDrawableHeight;
    const unsigned char a = sDimWindowAlpha;
    auto bar = [&](GLint x, GLint y, GLint w, GLint h) {
        if (w <= 0 || h <= 0) return;
        glScissor(x, y, w, h);
        dim_draw(a);
    };
    bar(0, 0, outX, dh);
    bar(outX + outW, 0, dw - (outX + outW), dh);
    bar(outX, 0, outW, outY);
    bar(outX, outY + outH, outW, dh - (outY + outH));
    glDisable(GL_SCISSOR_TEST);
}

int pc_gfx_get_ui_43(void) {
    return sUi43 ? 1 : 0;
}

void pc_gfx_set_hud_wide(int enabled) {
    const bool want = enabled != 0;
    if (want != sHudWide) {
        sHudWide = want;
        invalidate_gl_pipeline_guards();
    }
}

int pc_gfx_get_hud_wide(void) {
    return sHudWide ? 1 : 0;
}

int pc_gfx_get_hud_virtual_width(void) {
    return hud_virtual_width();
}

static int menu_wide_cached = -1;

int pc_gfx_menu_wide(void) {
    if (menu_wide_cached < 0) {
#if defined(NECTAR_MENU_PILLARBOX)
        menu_wide_cached = 0;
#else
        menu_wide_cached = (std::getenv("PIKMIN_MENU_PILLARBOX") != nullptr) ? 0 : 1;
#endif
    }
    return menu_wide_cached;
}

void pc_gfx_begin_menu_2d(void) {
    if (pc_gfx_menu_wide()) {
        pc_gfx_set_ui_43(0);
        pc_gfx_set_hud_wide(1);
    } else {
        pc_gfx_set_hud_wide(0);
        pc_gfx_set_ui_43(1);
    }
}

int pc_gfx_menu_virt_width(void) {
    return pc_gfx_menu_wide() ? hud_virtual_width() : 640;
}

int pc_gfx_menu_shift_center(void) {
    return (pc_gfx_menu_virt_width() - 640) / 2;
}

int pc_gfx_menu_shift_right(void) {
    return pc_gfx_menu_virt_width() - 640;
}

int pc_gfx_menu_shift_slot(int slotIndex) {
    if (slotIndex <= 0) {
        return 0;
    }
    if (slotIndex == 1) {
        return pc_gfx_menu_shift_center();
    }
    return pc_gfx_menu_shift_right();
}

static bool sMenuClip43 = false;

void pc_gfx_set_menu_clip_43(int enabled) {
    sMenuClip43 = enabled != 0;
}

void pc_gfx_apply_menu_clip_43(void) {
    if (!sMenuClip43) {
        return;
    }
    pc_gfx_set_scissor((u32)pc_gfx_menu_shift_center(), 0, 640, 480);
}

// ── GLSL Shaders ──
static const char* vShaderHead =
#if PIKI_USE_GLES
    "#version 300 es\n"
    "precision highp float;\n"
#else
    "#version 140\n"
#endif
    "in vec3 aPos;\n"
    "in vec3 aNormal;\n"
    "in vec4 aColor;\n"
    "in vec2 aTexCoord0;\n"
    "in vec2 aTexCoord1;\n"
    "in vec2 aTexCoord2;\n"
    "in vec2 aTexCoord3;\n"
    "in float aMatrixSlot;\n"
    // Índices de matriz de textura por vértice (TEXnMTXIDX) de las
    // generaciones 0-2, empaquetados: (idx0+1) + (idx1+1)*128 + (idx2+1)*16384.
    "in float aTexMtxPacked;\n"
    "uniform mat4 uProjMtx;\n"
    "uniform mat4 uPosMtx;\n"
    "uniform mat3 uNrmMtx;\n"
    "uniform int uUsePalette;\n"
    // Matrix palette for skinned geometry: 21 slots (GX position matrix ids
    // 0,3,...,60), each as three vec4 rows of its 3x4 matrix. Rows rather
    // than mat4/mat3 keep the vertex uniform budget at 126 vectors for both
    // palettes, inside the 256 GLES 3.0 guarantees once lights and texgen
    // matrices are added.
    "uniform vec4 uPosPalette[63];\n"
    "uniform vec4 uNrmPalette[63];\n"
    "out float vLitA0;\n"
    "uniform int uTcMode[8];\n"      // 0=direct uv, 1=position, 2=normal
    "uniform mat4 uTcMtx[8];\n"
    "uniform int uTcProj;\n"        // bit per slot: MTX3x4 texgen (STQ, divide by q)
    // Dual tex: matriz de post-transformación (filas) de las generaciones
    // 0-3, y por bits: si se aplica, si se normaliza antes, y si la primera
    // matriz es 2x4 (entonces entra (s, t, 1)).
    "uniform vec4 uTcPost[12];\n"
    "uniform int uTcPostOn;\n"
    "uniform int uTcNorm;\n"
    "uniform int uTc2x4;\n"
#if !PIKI_USE_GLES
    // Matrices de textura 30..59 (TEXMTX0..9) por filas, para TEXnMTXIDX por
    // vértice con geometría en espacio de modelo (paleta). En GLES no cabe en
    // el presupuesto de uniforms; allí solo hay la ruta de CPU.
    "#define PC_TEX_PALETTE 1\n"
    "uniform vec4 uTexPalette[30];\n"
#endif
    "out vec4 vTcQ;\n"
    "out vec3 vLit0;\n"
    "out vec3 vLit1;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "out vec4 vColor;\n"
    "out vec2 vTexCoord0;\n"
    "out vec2 vTexCoord1;\n"
    "out vec2 vTexCoord2;\n"
    "out vec2 vTexCoord3;\n"
    // Eye-space distance for the fog unit. Zero under an orthographic
    // projection (the interface), where GX's fog is effectively off.
    "out float vFogDist;\n";
// (kGxLightingGlsl va entre las dos mitades; ver vertex_shader_source.)
static const char* vShaderTail =
    "vec2 rawTc(int index) {\n"
    "    if (index == 0) return aTexCoord0;\n"
    "    if (index == 1) return aTexCoord1;\n"
    "    if (index == 2) return aTexCoord2;\n"
    "    if (index == 3) return aTexCoord3;\n"
    "    return aTexCoord3;\n"
    "}\n"
    // nrm is the OBJECT-space normal, not the lit one. GX feeds texgen from the
    // raw vertex attribute, and the texgen matrix the game loads for the
    // environment map already carries the modelview rotation (dgxGraphics
    // useMatrixQuick). Passing the transformed normal rotates it twice and
    // pushes the sphere-map coordinates off the useful range, which is what
    // made the gloss on Olimar, the pellets and the ship disappear.
    "vec3 genTc(int slot, vec4 viewPos, vec3 nrm, vec2 uvIn, vec2 tc0, vec2 tc1, vec2 tc2, vec2 tc3, int vIdx) {\n"
    "    int mode = uTcMode[slot];\n"
    "    if (mode == 0) return vec3(uvIn, 1.0);\n"
    "    vec4 src = (mode == 1) ? viewPos\n"
    "             : (mode == 2) ? vec4(nrm, 1.0)\n"
    "             : (mode >= 11) ? vec4((mode == 11) ? tc0 : (mode == 12) ? tc1 : (mode == 13) ? tc2 : tc3, 0.0, 1.0)\n"
    "             : vec4(rawTc(mode - 3), 0.0, 1.0);\n"
    "    vec4 r = uTcMtx[slot] * src;\n"
    // Matriz de textura elegida por el vértice. En la ruta de CPU la posición y
    // la normal ya vienen transformadas por la matriz de su hueso, que es la
    // que J3D carga como matriz de textura del reflejo (sTexMtxLoadType 0x2000).
    "    if (vIdx >= 30 && vIdx <= 57) {\n"
    "        if (uUsePalette == 0 && (mode == 1 || mode == 2)) r = vec4(src.xyz, 1.0);\n"
    "#ifdef PC_TEX_PALETTE\n"
    "        else { int b = vIdx - 30; r = vec4(dot(uTexPalette[b], src), dot(uTexPalette[b + 1], src), dot(uTexPalette[b + 2], src), 1.0); }\n"
    "#endif\n"
    "    }\n"
    // Dual tex: normalizar (si toca) y la matriz de post-transformación.
    "    if (slot < 4 && ((uTcPostOn >> slot) & 1) != 0) {\n"
    "        vec3 x = r.xyz;\n"
    "        if (((uTc2x4 >> slot) & 1) != 0) x.z = 1.0;\n"
    "        if (((uTcNorm >> slot) & 1) != 0) x = normalize(x);\n"
    "        vec4 x4 = vec4(x, 1.0);\n"
    "        r.xyz = vec3(dot(uTcPost[slot * 3], x4), dot(uTcPost[slot * 3 + 1], x4), dot(uTcPost[slot * 3 + 2], x4));\n"
    "    }\n"
    // GX interpolates S, T and Q and divides per pixel (the water's screen
    // projection); the fragment shader does the divide with vTcQ.
    "    return vec3(r.xy, ((uTcProj >> slot) & 1) != 0 && abs(r.z) > 1e-6 ? r.z : 1.0);\n"
    "}\n"
    "void main() {\n"
    "    int matrixRow = clamp(int(aMatrixSlot + 0.5), 0, 20) * 3;\n"
    "    vec4 aPos4 = vec4(aPos, 1.0);\n"
    "    vec4 worldPos = (uUsePalette != 0)\n"
    "        ? vec4(dot(uPosPalette[matrixRow], aPos4), dot(uPosPalette[matrixRow + 1], aPos4), dot(uPosPalette[matrixRow + 2], aPos4), 1.0)\n"
    "        : uPosMtx * aPos4;\n"
    "    gl_Position = uProjMtx * worldPos;\n"
    "    vec3 N = normalize((uUsePalette != 0)\n"
    "        ? vec3(dot(uNrmPalette[matrixRow].xyz, aNormal), dot(uNrmPalette[matrixRow + 1].xyz, aNormal), dot(uNrmPalette[matrixRow + 2].xyz, aNormal))\n"
    "        : uNrmMtx * aNormal);\n"
    "    vLit0 = gxLit0(N, worldPos);\n"
    "    vLit1 = gxLit1(N, worldPos);\n"
    "    vLitA0 = gxLitA0(N, worldPos);\n"
    "    vWorldPos = worldPos.xyz;\n"
    "    vNormal = N;\n"
    "    vColor = aColor;\n"
    // GX permits later texgens to use the output of an earlier texgen as
    // their source (GX_TG_TEXCOORD0..6). Evaluate in hardware order instead
    // of falling back to the usually absent raw attribute for that slot.
    "    int ti0 = int(mod(aTexMtxPacked, 128.0) + 0.5) - 1;\n"
    "    int ti1 = int(mod(floor(aTexMtxPacked / 128.0), 128.0) + 0.5) - 1;\n"
    "    int ti2 = int(floor(aTexMtxPacked / 16384.0) + 0.5) - 1;\n"
    "    vec3 g0 = genTc(0, aPos4, aNormal, aTexCoord0, vec2(0.0), vec2(0.0), vec2(0.0), vec2(0.0), ti0);\n"
    "    vec3 g1 = genTc(1, aPos4, aNormal, aTexCoord1, g0.xy, vec2(0.0), vec2(0.0), vec2(0.0), ti1);\n"
    "    vec3 g2 = genTc(2, aPos4, aNormal, aTexCoord2, g0.xy, g1.xy, vec2(0.0), vec2(0.0), ti2);\n"
    "    vec3 g3 = genTc(3, aPos4, aNormal, aTexCoord3, g0.xy, g1.xy, g2.xy, vec2(0.0), -1);\n"
    "    vec2 tc0 = g0.xy, tc1 = g1.xy, tc2 = g2.xy, tc3 = g3.xy;\n"
    "    vTcQ = vec4(g0.z, g1.z, g2.z, g3.z);\n"
    "    vTexCoord0 = tc0;\n"
    "    vTexCoord1 = tc1;\n"
    "    vTexCoord2 = tc2;\n"
    "    vTexCoord3 = tc3;\n"
    "    vFogDist = (uProjMtx[2][3] == 0.0) ? 0.0 : -worldPos.z;\n"
    "}\n";

// Fuente completa del shader de vértices: cabecera + iluminación GX compartida + cuerpo.
static const std::string& vertex_shader_source() {
    static const std::string src = std::string(vShaderHead) + kGxLightingGlsl + vShaderTail;
    return src;
}
static const char* vShaderSrc = nullptr; // se rellena en pc_gfx_init (vertex_shader_source)

static const char* fShaderHead =
#if PIKI_USE_GLES
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
#else
    "#version 140\n"
#endif
    "in vec3 vLit0;\n"
    "in vec3 vLit1;\n"
    "in float vLitA0;\n"
    "in vec4 vColor;\n"
    "in vec2 vTexCoord0;\n";
// (kGxLightingGlsl + kGxLightingFragGlsl van aquí; ver fragment_shader_source.)
static const char* fShaderTail =
    "in vec2 vTexCoord1;\n"
    "in vec2 vTexCoord2;\n"
    "in vec2 vTexCoord3;\n"
    "in vec4 vTcQ;\n"
    "out vec4 fragColor;\n"
    "uniform sampler2D uTex0;\n"
    "uniform sampler2D uTex1;\n"
    "uniform sampler2D uTex2;\n"
    "uniform sampler2D uTex3;\n"
    "uniform sampler2D uTex4;\n"
    "uniform sampler2D uTex5;\n"
    "uniform sampler2D uTex6;\n"
    "uniform sampler2D uTex7;\n"
    "uniform int uNumStages;\n"
    "uniform int uFastPath;\n" // 1=texture, 2=raster, 3=texture*raster
    "uniform vec4 uMaterialColor;\n"
    "uniform bool uUseMaterialRgb;\n"
    "uniform bool uUseMaterialAlpha;\n"
    "uniform vec4 uMaterialColor1;\n"
    "uniform bool uUseMaterialRgb1;\n"
    "uniform ivec4 uTevChan[16];\n"
    "uniform vec4 uTevPrev;\n"
    "uniform vec4 uTevReg0;\n"
    "uniform vec4 uTevReg1;\n"
    "uniform vec4 uTevReg2;\n"
    "uniform vec4 uKonst0;\n"
    "uniform vec4 uKonst1;\n"
    "uniform vec4 uKonst2;\n"
    "uniform vec4 uKonst3;\n"
    "uniform vec4 uTevKonst[16];\n"
    "uniform int uAlphaComp0;\n"
    "uniform int uAlphaComp1;\n"
    "uniform int uAlphaOp;\n"
    "uniform float uAlphaRef0;\n"
    "uniform float uAlphaRef1;\n"
    "uniform vec3 uOutTint;\n" // multiplicador final (HUD de J2 en coop)
    "uniform ivec4 uTevCSel[16];\n"
    "uniform ivec4 uTevASel[16];\n"
    "uniform ivec4 uTevCOps[16];\n"
    "uniform ivec4 uTevAOps[16];\n"
    "uniform ivec4 uTevTexInfo[16];\n"
    "uniform ivec4 uTevSwapTable[4];\n"
    "uniform ivec2 uTevSwapSel[16];\n"
    "bool alphaCompare(float value, float reference, int func) {\n"
    "    if (func == 0) return false;\n"
    "    if (func == 1) return value < reference;\n"
    "    if (func == 2) return abs(value - reference) < (0.5 / 255.0);\n"
    "    if (func == 3) return value <= reference;\n"
    "    if (func == 4) return value > reference;\n"
    "    if (func == 5) return abs(value - reference) >= (0.5 / 255.0);\n"
    "    if (func == 6) return value >= reference;\n"
    "    return true;\n"
    "}\n"
    "vec2 resolveCoord(int coordIdx) {\n"
    "    if (coordIdx == 0) return vTexCoord0 / vTcQ.x;\n"
    "    if (coordIdx == 1) return vTexCoord1 / vTcQ.y;\n"
    "    if (coordIdx == 2) return vTexCoord2 / vTcQ.z;\n"
    "    if (coordIdx == 3) return vTexCoord3 / vTcQ.w;\n"
    "    return vTexCoord3 / vTcQ.w;\n"
    "}\n"
    "vec4 resolveTex(int texIdx, int coordIdx) {\n"
    "    vec2 uv = resolveCoord(coordIdx);\n"
    "    if (texIdx == 0) return texture(uTex0, uv);\n"
    "    if (texIdx == 1) return texture(uTex1, uv);\n"
    "    if (texIdx == 2) return texture(uTex2, uv);\n"
    "    if (texIdx == 3) return texture(uTex3, uv);\n"
    "    if (texIdx == 4) return texture(uTex4, uv);\n"
    "    if (texIdx == 5) return texture(uTex5, uv);\n"
    "    if (texIdx == 6) return texture(uTex6, uv);\n"
    "    return texture(uTex7, uv);\n"
    "}\n"
    "float swapComponent(vec4 value, int component) {\n"
    "    if (component == 0) return value.r;\n"
    "    if (component == 1) return value.g;\n"
    "    if (component == 2) return value.b;\n"
    "    return value.a;\n"
    "}\n"
    "vec4 applySwap(vec4 value, int tableIndex) {\n"
    "    ivec4 table = uTevSwapTable[clamp(tableIndex, 0, 3)];\n"
    "    return vec4(swapComponent(value, table.x), swapComponent(value, table.y),\n"
    "                swapComponent(value, table.z), swapComponent(value, table.w));\n"
    "}\n"
    "float packTevCompare(vec3 value, int components) {\n"
    "    vec3 q = floor(clamp(value, 0.0, 1.0) * 255.0 + 0.5);\n"
    "    if (components == 1) return q.r;\n"
    "    if (components == 2) return q.r + q.g * 256.0;\n"
    "    return q.r + q.g * 256.0 + q.b * 65536.0;\n"
    "}\n"
    "vec3 compareTevColor(int op, vec3 a, vec3 b, vec3 c, vec3 d) {\n"
    "    bool isEqual = (op & 1) != 0;\n"
    "    if (op >= 14) {\n"
    "        vec3 qa = floor(clamp(a, 0.0, 1.0) * 255.0 + 0.5);\n"
    "        vec3 qb = floor(clamp(b, 0.0, 1.0) * 255.0 + 0.5);\n"
    "        bvec3 pass = isEqual ? equal(qa, qb) : greaterThan(qa, qb);\n"
    "        return d + vec3(pass.x ? c.r : 0.0, pass.y ? c.g : 0.0, pass.z ? c.b : 0.0);\n"
    "    }\n"
    "    int components = (op < 10) ? 1 : (op < 12) ? 2 : 3;\n"
    "    float av = packTevCompare(a, components);\n"
    "    float bv = packTevCompare(b, components);\n"
    "    bool pass = isEqual ? (av == bv) : (av > bv);\n"
    "    return d + (pass ? c : vec3(0.0));\n"
    "}\n"
    "vec4 resolveC(int id, vec4 prev, vec4 c0, vec4 c1, vec4 c2, vec4 konst, vec4 tex, vec4 rast, int stage) {\n"
    "    if (id == 0) return prev;\n"
    "    if (id == 1) return vec4(prev.a, prev.a, prev.a, 1.0);\n"
    "    if (id == 2) return c0;\n"
    "    if (id == 3) return vec4(c0.a, c0.a, c0.a, 1.0);\n"
    "    if (id == 4) return c1;\n"
    "    if (id == 5) return vec4(c1.a, c1.a, c1.a, 1.0);\n"
    "    if (id == 6) return c2;\n"
    "    if (id == 7) return vec4(c2.a, c2.a, c2.a, 1.0);\n"
    "    if (id == 8) return tex;\n"
    "    if (id == 9) return vec4(tex.a, tex.a, tex.a, 1.0);\n"
    "    if (id == 10) return rast;\n"
    "    if (id == 11) return vec4(rast.a, rast.a, rast.a, 1.0);\n"
    "    if (id == 12) return vec4(1.0);\n"
    "    if (id == 13) return vec4(0.5);\n"
    "    if (id == 14) return konst;\n"
    "    return vec4(0.0);\n"
    "}\n"
    "float resolveA(int id, vec4 prev, vec4 c0, vec4 c1, vec4 c2, vec4 konst, vec4 tex, vec4 rast, int stage) {\n"
    "    if (id == 0) return prev.a;\n"
    "    if (id == 1) return c0.a;\n"
    "    if (id == 2) return c1.a;\n"
    "    if (id == 3) return c2.a;\n"
    "    if (id == 4) return tex.a;\n"
    "    if (id == 5) return rast.a;\n"
    "    if (id == 6) return konst.a;\n"
    "    return 0.0;\n"
    "}\n"
    "void main() {\n"
    "    vec4 rast0;\n"
    "    vec4 base0 = vec4(uUseMaterialRgb ? uMaterialColor.rgb : vColor.rgb,\n"
    "                      uUseMaterialAlpha ? uMaterialColor.a : vColor.a);\n"
    "    vec3 lit0 = gxPixelLit0(vLit0);\n"
    "    if (lit0.x < -0.5) {\n"
    "        // Disabling channel lighting does not force vertex color: GX\n"
    "        // still selects the raster value with matSrc.\n"
    "        rast0 = base0;\n"
    "    } else {\n"
    "        rast0 = vec4(clamp(base0.rgb * lit0, 0.0, 1.0), base0.a);\n"
    "    }\n"
    "    if (vLitA0 >= 0.0) rast0.a = clamp(base0.a * vLitA0, 0.0, 1.0);\n"
    "    vec4 base1 = vec4(uUseMaterialRgb1 ? uMaterialColor1.rgb : vColor.rgb, base0.a);\n"
    "    vec3 lit1 = gxPixelLit1(vLit1);\n"
    "    vec4 rast1 = lit1.x < -0.5\n"
    "        ? base1\n"
    "        : vec4(clamp(base1.rgb * lit1, 0.0, 1.0), base1.a);\n"
    "    if (uFastPath != 0) {\n"
    "        vec4 fastRast = (uTevChan[0].x == 1) ? rast1 : rast0;\n"
    "        vec4 fastCol;\n"
    "        if (uFastPath == 1) fastCol = texture(uTex0, vTexCoord0 / vTcQ.x);\n"
    "        else if (uFastPath == 2) fastCol = fastRast;\n"
    "        else fastCol = texture(uTex0, vTexCoord0 / vTcQ.x) * fastRast;\n"
    "        bool fastTest0 = alphaCompare(fastCol.a, uAlphaRef0, uAlphaComp0);\n"
    "        bool fastTest1 = alphaCompare(fastCol.a, uAlphaRef1, uAlphaComp1);\n"
    "        bool fastPass = (uAlphaOp == 0) ? (fastTest0 && fastTest1) :\n"
    "                        (uAlphaOp == 1) ? (fastTest0 || fastTest1) :\n"
    "                        (uAlphaOp == 2) ? (fastTest0 != fastTest1) : (fastTest0 == fastTest1);\n"
    "        if (!fastPass) discard;\n"
    "        fragColor = vec4(fastCol.rgb * uOutTint, fastCol.a);\n"
    "        return;\n"
    "    }\n"
    // TEVPREV is a real programmable TEV register (BP E0/E1), not an
    // implicit zero value at the beginning of every primitive.
    "    vec4 prev = uTevPrev;\n"
    "    vec4 c0 = uTevReg0;\n"
    "    vec4 c1 = uTevReg1;\n"
    "    vec4 c2 = uTevReg2;\n"
    "    vec4 tex = vec4(1.0);\n"
    "    for (int i = 0; i < uNumStages; i++) {\n"
    "        vec4 konst = uTevKonst[i];\n"
    "        int tIdx = uTevTexInfo[i].x;\n"
    "        int tcIdx = uTevTexInfo[i].y;\n"
    "        vec4 rawTex = (tIdx >= 0) ? resolveTex(tIdx, tcIdx) : vec4(1.0);\n"
    "        tex = applySwap(rawTex, uTevSwapSel[i].y);\n"
    "        vec4 rawRast = (uTevChan[i].x < 0) ? vec4(0.0) : (uTevChan[i].x == 1) ? rast1 : rast0;\n"
    "        vec4 rast = applySwap(rawRast, uTevSwapSel[i].x);\n"
    "        vec4 a = tevU8(resolveC(uTevCSel[i].x, prev, c0, c1, c2, konst, tex, rast, i));\n"
    "        vec4 b = tevU8(resolveC(uTevCSel[i].y, prev, c0, c1, c2, konst, tex, rast, i));\n"
    "        vec4 cc = tevU8(resolveC(uTevCSel[i].z, prev, c0, c1, c2, konst, tex, rast, i));\n"
    "        vec4 d = resolveC(uTevCSel[i].w, prev, c0, c1, c2, konst, tex, rast, i);\n"
    "        float bias = (uTevCOps[i].y == 1) ? 0.5 : (uTevCOps[i].y == 2) ? -0.5 : 0.0;\n"
    "        float scl = (uTevCOps[i].z == 1) ? 2.0 : (uTevCOps[i].z == 2) ? 4.0 : (uTevCOps[i].z == 3) ? 0.5 : 1.0;\n"
    "        vec3 cResult;\n"
    "        vec3 cMix = a.rgb * (vec3(1.0) - cc.rgb) + b.rgb * cc.rgb;\n"
    "        int colorOp = uTevCOps[i].x & 0xff;\n"
    "        if (colorOp >= 8) cResult = compareTevColor(colorOp, a.rgb, b.rgb, cc.rgb, d.rgb);\n"
    "        else if (colorOp == 0) cResult = d.rgb + cMix;\n"
    "        else cResult = d.rgb - cMix;\n"
    "        if (colorOp < 8) cResult = (cResult + bias) * scl;\n"
    "        cResult = ((uTevCOps[i].x & 0x100) != 0) ? clamp(cResult, 0.0, 1.0) : clamp(cResult, -4.0, 4.0);\n"
    "        float al = tevU8(resolveA(uTevASel[i].x, prev, c0, c1, c2, konst, tex, rast, i));\n"
    "        float bl = tevU8(resolveA(uTevASel[i].y, prev, c0, c1, c2, konst, tex, rast, i));\n"
    "        float ac = tevU8(resolveA(uTevASel[i].z, prev, c0, c1, c2, konst, tex, rast, i));\n"
    "        float dl = resolveA(uTevASel[i].w, prev, c0, c1, c2, konst, tex, rast, i);\n"
    "        float abias = (uTevAOps[i].y == 1) ? 0.5 : (uTevAOps[i].y == 2) ? -0.5 : 0.0;\n"
    "        float ascl = (uTevAOps[i].z == 1) ? 2.0 : (uTevAOps[i].z == 2) ? 4.0 : (uTevAOps[i].z == 3) ? 0.5 : 1.0;\n"
    "        float aResult;\n"
    "        float aMix = al * (1.0 - ac) + bl * ac;\n"
    "        int alphaOp = uTevAOps[i].x & 0xff;\n"
    "        if (alphaOp >= 14) {\n"
    "            float qa = floor(clamp(al, 0.0, 1.0) * 255.0 + 0.5);\n"
    "            float qb = floor(clamp(bl, 0.0, 1.0) * 255.0 + 0.5);\n"
    "            bool pass = ((alphaOp & 1) != 0) ? (qa == qb) : (qa > qb);\n"
    "            aResult = dl + (pass ? ac : 0.0);\n"
    "        } else if (alphaOp == 0) aResult = dl + aMix;\n"
    "        else aResult = dl - aMix;\n"
    "        if (alphaOp < 8) aResult = (aResult + abias) * ascl;\n"
    "        aResult = ((uTevAOps[i].x & 0x100) != 0) ? clamp(aResult, 0.0, 1.0) : clamp(aResult, -4.0, 4.0);\n"
    "        int cReg = uTevCOps[i].w;\n"
    // GX routes the color and alpha combiner outputs independently. Writing a
    // complete vec4 here corrupts the destination register's alpha whenever
    // colorOutReg and alphaOutReg differ, which breaks later A0/A1/A2 inputs.
    "        if (cReg == 0) prev.rgb = cResult;\n"
    "        else if (cReg == 1) c0.rgb = cResult;\n"
    "        else if (cReg == 2) c1.rgb = cResult;\n"
    "        else c2.rgb = cResult;\n"
    "        int aReg = uTevAOps[i].w;\n"
    "        if (aReg == 0) prev.a = aResult;\n"
    "        else if (aReg == 1) c0.a = aResult;\n"
    "        else if (aReg == 2) c1.a = aResult;\n"
    "        else c2.a = aResult;\n"
    "    }\n"
    "    vec4 col = prev;\n"
    "    bool test0 = alphaCompare(col.a, uAlphaRef0, uAlphaComp0);\n"
    "    bool test1 = alphaCompare(col.a, uAlphaRef1, uAlphaComp1);\n"
    "    bool pass = (uAlphaOp == 0) ? (test0 && test1) :\n"
    "                (uAlphaOp == 1) ? (test0 || test1) :\n"
    "                (uAlphaOp == 2) ? (test0 != test1) : (test0 == test1);\n"
    "    if (!pass) discard;\n"
    "    fragColor = vec4(col.rgb * uOutTint, col.a);\n"
    "}\n";

static const std::string& fragment_shader_source() {
    static const std::string src = std::string(fShaderHead) + kGxLightingGlsl + kGxLightingFragGlsl + fShaderTail;
    return src;
}
static const char* fShaderSrc = nullptr; // se rellena en pc_gfx_init (fragment_shader_source)

static int sRenderResolutionW = 0, sRenderResolutionH = 0;

void pc_gfx_set_render_resolution(int width, int height) {
    sRenderResolutionW = width > 0 ? width : 0;
    sRenderResolutionH = height > 0 ? height : 0;
    printf("[PC Port] Internal render resolution requested: %dx%d\n", sRenderResolutionW, sRenderResolutionH);
}

void pc_gfx_set_render_scale(float scale) {
    sRenderScale = std::clamp(scale, 0.25f, 4.0f);
    sRenderScaleSet = true;
    printf("[PC Port] Internal render scale set to %.2f\n", sRenderScale);
}

float pc_gfx_get_render_scale(void) {
    return sRenderScale;
}

static float calculate_aspect_ratio(int mode, float windowAspect) {
    switch (mode) {
        case ASPECT_4_3:   return 4.0f / 3.0f;
        case ASPECT_16_10: return 16.0f / 10.0f;
        case ASPECT_16_9:  return 16.0f / 9.0f;
        case ASPECT_21_9:  return 21.0f / 9.0f;
        case ASPECT_AUTO:
        default:
            return windowAspect;
    }
}

void pc_gfx_set_aspect_ratio_mode(int mode) {
    sAspectRatioMode = std::clamp(mode, 0, (int)ASPECT_COUNT - 1);
}

int pc_gfx_get_aspect_ratio_mode(void) {
    return sAspectRatioMode;
}

// Pantalla partida (PLAN_COOP fase 3): mientras se dibuja una vista, la
// proyección y el culling deben usar el aspecto de esa vista, no el de la
// ventana. 0 = sin override.
static float sViewAspectOverride = 0.0f;

void pc_gfx_set_view_aspect_override(float aspect) {
    const float v = aspect > 0.0f ? aspect : 0.0f;
    if (v != sViewAspectOverride) {
        sViewAspectOverride = v;
        invalidate_gl_pipeline_guards();
    }
}

static float sProjOffX = 0.0f, sProjOffY = 0.0f;

// Multiplicador de color de salida del shader principal (HUD de J2).
static float sOutTint[3] = { 1.0f, 1.0f, 1.0f };

// Iluminación por píxel (Graphics > Lighting).
static bool sPerPixelLighting = false;
void pc_gfx_set_per_pixel_lighting(int enabled) {
    const bool want = enabled != 0;
    if (want != sPerPixelLighting) {
        sPerPixelLighting = want;
        state_touched();
        invalidate_gl_pipeline_guards();
    }
}

void pc_gfx_set_out_tint(float r, float g, float b) {
    if (r != sOutTint[0] || g != sOutTint[1] || b != sOutTint[2]) {
        sOutTint[0] = r; sOutTint[1] = g; sOutTint[2] = b;
        state_touched();
        invalidate_gl_pipeline_guards();
    }
}

void pc_gfx_clear_out_tint(void) { pc_gfx_set_out_tint(1.0f, 1.0f, 1.0f); }

void pc_gfx_set_proj_offset(float ndcX, float ndcY) {
    if (ndcX != sProjOffX || ndcY != sProjOffY) {
        sProjOffX = ndcX;
        sProjOffY = ndcY;
        invalidate_gl_pipeline_guards();
    }
}

void pc_gfx_get_proj_offset(float* ndcX, float* ndcY) {
    if (ndcX) *ndcX = sProjOffX;
    if (ndcY) *ndcY = sProjOffY;
}

void pc_gfx_set_view_subrect(float x0, float y0, float x1, float y1) {
    sViewSubrectOn = true;
    sViewSubX0 = x0; sViewSubY0 = y0; sViewSubX1 = x1; sViewSubY1 = y1;
    invalidate_gl_pipeline_guards();
}

void pc_gfx_clear_view_subrect(void) {
    if (!sViewSubrectOn) return;
    sViewSubrectOn = false;
    invalidate_gl_pipeline_guards();
}

float pc_gfx_get_current_aspect_ratio(void) {
    return sViewAspectOverride > 0.0f ? sViewAspectOverride : sCurrentAspectRatio;
}

float pc_gfx_get_window_aspect_ratio(void) {
    return sCurrentAspectRatio;
}

static void calculate_output_area(int drawableWidth, int drawableHeight,
                                  float renderedAspect, GLint& outX, GLint& outY,
                                  GLint& outWidth, GLint& outHeight) {
    const float windowAspect = float(drawableWidth) / float(drawableHeight);
    if (renderedAspect >= windowAspect) {
        outWidth = drawableWidth;
        outHeight = GLint(lroundf(float(drawableWidth) / renderedAspect));
        outX = 0;
        outY = (drawableHeight - outHeight) / 2;
    } else {
        outHeight = drawableHeight;
        outWidth = GLint(lroundf(float(drawableHeight) * renderedAspect));
        outX = (drawableWidth - outWidth) / 2;
        outY = 0;
    }
}

// Attribute indices are forced to fixed slots for every program so the one
// vertex array object stays valid no matter which specialised program is
// bound. Must be called before linking.

// Startup GL errors are latched and only surface at the first draw, which says
// nothing about where they came from. These checkpoints drain and name the
// stage that raised one, so a stray error is attributable instead of ambient.
static void gl_error_checkpoint(const char* stage) {
    // Read the environment once. Two of these sit in the per-draw path, so a
    // getenv per call meant well over a thousand environment scans per frame.
    static const bool enabled = std::getenv("PIKMIN_GL_CHECK") != nullptr;
    if (!enabled) return;
    for (;;) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) break;
        const char* name = "UNKNOWN";
        switch (error) {
            case GL_INVALID_ENUM: name = "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE: name = "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: name = "GL_INVALID_OPERATION"; break;
            case GL_OUT_OF_MEMORY: name = "GL_OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: name = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
        }
        printf("[PC GX check] %s raised %s\n", stage, name);
    }
}

static void bind_fixed_attrib_locations(GLuint program) {
    if (!glBindAttribLocation_ptr) return;
    glBindAttribLocation_ptr(program, kAttrPosIndex, "aPos");
    glBindAttribLocation_ptr(program, kAttrColorIndex, "aColor");
    glBindAttribLocation_ptr(program, kAttrNormalIndex, "aNormal");
    glBindAttribLocation_ptr(program, kAttrMatrixSlotIndex, "aMatrixSlot");
    glBindAttribLocation_ptr(program, kAttrTexMtxIndex, "aTexMtxPacked");
    for (int i = 0; i < kVertexTexCoordCount; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "aTexCoord%d", i);
        glBindAttribLocation_ptr(program, kAttrTexCoord0Index + i, name);
    }
}

// Resolves every uniform this backend can feed. A specialised program bakes
// most of them away; those come back as -1 and the matching glUniform call is
// ignored by the driver, which is what lets one upload path serve both the
// ubershader and the generated programs.
static void query_program_locations(GLuint program, ProgramLocations& out) {
    out = ProgramLocations();
    invalidate_uniform_cache();
    out.projMtx = glGetUniformLocation_ptr(program, "uProjMtx");
    out.posMtx = glGetUniformLocation_ptr(program, "uPosMtx");
    out.usePalette = glGetUniformLocation_ptr(program, "uUsePalette");
    out.posPalette = glGetUniformLocation_ptr(program, "uPosPalette[0]");
    out.nrmPalette = glGetUniformLocation_ptr(program, "uNrmPalette[0]");
    out.materialColor = glGetUniformLocation_ptr(program, "uMaterialColor");
    out.useMaterialRgb = glGetUniformLocation_ptr(program, "uUseMaterialRgb");
    out.useMaterialAlpha = glGetUniformLocation_ptr(program, "uUseMaterialAlpha");
    out.alphaComp0 = glGetUniformLocation_ptr(program, "uAlphaComp0");
    out.alphaComp1 = glGetUniformLocation_ptr(program, "uAlphaComp1");
    out.alphaOp = glGetUniformLocation_ptr(program, "uAlphaOp");
    out.alphaRef0 = glGetUniformLocation_ptr(program, "uAlphaRef0");
    out.alphaRef1 = glGetUniformLocation_ptr(program, "uAlphaRef1");
    out.outTint = glGetUniformLocation_ptr(program, "uOutTint");
    out.perPixel = glGetUniformLocation_ptr(program, "uPerPixel");
    out.numStages = glGetUniformLocation_ptr(program, "uNumStages");
    out.fastPath = glGetUniformLocation_ptr(program, "uFastPath");
    out.tevPrev = glGetUniformLocation_ptr(program, "uTevPrev");
    out.tevReg0 = glGetUniformLocation_ptr(program, "uTevReg0");
    out.tevReg1 = glGetUniformLocation_ptr(program, "uTevReg1");
    out.tevReg2 = glGetUniformLocation_ptr(program, "uTevReg2");
    for (int i = 0; i < 4; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uKonst%d", i);
        out.konst[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < GX_MAXTEVSTAGE; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "uTevCSel[%d]", i);
        out.tevCSel[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uTevASel[%d]", i);
        out.tevASel[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uTevCOps[%d]", i);
        out.tevCOps[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uTevAOps[%d]", i);
        out.tevAOps[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uTevTexInfo[%d]", i);
        out.tevTexInfo[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uTevKonst[%d]", i);
        out.tevKonst[i] = glGetUniformLocation_ptr(program, buf);
    }
    out.indMtx   = glGetUniformLocation_ptr(program, "uIndMtx[0]");
    out.indScale = glGetUniformLocation_ptr(program, "uIndScale[0]");
    for (int i = 0; i < 4; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uTevSwapTable[%d]", i);
        out.tevSwapTable[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < GX_MAXTEVSTAGE; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uTevSwapSel[%d]", i);
        out.tevSwapSel[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < 8; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "uTex%d", i);
        // Query only. The sampler values are written at the end of this
        // function, once the program is actually bound: writing them here sent
        // the new program's locations to whichever program was still current,
        // which GL either rejected or, when that location happened to hold an
        // integer uniform, silently overwrote with a sampler index.
        out.tex[i] = glGetUniformLocation_ptr(program, buf);
    }

    for (int i = 0; i < 8; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "aTexCoord%d", i);
        snprintf(name, sizeof(name), "uTcMode[%d]", i);
        out.tcMode[i] = glGetUniformLocation_ptr(program, name);
        snprintf(name, sizeof(name), "uTcMtx[%d]", i);
        out.tcMtx[i] = glGetUniformLocation_ptr(program, name);
    }
    out.tcProj = glGetUniformLocation_ptr(program, "uTcProj");
    out.tcPost = glGetUniformLocation_ptr(program, "uTcPost[0]");
    out.tcPostOn = glGetUniformLocation_ptr(program, "uTcPostOn");
    out.tcNorm = glGetUniformLocation_ptr(program, "uTcNorm");
    out.tc2x4 = glGetUniformLocation_ptr(program, "uTc2x4");
    out.texPalette = glGetUniformLocation_ptr(program, "uTexPalette[0]");
    out.nrmMtx = glGetUniformLocation_ptr(program, "uNrmMtx");
    out.numLights = glGetUniformLocation_ptr(program, "uNumLights");
    out.ambColor = glGetUniformLocation_ptr(program, "uAmbColor");
    out.chan0En = glGetUniformLocation_ptr(program, "uChan0En");
    out.chan1En = glGetUniformLocation_ptr(program, "uChan1En");
    out.chan0AttnFn = glGetUniformLocation_ptr(program, "uChan0AttnFn");
    out.chan1AttnFn = glGetUniformLocation_ptr(program, "uChan1AttnFn");
    out.numLights1 = glGetUniformLocation_ptr(program, "uNumLights1");
    out.ambColor1 = glGetUniformLocation_ptr(program, "uAmbColor1");
    out.materialColor1 = glGetUniformLocation_ptr(program, "uMaterialColor1");
    out.useMaterialRgb1 = glGetUniformLocation_ptr(program, "uUseMaterialRgb1");
    out.specHalf1 = glGetUniformLocation_ptr(program, "uSpecHalf1");
    out.specAttn1 = glGetUniformLocation_ptr(program, "uSpecAttn1");
    out.numLightsA = glGetUniformLocation_ptr(program, "uNumLightsA");
    out.chanA0En = glGetUniformLocation_ptr(program, "uChanA0En");
    out.chanA0AttnFn = glGetUniformLocation_ptr(program, "uChanA0AttnFn");
    out.ambAlpha0 = glGetUniformLocation_ptr(program, "uAmbAlpha0");
    out.lightAlphaA = glGetUniformLocation_ptr(program, "uLightAlphaA");
    for (int i = 0; i < 4; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uLightPosA[%d]", i);
        out.lightPosA[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightKA[%d]", i);
        out.lightKA[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightDirA[%d]", i);
        out.lightDirA[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightAA[%d]", i);
        out.lightAA[i] = glGetUniformLocation_ptr(program, buf);
    }
    out.fogParams = glGetUniformLocation_ptr(program, "uFogParams");
    out.fogColour = glGetUniformLocation_ptr(program, "uFogColour");
    for (int i = 0; i < 16; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uTevChan[%d]", i);
        out.tevChan[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < 4; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uLightPos[%d]", i);
        out.lightPos[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightColor[%d]", i);
        out.lightColor[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightK[%d]", i);
        out.lightK[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightPos1[%d]", i);
        out.lightPos1[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightColor1[%d]", i);
        out.lightColor1[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightK1[%d]", i);
        out.lightK1[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightDir[%d]", i);
        out.lightDir[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightA[%d]", i);
        out.lightA[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightDir1[%d]", i);
        out.lightDir1[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightA1[%d]", i);
        out.lightA1[i] = glGetUniformLocation_ptr(program, buf);
    }
    for (int i = 0; i < 4; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "uLightPos[%d]", i);
        out.lightPos[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightColor[%d]", i);
        out.lightColor[i] = glGetUniformLocation_ptr(program, buf);
        snprintf(buf, sizeof(buf), "uLightK[%d]", i);
        out.lightK[i] = glGetUniformLocation_ptr(program, buf);
    }
    // Sampler bindings are program state, so the program has to be current
    // before they are written. Leaving it out sent every glUniform1i to
    // whichever program happened to be bound: GL rejected them, the samplers
    // kept their default of zero, and every multi-texture stage sampled unit 0.
    glUseProgram_ptr(program);
    invalidate_uniform_cache();
    for (int i = 0; i < 8; i++) {
        if (out.tex[i] >= 0) glUniform1i_ptr(out.tex[i], i);
    }
}

static inline uint64_t hash_bytes(uint64_t h, const void* data, size_t bytes);
static inline double submit_clock_ms();

// ── Fase 3: caché de binarios de programa ───────────────────────────────────
//
// Compiling a specialised TEV program costs ~20 ms on Adreno 740 and happens
// the first time a material is seen: a hitch per new material, ~100 of them
// in the first level. The linked binary is written to <save>/shader_cache/
// keyed by the sources and the driver identity, and loaded instead of
// compiled on every later run. A binary the driver rejects (updated driver,
// corrupted file) falls back to compiling and is rewritten.
// PIKMIN_SHADER_CACHE=0 disables it.
#ifndef GL_PROGRAM_BINARY_LENGTH
#define GL_PROGRAM_BINARY_LENGTH 0x8741
#endif
#ifndef GL_NUM_PROGRAM_BINARY_FORMATS
#define GL_NUM_PROGRAM_BINARY_FORMATS 0x87FE
#endif
#ifndef GL_PROGRAM_BINARY_RETRIEVABLE_HINT
#define GL_PROGRAM_BINARY_RETRIEVABLE_HINT 0x8257
#endif
typedef void (APIENTRYP PCGLGETPROGRAMBINARYPROC) (GLuint program, GLsizei bufSize, GLsizei* length, GLenum* binaryFormat, void* binary);
typedef void (APIENTRYP PCGLPROGRAMBINARYPROC) (GLuint program, GLenum binaryFormat, const void* binary, GLsizei length);
typedef void (APIENTRYP PCGLPROGRAMPARAMETERIPROC) (GLuint program, GLenum pname, GLint value);
static PCGLGETPROGRAMBINARYPROC glGetProgramBinary_ptr = nullptr;
static PCGLPROGRAMBINARYPROC glProgramBinary_ptr = nullptr;
static PCGLPROGRAMPARAMETERIPROC glProgramParameteri_ptr = nullptr;
static bool sProgramBinaryReady = false;
static std::string sProgramBinaryDir;
static std::string sDriverIdentity;
static uint32_t sProgramBinaryHits = 0;
static uint32_t sProgramBinaryMisses = 0;
static double sShaderBuildMsThisFrame = 0.0;

static void program_binary_init() {
    if (const char* v = std::getenv("PIKMIN_SHADER_CACHE")) {
        if (v[0] == '0') { printf("[PC Port] Shader binary cache disabled (PIKMIN_SHADER_CACHE=0)\n"); return; }
    }
    glGetProgramBinary_ptr = (PCGLGETPROGRAMBINARYPROC)SDL_GL_GetProcAddress("glGetProgramBinary");
    glProgramBinary_ptr = (PCGLPROGRAMBINARYPROC)SDL_GL_GetProcAddress("glProgramBinary");
    glProgramParameteri_ptr = (PCGLPROGRAMPARAMETERIPROC)SDL_GL_GetProcAddress("glProgramParameteri");
    GLint formats = 0;
    glGetIntegerv(GL_NUM_PROGRAM_BINARY_FORMATS, &formats);
    glGetError();
    if (!glGetProgramBinary_ptr || !glProgramBinary_ptr || formats <= 0) {
        printf("[PC Port] Shader binary cache unavailable (%d binary formats)\n", int(formats));
        return;
    }
    const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    sDriverIdentity = std::string(renderer ? renderer : "?") + "|" + (version ? version : "?") + "|" + (glsl ? glsl : "?");
    const char* save = std::getenv("NECTAR_SAVE_DIR");
    sProgramBinaryDir = save && save[0] ? std::string(save) + "/shader_cache" : std::string("shader_cache");
#ifdef _WIN32
    _mkdir(sProgramBinaryDir.c_str());
#else
    mkdir(sProgramBinaryDir.c_str(), 0700);
#endif
    sProgramBinaryReady = true;
    printf("[PC Port] Shader binary cache: %s (%s)\n", sProgramBinaryDir.c_str(), sDriverIdentity.c_str());
}

static uint64_t program_binary_key(const char* vertexSource, const char* fragmentSource) {
    uint64_t h = 14695981039346656037ull;
    h = hash_bytes(h, vertexSource, strlen(vertexSource));
    h = hash_bytes(h, fragmentSource, strlen(fragmentSource));
    h = hash_bytes(h, sDriverIdentity.data(), sDriverIdentity.size());
    return h;
}

static std::string program_binary_path(uint64_t key) {
    char name[64];
    snprintf(name, sizeof name, "/%016llx.bin", (unsigned long long)key);
    return sProgramBinaryDir + name;
}

// Loads the cached binary into `program`. True only if the driver accepted
// it and the program links.
static bool program_binary_load(GLuint program, uint64_t key) {
    if (!sProgramBinaryReady) return false;
    FILE* f = fopen(program_binary_path(key).c_str(), "rb");
    if (!f) return false;
    uint32_t header[3] = {};
    PcGfxHostVector<unsigned char> blob;
    bool ok = fread(header, sizeof header, 1, f) == 1 && header[0] == 0x3142504Eu && header[2] > 0 && header[2] < (64u << 20);
    if (ok) {
        blob.resize(header[2]);
        ok = fread(blob.data(), 1, blob.size(), f) == blob.size();
    }
    fclose(f);
    if (!ok) return false;
    glProgramBinary_ptr(program, GLenum(header[1]), blob.data(), GLsizei(blob.size()));
    GLint status = 0;
    if (glGetProgramiv_ptr) glGetProgramiv_ptr(program, GL_LINK_STATUS, &status);
    glGetError();
    if (status != GL_TRUE) return false;
    ++sProgramBinaryHits;
    return true;
}

static void program_binary_store(GLuint program, uint64_t key) {
    if (!sProgramBinaryReady) return;
    GLint length = 0;
    glGetProgramiv_ptr(program, GL_PROGRAM_BINARY_LENGTH, &length);
    if (length <= 0) return;
    PcGfxHostVector<unsigned char> blob(static_cast<size_t>(length), 0);
    GLsizei written = 0;
    GLenum format = 0;
    glGetProgramBinary_ptr(program, length, &written, &format, blob.data());
    if (glGetError() != GL_NO_ERROR || written <= 0) return;
    // Write to a temporary name first so a crash mid-write never leaves a
    // truncated file that the next run would try to load.
    const std::string path = program_binary_path(key);
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    const uint32_t header[3] = { 0x3142504Eu, uint32_t(format), uint32_t(written) };
    const bool ok = fwrite(header, sizeof header, 1, f) == 1 && fwrite(blob.data(), 1, size_t(written), f) == size_t(written);
    fclose(f);
    if (ok) rename(tmp.c_str(), path.c_str());
    else remove(tmp.c_str());
    ++sProgramBinaryMisses;
}

// ── Fase 5: GPU móvil ───────────────────────────────────────────────────────
// PIKMIN_FB_INVALIDATE=0 keeps every attachment resolved to memory each
// frame (the pre-phase-5 behaviour) for A/B comparison.
// Solo en GLES (GPU de móvil por tiles, donde ahorra ancho de banda). En
// escritorio no aporta nada y era dañino: un fotograma en el que el juego no
// dibuja nada (cargando el vídeo de apertura) presentaba el framebuffer
// invalidado, memoria indefinida que en NVIDIA son restos de imágenes viejas
// troceados (la selección de partida con píxeles y colores raros).
// PIKMIN_FB_INVALIDATE=1/0 lo fuerza.
static bool fb_invalidate_enabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("PIKMIN_FB_INVALIDATE");
        if (v) return v[0] != '0';
        return PIKI_USE_GLES != 0;
    }();
    return enabled;
}
// Algo escribió en el framebuffer nativo desde la última presentación.
static bool sNativeWrittenThisFrame = false;
// The port never touches the stencil buffer (no glStencil* anywhere), so on
// GLES the depth attachment is plain 24-bit depth: less tile memory and
// bandwidth than packed depth-stencil. PIKMIN_DEPTH_STENCIL=1 restores the
// packed format. Desktop keeps it: the GL 2.1 fallback path expects it.
static bool depth_has_stencil() {
    static const bool packed = [] {
        const char* v = std::getenv("PIKMIN_DEPTH_STENCIL");
        if (v) return v[0] == '1';
        return PIKI_USE_GLES == 0;
    }();
    return packed;
}
static GLenum depth_internal_format() { return depth_has_stencil() ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24; }
static GLenum depth_attachment() { return depth_has_stencil() ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT; }
static void depth_tex_image(int width, int height) {
    if (depth_has_stencil()) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    }
}

// The one Vertex layout, described to whichever GL_ARRAY_BUFFER is bound:
// the streaming ring at init, the resident-mesh arena in its own VAO.
static void setup_vertex_attribs() {
    glEnableVertexAttribArray_ptr(sAttrPos);
    glVertexAttribPointer_ptr(sAttrPos, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));
    if (sAttrNormal >= 0) {
        glEnableVertexAttribArray_ptr(sAttrNormal);
        glVertexAttribPointer_ptr(sAttrNormal, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, nx));
    }
    glEnableVertexAttribArray_ptr(sAttrColor);
    glVertexAttribPointer_ptr(sAttrColor, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));
    glEnableVertexAttribArray_ptr(kAttrMatrixSlotIndex);
    glVertexAttribPointer_ptr(kAttrMatrixSlotIndex, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*)offsetof(Vertex, matrixSlot));
    glEnableVertexAttribArray_ptr(kAttrTexMtxIndex);
    glVertexAttribPointer_ptr(kAttrTexMtxIndex, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              (void*)offsetof(Vertex, texMtxPacked));
    for (int i = 0; i < 8; ++i) {
        if (sAttrTexCoord[i] >= 0) {
            glEnableVertexAttribArray_ptr(sAttrTexCoord[i]);
            const size_t offset = offsetof(Vertex, tex) + size_t(i) * 2 * sizeof(float);
            glVertexAttribPointer_ptr(sAttrTexCoord[i], 2, GL_FLOAT, GL_FALSE,
                                      sizeof(Vertex), reinterpret_cast<void*>(offset));
        }
    }
}

// Static VBO arena for resident meshes, with its own VAO so switching between
// it and the streaming ring is one glBindVertexArray. Leaves the streaming
// VAO and ring bound on return, which is what the rest of the file assumes.
static void mesh_arena_init() {
    if (!sMeshDecodeCacheEnabled || !glGenVertexArrays_ptr || !glBindVertexArray_ptr) {
        sMeshDecodeCacheEnabled = false;
        return;
    }
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &sStreamVAO);
    glGenVertexArrays_ptr(1, &sMeshVAO);
    glBindVertexArray_ptr(sMeshVAO);
    glGenBuffers_ptr(1, &sMeshArena);
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sMeshArena);
    glBufferData_ptr(GL_ARRAY_BUFFER, GLsizeiptr(sMeshArenaCapacity), nullptr, GL_STATIC_DRAW);
    setup_vertex_attribs();
    glBindVertexArray_ptr(GLuint(sStreamVAO));
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sVBO);
    sMeshArenaUsed = 0;
    sMeshArenaReady = glGetError() == GL_NO_ERROR;
    printf("[PC Port] Resident mesh arena: %s (%zu MB)\n", sMeshArenaReady ? "ready" : "unavailable",
           sMeshArenaCapacity >> 20);
    if (!sMeshArenaReady) sMeshDecodeCacheEnabled = false;
}

void pc_gfx_init(void) {
    // GXInit runs from JFWSystem::init with the JKR system heap current: the
    // static shader sources, program-cache paths and GL caches built here live
    // for the whole process, so keep them out of the game heaps.
    PcGfxHostAllocScope hostAllocScope;
    state_touched();
    load_gl_functions();
    if (const char* value = std::getenv("PIKMIN_GPU_SKINNING")) sGpuSkinningEnabled = value[0] != '0';
    if (const char* value = std::getenv("PIKMIN_MESH_CACHE")) sMeshDecodeCacheEnabled = value[0] != '0';
    // PLAN_TEXTURAS_HD fase 1: escanea Load/Textures/ y consulta el driver. Debe
    // correr antes del primer GXInitTexObj, que llega durante el arranque.
    pc_texpack_init();

    const GLubyte* glVersion = glGetString(GL_VERSION);
    const GLubyte* glslVersion = glGetString(GL_SHADING_LANGUAGE_VERSION);
    const GLubyte* glVendor = glGetString(GL_VENDOR);
    const GLubyte* glRenderer = glGetString(GL_RENDERER);
    gl_error_checkpoint("context creation");
    printf("[PC Port] OpenGL context: %s; GLSL: %s\n",
           glVersion ? reinterpret_cast<const char*>(glVersion) : "unknown",
           glslVersion ? reinterpret_cast<const char*>(glslVersion) : "unknown");
    // Which GPU actually got the context. On a switchable-graphics laptop this
    // is the only way to tell the dedicated card from the integrated one
    // without leaving the game, and the answer decides whether a report about
    // frame rate is about the port or about the wrong adapter.
    printf("[PC Port] GPU: %s -- %s\n",
           glVendor ? reinterpret_cast<const char*>(glVendor) : "unknown",
           glRenderer ? reinterpret_cast<const char*>(glRenderer) : "unknown");
    if (glRenderer) {
        const char* renderer = reinterpret_cast<const char*>(glRenderer);
        const bool integrated = std::strstr(renderer, "Intel") || std::strstr(renderer, "llvmpipe")
                             || std::strstr(renderer, "Softpipe") || std::strstr(renderer, "SVGA3D");
        if (integrated && std::getenv("__NV_PRIME_RENDER_OFFLOAD")) {
            printf("[PC Port] WARNING: requested NVIDIA PRIME but the context is %s. "
                   "Menus will sit around 30 fps. On Wayland use: prime-run ./build/bin/nectar\n",
                   renderer);
        }
    }
    if (const char* value = std::getenv("PIKMIN_TEV_SPECIALIZE")) {
        sSpecialiseShaders = value[0] != '0';
    }
    if (const char* value = std::getenv("PIKMIN_TEV_MAX_STAGES")) {
        sSpecialiseMaxStages = std::clamp(atoi(value), 0, 16);
    }
    printf("[PC Port] TEV specialisation: %s, up to %d stage(s); the rest use the ubershader.\n",
           sSpecialiseShaders ? "on" : "off", sSpecialiseMaxStages);
    // The environment is the diagnostic shell: it overrides the saved setting
    // so an A/B on scale can be run without touching the player's config.
    if (const char* value = std::getenv("PIKMIN_RENDER_SCALE")) {
        sRenderScale = std::clamp(strtof(value, nullptr), 0.25f, 4.0f);
        sRenderScaleSet = true;
        printf("[PC Port] Internal render scale forced to %.2f by PIKMIN_RENDER_SCALE\n", sRenderScale);
    }

    // Default identity matrices
    for (int i = 0; i < 16; i++) {
        sProjMatrix[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }
    for (int m = 0; m < 64; m++) {
        for (int i = 0; i < 16; i++) {
            sPosMatrix[m][i] = (i % 5 == 0) ? 1.0f : 0.0f;
            sTexMatrices[m][i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
        for (int c = 0; c < 4; c++) sPostRows[m][c] = (m >= 61 && c == m - 61) ? 1.0f : 0.0f;
        sTexMtxLoaded[m] = false;
        for (int c = 0; c < 8; c++) {
            sTexCoordGen[c].active = false;
            sTexCoordGen[c].type = 1;
            sTexCoordGen[c].src = 4;
            sTexCoordGen[c].mtxIdx = 0;
            sTexCoordGen[c].postIdx = 125;
            sTexCoordGen[c].normalize = false;
        }
    }

    if (!glCreateShader_ptr) {
        printf("[PC Port Error] OpenGL extension loader failed!\n");
        return;
    }

    // Compile Shaders
    GLuint vs = glCreateShader_ptr(GL_VERTEX_SHADER);
    vShaderSrc = vertex_shader_source().c_str();
    fShaderSrc = fragment_shader_source().c_str();
    glShaderSource_ptr(vs, 1, &vShaderSrc, NULL);
    glCompileShader_ptr(vs);

    program_binary_init();
    const uint64_t uberKey = program_binary_key(vShaderSrc, fShaderSrc);
    bool uberFromBinary = false;
    if (sProgramBinaryReady) {
        sShaderProgram = glCreateProgram_ptr();
        bind_fixed_attrib_locations(sShaderProgram);
        uberFromBinary = program_binary_load(sShaderProgram, uberKey);
        if (!uberFromBinary) { glDeleteProgram_ptr(sShaderProgram); sShaderProgram = 0; }
        else printf("[PC Port] Ubershader loaded from binary cache\n");
    }

    GLuint fs = glCreateShader_ptr(GL_FRAGMENT_SHADER);
    if (!uberFromBinary) {
        glShaderSource_ptr(fs, 1, &fShaderSrc, NULL);
        glCompileShader_ptr(fs);
    }

    // Report shader compile errors
    auto checkShader = [&](GLuint shader, const char* name) {
        GLint status = 0;
        if (glGetShaderiv_ptr) glGetShaderiv_ptr(shader, GL_COMPILE_STATUS, &status);
        if (status != GL_TRUE) {
            char log[4096] = { 0 };
            if (glGetShaderInfoLog_ptr) glGetShaderInfoLog_ptr(shader, sizeof(log), NULL, log);
            printf("[PC Port Shader Error] %s failed to compile:\n%s\n", name, log);
            // Con número de línea, para casar el error del driver con el
            // fuente (los drivers móviles son más estrictos que Mesa).
            const char* src = (shader == vs) ? vShaderSrc : fShaderSrc;
            int line = 1;
            printf("%4d: ", line);
            for (const char* c = src; *c; ++c) {
                putchar(*c);
                if (*c == '\n' && c[1]) printf("%4d: ", ++line);
            }
            printf("\n");
            return false;
        }
        return true;
    };
    bool vsOk = checkShader(vs, "vertex shader");
    bool fsOk = uberFromBinary || checkShader(fs, "fragment shader");
    if (!vsOk || !fsOk) {
        printf("[PC Port Error] Shader compilation failed - rendering will be broken!\n");
    }

    if (!uberFromBinary) {
        sShaderProgram = glCreateProgram_ptr();
        glAttachShader_ptr(sShaderProgram, vs);
        glAttachShader_ptr(sShaderProgram, fs);
        bind_fixed_attrib_locations(sShaderProgram);
        if (sProgramBinaryReady && glProgramParameteri_ptr) {
            glProgramParameteri_ptr(sShaderProgram, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
            glGetError();
        }
        glLinkProgram_ptr(sShaderProgram);
    }

    {
        GLint status = 0;
        if (glGetProgramiv_ptr) glGetProgramiv_ptr(sShaderProgram, GL_LINK_STATUS, &status);
        if (status != GL_TRUE) {
            char log[4096] = { 0 };
            if (glGetProgramInfoLog_ptr) glGetProgramInfoLog_ptr(sShaderProgram, sizeof(log), NULL, log);
            printf("[PC Port Shader Error] Program link failed:\n%s\n", log);
        } else {
            printf("[PC Port] TEV evaluator shaders compiled and linked OK\n");
            if (!uberFromBinary) program_binary_store(sShaderProgram, uberKey);
        }
    }

    gl_error_checkpoint("ubershader link");
    query_program_locations(sShaderProgram, sLoc);
    gl_error_checkpoint("ubershader uniform locations");
    sUberLocations = sLoc;
    sCurrentProgram = sShaderProgram;
    // The vertex shader is identical for every specialised program, so it is
    // compiled once here and reused when linking them.
    sSharedVertexShader = vs;

    gl_error_checkpoint("shader setup");
    // Core OpenGL requires every vertex attribute definition and draw to have
    // a non-zero VAO bound. The old Pikmin 1 compatibility context accepted
    // VAO 0, but the Pikmin 2 desktop context is 3.3 core.
    GLuint streamVao = 0;
    glGenVertexArrays_ptr(1, &streamVao);
    sStreamVAO = static_cast<GLint>(streamVao);
    glBindVertexArray_ptr(streamVao);
    glGenBuffers_ptr(1, &sVBO);
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sVBO);
    glBufferData_ptr(GL_ARRAY_BUFFER, sVboCapacity, nullptr, GL_STREAM_DRAW);

    if (glGenFramebuffers_ptr && glBindFramebuffer_ptr && glFramebufferTexture2D_ptr
        && glGenRenderbuffers_ptr && glBindRenderbuffer_ptr && glRenderbufferStorage_ptr
        && glFramebufferRenderbuffer_ptr && glCheckFramebufferStatus_ptr && glBlitFramebuffer_ptr) {
        glGenFramebuffers_ptr(1, &sNativeFramebuffer);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
        glGenTextures(1, &sNativeColorTexture);
        glBindTexture(GL_TEXTURE_2D, sNativeColorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sRenderWidth, sRenderHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   sNativeColorTexture, 0);
        // Depth as a texture rather than a renderbuffer. Nothing samples it
        // yet, but every screen-space effect worth having -- ambient occlusion,
        // depth-based fog, depth of field, outlines -- needs to read it, and a
        // renderbuffer cannot be read. A renderbuffer is still the fallback:
        // the port keeps a GL 2.1 context path for old drivers, and there
        // packed depth-stencil textures are an extension rather than a
        // guarantee. Losing the effects is acceptable; losing the game is not.
        sDepthIsTexture = false;
        glGenTextures(1, &sNativeDepthTexture);
        glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
        depth_tex_image(sRenderWidth, sRenderHeight);
        // Depth must not be filtered or wrapped: a sample has to be the value
        // written at that pixel, not a blend of its neighbours.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Sampled as a plain value, not as a shadow comparison.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, depth_attachment(),
                                   GL_TEXTURE_2D, sNativeDepthTexture, 0);
        if (glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
            sDepthIsTexture = true;
        } else {
            glDeleteTextures(1, &sNativeDepthTexture);
            sNativeDepthTexture = 0;
            glGenRenderbuffers_ptr(1, &sNativeDepthStencil);
            glBindRenderbuffer_ptr(GL_RENDERBUFFER, sNativeDepthStencil);
            glRenderbufferStorage_ptr(GL_RENDERBUFFER, depth_internal_format(), sRenderWidth, sRenderHeight);
            glFramebufferRenderbuffer_ptr(GL_FRAMEBUFFER, depth_attachment(),
                                          GL_RENDERBUFFER, sNativeDepthStencil);
        }
        sNativeFramebufferReady = glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebufferReady ? sNativeFramebuffer : 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        sBoundTextures[0] = 0;
        printf("[PC Port] Scalable render target: %s (scale %.2f), depth: %s\n",
               sNativeFramebufferReady ? "active" : "unavailable", sRenderScale,
               sDepthIsTexture ? "texture (readable)" : "renderbuffer (not readable)");
    }
    setup_vertex_attribs();
    gl_error_checkpoint("vertex array setup");
    mesh_arena_init();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    // GX defines clockwise screen-space triangles as front-facing, whereas
    // OpenGL defaults to counter-clockwise.  Matching GX here keeps culling
    // from rejecting every front face in 3D scenes and cinematics.
    glFrontFace(GL_CW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

#if PIKI_USE_GLES
    printf("[PC Port] OpenGL ES backend and GLSL ES shaders initialized successfully\n");
#else
    printf("[PC Port] OpenGL backend and GLSL shaders initialized successfully\n");
#endif
}

void pc_gfx_perf_scope_begin(const char* name) {
    sPerfCurrentScope = -1;
    if (!sPerfStatsEnabled || !name) return;
    for (int i = 0; i < 8; ++i) {
        if (sPerfScopes[i].name == name || (sPerfScopes[i].name && strcmp(sPerfScopes[i].name, name) == 0)) {
            sPerfCurrentScope = i;
            return;
        }
        if (!sPerfScopes[i].name) {
            sPerfScopes[i].name = name;
            sPerfCurrentScope = i;
            return;
        }
    }
}

void pc_gfx_perf_scope_end(void) {
    sPerfCurrentScope = -1;
}

static bool perf_gpu_queries_supported() {
#ifdef GL_TIME_ELAPSED
    return glGenQueries_ptr && glBeginQuery_ptr && glEndQuery_ptr
        && glGetQueryObjectiv_ptr && glGetQueryObjectui64v_ptr;
#else
    return false;
#endif
}

static void perf_gpu_queries_init() {
    if (sPerfGpuQueriesReady || !perf_gpu_queries_supported()) return;
    GLuint ids[PC_GPU_QUERY_RING_SIZE * 2] = {};
    glGenQueries_ptr(GLsizei(PC_GPU_QUERY_RING_SIZE * 2), ids);
    for (unsigned i = 0; i < PC_GPU_QUERY_RING_SIZE; ++i) {
        sPerfGpuQueries[i].scene = ids[i * 2];
        sPerfGpuQueries[i].blit = ids[i * 2 + 1];
    }
    sPerfGpuQueriesReady = ids[0] != 0;
    if (sPerfGpuQueriesReady)
        fprintf(stderr, "[PERF GPU] asynchronous timer queries enabled\n");
}

static void perf_gpu_queries_poll() {
#ifdef GL_TIME_ELAPSED
    if (!sPerfGpuQueriesReady) return;
    // Never wait for the GPU. Only consume query pairs whose two results are
    // already available, leaving busy slots pending in the ring.
    for (PerfGpuQuery& query : sPerfGpuQueries) {
        if (!query.pending) continue;
        GLint sceneAvailable = GL_FALSE;
        GLint blitAvailable = GL_FALSE;
        glGetQueryObjectiv_ptr(query.scene, GL_QUERY_RESULT_AVAILABLE, &sceneAvailable);
        glGetQueryObjectiv_ptr(query.blit, GL_QUERY_RESULT_AVAILABLE, &blitAvailable);
        if (sceneAvailable != GL_TRUE || blitAvailable != GL_TRUE) continue;
        GLuint64 sceneNs = 0;
        GLuint64 blitNs = 0;
        glGetQueryObjectui64v_ptr(query.scene, GL_QUERY_RESULT, &sceneNs);
        glGetQueryObjectui64v_ptr(query.blit, GL_QUERY_RESULT, &blitNs);
        query.pending = false;
#ifdef GL_GPU_DISJOINT_EXT
        // En móvil el reloj de la GPU cambia de frecuencia (térmica, ahorro
        // de energía); cuando eso pasa entre begin y end la extensión avisa y
        // el valor no significa nada. Se descarta la muestra, no el estado.
        {
            GLint disjoint = 0;
            glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
            if (disjoint) continue;
        }
#endif
        // A result over a second is not a frame: the counter wrapped or the
        // driver handed back garbage after a disjoint it did not flag.
        if (sceneNs > 1000000000ull || blitNs > 1000000000ull) continue;
        sPerfGpuSceneNs += uint64_t(sceneNs);
        sPerfGpuBlitNs += uint64_t(blitNs);
        ++sPerfGpuSamples;
        if (pc_tick_profiler_enabled()) {
            pc_tick_profiler_record(kPcTickGpuScene, double(sceneNs) / 1000000.0);
            pc_tick_profiler_record(kPcTickGpuBlit, double(blitNs) / 1000000.0);
        }
    }
#endif
}

static void perf_gpu_scene_begin() {
#ifdef GL_TIME_ELAPSED
    if (!sGpuTimingEnabled) return;
    perf_gpu_queries_init();
    perf_gpu_queries_poll();
    if (!sPerfGpuQueriesReady || sPerfGpuSceneActive) return;
    PerfGpuQuery& query = sPerfGpuQueries[sPerfGpuQueryWrite];
    if (query.pending) return;
    glBeginQuery_ptr(GL_TIME_ELAPSED, query.scene);
    sPerfGpuSceneActive = true;
#endif
}

void pc_gfx_begin_frame(void) {
    sUi43 = false;
    sHudWide = false;
    sMenuClip43 = false;
    // One frame's worth of projection changes every second. Printing every
    // frame drowns the log and changes the timing of what it is measuring.
    {
        static bool checked = false;
        static bool wanted = false;
        static int gate = 0;
        if (!checked) {
            checked = true;
            wanted = std::getenv("PIKMIN_PROJ_DEBUG") != nullptr;
        }
        sProjDebug = wanted && (++gate % 60 == 0);
        if (sProjDebug) {
            printf("[PC Port] ---- frame ----\n");
        }
        sProjSeenThisFrame = 0;
        sDrawsAtProjection = sPerfDraws;
    }
    ++sGfxFrameSerial;
    sPendingValid = false;
    if (!kP2Host) sBestDraws = -1;
    sDrawsAtFrameStart = sPerfDraws;
    using Clock = std::chrono::steady_clock;
    static const bool perfStats = std::getenv("PIKMIN_PERF_STATS") != nullptr;
    sPerfStatsEnabled = perfStats;
    sGpuTimingEnabled = perfStats || pc_tick_profiler_enabled();
    if (sGpuTimingEnabled) {
        perf_gpu_queries_init();
        perf_gpu_queries_poll();
    }
    static Clock::time_point lastFrame;
    static double elapsedMs = 0.0;
    static unsigned frames = 0;
    const Clock::time_point now = Clock::now();
    if (lastFrame.time_since_epoch().count() != 0) {
        elapsedMs += std::chrono::duration<double, std::milli>(now - lastFrame).count();
        ++frames;
    }
    lastFrame = now;
    if (perfStats && frames >= 120) {
        fprintf(stderr,
                "[PERF] %.1f fps %.2f ms | %.0f draws (%.0f source, %.0f fast) %.0f verts %.0f DL (%.2f MiB) %.2f tex uploads/frame\n",
                frames * 1000.0 / elapsedMs, elapsedMs / frames,
                double(sPerfDraws) / frames, double(sPerfSourcePrimitives) / frames,
                double(sPerfFastDraws) / frames,
                double(sPerfVertices) / frames,
                double(sPerfDisplayLists) / frames,
                double(sPerfDisplayListBytes) / frames / (1024.0 * 1024.0),
                double(sPerfTextureUploads) / frames);
        fprintf(stderr, "[PERF MESH] %zu resident meshes, arena %zu/%zu MB, %u resets; stream ring %zu KB/frame (peak %zu KB of %zu MB)\n",
                sResidentMeshes.size(), sMeshArenaUsed >> 20, sMeshArenaCapacity >> 20, sMeshResets,
                sVboBytesLastFrame >> 10, sVboBytesPeakFrame >> 10, sVboCapacity >> 20);
        fprintf(stderr, "[PERF SHADER] %llu compiled this run, binary cache %s: %u hits, %u stored\n",
                (unsigned long long)sPerfShaderCompiles, sProgramBinaryReady ? "on" : "off",
                sProgramBinaryHits, sProgramBinaryMisses);
        fprintf(stderr, "[PERF TEV] stages");
        for (int i = 0; i <= GX_MAXTEVSTAGE; ++i) {
            if (sPerfTevStageDraws[i])
                fprintf(stderr, " %d=%.0f", i, double(sPerfTevStageDraws[i]) / frames);
        }
        fprintf(stderr, "\n");
        unsigned matrixSlots = 0;
        for (uint64_t bits = sPerfPnMtxMask; bits; bits &= bits - 1) ++matrixSlots;
        fprintf(stderr, "[PERF DLMTX] %u distinct PNMTXIDX slots, max index %u, mask 0x%016llx\n",
                matrixSlots, unsigned(sPerfPnMtxMax),
                static_cast<unsigned long long>(sPerfPnMtxMask));
        if (sPerfGpuSamples != 0) {
            fprintf(stderr, "[PERF GPU] scene %.2f ms blit %.2f ms (%llu async samples)\n",
                    double(sPerfGpuSceneNs) / double(sPerfGpuSamples) / 1000000.0,
                    double(sPerfGpuBlitNs) / double(sPerfGpuSamples) / 1000000.0,
                    static_cast<unsigned long long>(sPerfGpuSamples));
        } else if (perf_gpu_queries_supported()) {
            fprintf(stderr, "[PERF GPU] results pending; CPU was not stalled\n");
        } else {
            fprintf(stderr, "[PERF GPU] timer queries unavailable on this OpenGL driver\n");
        }
        for (const PerfScope& scope : sPerfScopes) {
            if (scope.name && scope.draws) {
                fprintf(stderr, "[PERF ACTOR] %s %.0f draws %.0f verts\n", scope.name,
                        double(scope.draws) / frames, double(scope.vertices) / frames);
            }
        }
        fprintf(stderr, "[PERF FAST]");
        for (int i = 1; i < 4; ++i) {
            if (sPerfFastPathDraws[i]) {
                fprintf(stderr, " path%d=%.0f draws/%.2f verts", i,
                        double(sPerfFastPathDraws[i]) / frames,
                        double(sPerfFastPathVertices[i]) / double(sPerfFastPathDraws[i]));
            }
        }
        fprintf(stderr, " | prim tri=%.0f strip=%.0f fan=%.0f lines=%.0f line-strip=%.0f points=%.0f quads=%.0f\n",
                double(sPerfPrimitiveDraws[0]) / frames, double(sPerfPrimitiveDraws[1]) / frames,
                double(sPerfPrimitiveDraws[2]) / frames, double(sPerfPrimitiveDraws[3]) / frames,
                double(sPerfPrimitiveDraws[4]) / frames, double(sPerfPrimitiveDraws[5]) / frames,
                double(sPerfPrimitiveDraws[6]) / frames);
        for (int rank = 0; rank < 4; ++rank) {
            int best = -1;
            for (int i = 0; i < 128; ++i) {
                if (sPerfTevPatterns[i].count && (best < 0 || sPerfTevPatterns[i].count > sPerfTevPatterns[best].count))
                    best = i;
            }
            if (best < 0) break;
            const uint64_t key = sPerfTevPatterns[best].key;
            fprintf(stderr,
                    "[PERF TEV1] %.0f draws C=%u,%u,%u,%u A=%u,%u,%u,%u tex=%d tc=%d ras=%d op=%u/%u flags=0x%llx\n",
                    double(sPerfTevPatterns[best].count) / frames,
                    unsigned((key >> 0) & 15), unsigned((key >> 4) & 15),
                    unsigned((key >> 8) & 15), unsigned((key >> 12) & 15),
                    unsigned((key >> 16) & 7), unsigned((key >> 19) & 7),
                    unsigned((key >> 22) & 7), unsigned((key >> 25) & 7),
                    int((key >> 28) & 15) - 1, int((key >> 32) & 15) - 1,
                    int((key >> 36) & 3) - 1, unsigned((key >> 38) & 15),
                    unsigned((key >> 42) & 15),
                    static_cast<unsigned long long>(key >> 46));
            sPerfTevPatterns[best].count = 0;
        }
        for (int rank = 0; rank < 4; ++rank) {
            int best = -1;
            for (int i = 0; i < 128; ++i) {
                if (sPerfTevMultiPatterns[i].count
                    && (best < 0 || sPerfTevMultiPatterns[i].count > sPerfTevMultiPatterns[best].count))
                    best = i;
            }
            if (best < 0) break;
            const PerfTevMultiPattern& pattern = sPerfTevMultiPatterns[best];
            const uint64_t key = pattern.key;
            fprintf(stderr,
                    "[PERF TEVN] %.0f draws stages=%u final C=%u,%u,%u,%u A=%u,%u,%u,%u tex=%d tc=%d ras=%d op=%u/%u flags=0x%llx\n",
                    double(pattern.count) / frames, unsigned(pattern.stages),
                    unsigned((key >> 0) & 15), unsigned((key >> 4) & 15),
                    unsigned((key >> 8) & 15), unsigned((key >> 12) & 15),
                    unsigned((key >> 16) & 7), unsigned((key >> 19) & 7),
                    unsigned((key >> 22) & 7), unsigned((key >> 25) & 7),
                    int((key >> 28) & 15) - 1, int((key >> 32) & 15) - 1,
                    int((key >> 36) & 3) - 1, unsigned((key >> 38) & 15),
                    unsigned((key >> 42) & 15),
                    static_cast<unsigned long long>(key >> 46));
            sPerfTevMultiPatterns[best].count = 0;
        }
        frames = 0; elapsedMs = 0.0;
        sPerfDraws = sPerfFastDraws = sPerfVertices = sPerfDisplayLists = 0;
        sPerfSourcePrimitives = 0;
        sPerfPnMtxMask = 0;
        sPerfPnMtxMax = 0;
        sPerfDisplayListBytes = sPerfTextureUploads = 0;
        sPerfGpuSceneNs = sPerfGpuBlitNs = sPerfGpuSamples = 0;
        memset(sPerfTevStageDraws, 0, sizeof(sPerfTevStageDraws));
        memset(sPerfTevPatterns, 0, sizeof(sPerfTevPatterns));
        memset(sPerfTevMultiPatterns, 0, sizeof(sPerfTevMultiPatterns));
        memset(sPerfFastPathDraws, 0, sizeof(sPerfFastPathDraws));
        memset(sPerfFastPathVertices, 0, sizeof(sPerfFastPathVertices));
        memset(sPerfPrimitiveDraws, 0, sizeof(sPerfPrimitiveDraws));
        for (PerfScope& scope : sPerfScopes) {
            scope.draws = 0;
            scope.vertices = 0;
        }
    }

    SDL_Window* window = SDL_GL_GetCurrentWindow();
    if (window) SDL_GL_GetDrawableSize(window, &sDrawableWidth, &sDrawableHeight);
#ifdef __ANDROID__
    // SDL sigue diciendo el tamaño de la pantalla; la superficie real es la
    // que pidió pc_window (ver pc_android_request_surface_size).
    pc_android_surface_size(&sDrawableWidth, &sDrawableHeight);
#endif
    if (sNativeFramebufferReady && sDrawableWidth > 0 && sDrawableHeight > 0) {
        // Calculate aspect ratio for this frame
        float windowAspect = float(sDrawableWidth) / float(sDrawableHeight);
        sCurrentAspectRatio = calculate_aspect_ratio(sAspectRatioMode, windowAspect);
        
        // Scale the exact output viewport instead of first quantising the
        // aspect ratio through a 480-line base. The old calculation turned a
        // native 1920x1080 target into 1919x1080 and forced needless filtering.
        GLint outX, outY, outWidth, outHeight;
        calculate_output_area(sDrawableWidth, sDrawableHeight, sCurrentAspectRatio,
                              outX, outY, outWidth, outHeight);
        // Base del render: el área de salida, o la resolución pedida encajada
        // en su relación de aspecto (Android, donde la ventana no cambia).
        float baseWidth = float(outWidth), baseHeight = float(outHeight);
        if (sRenderResolutionW > 0 && sRenderResolutionH > 0) {
            baseHeight = float(sRenderResolutionH);
            baseWidth = baseHeight * sCurrentAspectRatio;
            if (baseWidth > float(sRenderResolutionW)) {
                baseWidth = float(sRenderResolutionW);
                baseHeight = baseWidth / sCurrentAspectRatio;
            }
        }
        const int wantedWidth = std::max(160, int(lroundf(baseWidth * sRenderScale)));
        const int wantedHeight = std::max(120, int(lroundf(baseHeight * sRenderScale)));
        if (wantedWidth != sRenderWidth || wantedHeight != sRenderHeight) {
            sRenderWidth = wantedWidth;
            sRenderHeight = wantedHeight;
            glBindTexture(GL_TEXTURE_2D, sNativeColorTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sRenderWidth, sRenderHeight,
                         0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            if (sDepthIsTexture) {
                glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
                depth_tex_image(sRenderWidth, sRenderHeight);
            } else {
                glBindRenderbuffer_ptr(GL_RENDERBUFFER, sNativeDepthStencil);
                glRenderbufferStorage_ptr(GL_RENDERBUFFER, depth_internal_format(),
                                          sRenderWidth, sRenderHeight);
            }
            glBindTexture(GL_TEXTURE_2D, 0);
            sBoundTextures[0] = 0;
            printf("[PC Port] Internal render resolution: %dx%d (aspect %.3f)\n", sRenderWidth, sRenderHeight, sCurrentAspectRatio);
        }
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    }

    if (!sVBO || !glBufferData_ptr) return;
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sVBO);
    if (vbo_upload_by_mapping() && glMapBufferRange_ptr && glFenceSync_ptr) {
        // Ruta de mapeo: anillo con fences, nunca huérfano (ver vbo_ring_*).
        vbo_ring_frame_begin();
    } else {
        // Orphan once per frame. The driver can finish consuming the previous
        // storage asynchronously while the CPU streams the next frame contiguously.
        glBufferData_ptr(GL_ARRAY_BUFFER, sVboCapacity, nullptr, GL_STREAM_DRAW);
        sVboWriteOffset = 0;
    }
    perf_gpu_scene_begin();
}

// ─── Post-processing ───
//
// Runs between the scene render and the blit to the window. The scene target
// is sampled into a second one of the same size, and the blit then reads from
// whichever holds the result.
//
// With nothing switched on this does nothing at all and the blit reads the
// scene directly, exactly as it did before: a fullscreen pass that only copies
// the frame is pure cost, and the port's reference GPU is a GTX 1050.

static PcPostEffects sPostEffects;
static PcPostEffects sPostCompiledFor;
static bool sPostCompiled = false;
static GLuint sPostFramebuffer = 0;
static GLuint sPostColorTexture = 0;
static GLuint sPostProgram = 0;
static GLuint sPostVAO = 0;
static int sPostWidth = 0, sPostHeight = 0;

// Bloom works at half resolution in two ping-pong targets: bright pass into
// the first, blur across into the second, blur down back into the first.
static GLuint sBloomFbo[2] = { 0, 0 };
static GLuint sBloomTex[2] = { 0, 0 };
static GLuint sBrightProgram = 0;
static GLuint sBlurProgram = 0;
static int sBloomWidth = 0, sBloomHeight = 0;

// Ambient occlusion shares the shape of the bloom chain: compute at half
// resolution, then blur the noise out with the same separable program.
static GLuint sAoFbo[2] = { 0, 0 };
static GLuint sAoTex[2] = { 0, 0 };
static GLuint sSsaoProgram = 0;
static GLuint sAoBlurProgram = 0;
static int sAoWidth = 0, sAoHeight = 0;

// Depth of field: same shape again -- half resolution, ping-pong, separable
// blur -- but the pair carries alpha, because coverage travels with the colour.
static GLuint sDofFbo[2] = { 0, 0 };
static GLuint sDofTex[2] = { 0, 0 };
static GLuint sDofCocProgram = 0;
static GLuint sDofBlurProgram = 0;
static int sDofWidth = 0, sDofHeight = 0;

// Where the lens is focused, in view units, pushed in by the game once a frame.
//
// Not part of PcPostEffects on purpose: that struct is compared field by field
// to decide whether the post-process program needs rebuilding, and a value that
// changes every frame would recompile the shader every frame.
//
// Zero means "nothing to focus on" -- the title screen, a cutscene, any moment
// with no captain in the world -- and the pass is skipped entirely rather than
// guessing a distance.
static float sDofFocusDistance = 0.0f;
// What the last pass's sub-chains actually managed, as opposed to what the
// settings asked for. "Switched on" and "built this frame" are different
// things, and the composite multiplies by the occlusion buffer either way.
static bool sLastAoReady = false;
static bool sLastBloomReady = false;
static bool sLastDofReady = false;

// The perspective projection's terms, kept for the post-process pass.
//
// Captured here rather than read back at the end of the frame, because by then
// the last projection set is the HUD's orthographic one and unprojecting screen
// pixels with that would place the whole world in the wrong position.
static float sViewInvP00 = 1.0f, sViewInvP11 = 1.0f;
static float sViewNear = 1.0f, sViewFar = 15000.0f;

void pc_gfx_set_post_effects(const PcPostEffects& fx) { sPostEffects = fx; }

void pc_gfx_set_dof_focus(float viewDistance)
{
    sDofFocusDistance = (viewDistance > 0.0f) ? viewDistance : 0.0f;
}

// Everything the depth-of-field pass decides from, once a second, with
// PIKMIN_DOF_DEBUG=1. The whole screen coming out blurred says the coverage is
// saturated, but not which of the three inputs is wrong: the focus the game
// pushes, the projection terms, or the depth read back from the buffer. This
// prints all three next to each other so the answer is read rather than
// guessed at.
static void dof_debug_report()
{
    static bool checked = false;
    static bool enabled = false;
    if (!checked) {
        checked = true;
        enabled = getenv("PIKMIN_DOF_DEBUG") != nullptr;
    }
    if (!enabled) return;

    static int frames = 0;
    if (++frames < 60) return;
    frames = 0;

    // Depth down a vertical line through the screen, raw and linearised.
    //
    // "Sharp at the top, blurred around the captain" is the blur running
    // backwards, and there are only two ways to get that: the depth buffer
    // read backwards, or a focus distance that does not match what the middle
    // of the screen actually is. Three points and their raw values separate
    // them -- distance has to grow from the bottom of the screen towards the
    // horizon, and the raw values have to stay inside the half range the
    // GameCube projection writes.
    struct Probe { const char* name; int x, y; float raw, view; };
    Probe probes[3] = {
        { "bottom", sRenderWidth / 2, sRenderHeight / 6,     0.0f, -1.0f },
        { "centre", sRenderWidth / 2, sRenderHeight / 2,     0.0f, -1.0f },
        { "top",    sRenderWidth / 2, sRenderHeight * 5 / 6, 0.0f, -1.0f },
    };
    float centreDepth = -1.0f;
    if (sDepthIsTexture && sNativeFramebuffer) {
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
        for (Probe& probe : probes) {
            glReadPixels(probe.x, probe.y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &probe.raw);
            const float ndc = probe.raw * 2.0f - 1.0f;
            const float denom = sViewNear - ndc * (sViewFar - sViewNear);
            probe.view = (std::fabs(denom) < 1e-6f) ? sViewFar
                                                    : (sViewNear * sViewFar) / denom;
        }
        centreDepth = probes[1].view;
        printf("[PC Port] DOF probe: bottom raw=%.5f d=%.1f | centre raw=%.5f d=%.1f | top raw=%.5f d=%.1f\n",
               probes[0].raw, probes[0].view, probes[1].raw, probes[1].view,
               probes[2].raw, probes[2].view);
    }

    const float sharp = sDofFocusDistance * sPostEffects.dofSharpFraction;
    const float falloff = sDofFocusDistance * sPostEffects.dofFalloffFraction;
    const float coc = (falloff > 0.0f && centreDepth >= 0.0f)
                          ? std::min(1.0f, std::max(0.0f,
                                (std::fabs(centreDepth - sDofFocusDistance) - sharp) / falloff))
                          : -1.0f;
    printf("[PC Port] DOF: focus=%.1f centreDepth=%.1f centreCoC=%.2f "
           "near=%.1f far=%.1f sharp=%.1f falloff=%.1f\n",
           sDofFocusDistance, centreDepth, coc, sViewNear, sViewFar, sharp, falloff);
    fflush(stdout);
}

static GLuint post_compile(GLenum type, const char* src, const char* what)
{
    GLuint sh = glCreateShader_ptr(type);
    glShaderSource_ptr(sh, 1, &src, nullptr);
    glCompileShader_ptr(sh);
    GLint ok = 0;
    if (glGetShaderiv_ptr) glGetShaderiv_ptr(sh, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[2048] = { 0 };
        if (glGetShaderInfoLog_ptr) glGetShaderInfoLog_ptr(sh, sizeof(log), nullptr, log);
        printf("[PC Port] post-process %s failed to compile: %s\n", what, log);
        glDeleteShader_ptr(sh);
        return 0;
    }
    return sh;
}

// Builds the program for the current effect set. Returns false if anything at
// all is missing, and every caller treats that as "no post-processing" rather
// than as a reason to stop drawing.
static bool post_ensure_program()
{
    if (sPostCompiled && sPostCompiledFor == sPostEffects && sPostProgram) return true;

    if (sPostProgram) {
        glDeleteProgram_ptr(sPostProgram);
        sPostProgram = 0;
    }
    sPostCompiled = false;

    if (!glCreateShader_ptr || !glCreateProgram_ptr || !glGenVertexArrays_ptr) return false;

    const std::string frag = pc_post_build_fragment_shader(sPostEffects);
    GLuint vs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "vertex shader");
    if (!vs) return false;
    GLuint fs = post_compile(GL_FRAGMENT_SHADER, frag.c_str(), "fragment shader");
    if (!fs) { glDeleteShader_ptr(vs); return false; }

    sPostProgram = glCreateProgram_ptr();
    glAttachShader_ptr(sPostProgram, vs);
    glAttachShader_ptr(sPostProgram, fs);
    glLinkProgram_ptr(sPostProgram);
    glDeleteShader_ptr(vs);
    glDeleteShader_ptr(fs);

    GLint linked = 0;
    if (glGetProgramiv_ptr) glGetProgramiv_ptr(sPostProgram, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char log[2048] = { 0 };
        if (glGetProgramInfoLog_ptr) glGetProgramInfoLog_ptr(sPostProgram, sizeof(log), nullptr, log);
        printf("[PC Port] post-process program failed to link: %s\n", log);
        glDeleteProgram_ptr(sPostProgram);
        sPostProgram = 0;
        return false;
    }

    if (!sPostVAO) glGenVertexArrays_ptr(1, &sPostVAO);

    // The bloom chain's two programs do not depend on the settings, only on
    // whether bloom is wanted at all, so they are built once and kept.
    const bool wantsBlur = pc_post_bloom_active(sPostEffects);
    if (pc_post_bloom_active(sPostEffects) && !sBrightProgram && glUniform2f_ptr) {
        const std::string brightSrc = pc_post_build_brightpass_shader();
        const std::string blurSrc   = pc_post_build_blur_shader();
        GLuint bvs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "bloom vertex shader");
        GLuint bfs = bvs ? post_compile(GL_FRAGMENT_SHADER, brightSrc.c_str(), "bright pass") : 0;
        GLuint lvs = bfs ? post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "blur vertex shader") : 0;
        GLuint lfs = lvs ? post_compile(GL_FRAGMENT_SHADER, blurSrc.c_str(), "blur") : 0;
        if (bvs && bfs && lvs && lfs) {
            sBrightProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sBrightProgram, bvs);
            glAttachShader_ptr(sBrightProgram, bfs);
            glLinkProgram_ptr(sBrightProgram);
            sBlurProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sBlurProgram, lvs);
            glAttachShader_ptr(sBlurProgram, lfs);
            glLinkProgram_ptr(sBlurProgram);
            GLint ok = 0;
            if (glGetProgramiv_ptr) {
                glGetProgramiv_ptr(sBrightProgram, GL_LINK_STATUS, &ok);
                if (ok == GL_TRUE) glGetProgramiv_ptr(sBlurProgram, GL_LINK_STATUS, &ok);
            }
            if (ok != GL_TRUE) {
                printf("[PC Port] bloom programs failed to link; bloom is off\n");
                glDeleteProgram_ptr(sBrightProgram); sBrightProgram = 0;
                glDeleteProgram_ptr(sBlurProgram); sBlurProgram = 0;
            }
        }
        if (bvs) glDeleteShader_ptr(bvs);
        if (bfs) glDeleteShader_ptr(bfs);
        if (lvs) glDeleteShader_ptr(lvs);
        if (lfs) glDeleteShader_ptr(lfs);
    }

    if (pc_post_ssao_active(sPostEffects) && !sSsaoProgram) {
        const std::string ssaoSrc = pc_post_build_ssao_shader();
        GLuint vs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "ssao vertex shader");
        GLuint fs = vs ? post_compile(GL_FRAGMENT_SHADER, ssaoSrc.c_str(), "ssao") : 0;
        if (vs && fs) {
            sSsaoProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sSsaoProgram, vs);
            glAttachShader_ptr(sSsaoProgram, fs);
            glLinkProgram_ptr(sSsaoProgram);
            GLint ok = 0;
            if (glGetProgramiv_ptr) glGetProgramiv_ptr(sSsaoProgram, GL_LINK_STATUS, &ok);
            if (ok != GL_TRUE) {
                printf("[PC Port] ssao program failed to link; ambient occlusion is off\n");
                glDeleteProgram_ptr(sSsaoProgram);
                sSsaoProgram = 0;
            }
        }
        if (vs) glDeleteShader_ptr(vs);
        if (fs) glDeleteShader_ptr(fs);
    }

    if (pc_post_ssao_active(sPostEffects) && !sAoBlurProgram && glUniform2f_ptr) {
        const std::string blurSrc = pc_post_build_ao_blur_shader();
        GLuint vs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "ao blur vertex shader");
        GLuint fs = vs ? post_compile(GL_FRAGMENT_SHADER, blurSrc.c_str(), "ao blur") : 0;
        if (vs && fs) {
            sAoBlurProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sAoBlurProgram, vs);
            glAttachShader_ptr(sAoBlurProgram, fs);
            glLinkProgram_ptr(sAoBlurProgram);
            GLint ok = 0;
            if (glGetProgramiv_ptr) glGetProgramiv_ptr(sAoBlurProgram, GL_LINK_STATUS, &ok);
            if (ok != GL_TRUE) {
                printf("[PC Port] ao blur failed to link; occlusion will be noisy\n");
                glDeleteProgram_ptr(sAoBlurProgram);
                sAoBlurProgram = 0;
            }
        }
        if (vs) glDeleteShader_ptr(vs);
        if (fs) glDeleteShader_ptr(fs);
    }

    // Bloom builds the blur alongside its bright pass. When occlusion is on
    // without it, the blur still has to exist -- it is what removes the noise
    // the rotated sample kernel deliberately introduces.
    if (wantsBlur && !sBlurProgram && glUniform2f_ptr) {
        const std::string blurSrc = pc_post_build_blur_shader();
        GLuint vs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "blur vertex shader");
        GLuint fs = vs ? post_compile(GL_FRAGMENT_SHADER, blurSrc.c_str(), "blur") : 0;
        if (vs && fs) {
            sBlurProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sBlurProgram, vs);
            glAttachShader_ptr(sBlurProgram, fs);
            glLinkProgram_ptr(sBlurProgram);
            GLint ok = 0;
            if (glGetProgramiv_ptr) glGetProgramiv_ptr(sBlurProgram, GL_LINK_STATUS, &ok);
            if (ok != GL_TRUE) {
                glDeleteProgram_ptr(sBlurProgram);
                sBlurProgram = 0;
            }
        }
        if (vs) glDeleteShader_ptr(vs);
        if (fs) glDeleteShader_ptr(fs);
    }

    sPostCompiledFor = sPostEffects;
    sPostCompiled = true;
    // Once per change of settings, not per frame. Says which effects the pass
    // is actually running, which is otherwise only visible by looking hard at
    // the picture.
    if (pc_post_dof_active(sPostEffects) && !sDofCocProgram) {
        const std::string src = pc_post_build_dof_coc_shader();
        GLuint vs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "dof coverage vertex shader");
        GLuint fs = vs ? post_compile(GL_FRAGMENT_SHADER, src.c_str(), "dof coverage") : 0;
        if (vs && fs) {
            sDofCocProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sDofCocProgram, vs);
            glAttachShader_ptr(sDofCocProgram, fs);
            glLinkProgram_ptr(sDofCocProgram);
            GLint ok = 0;
            if (glGetProgramiv_ptr) glGetProgramiv_ptr(sDofCocProgram, GL_LINK_STATUS, &ok);
            if (ok != GL_TRUE) {
                printf("[PC Port] dof coverage program failed to link; depth of field is off\n");
                glDeleteProgram_ptr(sDofCocProgram);
                sDofCocProgram = 0;
            }
        }
        if (vs) glDeleteShader_ptr(vs);
        if (fs) glDeleteShader_ptr(fs);
    }
    if (pc_post_dof_active(sPostEffects) && !sDofBlurProgram) {
        const std::string src = pc_post_build_dof_blur_shader();
        GLuint vs = post_compile(GL_VERTEX_SHADER, pc_post_vertex_shader(), "dof blur vertex shader");
        GLuint fs = vs ? post_compile(GL_FRAGMENT_SHADER, src.c_str(), "dof blur") : 0;
        if (vs && fs) {
            sDofBlurProgram = glCreateProgram_ptr();
            glAttachShader_ptr(sDofBlurProgram, vs);
            glAttachShader_ptr(sDofBlurProgram, fs);
            glLinkProgram_ptr(sDofBlurProgram);
            GLint ok = 0;
            if (glGetProgramiv_ptr) glGetProgramiv_ptr(sDofBlurProgram, GL_LINK_STATUS, &ok);
            if (ok != GL_TRUE) {
                printf("[PC Port] dof blur failed to link; depth of field is off\n");
                glDeleteProgram_ptr(sDofBlurProgram);
                sDofBlurProgram = 0;
            }
        }
        if (vs) glDeleteShader_ptr(vs);
        if (fs) glDeleteShader_ptr(fs);
    }

    printf("[PC Port] Post-process pass:%s%s%s%s%s\n",
           sPostEffects.fxaa ? " FXAA" : "",
           pc_post_ssao_active(sPostEffects) ? " SSAO" : "",
           pc_post_dof_active(sPostEffects) ? " depth-of-field" : "",
           pc_post_bloom_active(sPostEffects) ? " bloom" : "",
           sPostEffects.colourGrading ? " colour-grading" : "");
    fflush(stdout);
    return true;
}

// Keeps the destination the same size as the scene target, which the render
// scale changes at runtime.
static bool post_ensure_target()
{
    if (!glGenFramebuffers_ptr || !glBindFramebuffer_ptr || !glFramebufferTexture2D_ptr) return false;
    if (!sPostFramebuffer) {
        glGenFramebuffers_ptr(1, &sPostFramebuffer);
        glGenTextures(1, &sPostColorTexture);
        sPostWidth = sPostHeight = 0;
    }
    if (sPostWidth != sRenderWidth || sPostHeight != sRenderHeight) {
        glBindTexture(GL_TEXTURE_2D, sPostColorTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sRenderWidth, sRenderHeight,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sPostFramebuffer);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                   sPostColorTexture, 0);
        const bool complete = glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindTexture(GL_TEXTURE_2D, 0);
        sBoundTextures[0] = 0;
        if (!complete) return false;
        sPostWidth = sRenderWidth;
        sPostHeight = sRenderHeight;
    }
    return true;
}

// Half-resolution ping-pong pair for the bloom chain.
static bool bloom_ensure_targets()
{
    const int w = std::max(1, sRenderWidth / 2);
    const int h = std::max(1, sRenderHeight / 2);
    if (!sBloomFbo[0]) {
        glGenFramebuffers_ptr(2, sBloomFbo);
        glGenTextures(2, sBloomTex);
        sBloomWidth = sBloomHeight = 0;
    }
    if (sBloomWidth == w && sBloomHeight == h) return true;
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, sBloomTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        // Linear, and clamped: the blur reads between texels on purpose, and
        // wrapping would drag the far edge of the screen into the near one.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sBloomFbo[i]);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sBloomTex[i], 0);
        if (glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindTexture(GL_TEXTURE_2D, 0);
            sBoundTextures[0] = 0;
            return false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    sBoundTextures[0] = 0;
    sBloomWidth = w;
    sBloomHeight = h;
    return true;
}

// Half-resolution ping-pong pair for depth of field. RGBA8 like the bloom pair,
// but the alpha channel is load-bearing here: it carries the circle of
// confusion through the blur.
// One white pixel, made once. The neutral value for anything the composite
// multiplies by.
static GLuint post_white_texture()
{
    static GLuint tex = 0;
    if (!tex) {
        const unsigned char white[4] = { 255, 255, 255, 255 };
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        sBoundTextures[0] = 0;
    }
    return tex;
}

static bool dof_ensure_targets()
{
    const int w = std::max(1, sRenderWidth / 2);
    const int h = std::max(1, sRenderHeight / 2);
    if (!sDofFbo[0]) {
        glGenFramebuffers_ptr(2, sDofFbo);
        glGenTextures(2, sDofTex);
        sDofWidth = sDofHeight = 0;
    }
    if (sDofWidth == w && sDofHeight == h) return true;
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, sDofTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sDofFbo[i]);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sDofTex[i], 0);
        if (glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindTexture(GL_TEXTURE_2D, 0);
            sBoundTextures[0] = 0;
            return false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    sBoundTextures[0] = 0;
    sDofWidth = w;
    sDofHeight = h;
    return true;
}

// Coverage pass, then the separable blur, however many times the setting asks
// for. Leaves the result in sDofTex[0].
static bool dof_build()
{
    if (!sDofCocProgram || !sDofBlurProgram) return false;
    if (!dof_ensure_targets()) return false;

    glViewport(0, 0, sDofWidth, sDofHeight);
    glBindVertexArray_ptr(sPostVAO);

    // Scene and depth -> [0], premultiplied by coverage.
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sDofFbo[0]);
    post_bind_program(sDofCocProgram);
    if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sNativeColorTexture);
    if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sDofCocProgram, "uScene"), 0);
    if (glActiveTexture_ptr) {
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
        if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sDofCocProgram, "uDepth"), 1);
        glActiveTexture_ptr(GL_TEXTURE0);
    }
    if (glUniform4f_ptr) {
        glUniform4f_ptr(glGetUniformLocation_ptr(sDofCocProgram, "uProjInfo"),
                        sViewInvP00, sViewInvP11, sViewNear, sViewFar);
        glUniform4f_ptr(glGetUniformLocation_ptr(sDofCocProgram, "uDofFocus"),
                        sDofFocusDistance, sPostEffects.dofSharpFraction,
                        sPostEffects.dofFalloffFraction, sPostEffects.dofStrength);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Blur across and down, once per iteration. Running the same five-tap
    // kernel again is what widens the blur: stretching its offsets instead
    // would sample past its own weights and band.
    post_bind_program(sDofBlurProgram);
    const GLint blurSource = glGetUniformLocation_ptr(sDofBlurProgram, "uSource");
    const GLint blurStep = glGetUniformLocation_ptr(sDofBlurProgram, "uBlurStep");
    const int iterations = std::max(1, std::min(sPostEffects.dofIterations, 4));
    for (int pass = 0; pass < iterations; pass++) {
        for (int axis = 0; axis < 2; axis++) {
            const int from = axis == 0 ? 0 : 1;
            const int to   = axis == 0 ? 1 : 0;
            glBindFramebuffer_ptr(GL_FRAMEBUFFER, sDofFbo[to]);
            glBindTexture(GL_TEXTURE_2D, sDofTex[from]);
            if (glUniform1i_ptr) glUniform1i_ptr(blurSource, 0);
            if (glUniform2f_ptr && blurStep >= 0) {
                glUniform2f_ptr(blurStep,
                                axis == 0 ? 1.0f / float(sDofWidth) : 0.0f,
                                axis == 0 ? 0.0f : 1.0f / float(sDofHeight));
            }
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }

    restore_stream_vao();
    return true;
}

// Bright pass, blur across, blur down. Leaves the result in sBloomTex[0].
static bool bloom_build()
{
    if (!sBrightProgram || !sBlurProgram) return false;
    if (!bloom_ensure_targets()) return false;

    glViewport(0, 0, sBloomWidth, sBloomHeight);
    glBindVertexArray_ptr(sPostVAO);

    // Bright pass: scene -> [0]
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sBloomFbo[0]);
    post_bind_program(sBrightProgram);
    if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sNativeColorTexture);
    if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sBrightProgram, "uScene"), 0);
    if (glUniform1f_ptr) {
        glUniform1f_ptr(glGetUniformLocation_ptr(sBrightProgram, "uThreshold"),
                        sPostEffects.bloomThreshold);
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // Blur across: [0] -> [1], then down: [1] -> [0]
    post_bind_program(sBlurProgram);
    const GLint blurSource = glGetUniformLocation_ptr(sBlurProgram, "uSource");
    const GLint blurStep = glGetUniformLocation_ptr(sBlurProgram, "uBlurStep");
    for (int axis = 0; axis < 2; axis++) {
        const int from = axis == 0 ? 0 : 1;
        const int to   = axis == 0 ? 1 : 0;
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sBloomFbo[to]);
        glBindTexture(GL_TEXTURE_2D, sBloomTex[from]);
        if (glUniform1i_ptr) glUniform1i_ptr(blurSource, 0);
        if (glUniform2f_ptr && blurStep >= 0) {
            glUniform2f_ptr(blurStep,
                            axis == 0 ? 1.0f / float(sBloomWidth) : 0.0f,
                            axis == 0 ? 0.0f : 1.0f / float(sBloomHeight));
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    restore_stream_vao();
    return true;
}

// Half-resolution pair for occlusion and its denoise blur.
static bool ao_ensure_targets()
{
    // Full resolution, matching the depth buffer it reads. At half resolution
    // the pass point-samples one depth pixel in four -- depth is NEAREST, as it
    // must be -- and which one it lands on changes as geometry moves under a
    // pixel. That is an aliasing pattern that moves with the camera, which is
    // what the crawling lines and the shimmering grass looked like.
    const int w = std::max(1, sRenderWidth);
    const int h = std::max(1, sRenderHeight);
    if (!sAoFbo[0]) {
        glGenFramebuffers_ptr(2, sAoFbo);
        glGenTextures(2, sAoTex);
        sAoWidth = sAoHeight = 0;
    }
    if (sAoWidth == w && sAoHeight == h) return true;
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, sAoTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sAoFbo[i]);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sAoTex[i], 0);
        if (glCheckFramebufferStatus_ptr(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            glBindTexture(GL_TEXTURE_2D, 0);
            sBoundTextures[0] = 0;
            return false;
        }
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    sBoundTextures[0] = 0;
    sAoWidth = w;
    sAoHeight = h;
    return true;
}

// Occlusion into [0], blurred across into [1] and back down into [0].
static bool ao_build()
{
    if (!sSsaoProgram || !sAoBlurProgram) return false;
    // Depth is the whole input. Without a depth texture there is nothing to
    // compute from, which is exactly the fallback case pc_gfx warned about.
    if (!sDepthIsTexture || !sNativeDepthTexture) return false;
    if (!ao_ensure_targets()) return false;

    glViewport(0, 0, sAoWidth, sAoHeight);
    glBindVertexArray_ptr(sPostVAO);

    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sAoFbo[0]);
    post_bind_program(sSsaoProgram);
    if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
    if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sSsaoProgram, "uDepth"), 0);
    if (glUniform4f_ptr) {
        glUniform4f_ptr(glGetUniformLocation_ptr(sSsaoProgram, "uProjInfo"),
                        sViewInvP00, sViewInvP11, sViewNear, sViewFar);
        // The bias discards the shallow self-occlusion that the faceted
        // derivative normal produces on flat ground.
        glUniform4f_ptr(glGetUniformLocation_ptr(sSsaoProgram, "uAOParams"),
                        // Bias (z): samples within ~6 degrees of the surface
                        // plane are not occluders. At 0.02 the small normal
                        // error from stepped depth still counted, in bands.
                        sPostEffects.ssaoRadius, sPostEffects.ssaoIntensity, 0.1f, 0.0f);
    }
    if (glUniform2f_ptr) {
        // The neighbour taps that build the normal step one pixel of this
        // target, not of the full-resolution scene.
        glUniform2f_ptr(glGetUniformLocation_ptr(sSsaoProgram, "uTexel"),
                        1.0f / float(sAoWidth), 1.0f / float(sAoHeight));
    }
    glDrawArrays(GL_TRIANGLES, 0, 3);

    post_bind_program(sAoBlurProgram);
    const GLint blurSource = glGetUniformLocation_ptr(sAoBlurProgram, "uSource");
    const GLint blurDepth = glGetUniformLocation_ptr(sAoBlurProgram, "uDepth");
    const GLint blurStep = glGetUniformLocation_ptr(sAoBlurProgram, "uBlurStep");
    if (glUniform4f_ptr) {
        glUniform4f_ptr(glGetUniformLocation_ptr(sAoBlurProgram, "uProjInfo"),
                        sViewInvP00, sViewInvP11, sViewNear, sViewFar);
    }
    // Depth stays bound on its own unit for the whole blur: both axes weight
    // their taps by it, and rebinding per pass would only cost state changes.
    if (glActiveTexture_ptr) {
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
        if (glUniform1i_ptr) glUniform1i_ptr(blurDepth, 1);
        glActiveTexture_ptr(GL_TEXTURE0);
    }
    for (int axis = 0; axis < 2; axis++) {
        const int from = axis == 0 ? 0 : 1;
        const int to   = axis == 0 ? 1 : 0;
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sAoFbo[to]);
        glBindTexture(GL_TEXTURE_2D, sAoTex[from]);
        if (glUniform1i_ptr) glUniform1i_ptr(blurSource, 0);
        if (glUniform2f_ptr && blurStep >= 0) {
            glUniform2f_ptr(blurStep,
                            axis == 0 ? 1.0f / float(sAoWidth) : 0.0f,
                            axis == 0 ? 0.0f : 1.0f / float(sAoHeight));
        }
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    restore_stream_vao();
    return true;
}

static void gl_program_cache_invalidate();

// Returns the framebuffer the blit should read from.
// allowDof: the HUD/ortho path has a real world depth buffer and a captain
// to focus on. Late present (file select, title, cutscenes with no interface)
// applies the pass to the whole picture, including 2D chrome -- leftover
// gameplay focus then blurred the menus after exiting a stage.
// Pikmin 2 solo aplica postproceso en las secciones de partida: en el arranque
// y los menus de titulo, bloom/SSAO dejaban sin pintar el 2D posterior.
// PIKMIN_POST_ALWAYS: postproceso tambien fuera de partida (pruebas en el titulo).
static const bool sPostAlways = getenv("PIKMIN_POST_ALWAYS") != nullptr;
#if PIKI_P2_HOST
static bool sPostAllowed = sPostAlways; // el arranque ocurre antes de la primera createSection
#else
static bool sPostAllowed = true;
#endif
void pc_gfx_set_post_allowed(int allowed) {
    sPostAllowed = allowed != 0 || sPostAlways;
    if (getenv("PIKMIN_POST_DEBUG")) printf("[PC Port] post allowed=%d\n", sPostAllowed ? 1 : 0);
}
// Bloqueo aparte del de la seccion: los videos THP se reproducen dentro de la
// partida y el postproceso los dejaba en negro. Lo activa pc_thp al abrir.
static bool sPostSuppressed = false;
void pc_gfx_set_post_suppressed(int suppressed) {
    sPostSuppressed = suppressed != 0;
    if (getenv("PIKMIN_POST_DEBUG")) printf("[PC Port] post suppressed=%d\n", sPostSuppressed ? 1 : 0);
}
static bool sPostRanThisFrame = false;
// Declarados aquí (antes del módulo de sombras) en vez de junto a su uso.
static PcGfxPipelineState sPipelineState = { GX_TRUE, GX_LEQUAL, GX_TRUE, GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY, GX_CULL_BACK };
static constexpr int kPaletteSlots = 21;
static int sPaletteSlotsNeeded = kPaletteSlots;
#include "pc_gfx_shadows.inc"

static GLuint post_apply(bool allowDof)
{
    if (!sPostAllowed || sPostSuppressed || !pc_post_any_enabled(sPostEffects)) return sNativeFramebuffer;
    // An effect that needs depth cannot run when the driver made us fall back
    // to a renderbuffer. Drop the pass rather than sample a texture that is
    // not there.
    if (pc_post_needs_depth(sPostEffects) && !sDepthIsTexture) return sNativeFramebuffer;
    // Shader sources are std::string: also reached from pc_gfx_world_done and
    // pc_gfx_set_projection mid-frame, where the section heap may be full.
    PcGfxHostAllocScope hostAllocScope;
    // Any of these failing means no post-processing, never a stopped frame.
    if (!post_ensure_target() || !post_ensure_program()) return sNativeFramebuffer;

    // GX can leave colour or alpha writes disabled for the last scene draw.
    // Every post target is a replacement image, so inheriting that mask can
    // silently preserve stale channels (or the entire previous frame). Keep
    // the GX mirrors untouched: save the actual GL mask and restore it after
    // the chain, including when this function is called from present().
    GLboolean colourMask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
    glGetBooleanv(GL_COLOR_WRITEMASK, colourMask);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Every pass below draws a full-screen triangle, and none of them wants the
    // game's pipeline state. This has to happen BEFORE the chains, not just
    // before the composite: they are draws too.
    //
    // It used to sit after them, which was invisible while the pass ran at the
    // end of the frame -- by then the game had left the scissor at full screen.
    // Run in the middle of the frame it is fatal: the title screen's scissor
    // rejected the occlusion pass's fragments, so the buffer kept the zero it
    // was allocated with, and the composite multiplied the whole picture by it.
    // The screen went black with the occlusion pass reporting success, because
    // it had issued its draw and the draw had been thrown away.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    // Bloom runs its own chain first, at half resolution, leaving its result in
    // sBloomTex[0] for the main pass to add in. A failure here is not fatal:
    // the composite simply reads a texture that contributes nothing.
    // Sombras primero: su máscara la multiplica el compuesto igual que la
    // oclusión, y ambas leen la profundidad del mundo tal cual quedó.
    const bool shadowReady = shadow_build();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    bool aoReady = false;
    if (pc_post_ssao_active(sPostEffects)) {
        aoReady = ao_build();
    }
    sLastAoReady = aoReady;

    // Depth of field needs somewhere to focus. Without a captain in the world
    // -- the title screen, a cutscene -- there is no honest answer, so the
    // chain is skipped and the composite is told to blur nothing.
    bool dofReady = false;
    dof_debug_report();
    if (allowDof && pc_post_dof_active(sPostEffects) && sDofFocusDistance > 0.0f) {
        dofReady = dof_build();
    }

    bool bloomReady = false;
    if (pc_post_bloom_active(sPostEffects)) {
        bloomReady = bloom_build();
    }
    sLastBloomReady = bloomReady;
    sLastDofReady = dofReady;

    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sPostFramebuffer);
    glViewport(0, 0, sRenderWidth, sRenderHeight);

    post_bind_program(sPostProgram);
    if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sNativeColorTexture);
    if (glUniform1i_ptr) {
        glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uScene"), 0);
    }
    if (pc_post_needs_depth(sPostEffects) && glActiveTexture_ptr) {
        glActiveTexture_ptr(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, sNativeDepthTexture);
        if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uDepth"), 1);
        glActiveTexture_ptr(GL_TEXTURE0);
    }
    if (dofReady && glActiveTexture_ptr) {
        glActiveTexture_ptr(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, sDofTex[0]);
        if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uDof"), 4);
        glActiveTexture_ptr(GL_TEXTURE0);
        if (glUniform4f_ptr) {
            glUniform4f_ptr(glGetUniformLocation_ptr(sPostProgram, "uProjInfo"),
                            sViewInvP00, sViewInvP11, sViewNear, sViewFar);
            glUniform4f_ptr(glGetUniformLocation_ptr(sPostProgram, "uDofFocus"),
                            sDofFocusDistance, sPostEffects.dofSharpFraction,
                            sPostEffects.dofFalloffFraction, sPostEffects.dofStrength);
        }
    } else if (pc_post_dof_active(sPostEffects) && glUniform4f_ptr) {
        // The shader still samples uDof. An unbound unit is undefined (and on
        // some drivers, leftover TEV memory). Neutral white with zero strength
        // keeps the mix from seeing garbage when the chain did not run.
        if (glActiveTexture_ptr) {
            glActiveTexture_ptr(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, post_white_texture());
            if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uDof"), 4);
            glActiveTexture_ptr(GL_TEXTURE0);
        }
        glUniform4f_ptr(glGetUniformLocation_ptr(sPostProgram, "uDofFocus"),
                        1.0f, 1.0f, 1.0f, 0.0f);
    }
    if (pc_post_ssao_active(sPostEffects) && glActiveTexture_ptr) {
        // The shader was generated with "c *= texture(uAO, vUV).r" in it and
        // will run that whether or not the chain built this frame. Bloom's
        // equivalent is defused by zeroing its intensity; occlusion has no
        // intensity uniform to zero, so it gets a one-pixel white texture,
        // which multiplies by one. Without it a failed build does not mean
        // "no occlusion", it means a black screen -- sampling an unbound unit
        // returns zero.
        glActiveTexture_ptr(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, aoReady ? sAoTex[0] : post_white_texture());
        if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uAO"), 3);
        glActiveTexture_ptr(GL_TEXTURE0);
    }
    if (pc_post_shadows_active(sPostEffects) && glActiveTexture_ptr) {
        // Igual que la oclusión: blanco (x1) si la máscara no se construyó.
        glActiveTexture_ptr(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, shadowReady ? sShadowMaskTex : post_white_texture());
        if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uShadow"), 5);
        glActiveTexture_ptr(GL_TEXTURE0);
    }
    if (bloomReady && glActiveTexture_ptr) {
        glActiveTexture_ptr(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, sBloomTex[0]);
        if (glUniform1i_ptr) glUniform1i_ptr(glGetUniformLocation_ptr(sPostProgram, "uBloom"), 2);
        glActiveTexture_ptr(GL_TEXTURE0);
        if (glUniform1f_ptr) {
            glUniform1f_ptr(glGetUniformLocation_ptr(sPostProgram, "uBloomIntensity"),
                            sPostEffects.bloomIntensity);
        }
    } else if (pc_post_bloom_active(sPostEffects) && glUniform1f_ptr) {
        // The shader was built with the composite in it, so silence it rather
        // than sampling a texture that was never filled.
        glUniform1f_ptr(glGetUniformLocation_ptr(sPostProgram, "uBloomIntensity"), 0.0f);
    }
    if (sPostEffects.fxaa && glUniform4f_ptr) {
        glUniform4f_ptr(glGetUniformLocation_ptr(sPostProgram, "uTexelSize"),
                        1.0f / float(sRenderWidth), 1.0f / float(sRenderHeight),
                        float(sRenderWidth), float(sRenderHeight));
    }
    if (sPostEffects.colourGrading && glUniform1f_ptr) {
        glUniform1f_ptr(glGetUniformLocation_ptr(sPostProgram, "uGamma"), sPostEffects.gamma);
        glUniform1f_ptr(glGetUniformLocation_ptr(sPostProgram, "uBrightness"), sPostEffects.brightness);
        glUniform1f_ptr(glGetUniformLocation_ptr(sPostProgram, "uSaturation"), sPostEffects.saturation);
    }

    // The scene leaves vertex arrays and buffers bound. An empty vertex array
    // object detaches all of it, so the pass cannot be disturbed by whatever
    // the last draw was doing; the geometry comes from gl_VertexID.
    glBindVertexArray_ptr(sPostVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    restore_stream_vao();

    glUseProgram_ptr(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    // present() runs this pass on frames with no interface (the H4M player
    // is one). Leaving sCurrentProgram and sLastTevKeyValid pointing at a
    // program that is no longer bound made the next GX draw skip glUseProgram,
    // write uniforms to program 0 (GL_INVALID_OPERATION), and cover the
    // framebuffer with nothing. Measured: program=0, glerr=0x502, one-frame
    // flashes only when the TEV key changed and forced a rebind.
    for (int i = 0; i < 8; i++) sBoundTextures[i] = 0;
    gl_program_cache_invalidate();
    // Same class of bug as the program cache above: the pass has just
    // disabled blend/depth/cull and resized the viewport, so the GX
    // setters' redundancy guards are now lying.
    invalidate_gl_pipeline_guards();
    glColorMask(colourMask[0], colourMask[1], colourMask[2], colourMask[3]);
    return sPostFramebuffer;
}

// Runs the post-process where the world ends, instead of at the end of the
// frame, and puts the result back so the interface draws on top of it.
//
// The frame is world-then-interface, measured with PIKMIN_PROJ_DEBUG:
//
//   perspective, 336 draws   the world
//   orthographic             the interface starts here, and never goes back
//
// Running at the end of the frame meant post-processing a picture that already
// had the interface painted into it, and reading a depth buffer the interface
// had already overwritten -- the counter at the bottom of the screen came out
// as blurred as the ground behind it, and the depth under it was nonsense.
//
// The GL state is saved and put back exactly rather than invalidated. The
// port's setters all carry redundancy guards -- viewport, scissor, blend,
// depth, cull -- and a guard that has been lied to silently skips the call
// that would have corrected it.

// Defined after the TEV program cache it clears. Zeroing sCurrentProgram alone
// is not enough: use_program_for_current_state() has a fast path that returns
// without binding anything when the material key is unchanged and the current
// program is not the ubershader -- which is exactly what a zeroed cache looks
// like. It would have kept the post-process's own program bound for the
// interface, or bound nothing at all.

static void post_apply_before_interface()
{
    // Escape hatch back to the old behaviour, for telling a fault caused by
    // this pass apart from one that merely shows up at the same time.
    static bool checked = false;
    static bool disabled = false;
    if (!checked) {
        checked = true;
        disabled = getenv("PIKMIN_POST_LATE") != nullptr;
        if (disabled) printf("[PC Port] PIKMIN_POST_LATE: post-processing at the end of the frame\n");
    }
    if (disabled) return;

    if (sPostRanThisFrame) return;
    if (!sNativeFramebufferReady || !glBindFramebuffer_ptr || !glBlitFramebuffer_ptr) return;
    if (!sPostAllowed || sPostSuppressed || !pc_post_any_enabled(sPostEffects)) return;

    // The port batches draws. Anything still pending belongs to the world, and
    // running the pass first would leave it to be drawn over the top of a
    // finished image.
    pc_gfx_flush_batch();

    GLint viewport[4] = { 0, 0, 0, 0 };
    GLint scissor[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_SCISSOR_BOX, scissor);
    const GLboolean hadScissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean hadDepth   = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean hadBlend   = glIsEnabled(GL_BLEND);
    const GLboolean hadCull    = glIsEnabled(GL_CULL_FACE);

    // What the pass was handed and what it produced, once a second, with
    // PIKMIN_POST_DEBUG=1. A black title screen has two possible causes that
    // look identical on screen -- the pass output black, or something drawn
    // afterwards wiped it -- and the only way to tell them apart is to look at
    // the pixel on both sides of the pass.
    static bool postDbgChecked = false;
    static bool postDbg = false;
    static int postDbgGate = 0;
    static int postDbgEvery = 60;
    if (!postDbgChecked) {
        postDbgChecked = true;
        postDbg = getenv("PIKMIN_POST_DEBUG") != nullptr;
        // PIKMIN_POST_DEBUG=N: informa cada N pasadas (por defecto 60).
        if (postDbg && atoi(getenv("PIKMIN_POST_DEBUG")) > 0) postDbgEvery = atoi(getenv("PIKMIN_POST_DEBUG"));
    }
    const bool report = postDbg && (++postDbgGate % postDbgEvery == 0);
    unsigned char before[4] = { 0, 0, 0, 0 };
    if (report) {
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
        glReadPixels(sRenderWidth / 2, sRenderHeight / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, before);
    }

    const GLuint produced = post_apply(true);
    sPostRanThisFrame = true;

    if (report) {
        unsigned char after[4] = { 0, 0, 0, 0 };
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, produced);
        glReadPixels(sRenderWidth / 2, sRenderHeight / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, after);
        // The occlusion buffer itself: 255 is "nothing occluded", 0 is "the
        // composite is about to multiply the whole screen by zero".
        unsigned char ao[4] = { 0, 0, 0, 0 };
        if (sLastAoReady && sAoTex[0]) {
            glBindFramebuffer_ptr(GL_FRAMEBUFFER, sAoFbo[0]);
            glReadPixels(sAoWidth / 2, sAoHeight / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, ao);
        }
        // The depth at the same pixel, raw, and the terms the occlusion pass was
        // handed. An occlusion of exactly zero at intensity 0.5 is arithmetically
        // impossible -- the term is capped at the intensity -- so either the
        // depth is not what the shader assumes or the uniforms are not what this
        // code thinks it sent.
        float rawDepth = -1.0f;
        if (sDepthIsTexture) {
            glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
            glReadPixels(sRenderWidth / 2, sRenderHeight / 2, 1, 1,
                         GL_DEPTH_COMPONENT, GL_FLOAT, &rawDepth);
        }
        printf("[PC Port] POST: rawDepth=%.5f projInfo=(%.4f, %.4f, %.1f, %.1f) aoIntensity=%.2f aoRadius=%.1f\n",
               rawDepth, sViewInvP00, sViewInvP11, sViewNear, sViewFar,
               sPostEffects.ssaoIntensity, sPostEffects.ssaoRadius);
        printf("[PC Port] POST: scene=(%d,%d,%d) -> result=(%d,%d,%d) | aoOn=%d aoBuilt=%d aoValue=%d "
               "bloomOn=%d bloomBuilt=%d dofOn=%d dofBuilt=%d focus=%.1f\n",
               before[0], before[1], before[2], after[0], after[1], after[2],
               pc_post_ssao_active(sPostEffects) ? 1 : 0, sLastAoReady ? 1 : 0, ao[0],
               pc_post_bloom_active(sPostEffects) ? 1 : 0, sLastBloomReady ? 1 : 0,
               pc_post_dof_active(sPostEffects) ? 1 : 0, sLastDofReady ? 1 : 0,
               sDofFocusDistance);
        fflush(stdout);
    }

    if (produced != sNativeFramebuffer) {
        // Back into the framebuffer the game is still drawing into. Colour
        // only: the depth buffer is untouched by the pass and the interface
        // behind us may still want to test against it.
        glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, produced);
        glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, sNativeFramebuffer);
        glBlitFramebuffer_ptr(0, 0, sRenderWidth, sRenderHeight,
                              0, 0, sRenderWidth, sRenderHeight,
                              GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }

    if (report) {
        unsigned char back[4] = { 0, 0, 0, 0 };
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
        glReadPixels(sRenderWidth / 2, sRenderHeight / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, back);
        printf("[PC Port] POST: after blit back, scene=(%d,%d,%d)\n", back[0], back[1], back[2]);
        fflush(stdout);
    }

    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    filesel_debug_note_ortho_post();
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
    if (hadScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (hadDepth)   glEnable(GL_DEPTH_TEST);   else glDisable(GL_DEPTH_TEST);
    if (hadBlend)   glEnable(GL_BLEND);        else glDisable(GL_BLEND);
    if (hadCull)    glEnable(GL_CULL_FACE);    else glDisable(GL_CULL_FACE);

    // What cannot be put back has to be admitted to. The pass bound its own
    // program and left textures on units 1 to 4; the caches that mirror those
    // are cleared so the next draw programs them again rather than trusting a
    // record of state that is no longer true.
    if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
    for (int i = 0; i < 8; i++) sBoundTextures[i] = 0;
    gl_program_cache_invalidate();
    invalidate_uniform_cache();
}

// Pikmin 2: el juego llama aqui al terminar draw3D, antes de cualquier 2D.
#if PIKI_P2_HOST
static bool sP2FrameInterface = false;
#endif
extern "C" void pc_gfx_world_done(void);

// Pikmin 2: la interfaz (J2D) empieza. Si ya hubo mundo 3D en el frame y el
// postproceso no ha corrido (pantallas sin pc_gfx_world_done: dialogos,
// menus sobre la escena), se aplica ahora, debajo de la interfaz.
extern "C" void pc_gfx_p2_interface_begin(void)
{
#if PIKI_P2_HOST
    // Solo en el primer setPort del frame: despues ya se han dibujado cosas
    // 2D con perspectiva y contarian como "mundo".
    if (sP2FrameInterface) return;
    sP2FrameInterface = true;
    if (!sPostRanThisFrame) pc_gfx_world_done();
#endif
}

extern "C" void pc_gfx_world_done(void)
{
    persp_account_draws();
    sCurDrawStart = sPerfDraws;
    if (sBestDraws <= 0) return;
    sViewInvP00 = sBestInvP00;
    sViewInvP11 = sBestInvP11;
    sViewNear   = sBestNear;
    sViewFar    = sBestFar;
    sPendingValid = false;
    sBestDraws = -1;
    post_apply_before_interface();
}

extern "C" int pc_gfx_trace_j3d(void) {
    static const unsigned f = getenv("PIKMIN_J3D_TRACE_FRAME") ? (unsigned)atoi(getenv("PIKMIN_J3D_TRACE_FRAME")) : 0u;
    return f && sFrameSerial == f;
}

// Cinematicos a 30 Hz con presentacion a 60/120: el frame intermedio no se
// vuelve a dibujar, se presenta otra vez la imagen exacta del ultimo tick.
// Volver a dibujar la escena desde el estado guardado no daba la misma imagen
// (Louie y la nave alternaban entre dos poses cada frame).
static GLuint sHeldFrameFbo = 0, sHeldFrameTex = 0;
static int sHeldFrameW = 0, sHeldFrameH = 0;
static bool sHeldFrameValid = false;

bool pc_gfx_held_frame_capture(void)
{
    sHeldFrameValid = false;
    if (!sNativeFramebufferReady || !glBlitFramebuffer_ptr || !glBindFramebuffer_ptr || !glGenFramebuffers_ptr
        || !glFramebufferTexture2D_ptr)
        return false;
    if (sInPrimitive) pc_gfx_end();
    pc_gfx_flush_batch();
    if (!sHeldFrameFbo) glGenFramebuffers_ptr(1, &sHeldFrameFbo);
    if (!sHeldFrameTex) glGenTextures(1, &sHeldFrameTex);
    if (sHeldFrameW != sRenderWidth || sHeldFrameH != sRenderHeight) {
        glBindTexture(GL_TEXTURE_2D, sHeldFrameTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sRenderWidth, sRenderHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sHeldFrameFbo);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sHeldFrameTex, 0);
        sHeldFrameW = sRenderWidth;
        sHeldFrameH = sRenderHeight;
    }
    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sNativeFramebuffer);
    glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, sHeldFrameFbo);
    glBlitFramebuffer_ptr(0, 0, sRenderWidth, sRenderHeight, 0, 0, sRenderWidth, sRenderHeight, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    glEnable(GL_SCISSOR_TEST);
    sHeldFrameValid = true;
    return true;
}

bool pc_gfx_held_frame_restore(void)
{
    if (!sHeldFrameValid || sHeldFrameW != sRenderWidth || sHeldFrameH != sRenderHeight || !sNativeFramebufferReady)
        return false;
    if (sInPrimitive) pc_gfx_end();
    pc_gfx_flush_batch();
    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sHeldFrameFbo);
    glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, sNativeFramebuffer);
    glBlitFramebuffer_ptr(0, 0, sRenderWidth, sRenderHeight, 0, 0, sRenderWidth, sRenderHeight, GL_COLOR_BUFFER_BIT,
                          GL_NEAREST);
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    glEnable(GL_SCISSOR_TEST);
    sNativeWrittenThisFrame = true; // contenido valido: no limpiarlo a negro al presentar
    return true;
}

void pc_gfx_present(void) {
    // El post-proceso crea shaders con std::string: que no tire del heap JKR
    // actual (la Piklopedia lo deja sin memoria libre -> Memory Alloc Error).
    struct HostAllocScope {
        bool prev = pc_host_alloc_active();
        HostAllocScope() { pc_host_alloc_set(true); }
        ~HostAllocScope() { pc_host_alloc_set(prev); }
    } hostAllocScope;
    if (sInPrimitive) pc_gfx_end(); // the frame's last primitive, if GXEnd never came
    pc_gfx_flush_batch();
    if (fb_invalidate_enabled() && !sNativeWrittenThisFrame && sNativeFramebufferReady && glBindFramebuffer_ptr) {
        // El color se invalidó al presentar el anterior y nadie ha dibujado
        // desde entonces: su contenido es indefinido. Negro, no basura.
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
        const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (scissor) glEnable(GL_SCISSOR_TEST);
        invalidate_gl_pipeline_guards();
    }
    sNativeWrittenThisFrame = false;
    ++sFrameSerial;
#ifdef GL_TIME_ELAPSED
    if (sPerfGpuSceneActive) {
        glEndQuery_ptr(GL_TIME_ELAPSED);
        sPerfGpuSceneActive = false;
    }
#endif
    if (!sNativeFramebufferReady || !glBindFramebuffer_ptr || !glBlitFramebuffer_ptr) {
#ifdef PIKI_PC_PORT
        static int skipLog = 0;
        if (skipLog < 5 || (skipLog % 120) == 0) {
            printf("[PC Port] present skipped #%d: fbReady=%d bind=%p blit=%p\n",
                   skipLog, (int)sNativeFramebufferReady, (void*)glBindFramebuffer_ptr,
                   (void*)glBlitFramebuffer_ptr);
            fflush(stdout);
        }
        skipLog++;
#endif
        return;
    }
    if (sDrawableWidth <= 0 || sDrawableHeight <= 0) {
#ifdef PIKI_PC_PORT
        static int sizeSkip = 0;
        if (sizeSkip < 3) {
            printf("[PC Port] present skipped (drawable %dx%d)\n", sDrawableWidth, sDrawableHeight);
            fflush(stdout);
        }
        sizeSkip++;
#endif
        return;
    }
#ifdef PIKI_PC_PORT
    {
        static int okLog = 0;
        if (pc_gfx_verbose() && okLog < 3) {
            printf("[PC Port] present ok #%d drawable=%dx%d render=%dx%d dumpEnv=%s tid=%lu cur=%p\n",
                   okLog, sDrawableWidth, sDrawableHeight, sRenderWidth, sRenderHeight,
                   std::getenv("PIKMIN_FRAME_DUMP") ? std::getenv("PIKMIN_FRAME_DUMP") : "(null)",
                   (unsigned long)SDL_ThreadID(), SDL_GL_GetCurrentContext());
            fflush(stdout);
        }
        okLog++;
    }
#endif
#ifdef GL_TIME_ELAPSED
    PerfGpuQuery* gpuQuery = nullptr;
    if (sGpuTimingEnabled && sPerfGpuQueriesReady) {
        PerfGpuQuery& candidate = sPerfGpuQueries[sPerfGpuQueryWrite];
        if (!candidate.pending) {
            gpuQuery = &candidate;
            glBeginQuery_ptr(GL_TIME_ELAPSED, gpuQuery->blit);
        }
    }
#endif
    // Normally the pass has already run, where the world ended. It still runs
    // here when it did not -- a frame with no interface at all, which is what
    // a cutscene is -- so nothing is lost in those.
    const bool latePost = !sPostRanThisFrame;
    if (sFileSelDebugReport && latePost) {
        filesel_debug_probe_now("present_before_post", sNativeFramebuffer);
    }
#if PIKI_P2_HOST
    // Con interfaz en el frame y sin postproceso aun (no hubo mundo 3D antes
    // de ella), aplicarlo ahora lo pondria encima de los textos y el HUD.
    const bool skipLatePost = sP2FrameInterface && !sPostRanThisFrame;
    sP2FrameInterface = false;
#else
    const bool skipLatePost = false;
#endif
    const GLuint sourceFramebuffer = (sPostRanThisFrame || skipLatePost) ? sNativeFramebuffer : post_apply(false);
    if (sFileSelDebugReport) {
        filesel_debug_on_present(latePost, sourceFramebuffer);
    }
    sPostRanThisFrame = false;
    shadow_frame_reset();
    sBestDraws = -1;
    sPresentDrawStart = sPerfDraws;
    // PIKMIN_FRAME_DUMP_MOVIE=<dir>: cada frame consecutivo mientras hay un
    // cinematico, a 1/4 de resolucion (modelos duplicados o fuera de sitio).
    if (const char* movieDir = std::getenv("PIKMIN_FRAME_DUMP_MOVIE")) {
        static unsigned movieFrames = 0;
        static unsigned presentNo = 0;
        ++presentNo;
        if (pc_movie_active() && movieFrames < 1200 && sRenderWidth > 0 && sRenderHeight > 0) {
            ++movieFrames;
            std::unique_ptr<unsigned char, void (*)(void*)> rgba(
                (unsigned char*)malloc(size_t(sRenderWidth) * sRenderHeight * 4), free);
            glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sourceFramebuffer);
            glReadPixels(0, 0, sRenderWidth, sRenderHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba.get());
            char path[512];
            snprintf(path, sizeof path, "%s/movie_%06u.ppm", movieDir, presentNo);
            if (FILE* f = fopen(path, "wb")) {
                const int w = sRenderWidth / 4, h = sRenderHeight / 4;
                fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (int y = h - 1; y >= 0; --y) {
                    const unsigned char* row = rgba.get() + size_t(y * 4) * sRenderWidth * 4;
                    for (int x = 0; x < w; ++x) fwrite(row + x * 16, 1, 3, f);
                }
                fclose(f);
            }
        }
    }
    // PIKMIN_FRAME_DUMP=<dir>: the finished frame as PPM every 15 frames
    // (PIKMIN_FRAME_DUMP_EVERY=N overrides), for
    // looking at a scene where no screenshot tool reaches (Wayland, adb-less).
    if (const char* dumpDir = std::getenv("PIKMIN_FRAME_DUMP")) {
        static unsigned dumpFrame = 0;
        static bool dumpAnnounced = false;
        if (!dumpAnnounced) {
            dumpAnnounced = true;
            printf("[PC Port] PIKMIN_FRAME_DUMP=%s render=%dx%d\n", dumpDir, sRenderWidth, sRenderHeight);
            fflush(stdout);
        }
        static unsigned dumpEvery = 0;
        if (!dumpEvery) {
            const char* ev = std::getenv("PIKMIN_FRAME_DUMP_EVERY");
            dumpEvery = ev ? (unsigned)std::max(1, atoi(ev)) : 15u;
        }
        // PIKMIN_FRAME_DUMP_FROM/_TO: solo ese rango de frames (secuencias consecutivas).
        static const unsigned dumpFrom = std::getenv("PIKMIN_FRAME_DUMP_FROM") ? (unsigned)atoi(std::getenv("PIKMIN_FRAME_DUMP_FROM")) : 0u;
        static const unsigned dumpTo   = std::getenv("PIKMIN_FRAME_DUMP_TO") ? (unsigned)atoi(std::getenv("PIKMIN_FRAME_DUMP_TO")) : ~0u;
        ++dumpFrame;
        if (dumpFrame >= dumpFrom && dumpFrame <= dumpTo && dumpFrame % dumpEvery == 0 && sRenderWidth > 0 && sRenderHeight > 0) {
            // malloc, not std::vector: operator new is routed through the
            // game's JKR heap and a per-frame 1.7 MB buffer exhausts it.
            std::unique_ptr<unsigned char, void (*)(void*)> rgba(
                (unsigned char*)malloc(size_t(sRenderWidth) * sRenderHeight * 4), free);
            glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sourceFramebuffer);
            glReadPixels(0, 0, sRenderWidth, sRenderHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba.get());
            char path[512];
            snprintf(path, sizeof path, "%s/frame_%05u.ppm", dumpDir, dumpFrame);
            if (FILE* f = fopen(path, "wb")) {
                fprintf(f, "P6\n%d %d\n255\n", sRenderWidth, sRenderHeight);
                for (int y = sRenderHeight - 1; y >= 0; --y) {
                    const unsigned char* row = rgba.get() + size_t(y) * sRenderWidth * 4;
                    for (int x = 0; x < sRenderWidth; ++x) fwrite(row + x * 4, 1, 3, f);
                }
                fclose(f);
            } else {
                static bool fopenFail = false;
                if (!fopenFail) {
                    fopenFail = true;
                    printf("[PC Port] PIKMIN_FRAME_DUMP fopen failed for %s\n", path);
                    fflush(stdout);
                }
            }
        }
    }
    // Tile-based GPUs write every attachment back to memory at the end of a
    // render pass unless told the contents are dead. Depth is: nothing reads
    // it after the post pass (the game clears it next frame). Colour dies
    // once the blit has consumed it, below.
    const bool invalidate = fb_invalidate_enabled() && glInvalidateFramebuffer_ptr;
    if (invalidate) {
        static const GLenum depthAttachments[2] = { GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
        glInvalidateFramebuffer_ptr(GL_FRAMEBUFFER, depth_has_stencil() ? 2 : 1, depthAttachments);
    }
    glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sourceFramebuffer);
    glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, 0);
    // Game renders to the target aspect ratio. Display it centered in the window.
    float windowAspect = float(sDrawableWidth) / float(sDrawableHeight);
    sCurrentAspectRatio = calculate_aspect_ratio(sAspectRatioMode, windowAspect);

    GLint outX, outY, outWidth, outHeight;
    calculate_output_area(sDrawableWidth, sDrawableHeight, sCurrentAspectRatio,
                          outX, outY, outWidth, outHeight);

    glDisable(GL_SCISSOR_TEST);
    // Clearing a full 4K backbuffer immediately before covering every pixel
    // with the blit wastes bandwidth. Clear only when letter/pillarbox bars
    // are actually visible.
    if (outX != 0 || outY != 0 || outWidth != sDrawableWidth || outHeight != sDrawableHeight) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glBlitFramebuffer_ptr(0, 0, sRenderWidth, sRenderHeight, outX, outY, outX + outWidth, outY + outHeight,
                          GL_COLOR_BUFFER_BIT, GL_LINEAR);
    dim_window_letterbox(outX, outY, outWidth, outHeight);
#ifdef GL_TIME_ELAPSED
    if (gpuQuery) {
        glEndQuery_ptr(GL_TIME_ELAPSED);
        gpuQuery->pending = true;
        sPerfGpuQueryWrite = (sPerfGpuQueryWrite + 1) % PC_GPU_QUERY_RING_SIZE;
    }
#endif
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    if (invalidate) {
        static const GLenum colourAttachment[1] = { GL_COLOR_ATTACHMENT0 };
        glInvalidateFramebuffer_ptr(GL_FRAMEBUFFER, 1, colourAttachment);
    }
    glEnable(GL_SCISSOR_TEST);
}

// Planes of the perspective projection currently in force, for the fog
// register decoder (bp_apply_fog).
static float sCurProjNear = 1.0f, sCurProjFar = 10000.0f;
#if PIKI_P2_HOST
static bool sP2LastOrtho;
static void p2_update_ui43();
extern "C" void pc_gfx_p2_set_2d_widen(float k);
#endif
// Una primitiva inmediata ya completa pero sin GXEnd (el juego a veces lo
// omite, p.ej. TRenderingProcessor::drawImage) debe dibujarse con la matriz y
// proyeccion con las que se envio, como en el FIFO del hardware. Si no, el
// siguiente cambio de estado la dibujaba gigante (iconos de los mensajes).
static void close_complete_immediate() {
    if (!sInPrimitive) return;
    if (pc_gx_imm_primitive_complete(u32(sVertexStream.size()), sHaveVertex, sExpectedVerts)) pc_gfx_end();
}

void pc_gfx_set_projection(const Mtx44 mtx, GXProjectionType type) {
    close_complete_immediate();
    state_touched();
#if PIKI_P2_HOST
    sP2LastOrtho = type != GX_PERSPECTIVE;
    p2_update_ui43();
    pc_gfx_p2_set_2d_widen(0.0f); // J2DPerspGraph lo vuelve a poner tras esta llamada
#endif
    // The orthographic ones matter as much as the perspective ones: the point
    // of this trace is to find where the world stops and the interface starts,
    // and only half the frame was being logged.
    // The first orthographic projection that follows actual drawing is where
    // the world stops and the interface begins. Two things happen here, and
    // they are the same decision: the perspective that was active for the
    // world is committed as the one the depth reconstruction will use, and the
    // post-process runs while the picture is still only the world and the
    // depth buffer still describes it.
    //
    // Requiring draws is what makes it the world rather than whatever came
    // first: an orthographic projection set before anything has been drawn has
    // not ended anything.
    persp_account_draws();
    sCurDrawStart = sPerfDraws;
    if (type != GX_PERSPECTIVE) sCurPerspValid = false;
    // Pikmin 2 avisa del final del mundo con pc_gfx_world_done(): su 2D usa
    // perspectivas (J2DPerspGraph) y una ortografica no marca ese punto.
    if (!kP2Host && type != GX_PERSPECTIVE && sBestDraws > 0 && sPerfDraws > sDrawsAtFrameStart) {
        sViewInvP00 = sBestInvP00;
        sViewInvP11 = sBestInvP11;
        sViewNear   = sBestNear;
        sViewFar    = sBestFar;
        sPendingValid = false;
        sBestDraws = -1;
        post_apply_before_interface();
    }
    if (sProjDebug && type != GX_PERSPECTIVE) {
        printf("[PC Port] Proj #%d: ORTHOGRAPHIC drawsSincePrevious=%d\n",
               sProjSeenThisFrame, int(sPerfDraws - sDrawsAtProjection));
        fflush(stdout);
        sProjSeenThisFrame++;
        sDrawsAtProjection = sPerfDraws;
    }
    sShadowProjPerspective = type == GX_PERSPECTIVE;
    if (mtx && type == GX_PERSPECTIVE) {
        const float p00 = mtx[0][0];
        const float p11 = mtx[1][1];
        const float m22 = mtx[2][2];
        const float m23 = mtx[2][3];
        // C_MTXPerspective sets m22 = -n/(f-n) and m23 = -fn/(f-n), so their
        // ratio is the far plane and the near one follows from either.
        if (p00 != 0.0f && p11 != 0.0f && m22 != 0.0f && m22 != 1.0f) {
            const float f = m23 / m22;
            const float n = -m22 * f / (1.0f - m22);
            if (f > n && n > 0.0f) { sCurProjNear = n; sCurProjFar = f; }
            // Not the first perspective of the frame, and not the last: the one
            // that was active when the world finished drawing.
            //
            // "First" was the obvious rule and it is wrong. Measured over a
            // session, most frames arrive as
            //
            //   #0 perspective near=100 far=10000  <- the world, 336 draws
            //   #1 orthographic                    <- the interface starts
            //   #7 perspective near=1  far=5000    <- 3D bits of the HUD
            //
            // but some states put a different perspective in front of the
            // world, and those frames took near=1 far=15000 and linearised
            // every world pixel against the wrong planes. The symptom was a
            // frame here and there with the whole screen blurred, which is
            // exactly what it looks like when the depth is measured against a
            // projection nothing was drawn with.
            //
            // So the terms are merely remembered here, and committed where the
            // world demonstrably ends: at the first orthographic projection
            // that follows actual drawing. Whatever was active then is what
            // drew the world, by construction rather than by ordering luck.
            if (f > n && n > 0.0f) {
                sPendingInvP00 = 1.0f / p00;
                sPendingInvP11 = 1.0f / p11;
                sPendingNear = n;
                sPendingFar = f;
                sPendingValid = true;
                sCurPerspValid = true;
                sCurInvP00 = sPendingInvP00; sCurInvP11 = sPendingInvP11;
                sCurNear = n; sCurFar = f;
                if (sProjDebug) {
                    printf("[PC Port] Proj #%d: near=%.1f far=%.1f fovScale=%.3f drawsSincePrevious=%d\n",
                           sProjSeenThisFrame, n, f, p11,
                           int(sPerfDraws - sDrawsAtProjection));
                    fflush(stdout);
                }
                sProjSeenThisFrame++;
                sDrawsAtProjection = sPerfDraws;
            }
        }
    }
    if (mtx) {
        // Dolphin matrices are indexed [row][column], while OpenGL consumes a
        // column-major float array when transpose is GL_FALSE.
        for (int row = 0; row < 4; ++row) {
            for (int column = 0; column < 4; ++column) {
                sProjMatrix[column * 4 + row] = mtx[row][column];
            }
        }
        ++sProjMtxGen;
    }
}

void pc_gfx_set_current_mtx(u32 id) {
    close_complete_immediate();
    state_touched();
    if (id < 64) sCurrentPosMtxId = id;
}

// Último viewport GX aplicado, en píxeles del render target (origen abajo).
static GLint sLastViewportX = 0, sLastViewportY = 0;
static GLsizei sLastViewportW = 0, sLastViewportH = 0;

bool pc_gfx_project_current(float x, float y, float z, float* winX, float* winY)
{
    // Igual que un vértice del juego: matriz de posición actual, proyección,
    // viewport GX sobre el render target, y de ahí al área de salida de la
    // ventana (blit del present).
    const float* model = sPosMatrix[sCurrentPosMtxId];
    const float mx = model[0] * x + model[4] * y + model[8] * z + model[12];
    const float my = model[1] * x + model[5] * y + model[9] * z + model[13];
    const float mz = model[2] * x + model[6] * y + model[10] * z + model[14];
    const float mw = model[3] * x + model[7] * y + model[11] * z + model[15];
    const float cx = sProjMatrix[0] * mx + sProjMatrix[4] * my + sProjMatrix[8] * mz + sProjMatrix[12] * mw;
    const float cy = sProjMatrix[1] * mx + sProjMatrix[5] * my + sProjMatrix[9] * mz + sProjMatrix[13] * mw;
    const float cw = sProjMatrix[3] * mx + sProjMatrix[7] * my + sProjMatrix[11] * mz + sProjMatrix[15] * mw;
    if (fabsf(cw) < 1.0e-8f || sLastViewportW <= 0 || sLastViewportH <= 0 || sRenderWidth <= 0 || sRenderHeight <= 0) return false;
    const float nx = cx / cw, ny = cy / cw;
    const float rtX = sLastViewportX + (nx * 0.5f + 0.5f) * sLastViewportW;
    const float rtY = sLastViewportY + (ny * 0.5f + 0.5f) * sLastViewportH; // origen abajo
    GLint outX, outY, outW, outH;
    calculate_output_area(sDrawableWidth, sDrawableHeight, sCurrentAspectRatio, outX, outY, outW, outH);
    const float wx = outX + rtX / float(sRenderWidth) * outW;
    const float wyUp = outY + rtY / float(sRenderHeight) * outH;
    if (winX) *winX = wx;
    if (winY) *winY = float(sDrawableHeight) - wyUp; // ventana: origen arriba
    return true;
}

// Ultimos viewport/scissor en coordenadas GX, para reaplicarlos cuando cambia
// el mapeo 640x480 -> render (HUD 4:3 automatico de Pikmin 2).
static float sGxVp[4] = { 0.0f, 0.0f, 640.0f, 480.0f };
static u32 sGxSc[4] = { 0, 0, 640, 480 };

void pc_gfx_set_viewport(f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ) {
    (void)nearZ; (void)farZ;
    sGxVp[0] = xOrig; sGxVp[1] = yOrig; sGxVp[2] = wd; sGxVp[3] = ht;
    GLint x, y; GLsizei width, height;
    map_gx_rect(xOrig, yOrig, wd, ht, x, y, width, height);
    sLastViewportX = x; sLastViewportY = y; sLastViewportW = width; sLastViewportH = height;
    static GLint lastX = 0, lastY = 0;
    static GLsizei lastWidth = 0, lastHeight = 0;
    static uint32_t seenSerial = 0;
    if (seenSerial == sGlPipelineGuardSerial
        && x == lastX && y == lastY && width == lastWidth && height == lastHeight) return;
    seenSerial = sGlPipelineGuardSerial;
    lastX = x; lastY = y; lastWidth = width; lastHeight = height;
    pc_gfx_note_gl_state_change();
    glViewport(x, y, width, height);
}

#if PIKI_P2_HOST
// 2D en perspectiva sin deformar (J2DPerspGraph ensancha su proyeccion k
// veces): los recortes que el juego calcula en coordenadas de paneles se
// llevan a donde quedan ahora esos paneles, y el recorte de pantalla completa
// se limita a la zona 4:3 original para no destapar lo que estaba fuera de
// pantalla. Los fondos ensanchados y el HUD anclado piden no recortar.
static float sP2Widen = 0.0f;
static int sP2WidenNoClip = 0;

static void p2_widen_scissor(float& x, float& w) {
    if (sP2Widen <= 1.0f || sUi43) return;
    const float cx = sGxW * 0.5f;
    // Casi toda la pantalla cuenta como pantalla completa (hay pantallas que
    // recortan a 600 o 640 de ancho en vez de exactamente sGxW).
    if (x <= 0.05f * sGxW && x + w >= 0.95f * sGxW) {
        if (sP2WidenNoClip > 0) return;
        x = cx - cx / sP2Widen;
        w = sGxW / sP2Widen;
        return;
    }
    x = cx + (x - cx) / sP2Widen;
    w = w / sP2Widen;
}

static void p2_reapply_scissor() {
    pc_gfx_flush_batch();
    invalidate_gl_pipeline_guards();
    pc_gfx_set_scissor(sGxSc[0], sGxSc[1], sGxSc[2], sGxSc[3]);
}

extern "C" void pc_gfx_p2_set_2d_widen(float k) {
    if (k == sP2Widen) return;
    sP2Widen = k;
    p2_reapply_scissor();
}

extern "C" void pc_gfx_p2_2d_widen_noclip(int on) {
    sP2WidenNoClip += on ? 1 : -1;
    if (sP2WidenNoClip < 0) sP2WidenNoClip = 0;
    if (sP2Widen > 1.0f) p2_reapply_scissor();
}
#endif

#if PIKI_P2_HOST
// Los vectores de estas funciones no deben salir del heap JKR del juego: se
// llaman desde secciones con heaps distintos y un buffer reservado en una se
// liberaria en otra (Bad Block). Mismo criterio que pc_gfx_present.
struct P2HostAllocScope {
    bool prev = pc_host_alloc_active();
    P2HostAllocScope() { pc_host_alloc_set(true); }
    ~P2HostAllocScope() { pc_host_alloc_set(prev); }
};

// Color medio de cada fila de una region del render target, suavizado en
// vertical para quitar el dibujo de las texturas y quedarse con el degradado.
static void p2_measure_rows(int x, int y, int w, int h, std::vector<float>& out) {
    static std::vector<unsigned char> px;
    px.resize(size_t(w) * size_t(h) * 4);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<float> rows(size_t(h) * 3, 0.0f);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            for (int c = 0; c < 3; c++) rows[size_t(j) * 3 + c] += px[(size_t(j) * w + i) * 4 + c] / float(w);
    const int win = h / 40 < 2 ? 2 : h / 40;
    out.assign(size_t(h) * 3, 0.0f);
    for (int j = 0; j < h; j++)
        for (int c = 0; c < 3; c++) {
            float sum = 0.0f;
            int n = 0;
            for (int k = j - win; k <= j + win; k++) {
                if (k < 0 || k >= h) continue;
                sum += rows[size_t(k) * 3 + c];
                n++;
            }
            out[size_t(j) * 3 + c] = sum / float(n);
        }
}

// Rectangulo del 4:3 original en el render target para [gxY0, gxY1].
static bool p2_box_rows(float gxY0, float gxY1, GLint& bx, GLint& by, GLsizei& bw, GLsizei& bh) {
    const float boxW = sGxW / sP2Widen, boxX = sGxW * 0.5f - boxW * 0.5f;
    map_gx_rect(boxX, gxY0, boxW, gxY1 - gxY0, bx, by, bw, bh);
    return bw > 0 && bh > 0 && bx > 0;
}

// Pinta los laterales fila a fila: dst = color (blend off) o dst *= color.
static void p2_paint_sides(GLint bx, GLint by, GLsizei bw, GLsizei bh, const std::vector<float>* colors[2], bool multiply) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_SCISSOR_TEST);
    if (multiply) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_ZERO, GL_SRC_COLOR);
    } else {
        glDisable(GL_BLEND);
    }
    glUseProgram_ptr(sDimProgram);
    glBindVertexArray_ptr(sDimVAO);
    const int right = bx + bw;
    for (int side = 0; side < 2; side++) {
        const std::vector<float>& c = *colors[side];
        const int x0 = side == 0 ? 0 : right, w0 = side == 0 ? bx : sRenderWidth - right;
        for (int j = 0; j < bh; j += 2) {
            const int h = j + 2 <= bh ? 2 : 1;
            glScissor(x0, by + j, w0, h);
            glUniform4f_ptr(sDimColorLoc, c[size_t(j) * 3], c[size_t(j) * 3 + 1], c[size_t(j) * 3 + 2], 1.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }
    restore_stream_vao();
    glUseProgram_ptr(0);
    for (int i = 0; i < 8; i++) sBoundTextures[i] = 0;
    gl_program_cache_invalidate();
    invalidate_uniform_cache();
    invalidate_gl_pipeline_guards();
    // El recorte GL quedo en la ultima fila: se repone el del juego.
    pc_gfx_set_scissor(sGxSc[0], sGxSc[1], sGxSc[2], sGxSc[3]);
}
#endif

#if PIKI_P2_HOST
// Fondos que solo existen en el 4:3 (la banda oscura de Opciones): se lleva
// a los laterales su sombreado, no sus pixeles. Por cada fila de [gxY0, gxY1]
// se divide el color medio de la franja interior del 4:3 (stripW de ancho)
// por el de la franja igual justo fuera del borde. Ese factor multiplica el
// lateral entero, que conserva su propia textura.
extern "C" void pc_gfx_p2_extend_shade(float gxY0, float gxY1, float stripW) {
    P2HostAllocScope hostAlloc;
    if (sP2Widen <= 1.0f || !sNativeFramebufferReady || !glBindFramebuffer_ptr || !ensure_dim_program()
        || !glUseProgram_ptr || !glBindVertexArray_ptr) return;
    pc_gfx_flush_batch();
    GLint bx, by;
    GLsizei bw, bh;
    if (!p2_box_rows(gxY0, gxY1, bx, by, bw, bh)) return;
    const int s = int(lroundf(stripW * float(bw) / sGxW));
    if (s < 1 || bx < s) return;
    const int right = bx + bw;
    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    std::vector<float> factor[2];
    for (int side = 0; side < 2; side++) {
        const int inX = side == 0 ? bx : right - s, outX = side == 0 ? bx - s : right;
        std::vector<float> inner, outer;
        p2_measure_rows(inX, by, s, bh, inner);
        p2_measure_rows(outX, by, s, bh, outer);
        factor[side].resize(inner.size());
        for (size_t i = 0; i < inner.size(); i++) {
            const float f = outer[i] > 1.0f ? inner[i] / outer[i] : 1.0f;
            factor[side][i] = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
        }
    }
    const std::vector<float>* colors[2] = { &factor[0], &factor[1] };
    p2_paint_sides(bx, by, bw, bh, colors, true);
}

// Fondos periodicos (rejillas) que solo existen en el 4:3: se copia un periodo
// pegado a cada borde interior y se repite hacia fuera, asi el dibujo
// continua sin cortes. period y [gxY0, gxY1] en unidades de la pantalla.
// maxLuma (0-1, <=0 sin limite): si la franja a copiar es mas clara (un
// destello que pasa por el centro) no se copia. Devuelve 1 si se copio.
extern "C" int pc_gfx_p2_tile_edges(float gxY0, float gxY1, float period, float maxLuma) {
    if (sP2Widen <= 1.0f || !sNativeFramebufferReady || !glBlitFramebuffer_ptr || !glBindFramebuffer_ptr) return 0;
    P2HostAllocScope hostAlloc;
    pc_gfx_flush_batch();
    GLint bx, by;
    GLsizei bw, bh;
    if (!p2_box_rows(gxY0, gxY1, bx, by, bw, bh)) return 0;
    const float pp = period * float(bw) / sGxW; // periodo en pixeles
    if (pp < 2.0f) return 0;
    const int ps = int(lroundf(pp));
    const int right = bx + bw;
    if (maxLuma > 0.0f) {
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
        std::vector<float> rows[2];
        p2_measure_rows(bx, by, ps, bh, rows[0]);
        p2_measure_rows(right - ps, by, ps, bh, rows[1]);
        float sum = 0.0f;
        for (int side = 0; side < 2; side++)
            for (size_t i = 0; i + 2 < rows[side].size(); i += 3)
                sum += 0.299f * rows[side][i] + 0.587f * rows[side][i + 1] + 0.114f * rows[side][i + 2];
        const float luma = sum / (2.0f * float(bh) * 255.0f);
        if (luma > maxLuma) return 0;
    }
    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    glDisable(GL_SCISSOR_TEST);
    glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sNativeFramebuffer);
    glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, sNativeFramebuffer);
    for (int n = 1; float(bx) - (n - 1) * pp > 0.0f; n++) {
        const int d0 = int(lroundf(float(bx) - n * pp)), d1 = int(lroundf(float(bx) - (n - 1) * pp));
        glBlitFramebuffer_ptr(bx, by, bx + ps, by + bh, d0, by, d1, by + bh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    for (int n = 1; float(right) + (n - 1) * pp < float(sRenderWidth); n++) {
        const int d0 = int(lroundf(float(right) + (n - 1) * pp)), d1 = int(lroundf(float(right) + n * pp));
        glBlitFramebuffer_ptr(right - ps, by, right, by + bh, d0, by, d1, by + bh, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    glEnable(GL_SCISSOR_TEST);
    invalidate_gl_pipeline_guards();
    pc_gfx_set_scissor(sGxSc[0], sGxSc[1], sGxSc[2], sGxSc[3]);
    return 1;
}

// Laterales congelados: con capture=1 (o si no hay copia valida) se guardan
// los dos laterales del frame actual; si no, se reponen los guardados. Para
// pantallas cuyas transiciones solo existen en el 4:3.
static GLuint sSidesFbo = 0, sSidesTex = 0;
static int sSidesW = 0, sSidesH = 0;
static bool sSidesValid = false;

extern "C" void pc_gfx_p2_sides_static_reset(void) { sSidesValid = false; }

extern "C" void pc_gfx_p2_sides_static(int capture) {
    if (sP2Widen <= 1.0f || !sNativeFramebufferReady || !glBlitFramebuffer_ptr || !glBindFramebuffer_ptr
        || !glGenFramebuffers_ptr || !glFramebufferTexture2D_ptr) return;
    pc_gfx_flush_batch();
    GLint bx, by;
    GLsizei bw, bh;
    if (!p2_box_rows(0.0f, sGxH, bx, by, bw, bh)) return;
    const int W = sRenderWidth, H = sRenderHeight, right = bx + bw;
    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    if (!sSidesFbo) glGenFramebuffers_ptr(1, &sSidesFbo);
    if (!sSidesTex) glGenTextures(1, &sSidesTex);
    if (sSidesW != W || sSidesH != H) {
        glBindTexture(GL_TEXTURE_2D, sSidesTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sSidesFbo);
        glFramebufferTexture2D_ptr(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sSidesTex, 0);
        for (int i = 0; i < 8; i++) sBoundTextures[i] = 0;
        sSidesW = W;
        sSidesH = H;
        sSidesValid = false;
    }
    glDisable(GL_SCISSOR_TEST);
    const bool save = capture || !sSidesValid;
    glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, save ? sNativeFramebuffer : sSidesFbo);
    glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, save ? sSidesFbo : sNativeFramebuffer);
    glBlitFramebuffer_ptr(0, 0, bx, H, 0, 0, bx, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBlitFramebuffer_ptr(right, 0, W, H, right, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    sSidesValid = true;
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    glEnable(GL_SCISSOR_TEST);
    invalidate_gl_pipeline_guards();
    pc_gfx_set_scissor(sGxSc[0], sGxSc[1], sGxSc[2], sGxSc[3]);
}

// Fondos con vineta que oscurece hacia el borde del 4:3: los laterales se
// rellenan, fila a fila, con el color medio de la franja interior del borde.
extern "C" void pc_gfx_p2_extend_edge_color(float gxY0, float gxY1, float stripW) {
    P2HostAllocScope hostAlloc;
    if (sP2Widen <= 1.0f || !sNativeFramebufferReady || !glBindFramebuffer_ptr || !ensure_dim_program()
        || !glUseProgram_ptr || !glBindVertexArray_ptr) return;
    pc_gfx_flush_batch();
    GLint bx, by;
    GLsizei bw, bh;
    if (!p2_box_rows(gxY0, gxY1, bx, by, bw, bh)) return;
    const int s = int(lroundf(stripW * float(bw) / sGxW));
    if (s < 1) return;
    pc_gfx_note_gl_state_change();
    invalidate_gl_pipeline_guards();
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    std::vector<float> col[2];
    p2_measure_rows(bx, by, s, bh, col[0]);
    p2_measure_rows(bx + bw - s, by, s, bh, col[1]);
    for (int side = 0; side < 2; side++)
        for (float& v : col[side]) v /= 255.0f;
    const std::vector<float>* colors[2] = { &col[0], &col[1] };
    p2_paint_sides(bx, by, bw, bh, colors, false);
}
#endif

extern "C" void pc_gfx_get_scissor(u32* l, u32* t, u32* w, u32* h) {
    *l = sGxSc[0]; *t = sGxSc[1]; *w = sGxSc[2]; *h = sGxSc[3];
}

void pc_gfx_set_scissor(u32 xOrig, u32 yOrig, u32 wd, u32 ht) {
    sGxSc[0] = xOrig; sGxSc[1] = yOrig; sGxSc[2] = wd; sGxSc[3] = ht;
    GLint x, y; GLsizei width, height;
    float scX = (float)xOrig, scW = (float)wd;
#if PIKI_P2_HOST
    p2_widen_scissor(scX, scW);
#endif
    map_gx_rect(scX, (float)yOrig, scW, (float)ht, x, y, width, height);
    static GLint lastX = -1, lastY = -1;
    static GLsizei lastWidth = -1, lastHeight = -1;
    static uint32_t seenSerial = 0;
    if (seenSerial == sGlPipelineGuardSerial
        && x == lastX && y == lastY && width == lastWidth && height == lastHeight) return;
    seenSerial = sGlPipelineGuardSerial;
    lastX = x; lastY = y; lastWidth = width; lastHeight = height;
    pc_gfx_note_gl_state_change();
    glScissor(x, y, width, height);
    glEnable(GL_SCISSOR_TEST);
}

#if PIKI_P2_HOST
// ── Pikmin 2: HUD/menus 2D en 4:3 centrado ───────────────────────────────────
// El 3D ya es hor+ (la camara usa el aspecto de la ventana). El 2D del juego
// se construye para 640x480: toda proyeccion ortografica se mapea a 4:3
// centrado salvo que el juego pida estirar (fundidos a pantalla completa).
static int sP2ForceStretch = 0;

static void p2_apply_ui43(bool want)
{
    if (want == sUi43) return;
    pc_gfx_flush_batch();
    sUi43 = want;
    invalidate_gl_pipeline_guards();
    pc_gfx_set_viewport(sGxVp[0], sGxVp[1], sGxVp[2], sGxVp[3], 0.0f, 1.0f);
    pc_gfx_set_scissor(sGxSc[0], sGxSc[1], sGxSc[2], sGxSc[3]);
}

static void p2_update_ui43()
{
    p2_apply_ui43(sP2LastOrtho && sP2ForceStretch == 0);
}

void pc_gfx_p2_force_stretch(int enabled)
{
    sP2ForceStretch += enabled ? 1 : -1;
    if (sP2ForceStretch < 0) sP2ForceStretch = 0;
    p2_update_ui43();
}
#else
void pc_gfx_p2_force_stretch(int) {}
#endif

void pc_gfx_load_pos_mtx(const Mtx mtx, u32 id) {
    close_complete_immediate();
    state_touched();
    if (id < 64 && mtx) {
        float* dst = sPosMatrix[id];
        dst[0] = mtx[0][0]; dst[1] = mtx[1][0]; dst[2] = mtx[2][0]; dst[3] = 0.0f;
        dst[4] = mtx[0][1]; dst[5] = mtx[1][1]; dst[6] = mtx[2][1]; dst[7] = 0.0f;
        dst[8] = mtx[0][2]; dst[9] = mtx[1][2]; dst[10] = mtx[2][2]; dst[11] = 0.0f;
        dst[12] = mtx[0][3]; dst[13] = mtx[1][3]; dst[14] = mtx[2][3]; dst[15] = 1.0f;
        ++sPosMtxGen[id];
    }
}

void pc_gfx_load_nrm_mtx(const Mtx mtx, u32 id) {
    state_touched();
    if (id >= 64 || !mtx) return;
    float* d = sNrmMatrix[id];
    d[0] = mtx[0][0]; d[1] = mtx[1][0]; d[2] = mtx[2][0];
    d[3] = mtx[0][1]; d[4] = mtx[1][1]; d[5] = mtx[2][1];
    d[6] = mtx[0][2]; d[7] = mtx[1][2]; d[8] = mtx[2][2];
    ++sNrmMtxGen[id];
}
void pc_gfx_load_tex_mtx(const Mtx mtx, u32 id) {
    state_touched();
    if (id >= 64 || !mtx) return;
    float* d = sTexMatrices[id];
    // 3x4 row-major GX matrix -> column-major 4x4
    d[0]  = mtx[0][0]; d[1]  = mtx[1][0]; d[2]  = mtx[2][0]; d[3]  = 0.0f;
    d[4]  = mtx[0][1]; d[5]  = mtx[1][1]; d[6]  = mtx[2][1]; d[7]  = 0.0f;
    d[8]  = mtx[0][2]; d[9]  = mtx[1][2]; d[10] = mtx[2][2]; d[11] = 0.0f;
    d[12] = mtx[0][3]; d[13] = mtx[1][3]; d[14] = mtx[2][3]; d[15] = 1.0f;
    sTexMtxLoaded[id] = true;
    ++sTexMtxGen[id];
    if (id >= 30 && id < 60) ++sTexPaletteGen;
}

// ── Texture coordinate generation ──
void pc_gfx_set_tex_coord_gen(GXTexCoordID coord, GXTexGenType type, GXTexGenSrc src,
                              u32 matrixIdx) {
    state_touched();
    if (coord < 0 || coord >= 8) return;
    sTexCoordGen[coord].active = true;
    sTexCoordGen[coord].type = type;
    sTexCoordGen[coord].src = src;
    sTexCoordGen[coord].mtxIdx = matrixIdx;
}

void pc_gfx_set_tex_coord_post(u32 coord, u32 postIdx, u32 normalize) {
    state_touched();
    if (coord >= 8) return;
    sTexCoordGen[coord].postIdx = (postIdx >= 64 && postIdx <= 127) ? postIdx : 125;
    sTexCoordGen[coord].normalize = normalize != 0;
}

void pc_gfx_load_post_tex_mtx(const f32 mtx[3][4], u32 id) {
    state_touched();
    if (id < 64 || id > 125 || !mtx) return;
    const u32 row = id - 64;
    for (u32 r = 0; r < 3 && row + r < 64; ++r)
        for (u32 c = 0; c < 4; ++c) sPostRows[row + r][c] = mtx[r][c];
    ++sPostMtxGen;
}

// Last values handed to the z/blend/cull setters, for pc_gfx_get_pipeline_state.

PcGfxPipelineState pc_gfx_get_pipeline_state(void) { return sPipelineState; }

void pc_gfx_set_pipeline_state(const PcGfxPipelineState& st) {
    pc_gfx_set_z_mode(st.zCompare, st.zFunc, st.zUpdate);
    pc_gfx_set_blend_mode(st.blendType, st.blendSrc, st.blendDst, st.blendOp);
    pc_gfx_set_cull_mode(st.cull);
}

void pc_gfx_set_z_mode(GXBool compareEnable, GXCompare func, GXBool updateEnable) {
    sPipelineState.zCompare = compareEnable; sPipelineState.zFunc = func; sPipelineState.zUpdate = updateEnable;
    static bool valid = false;
    static uint32_t seenSerial = 0;
    static GXBool lastCompare = GX_FALSE, lastUpdate = GX_FALSE;
    static GXCompare lastFunc = GX_NEVER;
    if (seenSerial == sGlPipelineGuardSerial && valid
        && lastCompare == compareEnable && lastFunc == func && lastUpdate == updateEnable) return;
    seenSerial = sGlPipelineGuardSerial;
    valid = true; lastCompare = compareEnable; lastFunc = func; lastUpdate = updateEnable;
    pc_gfx_note_gl_state_change();
    if (compareEnable) {
        glEnable(GL_DEPTH_TEST);
        GLenum glfunc = GL_LEQUAL;
        switch (func) {
            case GX_NEVER: glfunc = GL_NEVER; break;
            case GX_LESS: glfunc = GL_LESS; break;
            case GX_EQUAL: glfunc = GL_EQUAL; break;
            case GX_LEQUAL: glfunc = GL_LEQUAL; break;
            case GX_GREATER: glfunc = GL_GREATER; break;
            case GX_NEQUAL: glfunc = GL_NOTEQUAL; break;
            case GX_GEQUAL: glfunc = GL_GEQUAL; break;
            case GX_ALWAYS: glfunc = GL_ALWAYS; break;
        }
        glDepthFunc(glfunc);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(updateEnable ? GL_TRUE : GL_FALSE);
}

// GX EFB pixel format: only RGBA6_Z24 stores destination alpha. With RGB8 or
// RGB565 the blender reads DSTALPHA as 1, but the GL target always keeps alpha.
static bool sEfbHasAlpha = false;

void pc_gfx_set_pixel_fmt(int pixFmt) {
    sEfbHasAlpha = pixFmt == GX_PF_RGBA6_Z24;
}

void pc_gfx_set_blend_mode(GXBlendMode type, GXBlendFactor srcFactor, GXBlendFactor dstFactor, GXLogicOp op) {
    sPipelineState.blendType = type; sPipelineState.blendSrc = srcFactor; sPipelineState.blendDst = dstFactor; sPipelineState.blendOp = op;
    static bool valid = false;
    static uint32_t seenSerial = 0;
    static GXBlendMode lastType = GX_BM_NONE;
    static GXBlendFactor lastSrc = GX_BL_ZERO, lastDst = GX_BL_ZERO;
    static GXLogicOp lastOp = GX_LO_CLEAR;
    static bool lastEfbHasAlpha = false;
    if (seenSerial == sGlPipelineGuardSerial && valid && lastEfbHasAlpha == sEfbHasAlpha
        && lastType == type && lastSrc == srcFactor && lastDst == dstFactor && lastOp == op) return;
    seenSerial = sGlPipelineGuardSerial;
    valid = true; lastType = type; lastSrc = srcFactor; lastDst = dstFactor; lastOp = op;
    lastEfbHasAlpha = sEfbHasAlpha;
    pc_gfx_note_gl_state_change();
#if !PIKI_USE_GLES
    glDisable(GL_COLOR_LOGIC_OP);
#endif
    glBlendEquation_ptr(GL_FUNC_ADD);
    if (type == GX_BM_BLEND) {
        glEnable(GL_BLEND);
        GLenum s = GL_ONE, d = GL_ZERO;
        switch (srcFactor) {
            case GX_BL_ZERO: s = GL_ZERO; break;
            case GX_BL_ONE: s = GL_ONE; break;
            case GX_BL_SRCCOL: s = GL_DST_COLOR; break;
            case GX_BL_INVSRCCOL: s = GL_ONE_MINUS_DST_COLOR; break;
            case GX_BL_SRCALPHA: s = GL_SRC_ALPHA; break;
            case GX_BL_INVSRCALPHA: s = GL_ONE_MINUS_SRC_ALPHA; break;
            case GX_BL_DSTALPHA: s = GL_DST_ALPHA; break;
            case GX_BL_INVDSTALPHA: s = GL_ONE_MINUS_DST_ALPHA; break;
        }
        switch (dstFactor) {
            case GX_BL_ZERO: d = GL_ZERO; break;
            case GX_BL_ONE: d = GL_ONE; break;
            case GX_BL_DSTCOL: d = GL_SRC_COLOR; break;
            case GX_BL_INVDSTCOL: d = GL_ONE_MINUS_SRC_COLOR; break;
            case GX_BL_SRCALPHA: d = GL_SRC_ALPHA; break;
            case GX_BL_INVSRCALPHA: d = GL_ONE_MINUS_SRC_ALPHA; break;
            case GX_BL_DSTALPHA: d = GL_DST_ALPHA; break;
            case GX_BL_INVDSTALPHA: d = GL_ONE_MINUS_DST_ALPHA; break;
        }
        if (!sEfbHasAlpha) {
            if (s == GL_DST_ALPHA) s = GL_ONE; else if (s == GL_ONE_MINUS_DST_ALPHA) s = GL_ZERO;
            if (d == GL_DST_ALPHA) d = GL_ONE; else if (d == GL_ONE_MINUS_DST_ALPHA) d = GL_ZERO;
        }
        glBlendFunc(s, d);
    } else if (type == GX_BM_SUBTRACT) {
        glEnable(GL_BLEND);
        glBlendEquation_ptr(GL_FUNC_SUBTRACT);
        glBlendFunc(GL_ONE, GL_ONE);
    } else if (type == GX_BM_LOGIC) {
        glDisable(GL_BLEND);
#if PIKI_USE_GLES
        // OpenGL ES has no colour logic operations. GX_COPY (the normal case)
        // is already represented by drawing with blending disabled. The
        // other operations need shader/framebuffer emulation if live content
        // is ever found to rely on them.
        static bool warned = false;
        if (op != GX_LO_COPY && !warned) {
            warned = true;
            fprintf(stderr, "[PC Port Warning] GLES does not support GX logic op %u; using COPY\n",
                    static_cast<unsigned>(op));
        }
#else
        glEnable(GL_COLOR_LOGIC_OP);
        static const GLenum logicOps[] = {
            GL_CLEAR, GL_AND, GL_AND_REVERSE, GL_COPY, GL_AND_INVERTED, GL_NOOP,
            GL_XOR, GL_OR, GL_NOR, GL_EQUIV, GL_INVERT, GL_OR_REVERSE,
            GL_COPY_INVERTED, GL_OR_INVERTED, GL_NAND, GL_SET
        };
        const unsigned index = static_cast<unsigned>(op);
        glLogicOp(index < sizeof(logicOps) / sizeof(logicOps[0]) ? logicOps[index] : GL_COPY);
#endif
    } else {
        glDisable(GL_BLEND);
    }
}

void pc_gfx_set_cull_mode(GXCullMode mode) {
    sPipelineState.cull = mode;
    static bool valid = false;
    static uint32_t seenSerial = 0;
    static GXCullMode lastMode = GX_CULL_NONE;
    if (seenSerial == sGlPipelineGuardSerial && valid && lastMode == mode) return;
    seenSerial = sGlPipelineGuardSerial;
    valid = true; lastMode = mode;
    pc_gfx_note_gl_state_change();
    if (mode == GX_CULL_NONE) {
        glDisable(GL_CULL_FACE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(mode == GX_CULL_FRONT ? GL_FRONT : GL_BACK);
    }
}

void pc_gfx_set_color_update(GXBool updateEnable) {
    if (sColorUpdate == updateEnable) return;
    sColorUpdate = updateEnable;
    pc_gfx_note_gl_state_change();
    glColorMask(sColorUpdate, sColorUpdate, sColorUpdate, sAlphaUpdate);
}

void pc_gfx_set_alpha_update(GXBool updateEnable) {
    if (sAlphaUpdate == updateEnable) return;
    sAlphaUpdate = updateEnable;
    pc_gfx_note_gl_state_change();
    glColorMask(sColorUpdate, sColorUpdate, sColorUpdate, sAlphaUpdate);
}

void pc_gfx_set_alpha_compare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {
    state_touched();
    sAlphaComp0 = comp0;
    sAlphaComp1 = comp1;
    sAlphaOp = op;
    sAlphaRef0 = ref0 / 255.0f;
    sAlphaRef1 = ref1 / 255.0f;
}

void pc_gfx_set_chan_ctrl(GXChannelID chan, GXBool enable, GXColorSrc ambSrc, GXColorSrc matSrc,
                          u32 lightMask, GXDiffuseFn diffFn, GXAttnFn attnFn) {
    state_touched();
    GfxChannel* ch = nullptr;
    if (chan == GX_COLOR0 || chan == GX_ALPHA0 || chan == GX_COLOR0A0) ch = &sChannels[0];
    else if (chan == GX_COLOR1 || chan == GX_ALPHA1 || chan == GX_COLOR1A1) ch = &sChannels[1];
    if (!ch) return;
    if (chan == GX_COLOR0 || chan == GX_COLOR1 || chan == GX_COLOR0A0 || chan == GX_COLOR1A1) {
        ch->enabled = enable;
        ch->matSrc = matSrc;
        ch->ambSrc = ambSrc;
        ch->lightMask = lightMask;
        ch->diffFn = diffFn;
        ch->attnFn = attnFn;
    }
    if (chan == GX_ALPHA0 || chan == GX_ALPHA1 || chan == GX_COLOR0A0 || chan == GX_COLOR1A1) {
        ch->alphaEnabled = enable;
        ch->alphaMatSrc = matSrc;
        ch->alphaAmbSrc = ambSrc;
        ch->alphaLightMask = lightMask;
        ch->alphaDiffFn = diffFn;
        ch->alphaAttnFn = attnFn;
    }
}

void pc_gfx_set_chan_amb_color(GXChannelID chan, GXColor color) {
    state_touched();
    GfxChannel* ch = nullptr;
    if (chan == GX_COLOR0 || chan == GX_ALPHA0 || chan == GX_COLOR0A0) ch = &sChannels[0];
    else if (chan == GX_COLOR1 || chan == GX_ALPHA1 || chan == GX_COLOR1A1) ch = &sChannels[1];
    if (!ch) return;
    // COLORn and ALPHAn are halves of one register: GX_ALPHA0 writes only the
    // alpha byte and GX_COLOR0 only the RGB. J2D fades a pane with
    // GXSetChanMatColor(GX_ALPHA0, TColor(alpha)), whose RGB is zero;
    // taking that as a full write painted every faded pane black.
    const bool writeRgb = (chan != GX_ALPHA0 && chan != GX_ALPHA1);
    const bool writeAlpha = (chan != GX_COLOR0 && chan != GX_COLOR1);
    if (writeRgb) {
        ch->ambColor[0] = color.r / 255.0f;
        ch->ambColor[1] = color.g / 255.0f;
        ch->ambColor[2] = color.b / 255.0f;
    }
    if (writeAlpha) ch->ambColor[3] = color.a / 255.0f;
}

static GXColor sMatColorTint = { 255, 255, 255, 255 };
static bool    sMatColorTintOn = false;

void pc_gfx_set_mat_color_tint(GXColor tint) {
    sMatColorTint   = tint;
    sMatColorTintOn = true;
}

void pc_gfx_clear_mat_color_tint(void) {
    sMatColorTintOn = false;
}

void pc_gfx_set_chan_mat_color(GXChannelID chan, GXColor color) {
    state_touched();
    GfxChannel* ch = nullptr;
    if (chan == GX_COLOR0 || chan == GX_ALPHA0 || chan == GX_COLOR0A0) ch = &sChannels[0];
    else if (chan == GX_COLOR1 || chan == GX_ALPHA1 || chan == GX_COLOR1A1) ch = &sChannels[1];
    if (!ch) return;
    if (sMatColorTintOn) {
        color.r = (u8)((color.r * sMatColorTint.r) / 255);
        color.g = (u8)((color.g * sMatColorTint.g) / 255);
        color.b = (u8)((color.b * sMatColorTint.b) / 255);
    }
    // COLORn and ALPHAn are halves of one register: GX_ALPHA0 writes only the
    // alpha byte and GX_COLOR0 only the RGB. J2D fades a pane with
    // GXSetChanMatColor(GX_ALPHA0, TColor(alpha)), whose RGB is zero;
    // taking that as a full write painted every faded pane black.
    const bool writeRgb = (chan != GX_ALPHA0 && chan != GX_ALPHA1);
    const bool writeAlpha = (chan != GX_COLOR0 && chan != GX_COLOR1);
    if (writeRgb) {
        ch->matColor[0] = color.r / 255.0f;
        ch->matColor[1] = color.g / 255.0f;
        ch->matColor[2] = color.b / 255.0f;
    }
    if (writeAlpha) ch->matColor[3] = color.a / 255.0f;
}

// ── Light management ──
void pc_gfx_init_light_pos(void* ltObj, f32 x, f32 y, f32 z) {
    if (!ltObj) return;
    // GXLightObjPriv at +0x28 is lpos[3]
    u8* raw = static_cast<u8*>(ltObj);
    f32* lpos = reinterpret_cast<f32*>(raw + 0x28);
    lpos[0] = x; lpos[1] = y; lpos[2] = z;
}
void pc_gfx_init_light_dir(void* ltObj, f32 x, f32 y, f32 z) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    f32* ldir = reinterpret_cast<f32*>(raw + 0x34);
    ldir[0] = x; ldir[1] = y; ldir[2] = z;
}
void pc_gfx_init_light_color(void* ltObj, GXColor color) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    raw[0x0C] = color.r; raw[0x0D] = color.g; raw[0x0E] = color.b; raw[0x0F] = color.a;
}
void pc_gfx_init_light_attn(void* ltObj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    f32* a = reinterpret_cast<f32*>(raw + 0x10);
    a[0] = a0; a[1] = a1; a[2] = a2;
    f32* k = reinterpret_cast<f32*>(raw + 0x1C);
    k[0] = k0; k[1] = k1; k[2] = k2;
}
void pc_gfx_init_light_attn_a(void* ltObj, f32 a0, f32 a1, f32 a2) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    f32* a = reinterpret_cast<f32*>(raw + 0x10);
    a[0] = a0; a[1] = a1; a[2] = a2;
}
void pc_gfx_init_light_attn_k(void* ltObj, f32 k0, f32 k1, f32 k2) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    f32* k = reinterpret_cast<f32*>(raw + 0x1C);
    k[0] = k0; k[1] = k1; k[2] = k2;
}
void pc_gfx_init_specular_dir(void* ltObj, f32 x, f32 y, f32 z) {
    if (!ltObj) return;
    u8* raw = static_cast<u8*>(ltObj);
    // This was a copy of pc_gfx_init_light_dir: it stored the raw direction
    // and left the position alone. A specular light is not shaped like that.
    // GXInitSpecularDir puts the half-angle vector between the reversed light
    // direction and the eye (0,0,1) into ldir, and encodes the direction
    // itself into lpos scaled by 1024*1024. Storing the plain direction gave a
    // half-vector with negative Z -- pointing away from the camera -- so the
    // highlight always landed on the far side of the model and never showed.
    // The Onions were the obvious casualty.
    f32 vx = -x;
    f32 vy = -y;
    f32 vz = -z + 1.0f;
    const f32 mag = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (mag > 1e-6f) {
        const f32 inv = 1.0f / mag;
        vx *= inv; vy *= inv; vz *= inv;
    } else {
        // The light points straight at the eye and the half-vector degenerates.
        vx = 0.0f; vy = 0.0f; vz = 1.0f;
    }
    f32* ldir = reinterpret_cast<f32*>(raw + 0x34);
    ldir[0] = vx; ldir[1] = vy; ldir[2] = vz;

    const f32 kSpecularPosScale = 1024.0f * 1024.0f;
    f32* lpos = reinterpret_cast<f32*>(raw + 0x28);
    lpos[0] = -x * kSpecularPosScale;
    lpos[1] = -y * kSpecularPosScale;
    lpos[2] = -z * kSpecularPosScale;
}
void pc_gfx_load_light(void* ltObj, u32 lightMask) {
    state_touched();
    if (!ltObj) return;
    for (int i = 0; i < 8; i++) {
        if (lightMask & (1 << i)) {
            ++sLightGen[i];
            u8* raw = static_cast<u8*>(ltObj);
            f32* lpos = reinterpret_cast<f32*>(raw + 0x28);
            f32* lk   = reinterpret_cast<f32*>(raw + 0x1C);
            u8* col  = raw + 0x0C;
            sLights[i].pos[0] = lpos[0]; sLights[i].pos[1] = lpos[1]; sLights[i].pos[2] = lpos[2];
            sLights[i].k[0] = lk[0]; sLights[i].k[1] = lk[1]; sLights[i].k[2] = lk[2];
            f32* ld = reinterpret_cast<f32*>(raw + 0x34);
            // Half-vector for specular (light 7) is stored directly in the direction field
            // It's already normalized, use it directly
            sLights[i].dir[0] = ld[0];
            sLights[i].dir[1] = ld[1];
            sLights[i].dir[2] = ld[2];
            f32* la = reinterpret_cast<f32*>(raw + 0x10);
            sLights[i].a[0] = la[0]; sLights[i].a[1] = la[1]; sLights[i].a[2] = la[2];
            sLights[i].color[0] = col[0] / 255.0f;
            sLights[i].color[1] = col[1] / 255.0f;
            sLights[i].color[2] = col[2] / 255.0f;
            sLights[i].color[3] = col[3] / 255.0f;
            sLights[i].active = true;
        }
    }
}

void pc_gfx_set_tev_order(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID chan) {
    state_touched();
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevStages[stage].texMap = map;
        // GXSetTevOrder writes GX_TEXCOORD_NULL as TEXCOORD0 and keeps the texture
        // enabled when the map is valid; the raw 0xFF used to be clamped to coordinate 3 (#58).
        sTevStages[stage].texCoord = (coord >= GX_MAX_TEXCOORD) ? GX_TEXCOORD0 : coord;
        sTevStages[stage].textureEnabled = map >= GX_TEXMAP0 && map < GX_MAX_TEXMAP;
        if (chan == GX_COLOR_NULL || chan == GX_COLOR_ZERO) sTevStages[stage].rasChannel = -1;
        else if (chan == GX_COLOR1 || chan == GX_ALPHA1 || chan == GX_COLOR1A1) sTevStages[stage].rasChannel = 1;
        else sTevStages[stage].rasChannel = 0;
    }
}

void pc_gfx_set_tev_op(GXTevStageID stage, GXTevMode mode) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        TevStageState& st = sTevStages[stage];
        st.colorOp = st.alphaOp = GX_TEV_ADD;
        st.colorBias = st.alphaBias = GX_TB_ZERO;
        st.colorScale = st.alphaScale = GX_CS_SCALE_1;
        st.colorClamp = st.alphaClamp = GX_TRUE;
        st.colorOutReg = st.alphaOutReg = GX_TEVPREV;
        if (mode == GX_REPLACE) {
            st.colorIn[0] = GX_CC_ZERO; st.colorIn[1] = GX_CC_ZERO;
            st.colorIn[2] = GX_CC_ZERO; st.colorIn[3] = GX_CC_TEXC;
            st.alphaIn[0] = GX_CA_ZERO; st.alphaIn[1] = GX_CA_ZERO;
            st.alphaIn[2] = GX_CA_ZERO; st.alphaIn[3] = GX_CA_TEXA;
        } else if (mode == GX_PASSCLR) {
            st.colorIn[0] = GX_CC_ZERO; st.colorIn[1] = GX_CC_ZERO;
            st.colorIn[2] = GX_CC_ZERO; st.colorIn[3] = GX_CC_RASC;
            st.alphaIn[0] = GX_CA_ZERO; st.alphaIn[1] = GX_CA_ZERO;
            st.alphaIn[2] = GX_CA_ZERO; st.alphaIn[3] = GX_CA_RASA;
            st.texMap = GX_TEXMAP_NULL;
            st.textureEnabled = false;
        } else if (mode == GX_MODULATE) {
            st.colorIn[0] = GX_CC_ZERO; st.colorIn[1] = GX_CC_TEXC;
            st.colorIn[2] = GX_CC_RASC; st.colorIn[3] = GX_CC_ZERO;
            st.alphaIn[0] = GX_CA_ZERO; st.alphaIn[1] = GX_CA_TEXA;
            st.alphaIn[2] = GX_CA_RASA; st.alphaIn[3] = GX_CA_ZERO;
        } else if (mode == GX_DECAL) {
            st.colorIn[0] = GX_CC_RASC; st.colorIn[1] = GX_CC_TEXC;
            st.colorIn[2] = GX_CC_TEXA; st.colorIn[3] = GX_CC_ZERO;
            st.alphaIn[0] = GX_CA_ZERO; st.alphaIn[1] = GX_CA_ZERO;
            st.alphaIn[2] = GX_CA_ZERO; st.alphaIn[3] = GX_CA_RASA;
        } else if (mode == GX_BLEND) {
            st.colorIn[0] = GX_CC_RASC; st.colorIn[1] = GX_CC_ONE;
            st.colorIn[2] = GX_CC_TEXC; st.colorIn[3] = GX_CC_ZERO;
            st.alphaIn[0] = GX_CA_ZERO; st.alphaIn[1] = GX_CA_TEXA;
            st.alphaIn[2] = GX_CA_RASA; st.alphaIn[3] = GX_CA_ZERO;
        }
    }
}

void pc_gfx_set_num_tev_stages(u8 num) {
    state_touched();
    sNumTevStages = std::min<u8>(num, GX_MAXTEVSTAGE);
}

void pc_gfx_set_tev_indirect(u32 stage, u32 indStage, u32 fmt, u32 bias, u32 mtx, u32 wrapS, u32 wrapT,
                             u32 addPrev) {
    state_touched();
    if (stage >= GX_MAXTEVSTAGE) return;
    TevIndState& st = sTevInd[stage];
    st.indStage = u8(indStage & 3);
    st.fmt      = u8(fmt & 3);
    st.bias     = u8(bias & 7);
    st.mtx      = u8(mtx & 15);
    st.wrapS    = u8(std::min<u32>(wrapS, 6));
    st.wrapT    = u8(std::min<u32>(wrapT, 6));
    st.addPrev  = u8(addPrev ? 1 : 0);
}

void pc_gfx_set_ind_tex_order(u32 indStage, u32 texCoord, u32 texMap) {
    state_touched();
    if (indStage >= 4) return;
    sIndStages[indStage].texCoord = u8(texCoord & 7);
    sIndStages[indStage].texMap   = u8(texMap & 7);
}

void pc_gfx_set_num_ind_stages(u32 num) {
    state_touched();
    sNumIndStages = u8(std::min<u32>(num, 4));
}

void pc_gfx_set_ind_tex_coord_scale(u32 indStage, u32 scaleS, u32 scaleT) {
    state_touched();
    if (indStage >= 4) return;
    sIndStages[indStage].scaleS = u8(std::min<u32>(scaleS, 8));
    sIndStages[indStage].scaleT = u8(std::min<u32>(scaleT, 8));
}

void pc_gfx_set_ind_tex_mtx(u32 id, const f32 mtx[2][3], s32 scaleExp) {
    state_touched();
    // GXIndTexMtxID: GX_ITM_0..2 = 1..3.
    if (id < 1 || id > 3 || !mtx) return;
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 3; ++c) sIndMtx[id - 1][r][c] = mtx[r][c];
    sIndMtxScale[id - 1] = scaleExp;
}

void pc_gfx_set_tev_color_in(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b,
                             GXTevColorArg c, GXTevColorArg d) {
    state_touched();
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevStages[stage].colorIn[0] = a;
        sTevStages[stage].colorIn[1] = b;
        sTevStages[stage].colorIn[2] = c;
        sTevStages[stage].colorIn[3] = d;
    }
}

void pc_gfx_set_tev_alpha_in(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b,
                             GXTevAlphaArg c, GXTevAlphaArg d) {
    state_touched();
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevStages[stage].alphaIn[0] = a;
        sTevStages[stage].alphaIn[1] = b;
        sTevStages[stage].alphaIn[2] = c;
        sTevStages[stage].alphaIn[3] = d;
    }
}

void pc_gfx_set_tev_color_op(GXTevStageID stage, GXTevOp op, GXTevBias bias,
                             GXTevScale scale, GXBool clamp, GXTevRegID outReg) {
    state_touched();
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevStages[stage].colorOp = op;
        sTevStages[stage].colorBias = bias;
        sTevStages[stage].colorScale = scale;
        sTevStages[stage].colorClamp = clamp;
        sTevStages[stage].colorOutReg = outReg;
    }
}

void pc_gfx_set_tev_alpha_op(GXTevStageID stage, GXTevOp op, GXTevBias bias,
                             GXTevScale scale, GXBool clamp, GXTevRegID outReg) {
    state_touched();
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevStages[stage].alphaOp = op;
        sTevStages[stage].alphaBias = bias;
        sTevStages[stage].alphaScale = scale;
        sTevStages[stage].alphaClamp = clamp;
        sTevStages[stage].alphaOutReg = outReg;
    }
}

void pc_gfx_set_tev_color(GXTevRegID reg, GXColor color) {
    state_touched();
    if (reg < GX_TEVPREV || reg >= GX_MAX_TEVREG) return;
    sTevRegisters[reg][0] = color.r / 255.0f;
    sTevRegisters[reg][1] = color.g / 255.0f;
    sTevRegisters[reg][2] = color.b / 255.0f;
    sTevRegisters[reg][3] = color.a / 255.0f;
#ifdef PC_GFX_TRACE
    {
        static FILE* tf = nullptr;
        if (!tf) tf = fopen("/tmp/opencode/tevreg.log", "a");
        if (tf) { fprintf(tf, "[API] reg=%d u8 -> (%.3f,%.3f,%.3f,%.3f)\n",
                          (int)reg, color.r / 255.0f, color.g / 255.0f,
                          color.b / 255.0f, color.a / 255.0f); fflush(tf); }
    }
#endif
}

void pc_gfx_set_tev_color_s10(GXTevRegID reg, GXColorS10 color) {
    state_touched();
    if (reg < GX_TEVPREV || reg >= GX_MAX_TEVREG) return;
    sTevRegisters[reg][0] = color.r / 255.0f;
    sTevRegisters[reg][1] = color.g / 255.0f;
    sTevRegisters[reg][2] = color.b / 255.0f;
    sTevRegisters[reg][3] = color.a / 255.0f;
#ifdef PC_GFX_TRACE
    {
        static FILE* tf = nullptr;
        if (!tf) tf = fopen("/tmp/opencode/tevreg.log", "a");
        if (tf) { fprintf(tf, "[API] reg=%d s10 -> (%.3f,%.3f,%.3f,%.3f)\n",
                          (int)reg, color.r / 255.0f, color.g / 255.0f,
                          color.b / 255.0f, color.a / 255.0f); fflush(tf); }
    }
#endif
}

void pc_gfx_set_tev_kcolor(GXTevKColorID id, GXColor color) {
    state_touched();
    if (id < 0 || id > 3) return;
    sKonstColors[id][0] = color.r / 255.0f;
    sKonstColors[id][1] = color.g / 255.0f;
    sKonstColors[id][2] = color.b / 255.0f;
    sKonstColors[id][3] = color.a / 255.0f;
}

void pc_gfx_set_tev_kcolor_sel(GXTevStageID stage, GXTevKColorSel sel) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sKonstColorSel[stage] = sel;
    }
}

void pc_gfx_set_tev_kalpha_sel(GXTevStageID stage, GXTevKAlphaSel sel) {
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sKonstAlphaSel[stage] = sel;
    }
}

void pc_gfx_set_tev_swap_mode(GXTevStageID stage, GXTevSwapSel rasSel, GXTevSwapSel texSel) {
    state_touched();
    if (stage >= GX_TEVSTAGE0 && stage < GX_MAXTEVSTAGE) {
        sTevRasSwapSel[stage] = rasSel;
        sTevTexSwapSel[stage] = texSel;
    }
}

void pc_gfx_set_tev_swap_mode_table(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha) {
    state_touched();
    if (table >= GX_TEV_SWAP0 && table <= GX_TEV_SWAP3) {
        int idx = table - GX_TEV_SWAP0;
        sTevSwapModes[idx].red = red;
        sTevSwapModes[idx].green = green;
        sTevSwapModes[idx].blue = blue;
        sTevSwapModes[idx].alpha = alpha;
    }
}

// ── Texture Decoding & Binding ──
// Texture filtering.
//
// Every texture was uploaded with a single level and GL_LINEAR, and the mipmap
// flag the game passes -- it sets it whenever a texture carries LOD levels of
// its own -- was discarded. Without a mip chain a receding surface samples one
// texel per pixel from a texture far denser than the screen, which is not
// blurring but aliasing: the ground sparkles as the camera moves.
//
// Anisotropic filtering is meaningless on its own here for the same reason. It
// chooses among mip levels along the direction of anisotropy, and there were
// none to choose from.
static void apply_texture_filtering(bool gameRequestedMipmaps);

// Replaces whatever this key was costing. An upload into an existing texture
// swaps the old figure rather than adding to it.
static void note_texture_bytes(uintptr_t key, size_t bytes)
{
    // A mip chain adds a third again on top of the base level.
    const size_t withMips = bytes + bytes / 3;
    auto it = sTextureBytes.find(key);
    if (it != sTextureBytes.end()) {
        sTextureBytesLive -= it->second;
        it->second = withMips;
    } else {
        sTextureBytes[key] = withMips;
    }
    sTextureBytesLive += withMips;
    if (sTextureBytesLive > sTextureBytesPeak) sTextureBytesPeak = sTextureBytesLive;
}

void pc_gfx_release_texture(void* gxTexObj)
{
    if (!gxTexObj) return;
    const uintptr_t key = reinterpret_cast<uintptr_t>(gxTexObj);
    auto it = sTextureCache.find(key);
    if (it == sTextureCache.end()) return;

    pc_gfx_flush_batch();  // the batch may still reference this texture
    GLuint id = it->second;
    for (int unit = 0; unit < 8; ++unit) {
        if (sBoundTextures[unit] == id) sBoundTextures[unit] = 0;
    }
    glDeleteTextures(1, &id);
    sExternalMipChain.erase(id);
    sTextureCache.erase(it);
    sTextureSignatures.erase(key);

    auto bytesIt = sTextureBytes.find(key);
    if (bytesIt != sTextureBytes.end()) {
        sTextureBytesLive -= bytesIt->second;
        sTextureBytes.erase(bytesIt);
    }
    sTexturesReleased++;
}

void pc_gfx_get_texture_stats(size_t* live, size_t* liveBytes, size_t* peakBytes,
                              size_t* created, size_t* released)
{
    if (live) *live = sTextureCache.size();
    if (liveBytes) *liveBytes = sTextureBytesLive;
    if (peakBytes) *peakBytes = sTextureBytesPeak;
    if (created) *created = sTexturesCreated;
    if (released) *released = sTexturesReleased;
}

// Every texture already uploaded keeps the filter state it was given, so
// changing this from the menu did nothing to the scene in front of you -- the
// stage's textures were uploaded long before. Re-applying to the whole cache is
// what makes the setting answer immediately, which is also the only way anyone
// can judge it: nobody restarts the game twice to compare a filter.
void pc_gfx_set_anisotropy(int samples)
{
    if (samples == sAnisotropyRequested) return;
    sAnisotropyRequested = samples;
    if (!glActiveTexture_ptr) return;

    pc_gfx_flush_batch();  // about to rebind texture unit 0 under the batch
    glActiveTexture_ptr(GL_TEXTURE0);
    int touched = 0;
    for (const auto& entry : sTextureCache) {
        glBindTexture(GL_TEXTURE_2D, entry.second);
        // Level zero is still there, so the chain can be rebuilt in place
        // without re-decoding anything.
        apply_texture_filtering(false);
        touched++;
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    sBoundTextures[0] = 0;
    printf("[PC Port] Texture filtering changed to %dx, %d textures updated\n",
           sAnisotropyRequested, touched);
    fflush(stdout);
}

// Applies the filter state to whatever is bound. Mipmapping is honoured per
// texture rather than forced: a texture with no LOD levels of its own is one
// the game means to sample flat, and the HUD is full of them.
static void apply_texture_filtering(bool gameRequestedMipmaps)
{
    // The game asks for mipmaps only where a material carries LOD levels of its
    // own, which turns out to be almost nowhere -- so honouring the flag alone
    // produced a setting with nothing to act on.
    //
    // Turning the setting up therefore builds the chain regardless. That is
    // what a player means by raising texture filtering, and it is safe for the
    // interface: a mip chain only changes a surface under minification, and the
    // HUD is drawn at its own size, where level zero is chosen anyway.
    const bool wantsMipmaps = gameRequestedMipmaps || sAnisotropyRequested > 1;

    const auto externalIt = sExternalMipChain.find(sBoundTextures[0]);
    const bool isExternal = externalIt != sExternalMipChain.end();
    const bool externalChain = isExternal && externalIt->second;

    // Las HD del pack suben su propia cadena de mips: generarla aquí daría
    // GL_INVALID_OPERATION sobre un bloque comprimido y arruinaría la textura.
    if (wantsMipmaps && !isExternal && glGenerateMipmap_ptr) {
        glGenerateMipmap_ptr(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        // Once, the first time a texture actually carries LOD levels. Without
        // this line in the log the setting has nothing to act on, and that is
        // not visible any other way.
        static bool toldMipmapped = false;
        if (!toldMipmapped) {
            toldMipmapped = true;
            printf("[PC Port] First mipmapped texture built\n");
            fflush(stdout);
        }
    } else if (wantsMipmaps && externalChain) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        static bool toldHdMipmapped = false;
        if (!toldHdMipmapped) {
            toldHdMipmapped = true;
            printf("[PC Port] First texture pack mip chain used\n");
            fflush(stdout);
        }
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    if (sAnisotropySupported) {
        // Asking for more than the driver offers is an error, not a request.
        float level = float(sAnisotropyRequested);
        if (level < 1.0f) level = 1.0f;
        if (level > sAnisotropyMax) level = sAnisotropyMax;
        // Only where there is a mip chain to choose among. Elsewhere it costs
        // samples and changes nothing.
        const bool chainForSampling = !isExternal || externalChain;
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        wantsMipmaps && chainForSampling ? level : 1.0f);
    }

    if (!sFilteringReported) {
        sFilteringReported = true;
        printf("[PC Port] Texture filtering: anisotropy %s (max %.0fx), mipmaps %s\n",
               sAnisotropySupported ? "available" : "unavailable", sAnisotropyMax,
               glGenerateMipmap_ptr ? "available" : "unavailable");
        fflush(stdout);
    }
}

void pc_gfx_init_tex_obj_rgba(GXTexObj* obj, void* rgba, u16 width, u16 height, GXTexWrapMode wrapS, GXTexWrapMode wrapT) {
    if (!obj || !rgba || width == 0 || height == 0) return;

    const uintptr_t key = (uintptr_t)obj;
    GLuint texId = 0;
    auto it = sTextureCache.find(key);
    if (it != sTextureCache.end()) {
        texId = it->second;
    } else {
        glGenTextures(1, &texId);
        if (texId == 0 || (texId && glIsTexture(texId) == GL_FALSE)) {
            printf("[PC TexIns] ZERO gen (rgba) key=%p ctxErr=%u texId=%u cur=%p\n", (void*)key, (unsigned)glGetError(), (unsigned)texId,
                   SDL_GL_GetCurrentContext());
            fflush(stdout);
        }
        sTextureCache[key] = texId;
        sTexturesCreated++;
    }

    // Uploaded every call, with no signature check: the movie hands over a new
    // picture each frame in the same buffer, so "same pointer, same size" is
    // exactly the case that must still re-upload.
    pc_gfx_flush_batch();
    glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
    sBoundTextures[0] = texId;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gxWrapToGl(wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gxWrapToGl(wrapT));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    sBoundTextures[0] = 0;
}

void pc_gfx_init_tex_obj(GXTexObj* obj, void* imagePtr, u16 width, u16 height, GXTexFmt format,
                         GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap) {
    if (!obj || !imagePtr || width == 0 || height == 0) return;
    const size_t sourceBytes = gx_base_level_size(width, height, static_cast<u32>(format));
    if (sourceBytes == 0) return;

    const uintptr_t key = reinterpret_cast<uintptr_t>(obj);
    std::lock_guard<std::mutex> lock(sTextureRegistryMutex);
    PcTextureSource& sourceRecord = sTextureSources[key];
    sourceRecord.image.assign(static_cast<const u8*>(imagePtr),
                              static_cast<const u8*>(imagePtr) + sourceBytes);
    sourceRecord.width = width;
    sourceRecord.height = height;
    sourceRecord.format = static_cast<u32>(format);
    sourceRecord.wrapS = wrapS;
    sourceRecord.wrapT = wrapT;
    sourceRecord.indexed = false;
    sourceRecord.tlutName = 0;
    sourceRecord.mipmap = mipmap != GX_FALSE;
    sourceRecord.srcAddr = reinterpret_cast<uintptr_t>(imagePtr);
    ++sourceRecord.generation;
}

static bool upload_regular_texture(GXTexObj* obj, PcTextureSource& sourceRecord) {
    if (!obj || sourceRecord.indexed || sourceRecord.image.empty()) return false;
    const uintptr_t key = reinterpret_cast<uintptr_t>(obj);
    const u16 width = sourceRecord.width;
    const u16 height = sourceRecord.height;
    const GXTexFmt format = static_cast<GXTexFmt>(sourceRecord.format);
    const GXTexWrapMode wrapS = sourceRecord.wrapS;
    const GXTexWrapMode wrapT = sourceRecord.wrapT;
    const GXBool mipmap = sourceRecord.mipmap ? GX_TRUE : GX_FALSE;
    const u8* imagePtr = sourceRecord.image.data();

    const PcTextureSignature signature {
        imagePtr, width, height, static_cast<u32>(format), wrapS, wrapT, false, 0
    };

    if (sDumpTextureNames) {
        dump_dolphin_texture_name(imagePtr, width, height,
                                  static_cast<u32>(format), mipmap != GX_FALSE, nullptr, 0);
    }

    GLuint texId = 0;
    auto it = sTextureCache.find(key);
    if (it != sTextureCache.end()) {
        texId = it->second;
    } else {
        glGenTextures(1, &texId);
        static u32 allGenLog = 0;
        if (pc_gfx_verbose() && allGenLog++ < 40) {
            printf("[PC TexGen] key=%p texId=%u cur=%p isTex=%u err=%u tid=%lu\n", (void*)key, (unsigned)texId,
                   SDL_GL_GetCurrentContext(), (unsigned)glIsTexture(texId), (unsigned)glGetError(), (unsigned long)SDL_ThreadID());
            fflush(stdout);
        }
        if (texId == 0) {
            printf("[PC TexIns] ZERO gen (tex) key=%p ctxErr=%u\n", (void*)key, (unsigned)glGetError());
            if (!SDL_GL_GetCurrentContext()) {
                void* bt[24]; int n = backtrace(bt, 24);
                char** syms = backtrace_symbols(bt, n);
                printf("[PC TexIns]   BACKTRACE (no context):\n");
                for (int i = 0; i < n && i < 8; i++) printf("  %s\n", syms[i]);
                free(syms);
            }
            fflush(stdout);
            return false;
        }
        sTextureCache[key] = texId;
        sTexturesCreated++;
    }

    pc_gfx_flush_batch();  // about to rebind and rewrite texture unit 0
    glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
    sBoundTextures[0] = texId;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gxWrapToGl(wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gxWrapToGl(wrapT));
    // Un texId que ya llevó una HD del pack (o un id que GL reutiliza tras
    // borrarlo) conserva la marca y el tope de niveles: limpiarlos antes de
    // decidir de nuevo, o la textura original se quedaría sin mipmaps.
    if (sExternalMipChain.erase(texId))
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);

    if (pc_texpack_enabled()) {
        char texPackName[96];
        int glLevels = 0;
        size_t gpuBytes = 0;
        if (compute_dolphin_name(imagePtr, width, height,
                                 static_cast<u32>(format), mipmap != GX_FALSE, nullptr, 0,
                                 texPackName, sizeof(texPackName), nullptr)
            && pc_texpack_try_upload(texPackName, texId, &glLevels, &gpuBytes)) {
            sExternalMipChain[texId] = glLevels > 1;
            note_texture_bytes(key, gpuBytes ? gpuBytes : size_t(width) * height * 4);
            apply_texture_filtering(mipmap != GX_FALSE);
            ++sPerfTextureUploads;
            sTextureSignatures[key] = signature;
            return true;
        }
    }

    PcGfxHostVector<u8> rgba(width * height * 4, 255);
    const u8* source = imagePtr;
    auto writePixel = [&](int x, int y, u8 r, u8 g, u8 b, u8 a) {
        if (x >= width || y >= height) return;
        size_t offset = (static_cast<size_t>(y) * width + x) * 4;
        rgba[offset] = r; rgba[offset + 1] = g;
        rgba[offset + 2] = b; rgba[offset + 3] = a;
    };
    auto expand5 = [](u16 value) -> u8 { return (value << 3) | (value >> 2); };
    auto expand6 = [](u16 value) -> u8 { return (value << 2) | (value >> 4); };

    int tileWidth = 1, tileHeight = 1, bytesPerTile = 0;
    switch (format) {
        case GX_TF_I4: tileWidth = 8; tileHeight = 8; bytesPerTile = 32; break;
        case GX_TF_I8: case GX_TF_IA4: tileWidth = 8; tileHeight = 4; bytesPerTile = 32; break;
        case GX_TF_IA8: case GX_TF_RGB565: case GX_TF_RGB5A3:
            tileWidth = 4; tileHeight = 4; bytesPerTile = 32; break;
        case GX_TF_RGBA8: tileWidth = 4; tileHeight = 4; bytesPerTile = 64; break;
        case GX_TF_CMPR: tileWidth = 8; tileHeight = 8; bytesPerTile = 32; break;
        default: break;
    }

    if (bytesPerTile == 0) {
        // El búfer se inicializa a blanco opaco, así que un formato que no se
        // decodifique acaba en pantalla como un cuadrado blanco sólido y sin
        // que nada lo señale. Se avisa una vez por formato para que deje de ser
        // invisible durante la depuración.
        static u32 reportedFormats = 0;
        const u32 formatBit = format < 32 ? (1u << format) : 0x80000000u;
        const bool firstTime = (reportedFormats & formatBit) == 0;
        reportedFormats |= formatBit;

        // Los formatos Z son un caso conocido y distinto: el juego los usa para
        // texturas que se rellenan copiando el framebuffer (mapMgr crea el
        // «internalLightmap» de 320x240 con TEX_FMT_Z8), y en el port GXCopyTex
        // es un stub vacío, así que esos píxeles nunca llegan a escribirse.
        // Decodificarlos leería memoria sin inicializar. El blanco es además el
        // valor neutro para cómo se combinan, así que se conserva.
        const bool isDepthFormat = format == GX_TF_Z8 || format == GX_TF_Z16
                                || format == GX_TF_Z24X8;
        if (firstTime) {
            printf("[PC GX Warning] Formato de textura no decodificado 0x%X (%dx%d): %s\n",
                   static_cast<unsigned>(format), width, height,
                   isDepthFormat
                       ? "formato Z, su origen (GXCopyTex) es un stub; se deja neutro"
                       : "se dibujará transparente en vez de blanco");
            fflush(stdout);
        }
        if (!isDepthFormat) {
            // Transparente estropea mucho menos la escena que un cuadrado
            // blanco opaco encima de ella.
            std::fill(rgba.begin(), rgba.end(), static_cast<u8>(0));
        }
    }

    if (bytesPerTile) {
        size_t tileOffset = 0;
        for (int tileY = 0; tileY < height; tileY += tileHeight) {
            for (int tileX = 0; tileX < width; tileX += tileWidth, tileOffset += bytesPerTile) {
                for (int y = 0; y < tileHeight; ++y) {
                    for (int x = 0; x < tileWidth; ++x) {
                        int index = y * tileWidth + x;
                        u8 r = 255, g = 255, b = 255, a = 255;
                        if (format == GX_TF_I4) {
                            u8 packed = source[tileOffset + index / 2];
                            u8 intensity = ((index & 1) ? packed : packed >> 4) & 0x0f;
                            r = g = b = a = intensity * 17;
                        } else if (format == GX_TF_I8) {
                            r = g = b = a = source[tileOffset + index];
                        } else if (format == GX_TF_IA4) {
                            // Like IA8: alpha in the high nibble, intensity
                            // in the low one. The other way round made every
                            // IA4 sprite (the title-screen Pikmin, for one)
                            // fully transparent or fully opaque.
                            u8 packed = source[tileOffset + index];
                            a = (packed >> 4) * 17; r = g = b = (packed & 0x0f) * 17;
                        } else if (format == GX_TF_IA8) {
                            // Alpha first, intensity second.
                            //
                            // This looks wrong against the hardware format,
                            // where intensity is the high byte, and it was
                            // changed to match -- which fixed the H4M movie's
                            // colours and put a visible rectangle around every
                            // window frame in the game, because a frame that
                            // should have been transparent became opaque.
                            // Confirmed by switching it back on the machine.
                            //
                            // So the rest of the port reads these two channels
                            // consistently with what is here, and it is the
                            // movie that packs its chroma the other way round.
                            // Fixed there instead: one player against every
                            // IA8 texture in the game.
                            a = source[tileOffset + index * 2];
                            r = g = b = source[tileOffset + index * 2 + 1];
                        } else if (format == GX_TF_RGB565 || format == GX_TF_RGB5A3) {
                            u16 value = (source[tileOffset + index * 2] << 8) | source[tileOffset + index * 2 + 1];
                            if (format == GX_TF_RGB565) {
                                r = expand5(value >> 11); g = expand6((value >> 5) & 0x3f); b = expand5(value & 0x1f);
                            } else if (value & 0x8000) {
                                r = expand5((value >> 10) & 0x1f); g = expand5((value >> 5) & 0x1f); b = expand5(value & 0x1f);
                            } else {
                                a = ((value >> 12) & 7) * 255 / 7;
                                r = ((value >> 8) & 15) * 17; g = ((value >> 4) & 15) * 17; b = (value & 15) * 17;
                            }
                        } else if (format == GX_TF_RGBA8) {
                            a = source[tileOffset + index * 2]; r = source[tileOffset + index * 2 + 1];
                            g = source[tileOffset + 32 + index * 2]; b = source[tileOffset + 33 + index * 2];
                        } else if (format == GX_TF_CMPR) {
                            // GameCube CMPR is DXT1 arranged as four 4x4
                            // sub-blocks inside every 8x8 tile. Endpoints are
                            // big-endian and selectors are stored two bits per
                            // pixel, most-significant pair first.
                            const int block = (y / 4) * 2 + (x / 4);
                            const u8* encoded = source + tileOffset + block * 8;
                            const u16 c0 = (u16(encoded[0]) << 8) | encoded[1];
                            const u16 c1 = (u16(encoded[2]) << 8) | encoded[3];
                            u8 colors[4][4] = {};
                            colors[0][0] = expand5(c0 >> 11); colors[0][1] = expand6((c0 >> 5) & 0x3f);
                            colors[0][2] = expand5(c0 & 0x1f); colors[0][3] = 255;
                            colors[1][0] = expand5(c1 >> 11); colors[1][1] = expand6((c1 >> 5) & 0x3f);
                            colors[1][2] = expand5(c1 & 0x1f); colors[1][3] = 255;
                            if (c0 > c1) {
                                for (int channel = 0; channel < 3; ++channel) {
                                    colors[2][channel] = (2 * colors[0][channel] + colors[1][channel]) / 3;
                                    colors[3][channel] = (colors[0][channel] + 2 * colors[1][channel]) / 3;
                                }
                                colors[2][3] = colors[3][3] = 255;
                            } else {
                                for (int channel = 0; channel < 3; ++channel) {
                                    colors[2][channel] = (colors[0][channel] + colors[1][channel]) / 2;
                                }
                                colors[2][3] = 255;
                                colors[3][3] = 0;
                            }
                            const int localX = x & 3;
                            const int localY = y & 3;
                            const int selector = (encoded[4 + localY] >> (6 - localX * 2)) & 3;
                            r = colors[selector][0]; g = colors[selector][1];
                            b = colors[selector][2]; a = colors[selector][3];
                        }
                        writePixel(tileX + x, tileY + y, r, g, b, a);
                    }
                }
            }
        }
    }

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    static u32 uploadProbeCount = 0;
    if ((getenv("PIKMIN_TEXLOG") || pc_gfx_verbose()) && uploadProbeCount++ < (getenv("PIKMIN_TEXLOG") ? 100000u : 12u)) {
        u8 minRgb = 255, maxRgb = 0, minAlpha = 255, maxAlpha = 0;
        size_t visible = 0;
        for (size_t pixel = 0; pixel < rgba.size(); pixel += 4) {
            minRgb = std::min(minRgb, std::min(rgba[pixel], std::min(rgba[pixel + 1], rgba[pixel + 2])));
            maxRgb = std::max(maxRgb, std::max(rgba[pixel], std::max(rgba[pixel + 1], rgba[pixel + 2])));
            minAlpha = std::min(minAlpha, rgba[pixel + 3]);
            maxAlpha = std::max(maxAlpha, rgba[pixel + 3]);
            if (rgba[pixel + 3] != 0) ++visible;
        }
        const GLenum uploadError = glGetError();
        printf("[PC TexUpload] key=%p id=%u fmt=0x%X size=%ux%u rgb=%u..%u a=%u..%u visible=%zu/%zu isTex=%u err=%u\n",
               (void*)key, (unsigned)texId, (unsigned)format, (unsigned)width, (unsigned)height,
               (unsigned)minRgb, (unsigned)maxRgb, (unsigned)minAlpha, (unsigned)maxAlpha,
               visible, rgba.size() / 4, (unsigned)glIsTexture(texId), (unsigned)uploadError);
        fflush(stdout);
    }
    note_texture_bytes(key, size_t(width) * size_t(height) * 4);
    // After the upload: the mip chain is built from the data, so it cannot be
    // asked for before there is any.
    apply_texture_filtering(mipmap != GX_FALSE);
    ++sPerfTextureUploads;
    sTextureSignatures[key] = signature;
    return true;
}

void pc_gfx_init_tlut_obj(GXTlutObj* obj, void* lut, GXTlutFmt format, u16 numEntries) {
    if (!obj || !lut || numEntries == 0) return;
    PcTlut decoded;
    decoded.rgba.resize(static_cast<size_t>(numEntries) * 4);
    const u8* source = static_cast<const u8*>(lut);
    auto expand5 = [](u16 value) -> u8 { return (value << 3) | (value >> 2); };
    auto expand6 = [](u16 value) -> u8 { return (value << 2) | (value >> 4); };
    for (u16 i = 0; i < numEntries; ++i) {
        const u16 value = (u16(source[i * 2]) << 8) | source[i * 2 + 1];
        u8 r = 255, g = 255, b = 255, a = 255;
        if (format == GX_TL_IA8) {
            r = g = b = u8(value >> 8);
            a = u8(value & 0xFF);
        } else if (format == GX_TL_RGB565) {
            r = expand5(value >> 11);
            g = expand6((value >> 5) & 0x3F);
            b = expand5(value & 0x1F);
        } else if (value & 0x8000) {
            r = expand5((value >> 10) & 0x1F);
            g = expand5((value >> 5) & 0x1F);
            b = expand5(value & 0x1F);
        } else {
            a = u8(((value >> 12) & 0x7) * 255 / 7);
            r = u8(((value >> 8) & 0xF) * 17);
            g = u8(((value >> 4) & 0xF) * 17);
            b = u8((value & 0xF) * 17);
        }
        const size_t offset = static_cast<size_t>(i) * 4;
        decoded.rgba[offset] = r; decoded.rgba[offset + 1] = g;
        decoded.rgba[offset + 2] = b; decoded.rgba[offset + 3] = a;
    }
    if (sDumpTextureNames) {
        decoded.raw.assign(source, source + static_cast<size_t>(numEntries) * 2);
    }
    std::lock_guard<std::mutex> lock(sTextureRegistryMutex);
    sTlutObjects[reinterpret_cast<uintptr_t>(obj)] = std::move(decoded);
}

void pc_gfx_load_tlut(GXTlutObj* obj, u32 tlutName) {
    if (!obj) return;
    std::lock_guard<std::mutex> lock(sTextureRegistryMutex);
    auto it = sTlutObjects.find(reinterpret_cast<uintptr_t>(obj));
    if (it != sTlutObjects.end()) sLoadedTluts[tlutName] = it->second;
}

static bool upload_ci_texture(GXTexObj* obj, PcTextureSource& ci) {
    if (!obj || !ci.indexed || ci.image.empty()) return false;
    PcTlut palette;
    {
        std::lock_guard<std::mutex> lock(sTextureRegistryMutex);
        auto paletteIt = sLoadedTluts.find(ci.tlutName);
        if (paletteIt == sLoadedTluts.end() || paletteIt->second.rgba.empty()) return false;
        palette = paletteIt->second;
    }
    if (sDumpTextureNames) {
        dump_dolphin_texture_name(ci.image.data(), ci.width, ci.height, ci.format,
                                  ci.mipmap,
                                  palette.raw.empty() ? nullptr : palette.raw.data(),
                                  palette.raw.size() / 2);
    }
    PcGfxHostVector<u8> rgba(static_cast<size_t>(ci.width) * ci.height * 4, 0);
    int tileWidth = ci.format == GX_TF_C4 || ci.format == GX_TF_C8 ? 8 : 4;
    int tileHeight = ci.format == GX_TF_C4 ? 8 : 4;
    int bytesPerTile = 32;
    size_t tileOffset = 0;
    for (int tileY = 0; tileY < ci.height; tileY += tileHeight) {
        for (int tileX = 0; tileX < ci.width; tileX += tileWidth, tileOffset += bytesPerTile) {
            for (int y = 0; y < tileHeight; ++y) {
                for (int x = 0; x < tileWidth; ++x) {
                    const int local = y * tileWidth + x;
                    u32 paletteIndex = 0;
                    if (ci.format == GX_TF_C4) {
                        const u8 packed = ci.image[tileOffset + local / 2];
                        paletteIndex = (local & 1) ? (packed & 0xF) : (packed >> 4);
                    } else if (ci.format == GX_TF_C8) {
                        paletteIndex = ci.image[tileOffset + local];
                    } else {
                        paletteIndex = ((u32(ci.image[tileOffset + local * 2]) << 8)
                                      | ci.image[tileOffset + local * 2 + 1]) & 0x3FFF;
                    }
                    const int dstX = tileX + x, dstY = tileY + y;
                    const size_t paletteOffset = static_cast<size_t>(paletteIndex) * 4;
                    if (dstX >= ci.width || dstY >= ci.height || paletteOffset + 3 >= palette.rgba.size()) continue;
                    const size_t dst = (static_cast<size_t>(dstY) * ci.width + dstX) * 4;
                    memcpy(&rgba[dst], &palette.rgba[paletteOffset], 4);
                }
            }
        }
    }

    const uintptr_t key = reinterpret_cast<uintptr_t>(obj);
    GLuint texId = 0;
    auto textureIt = sTextureCache.find(key);
    if (textureIt == sTextureCache.end()) {
        glGenTextures(1, &texId);
        if (texId == 0) {
            printf("[PC TexIns] ZERO gen (ci) key=%p ctxErr=%u texId=%u\n", (void*)key, (unsigned)glGetError(), (unsigned)texId);
            fflush(stdout);
            return false;
        }
        sTextureCache[key] = texId;
        sTexturesCreated++;
    } else {
        texId = textureIt->second;
    }
    pc_gfx_flush_batch();  // about to rebind and rewrite texture unit 0
    glActiveTexture_ptr(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texId);
    sBoundTextures[0] = texId;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gxWrapToGl(ci.wrapS));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gxWrapToGl(ci.wrapT));
    if (sExternalMipChain.erase(texId))
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
    if (pc_texpack_enabled()) {
        char texPackName[96];
        int glLevels = 0;
        size_t gpuBytes = 0;
        if (compute_dolphin_name(ci.image.data(), ci.width, ci.height, ci.format,
                                 ci.mipmap,
                                 palette.raw.empty() ? nullptr : palette.raw.data(),
                                 palette.raw.size() / 2,
                                 texPackName, sizeof(texPackName), nullptr)
            && pc_texpack_try_upload(texPackName, texId, &glLevels, &gpuBytes)) {
            sExternalMipChain[texId] = glLevels > 1;
            note_texture_bytes(key, gpuBytes ? gpuBytes
                                             : size_t(ci.width) * size_t(ci.height) * 4);
            apply_texture_filtering(false);
            ++sPerfTextureUploads;
            return true;
        }
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ci.width, ci.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    note_texture_bytes(key, size_t(ci.width) * size_t(ci.height) * 4);
    // Palettised textures carry no LOD levels through this path, so they are
    // filtered flat -- but they still take the wrap and magnification state.
    apply_texture_filtering(false);
    ++sPerfTextureUploads;
    return true;
}

void pc_gfx_init_tex_obj_ci(GXTexObj* obj, void* imagePtr, u16 width, u16 height, GXCITexFmt format,
                            GXTexWrapMode wrapS, GXTexWrapMode wrapT, GXBool mipmap, u32 tlutName) {
    if (!obj || !imagePtr || width == 0 || height == 0) return;
    const uintptr_t key = reinterpret_cast<uintptr_t>(obj);
    const size_t sourceBytes = gx_base_level_size(width, height, static_cast<u32>(format));
    if (sourceBytes == 0) return;
    std::lock_guard<std::mutex> lock(sTextureRegistryMutex);
    PcTextureSource& ci = sTextureSources[key];
    ci.image.assign(static_cast<const u8*>(imagePtr),
                    static_cast<const u8*>(imagePtr) + sourceBytes);
    ci.width = width;
    ci.height = height;
    ci.format = static_cast<u32>(format);
    ci.wrapS = wrapS;
    ci.wrapT = wrapT;
    ci.indexed = true;
    ci.tlutName = tlutName;
    ci.mipmap = mipmap != GX_FALSE;
    ++ci.generation;
}

static u8 sBpTexDirty = 0;
void pc_gfx_load_tex_obj(GXTexObj* obj, GXTexMapID id) {
    if (id >= 0 && id < 8) sBpTexDirty &= u8(~(1u << id));
    // On hardware, GXLoadTexObj is a FIFO command that lands after any
    // vertices already written. Immediate GL binds break that ordering when
    // a drawer omits GXEnd (JUTResFont::drawChar_scale): the next glyph's
    // LoadTexObj runs while the previous quad is still open, so pc_gfx_begin
    // flushes it under the wrong page — "60"→"06", "mode."→"`mod%".
    // Finish any pending immediate verts before changing the bound texture.
    if (sInPrimitive && (sHaveVertex || !sVertexStream.empty())) {
        pc_gfx_end();
    }
    state_touched();
    if (id < GX_TEXMAP0 || id >= GX_MAX_TEXMAP) return;
    if (!obj) {
        sHasActiveTextures[id] = false;
        return;
    }

    uintptr_t key = (uintptr_t)obj;
    PcTextureSource sourceSnapshot;
    bool needsUpload = false;
    {
        std::lock_guard<std::mutex> lock(sTextureRegistryMutex);
        auto source = sTextureSources.find(key);
        if (source != sTextureSources.end() && !sEfbCopies.empty()) {
            auto copy = sEfbCopies.find(source->second.srcAddr);
            if (copy == sEfbCopies.end()) {
                // Las texturas que llegan por registros BP (listas de display
                // J3D) reconstruyen la direccion desde TX_SETIMAGE3, que la
                // guarda en unidades de 32 bytes: si la imagen copiada no esta
                // alineada a 32, llega redondeada hacia abajo y no coincide
                // (agua de P2: negra en los frames que la dibuja esa ruta).
                const uintptr_t block = source->second.srcAddr & ~uintptr_t(31);
                for (auto c = sEfbCopies.begin(); c != sEfbCopies.end(); ++c) {
                    if ((c->first & ~uintptr_t(31)) == block) { copy = c; break; }
                }
            }
            if (copy != sEfbCopies.end() && copy->second.tex) {
                sActiveGLTextures[id] = copy->second.tex;
                sHasActiveTextures[id] = true;
                return;
            }
        }
        if (source != sTextureSources.end()
            && source->second.uploadedGeneration != source->second.generation) {
            sourceSnapshot = source->second;
            needsUpload = true;
        }
    }
    if (needsUpload) {
        const bool uploaded = sourceSnapshot.indexed
            ? upload_ci_texture(obj, sourceSnapshot)
            : upload_regular_texture(obj, sourceSnapshot);
        if (uploaded) {
            std::lock_guard<std::mutex> lock(sTextureRegistryMutex);
            auto source = sTextureSources.find(key);
            if (source != sTextureSources.end() && source->second.generation == sourceSnapshot.generation) {
                source->second.uploadedGeneration = sourceSnapshot.generation;
            }
        }
    }
    auto it = sTextureCache.find(key);
    if (it != sTextureCache.end()) {
        static const char* dumpId = getenv("PIKMIN_TEXDUMP_ID");
        static bool dumped = false;
        GLint w = 0, h = 0;
        if (dumpId && !dumped) {
            glBindTexture(GL_TEXTURE_2D, it->second);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
            glBindTexture(GL_TEXTURE_2D, sBoundTextures[0]);
        }
        if (dumpId && !dumped && w * 10000 + h == atoi(dumpId)) {
            dumped = true;
            glBindTexture(GL_TEXTURE_2D, it->second);
            PcGfxHostVector<u8> px(size_t(w) * h * 4);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            FILE* f = fopen("/tmp/claude-1000/texdump.ppm", "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", w, h);
                for (size_t i = 0; i < px.size(); i += 4) { u8 a = px[i + 3]; u8 rgb[3] = { u8(px[i] * a / 255), u8(px[i + 1] * a / 255), u8(px[i + 2] * a / 255) }; fwrite(rgb, 1, 3, f); }
                fclose(f);
            }
            glBindTexture(GL_TEXTURE_2D, sBoundTextures[0]);
        }
        apply_tex_obj_filter(key, it->second);
        sActiveGLTextures[id] = it->second;
        sHasActiveTextures[id] = true;
        static u32 loadLogCount = 0;
        if (pc_gfx_verbose() && loadLogCount++ < 20) {
            printf("[PC TexLoad] key=%p map=%d GLid=%u firstLoad=%d\n",
                   (void*)key, (int)id, (unsigned)it->second, (int)0);
            fflush(stdout);
        }
    } else {
        // Hasta ahora esto era mudo, y una textura que nunca llegó a subirse
        // se manifestaba solo como un artefacto en pantalla.
        static u32 reported = 0;
        if (reported < 8) {
            ++reported;
            printf("[PC GX Warning] Se pidió la textura %p para el mapa %d, "
                   "pero nunca se subió; se dibujará transparente.\n",
                   static_cast<void*>(obj), static_cast<int>(id));
            fflush(stdout);
        }
        sActiveGLTextures[id] = 0;
        sHasActiveTextures[id] = false;
    }
}

// ── Vertex Descriptor & Setup ──
// The game caches which vertex attributes it has already programmed and skips
// redundant GXSetVtxDesc calls (DGXGraphics::setupVtxDesc). That cache assumes
// nothing else touches the descriptor. Recording the history lets a misparsed
// vertex say which calls actually reached us before it.
//
// Off unless asked for. setupVtxDesc programs every attribute for every mesh
// (see the comment on it in dgxGraphics.cpp), so this runs about fifteen times
// per mesh in every frame of a normal session, purely to keep a history that
// only the wild-vertex report ever reads.
bool pc_gfx_gx_diagnostics_enabled(void) {
    static const bool enabled = [] {
        const char* value = getenv("PIKMIN_WILD_VERTS");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

struct VtxDescEvent { uint32_t serial; int attr; int type; bool cleared; };
static VtxDescEvent sVtxDescLog[16] = {};
static uint32_t sVtxDescSerial = 0;
static inline void log_vtx_desc(int attr, int type, bool cleared) {
    if (!pc_gfx_gx_diagnostics_enabled()) return;
    sVtxDescLog[sVtxDescSerial % 16] = { sVtxDescSerial, attr, type, cleared };
    ++sVtxDescSerial;
}
void pc_gfx_dump_vtx_desc_history(const char* why) {
    fprintf(stderr, "[PC GX] vtxdesc history (%s), most recent last, %u calls total:\n", why, sVtxDescSerial);
    const uint32_t first = sVtxDescSerial > 16 ? sVtxDescSerial - 16 : 0;
    for (uint32_t i = first; i < sVtxDescSerial; ++i) {
        const VtxDescEvent& e = sVtxDescLog[i % 16];
        if (e.cleared) fprintf(stderr, "    #%u  CLEAR ALL\n", e.serial);
        else fprintf(stderr, "    #%u  attr a%d = %s\n", e.serial, e.attr,
                     e.type == GX_NONE ? "NONE" : e.type == GX_DIRECT ? "DIRECT"
                     : e.type == GX_INDEX8 ? "IDX8" : "IDX16");
    }
}

void pc_gfx_clear_vtx_desc(void) {
    // GX code may omit GXEnd (a no-op on GameCube); a vertex-layout change
    // must not reinterpret or extend the primitive that was just written.
    close_complete_immediate();
    for (int i = 0; i < GX_VA_MAX_ATTR; ++i) sVtxDesc[i] = GX_NONE;
    log_vtx_desc(0, 0, true);
}
void pc_gfx_set_vtx_desc(GXAttr attr, GXAttrType type) {
    close_complete_immediate();
    if (attr >= 0 && attr < GX_VA_MAX_ATTR) sVtxDesc[attr] = type;
    log_vtx_desc(int(attr), int(type), false);
}
void pc_gfx_set_vtx_attr_fmt(GXVtxFmt fmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
    close_complete_immediate();
    if (fmt >= 0 && fmt < GX_MAX_VTXFMT && attr >= 0 && attr < GX_VA_MAX_ATTR) {
        // GX ignores the caller's shift for normals: it is fixed at 6 bits for
        // s8 and 14 for s16 (the VAT does not even carry one). Lighting
        // normalises the vector so a wrong scale never showed there, but a
        // GX_TG_NRM texgen (the ship's environment map) turned into noise.
        if (attr == GX_VA_NRM || attr == GX_VA_NBT)
            frac = type == GX_S8 ? 6 : type == GX_S16 ? 14 : 0;
        sVtxFormats[fmt][attr] = { cnt, type, frac };
    }
}
void pc_gfx_set_array(GXAttr attr, void* basePtr, u8 stride) {
    if (attr >= 0 && attr < GX_VA_MAX_ATTR) {
        sVtxArrays[attr] = { static_cast<const u8*>(basePtr), stride };
        sArraySetFrame[attr]  = sFrameSerial;
        sArraySetSerial[attr] = ++sArraySetCounter;
    }
}

static Vertex sCurVertex = {};
static GXVtxFmt sImmVtxFmt = GX_VTXFMT0;
static bool sFifoImmActive = false;
static int sFifoAttr = GX_VA_PNMTXIDX;
static u8 sFifoMtxId = 0;
static u8 sFifoNeed = 0;
static u8 sFifoGot = 0;
static u8 sFifoTmp[32];
static Vertex sFifoVertex = {};

static bool vtx_desc_uses_fifo()
{
    // Matrix-index DIRECT is the normal GX default. Treating it as a FIFO
    // stream made every UI and world draw parse vertices a byte at a time.
    // Only indexed arrays, or packed non-float immediates (GXTexCoord2u8),
    // need the byte parser.
    for (int a = GX_VA_POS; a <= GX_VA_TEX7; ++a) {
        const GXAttrType d = sVtxDesc[a];
        if (d == GX_INDEX8 || d == GX_INDEX16) return true;
        if (d == GX_DIRECT && sVtxFormats[sImmVtxFmt][a].type != GX_F32) return true;
    }
    return false;
}

static void fifo_imm_byte(u8 val);
static void fifo_imm_start_vertex();

static void fifo_imm_reset()
{
    sFifoImmActive = vtx_desc_uses_fifo();
    sFifoMtxId     = static_cast<u8>(sCurrentPosMtxId);
    fifo_imm_start_vertex();
}

// ── Drawing & FIFO Stream Parser ──
static void bp_resolve_textures();
void pc_gfx_begin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts) {
    sNativeWrittenThisFrame = true;
    sTexBakedMask = 0;
    if (sInPrimitive) {
        pc_gfx_end();
    }
    bp_resolve_textures();
    sImmVtxFmt = vtxfmt;
    sCurrentPrimType = type;
    sExpectedVerts = nverts;
    sVertexStream.clear();
    if (sVertexStream.capacity() < nverts) sVertexStream.reserve(nverts);
    sInPrimitive = true;
    sHaveVertex = false;
    sVerticesPretransformed = false;
    sVertexUsesPalette = false;
    sCurVertex.r = 1.0f;
    sCurVertex.g = 1.0f;
    sCurVertex.b = 1.0f;
    sCurVertex.a = 1.0f;
    sCurVertex.nx = 0.0f;
    sCurVertex.ny = 0.0f;
    sCurVertex.nz = 1.0f;
    sCurVertex.matrixSlot = 0.0f;
    for (int i = 0; i < 4; ++i) {
        sCurVertex.tex[i][0] = 0.0f;
        sCurVertex.tex[i][1] = 0.0f;
    }
    fifo_imm_reset();
}
static int sAttrStep = 0;

// En la consola todo va al mismo FIFO y el formato de vértice decide qué es
// cada dato: código del juego que manda la coordenada de textura con
// GXPosition2f32 (los números de carga de CarryInfo) funciona allí. Aquí las
// funciones van por nombre, así que una "posición" que llega cuando el vértice
// ya tiene la suya, el formato lleva TEX0 directa y aún no ha llegado, es la
// coordenada de textura y no un vértice nuevo.
static bool sCurVertexHasTex0 = false;
void pc_gfx_position(f32 x, f32 y, f32 z) {
    if (!sInPrimitive) return;
    if (sHaveVertex && !sCurVertexHasTex0 && sVtxDesc[GX_VA_TEX0] == GX_DIRECT) {
        pc_gfx_texcoord(x, y);
        return;
    }
    sCurVertexHasTex0 = false;
    if (sHaveVertex) sVertexStream.push_back(sCurVertex);
    sCurVertex.x = x;
    sCurVertex.y = y;
    sCurVertex.z = z;
    sHaveVertex = true;
}

void pc_gfx_normal(f32 x, f32 y, f32 z) {
    sCurVertex.nx = x;
    sCurVertex.ny = y;
    sCurVertex.nz = z;
}

void pc_gfx_color(u8 r, u8 g, u8 b, u8 a) {
    sCurVertex.r = r / 255.0f;
    sCurVertex.g = g / 255.0f;
    sCurVertex.b = b / 255.0f;
    sCurVertex.a = a / 255.0f;
}

void pc_gfx_texcoord(f32 u, f32 v) {
    sCurVertex.tex[0][0] = u;
    sCurVertex.tex[0][1] = v;
    sCurVertexHasTex0 = true;
}

void pc_gfx_texcoord_fixed(s32 u, s32 v) {
    const VertexFormatState& format = sVtxFormats[sImmVtxFmt][GX_VA_TEX0];
    pc_gfx_texcoord(pc_gx_decode_fixed_component(u, format.type, format.frac),
                    pc_gx_decode_fixed_component(v, format.type, format.frac));
}

void pc_gfx_push_f32(f32 val) {
    if (!sInPrimitive) return;

    if (sAttrStep == 0) sCurVertex.x = val;
    else if (sAttrStep == 1) sCurVertex.y = val;
    else if (sAttrStep == 2) {
        sCurVertex.z = val;
        // Push vertex to stream
        sVertexStream.push_back(sCurVertex);
        sAttrStep = -1; // Reset step for next vertex
    }
    sAttrStep++;
}

void pc_gfx_push_u8(u8 val) { fifo_imm_byte(val); }
void pc_gfx_push_u16(u16 val)
{
    fifo_imm_byte(static_cast<u8>(val >> 8));
    fifo_imm_byte(static_cast<u8>(val));
}
void pc_gfx_push_u32(u32 val)
{
    fifo_imm_byte(static_cast<u8>(val >> 24));
    fifo_imm_byte(static_cast<u8>(val >> 16));
    fifo_imm_byte(static_cast<u8>(val >> 8));
    fifo_imm_byte(static_cast<u8>(val));
}
void pc_gfx_push_s8(s8 val) { fifo_imm_byte(static_cast<u8>(val)); }
void pc_gfx_push_s16(s16 val) { pc_gfx_push_u16(static_cast<u16>(val)); }
void pc_gfx_push_s32(s32 val) { pc_gfx_push_u32(static_cast<u32>(val)); }


// ── Specialised TEV programs ──
// The ubershader interprets the TEV configuration per pixel. That
// configuration is constant for a draw, so it belongs in the program: each
// distinct configuration gets its own generated shader with the selectors
// resolved, and the results are cached because a scene reuses a small number
// of materials. The ubershader stays as the fallback and as an A/B reference.

struct TevProgramEntry {
    PcTevShaderKey key;
    uint64_t hash = 0;
    GLuint program = 0;
    ProgramLocations locations;
};

static PcGfxHostVector<TevProgramEntry> sTevPrograms;
// Consecutive draws almost always share a material, so remember the last
// configuration and skip both the hash and the table scan when it repeats.
// Fog, as the game asks for it. Kept here rather than thrown away in the stub:
// the values are per stage and the game already computes them correctly.
// What the game asked for, and whether the player allows it. Kept apart so a
// toggle takes effect on the next frame drawn rather than on the next time the
// game happens to call setFog.
static bool sFogRequested = false;
static bool sFogAllowed = getenv("PIKMIN_NO_FOG") == nullptr;
static float sFogStart = 0.0f, sFogEnd = 0.0f, sFogNear = 0.0f, sFogFar = 0.0f;
static float sFogColour[3] = { 0.0f, 0.0f, 0.0f };

void pc_gfx_set_fog(int enabled, float startZ, float endZ, float nearZ, float farZ,
                    unsigned char r, unsigned char g, unsigned char b)
{
    // A span of zero would divide by nothing and a near/far pair that is not
    // ordered cannot describe a view, so treat either as "no fog" rather than
    // letting it reach the shader.
    sFogRequested = enabled != 0 && endZ != startZ && farZ > nearZ;
    sFogStart = startZ;
    sFogEnd = endZ;
    sFogNear = nearZ;
    sFogFar = farZ;
    sFogColour[0] = r / 255.0f;
    sFogColour[1] = g / 255.0f;
    sFogColour[2] = b / 255.0f;
    // Once, the first time a stage actually asks for fog. Says that the values
    // are arriving and what they are, which is otherwise only answerable by
    // staring at a horizon.
    static bool reported = false;
    if (sFogRequested && !reported) {
        reported = true;
        printf("[PC Port] Fog active: %.0f..%.0f (view %.0f..%.0f) colour %d,%d,%d\n",
               startZ, endZ, nearZ, farZ, r, g, b);
        fflush(stdout);
    }
}

void pc_gfx_set_fog_allowed(int allowed) { sFogAllowed = allowed != 0; }

static bool filesel_debug_enabled()
{
    static int cached = -1;
    if (cached < 0) {
        cached = getenv("PIKMIN_FILESEL_DEBUG") != nullptr ? 1 : 0;
        if (cached) {
            printf("[PC Port] PIKMIN_FILESEL_DEBUG: file-select stain probe on (once per second)\n");
            fflush(stdout);
        }
    }
    return cached != 0;
}

static const char* gl_blend_name(GLint factor)
{
    switch (factor) {
    case GL_ZERO: return "ZERO";
    case GL_ONE: return "ONE";
    case GL_SRC_ALPHA: return "SRCALPHA";
    case GL_ONE_MINUS_SRC_ALPHA: return "INVSRCALPHA";
    case GL_DST_ALPHA: return "DSTALPHA";
    case GL_ONE_MINUS_DST_ALPHA: return "INVDSTALPHA";
    case GL_DST_COLOR: return "DSTCOL";
    case GL_ONE_MINUS_DST_COLOR: return "INVDSTCOL";
    case GL_SRC_COLOR: return "SRCCOL";
    case GL_ONE_MINUS_SRC_COLOR: return "INVSRCCOL";
    default: return "?";
    }
}

static void filesel_debug_probe_now(const char* tag, GLuint fbo)
{
    if (!sFileSelDebugReport || !sNativeFramebufferReady || !glBindFramebuffer_ptr) return;
    pc_gfx_flush_batch();
    if (!fbo) fbo = sNativeFramebuffer;
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, fbo);

    struct Probe {
        const char* name;
        int x;
        int y;
    };
    const int yBand = std::max(0, sRenderHeight * 3 / 10);
    const Probe probes[3] = {
        { "L", std::max(0, sRenderWidth / 20), yBand },
        { "R", std::max(0, sRenderWidth * 19 / 20), yBand },
        { "C", sRenderWidth / 2, sRenderHeight / 2 },
    };

    GLint glSrc = 0, glDst = 0;
    const GLboolean blendOn = glIsEnabled(GL_BLEND);
    const GLboolean depthOn = glIsEnabled(GL_DEPTH_TEST);
    glGetIntegerv(GL_BLEND_SRC_RGB, &glSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &glDst);

    printf("[PC Port] FILESEL %s: %dx%d aspect=%.3f blend=%d src=%s dst=%s depth=%d fogReq=%d fogAllow=%d prog=%u ssao=%d dof=%d bloom=%d\n",
           tag ? tag : "?",
           sRenderWidth, sRenderHeight,
           sCurrentAspectRatio,
           blendOn ? 1 : 0, gl_blend_name(glSrc), gl_blend_name(glDst),
           depthOn ? 1 : 0,
           sFogRequested ? 1 : 0, sFogAllowed ? 1 : 0,
           unsigned(sCurrentProgram),
           pc_post_ssao_active(sPostEffects) ? 1 : 0,
           pc_post_dof_active(sPostEffects) ? 1 : 0,
           pc_post_bloom_active(sPostEffects) ? 1 : 0);
    for (const Probe& probe : probes) {
        unsigned char rgba[4] = { 0, 0, 0, 0 };
        float depth = -1.0f;
        glReadPixels(probe.x, probe.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        if (sDepthIsTexture) {
            glReadPixels(probe.x, probe.y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
        }
        printf("[PC Port] FILESEL %s %s=(%d,%d) rgb=(%d,%d,%d) a=%d z=%.4f\n",
               tag ? tag : "?", probe.name, probe.x, probe.y,
               rgba[0], rgba[1], rgba[2], rgba[3], depth);
    }
    {
        const int xs[7] = {
            std::max(0, sRenderWidth / 20),
            std::max(0, sRenderWidth * 3 / 20),
            std::max(0, sRenderWidth / 4),
            sRenderWidth / 2,
            std::max(0, sRenderWidth * 3 / 4),
            std::max(0, sRenderWidth * 17 / 20),
            std::max(0, sRenderWidth * 19 / 20),
        };
        const char* names[7] = { "5%", "15%", "25%", "50%", "75%", "85%", "95%" };
        printf("[PC Port] FILESEL %s scan y=%d:", tag ? tag : "?", yBand);
        for (int i = 0; i < 7; ++i) {
            unsigned char rgba[4] = { 0, 0, 0, 0 };
            glReadPixels(xs[i], yBand, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
            printf(" %s=(%d,%d,%d)", names[i], rgba[0], rgba[1], rgba[2]);
        }
        printf("\n");
    }
    fflush(stdout);
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
}

static uint64_t sFileSelDrawKeys[12];
static int sFileSelDrawKeyCount = 0;
static int sFileSelLargeQuadCount = 0;

static void filesel_debug_log_draw()
{
    if (!sFileSelDebugReport || !sFileSelFxWindow) return;

    const TevStageState& st = sTevStages[0];
    const int c0r = int(sTevRegisters[GX_TEVREG0][0] * 255.0f + 0.5f);
    const int c0g = int(sTevRegisters[GX_TEVREG0][1] * 255.0f + 0.5f);
    const int c0b = int(sTevRegisters[GX_TEVREG0][2] * 255.0f + 0.5f);
    const int c0a = int(sTevRegisters[GX_TEVREG0][3] * 255.0f + 0.5f);
    const int c1r = int(sTevRegisters[GX_TEVREG1][0] * 255.0f + 0.5f);
    const int c1g = int(sTevRegisters[GX_TEVREG1][1] * 255.0f + 0.5f);
    const int c1b = int(sTevRegisters[GX_TEVREG1][2] * 255.0f + 0.5f);
    const int c1a = int(sTevRegisters[GX_TEVREG1][3] * 255.0f + 0.5f);

    GLint glSrc = 0, glDst = 0;
    const GLboolean blendOn = glIsEnabled(GL_BLEND);
    glGetIntegerv(GL_BLEND_SRC_RGB, &glSrc);
    glGetIntegerv(GL_BLEND_DST_RGB, &glDst);

    uint64_t key = uint64_t(st.colorIn[0] & 15)
        | (uint64_t(st.colorIn[1] & 15) << 4)
        | (uint64_t(st.colorIn[2] & 15) << 8)
        | (uint64_t(st.colorIn[3] & 15) << 12)
        | (uint64_t(glSrc & 0xFFFF) << 16)
        | (uint64_t(glDst & 0xFFFF) << 32)
        | (uint64_t(c1r & 255) << 48)
        | (uint64_t(blendOn ? 1 : 0) << 56);
    bool seen = false;
    for (int i = 0; i < sFileSelDrawKeyCount; ++i) {
        if (sFileSelDrawKeys[i] == key) {
            seen = true;
            break;
        }
    }
    if (!seen && sFileSelDrawKeyCount < 12) {
        sFileSelDrawKeys[sFileSelDrawKeyCount++] = key;
        printf("[PC Port] FILESEL draw: blend=%d src=%s dst=%s tevC=(%d,%d,%d,%d) tevA=(%d,%d,%d,%d) C0=(%d,%d,%d,%d) C1=(%d,%d,%d,%d) fog=%d prog=%u verts=%zu\n",
               blendOn ? 1 : 0, gl_blend_name(glSrc), gl_blend_name(glDst),
               int(st.colorIn[0]), int(st.colorIn[1]), int(st.colorIn[2]), int(st.colorIn[3]),
               int(st.alphaIn[0]), int(st.alphaIn[1]), int(st.alphaIn[2]), int(st.alphaIn[3]),
               c0r, c0g, c0b, c0a, c1r, c1g, c1b, c1a,
               (sFogRequested && sFogAllowed) ? 1 : 0,
               unsigned(sCurrentProgram), sVertexStream.size());
        fflush(stdout);
    }

    if (sFileSelLargeQuadCount >= 6 || sVertexStream.empty()) return;
    static const float identity[16] = {
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
    };
    const float* model = sVerticesPretransformed ? identity : sPosMatrix[sCurrentPosMtxId];
    float ndcMinX = 1.0e30f, ndcMaxX = -1.0e30f, ndcMinY = 1.0e30f, ndcMaxY = -1.0e30f;
    for (const Vertex& vertex : sVertexStream) {
        const float mx = model[0] * vertex.x + model[4] * vertex.y + model[8] * vertex.z + model[12];
        const float my = model[1] * vertex.x + model[5] * vertex.y + model[9] * vertex.z + model[13];
        const float mz = model[2] * vertex.x + model[6] * vertex.y + model[10] * vertex.z + model[14];
        const float mw = model[3] * vertex.x + model[7] * vertex.y + model[11] * vertex.z + model[15];
        const float cx = sProjMatrix[0] * mx + sProjMatrix[4] * my + sProjMatrix[8] * mz + sProjMatrix[12] * mw;
        const float cy = sProjMatrix[1] * mx + sProjMatrix[5] * my + sProjMatrix[9] * mz + sProjMatrix[13] * mw;
        const float cw = sProjMatrix[3] * mx + sProjMatrix[7] * my + sProjMatrix[11] * mz + sProjMatrix[15] * mw;
        if (fabsf(cw) > 1.0e-8f) {
            const float nx = cx / cw;
            const float ny = cy / cw;
            ndcMinX = std::min(ndcMinX, nx);
            ndcMaxX = std::max(ndcMaxX, nx);
            ndcMinY = std::min(ndcMinY, ny);
            ndcMaxY = std::max(ndcMaxY, ny);
        }
    }
    const float spanX = ndcMaxX - ndcMinX;
    const bool side = ndcMinX < -0.7f || ndcMaxX > 0.7f;
    if (spanX > 0.15f && side) {
        ++sFileSelLargeQuadCount;
        printf("[PC Port] FILESEL quad: ndc=(%.2f,%.2f)-(%.2f,%.2f) spanX=%.2f C1=(%d,%d,%d) dst=%s\n",
               ndcMinX, ndcMinY, ndcMaxX, ndcMaxY, spanX, c1r, c1g, c1b, gl_blend_name(glDst));
        fflush(stdout);
    }
}

static uint64_t sFileSelPtclKeys[12];
static int sFileSelPtclKeyCount = 0;

static bool title_debug_enabled()
{
    static int cached = -1;
    if (cached < 0) {
        cached = std::getenv("PIKMIN_TITLE_DEBUG") != nullptr ? 1 : 0;
        if (cached) {
            printf("[PC Port] PIKMIN_TITLE_DEBUG: title crop probe on (heartbeat 1 Hz; extra on black sides)\n");
            fflush(stdout);
        }
    }
    return cached != 0;
}

void pc_gfx_title_debug_probe(const char* tag)
{
    if (!title_debug_enabled()) return;
    if (!sNativeFramebufferReady || !glBindFramebuffer_ptr) return;

    static uint64_t seenFrame = 0;
    static bool printThisFrame = false;
    static int gate = 0;
    if (seenFrame != sGfxFrameSerial) {
        seenFrame = sGfxFrameSerial;
        printThisFrame = (++gate % 60 == 1);
    }

    pc_gfx_flush_batch();
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);

    const int yBand = std::max(0, sRenderHeight * 3 / 10);
    const int xs[7] = {
        std::max(0, sRenderWidth / 20),
        std::max(0, sRenderWidth * 3 / 20),
        std::max(0, sRenderWidth / 4),
        sRenderWidth / 2,
        std::max(0, sRenderWidth * 3 / 4),
        std::max(0, sRenderWidth * 17 / 20),
        std::max(0, sRenderWidth * 19 / 20),
    };
    unsigned char scan[7][4] = {};
    for (int i = 0; i < 7; ++i) {
        glReadPixels(xs[i], yBand, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, scan[i]);
    }

    const int lumL = std::max(scan[0][0], std::max(scan[0][1], scan[0][2]));
    const int lumR = std::max(scan[6][0], std::max(scan[6][1], scan[6][2]));
    const int lumC = std::max(scan[3][0], std::max(scan[3][1], scan[3][2]));
    const bool sidesBlack = lumL < 16 && lumR < 16 && lumC > 40;
    if (sidesBlack) {
        printThisFrame = true;
    }
    if (!printThisFrame) {
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
        return;
    }

    GLint vp[4] = { 0, 0, 0, 0 };
    GLint sc[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_SCISSOR_BOX, sc);
    const GLboolean scissorOn = glIsEnabled(GL_SCISSOR_TEST);

    GLint map640x = 0, map640y = 0;
    GLsizei map640w = 0, map640h = 0;
    GLint mapVx = 0, mapVy = 0;
    GLsizei mapVw = 0, mapVh = 0;
    map_gx_rect(0.0f, 0.0f, sGxW, sGxH, map640x, map640y, map640w, map640h);
    map_gx_rect(0.0f, 0.0f, float(hud_virtual_width()), 480.0f, mapVx, mapVy, mapVw, mapVh);

    printf("[PC Port] TITLE %s: ui43=%d hudWide=%d menuWide=%d virtW=%d rt=%dx%d aspect=%.3f scissorOn=%d%s\n",
           tag ? tag : "?",
           sUi43 ? 1 : 0, sHudWide ? 1 : 0, pc_gfx_menu_wide(),
           hud_virtual_width(), sRenderWidth, sRenderHeight, sCurrentAspectRatio,
           scissorOn ? 1 : 0,
           sidesBlack ? " SIDES_BLACK" : "");
    printf("[PC Port] TITLE %s: gl vp=(%d,%d,%d,%d) sc=(%d,%d,%d,%d) map640=(%d,%d,%d,%d) mapV=(%d,%d,%d,%d)\n",
           tag ? tag : "?",
           vp[0], vp[1], vp[2], vp[3],
           sc[0], sc[1], sc[2], sc[3],
           map640x, map640y, int(map640w), int(map640h),
           mapVx, mapVy, int(mapVw), int(mapVh));
    printf("[PC Port] TITLE %s scan y=%d: 5%%=(%d,%d,%d) 15%%=(%d,%d,%d) 25%%=(%d,%d,%d) 50%%=(%d,%d,%d) 75%%=(%d,%d,%d) 85%%=(%d,%d,%d) 95%%=(%d,%d,%d)\n",
           tag ? tag : "?", yBand,
           scan[0][0], scan[0][1], scan[0][2],
           scan[1][0], scan[1][1], scan[1][2],
           scan[2][0], scan[2][1], scan[2][2],
           scan[3][0], scan[3][1], scan[3][2],
           scan[4][0], scan[4][1], scan[4][2],
           scan[5][0], scan[5][1], scan[5][2],
           scan[6][0], scan[6][1], scan[6][2]);
    fflush(stdout);
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
}

void pc_gfx_filesel_debug_probe(const char* tag)
{
    if (!filesel_debug_enabled()) return;
    static int gate = 0;
    if (tag && std::strcmp(tag, "before_slots") == 0) {
        sFileSelDebugReport = (++gate % 60 == 1);
        sFileSelDrawKeyCount = 0;
        sFileSelPtclKeyCount = 0;
        sFileSelLargeQuadCount = 0;
    }
    if (!sFileSelDebugReport) return;
    if (tag && std::strcmp(tag, "before_slots") == 0) {
        filesel_debug_print_post_ortho();
    }
    filesel_debug_probe_now(tag, sNativeFramebuffer);
}

void pc_gfx_filesel_debug_set_fx(int active)
{
    sFileSelFxWindow = active != 0 && sFileSelDebugReport;
}

void pc_gfx_filesel_debug_note_aspect(float aspect, int screenW, int screenH)
{
    if (!sFileSelDebugReport) return;
    printf("[PC Port] FILESEL fxCam: aspect=%.3f screen=%dx%d (4:3 would be 1.333) winAspect=%.3f\n",
           aspect, screenW, screenH, sCurrentAspectRatio);
    fflush(stdout);
}

void pc_gfx_filesel_debug_note_ptcl(unsigned blendFactor, unsigned zMode, unsigned tevMode, float scaleSize)
{
    if (!sFileSelDebugReport || !sFileSelFxWindow) return;
    const uint64_t key = uint64_t(blendFactor & 255)
        | (uint64_t(zMode & 255) << 8)
        | (uint64_t(tevMode & 255) << 16);
    for (int i = 0; i < sFileSelPtclKeyCount; ++i) {
        if (sFileSelPtclKeys[i] == key) return;
    }
    if (sFileSelPtclKeyCount >= 12) return;
    sFileSelPtclKeys[sFileSelPtclKeyCount++] = key;
    printf("[PC Port] FILESEL pcr: blendFactor=0x%02x zMode=0x%02x tevMode=%u scale=%.2f (src=%u dst=%u)\n",
           blendFactor, zMode, tevMode, scaleSize,
           blendFactor & 0xf, (blendFactor >> 4) & 0xf);
    fflush(stdout);
}

static uint64_t sPostOrthoFrame = 0;
static int sPostOrthoDraws = -1;
static float sPostOrthoNear = 0.0f, sPostOrthoFar = 0.0f;
static float sPostOrthoInvP00 = 0.0f, sPostOrthoInvP11 = 0.0f;
static int sPostOrthoViewport[4] = { 0, 0, 0, 0 };
static unsigned char sPostOrthoL[4] = { 0, 0, 0, 0 };
static unsigned char sPostOrthoC[4] = { 0, 0, 0, 0 };
static unsigned char sPostOrthoScan[7][4] = {};

static void filesel_debug_note_ortho_post()
{
    if (!filesel_debug_enabled() || !glBindFramebuffer_ptr) return;
    sPostOrthoFrame = sGfxFrameSerial;
    sPostOrthoDraws = int(sPerfDraws - sDrawsAtFrameStart);
    sPostOrthoNear = sViewNear;
    sPostOrthoFar = sViewFar;
    sPostOrthoInvP00 = sViewInvP00;
    sPostOrthoInvP11 = sViewInvP11;
    glGetIntegerv(GL_VIEWPORT, sPostOrthoViewport);
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    const int yBand = std::max(0, sRenderHeight * 3 / 10);
    glReadPixels(std::max(0, sRenderWidth / 20), yBand, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sPostOrthoL);
    glReadPixels(sRenderWidth / 2, sRenderHeight / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sPostOrthoC);
    const int xs[7] = {
        std::max(0, sRenderWidth / 20),
        std::max(0, sRenderWidth * 3 / 20),
        std::max(0, sRenderWidth / 4),
        sRenderWidth / 2,
        std::max(0, sRenderWidth * 3 / 4),
        std::max(0, sRenderWidth * 17 / 20),
        std::max(0, sRenderWidth * 19 / 20),
    };
    for (int i = 0; i < 7; ++i) {
        glReadPixels(xs[i], yBand, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, sPostOrthoScan[i]);
    }
}

static void filesel_debug_print_post_ortho()
{
    if (sPostOrthoFrame != sGfxFrameSerial) {
        printf("[PC Port] FILESEL post_ortho: none this frame (post has not run yet)\n");
        fflush(stdout);
        return;
    }
    printf("[PC Port] FILESEL post_ortho: draws=%d near=%.1f far=%.1f invP=(%.3f,%.3f) vp=(%d,%d,%d,%d) L=(%d,%d,%d) C=(%d,%d,%d)\n",
           sPostOrthoDraws, sPostOrthoNear, sPostOrthoFar,
           sPostOrthoInvP00, sPostOrthoInvP11,
           sPostOrthoViewport[0], sPostOrthoViewport[1], sPostOrthoViewport[2], sPostOrthoViewport[3],
           sPostOrthoL[0], sPostOrthoL[1], sPostOrthoL[2],
           sPostOrthoC[0], sPostOrthoC[1], sPostOrthoC[2]);
    printf("[PC Port] FILESEL post_ortho scan: 5%%=(%d,%d,%d) 15%%=(%d,%d,%d) 25%%=(%d,%d,%d) 50%%=(%d,%d,%d) 75%%=(%d,%d,%d) 85%%=(%d,%d,%d) 95%%=(%d,%d,%d)\n",
           sPostOrthoScan[0][0], sPostOrthoScan[0][1], sPostOrthoScan[0][2],
           sPostOrthoScan[1][0], sPostOrthoScan[1][1], sPostOrthoScan[1][2],
           sPostOrthoScan[2][0], sPostOrthoScan[2][1], sPostOrthoScan[2][2],
           sPostOrthoScan[3][0], sPostOrthoScan[3][1], sPostOrthoScan[3][2],
           sPostOrthoScan[4][0], sPostOrthoScan[4][1], sPostOrthoScan[4][2],
           sPostOrthoScan[5][0], sPostOrthoScan[5][1], sPostOrthoScan[5][2],
           sPostOrthoScan[6][0], sPostOrthoScan[6][1], sPostOrthoScan[6][2]);
    fflush(stdout);
}

static void filesel_debug_on_present(bool latePost, GLuint produced)
{
    if (!sFileSelDebugReport) return;
    filesel_debug_probe_now(latePost ? "present_after_post" : "present_no_late_post", produced);
    if (sLastAoReady && sAoFbo[0] && glBindFramebuffer_ptr && sAoWidth > 0 && sAoHeight > 0) {
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sAoFbo[0]);
        const int ax = std::max(0, sAoWidth / 20);
        const int ay = std::max(0, sAoHeight * 3 / 10);
        unsigned char aoL[4] = { 0, 0, 0, 0 };
        unsigned char ao15[4] = { 0, 0, 0, 0 };
        unsigned char aoC[4] = { 0, 0, 0, 0 };
        glReadPixels(ax, ay, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, aoL);
        glReadPixels(std::max(0, sAoWidth * 3 / 20), ay, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, ao15);
        glReadPixels(sAoWidth / 2, sAoHeight / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, aoC);
        printf("[PC Port] FILESEL AO: built=%d latePost=%d L=%d 15%%=%d C=%d (255=none, 0=full darken) produced=%u\n",
               sLastAoReady ? 1 : 0, latePost ? 1 : 0, aoL[0], ao15[0], aoC[0], unsigned(produced));
        glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);
    } else {
        printf("[PC Port] FILESEL AO: built=%d ssaoOn=%d latePost=%d produced=%u\n",
               sLastAoReady ? 1 : 0,
               pc_post_ssao_active(sPostEffects) ? 1 : 0,
               latePost ? 1 : 0,
               unsigned(produced));
    }
    fflush(stdout);
    sFileSelDebugReport = false;
    sFileSelFxWindow = false;
}

static PcTevShaderKey sLastTevKey;
static bool sLastTevKeyValid = false;

// Builds the key describing what the fragment stage must compute. Only
// configuration goes in; per-draw values stay as uniforms so materials that
// differ solely by colour still share one program.
static void build_tev_shader_key(PcTevShaderKey& key) {
    key = PcTevShaderKey();
    const int stages = std::clamp<int>(sNumTevStages, 1, GX_MAXTEVSTAGE);
    key.numStages = uint8_t(stages);
    for (int i = 0; i < stages; ++i) {
        const TevStageState& st = sTevStages[i];
        PcTevStageKey& out = key.stages[i];
        for (int arg = 0; arg < 4; ++arg) {
            out.colorIn[arg] = uint8_t(st.colorIn[arg]);
            out.alphaIn[arg] = uint8_t(st.alphaIn[arg]);
        }
        out.colorOp     = uint8_t(st.colorOp);
        out.colorBias   = uint8_t(st.colorBias);
        out.colorScale  = uint8_t(st.colorScale);
        out.colorClamp  = uint8_t(st.colorClamp ? 1 : 0);
        out.colorOutReg = uint8_t(st.colorOutReg);
        out.alphaOp     = uint8_t(st.alphaOp);
        out.alphaBias   = uint8_t(st.alphaBias);
        out.alphaScale  = uint8_t(st.alphaScale);
        out.alphaClamp  = uint8_t(st.alphaClamp ? 1 : 0);
        out.alphaOutReg = uint8_t(st.alphaOutReg);
        const bool textured = st.textureEnabled && st.texMap >= GX_TEXMAP0 && st.texMap < GX_MAX_TEXMAP;
        out.texMap     = textured ? int8_t(st.texMap) : int8_t(-1);
        // The ubershader clamps the varying set it carries to four slots.
        out.texCoord   = uint8_t(std::clamp<int>(int(st.texCoord), 0, 3));
        out.rasChannel = int8_t(st.rasChannel);
        out.rasSwapSel = uint8_t(sTevRasSwapSel[i] & 3);
        out.texSwapSel = uint8_t(sTevTexSwapSel[i] & 3);
        const TevIndState& ind = sTevInd[i];
        out.indMtx     = ind.mtx;
        out.indWrapS   = ind.wrapS;
        out.indWrapT   = ind.wrapT;
        out.indAddPrev = ind.addPrev;
        if (out.indMtx != 0) {
            out.indStage = ind.indStage;
            out.indFmt   = ind.fmt;
            out.indBias  = ind.bias;
        }
    }
    // Las etapas indirectas solo entran en la clave si alguna etapa TEV las lee.
    bool anyIndirect = false;
    for (int i = 0; i < stages; ++i) anyIndirect |= key.stages[i].indMtx != 0;
    if (anyIndirect) {
        key.numIndStages = uint8_t(std::min<int>(sNumIndStages, 4));
        for (int n = 0; n < key.numIndStages; ++n) {
            key.indTexCoord[n] = uint8_t(std::min<int>(sIndStages[n].texCoord, 3));
            key.indTexMap[n]   = uint8_t(sIndStages[n].texMap & 7);
            key.indScaleS[n]   = sIndStages[n].scaleS;
            key.indScaleT[n]   = sIndStages[n].scaleT;
        }
    }
    for (int t = 0; t < 4; ++t) {
        key.swapTable[t][0] = uint8_t(sTevSwapModes[t].red);
        key.swapTable[t][1] = uint8_t(sTevSwapModes[t].green);
        key.swapTable[t][2] = uint8_t(sTevSwapModes[t].blue);
        key.swapTable[t][3] = uint8_t(sTevSwapModes[t].alpha);
    }
    key.alphaComp0       = uint8_t(sAlphaComp0);
    key.alphaComp1       = uint8_t(sAlphaComp1);
    key.alphaTestOp      = uint8_t(sAlphaOp);
    key.useMaterialRgb   = uint8_t(sChannels[0].matSrc == GX_SRC_REG ? 1 : 0);
    key.useMaterialAlpha = uint8_t(sChannels[0].alphaMatSrc == GX_SRC_REG ? 1 : 0);
    key.useMaterialRgb1  = uint8_t(sChannels[1].matSrc == GX_SRC_REG ? 1 : 0);
    key.fog              = uint8_t(sFogRequested && sFogAllowed ? 1 : 0);
}

// Compiles and links one specialised program. Returns 0 on failure, which
// sends the caller back to the ubershader rather than dropping the draw.
static inline double submit_clock_ms();
static GLuint compile_specialised_program(const PcTevShaderKey& key) {
    if (!glCreateShader_ptr || !sSharedVertexShader) return 0;
    // El fuente del shader (std::string) no puede salir del heap de la seccion:
    // en pantallas con el heap lleno (Piklopedia) daria Memory Alloc Error.
    struct HostAllocScope {
        bool prev = pc_host_alloc_active();
        HostAllocScope() { pc_host_alloc_set(true); }
        ~HostAllocScope() { pc_host_alloc_set(prev); }
    } hostAllocScope;

    const double buildT0 = submit_clock_ms();
    const std::string source = pc_tev_build_fragment_source(key);
    const char* sourcePtr = source.c_str();

    // Cached binary first: no compile, no link, a few hundred microseconds.
    const uint64_t binaryKey = program_binary_key(vShaderSrc, sourcePtr);
    if (sProgramBinaryReady) {
        GLuint program = glCreateProgram_ptr();
        bind_fixed_attrib_locations(program);
        if (program_binary_load(program, binaryKey)) {
            sShaderBuildMsThisFrame += submit_clock_ms() - buildT0;
            return program;
        }
        glDeleteProgram_ptr(program);
    }

    GLuint fragment = glCreateShader_ptr(GL_FRAGMENT_SHADER);
    glShaderSource_ptr(fragment, 1, &sourcePtr, nullptr);
    glCompileShader_ptr(fragment);
    GLint status = 0;
    if (glGetShaderiv_ptr) glGetShaderiv_ptr(fragment, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char log[2048] = { 0 };
        if (glGetShaderInfoLog_ptr) glGetShaderInfoLog_ptr(fragment, sizeof(log), nullptr, log);
        fprintf(stderr, "[PC GX] Specialised TEV shader failed to compile:\n%s\nSource:\n%s\n",
                log, source.c_str());
        glDeleteShader_ptr(fragment);
        return 0;
    }

    GLuint program = glCreateProgram_ptr();
    glAttachShader_ptr(program, sSharedVertexShader);
    glAttachShader_ptr(program, fragment);
    bind_fixed_attrib_locations(program);
    if (sProgramBinaryReady && glProgramParameteri_ptr) {
        glProgramParameteri_ptr(program, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
        glGetError();
    }
    glLinkProgram_ptr(program);
    if (glGetProgramiv_ptr) glGetProgramiv_ptr(program, GL_LINK_STATUS, &status);
    glDeleteShader_ptr(fragment);
    if (status != GL_TRUE) {
        char log[2048] = { 0 };
        if (glGetProgramInfoLog_ptr) glGetProgramInfoLog_ptr(program, sizeof(log), nullptr, log);
        fprintf(stderr, "[PC GX] Specialised TEV program failed to link:\n%s\n", log);
        glDeleteProgram_ptr(program);
        return 0;
    }
    program_binary_store(program, binaryKey);
    sShaderBuildMsThisFrame += submit_clock_ms() - buildT0;
    ++sPerfShaderCompiles;
    gl_error_checkpoint("specialised program creation");
    if (const char* dumpDir = std::getenv("PIKMIN_DUMP_SHADERS")) {
        // A value starting with '/' names the directory (sandboxes may deny /tmp).
        char path[512];
        snprintf(path, sizeof(path), "%s/pikmin_tev_%llu.frag", dumpDir[0] == '/' ? dumpDir : "/tmp",
                 (unsigned long long)sPerfShaderCompiles);
        if (FILE* out = fopen(path, "w")) {
            fputs(source.c_str(), out);
            fclose(out);
        }
    }
    return program;
}

static void gl_program_cache_invalidate()
{
    state_touched();
    sCurrentProgram = 0;
    sLastTevKeyValid = false;
}

// Selects the program for the current TEV state and makes its uniform
// locations active. Falls back to the ubershader whenever generation fails.
static void use_program_for_current_state() {
    state_touched();
    if (!sSpecialiseShaders || sSpecialiseFailed
        || int(sNumTevStages) > sSpecialiseMaxStages) {
        if (sCurrentProgram != sShaderProgram) {
            glUseProgram_ptr(sShaderProgram);
            invalidate_uniform_cache();
            sCurrentProgram = sShaderProgram;
            sCheckProgram = sShaderProgram;
            sLoc = sUberLocations;
            sLastTevKeyValid = false;
        }
        return;
    }

    PcTevShaderKey key;
    build_tev_shader_key(key);
    if (sLastTevKeyValid && sCurrentProgram != 0 && sCurrentProgram != sShaderProgram
        && key == sLastTevKey) {
        return;
    }
    const uint64_t hash = pc_tev_hash_key(key);

    for (TevProgramEntry& entry : sTevPrograms) {
        if (entry.hash != hash || !(entry.key == key)) continue;
        if (sCurrentProgram != entry.program) {
            glUseProgram_ptr(entry.program);
            invalidate_uniform_cache();
            sCurrentProgram = entry.program;
            sCheckProgram = entry.program;
            sLoc = entry.locations;
        }
        sLastTevKey = key;
        sLastTevKeyValid = true;
        return;
    }

    const double compileT0 = submit_clock_ms();
    const GLuint program = compile_specialised_program(key);
    // Cada compilación es un tirón en móvil (decenas o cientos de ms): se
    // deja constancia con su coste para poder correlacionarla con parones.
    if (pc_gfx_verbose() || !program) printf("[PC GX] specialised TEV program #%zu: %u stages, %.1f ms%s\n",
           sTevPrograms.size() + 1, (unsigned)sNumTevStages, submit_clock_ms() - compileT0,
           program ? "" : " (FAILED)");
    if (!program) {
        // One failure is treated as a permanent fallback: a configuration this
        // generator cannot express must not be retried for every draw.
        sSpecialiseFailed = true;
        sLastTevKeyValid = false;
        fprintf(stderr, "[PC GX] Falling back to the TEV ubershader for the rest of the session.\n");
        glUseProgram_ptr(sShaderProgram);
        invalidate_uniform_cache();
        sCurrentProgram = sShaderProgram;
        sLoc = sUberLocations;
        return;
    }

    TevProgramEntry entry;
    entry.key = key;
    entry.hash = hash;
    entry.program = program;
    query_program_locations(program, entry.locations);
    sTevPrograms.push_back(entry);

    if (std::getenv("PIKMIN_DUMP_SHADERS")) {
        printf("[PC GX dump] program %llu stages=%d; samplers:",
               (unsigned long long)sPerfShaderCompiles, int(key.numStages));
        for (int i = 0; i < 8; ++i) {
            if (entry.locations.tex[i] < 0) continue;
            GLint bound = -1;
            if (glGetUniformiv_ptr) {
                glGetUniformiv_ptr(program, entry.locations.tex[i], &bound);
            }
            printf(" uTex%d=%d", i, bound);
        }
        // The stages that actually sample, so the two can be compared.
        printf(" | wants:");
        for (int i = 0; i < int(key.numStages); ++i) {
            printf(" s%d:map=%d,tc=%d", i, int(key.stages[i].texMap), int(key.stages[i].texCoord));
        }
        printf("\n");
        fflush(stdout);
    }

    glUseProgram_ptr(program);
    invalidate_uniform_cache();
    sCurrentProgram = program;
    sCheckProgram = program;
    sLoc = entry.locations;
    sLastTevKey = key;
    sLastTevKeyValid = true;
}

void pc_gfx_set_shader_specialisation(bool enabled) {
    pc_gfx_flush_batch();  // changes which program the next batch compiles to
    sSpecialiseShaders = enabled;
}

bool pc_gfx_get_shader_specialisation(void) {
    return sSpecialiseShaders && !sSpecialiseFailed;
}

size_t pc_gfx_get_specialised_program_count(void) {
    return sTevPrograms.size();
}

unsigned int pc_gfx_get_depth_texture(void)
{
    return sDepthIsTexture ? sNativeDepthTexture : 0;
}

unsigned int pc_gfx_get_colour_texture(void)
{
    return sNativeFramebufferReady ? sNativeColorTexture : 0;
}

void pc_gfx_get_render_size(int* width, int* height)
{
    if (width) *width = sRenderWidth;
    if (height) *height = sRenderHeight;
}

// Per-frame submission cost, accumulated across every GX primitive and handed
// to the tick profiler once per frame. Only touched when PIKMIN_TICK_STATS is
// on: three clock reads per draw would otherwise be the very overhead under
// investigation.
static double sSubmitUniformMs = 0.0;
static double sSubmitVboMs     = 0.0;
static double sSubmitDrawMs    = 0.0;
// CPU spent inside pc_gfx_call_display_list minus whatever the three above
// accrued while there: parsing, dequantising and transforming vertices.
static double sSubmitDlMs      = 0.0;
static double sSubmitKeyMs     = 0.0;
static double sSubmitProgramMs = 0.0;
static double sSubmitTexBindMs = 0.0;
static uint32_t sSubmitDraws   = 0;
static uint64_t sSubmitVerts   = 0;
static uint32_t sSubmitPrims   = 0;  // GX primitives, i.e. draws before batching
// Display-list parse failures this frame. The individual messages report once
// and then go quiet, which hides whether a fault happened at startup or is
// recurring every frame -- the difference between a cosmetic warning and a
// parser that is desynced and drawing garbage.
static uint32_t sDlDesyncs   = 0;
static uint32_t sBadMtxIdx   = 0;
static uint32_t sWildVerts   = 0;
// Captured at the position fetch so a wild vertex can say which of the three
// inputs to `base + index * stride` was wrong.
static u16      sLastPosIndex  = 0;
static const u8* sLastPosBase  = nullptr;
static u8       sLastPosStride = 0;

// The one-shot message said a desync happened but not which list, where in it,
// or what the bytes look like -- which is the difference between "the game
// handed us a buffer that was already garbage" and "the parser drifted part
// way through a valid list". Reports the first few with context, then stops.
static u8 attr_inline_size(GXAttr attr, const VertexFormatState& fmt);
static int sDesyncReports = 0;
static void report_desync(const char* what, const void* list, size_t offset, u32 nbytes) {
    if (sDesyncReports >= 8) return;
    ++sDesyncReports;
    const u8* base = static_cast<const u8*>(list);
    fprintf(stderr, "[PC GX] DESYNC #%d %s at byte %zu/%u  head:", sDesyncReports, what, offset, nbytes);
    for (size_t i = 0; i < 12 && i < nbytes; ++i) fprintf(stderr, " %02X", base[i]);
    fprintf(stderr, "  near:");
    const size_t from = offset > 6 ? offset - 6 : 0;
    for (size_t i = from; i < from + 12 && i < nbytes; ++i) {
        fprintf(stderr, "%s%02X", i == offset ? " >" : " ", base[i]);
    }
    fprintf(stderr, "\n");
    // Full command walk of the list up to the failure, for material lists.
    {
        size_t at = 0;
        int printed = 0;
        fprintf(stderr, "[PC GX] DESYNC #%d walk:", sDesyncReports);
        while (at < offset && printed < 400) {
            const u8 c = base[at];
            size_t len = 1;
            if (c == 0x61) { fprintf(stderr, " BP%02X:%02X%02X%02X", base[at+1], base[at+2], base[at+3], base[at+4]); len = 5; }
            else if (c == 0x08) { fprintf(stderr, " CP%02X", base[at+1]); len = 6; }
            else if (c == 0x10) { const u32 n = ((base[at+1] << 8) | base[at+2]) + 1; fprintf(stderr, " XF%02X%02X[%u]", base[at+3], base[at+4], n); len = 5 + n * 4; }
            else if (c == 0x00) { len = 1; }
            else { fprintf(stderr, " ?%02X@%zu", c, at); break; }
            at += len; ++printed;
        }
        fprintf(stderr, "\n");
    }

    // The stride is what actually went wrong: the head of every list parses,
    // so the parser is consuming the wrong number of bytes per vertex and
    // landing mid-data. Print the descriptor that produced it.
    fprintf(stderr, "[PC GX] DESYNC #%d vtxdesc:", sDesyncReports);
    unsigned stride = 0;
    for (int a = GX_VA_PNMTXIDX; a <= GX_VA_TEX7; ++a) {
        const GXAttrType d = sVtxDesc[a];
        if (d == GX_NONE) continue;
        const char* kind = d == GX_DIRECT ? "direct" : d == GX_INDEX8 ? "idx8"
                         : d == GX_INDEX16 ? "idx16" : "?";
        unsigned bytes = d == GX_DIRECT
                             ? attr_inline_size(static_cast<GXAttr>(a), sVtxFormats[0][a])
                             : (d == GX_INDEX8 ? 1u : 2u);
        stride += bytes;
        fprintf(stderr, " a%d=%s(%u)", a, kind, bytes);
    }
    fprintf(stderr, "  stride=%u bytes\n", stride);
}

// Consecutive draws that share every piece of state a batch would have to
// share. sRunKey is the previous draw's key; a run ends when it differs.
static uint64_t sRunKey        = 0;
static bool     sHaveRunKey    = false;
static uint32_t sRunCurrent    = 0;  // draws in the run being built
static uint32_t sRunLongest    = 0;
static uint32_t sRunCount      = 0;  // runs closed this frame
static uint32_t sRunGlBreaks   = 0;  // of which, broken by GL pipeline state
static uint64_t sRunEpoch      = 0;  // GL epoch the current run was opened at

// Eight bytes per round rather than one. This runs for every GX primitive --
// 25.000 times in a heavy frame -- because the batcher, not just the profiler,
// depends on it: a byte-at-a-time FNV over the ~2 KB of live state measured
// 42 ms/frame and became the bottleneck it was meant to remove.
//
// Four independent lanes rather than one: the multiply of each round depended
// on the previous round's result, so the CPU ran one 8-byte word per ~5
// cycles. Measured at 6.8 ms/frame on an Adreno-class phone (PLAN_RENDIMIENTO
// fase 2), i.e. more than the uniform writes it decides. With four lanes the
// multiplies overlap; the lanes are folded at the end.
static inline uint64_t hash_bytes(uint64_t h, const void* data, size_t bytes) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    constexpr uint64_t kPrime = 1099511628211ull;
    if (bytes >= 32) {
        uint64_t h0 = h, h1 = h ^ 0x9E3779B97F4A7C15ull, h2 = h ^ 0xC2B2AE3D27D4EB4Full, h3 = h ^ 0x165667B19E3779F9ull;
        while (bytes >= 32) {
            uint64_t w0, w1, w2, w3;
            memcpy(&w0, p, 8);
            memcpy(&w1, p + 8, 8);
            memcpy(&w2, p + 16, 8);
            memcpy(&w3, p + 24, 8);
            h0 = (h0 ^ w0) * kPrime;
            h1 = (h1 ^ w1) * kPrime;
            h2 = (h2 ^ w2) * kPrime;
            h3 = (h3 ^ w3) * kPrime;
            p += 32;
            bytes -= 32;
        }
        h = (h0 ^ (h1 >> 29)) * kPrime;
        h = (h ^ (h2 >> 31)) * kPrime;
        h = (h ^ (h3 >> 27)) * kPrime;
        h ^= h >> 29;
    }
    while (bytes >= 8) {
        uint64_t word;
        memcpy(&word, p, 8);
        h = (h ^ word) * kPrime;
        h ^= h >> 29;
        p += 8;
        bytes -= 8;
    }
    if (bytes) {
        uint64_t tail = 0;
        memcpy(&tail, p, bytes);
        h = (h ^ tail) * kPrime;
        h ^= h >> 29;
    }
    return h;
}

// Everything the uniform block below reads, plus the bound textures and the
// primitive class. Deliberately over-inclusive: a key that is too coarse would
// report runs that a real batcher could not merge, which is the one error that
// would make this measurement worse than useless.
static uint64_t compute_batch_state_key_full();
static bool statekey_check_enabled() {
    static const bool enabled = std::getenv("PIKMIN_STATEKEY_CHECK") != nullptr;
    return enabled;
}
// The three inputs that legitimately change between consecutive primitives
// without any setter running are kept beside the generation: the primitive
// class (per pc_gfx_begin), whether the vertices arrived pretransformed (per
// vertex source) and, for immediate geometry, the selected matrix.
static uint64_t compute_batch_state_key() {
    static uint32_t cachedGen = 0;
    static bool cachedPretransformed = false;
    static bool cachedUsesPalette = false;
    static u32 cachedMtx = ~0u;
    static GXPrimitive cachedPrim = GXPrimitive(0);
    static uint64_t cachedKey = 0;
    const u32 mtx = sVerticesPretransformed ? 0 : sCurrentPosMtxId;
    if (cachedGen != sStateGen || cachedPretransformed != sVerticesPretransformed
        || cachedUsesPalette != sVertexUsesPalette || cachedMtx != mtx
        || cachedPrim != sCurrentPrimType) {
        cachedKey = compute_batch_state_key_full();
        cachedGen = sStateGen;
        cachedPretransformed = sVerticesPretransformed;
        cachedUsesPalette = sVertexUsesPalette;
        cachedMtx = mtx;
        cachedPrim = sCurrentPrimType;
    } else if (statekey_check_enabled()) {
        const uint64_t full = compute_batch_state_key_full();
        if (full != cachedKey) {
            static int reported = 0;
            if (reported++ < 20) {
                fprintf(stderr, "[PC GX statekey] state changed without state_touched() (gen %u): a setter is missing\n", sStateGen);
            }
            cachedKey = full;
        }
    }
    return cachedKey;
}

static uint64_t compute_batch_state_key_full() {
    uint64_t h = 14695981039346656037ull;
    h = hash_bytes(h, &sCurrentProgram, sizeof(sCurrentProgram));
    h = hash_bytes(h, &sProjMtxGen, sizeof(sProjMtxGen));
    h = hash_bytes(h, &sVerticesPretransformed, sizeof(sVerticesPretransformed));
    h = hash_bytes(h, &sVertexUsesPalette, sizeof(sVertexUsesPalette));
    // Only the matrices actually selected: hashing all 64 slots would report a
    // break every time an unrelated model loaded its own matrix.
    if (!sVerticesPretransformed) {
        const u32 id = sCurrentPosMtxId < 64 ? sCurrentPosMtxId : 0;
        const uint64_t mtxRevision = (uint64_t(id) << 40) ^ (uint64_t(sPosMtxGen[id]) << 20)
                                   ^ uint64_t(sNrmMtxGen[id]);
        h = hash_bytes(h, &mtxRevision, sizeof(mtxRevision));
    } else if (sVertexUsesPalette) {
        // A GPU-skinned batch captures the ten matrices current when it opens.
        // Without these revisions, primitives from different animated models
        // merge and the whole batch is drawn with the last model's palette.
        for (u32 slot = 0; slot < 10; ++slot) {
            const u32 id = slot * 3;
            const uint64_t revision = (uint64_t(id) << 40)
                                    ^ (uint64_t(sPosMtxGen[id]) << 20)
                                    ^ uint64_t(sNrmMtxGen[id]);
            h = hash_bytes(h, &revision, sizeof(revision));
        }
    }
    h = hash_bytes(h, sChannels, sizeof(sChannels));
    for (int ch = 0; ch < 2; ++ch) {
        if (!sChannels[ch].enabled) continue;
        const u32 mask = sChannels[ch].lightMask;
        for (int i = 0; i < 8; ++i) {
            if (mask & (1u << i)) {
                const uint64_t lightRevision = (uint64_t(i) << 40) ^ uint64_t(sLightGen[i]);
                h = hash_bytes(h, &lightRevision, sizeof(lightRevision));
            }
        }
    }
    h = hash_bytes(h, &sAlphaComp0, sizeof(sAlphaComp0));
    h = hash_bytes(h, &sAlphaComp1, sizeof(sAlphaComp1));
    h = hash_bytes(h, &sAlphaOp, sizeof(sAlphaOp));
    h = hash_bytes(h, &sAlphaRef0, sizeof(sAlphaRef0));
    h = hash_bytes(h, &sAlphaRef1, sizeof(sAlphaRef1));
    h = hash_bytes(h, sTevRegisters, sizeof(sTevRegisters));
    h = hash_bytes(h, sKonstColors, sizeof(sKonstColors));
    h = hash_bytes(h, &sNumTevStages, sizeof(sNumTevStages));
    const int stages = sNumTevStages < GX_MAXTEVSTAGE ? sNumTevStages : GX_MAXTEVSTAGE;
    for (int i = 0; i < stages; ++i) {
        h = hash_bytes(h, &sTevStages[i], sizeof(sTevStages[i]));
        h = hash_bytes(h, &sTevRasSwapSel[i], sizeof(sTevRasSwapSel[i]));
        h = hash_bytes(h, &sTevTexSwapSel[i], sizeof(sTevTexSwapSel[i]));
    }
    h = hash_bytes(h, sTevSwapModes, sizeof(sTevSwapModes));
    for (int slot = 0; slot < 8; ++slot) {
        h = hash_bytes(h, &sTexCoordGen[slot], sizeof(sTexCoordGen[slot]));
        if (sTexCoordGen[slot].active) {
            const u32 m = sTexCoordGen[slot].mtxIdx < 64 ? sTexCoordGen[slot].mtxIdx : 0;
            const uint64_t texRevision = (uint64_t(m) << 32) ^ uint64_t(sTexMtxGen[m]);
            h = hash_bytes(h, &texRevision, sizeof(texRevision));
        }
    }
    h = hash_bytes(h, &sPostMtxGen, sizeof(sPostMtxGen));
    h = hash_bytes(h, &sTexBakedMask, sizeof(sTexBakedMask));
    h = hash_bytes(h, &sTexPaletteGen, sizeof(sTexPaletteGen));
    h = hash_bytes(h, sActiveGLTextures, sizeof(sActiveGLTextures));
    h = hash_bytes(h, sHasActiveTextures, sizeof(sHasActiveTextures));
    // Triangles, strips, fans and quads can all become GL_TRIANGLES and so may
    // share a batch; lines and points cannot mix with them or each other.
    int primClass = 0;
    switch (sCurrentPrimType) {
        case GX_LINES: case GX_LINESTRIP: primClass = 1; break;
        case GX_POINTS:                   primClass = 2; break;
        default:                          primClass = 0; break;
    }
    h = hash_bytes(h, &primClass, sizeof(primClass));
    return h;
}

// Called once per draw while PIKMIN_TICK_STATS is on. Splits run breaks by
// cause: a break that a batcher could never avoid (GL pipeline state changed
// outside our reach) counts differently from one caused by material state.
static inline double submit_clock_ms();

// ── Primitive batching (PERF-NATIVE-002) ────────────────────────────────────
//
// The port used to emit one glDrawArrays per GX primitive: ~8.000 draws of ten
// vertices per frame, each preceded by the whole uniform block. Measurement
// (see SIGUIENTE_SESION.md) showed consecutive primitives share state in runs
// of ~20, and 60-80 in the heavy frames, so the draws collapse to a few
// hundred.
//
// The scheme rests on one invariant, and every flush point below exists to
// keep it: BETWEEN OPENING A BATCH AND DRAWING IT, NOTHING MAY TOUCH GL STATE
// OR UNIFORMS. The uniforms for a batch are written once, when it opens; the
// draw happens later, and reads whatever the GPU still holds. So a batch must
// be flushed before any GL mutation, and drawn *before* the next batch's
// uniforms are written. Material state needs no flush of its own: it is
// covered by the state key, which is compared on every primitive.
static PcGfxHostVector<Vertex> sBatchVerts;
static bool     sBatchOpen = false;
static uint64_t sBatchKey  = 0;
static GLenum   sBatchMode = GL_TRIANGLES;
static uint32_t sBatchPrims = 0;   // primitives merged into the open batch

// Everything becomes GL_TRIANGLES (or GL_LINES) so that consecutive primitives
// of different topologies can still share one draw. Order is preserved
// exactly, and so is winding: a strip's odd triangles keep the swap that
// GL_TRIANGLE_STRIP would have applied, or every other face would flip.
static void append_primitive(const PcGfxHostVector<Vertex>& v, GXPrimitive type, PcGfxHostVector<Vertex>& sBatchVerts) {
    const size_t n = v.size();
    switch (type) {
    case GX_TRIANGLES:
        sBatchVerts.insert(sBatchVerts.end(), v.begin(), v.end());
        break;
    case GX_TRIANGLESTRIP:
        for (size_t i = 2; i < n; ++i) {
            if ((i & 1) == 0) {
                sBatchVerts.push_back(v[i - 2]);
                sBatchVerts.push_back(v[i - 1]);
            } else {
                sBatchVerts.push_back(v[i - 1]);
                sBatchVerts.push_back(v[i - 2]);
            }
            sBatchVerts.push_back(v[i]);
        }
        break;
    case GX_TRIANGLEFAN:
        for (size_t i = 2; i < n; ++i) {
            sBatchVerts.push_back(v[0]);
            sBatchVerts.push_back(v[i - 1]);
            sBatchVerts.push_back(v[i]);
        }
        break;
    case GX_QUADS:
        for (size_t i = 0; i + 3 < n; i += 4) {
            sBatchVerts.push_back(v[i]);
            sBatchVerts.push_back(v[i + 1]);
            sBatchVerts.push_back(v[i + 2]);
            sBatchVerts.push_back(v[i]);
            sBatchVerts.push_back(v[i + 2]);
            sBatchVerts.push_back(v[i + 3]);
        }
        break;
    case GX_LINES:
        sBatchVerts.insert(sBatchVerts.end(), v.begin(), v.end());
        break;
    case GX_LINESTRIP:
        for (size_t i = 1; i < n; ++i) {
            sBatchVerts.push_back(v[i - 1]);
            sBatchVerts.push_back(v[i]);
        }
        break;
    case GX_POINTS:
        sBatchVerts.insert(sBatchVerts.end(), v.begin(), v.end());
        break;
    }
}

static void append_primitive_to_batch() { append_primitive(sVertexStream, sCurrentPrimType, sBatchVerts); }

// PIKMIN_BATCH=0 draws every primitive on its own, exactly as before this
// change. It is the A/B for the failure mode this design can produce: a
// missed flush point corrupts pixels in a way that depends on scene content,
// and comparing the two modes in the same scene says in one step whether a
// visual defect belongs to batching or was already there.
static bool batching_enabled() {
    static const bool enabled = [] {
        const char* value = getenv("PIKMIN_BATCH");
        return !(value != nullptr && value[0] == '0');
    }();
    return enabled;
}

static GLenum batch_mode_for(GXPrimitive prim) {
    switch (prim) {
    case GX_LINES: case GX_LINESTRIP: return GL_LINES;
    case GX_POINTS:                   return GL_POINTS;
    default:                          return GL_TRIANGLES;
    }
}

// Mapeo sin sincronizar por defecto en GLES (el caso móvil); en escritorio se
// conserva glBufferSubData, que es la ruta medida a 60 FPS. PIKMIN_VBO_MAP=0/1
// fuerza una u otra para comparar.
static bool vbo_upload_by_mapping() {
    static const bool enabled = [] {
        if (const char* v = std::getenv("PIKMIN_VBO_MAP")) return v[0] == '1';
        return PIKI_USE_GLES != 0;
    }();
    return enabled;
}

// ── Anillo de vértices con fences (ruta de mapeo) ───────────────────────────
//
// Mapear sin sincronizar obliga a garantizar por nuestra cuenta que la zona
// escrita no la está leyendo la GPU. Dejar el buffer huérfano cada frame no
// lo garantiza en Adreno: el driver recicla el almacenamiento del frame
// anterior aunque sus draws sigan en vuelo, y el resultado son vértices
// basura parpadeando (visto en el logo y en la animación de hojas). En su
// lugar el VBO es un anillo: cada frame deja un fence sobre el rango que
// escribió, y antes de escribir encima de un rango se espera a su fence.
// Con 3 frames de capacidad la espera no ocurre nunca en la práctica.
struct VboFenceRange {
    GLsync sync;
    size_t start, end; // [start, end) sin envolver: end > start
};
static PcGfxHostVector<VboFenceRange> sVboFences;
static size_t sVboFrameStart = 0;

static bool vbo_ranges_overlap(size_t a0, size_t a1, size_t b0, size_t b1) {
    return a0 < b1 && b0 < a1;
}

static void vbo_fence_current_range(size_t end) {
    if (!glFenceSync_ptr || end <= sVboFrameStart) { sVboFrameStart = end; return; }
    sVboFences.push_back({ glFenceSync_ptr(GL_SYNC_GPU_COMMANDS_COMPLETE, 0), sVboFrameStart, end });
    sVboFrameStart = end;
}

// Reserva [offset, offset+bytes) para escribir ahora mismo. Envuelve al
// principio si no cabe, y espera a cualquier fence cuyo rango pise.
static size_t vbo_ring_reserve(size_t bytes) {
    if (sVboWriteOffset + bytes > sVboCapacity) {
        // Lo escrito en este frame antes de envolver queda protegido por su
        // propio fence: si el frame no cabe entero, se espera a la GPU.
        vbo_fence_current_range(sVboWriteOffset);
        sVboWriteOffset = 0;
        sVboFrameStart = 0;
    }
    const size_t off = sVboWriteOffset;
    for (size_t i = 0; i < sVboFences.size();) {
        VboFenceRange& f = sVboFences[i];
        if (vbo_ranges_overlap(off, off + bytes, f.start, f.end)) {
            if (glClientWaitSync_ptr) glClientWaitSync_ptr(f.sync, GL_SYNC_FLUSH_COMMANDS_BIT, ~GLuint64(0));
            if (glDeleteSync_ptr) glDeleteSync_ptr(f.sync);
            sVboFences.erase(sVboFences.begin() + i);
        } else {
            ++i;
        }
    }
    return off;
}

// Al empezar un frame: fence sobre lo escrito por el anterior, y limpieza de
// fences ya señalados para que la lista no crezca.
static void vbo_ring_frame_begin() {
    sVboBytesLastFrame = sVboWriteOffset >= sVboFrameStart ? sVboWriteOffset - sVboFrameStart
                                                          : sVboCapacity - sVboFrameStart + sVboWriteOffset;
    if (sVboBytesLastFrame > sVboBytesPeakFrame) sVboBytesPeakFrame = sVboBytesLastFrame;
    vbo_fence_current_range(sVboWriteOffset);
    for (size_t i = 0; i < sVboFences.size();) {
        VboFenceRange& f = sVboFences[i];
        if (glClientWaitSync_ptr && glClientWaitSync_ptr(f.sync, 0, 0) != GL_TIMEOUT_EXPIRED) {
            if (glDeleteSync_ptr) glDeleteSync_ptr(f.sync);
            sVboFences.erase(sVboFences.begin() + i);
        } else {
            ++i;
        }
    }
}

// Draws whatever has accumulated. Safe to call at any time: a no-op when no
// batch is open, which is what makes it cheap to place at every flush point.
void pc_gfx_flush_batch(void) {
    if (!sBatchOpen || sBatchVerts.empty()) {
        sBatchOpen  = false;
        sBatchPrims = 0;
        sBatchVerts.clear();
        return;
    }

    const bool profiling = pc_tick_profiler_enabled();
    const double t0 = profiling ? submit_clock_ms() : 0.0;

    const size_t vertexBytes = sBatchVerts.size() * sizeof(Vertex);
    const bool mapping = vbo_upload_by_mapping() && glMapBufferRange_ptr && glUnmapBuffer_ptr;
    if (mapping) {
        if (vertexBytes > sVboCapacity) {
            // Crecer es dejar huérfano: solo tras esperar a todo lo pendiente.
            for (VboFenceRange& f : sVboFences) {
                if (glClientWaitSync_ptr) glClientWaitSync_ptr(f.sync, GL_SYNC_FLUSH_COMMANDS_BIT, ~GLuint64(0));
                if (glDeleteSync_ptr) glDeleteSync_ptr(f.sync);
            }
            sVboFences.clear();
            while (sVboCapacity < vertexBytes) sVboCapacity *= 2;
            glBufferData_ptr(GL_ARRAY_BUFFER, sVboCapacity, nullptr, GL_STREAM_DRAW);
            sVboWriteOffset = 0;
            sVboFrameStart = 0;
        }
        sVboWriteOffset = vbo_ring_reserve(vertexBytes);
    } else if (vertexBytes > sVboCapacity - sVboWriteOffset) {
        if (vertexBytes > sVboCapacity) {
            while (sVboCapacity < vertexBytes) sVboCapacity *= 2;
        }
        // Orphaning is safe here: draws already queued retain the old storage.
        glBufferData_ptr(GL_ARRAY_BUFFER, sVboCapacity, nullptr, GL_STREAM_DRAW);
        sVboWriteOffset = 0;
    }
    // Cómo llegan los vértices al VBO importa más que cuántos son. Mesa
    // renombra el buffer bajo glBufferSubData y la escritura no espera a nada;
    // los drivers móviles (Adreno, Mali) no: si la GPU aún lee draws
    // anteriores del mismo buffer, glBufferSubData se bloquea hasta que
    // terminen, y con ~400 draws por frame eso son ~0,75 ms de espera por
    // draw (medido en un Adreno 740: 300 ms por frame solo aquí). El mapeo
    // sin sincronizar promete al driver que la zona escrita no está en uso
    // -- cierto, porque el offset solo avanza y el buffer se deja huérfano
    // al dar la vuelta -- y no espera nunca.
    //
    // Sin GL_MAP_INVALIDATE_RANGE_BIT, y no por descuido: con él, el driver de
    // Adreno ignora UNSYNCHRONIZED, reserva un buffer temporal y copia con
    // espera (0,78 ms por llamada, medido; 0,000 ms sin el flag). Solo con
    // WRITE|UNSYNCHRONIZED el juego pasó de 2 a 60 FPS en el mismo dispositivo.
    bool uploaded = false;
    if (mapping) {
        void* dst = glMapBufferRange_ptr(GL_ARRAY_BUFFER, sVboWriteOffset, vertexBytes,
                                         GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (dst) {
            memcpy(dst, sBatchVerts.data(), vertexBytes);
            uploaded = glUnmapBuffer_ptr(GL_ARRAY_BUFFER) == GL_TRUE;
        }
    }
    if (!uploaded) glBufferSubData_ptr(GL_ARRAY_BUFFER, sVboWriteOffset, vertexBytes, sBatchVerts.data());

    const double t1 = profiling ? submit_clock_ms() : 0.0;
    if (profiling) sSubmitVboMs += t1 - t0;

    gl_error_checkpoint("batch upload");
    const GLint firstVertex = static_cast<GLint>(sVboWriteOffset / sizeof(Vertex));
    shadow_record_stream(sBatchVerts, sBatchMode);
    // PIKMIN_PROBE_DIFF=1: read the viewport back before and after each batch
    // and report how many pixels the draw changed, and where. Diagnostic
    // only. malloc, not new: operator new is routed to the game's heaps.
    static const bool probeDiff = getenv("PIKMIN_PROBE_DIFF") != nullptr;
    static unsigned char* diffBefore = nullptr;
    static unsigned char* diffAfter = nullptr;
    static size_t diffCap = 0;
    GLint diffVp[4] = {0, 0, 0, 0};
    bool diffArmed = false;
    if (probeDiff && sBatchVerts.size() <= 64) {
        glGetIntegerv(GL_VIEWPORT, diffVp);
        const size_t need = size_t(diffVp[2]) * diffVp[3] * 4;
        if (need > diffCap) { diffBefore = (unsigned char*)realloc(diffBefore, need); diffAfter = (unsigned char*)realloc(diffAfter, need); diffCap = need; }
        glReadPixels(diffVp[0], diffVp[1], diffVp[2], diffVp[3], GL_RGBA, GL_UNSIGNED_BYTE, diffBefore);
        diffArmed = true;
    }
    glDrawArrays(sBatchMode, firstVertex, (GLsizei)sBatchVerts.size());
    gl_error_checkpoint("batch draw");
    if (diffArmed) {
        glReadPixels(diffVp[0], diffVp[1], diffVp[2], diffVp[3], GL_RGBA, GL_UNSIGNED_BYTE, diffAfter);
        size_t changed = 0; int minX = 1 << 30, minY = 1 << 30, maxX = -1, maxY = -1;
        for (int y = 0; y < diffVp[3]; ++y) for (int x = 0; x < diffVp[2]; ++x) {
            const size_t i = (size_t(y) * diffVp[2] + x) * 4;
            if (memcmp(diffBefore + i, diffAfter + i, 3) != 0) { ++changed; minX = std::min(minX, x); maxX = std::max(maxX, x); minY = std::min(minY, y); maxY = std::max(maxY, y); }
        }
        // Where the first vertex lands: pos matrix (column-major) then projection.
        const Vertex& fv = sBatchVerts[0];
        const float* pm = sPosMatrix[sCurrentPosMtxId < 64 ? sCurrentPosMtxId : 0];
        float w[4] = { pm[0] * fv.x + pm[4] * fv.y + pm[8] * fv.z + pm[12], pm[1] * fv.x + pm[5] * fv.y + pm[9] * fv.z + pm[13],
                       pm[2] * fv.x + pm[6] * fv.y + pm[10] * fv.z + pm[14], 1.0f };
        float c[4];
        for (int r = 0; r < 4; ++r) c[r] = sProjMatrix[r] * w[0] + sProjMatrix[4 + r] * w[1] + sProjMatrix[8 + r] * w[2] + sProjMatrix[12 + r] * w[3];
        fprintf(stderr, "[PC ProbeDiff] frame=%u verts=%zu changed=%zu bbox=(%d,%d)-(%d,%d) of %dx%d mtx=%u view=(%.1f,%.1f,%.1f) ndc=(%.2f,%.2f,%.2f) w=%.2f\n",
                (unsigned)sFrameSerial, sBatchVerts.size(), changed, minX, minY, maxX, maxY, diffVp[2], diffVp[3],
                sCurrentPosMtxId, w[0], w[1], w[2], c[0] / c[3], c[1] / c[3], c[2] / c[3], c[3]);
        if (changed == 0 && sHasActiveTextures[0]) {
            GLint w0 = 0, h0 = 0, prevTex = 0;
            glActiveTexture_ptr(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
            fprintf(stderr, "[PC ProbeDiff]   unit0 real=%d record=%u boundCache=%u\n", prevTex, sActiveGLTextures[0], sBoundTextures[0]);
            glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w0);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h0);
            const size_t n = size_t(w0) * h0 * 4;
            unsigned char* px = (unsigned char*)malloc(n);
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
            size_t opaque = 0, lum = 0;
            for (size_t i = 0; i < n; i += 4) { if (px[i + 3] > 8) ++opaque; lum += px[i]; }
            fprintf(stderr, "[PC ProbeDiff]   tex0 gl=%u %dx%d opaque=%zu/%zu meanR=%zu uv0=(%.2f,%.2f)\n", sActiveGLTextures[0], w0, h0, opaque, n / 4, n ? lum / (n / 4) : 0, fv.tex[0][0], fv.tex[0][1]);
            free(px);
            glBindTexture(GL_TEXTURE_2D, prevTex);
        }
    }

    {
        static int probeCount = 0;
        static const int probeLimit = getenv("PIKMIN_PROBE_DRAWS") ? atoi(getenv("PIKMIN_PROBE_DRAWS")) : (pc_gfx_verbose() ? 25 : 0);
        static const unsigned probeFrame = getenv("PIKMIN_PROBE_FRAME") ? (unsigned)atoi(getenv("PIKMIN_PROBE_FRAME")) : 0u;
        // PIKMIN_PROBE_TEXSIZE=WxH: only draws with a texture of that size bound to units 0-3.
        static int probeTexW = 0, probeTexH = 0;
        static const bool probeTexSize = getenv("PIKMIN_PROBE_TEXSIZE")
            && sscanf(getenv("PIKMIN_PROBE_TEXSIZE"), "%dx%d", &probeTexW, &probeTexH) == 2;
        // PIKMIN_PROBE_PIXEL=x,y (frame-dump coordinates, y down): only draws that change that pixel.
        static int probePx = -1, probePy = -1;
        static const bool probePixel = getenv("PIKMIN_PROBE_PIXEL")
            && sscanf(getenv("PIKMIN_PROBE_PIXEL"), "%d,%d", &probePx, &probePy) == 2;
        static unsigned char lastPix[4] = {0, 0, 0, 0};
        bool probePixelOk = !probePixel;
        if (probePixel && sFrameSerial >= probeFrame && probeCount < probeLimit) {
            GLint vpNow[4] = {0, 0, 0, 0};
            glGetIntegerv(GL_VIEWPORT, vpNow);
            unsigned char pix[4] = {0, 0, 0, 0};
            const bool sc = glIsEnabled(GL_SCISSOR_TEST);
            if (sc) glDisable(GL_SCISSOR_TEST);
            GLint drawFbo = 0, readFbo = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
            if (readFbo != drawFbo) glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, (GLuint)drawFbo);
            glReadPixels(probePx, sRenderHeight - 1 - probePy, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pix);
            if (readFbo != drawFbo) glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, (GLuint)readFbo);
            if (sc) glEnable(GL_SCISSOR_TEST);
            (void)vpNow;
            if (memcmp(pix, lastPix, 3) != 0) {
                GLint fbo = 0; glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo);
                probePixelOk = true;
                fprintf(stderr, "[PC ProbePixel] frame=%u now %d,%d,%d fbo=%d vp=%d,%d,%d,%d rh=%d\n", (unsigned)sFrameSerial, pix[0], pix[1], pix[2], fbo, vpNow[0], vpNow[1], vpNow[2], vpNow[3], sRenderHeight);
            }
            memcpy(lastPix, pix, 4);
        }
        bool probeSizeOk = !probeTexSize && probePixelOk;
        if (probeTexSize && probePixelOk && sFrameSerial >= probeFrame && probeCount < probeLimit) {
            for (int u = 0; u < 4 && !probeSizeOk; ++u) {
                if (!sHasActiveTextures[u]) continue;
                GLint w = 0, h = 0;
                glActiveTexture_ptr(GL_TEXTURE0 + u);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
                probeSizeOk = (w == probeTexW && h == probeTexH);
            }
            glActiveTexture_ptr(GL_TEXTURE0);
        }
        // PIKMIN_PROBE_AFTER=N: also report the N draws that follow a match
        // (a model's later materials, drawn right after the one filtered on).
        static const int probeAfter = getenv("PIKMIN_PROBE_AFTER") ? atoi(getenv("PIKMIN_PROBE_AFTER")) : 0;
        static int probeTail = 0;
        if (probeSizeOk && probeAfter > 0 && sFrameSerial >= probeFrame) probeTail = probeAfter;
        else if (!probeSizeOk && probeTail > 0 && sFrameSerial >= probeFrame) { probeSizeOk = true; --probeTail; }
        if (probeSizeOk && sFrameSerial >= probeFrame && probeCount++ < probeLimit) {
            GLint boundFBO = 0;
            GLint vp[4] = {0, 0, 0, 0};
            GLint sc[4] = {0, 0, 0, 0};
            if (glGetIntegerv) {
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &boundFBO);
                glGetIntegerv(GL_VIEWPORT, vp);
                glGetIntegerv(GL_SCISSOR_BOX, sc);
            }
            unsigned char px[4] = {0, 0, 0, 0};
            unsigned char pxTL[4] = {0, 0, 0, 0};
            bool scissorWas = glIsEnabled(GL_SCISSOR_TEST);
            if (glReadPixels) {
                glDisable(GL_SCISSOR_TEST);
                glReadPixels(vp[2] / 2, vp[3] / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
                glReadPixels(vp[0] + 5, vp[1] + 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pxTL);
                if (scissorWas) glEnable(GL_SCISSOR_TEST);
            }
            const Vertex* v0 = sBatchVerts.empty() ? nullptr : &sBatchVerts[0];
            const Vertex* vM = sBatchVerts.size() > 1 ? &sBatchVerts[sBatchVerts.size() / 2] : nullptr;
            GLint pm = 0;
            GLboolean colorMask[4] = {GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE};
            GLboolean depthTest = GL_FALSE, blend = GL_FALSE, cull = GL_FALSE;
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &pm);
            glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
            depthTest = glIsEnabled(GL_DEPTH_TEST);
            blend     = glIsEnabled(GL_BLEND);
            cull      = glIsEnabled(GL_CULL_FACE);
            GLint activeUnit = 0;
            GLint texBindings[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            if (glGetIntegerv) glGetIntegerv(GL_ACTIVE_TEXTURE, &activeUnit);
            for (int u = 0; u < 8; ++u) {
                if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0 + u);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &texBindings[u]);
            }
            if (glActiveTexture_ptr) glActiveTexture_ptr(GL_TEXTURE0);
            int fastPath = -1;
            float alphaRef0 = -1.0f;
            int alphaComp0 = -1;
            if (glGetUniformiv_ptr && sShaderProgram) {
                GLint fpLoc = glGetUniformLocation_ptr ? glGetUniformLocation_ptr(sShaderProgram, "uFastPath") : -1;
                if (fpLoc >= 0) glGetUniformiv_ptr(sShaderProgram, fpLoc, &fastPath);
            }
                        if (getenv("PIKMIN_PROBE_TEV")) {
                fprintf(stderr, "[PC ProbeTEV] stages=%d", (int)sNumTevStages);
                for (int st = 0; st < sNumTevStages && st < 16; ++st) {
                    const TevStageState& t = sTevStages[st];
                    fprintf(stderr, " | s%d map=%d tc=%d en=%d ras=%d c=[%d,%d,%d,%d] op=%d bias=%d sc=%d out=%d a=[%d,%d,%d,%d] aop=%d aout=%d kc=%d ka=%d",
                            st, (int)t.texMap, (int)t.texCoord, (int)t.textureEnabled, t.rasChannel,
                            t.colorIn[0], t.colorIn[1], t.colorIn[2], t.colorIn[3], (int)t.colorOp, (int)t.colorBias, (int)t.colorScale, (int)t.colorOutReg,
                            t.alphaIn[0], t.alphaIn[1], t.alphaIn[2], t.alphaIn[3], (int)t.alphaOp, (int)t.alphaOutReg,
                            (int)sKonstColorSel[st], (int)sKonstAlphaSel[st]);
                }
                fprintf(stderr, " | chan0 en=%d matSrc=%d ambSrc=%d lights=%u mat=%.2f,%.2f,%.2f,%.2f amb=%.2f,%.2f,%.2f",
                        sChannels[0].enabled, (int)sChannels[0].matSrc, (int)sChannels[0].ambSrc, sChannels[0].lightMask,
                        sChannels[0].matColor[0], sChannels[0].matColor[1], sChannels[0].matColor[2], sChannels[0].matColor[3],
                        sChannels[0].ambColor[0], sChannels[0].ambColor[1], sChannels[0].ambColor[2]);
                for (int u = 0; u < 4; ++u) {
                    if (!sHasActiveTextures[u]) continue;
                    GLint w = 0, h = 0;
                    glActiveTexture_ptr(GL_TEXTURE0 + u);
                    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
                    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
                    fprintf(stderr, " | unit%d gl=%u %dx%d", u, sActiveGLTextures[u], w, h);
                    if (const char* dumpDir = getenv("PIKMIN_PROBE_TEXDUMP")) {
                        // Dump the bound texture as PPM (RGB) + PGM (alpha) for inspection.
                        const size_t n = size_t(w) * h * 4;
                        unsigned char* px = (unsigned char*)malloc(n);
                        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
                        char path[512];
                        snprintf(path, sizeof path, "%s/tex_%u_%dx%d.ppm", dumpDir, sActiveGLTextures[u], w, h);
                        if (FILE* f = fopen(path, "wb")) {
                            fprintf(f, "P6\n%d %d\n255\n", w, h);
                            for (size_t i = 0; i < n; i += 4) fwrite(px + i, 1, 3, f);
                            fclose(f);
                        }
                        snprintf(path, sizeof path, "%s/tex_%u_%dx%d_a.pgm", dumpDir, sActiveGLTextures[u], w, h);
                        if (FILE* f = fopen(path, "wb")) {
                            fprintf(f, "P5\n%d %d\n255\n", w, h);
                            for (size_t i = 3; i < n; i += 4) fwrite(px + i, 1, 1, f);
                            fclose(f);
                        }
                        free(px);
                    }
                }
                glActiveTexture_ptr(GL_TEXTURE0);
                for (int t = 0; t < 4; ++t)
                    fprintf(stderr, " | swap%d=%d%d%d%d", t, (int)sTevSwapModes[t].red, (int)sTevSwapModes[t].green,
                            (int)sTevSwapModes[t].blue, (int)sTevSwapModes[t].alpha);
                for (int st = 0; st < sNumTevStages && st < 16; ++st)
                    fprintf(stderr, " s%d:ras=%d,tex=%d", st, (int)sTevRasSwapSel[st], (int)sTevTexSwapSel[st]);
                fprintf(stderr, " | chan1 en=%d matSrc=%d ambSrc=%d lights=%u attn=%d diff=%d mat=%.2f,%.2f,%.2f amb=%.2f,%.2f,%.2f",
                        sChannels[1].enabled, (int)sChannels[1].matSrc, (int)sChannels[1].ambSrc, sChannels[1].lightMask,
                        (int)sChannels[1].attnFn, (int)sChannels[1].diffFn,
                        sChannels[1].matColor[0], sChannels[1].matColor[1], sChannels[1].matColor[2],
                        sChannels[1].ambColor[0], sChannels[1].ambColor[1], sChannels[1].ambColor[2]);
                for (int li = 0; li < 8; ++li) {
                    const GfxLight& L = sLights[li];
                    if (!L.active) continue;
                    fprintf(stderr, " | L%d col=%.2f,%.2f,%.2f pos=%.0f,%.0f,%.0f dir=%.2f,%.2f,%.2f a=%.2f,%.2f,%.2f k=%.2f,%.4f,%.6f", li,
                            L.color[0], L.color[1], L.color[2], L.pos[0], L.pos[1], L.pos[2], L.dir[0], L.dir[1], L.dir[2],
                            L.a[0], L.a[1], L.a[2], L.k[0], L.k[1], L.k[2]);
                }
                fprintf(stderr, " | fog req=%d allowed=%d %.0f..%.0f view %.0f..%.0f col=%.2f,%.2f,%.2f",
                        (int)sFogRequested, (int)sFogAllowed, sFogStart, sFogEnd, sFogNear, sFogFar, sFogColour[0], sFogColour[1], sFogColour[2]);
                {
                    const TexCoordGen& tg = sTexCoordGen[0];
                    const float* tm = sTexMatrices[tg.mtxIdx < 64 ? tg.mtxIdx : 0];
                    fprintf(stderr, " | texgen0 act=%d type=%d src=%d mtx=%u loaded=%d m=[%g %g %g %g | %g %g %g %g]",
                            tg.active, tg.type, tg.src, tg.mtxIdx, (int)sTexMtxLoaded[tg.mtxIdx < 64 ? tg.mtxIdx : 0],
                            tm[0], tm[4], tm[8], tm[12], tm[1], tm[5], tm[9], tm[13]);
                }
                fprintf(stderr, " | konst0=%.2f,%.2f,%.2f,%.2f prev=%.2f,%.2f,%.2f,%.2f reg0=%.2f,%.2f,%.2f,%.2f reg1=%.2f,%.2f,%.2f,%.2f\n",
                        sKonstColors[0][0], sKonstColors[0][1], sKonstColors[0][2], sKonstColors[0][3],
                        sTevRegisters[0][0], sTevRegisters[0][1], sTevRegisters[0][2], sTevRegisters[0][3],
                        sTevRegisters[1][0], sTevRegisters[1][1], sTevRegisters[1][2], sTevRegisters[1][3],
                        sTevRegisters[2][0], sTevRegisters[2][1], sTevRegisters[2][2], sTevRegisters[2][3]);
            }
            if (getenv("PIKMIN_PROBE_VERTS") && sBatchVerts.size() <= 24) {
                for (const Vertex& bv : sBatchVerts)
                    fprintf(stderr, "[PC ProbeV] (%.1f,%.1f,%.1f) rgba=%.2f,%.2f,%.2f,%.2f slot=%.0f uv=(%.3f,%.3f)\n", bv.x, bv.y, bv.z, bv.r, bv.g, bv.b, bv.a, bv.matrixSlot, bv.tex[0][0], bv.tex[0][1]);
            }
            float uMin = 1e9f, uMax = -1e9f, vMin = 1e9f, vMax = -1e9f;
            for (const Vertex& bv : sBatchVerts) { uMin = std::min(uMin, bv.tex[0][0]); uMax = std::max(uMax, bv.tex[0][0]); vMin = std::min(vMin, bv.tex[0][1]); vMax = std::max(vMax, bv.tex[0][1]); }
printf("[PC Probe] frame=%u draw %d: mode=%s verts=%zu boundFBO=%u nativeFBO=%u vp=%d,%d,%dx%d sc=%d,%d,%dx%d scOn=%d win=%d,%d,%d tl=%d,%d,%d "
                   "v0=(%.1f,%.1f,%.1f)r=%d g=%d b=%d a=%d vm=(%.1f,%.1f,%.1f)r=%d g=%d b=%d a=%d depth=%d blend=%d cull=%d cm=%d,%d,%d,%d "
                   "act=%d tex={%u,%u,%u,%u,%u,%u,%u,%u} port={%u,%u,%u,%u,%u,%u,%u,%u} hasTex={%d,%d,%d,%d,%d,%d,%d,%d} "
                   "tex0uv=(%.3f,%.3f) uvrange=(%.2f..%.2f,%.2f..%.2f) fastPath=%d alphaC0=%d ref0=%.1f alphaOp=%d tevStages=%d\n",
                   (unsigned)sFrameSerial,                   probeCount - 1,
                   (sBatchMode == GL_TRIANGLES ? "TRI" : sBatchMode == GL_LINES ? "LIN" : sBatchMode == GL_POINTS ? "PNT" : "??"),
                   sBatchVerts.size(), (unsigned)boundFBO, (unsigned)sNativeFramebuffer,
                   vp[0], vp[1], vp[2], vp[3], sc[0], sc[1], sc[2], sc[3],
                   scissorWas ? 1 : 0, (int)px[0], (int)px[1], (int)px[2],
                   (int)pxTL[0], (int)pxTL[1], (int)pxTL[2],
                   v0 ? (double)v0->x : -999.0, v0 ? (double)v0->y : -999.0, v0 ? (double)v0->z : -999.0,
                   v0 ? (int)(v0->r * 255.0f) : -1, v0 ? (int)(v0->g * 255.0f) : -1, v0 ? (int)(v0->b * 255.0f) : -1, v0 ? (int)(v0->a * 255.0f) : -1,
                   vM ? (double)vM->x : -999.0, vM ? (double)vM->y : -999.0, vM ? (double)vM->z : -999.0,
                   vM ? (int)(vM->r * 255.0f) : -1, vM ? (int)(vM->g * 255.0f) : -1, vM ? (int)(vM->b * 255.0f) : -1, vM ? (int)(vM->a * 255.0f) : -1,
                   depthTest ? 1 : 0, blend ? 1 : 0, cull ? 1 : 0, colorMask[0], colorMask[1], colorMask[2], colorMask[3],
                   (int)(activeUnit - GL_TEXTURE0),
                   (unsigned)texBindings[0], (unsigned)texBindings[1], (unsigned)texBindings[2], (unsigned)texBindings[3],
                   (unsigned)texBindings[4], (unsigned)texBindings[5], (unsigned)texBindings[6], (unsigned)texBindings[7],
                   (unsigned)sActiveGLTextures[0], (unsigned)sActiveGLTextures[1], (unsigned)sActiveGLTextures[2], (unsigned)sActiveGLTextures[3],
                   (unsigned)sActiveGLTextures[4], (unsigned)sActiveGLTextures[5], (unsigned)sActiveGLTextures[6], (unsigned)sActiveGLTextures[7],
                   sHasActiveTextures[0] ? 1 : 0, sHasActiveTextures[1] ? 1 : 0, sHasActiveTextures[2] ? 1 : 0, sHasActiveTextures[3] ? 1 : 0,
                   sHasActiveTextures[4] ? 1 : 0, sHasActiveTextures[5] ? 1 : 0, sHasActiveTextures[6] ? 1 : 0, sHasActiveTextures[7] ? 1 : 0,
                   v0 ? (double)v0->tex[0][0] : -999.0, v0 ? (double)v0->tex[0][1] : -999.0,
                   uMin, uMax, vMin, vMax, fastPath, (int)sAlphaComp0, (double)sAlphaRef0, (int)sAlphaOp, (int)sNumTevStages);
            fflush(stdout);
        }
    }

    if (profiling) {
        sSubmitDrawMs += submit_clock_ms() - t1;
        ++sSubmitDraws;
        sSubmitVerts += sBatchVerts.size();
    }
    ++sPerfDraws;
    sPerfVertices += sBatchVerts.size();
    if (sPerfCurrentScope >= 0) {
        ++sPerfScopes[sPerfCurrentScope].draws;
        sPerfScopes[sPerfCurrentScope].vertices += sBatchVerts.size();
    }
    sVboWriteOffset += vertexBytes;

    sBatchVerts.clear();
    sBatchOpen  = false;
    sBatchPrims = 0;
}

static void note_batch_run(uint64_t materialKey, uint64_t epoch) {
    const uint64_t key = hash_bytes(materialKey, &epoch, sizeof(epoch));
    if (sHaveRunKey && key == sRunKey) {
        ++sRunCurrent;
        return;
    }
    if (sHaveRunKey) {
        if (sRunCurrent > sRunLongest) sRunLongest = sRunCurrent;
        ++sRunCount;
        // Attribute the break: did the GL epoch move, or only the material?
        if (epoch != sRunEpoch) ++sRunGlBreaks;
    }
    sRunKey     = key;
    sRunEpoch   = epoch;
    sHaveRunKey = true;
    sRunCurrent = 1;
}

static inline double submit_clock_ms() {
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void pc_gfx_flush_submit_stats(void) {
    pc_gfx_flush_batch();
    ++sFrameSerial;
    if (!pc_tick_profiler_enabled()) return;
    pc_tick_profiler_record(kPcTickGfxUniforms, sSubmitUniformMs);
    pc_tick_profiler_record(kPcTickGfxVbo, sSubmitVboMs);
    pc_tick_profiler_record(kPcTickGfxDraw, sSubmitDrawMs);
    pc_tick_profiler_record(kPcTickGfxDisplayList, sSubmitDlMs);
    pc_tick_profiler_record(kPcTickGfxStateKey, sSubmitKeyMs);
    pc_tick_profiler_record(kPcTickGfxProgram, sSubmitProgramMs);
    pc_tick_profiler_record(kPcTickGfxTexBind, sSubmitTexBindMs);
    pc_tick_profiler_record(kPcTickGfxMeshDraws, double(sMeshDrawsThisFrame));
    pc_tick_profiler_record(kPcTickGfxMeshVerts, double(sMeshVertsThisFrame));
    pc_tick_profiler_record(kPcTickGfxShaderBuild, sShaderBuildMsThisFrame);
    sShaderBuildMsThisFrame = 0.0;
    pc_tick_profiler_record(kPcTickGfxMeshBuilds, double(sMeshBuildsThisFrame));
    // A frame that parsed and uploaded a lot of new geometry is a hitch the
    // player felt unless it was a loading screen; say which it was.
    if (sSubmitDlMs > 8.0 && sMeshBuildsThisFrame > 0) {
        fprintf(stderr, "[PC MESH] frame %llu: %u meshes built (%u KB, upload %.1f ms), display lists %.1f ms\n",
                (unsigned long long)sFrameSerial, sMeshBuildsThisFrame, sMeshBuildBytesThisFrame >> 10,
                sMeshBuildMsThisFrame, sSubmitDlMs);
    }
    sMeshBuildMsThisFrame = 0.0;
    sMeshBuildsThisFrame = 0;
    sMeshBuildBytesThisFrame = 0;
    pc_tick_profiler_record(kPcTickGfxDrawCount, static_cast<double>(sSubmitDraws));
    pc_tick_profiler_record(kPcTickGfxPrimCount, static_cast<double>(sSubmitPrims));
    pc_tick_profiler_record(kPcTickGfxVertsPerDraw,
                            sSubmitDraws ? static_cast<double>(sSubmitVerts) / sSubmitDraws : 0.0);
    sSubmitUniformMs = sSubmitVboMs = sSubmitDrawMs = sSubmitDlMs = 0.0;
    sSubmitKeyMs = sSubmitProgramMs = sSubmitTexBindMs = 0.0;
    sMeshDrawsThisFrame = 0;
    sMeshVertsThisFrame = 0;
    // Close the run still open at the frame boundary so it is counted.
    if (sHaveRunKey) {
        if (sRunCurrent > sRunLongest) sRunLongest = sRunCurrent;
        ++sRunCount;
    }
    pc_tick_profiler_record(kPcTickGfxRunLength,
                            sRunCount ? static_cast<double>(sSubmitPrims) / sRunCount : 0.0);
    pc_tick_profiler_record(kPcTickGfxRunLongest, static_cast<double>(sRunLongest));
    pc_tick_profiler_record(kPcTickGfxGlBreakPct,
                            sRunCount ? 100.0 * sRunGlBreaks / sRunCount : 0.0);
    pc_tick_profiler_record(kPcTickGxDlDesync, static_cast<double>(sDlDesyncs));
    pc_tick_profiler_record(kPcTickGxBadMtxIdx, static_cast<double>(sBadMtxIdx));
    pc_tick_profiler_record(kPcTickGxWildVerts, static_cast<double>(sWildVerts));
    sWildVerts = 0;
    sDlDesyncs = 0;
    sBadMtxIdx = 0;
    sSubmitDraws     = 0;
    sSubmitVerts     = 0;
    sSubmitPrims     = 0;
    sHaveRunKey      = false;
    sRunCurrent      = 0;
    sRunLongest      = 0;
    sRunCount        = 0;
    sRunGlBreaks     = 0;
}

// ── Matrix palette (GPU skinning) ───────────────────────────────────────────
//
// Slots 0..20 map to GX position matrix ids 0,3,...,60 (the game's
// useMatrixQuick loads at id = index*3). Uploaded as rows; only the first
// `slots` slots a mesh actually references, and only when one of those
// matrices was reloaded since the last upload to this program.
static void upload_matrix_palette(int slots) {
    if (sLoc.posPalette < 0 && sLoc.nrmPalette < 0) return;
    if (slots < 1) slots = 1;
    if (slots > kPaletteSlots) slots = kPaletteSlots;
    // Fingerprint: which program, which slots, and their revisions.
    uint64_t fp = uint64_t(sUniformGeneration) << 32 ^ uint64_t(slots);
    for (int slot = 0; slot < slots; ++slot) {
        const u32 id = u32(slot) * 3;
        fp = (fp ^ (uint64_t(sPosMtxGen[id]) << 20) ^ uint64_t(sNrmMtxGen[id]) ^ (uint64_t(slot) << 56)) * 1099511628211ull;
    }
    static uint64_t lastFingerprint = 0;
    static int lastSlots = 0;
    if (fp == lastFingerprint && slots <= lastSlots) return;
    lastFingerprint = fp;
    lastSlots = slots;
    float pos[kPaletteSlots * 3][4];
    float nrm[kPaletteSlots * 3][4];
    for (int slot = 0; slot < slots; ++slot) {
        const float* m = sPosMatrix[slot * 3];
        const float* n = sNrmMatrix[slot * 3];
        for (int r = 0; r < 3; ++r) {
            pos[slot * 3 + r][0] = m[r];  pos[slot * 3 + r][1] = m[4 + r];
            pos[slot * 3 + r][2] = m[8 + r]; pos[slot * 3 + r][3] = m[12 + r];
            nrm[slot * 3 + r][0] = n[r];  nrm[slot * 3 + r][1] = n[3 + r];
            nrm[slot * 3 + r][2] = n[6 + r]; nrm[slot * 3 + r][3] = 0.0f;
        }
    }
    if (sLoc.posPalette >= 0) glUniform4fv_ptr(sLoc.posPalette, slots * 3, &pos[0][0]);
    if (sLoc.nrmPalette >= 0) glUniform4fv_ptr(sLoc.nrmPalette, slots * 3, &nrm[0][0]);
}

// Programs the GL pipeline for the current GX material state: program,
// every uniform, textures. Shared by the batched immediate path (pc_gfx_end)
// and the resident-mesh path, which has no vertex stream to hand over.
// stateT0 is the clock at entry when profiling, so gl:program can be split out.
static void apply_draw_state(bool profilingSubmit, double stateT0) {
    // Pick the program for this material first: every uniform below is written
    // through sLoc, which describes whichever program is now bound.
    use_program_for_current_state();
    if (profilingSubmit) sSubmitProgramMs += submit_clock_ms() - stateT0;
    filesel_debug_log_draw();

    glUniformMatrix4fv_ptr(sLoc.projMtx, 1, GL_FALSE, sProjMatrix);
    static const float identity[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };
    glUniformMatrix4fv_ptr(sLoc.posMtx, 1, GL_FALSE,
                          sVerticesPretransformed ? identity : sPosMatrix[sCurrentPosMtxId]);
    if (sLoc.usePalette >= 0) glUniform1i_ptr(sLoc.usePalette, sVertexUsesPalette ? 1 : 0);
    if (sVertexUsesPalette) upload_matrix_palette(sPaletteSlotsNeeded);
    glUniform4f_ptr(sLoc.materialColor, sChannels[0].matColor[0], sChannels[0].matColor[1],
                    sChannels[0].matColor[2], sChannels[0].matColor[3]);
    glUniform1i_ptr(sLoc.useMaterialRgb, sChannels[0].matSrc == GX_SRC_REG ? 1 : 0);
    glUniform1i_ptr(sLoc.useMaterialAlpha, sChannels[0].alphaMatSrc == GX_SRC_REG ? 1 : 0);
    glUniform1i_ptr(sLoc.alphaComp0, static_cast<int>(sAlphaComp0));
    glUniform1i_ptr(sLoc.alphaComp1, static_cast<int>(sAlphaComp1));
    glUniform1i_ptr(sLoc.alphaOp, static_cast<int>(sAlphaOp));
    glUniform1f_ptr(sLoc.alphaRef0, sAlphaRef0);
    glUniform1f_ptr(sLoc.alphaRef1, sAlphaRef1);
    if (sLoc.outTint >= 0) glUniform3f_ptr(sLoc.outTint, sOutTint[0], sOutTint[1], sOutTint[2]);
    if (sLoc.perPixel >= 0) glUniform1i_ptr(sLoc.perPixel, sPerPixelLighting ? 1 : 0);
    glUniform4f_ptr(sLoc.tevPrev, sTevRegisters[GX_TEVPREV][0], sTevRegisters[GX_TEVPREV][1],
                   sTevRegisters[GX_TEVPREV][2], sTevRegisters[GX_TEVPREV][3]);
    glUniform4f_ptr(sLoc.tevReg0, sTevRegisters[GX_TEVREG0][0], sTevRegisters[GX_TEVREG0][1],
                   sTevRegisters[GX_TEVREG0][2], sTevRegisters[GX_TEVREG0][3]);
    glUniform4f_ptr(sLoc.tevReg1, sTevRegisters[GX_TEVREG1][0], sTevRegisters[GX_TEVREG1][1],
                   sTevRegisters[GX_TEVREG1][2], sTevRegisters[GX_TEVREG1][3]);
    glUniform4f_ptr(sLoc.tevReg2, sTevRegisters[GX_TEVREG2][0], sTevRegisters[GX_TEVREG2][1],
                   sTevRegisters[GX_TEVREG2][2], sTevRegisters[GX_TEVREG2][3]);
    for (int i = 0; i < 4; i++) {
        glUniform4f_ptr(sLoc.konst[i], sKonstColors[i][0], sKonstColors[i][1],
                        sKonstColors[i][2], sKonstColors[i][3]);
    }
    {
        // PIKMIN_MAX_STAGES=N: debugging aid, truncate every TEV chain to N stages.
        static const int maxStages = getenv("PIKMIN_MAX_STAGES") ? atoi(getenv("PIKMIN_MAX_STAGES")) : 0;
        glUniform1i_ptr(sLoc.numStages, (maxStages > 0 && sNumTevStages > maxStages) ? maxStages : (int)sNumTevStages);
    }
    int fastPath = 0;
    if (sNumTevStages == 1) {
        const TevStageState& st = sTevStages[0];
        const bool simpleOp = st.colorOp == GX_TEV_ADD && st.alphaOp == GX_TEV_ADD
            && st.colorBias == GX_TB_ZERO && st.alphaBias == GX_TB_ZERO
            && st.colorScale == GX_CS_SCALE_1 && st.alphaScale == GX_CS_SCALE_1
            && st.colorOutReg == GX_TEVPREV && st.alphaOutReg == GX_TEVPREV
            && sTevRasSwapSel[0] == GX_TEV_SWAP0 && sTevTexSwapSel[0] == GX_TEV_SWAP0;
        const bool directTexture = st.textureEnabled && st.texMap == GX_TEXMAP0
            && st.texCoord == GX_TEXCOORD0
            && ((st.colorIn[0] == GX_CC_ZERO && st.colorIn[1] == GX_CC_ZERO
                 && st.colorIn[2] == GX_CC_ZERO && st.colorIn[3] == GX_CC_TEXC)
                || (st.colorIn[0] == GX_CC_TEXC && st.colorIn[1] == GX_CC_ZERO
                    && st.colorIn[2] == GX_CC_ZERO && st.colorIn[3] == GX_CC_ZERO))
            && ((st.alphaIn[0] == GX_CA_ZERO && st.alphaIn[1] == GX_CA_ZERO
                 && st.alphaIn[2] == GX_CA_ZERO && st.alphaIn[3] == GX_CA_TEXA)
                || (st.alphaIn[0] == GX_CA_TEXA && st.alphaIn[1] == GX_CA_ZERO
                    && st.alphaIn[2] == GX_CA_ZERO && st.alphaIn[3] == GX_CA_ZERO));
        const bool rasterInD = st.colorIn[0] == GX_CC_ZERO && st.colorIn[1] == GX_CC_ZERO
            && st.colorIn[2] == GX_CC_ZERO && st.colorIn[3] == GX_CC_RASC
            && st.alphaIn[0] == GX_CA_ZERO && st.alphaIn[1] == GX_CA_ZERO
            && st.alphaIn[2] == GX_CA_ZERO && st.alphaIn[3] == GX_CA_RASA;
        // Some original display lists encode the same pass-through as
        // A=RASC, B=C=D=0. With add/bias-zero/scale-one the TEV equation is
        // still exactly RASC (and RASA for alpha).
        const bool rasterInA = st.colorIn[0] == GX_CC_RASC && st.colorIn[1] == GX_CC_ZERO
            && st.colorIn[2] == GX_CC_ZERO && st.colorIn[3] == GX_CC_ZERO
            && st.alphaIn[0] == GX_CA_RASA && st.alphaIn[1] == GX_CA_ZERO
            && st.alphaIn[2] == GX_CA_ZERO && st.alphaIn[3] == GX_CA_ZERO;
        const bool directRaster = !st.textureEnabled && (rasterInD || rasterInA);
        // GX_MODULATE, heavily used by UI glyphs, particles and simple model
        // materials. It is exactly texture * raster for both color and alpha;
        // running the generic TEV loop for every covered pixel is unnecessary.
        const bool modulateTextureRaster = st.textureEnabled && st.texMap == GX_TEXMAP0
            && st.texCoord == GX_TEXCOORD0
            && st.colorIn[0] == GX_CC_ZERO && st.colorIn[1] == GX_CC_TEXC
            && st.colorIn[2] == GX_CC_RASC && st.colorIn[3] == GX_CC_ZERO
            && st.alphaIn[0] == GX_CA_ZERO && st.alphaIn[1] == GX_CA_TEXA
            && st.alphaIn[2] == GX_CA_RASA && st.alphaIn[3] == GX_CA_ZERO;
        if (simpleOp && directTexture) fastPath = 1;
        else if (simpleOp && directRaster) fastPath = 2;
        else if (simpleOp && modulateTextureRaster) fastPath = 3;
    }
    {
        // PIKMIN_DEBUG_FASTPATH=N: debugging aid, force the ubershader's fast path (2 = raster colour only).
        static const int forced = getenv("PIKMIN_DEBUG_FASTPATH") ? atoi(getenv("PIKMIN_DEBUG_FASTPATH")) : 0;
        if (forced) fastPath = forced;
    }
    glUniform1i_ptr(sLoc.fastPath, fastPath);
    if (fastPath != 0) {
        ++sPerfFastDraws;
        ++sPerfFastPathDraws[fastPath];
        sPerfFastPathVertices[fastPath] += sVertexStream.size();
    }
    if (sPerfStatsEnabled) {
        sPerfTevStageDraws[std::min<int>(sNumTevStages, GX_MAXTEVSTAGE)]++;
        // One bounded snapshot of each title-scene pipeline is substantially
        // more useful than logging thousands of identical draws. It exposes
        // where CPREV/A1 are produced while keeping the hot path allocation-
        // free and the log small.
        PerfScope* pipelineScope = sPerfCurrentScope >= 0 ? &sPerfScopes[sPerfCurrentScope] : nullptr;
        u8* pipelineReportCount = !pipelineScope ? nullptr
                                : sNumTevStages == 5 ? &pipelineScope->pipeline5ReportCount
                                : sNumTevStages == 7 ? &pipelineScope->pipeline7ReportCount : nullptr;
        if (pipelineReportCount && *pipelineReportCount < 4) {
            const unsigned pipelineIndex = unsigned((*pipelineReportCount)++);
            if (pipelineIndex == 0) {
                fprintf(stderr, "[PERF TEVPIPE%u %s] regs", unsigned(sNumTevStages), pipelineScope->name);
                for (int reg = 0; reg < 4; ++reg) {
                    fprintf(stderr, " r%d=(%.3f,%.3f,%.3f,%.3f)", reg,
                            sTevRegisters[reg][0], sTevRegisters[reg][1],
                            sTevRegisters[reg][2], sTevRegisters[reg][3]);
                }
                fprintf(stderr, "\n");
                for (u8 stage = 0; stage < sNumTevStages; ++stage) {
                    const TevStageState& pipe = sTevStages[stage];
                    fprintf(stderr,
                            "[PERF TEVPIPE%u %s] s%u C=%d,%d,%d,%d->%d A=%d,%d,%d,%d->%d tex=%d/%d/%d ras=%d op=%d/%d bias=%d/%d scale=%d/%d clamp=%d/%d\n",
                            unsigned(sNumTevStages), pipelineScope->name, unsigned(stage),
                            int(pipe.colorIn[0]), int(pipe.colorIn[1]), int(pipe.colorIn[2]), int(pipe.colorIn[3]), int(pipe.colorOutReg),
                            int(pipe.alphaIn[0]), int(pipe.alphaIn[1]), int(pipe.alphaIn[2]), int(pipe.alphaIn[3]), int(pipe.alphaOutReg),
                            pipe.textureEnabled ? 1 : 0, int(pipe.texMap), int(pipe.texCoord), pipe.rasChannel,
                            int(pipe.colorOp), int(pipe.alphaOp), int(pipe.colorBias), int(pipe.alphaBias),
                            int(pipe.colorScale), int(pipe.alphaScale), int(pipe.colorClamp), int(pipe.alphaClamp));
                }
            }

            // Identify what this pipeline actually covers before attributing
            // a visual defect to it. Report projected NDC bounds and the UV
            // ranges consumed by its active texture coordinates. This is
            // diagnostic-only and does not alter material state or pixels.
            float ndcMinX = 1.0e30f, ndcMinY = 1.0e30f;
            float ndcMaxX = -1.0e30f, ndcMaxY = -1.0e30f;
            float uvMin[4][2];
            float uvMax[4][2];
            for (int tc = 0; tc < 4; ++tc) {
                uvMin[tc][0] = uvMin[tc][1] = 1.0e30f;
                uvMax[tc][0] = uvMax[tc][1] = -1.0e30f;
            }
            const float* model = sVerticesPretransformed ? identity : sPosMatrix[sCurrentPosMtxId];
            for (const Vertex& vertex : sVertexStream) {
                const float mx = model[0] * vertex.x + model[4] * vertex.y + model[8] * vertex.z + model[12];
                const float my = model[1] * vertex.x + model[5] * vertex.y + model[9] * vertex.z + model[13];
                const float mz = model[2] * vertex.x + model[6] * vertex.y + model[10] * vertex.z + model[14];
                const float mw = model[3] * vertex.x + model[7] * vertex.y + model[11] * vertex.z + model[15];
                const float cx = sProjMatrix[0] * mx + sProjMatrix[4] * my + sProjMatrix[8] * mz + sProjMatrix[12] * mw;
                const float cy = sProjMatrix[1] * mx + sProjMatrix[5] * my + sProjMatrix[9] * mz + sProjMatrix[13] * mw;
                const float cw = sProjMatrix[3] * mx + sProjMatrix[7] * my + sProjMatrix[11] * mz + sProjMatrix[15] * mw;
                if (fabsf(cw) > 1.0e-8f) {
                    const float nx = cx / cw;
                    const float ny = cy / cw;
                    ndcMinX = std::min(ndcMinX, nx);
                    ndcMaxX = std::max(ndcMaxX, nx);
                    ndcMinY = std::min(ndcMinY, ny);
                    ndcMaxY = std::max(ndcMaxY, ny);
                }
                for (int tc = 0; tc < 4; ++tc) {
                    uvMin[tc][0] = std::min(uvMin[tc][0], vertex.tex[tc][0]);
                    uvMax[tc][0] = std::max(uvMax[tc][0], vertex.tex[tc][0]);
                    uvMin[tc][1] = std::min(uvMin[tc][1], vertex.tex[tc][1]);
                    uvMax[tc][1] = std::max(uvMax[tc][1], vertex.tex[tc][1]);
                }
            }
            fprintf(stderr, "[PERF TEVGEOM%u %s #%u] verts=%zu ndc=(%.3f,%.3f)-(%.3f,%.3f)",
                    unsigned(sNumTevStages), pipelineScope->name, pipelineIndex, sVertexStream.size(),
                    ndcMinX, ndcMinY, ndcMaxX, ndcMaxY);
            bool reportedTc[4] = {};
            for (u8 stage = 0; stage < sNumTevStages; ++stage) {
                const int tc = int(sTevStages[stage].texCoord);
                if (sTevStages[stage].textureEnabled && tc >= 0 && tc < 4 && !reportedTc[tc]) {
                    reportedTc[tc] = true;
                    fprintf(stderr, " uv%d=(%.3f,%.3f)-(%.3f,%.3f)", tc,
                            uvMin[tc][0], uvMin[tc][1], uvMax[tc][0], uvMax[tc][1]);
                }
            }
            fprintf(stderr, "\n");
        }
        if (sNumTevStages == 1) {
            const TevStageState& st = sTevStages[0];
            uint64_t key = uint64_t(st.colorIn[0] & 15)
                | (uint64_t(st.colorIn[1] & 15) << 4)
                | (uint64_t(st.colorIn[2] & 15) << 8)
                | (uint64_t(st.colorIn[3] & 15) << 12)
                | (uint64_t(st.alphaIn[0] & 7) << 16)
                | (uint64_t(st.alphaIn[1] & 7) << 19)
                | (uint64_t(st.alphaIn[2] & 7) << 22)
                | (uint64_t(st.alphaIn[3] & 7) << 25)
                | (uint64_t((int(st.texMap) + 1) & 15) << 28)
                | (uint64_t((int(st.texCoord) + 1) & 15) << 32)
                | (uint64_t((st.rasChannel + 1) & 3) << 36)
                | (uint64_t(st.colorOp & 15) << 38)
                | (uint64_t(st.alphaOp & 15) << 42);
            uint64_t flags = uint64_t(st.colorBias & 3)
                | (uint64_t(st.alphaBias & 3) << 2)
                | (uint64_t(st.colorScale & 3) << 4)
                | (uint64_t(st.alphaScale & 3) << 6)
                | (uint64_t(st.colorClamp ? 1 : 0) << 8)
                | (uint64_t(st.alphaClamp ? 1 : 0) << 9)
                | (uint64_t(st.colorOutReg & 3) << 10)
                | (uint64_t(st.alphaOutReg & 3) << 12);
            key |= flags << 46;
            size_t slot = size_t((key ^ (key >> 33) ^ (key >> 17)) & 127);
            for (size_t probe = 0; probe < 128; ++probe) {
                PerfTevPattern& entry = sPerfTevPatterns[(slot + probe) & 127];
                if (entry.count == 0 || entry.key == key) {
                    entry.key = key;
                    entry.count++;
                    break;
                }
            }
        } else if (sNumTevStages > 1) {
            // The final stage must produce TEVPREV, which is the fragment
            // shader output. Grouping its signature exposes broken multi-stage
            // materials without logging every draw or tying diagnostics to a
            // particular model.
            const TevStageState& st = sTevStages[std::min<int>(sNumTevStages, GX_MAXTEVSTAGE) - 1];
            uint64_t key = uint64_t(st.colorIn[0] & 15)
                | (uint64_t(st.colorIn[1] & 15) << 4)
                | (uint64_t(st.colorIn[2] & 15) << 8)
                | (uint64_t(st.colorIn[3] & 15) << 12)
                | (uint64_t(st.alphaIn[0] & 7) << 16)
                | (uint64_t(st.alphaIn[1] & 7) << 19)
                | (uint64_t(st.alphaIn[2] & 7) << 22)
                | (uint64_t(st.alphaIn[3] & 7) << 25)
                | (uint64_t((int(st.texMap) + 1) & 15) << 28)
                | (uint64_t((int(st.texCoord) + 1) & 15) << 32)
                | (uint64_t((st.rasChannel + 1) & 3) << 36)
                | (uint64_t(st.colorOp & 15) << 38)
                | (uint64_t(st.alphaOp & 15) << 42);
            uint64_t flags = uint64_t(st.colorBias & 3)
                | (uint64_t(st.alphaBias & 3) << 2)
                | (uint64_t(st.colorScale & 3) << 4)
                | (uint64_t(st.alphaScale & 3) << 6)
                | (uint64_t(st.colorClamp ? 1 : 0) << 8)
                | (uint64_t(st.alphaClamp ? 1 : 0) << 9)
                | (uint64_t(st.colorOutReg & 3) << 10)
                | (uint64_t(st.alphaOutReg & 3) << 12);
            key |= flags << 46;
            const u8 stages = std::min<int>(sNumTevStages, GX_MAXTEVSTAGE);
            size_t slot = size_t((key ^ (key >> 33) ^ (key >> 17) ^ stages) & 127);
            for (size_t probe = 0; probe < 128; ++probe) {
                PerfTevMultiPattern& entry = sPerfTevMultiPatterns[(slot + probe) & 127];
                if (entry.count == 0 || (entry.key == key && entry.stages == stages)) {
                    entry.key = key;
                    entry.stages = stages;
                    entry.count++;
                    break;
                }
            }
        }
    }
    if (sLoc.indMtx >= 0 && glUniform3fv_ptr) {
        // Filas de las matrices indirectas ya escaladas por 2^escala: el
        // shader suma el desplazamiento en texels con un dot por eje.
        float rows[6][3];
        float scales[3];
        for (int m = 0; m < 3; ++m) {
            scales[m] = std::ldexp(1.0f, sIndMtxScale[m]);
            for (int r = 0; r < 2; ++r)
                for (int c = 0; c < 3; ++c) rows[m * 2 + r][c] = sIndMtx[m][r][c] * scales[m];
        }
        glUniform3fv_ptr(sLoc.indMtx, 6, &rows[0][0]);
        if (sLoc.indScale >= 0 && glUniform1fv_ptr) glUniform1fv_ptr(sLoc.indScale, 3, scales);
    }
    for (u8 stage = 0; stage < sNumTevStages && stage < GX_MAXTEVSTAGE; ++stage) {
        const TevStageState& st = sTevStages[stage];
        float konst[4];
        resolve_tev_konst(stage, konst);
        glUniform4f_ptr(sLoc.tevKonst[stage], konst[0], konst[1], konst[2], konst[3]);
        glUniform4i_ptr(sLoc.tevCSel[stage], st.colorIn[0], st.colorIn[1], st.colorIn[2], st.colorIn[3]);
        glUniform4i_ptr(sLoc.tevASel[stage], st.alphaIn[0], st.alphaIn[1], st.alphaIn[2], st.alphaIn[3]);
        int texIdx = (st.textureEnabled && st.texMap >= GX_TEXMAP0 && st.texMap < GX_MAX_TEXMAP)
                         ? static_cast<int>(st.texMap) : -1;
        glUniform4i_ptr(sLoc.tevTexInfo[stage], texIdx, st.texCoord, 0, 0);
        const int packedColorOp = static_cast<int>(st.colorOp) | (st.colorClamp ? 0x100 : 0);
        const int packedAlphaOp = static_cast<int>(st.alphaOp) | (st.alphaClamp ? 0x100 : 0);
        glUniform4i_ptr(sLoc.tevCOps[stage], packedColorOp, st.colorBias, st.colorScale, st.colorOutReg);
        glUniform4i_ptr(sLoc.tevAOps[stage], packedAlphaOp, st.alphaBias, st.alphaScale, st.alphaOutReg);
        if (sLoc.tevChan[stage] >= 0) {
            // GX_COLOR1/GX_COLOR1A1 stages sample the specular channel's raster.
            glUniform4i_ptr(sLoc.tevChan[stage], st.rasChannel, 0, 0, 0);
        }
        // TEV swap mode
        if (sLoc.tevSwapSel[stage] >= 0) {
            int rasSel = (int)sTevRasSwapSel[stage];
            int texSel = (int)sTevTexSwapSel[stage];
            glUniform2i_ptr(sLoc.tevSwapSel[stage], rasSel, texSel);
        }
    }
    // TEV swap mode tables
    for (int t = 0; t < 4; ++t) {
        if (sLoc.tevSwapTable[t] >= 0) {
            glUniform4i_ptr(sLoc.tevSwapTable[t],
                (int)sTevSwapModes[t].red,
                (int)sTevSwapModes[t].green,
                (int)sTevSwapModes[t].blue,
                (int)sTevSwapModes[t].alpha);
        }
    }

    // Only touch texture units referenced by an active TEV stage. Most Pikmin
    // materials use one map; rebinding all eight for every tiny primitive was
    // a substantial driver overhead. Sampler locations are fixed at init.
    bool usedTextureMaps[8] = {};
    for (u8 stage = 0; stage < sNumTevStages && stage < GX_MAXTEVSTAGE; ++stage) {
        const TevStageState& st = sTevStages[stage];
        if (st.textureEnabled && st.texMap >= GX_TEXMAP0 && st.texMap < 8) {
            usedTextureMaps[static_cast<int>(st.texMap)] = true;
        }
    }
    int lastActiveMap = 0;
    const double texT0 = profilingSubmit ? submit_clock_ms() : 0.0;
    static u32 bindLogCount = 0;
    for (int map = 0; map < 8; ++map) {
        if (!usedTextureMaps[map]) continue;
        const GLuint wanted = (sHasActiveTextures[map] ? sActiveGLTextures[map] : 0);
        if (pc_gfx_verbose() && bindLogCount++ < 20) {
            printf("[PC TexBind] map=%d used=%d has=%d active=%u bound=%u wanted=%u\n",
                   map, usedTextureMaps[map] ? 1 : 0, sHasActiveTextures[map] ? 1 : 0,
                   (unsigned)sActiveGLTextures[map], (unsigned)sBoundTextures[map], (unsigned)wanted);
            fflush(stdout);
        }
        if (sBoundTextures[map] != wanted) {
            glActiveTexture_ptr(GL_TEXTURE0 + map);
            glBindTexture(GL_TEXTURE_2D, wanted);
            sBoundTextures[map] = wanted;
            lastActiveMap = map;
        }
    }
    if (lastActiveMap != 0) glActiveTexture_ptr(GL_TEXTURE0);
    if (profilingSubmit) sSubmitTexBindMs += submit_clock_ms() - texT0;

    // Upload lighting
    if (sLoc.nrmMtx >= 0) {
        static const float identityNrm[9] = {
            1.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 1.0f
        };
        const float* nrmMtx = sVerticesPretransformed
            ? identityNrm
            : sNrmMatrix[sCurrentPosMtxId < 64 ? sCurrentPosMtxId : 0];
        glUniformMatrix3fv_ptr(sLoc.nrmMtx, 1, GL_FALSE, nrmMtx);
    }
    int numLights = 0;
    float ambR = 1.0f, ambG = 1.0f, ambB = 1.0f, ambA = 1.0f;
    if (sShadowProjPerspective && !sPostRanThisFrame) sun_capture_from_channel0();
    if (sChannels[0].enabled) {
        // Only lit channels contribute lighting; a disabled channel must pass
        // rasterized colors through untouched (matches GX hardware behavior).
        ambR = sChannels[0].ambColor[0];
        ambG = sChannels[0].ambColor[1];
        ambB = sChannels[0].ambColor[2];
        ambA = sChannels[0].ambColor[3];
        u32 mask = sChannels[0].lightMask;
        for (int i = 0; i < 8 && numLights < 4; i++) {
            if (mask & (1u << i)) {
                glUniform4f_ptr(sLoc.lightPos[numLights], sLights[i].pos[0], sLights[i].pos[1], sLights[i].pos[2], 0.0f);
                glUniform4f_ptr(sLoc.lightColor[numLights], sLights[i].color[0], sLights[i].color[1], sLights[i].color[2], sLights[i].color[3]);
                // Distance attenuation applies when the channel's attention
                // function requests it (game uses GX_AF_SPOT with lights).
                const float* k = sLights[i].k;
                bool attnOn = (sChannels[0].attnFn != GX_AF_NONE);
                glUniform4f_ptr(sLoc.lightK[numLights], k[0], k[1], k[2], attnOn ? 1.0f : 0.0f);
                if (sLoc.lightDir[numLights] >= 0)
                    glUniform4f_ptr(sLoc.lightDir[numLights], sLights[i].dir[0], sLights[i].dir[1], sLights[i].dir[2], 0.0f);
                if (sLoc.lightA[numLights] >= 0)
                    glUniform4f_ptr(sLoc.lightA[numLights], sLights[i].a[0], sLights[i].a[1], sLights[i].a[2], 0.0f);
                numLights++;
            }
        }
    }
    if (sLoc.numLights >= 0) glUniform1i_ptr(sLoc.numLights, numLights);
    if (sLoc.ambColor >= 0) glUniform4f_ptr(sLoc.ambColor, ambR, ambG, ambB, ambA);
    if (sLoc.chan0En >= 0) glUniform1i_ptr(sLoc.chan0En, sChannels[0].enabled ? 1 : 0);
    if (sLoc.chan0AttnFn >= 0) glUniform1i_ptr(sLoc.chan0AttnFn, (int)sChannels[0].attnFn);

    // Channel 1 (typically specular) feeds stages whose TEV order selects it.
    int numLights1 = 0;
    float a1r = 0.0f, a1g = 0.0f, a1b = 0.0f, a1a = 1.0f;
    if (sChannels[1].enabled) {
        a1r = sChannels[1].ambColor[0];
        a1g = sChannels[1].ambColor[1];
        a1b = sChannels[1].ambColor[2];
        a1a = sChannels[1].ambColor[3];
        u32 mask1 = sChannels[1].lightMask;
        for (int i = 0; i < 8 && numLights1 < 4; i++) {
            if (mask1 & (1u << i)) {
                glUniform4f_ptr(sLoc.lightPos1[numLights1], sLights[i].pos[0], sLights[i].pos[1], sLights[i].pos[2], 0.0f);
                glUniform4f_ptr(sLoc.lightColor1[numLights1], sLights[i].color[0], sLights[i].color[1], sLights[i].color[2], sLights[i].color[3]);
                const float* k1 = sLights[i].k;
                bool attnOn1 = (sChannels[1].attnFn != GX_AF_NONE);
                glUniform4f_ptr(sLoc.lightK1[numLights1], k1[0], k1[1], k1[2], attnOn1 ? 1.0f : 0.0f);
                if (sLoc.lightDir1[numLights1] >= 0)
                    glUniform4f_ptr(sLoc.lightDir1[numLights1], sLights[i].dir[0], sLights[i].dir[1], sLights[i].dir[2], 0.0f);
                if (sLoc.lightA1[numLights1] >= 0)
                    glUniform4f_ptr(sLoc.lightA1[numLights1], sLights[i].a[0], sLights[i].a[1], sLights[i].a[2], 0.0f);
                numLights1++;
            }
        }
    }
    if (sLoc.numLights1 >= 0) glUniform1i_ptr(sLoc.numLights1, numLights1);
    if (sLoc.ambColor1 >= 0) glUniform4f_ptr(sLoc.ambColor1, a1r, a1g, a1b, a1a);
    if (sLoc.chan1En >= 0) glUniform1i_ptr(sLoc.chan1En, sChannels[1].enabled ? 1 : 0);
    if (sLoc.chan1AttnFn >= 0) glUniform1i_ptr(sLoc.chan1AttnFn, (int)sChannels[1].attnFn);
    // Specular half-vector: light 7's dir field (offset 0x34) holds it.
    if (sChannels[1].enabled && sChannels[1].attnFn == GX_AF_SPEC) {
        u32 mask1 = sChannels[1].lightMask;
        for (int i = 7; i < 8; i++) {
            if (mask1 & (1u << i) && sLights[i].active) {
                if (sLoc.specHalf1 >= 0) {
                    glUniform4f_ptr(sLoc.specHalf1, sLights[i].dir[0], sLights[i].dir[1], sLights[i].dir[2], 0.0f);
                }
                if (sLoc.specAttn1 >= 0) {
                    glUniform4f_ptr(sLoc.specAttn1, sLights[i].a[0], sLights[i].a[1], sLights[i].a[2], 0.0f);
                }
                
                break;
            }
        }
    }
    // ALPHA0 channel: same light table, its own mask/attenuation.
    if (sLoc.chanA0En >= 0) {
        const GfxChannel& c0 = sChannels[0];
        int numLightsA = 0;
        float lightAlpha[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (c0.alphaEnabled) {
            const bool attnOnA = (c0.alphaAttnFn != GX_AF_NONE);
            for (int i = 0; i < 8 && numLightsA < 4; i++) {
                if (!(c0.alphaLightMask & (1u << i))) continue;
                glUniform4f_ptr(sLoc.lightPosA[numLightsA], sLights[i].pos[0], sLights[i].pos[1], sLights[i].pos[2], 0.0f);
                const float* kA = sLights[i].k;
                glUniform4f_ptr(sLoc.lightKA[numLightsA], kA[0], kA[1], kA[2], attnOnA ? 1.0f : 0.0f);
                if (sLoc.lightDirA[numLightsA] >= 0)
                    glUniform4f_ptr(sLoc.lightDirA[numLightsA], sLights[i].dir[0], sLights[i].dir[1], sLights[i].dir[2], 0.0f);
                if (sLoc.lightAA[numLightsA] >= 0)
                    glUniform4f_ptr(sLoc.lightAA[numLightsA], sLights[i].a[0], sLights[i].a[1], sLights[i].a[2], 0.0f);
                lightAlpha[numLightsA] = sLights[i].color[3];
                numLightsA++;
            }
        }
        glUniform1i_ptr(sLoc.chanA0En, c0.alphaEnabled ? 1 : 0);
        glUniform1i_ptr(sLoc.chanA0AttnFn, (int)c0.alphaAttnFn);
        glUniform1i_ptr(sLoc.numLightsA, numLightsA);
        glUniform1f_ptr(sLoc.ambAlpha0, c0.ambColor[3]);
        glUniform4f_ptr(sLoc.lightAlphaA, lightAlpha[0], lightAlpha[1], lightAlpha[2], lightAlpha[3]);
    }
    if (sLoc.materialColor1 >= 0)
        glUniform4f_ptr(sLoc.materialColor1, sChannels[1].matColor[0], sChannels[1].matColor[1],
                        sChannels[1].matColor[2], sChannels[1].matColor[3]);
    // Only present in programs built with fog in the key, so the locations are
    // -1 everywhere else and this costs nothing on the draws that have none.
    if (sLoc.fogParams >= 0)
        glUniform4f_ptr(sLoc.fogParams, sFogStart, sFogEnd, sFogNear, sFogFar);
    if (sLoc.fogColour >= 0)
        glUniform4f_ptr(sLoc.fogColour, sFogColour[0], sFogColour[1], sFogColour[2], 1.0f);
    if (sLoc.useMaterialRgb1 >= 0)
        glUniform1i_ptr(sLoc.useMaterialRgb1, sChannels[1].matSrc == GX_SRC_REG ? 1 : 0);

    // Texture coordinate generation for every GX slot. TEX0-TEX7 sources are
    // independent from the destination slot and may use different matrices.
    int tcProj = 0;
    for (int slot = 0; slot < 8; slot++) {
        int mode = 0;
        // Type 0 is the STQ (projective) texgen on hardware, named MTX2X4 in
        // this decomp's enum; 2D code passes it by that name for plain UVs, so
        // only position/normal sources (the water's screen projection) divide.
        if (sTexCoordGen[slot].active && sTexCoordGen[slot].type == 0
            && (sTexCoordGen[slot].src == GX_TG_POS || sTexCoordGen[slot].src == GX_TG_NRM))
            tcProj |= 1 << slot;
        if (slot < 8 && sTexCoordGen[slot].active) {
            GXTexGenSrc src = static_cast<GXTexGenSrc>(sTexCoordGen[slot].src);
            if (src == GX_TG_POS) mode = 1;
            else if (src == GX_TG_NRM) mode = 2;
            else if (src >= GX_TG_TEX0 && src <= GX_TG_TEX7)
                mode = 3 + int(src - GX_TG_TEX0);
            else if (src >= GX_TG_TEXCOORD0 && src <= GX_TG_TEXCOORD6) {
                // Solo los tipos de relieve (BUMP0..7 = 2..9) encadenan la salida
                // de una generación anterior. Con matriz (MTX2x4/MTX3x4) la SDK
                // deja la fila de entrada en TEX0: la coordenada del vértice.
                // Leerlo como encadenado daba (0,0) en la generación 0, y los
                // números de carga y el borde del círculo de vida (los dos
                // usan GX_TG_TEXCOORD0) salían transparentes.
                const u32 type = sTexCoordGen[slot].type;
                mode = (type >= 2 && type <= 9) ? 11 + int(src - GX_TG_TEXCOORD0) : 3;
            }
        }
        if (slot < 4 && (sTexBakedMask & (1 << slot))) mode = 0; // ya calculada en la CPU
        if (sLoc.tcMode[slot] >= 0) glUniform1i_ptr(sLoc.tcMode[slot], mode);
        if (mode != 0 && sLoc.tcMtx[slot] >= 0) {
            u32 mtxIdx = (slot < 8) ? sTexCoordGen[slot].mtxIdx : 0;
            if (mtxIdx >= 64) mtxIdx = 0;
            glUniformMatrix4fv_ptr(sLoc.tcMtx[slot], 1, GL_FALSE, sTexMatrices[mtxIdx]);
        }
    }
    if (sLoc.tcProj >= 0) glUniform1i_ptr(sLoc.tcProj, tcProj);
    if (sLoc.tcPostOn >= 0) {
        // Dual tex de las generaciones 0-3 (las que lleva el shader).
        int postOn = 0, norm = 0, is2x4 = 0;
        float post[12][4];
        for (int slot = 0; slot < 4; ++slot) {
            const TexCoordGen& tg = sTexCoordGen[slot];
            const u32 row = (tg.postIdx >= 64 && tg.postIdx <= 125) ? tg.postIdx - 64 : 61;
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c) post[slot * 3 + r][c] = sPostRows[std::min<u32>(row + r, 63)][c];
            if (!tg.active || tg.type >= GX_TG_BUMP0 || (sTexBakedMask & (1 << slot))) continue;
            if (tg.postIdx != 125 || tg.normalize) postOn |= 1 << slot;
            if (tg.normalize) norm |= 1 << slot;
            if (tg.type == GX_TG_MTX3X4) is2x4 |= 1 << slot; // este enum: MTX3X4 = 1 = 2x4 del hardware
        }
        glUniform1i_ptr(sLoc.tcPostOn, postOn);
        if (sLoc.tcNorm >= 0) glUniform1i_ptr(sLoc.tcNorm, norm);
        if (sLoc.tc2x4 >= 0) glUniform1i_ptr(sLoc.tc2x4, is2x4);
        if (postOn && sLoc.tcPost >= 0) glUniform4fv_ptr(sLoc.tcPost, 12, &post[0][0]);
    }
    if (sLoc.texPalette >= 0) {
        float rows[30][4];
        for (int m = 0; m < 10; ++m) {
            const float* d = sTexMatrices[30 + m * 3];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 4; ++c) rows[m * 3 + r][c] = d[c * 4 + r];
        }
        glUniform4fv_ptr(sLoc.texPalette, 30, &rows[0][0]);
    }

}

extern "C" void pc_gfx_debug_font_glyph(
    int character, int fontCode, int pageIndex, int row, int col, int mTexPageIdx,
    void* texObj, void* pageData,
    int minTexX, int minTexY, int maxTexX, int maxTexY,
    float posMinX, float posMinY, float posMaxX, float posMaxY)
{
    // One line per glyph: only when asked for (PIKMIN_FONT_DEBUG=1).
    static const bool enabled = getenv("PIKMIN_FONT_DEBUG") != nullptr;
    if (!enabled) return;
    uint32_t cpuGen = 0;
    uint32_t gpuGen = 0;
    if (texObj) {
        std::lock_guard<std::mutex> lock(sTextureRegistryMutex);
        uintptr_t key = (uintptr_t)texObj;
        auto it = sTextureSources.find(key);
        if (it != sTextureSources.end()) {
            cpuGen = it->second.generation;
            gpuGen = it->second.uploadedGeneration;
        }
    }
    
    GLuint activeGL = sActiveGLTextures[0];
    GLuint boundTex = sBoundTextures[0];
    int texGenMode = -1;
    if (sTexCoordGen[0].active) {
        GXTexGenSrc src = static_cast<GXTexGenSrc>(sTexCoordGen[0].src);
        if (src == GX_TG_POS) texGenMode = 1;
        else if (src == GX_TG_NRM) texGenMode = 2;
        else if (src >= GX_TG_TEX0 && src <= GX_TG_TEX7)
            texGenMode = 3 + int(src - GX_TG_TEX0);
        else if (src >= GX_TG_TEXCOORD0 && src <= GX_TG_TEXCOORD6)
            texGenMode = 11 + int(src - GX_TG_TEXCOORD0);
    }

    float u0 = minTexX / 32768.0f;
    float v0 = minTexY / 32768.0f;
    float u1 = maxTexX / 32768.0f;
    float v1 = maxTexY / 32768.0f;

    char safeC = (character >= 32 && character <= 126) ? (char)character : '?';

    printf("[FONT DEBUG] '%c' charCode=0x%04x code=%04x page=%d r/c=%d/%d mTexPageIdx=%d texObj=%p pageData=%p cpuGen=%u gpuGen=%u glActive=%u glBound=%u uv=(%.3f,%.3f)-(%.3f,%.3f) pos=(%.1f,%.1f)-(%.1f,%.1f) batchOpen=%d mode=%d gen=%u\n",
        safeC, character, fontCode, pageIndex, row, col, mTexPageIdx, texObj, pageData,
        cpuGen, gpuGen, activeGL, boundTex,
        u0, v0, u1, v1,
        posMinX, posMinY, posMaxX, posMaxY,
        sBatchOpen, texGenMode, sStateGen);
    fflush(stdout);
}


static void pc_gfx_end_impl(void);
#if PIKI_P2_HOST
static void p2_apply_ui43(bool want);
// Pikmin 2 dibuja el 2D en 4:3 centrado. Un quad que tapa toda la pantalla
// (oscurecido de cinematica, fundido a negro fuera de JUTFader) se quedaba en
// 4:3 con los laterales sin cubrir; ese se dibuja estirado.
static bool p2_prim_covers_screen(bool requireUi43 = true) {
    if ((requireUi43 && !sUi43) || !sInPrimitive || sVertexStream.empty() || sVertexStream.size() > 6) return false;
    const float* m = sPosMatrix[sCurrentPosMtxId];
    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
    for (const Vertex& v : sVertexStream) {
        const float mx = m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12];
        const float my = m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13];
        const float mz = m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14];
        const float cx = sProjMatrix[0] * mx + sProjMatrix[4] * my + sProjMatrix[8] * mz + sProjMatrix[12];
        const float cy = sProjMatrix[1] * mx + sProjMatrix[5] * my + sProjMatrix[9] * mz + sProjMatrix[13];
        const float cw = sProjMatrix[3] * mx + sProjMatrix[7] * my + sProjMatrix[11] * mz + sProjMatrix[15];
        if (std::fabs(cw) < 1e-6f) return false;
        minX = std::min(minX, cx / cw); maxX = std::max(maxX, cx / cw);
        minY = std::min(minY, cy / cw); maxY = std::max(maxY, cy / cw);
    }
    return minX <= -0.98f && maxX >= 0.98f && minY <= -0.98f && maxY >= 0.98f;
}
#endif

void pc_gfx_end(void) {
#if PIKI_P2_HOST
    if (sInPrimitive && sHaveVertex) {
        sVertexStream.push_back(sCurVertex);
        sHaveVertex = false;
    }
    // PIKMIN_FULLSCREEN_DEBUG=1: quien dibuja una primitiva que tapa toda la
    // pantalla (pantalla verde/azul del dialogo de la nave), en 2D o en 3D.
    static const bool dbg = getenv("PIKMIN_FULLSCREEN_DEBUG") != nullptr;
    static int dbgCount = 0;
    const bool coversUi = p2_prim_covers_screen();
    auto colourful = []() {
        for (const Vertex& v : sVertexStream)
            if (std::fabs(v.r - v.g) > 0.08f || std::fabs(v.g - v.b) > 0.08f) return true;
        return false;
    };
    if (dbg && dbgCount < 40 && sInPrimitive && !sVertexStream.empty() && colourful() && (coversUi || (!sShadowProjPerspective && p2_prim_covers_screen(false))
                                 || (sShadowProjPerspective && sVertexStream.size() <= 4 && p2_prim_covers_screen(false)))) {
        {
            ++dbgCount;
            const Vertex& v0 = sVertexStream.front();
            const Vertex& v1 = sVertexStream.back();
            fprintf(stderr, "[FULLSCREEN] persp=%d verts=%zu col0=(%.2f %.2f %.2f %.2f) colN=(%.2f %.2f %.2f) tex0=%u hasTex=%d posMtx=%u\n",
                    int(sShadowProjPerspective), sVertexStream.size(), v0.r, v0.g, v0.b, v0.a, v1.r, v1.g, v1.b, (unsigned)sActiveGLTextures[0],
                    int(sHasActiveTextures[0]), (unsigned)sCurrentPosMtxId);
            void* frames[16];
            const int n = backtrace(frames, 16);
            backtrace_symbols_fd(frames, n, 2);
        }
    }
    if (coversUi) {
        p2_apply_ui43(false);
        pc_gfx_end_impl();
        p2_apply_ui43(true);
        return;
    }
#endif
    pc_gfx_end_impl();
}

static void pc_gfx_end_impl(void) {
    if (sInPrimitive && sHaveVertex) {
        sVertexStream.push_back(sCurVertex);
        sHaveVertex = false;
    }

    // GXBegin's vertex count is part of the command stream contract.  Keep
    // immediate-mode callers to the same invariant as the display-list
    // parser: one VCD/VAT layout and exactly nverts complete vertices.  This
    // catches decomp mistakes such as writing a texture coordinate with a
    // GXPosition helper before they turn into unrelated triangles downstream.
    if (sInPrimitive && sVertexStream.size() != sExpectedVerts) {
        static unsigned mismatchReports = 0;
        if (pc_gfx_verbose() && mismatchReports++ < 16) {
            fprintf(stderr,
                    "[PC GX] immediate vertex-count mismatch: primitive=0x%02x fmt=%u expected=%u got=%zu\n",
                    unsigned(sCurrentPrimType), unsigned(sImmVtxFmt), unsigned(sExpectedVerts),
                    sVertexStream.size());
        }
        // The hardware consumes only the count declared by GXBegin.  Do not
        // let surplus host-side vertices alter the following primitive.
        if (sVertexStream.size() > sExpectedVerts) sVertexStream.resize(sExpectedVerts);
    }
    if (!sInPrimitive || sVertexStream.empty()) {
        sInPrimitive = false;
        return;
    }

    // Capture immutable packet before emitting to OpenGL (disabled - use display list capture)
    // captureCurrentPacket();

    const bool profilingSubmit = pc_tick_profiler_enabled();
    const double submitT0      = profilingSubmit ? submit_clock_ms() : 0.0;

    // The state key decides everything: identical state means this primitive
    // joins the open batch and no GL call is made at all. The hash is charged
    // to gl:uniforms on purpose, so the uniform cost it replaces is never
    // flattered by moving work out of the measured span.
    const uint64_t stateKey  = compute_batch_state_key();
    const GLenum   batchMode = batch_mode_for(sCurrentPrimType);
    ++sSubmitPrims;
    if (profilingSubmit) {
        sSubmitKeyMs += submit_clock_ms() - submitT0;
        note_batch_run(stateKey, sGlStateEpoch);
    }

    if (sBatchOpen && batching_enabled() && stateKey == sBatchKey && batchMode == sBatchMode) {
        append_primitive_to_batch();
        ++sBatchPrims;
        sInPrimitive = false;
        sAttrStep = 0;
        if (profilingSubmit) sSubmitUniformMs += submit_clock_ms() - submitT0;
        return;
    }

    // Different state: draw what is queued while the GPU still holds the
    // uniforms it was built with, and only then reprogram for the new batch.
    // The flush's own vbo/draw time is charged to its rows, not to uniforms.
    const double flushT0 = profilingSubmit ? submit_clock_ms() : 0.0;
    pc_gfx_flush_batch();
    const double flushT1 = profilingSubmit ? submit_clock_ms() : 0.0;

    apply_draw_state(profilingSubmit, flushT1);
    const double submitT1 = profilingSubmit ? submit_clock_ms() : 0.0;
    if (profilingSubmit) sSubmitUniformMs += (submitT1 - submitT0) - (flushT1 - flushT0);

    switch (sCurrentPrimType) {
        case GX_TRIANGLES: ++sPerfPrimitiveDraws[0]; break;
        case GX_TRIANGLESTRIP: ++sPerfPrimitiveDraws[1]; break;
        case GX_TRIANGLEFAN: ++sPerfPrimitiveDraws[2]; break;
        case GX_LINES: ++sPerfPrimitiveDraws[3]; break;
        case GX_LINESTRIP: ++sPerfPrimitiveDraws[4]; break;
        case GX_POINTS: ++sPerfPrimitiveDraws[5]; break;
        case GX_QUADS: ++sPerfPrimitiveDraws[6]; break;
    }

    gl_error_checkpoint("uniform upload");

    // Uniforms for this state are now on the GPU. Open the batch; the draw
    // happens at the next flush, with these same uniforms still bound.
    sBatchKey   = stateKey;
    sBatchMode  = batchMode;
    sBatchOpen  = true;
    sBatchPrims = 1;
    append_primitive_to_batch();
    if (!batching_enabled()) pc_gfx_flush_batch();

    static bool reportedFirstDraw = false;
    if (!reportedFirstDraw) {
        GLenum error = glGetError();
        printf("[PC Port] First GX draw: %zu vertices, GL status 0x%04x\n",
               sVertexStream.size(), static_cast<unsigned>(error));
        if (error != GL_NO_ERROR) {
            const char* errStr = "UNKNOWN";
            switch (error) {
                case GL_INVALID_ENUM: errStr = "GL_INVALID_ENUM"; break;
                case GL_INVALID_VALUE: errStr = "GL_INVALID_VALUE"; break;
                case GL_INVALID_OPERATION: errStr = "GL_INVALID_OPERATION"; break;
#if !PIKI_USE_GLES
                case GL_STACK_OVERFLOW: errStr = "GL_STACK_OVERFLOW"; break;
                case GL_STACK_UNDERFLOW: errStr = "GL_STACK_UNDERFLOW"; break;
#endif
                case GL_OUT_OF_MEMORY: errStr = "GL_OUT_OF_MEMORY"; break;
                case GL_INVALID_FRAMEBUFFER_OPERATION: errStr = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
            }
            printf("[PC Port] GL Error: %s\n", errStr);
        }
        // Check shader program
        GLint linkStatus;
        glGetProgramiv_ptr(sShaderProgram, GL_LINK_STATUS, &linkStatus);
        if (!linkStatus) {
            char log[4096];
            if (glGetProgramInfoLog_ptr) {
                glGetProgramInfoLog_ptr(sShaderProgram, sizeof(log), NULL, log);
            }
            printf("[PC Port] Shader link error: %s\n", log);
        }
        // Check active uniforms
        GLint numUniforms = 0;
        glGetProgramiv_ptr(sShaderProgram, GL_ACTIVE_UNIFORMS, &numUniforms);
        printf("[PC Port] Active uniforms: %d\n", numUniforms);
        for (int i = 0; i < numUniforms; i++) {
            char name[256];
            GLsizei length;
            GLint size;
            GLenum type;
            if (glGetActiveUniform_ptr) {
                glGetActiveUniform_ptr(sShaderProgram, i, sizeof(name), &length, &size, &type, name);
            } else {
                name[0] = '\0';
            }
            printf("[PC Port] Uniform %d: %s (type 0x%04x)\n", i, name, type);
        }
        const Vertex& first = sVertexStream.front();
        printf("[PC Port] First vertex: (%.2f, %.2f, %.2f); projection scale=(%.4f, %.4f), offset=(%.4f, %.4f); view offset=(%.2f, %.2f, %.2f)\n",
               first.x, first.y, first.z, sProjMatrix[0], sProjMatrix[5],
               sProjMatrix[12], sProjMatrix[13], sPosMatrix[sCurrentPosMtxId][12],
               sPosMatrix[sCurrentPosMtxId][13], sPosMatrix[sCurrentPosMtxId][14]);
        reportedFirstDraw = true;
    }

    sInPrimitive = false;
    sAttrStep = 0;
}

static bool read_be_u16(const u8*& cursor, const u8* end, u16& value) {
    if (end - cursor < 2) return false;
    value = (u16(cursor[0]) << 8) | cursor[1];
    cursor += 2;
    return true;
}

static bool read_attribute_index(GXAttrType type, const u8*& cursor, const u8* end, u16& index) {
    if (type == GX_INDEX8) {
        if (cursor >= end) return false;
        index = *cursor++;
        return true;
    }
    if (type == GX_INDEX16) return read_be_u16(cursor, end, index);
    return false;
}

static inline float read_attr_float(const u8* ptr, GXCompType type, u8 frac) {
    if (type == GX_F32) {
        float f;
        memcpy(&f, ptr, sizeof(float));
        return f;
    } else if (type == GX_S16) {
        s16 val;
        memcpy(&val, ptr, sizeof(s16));
        return static_cast<float>(val) / static_cast<float>(1 << frac);
    } else if (type == GX_U16) {
        u16 val;
        memcpy(&val, ptr, sizeof(u16));
        return static_cast<float>(val) / static_cast<float>(1 << frac);
    } else if (type == GX_S8) {
        s8 val = static_cast<s8>(*ptr);
        return static_cast<float>(val) / static_cast<float>(1 << frac);
    } else if (type == GX_U8) {
        u8 val = *ptr;
        return static_cast<float>(val) / static_cast<float>(1 << frac);
    }
    return 0.0f;
}

static inline u8 get_comptype_size(GXCompType type) {
    if (type == GX_F32) return 4;
    if (type == GX_S16 || type == GX_U16) return 2;
    return 1;
}

static inline u8 get_color_size(GXCompType type) {
    if (type == GX_RGB565 || type == GX_RGBA4) return 2;
    if (type == GX_RGB8 || type == GX_RGBA6) return 3;
    return 4; // GX_RGBA8 / GX_RGBX8
}

static inline void read_attr_color(const u8* ptr, GXCompType type, u8& r, u8& g, u8& b, u8& a) {
    if (type == GX_RGBA8 || type == GX_RGBX8) {
        r = ptr[0]; g = ptr[1]; b = ptr[2]; a = ptr[3];
    } else if (type == GX_RGB8 || type == GX_RGBA6) {
        r = ptr[0]; g = ptr[1]; b = ptr[2]; a = 255;
    } else if (type == GX_RGB565) {
        u16 val;
        memcpy(&val, ptr, sizeof(u16));
        r = (val >> 11) & 0x1F; r = (r << 3) | (r >> 2);
        g = (val >> 5) & 0x3F;  g = (g << 2) | (g >> 4);
        b = val & 0x1F;         b = (b << 3) | (b >> 2);
        a = 255;
    } else if (type == GX_RGBA4) {
        u16 val;
        memcpy(&val, ptr, sizeof(u16));
        r = (val >> 12) & 0xF; r = (r << 4) | r;
        g = (val >> 8) & 0xF;  g = (g << 4) | g;
        b = (val >> 4) & 0xF;  b = (b << 4) | b;
        a = val & 0xF;         a = (a << 4) | a;
    } else {
        r = 255; g = 255; b = 255; a = 255;
    }
}

static void transform_position(u8 matrixId, float x, float y, float z,
                               float& outX, float& outY, float& outZ) {
    const float* m = sPosMatrix[matrixId < 64 ? matrixId : 0];
    outX = m[0] * x + m[4] * y + m[8] * z + m[12];
    outY = m[1] * x + m[5] * y + m[9] * z + m[13];
    outZ = m[2] * x + m[6] * y + m[10] * z + m[14];
}

static void transform_normal(u8 matrixId, float x, float y, float z,
                             float& outX, float& outY, float& outZ) {
    const float* m = sNrmMatrix[matrixId < 64 ? matrixId : 0];
    outX = m[0] * x + m[3] * y + m[6] * z;
    outY = m[1] * x + m[4] * y + m[7] * z;
    outZ = m[2] * x + m[5] * y + m[8] * z;
}

// Byte size of one vertex attribute when stored directly in the stream.
static u8 attr_inline_size(GXAttr attr, const VertexFormatState& fmt) {
    if (attr >= GX_VA_PNMTXIDX && attr <= GX_VA_TEX7MTXIDX) return 1;
    if (attr == GX_VA_POS) return fmt.count * get_comptype_size(fmt.type);
    if (attr == GX_VA_NRM || attr == GX_VA_NBT) {
        if (fmt.count == GX_NRM_NBT) return 9 * get_comptype_size(fmt.type);
        return 3 * get_comptype_size(fmt.type); // XYZ and NBT3 both lead with 3 values
    }
    if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) return get_color_size(fmt.type);
    if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        return (fmt.count == GX_TEX_ST ? 2 : 1) * get_comptype_size(fmt.type);
    }
    return 0;
}

static u8 fifo_imm_attr_bytes(GXAttr attr)
{
    const GXAttrType desc = sVtxDesc[attr];
    if (desc == GX_NONE) return 0;
    if (desc == GX_INDEX8) return 1;
    if (desc == GX_INDEX16) return 2;
    return attr_inline_size(attr, sVtxFormats[sImmVtxFmt][attr]);
}

static void fifo_imm_start_vertex()
{
    sFifoVertex.x  = 0.0f;
    sFifoVertex.y  = 0.0f;
    sFifoVertex.z  = 0.0f;
    sFifoVertex.nx = 0.0f;
    sFifoVertex.ny = 0.0f;
    sFifoVertex.nz = 1.0f;
    sFifoVertex.r  = 1.0f;
    sFifoVertex.g  = 1.0f;
    sFifoVertex.b  = 1.0f;
    sFifoVertex.a  = 1.0f;
    sFifoVertex.matrixSlot = 0.0f;
    for (int tc = 0; tc < 4; ++tc) {
        sFifoVertex.tex[tc][0] = 0.0f;
        sFifoVertex.tex[tc][1] = 0.0f;
    }
    sFifoGot  = 0;
    sFifoNeed = 0;
    sFifoAttr = GX_VA_PNMTXIDX;
    if (!sFifoImmActive) return;
    while (sFifoAttr <= GX_VA_TEX7 && sVtxDesc[sFifoAttr] == GX_NONE) {
        ++sFifoAttr;
    }
    if (sFifoAttr <= GX_VA_TEX7) {
        sFifoNeed = fifo_imm_attr_bytes(static_cast<GXAttr>(sFifoAttr));
    }
}

static void fifo_imm_apply_attr(GXAttr attr, const u8* data)
{
    const GXAttrType desc = sVtxDesc[attr];
    if (attr <= GX_VA_TEX7MTXIDX) {
        if (attr == GX_VA_PNMTXIDX) {
            sFifoMtxId = data[0];
        }
        return;
    }

    const u8* element = data;
    if (desc == GX_INDEX8 || desc == GX_INDEX16) {
        u16 index = data[0];
        if (desc == GX_INDEX16) {
            index = static_cast<u16>((data[0] << 8) | data[1]);
        }
        const VertexArrayState& array = sVtxArrays[attr];
        if (!array.base || array.stride == 0) return;
        element = array.base + size_t(index) * array.stride;
    }

    if (attr == GX_VA_POS) {
        const VertexFormatState& fmtState = sVtxFormats[sImmVtxFmt][attr];
        const u8 compSize                 = get_comptype_size(fmtState.type);
        const float x                     = read_attr_float(element, fmtState.type, fmtState.frac);
        const float y                     = read_attr_float(element + compSize, fmtState.type, fmtState.frac);
        const float z = (fmtState.count == GX_POS_XYZ) ? read_attr_float(element + 2 * compSize, fmtState.type, fmtState.frac) : 0.0f;
        transform_position(sFifoMtxId, x, y, z, sFifoVertex.x, sFifoVertex.y, sFifoVertex.z);
        sVerticesPretransformed = true;
        sVertexUsesPalette = sGpuSkinningEnabled && sVtxDesc[GX_VA_PNMTXIDX] != GX_NONE;
    } else if (attr == GX_VA_CLR0) {
        const VertexFormatState& fmtState = sVtxFormats[sImmVtxFmt][attr];
        u8 r, g, b, a;
        read_attr_color(element, fmtState.type, r, g, b, a);
        sFifoVertex.r = r / 255.0f;
        sFifoVertex.g = g / 255.0f;
        sFifoVertex.b = b / 255.0f;
        sFifoVertex.a = a / 255.0f;
    } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
        const VertexFormatState& fmtState = sVtxFormats[sImmVtxFmt][attr];
        const u8 compSize                 = get_comptype_size(fmtState.type);
        const int tc                      = int(attr) - int(GX_VA_TEX0);
        if (tc < 4) {
            sFifoVertex.tex[tc][0] = read_attr_float(element, fmtState.type, fmtState.frac);
            sFifoVertex.tex[tc][1]
                = (fmtState.count == GX_TEX_ST) ? read_attr_float(element + compSize, fmtState.type, fmtState.frac) : 0.0f;
        }
    }
}

static void fifo_cmd_byte(u8 val);
static void fifo_imm_byte(u8 val)
{
    if (!sInPrimitive) { fifo_cmd_byte(val); return; }
    if (!sFifoImmActive || sFifoAttr > GX_VA_TEX7) return;
    if (sFifoNeed == 0) return;
    if (sFifoGot < sizeof(sFifoTmp)) {
        sFifoTmp[sFifoGot++] = val;
    }
    if (sFifoGot < sFifoNeed) return;

    fifo_imm_apply_attr(static_cast<GXAttr>(sFifoAttr), sFifoTmp);
    ++sFifoAttr;
    while (sFifoAttr <= GX_VA_TEX7 && sVtxDesc[sFifoAttr] == GX_NONE) {
        ++sFifoAttr;
    }
    sFifoGot = 0;
    if (sFifoAttr <= GX_VA_TEX7) {
        sFifoNeed = fifo_imm_attr_bytes(static_cast<GXAttr>(sFifoAttr));
        return;
    }

    sVertexStream.push_back(sFifoVertex);
    if (sVertexStream.size() >= sExpectedVerts) {
        pc_gfx_end();
        return;
    }
    fifo_imm_start_vertex();
}

// ── Embedded GP command stream handlers ──
// Display lists carry more than vertex streams: material state (BP registers),
// vertex descriptors (CP registers), matrices/lighting (XF registers) and even
// nested display lists travel inline. Pikmin 2's J3D bakes *all* of its
// material and shape setup this way (J3DMaterial::makeDisplayList,
// J3DShape::makeVcdVatCmd, J3DShapeMtx via direct FIFO writes), so this is a
// real register-level decoder: every register the game writes is turned into
// the same pc_gfx state the GX API entry points produce.

extern "C" void* pc_host_from_gc_phys(uint32_t phys);

static bool read_be_u32(const u8*& cursor, const u8* end, u32& value) {
    if (end - cursor < 4) return false;
    value = (u32(cursor[0]) << 24) | (u32(cursor[1]) << 16) | (u32(cursor[2]) << 8) | cursor[3];
    cursor += 4;
    return true;
}

static inline float sext11_to_float(u32 v) {
    v &= 0x7FF;
    if (v & 0x400) v -= 0x800; // sign extend 11-bit
    return static_cast<float>(static_cast<s32>(v)) / 256.0f;
}

static inline float bits_to_float(u32 bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static void gx_regs_report_once(const char* what, u32 reg) {
    // Fixed array, no heap: a corrupt display list after a cutscene skip
    // could crash inside the old unordered_set's heap nodes.
    static u32 reported[64];
    static u32 count = 0;
    for (u32 i = 0; i < count; ++i)
        if (reported[i] == reg) return;
    if (count >= 64) return;
    reported[count++] = reg;
    fprintf(stderr, "[PC GX] %s register 0x%03X not modelled; ignored\n", what, reg);
}

// ─── CP registers: vertex descriptor, formats, arrays, matrix indices ───

static u32 sCpVat[3][8] = {};       // 0x70/0x80/0x90 + fmt
static u32 sCpVcdLo = 0;
static u32 sCpArrayBase[16] = {};   // 0xA0..0xAF as written (physical)
static u32 sCpArrayStride[16] = {}; // 0xB0..0xBF
static u32 sCpMatIdxA = 0x3CF3CF00; // 0x30: pos + tex0..3 matrix indices
static u32 sCpMatIdxB = 0x00F3CF3C; // 0x40: tex4..7 matrix indices

static void cp_apply_tex_mtx_indices() {
    // Matrix indices live outside the XF texgen registers; the texgen keeps
    // whatever mtxIdx it was configured with, so refresh them from CP.
    for (int c = 0; c < 8; ++c) {
        const u32 idx = c < 4 ? (sCpMatIdxA >> (6 + 6 * c)) & 0x3F : (sCpMatIdxB >> (6 * (c - 4))) & 0x3F;
        if (sTexCoordGen[c].mtxIdx != idx) {
            sTexCoordGen[c].mtxIdx = idx;
            state_touched();
        }
    }
}

static void cp_apply_array(u32 slot) {
    // 0xA0+i: POS..TEX7 (i 0..11), then the four index arrays (12..15).
    void* base = sCpArrayBase[slot] ? pc_host_from_gc_phys(sCpArrayBase[slot]) : nullptr;
    const GXAttr attr = slot < 12 ? GXAttr(GX_VA_POS + slot) : GXAttr(GX_POS_MTX_ARRAY + (slot - 12));
    pc_gfx_set_array(attr, base, u8(sCpArrayStride[slot] & 0xFF));
}

static void cp_apply_vat(u32 fmt) {
    if (fmt >= GX_MAX_VTXFMT) return;
    {
        static const bool trace = getenv("PIKMIN_GXTRACE") != nullptr;
        if (trace) fprintf(stderr, "[GXvat] fmt=%u A=%08X B=%08X C=%08X pos(t%u s%u) nrm(t%u) tex0(t%u s%u)\n", fmt,
                           sCpVat[0][fmt], sCpVat[1][fmt], sCpVat[2][fmt], (sCpVat[0][fmt] >> 1) & 7, (sCpVat[0][fmt] >> 4) & 0x1F,
                           (sCpVat[0][fmt] >> 10) & 7, (sCpVat[0][fmt] >> 22) & 7, (sCpVat[0][fmt] >> 25) & 0x1F);
    }
    // Hardware VAT layout; see J3DGDSetVtxAttrFmtv / GXSetVtxAttrFmt.
    const u32 a = sCpVat[0][fmt], b = sCpVat[1][fmt], c = sCpVat[2][fmt];
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_POS, GXCompCnt(a & 1), GXCompType((a >> 1) & 7), u8((a >> 4) & 0x1F));
    {
        const bool nbt = ((a >> 9) & 1) != 0;
        const bool nbt3 = ((a >> 31) & 1) != 0;
        const GXCompCnt cnt = nbt ? (nbt3 ? GX_NRM_NBT3 : GX_NRM_NBT) : GX_NRM_XYZ;
        pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_NRM, cnt, GXCompType((a >> 10) & 7), 0);
        pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_NBT, cnt, GXCompType((a >> 10) & 7), 0);
    }
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_CLR0, GXCompCnt((a >> 13) & 1), GXCompType((a >> 14) & 7), 0);
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_CLR1, GXCompCnt((a >> 17) & 1), GXCompType((a >> 18) & 7), 0);
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_TEX0, GXCompCnt((a >> 21) & 1), GXCompType((a >> 22) & 7), u8((a >> 25) & 0x1F));
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_TEX1, GXCompCnt(b & 1), GXCompType((b >> 1) & 7), u8((b >> 4) & 0x1F));
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_TEX2, GXCompCnt((b >> 9) & 1), GXCompType((b >> 10) & 7), u8((b >> 13) & 0x1F));
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_TEX3, GXCompCnt((b >> 18) & 1), GXCompType((b >> 19) & 7), u8((b >> 22) & 0x1F));
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_TEX4, GXCompCnt((b >> 27) & 1), GXCompType((b >> 28) & 7), u8(c & 0x1F));
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_TEX5, GXCompCnt((c >> 5) & 1), GXCompType((c >> 6) & 7), u8((c >> 9) & 0x1F));
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_TEX6, GXCompCnt((c >> 14) & 1), GXCompType((c >> 15) & 7), u8((c >> 18) & 0x1F));
    pc_gfx_set_vtx_attr_fmt(GXVtxFmt(fmt), GX_VA_TEX7, GXCompCnt((c >> 23) & 1), GXCompType((c >> 24) & 7), u8((c >> 27) & 0x1F));
}

static bool gx_regs_disabled();
static void handle_cp_reg(u8 addr, u32 value) {
    if (gx_regs_disabled()) return;
    switch (addr) {
    case 0x30: // MATINDEX_A
        sCpMatIdxA = value;
        pc_gfx_set_current_mtx(value & 0x3F);
        cp_apply_tex_mtx_indices();
        break;
    case 0x40: // MATINDEX_B
        sCpMatIdxB = value;
        cp_apply_tex_mtx_indices();
        break;
    case 0x50: { // VCD_LO
        sCpVcdLo = value;
        pc_gfx_set_vtx_desc(GX_VA_PNMTXIDX, (value & 1) ? GX_DIRECT : GX_NONE);
        for (int t = 0; t < 8; ++t)
            pc_gfx_set_vtx_desc(GXAttr(GX_VA_TEX0MTXIDX + t), ((value >> (1 + t)) & 1) ? GX_DIRECT : GX_NONE);
        pc_gfx_set_vtx_desc(GX_VA_POS, GXAttrType((value >> 9) & 3));
        pc_gfx_set_vtx_desc(GX_VA_NRM, GXAttrType((value >> 11) & 3));
        pc_gfx_set_vtx_desc(GX_VA_NBT, GX_NONE);
        pc_gfx_set_vtx_desc(GX_VA_CLR0, GXAttrType((value >> 13) & 3));
        pc_gfx_set_vtx_desc(GX_VA_CLR1, GXAttrType((value >> 15) & 3));
        break;
    }
    case 0x60: // VCD_HI
        for (int t = 0; t < 8; ++t)
            pc_gfx_set_vtx_desc(GXAttr(GX_VA_TEX0 + t), GXAttrType((value >> (2 * t)) & 3));
        break;
    case 0x70 ... 0x77: sCpVat[0][addr - 0x70] = value; cp_apply_vat(addr - 0x70); break;
    case 0x80 ... 0x87: sCpVat[1][addr - 0x80] = value; cp_apply_vat(addr - 0x80); break;
    case 0x90 ... 0x97: sCpVat[2][addr - 0x90] = value; cp_apply_vat(addr - 0x90); break;
    case 0xA0 ... 0xAF: sCpArrayBase[addr - 0xA0] = value; cp_apply_array(addr - 0xA0); break;
    case 0xB0 ... 0xBF: sCpArrayStride[addr - 0xB0] = value; cp_apply_array(addr - 0xB0); break;
    case 0x20: // VCACHE / misc
        break;
    default:
        gx_regs_report_once("CP", addr);
        break;
    }
}

// ─── BP registers: textures, TEV, blending, fog ───

// Shadow of the BP registers, for masked writes. Seeded with what GXInit
// leaves in the real ones: colour and alpha updates enabled (0x41), else the
// mask J3D uses for blend modes keeps them off and nothing is ever drawn.
static u32 sBpRegs[256] = {};
static struct BpRegsInit { BpRegsInit() { sBpRegs[0x41] = 0x18; } } sBpRegsInit;
static u32 sBpMask = 0xFFFFFF;

// Texture units come in as five separate registers; they are only combined
// into a texture object when a primitive is about to be drawn, so partially
// updated units never reach the cache (bp_resolve_textures, from pc_gfx_begin).
struct BpTlutLoad { u32 phys = 0; u16 entries = 0; u32 revision = 0; };
static PcGfxHostMap<u32, BpTlutLoad> sBpTlutLoads;
struct BpTlutObj { GXTlutObj obj; u32 phys = 0; u16 entries = 0; u32 fmt = 0xFF; };
static PcGfxHostMap<u32, BpTlutObj> sBpTlutObjs;
// Host-owned (survives section heap teardown): map nodes and the GXTexObj
// itself must not come from the game's current JKR heap.
struct BpTexObjFree { void operator()(GXTexObj* p) const { free(p); } };
static PcGfxHostMap<uint64_t, std::unique_ptr<GXTexObj, BpTexObjFree>> sBpTexObjs;

static GXTexFilter bp_min_filter(u32 hw) {
    switch (hw & 7) {
    case 1: return GX_NEAR_MIP_NEAR;
    case 2: return GX_NEAR_MIP_LIN;
    case 4: return GX_LINEAR;
    case 5: return GX_LIN_MIP_NEAR;
    case 6: return GX_LIN_MIP_LIN;
    default: return GX_NEAR;
    }
}

static void bp_resolve_texture(int map) {
    const u32 regBase = map < 4 ? 0x80 + map : 0xA0 + (map - 4);
    const u32 mode0  = sBpRegs[regBase];
    const u32 mode1  = sBpRegs[regBase + 4];
    const u32 image0 = sBpRegs[regBase + 8];
    const u32 image3 = sBpRegs[regBase + 20];
    const u32 tlut   = sBpRegs[regBase + 24];
    (void)mode1;
    const u16 width  = u16((image0 & 0x3FF) + 1);
    const u16 height = u16(((image0 >> 10) & 0x3FF) + 1);
    const u32 format = (image0 >> 20) & 0xF;
    const u32 phys   = (image3 & 0xFFFFFF) << 5;
    void* image = phys ? pc_host_from_gc_phys(phys) : nullptr;
    if (!image) return;
    const GXTexWrapMode wrapS = GXTexWrapMode(mode0 & 3);
    const GXTexWrapMode wrapT = GXTexWrapMode((mode0 >> 2) & 3);
    const GXTexFilter magFilt = ((mode0 >> 4) & 1) ? GX_LINEAR : GX_NEAR;
    const GXTexFilter minFilt = bp_min_filter(mode0 >> 5);
    const bool mipmap = minFilt != GX_NEAR && minFilt != GX_LINEAR;
    const bool indexed = format == GX_TF_C4 || format == GX_TF_C8 || format == GX_TF_C14X2;

    u32 tlutName = 0;
    u32 tlutPhys = 0;
    if (indexed) {
        tlutName = 0x1000 | (tlut & 0x3FF);
        const GXTlutFmt tlutFmt = GXTlutFmt((tlut >> 10) & 3);
        auto load = sBpTlutLoads.find(tlutName);
        if (load != sBpTlutLoads.end() && load->second.phys) {
            BpTlutObj& t = sBpTlutObjs[tlutName];
            if (t.phys != load->second.phys || t.entries != load->second.entries || t.fmt != u32(tlutFmt)) {
                void* lut = pc_host_from_gc_phys(load->second.phys);
                if (lut) {
                    pc_gfx_init_tlut_obj(&t.obj, lut, tlutFmt, load->second.entries);
                    pc_gfx_load_tlut(&t.obj, tlutName);
                    t.phys = load->second.phys;
                    t.entries = load->second.entries;
                    t.fmt = u32(tlutFmt);
                }
            }
            tlutPhys = t.phys;
        }
    }

    uint64_t key = uint64_t(phys) | (uint64_t(format) << 32) | (uint64_t(mipmap) << 36) | (uint64_t(wrapS) << 37)
                 | (uint64_t(wrapT) << 39);
    key ^= uint64_t(width) << 41;
    key ^= uint64_t(height) << 51;
    key ^= uint64_t(tlutPhys) * 0x9E3779B97F4A7C15ull;
    auto it = sBpTexObjs.find(key);
    if (it == sBpTexObjs.end()) {
        std::unique_ptr<GXTexObj, BpTexObjFree> obj((GXTexObj*)calloc(1, sizeof(GXTexObj)));
        if (indexed)
            pc_gfx_init_tex_obj_ci(obj.get(), image, width, height, GXCITexFmt(format), wrapS, wrapT,
                                   mipmap ? GX_TRUE : GX_FALSE, tlutName);
        else
            pc_gfx_init_tex_obj(obj.get(), image, width, height, GXTexFmt(format), wrapS, wrapT,
                                mipmap ? GX_TRUE : GX_FALSE);
        it = sBpTexObjs.emplace(key, std::move(obj)).first;
        static const bool sTexLog = getenv("PIKMIN_TEXLOG") != nullptr;
        if (sTexLog)
            printf("[PC BPTex] map=%d phys=0x%08X fmt=%u %ux%u wrap=%u,%u mip=%d tlut=0x%X min=%u mag=%u\n", map, phys,
                   format, width, height, unsigned(wrapS), unsigned(wrapT), int(mipmap), tlutName, unsigned(minFilt),
                   unsigned(magFilt));
    }
    pc_gfx_set_tex_obj_filter(it->second.get(), minFilt, magFilt);
    pc_gfx_load_tex_obj(it->second.get(), GXTexMapID(map));
}

static void bp_resolve_textures() {
    if (!sBpTexDirty) return;
    const u8 dirty = sBpTexDirty;
    sBpTexDirty = 0;
    for (int map = 0; map < 8; ++map)
        if (dirty & (1u << map)) bp_resolve_texture(map);
}

static void bp_apply_fog() {
    // Rebuild a (near, far, start, end) set that reproduces the A/B/C fog
    // coefficients the game wrote; the shader only ever sees those through
    // this same mapping, so the exact original planes are not needed.
    const u32 c32 = sBpRegs[0xF1];
    const u32 fsel = (c32 >> 21) & 7;
    const u32 colour = sBpRegs[0xF2];
    const u8 r = u8((colour >> 16) & 0xFF), g = u8((colour >> 8) & 0xFF), b = u8(colour & 0xFF);
    if (fsel == 0) {
        pc_gfx_set_fog(0, 0.0f, 0.0f, 0.0f, 0.0f, r, g, b);
        return;
    }
    const float tA = bits_to_float((sBpRegs[0xEE] & 0xFFFFF) << 12);
    const u32 exponent = sBpRegs[0xF0] & 0xFF;
    const float mantissa = float(sBpRegs[0xEF] & 0xFFFFFF) / 8388638.0f;
    const float A = tA * float(1u << (exponent & 31));
    const float B = mantissa * std::ldexp(1.0f, int(exponent) - 1);
    const float C = bits_to_float((c32 & 0xFFFFF) << 12);
    if (A == 0.0f || !(sCurProjFar > sCurProjNear)) {
        pc_gfx_set_fog(0, 0.0f, 0.0f, 0.0f, 0.0f, r, g, b);
        return;
    }
    // The fog unit only sees A/B/C, but the shader reconstructs eye depth from
    // the real projection planes, so rebuild start/end against those.
    const float nearZ = sCurProjNear;
    const float farZ = sCurProjFar;
    (void)B;
    const float span = farZ * nearZ / ((farZ - nearZ) * A);
    const float startZ = C * span;
    pc_gfx_set_fog(1, startZ, startZ + span, nearZ, farZ, r, g, b);
}

static bool gx_regs_disabled();
static void handle_bp_reg(u32 hex) {
    if (gx_regs_disabled()) return;
    state_touched();
    const u32 reg = (hex >> 24) & 0xFF;
    if (reg == 0xFE) { // Mask for the next write.
        sBpMask = hex & 0xFFFFFF;
        return;
    }
    const u32 value = (sBpRegs[reg] & ~sBpMask & 0xFFFFFF) | (hex & sBpMask & 0xFFFFFF);
    sBpMask = 0xFFFFFF;
    sBpRegs[reg] = value;
    hex = (reg << 24) | value;

    switch (reg) {
    case 0x00: { // GEN_MODE
        pc_gfx_set_num_tev_stages(u8(((hex >> 10) & 0xF) + 1));
        pc_gfx_set_num_ind_stages((hex >> 16) & 0x7);
        static const GXCullMode hw2gx[4] = { GX_CULL_NONE, GX_CULL_BACK, GX_CULL_FRONT, GX_CULL_ALL };
        pc_gfx_set_cull_mode(hw2gx[(hex >> 14) & 3]);
        break;
    }
    case 0x28 ... 0x2F: { // TREF: TEV order, two stages per register
        const u32 base = reg - 0x28;
        for (int half = 0; half < 2; ++half) {
            const u32 s = base * 2 + half;
            if (s >= GX_MAXTEVSTAGE) break;
            const u32 bits = hex >> (12 * half);
            sTevStages[s].texMap = GXTexMapID(bits & 0x7);
            sTevStages[s].texCoord = GXTexCoordID((bits >> 3) & 0x7);
            sTevStages[s].textureEnabled = ((bits >> 6) & 1) != 0;
            const u32 ras = (bits >> 7) & 0x7;
            sTevStages[s].rasChannel = ras == 7 ? -1 : ras == 1 ? 1 : 0;
        }
        break;
    }
    case 0x40: // PE Z mode
        pc_gfx_set_z_mode(GXBool(hex & 1), GXCompare((hex >> 1) & 7), GXBool((hex >> 4) & 1));
        break;
    case 0x41: { // PE color mode: blend/logic operation and write masks
        const bool blend = (hex & 0x1) != 0;
        const bool logic = ((hex >> 1) & 0x1) != 0;
        const bool subtract = ((hex >> 11) & 0x1) != 0;
        GXBlendMode mode = subtract ? GX_BM_SUBTRACT : logic ? GX_BM_LOGIC : blend ? GX_BM_BLEND : GX_BM_NONE;
        pc_gfx_set_blend_mode(mode, GXBlendFactor((hex >> 8) & 0x7),
                              GXBlendFactor((hex >> 5) & 0x7), GXLogicOp((hex >> 12) & 0xF));
        pc_gfx_set_color_update(GXBool((hex >> 3) & 0x1));
        pc_gfx_set_alpha_update(GXBool((hex >> 4) & 0x1));
        break;
    }
    case 0x42: // PE CMODE1 (constant destination alpha): not modelled by the GX API path either
    case 0x43: // Z compare location
        break;
    case 0x64: { // TLUT load: source address
        sBpRegs[0x64] = value;
        break;
    }
    case 0x65: { // TLUT load: destination tmem + size
        const u32 name = 0x1000 | (value & 0x3FF);
        BpTlutLoad& load = sBpTlutLoads[name];
        load.phys = (sBpRegs[0x64] & 0xFFFFFF) << 5;
        load.entries = u16(((value >> 10) & 0x3FF) * 16);
        ++load.revision;
        break;
    }
    case 0x80 ... 0x8B: case 0x94 ... 0x9B: // texture units 0-3
        sBpTexDirty |= u8(1u << ((reg - 0x80) & 3));
        break;
    case 0xA0 ... 0xAB: case 0xB4 ... 0xBB: // texture units 4-7
        sBpTexDirty |= u8(1u << (4 + ((reg - 0xA0) & 3)));
        break;
    case 0x8C ... 0x93: case 0xAC ... 0xB3: // tmem cache layout: irrelevant here
        break;
    case 0xC0 ... 0xDF: { // TEV combiner environment, color+alpha per stage
        const u32 idx = reg - 0xC0;
        const u32 stage = idx >> 1;
        if (stage >= GX_MAXTEVSTAGE) break;
        TevStageState& st = sTevStages[stage];
        if ((idx & 1) == 0) { // color combiner
            st.colorIn[0] = decode_tev_color_a(hex);
            st.colorIn[1] = decode_tev_color_b(hex);
            st.colorIn[2] = decode_tev_color_c(hex);
            st.colorIn[3] = decode_tev_color_d(hex);
            const u32 biasBits = (hex >> 16) & 0x3;
            const u32 opBit = (hex >> 18) & 0x1;
            const u32 scaleBits = (hex >> 20) & 0x3;
            st.colorOp = biasBits == 3 ? GXTevOp(8 + scaleBits * 2 + opBit)
                                       : (opBit ? GX_TEV_SUB : GX_TEV_ADD);
            st.colorBias = biasBits == 3 ? GX_TB_ZERO : GXTevBias(biasBits);
            st.colorClamp = GXBool((hex >> 19) & 0x1);
            st.colorScale = biasBits == 3 ? GX_CS_SCALE_1 : GXTevScale(scaleBits);
            st.colorOutReg = GXTevRegID((hex >> 22) & 0x3);
        } else { // alpha combiner
            // Bits 0..3 are the texture/raster swap selectors, not alpha inputs.
            sTevRasSwapSel[stage] = GXTevSwapSel(hex & 0x3);
            sTevTexSwapSel[stage] = GXTevSwapSel((hex >> 2) & 0x3);
            st.alphaIn[0] = decode_tev_alpha_a(hex);
            st.alphaIn[1] = decode_tev_alpha_b(hex);
            st.alphaIn[2] = decode_tev_alpha_c(hex);
            st.alphaIn[3] = decode_tev_alpha_d(hex);
            const u32 biasBits = (hex >> 16) & 0x3;
            const u32 opBit = (hex >> 18) & 0x1;
            const u32 scaleBits = (hex >> 20) & 0x3;
            st.alphaOp = biasBits == 3 ? GXTevOp(8 + scaleBits * 2 + opBit)
                                       : (opBit ? GX_TEV_SUB : GX_TEV_ADD);
            st.alphaBias = biasBits == 3 ? GX_TB_ZERO : GXTevBias(biasBits);
            st.alphaClamp = GXBool((hex >> 19) & 0x1);
            st.alphaScale = biasBits == 3 ? GX_CS_SCALE_1 : GXTevScale(scaleBits);
            st.alphaOutReg = GXTevRegID((hex >> 22) & 0x3);
        }
        break;
    }
    case 0xE0 ... 0xE7: { // TEV/Konst colors: RA and BG word pairs
        // GXTevRegID is PREV=0, REG0=1, REG1=2, REG2=3, exactly matching
        // BP pairs E0/E1 through E6/E7. Bit 23 distinguishes 8-bit Konst
        // writes from signed 11-bit TEV-register writes.
        const u32 pairIdx = (reg - 0xE0) >> 1;
        const bool isRaWord = ((reg - 0xE0) & 1) == 0;
        if (isRaWord) break; // Applied together with the BG word below.
        const u32 ra = sBpRegs[reg - 1] & 0xFFFFFF;
        const u32 bg = hex & 0xFFFFFF;
        {
            static int traced = 0;
            static const bool trace = getenv("PIKMIN_GXTRACE") != nullptr;
            if (trace && traced++ < 200) fprintf(stderr, "[GXtev] pair%u ra=%06X bg=%06X\n", pairIdx, ra, bg);
        }
        if ((ra & 0x800000) != 0 || (bg & 0x800000) != 0) {
            float* dst = sKonstColors[pairIdx];
            dst[0] = float(ra & 0xFF) / 255.0f;
            dst[1] = float((bg >> 12) & 0xFF) / 255.0f;
            dst[2] = float(bg & 0xFF) / 255.0f;
            dst[3] = float((ra >> 12) & 0xFF) / 255.0f;
        } else {
            float* dst = sTevRegisters[pairIdx];
            dst[0] = sext11_to_float(ra & 0x7FF);
            dst[1] = sext11_to_float((bg >> 12) & 0x7FF);
            dst[2] = sext11_to_float(bg & 0x7FF);
            dst[3] = sext11_to_float((ra >> 12) & 0x7FF);
        }
        break;
    }
    case 0xE8 ... 0xED: // fog range adjustment: not modelled
        break;
    case 0xEE ... 0xF1: // fog coefficients; applied when the colour arrives
        break;
    case 0xF2:
        bp_apply_fog();
        break;
    case 0xF3: { // PE alpha compare
        pc_gfx_set_alpha_compare(GXCompare((hex >> 16) & 0x7), u8(hex & 0xFF),
                                 GXAlphaOp((hex >> 22) & 0x3),
                                 GXCompare((hex >> 19) & 0x7), u8((hex >> 8) & 0xFF));
        break;
    }
    case 0xF6 ... 0xFD: { // KSEL: konst selection plus two channels of a swap table
        const u32 index = reg - 0xF6;
        const u32 evenStage = index * 2;
        if (evenStage < GX_MAXTEVSTAGE) {
            sKonstColorSel[evenStage] = GXTevKColorSel((hex >> 4) & 0x1F);
            sKonstAlphaSel[evenStage] = GXTevKAlphaSel((hex >> 9) & 0x1F);
        }
        if (evenStage + 1 < GX_MAXTEVSTAGE) {
            sKonstColorSel[evenStage + 1] = GXTevKColorSel((hex >> 14) & 0x1F);
            sKonstAlphaSel[evenStage + 1] = GXTevKAlphaSel((hex >> 19) & 0x1F);
        }
        TevSwapMode& table = sTevSwapModes[index >> 1];
        if ((index & 1) == 0) {
            table.red = GXTevColorChan(hex & 0x3);
            table.green = GXTevColorChan((hex >> 2) & 0x3);
        } else {
            table.blue = GXTevColorChan(hex & 0x3);
            table.alpha = GXTevColorChan((hex >> 2) & 0x3);
        }
        break;
    }
    case 0x06 ... 0x0E: { // IND_MTXA/B/C: una columna (s1.10) por registro
        const u32 m = (reg - 0x06) / 3, col = (reg - 0x06) % 3;
        auto s11 = [](u32 v) { return f32(s32(v << 21) >> 21) / 1024.0f; };
        state_touched();
        sIndMtx[m][0][col] = s11(hex & 0x7FF);
        sIndMtx[m][1][col] = s11((hex >> 11) & 0x7FF);
        // Los 6 bits de escala van repartidos en las tres columnas, sesgo 17.
        const u32 scaleBits = (sBpRegs[0x06 + m * 3] >> 22 & 3) | ((sBpRegs[0x07 + m * 3] >> 22 & 3) << 2)
                            | ((sBpRegs[0x08 + m * 3] >> 22 & 3) << 4);
        sIndMtxScale[m] = int(scaleBits) - 17;
        break;
    }
    case 0x10 ... 0x1F: // IND_CMD: indirecto de cada etapa TEV
        pc_gfx_set_tev_indirect(reg - 0x10, hex & 3, (hex >> 2) & 3, (hex >> 4) & 7, (hex >> 9) & 0xF,
                                (hex >> 13) & 7, (hex >> 16) & 7, (hex >> 20) & 1);
        break;
    case 0x25: case 0x26: { // RAS1_SS0/1: escala de coordenada, dos etapas por registro
        const u32 base = (reg - 0x25) * 2;
        pc_gfx_set_ind_tex_coord_scale(base, hex & 0xF, (hex >> 4) & 0xF);
        pc_gfx_set_ind_tex_coord_scale(base + 1, (hex >> 8) & 0xF, (hex >> 12) & 0xF);
        break;
    }
    case 0x27: // RAS1_IREF: textura y coordenada de cada etapa indirecta
        for (u32 n = 0; n < 4; ++n) {
            pc_gfx_set_ind_tex_order(n, (hex >> (6 * n + 3)) & 7, (hex >> (6 * n)) & 7);
        }
        break;
    case 0x0F:          // indirect mask
    case 0x30 ... 0x3F: // texcoord scale (SU): the shader works in normalised coordinates
    case 0x20 ... 0x22: // scissor / line size: driven by the GX API on this port
    case 0x45 ... 0x5F: // PE control, copy, clear: driven by the GX API on this port
    case 0x0F4: case 0x0F5: // Z texture
        break;
    default:
        gx_regs_report_once("BP", reg);
        break;
    }
}

// ─── XF registers: matrices, lights, channels, texgen ───

static u8 sXfLightRaw[8][64] = {};

static void xf_apply_texgen(u32 coord, u32 hex) {
    if (coord >= 8) return;
    const u32 proj = (hex >> 1) & 1;
    const u32 tgType = (hex >> 4) & 7;
    const u32 row = (hex >> 7) & 0x1F;
    const u32 embossRow = (hex >> 12) & 7;
    const u32 embossLit = (hex >> 15) & 7;
    GXTexGenType type = proj ? GX_TG_MTX2X4 : GX_TG_MTX3X4;
    GXTexGenSrc src = GX_TG_TEX0;
    switch (row) {
    case 0: src = GX_TG_POS; break;
    case 1: src = GX_TG_NRM; break;
    case 2: src = tgType == 3 ? GX_TG_COLOR1 : GX_TG_COLOR0; break;
    case 3: src = GX_TG_BINRM; break;
    case 4: src = GX_TG_TANGENT; break;
    default: src = GXTexGenSrc(GX_TG_TEX0 + (row >= 5 && row <= 12 ? row - 5 : 0)); break;
    }
    if (tgType == 1) {
        type = GXTexGenType(GX_TG_BUMP0 + embossLit);
        src = GXTexGenSrc(GX_TG_TEXCOORD0 + (embossRow < 7 ? embossRow : 0));
    } else if (tgType >= 2) {
        type = GX_TG_SRTG;
    }
    const u32 mtxIdx = coord < 4 ? (sCpMatIdxA >> (6 + 6 * coord)) & 0x3F : (sCpMatIdxB >> (6 * (coord - 4))) & 0x3F;
    pc_gfx_set_tex_coord_gen(GXTexCoordID(coord), type, src, mtxIdx);
}

static void handle_xf_regs(u32 addrBase, u32 numWords, const u32* words) {
    if (gx_regs_disabled()) return;
    state_touched();
    // Matrix memory: position/texture matrices share 0x000-0x3FF (row = 4
    // words, GX id = row), normal matrices 0x400-0x4FF (3 words per row),
    // post-transform texture matrices 0x500-0x5FF.
    if (addrBase < 0x400) {
        const u32 id = addrBase / 4;
        if ((addrBase % 4) != 0 || (numWords != 12 && numWords != 8) || id >= 64) {
            gx_regs_report_once("XF matrix", addrBase);
            return;
        }
        Mtx m;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 4; ++c)
                m[r][c] = (r * 4 + c < int(numWords)) ? bits_to_float(words[r * 4 + c]) : (r == c ? 1.0f : 0.0f);
        if (id >= GX_TEXMTX0) pc_gfx_load_tex_mtx(m, id);
        else pc_gfx_load_pos_mtx(m, id);
        {
            static int traced = 0;
            static const bool trace = getenv("PIKMIN_GXTRACE") != nullptr;
            if (trace && (id >= 30 ? traced < 100000 : traced++ < 60))
                fprintf(stderr, "[GXmtx] id=%u n=%u [%g %g %g %g | %g %g %g %g | %g %g %g %g]\n", id, numWords,
                        m[0][0], m[0][1], m[0][2], m[0][3], m[1][0], m[1][1], m[1][2], m[1][3], m[2][0], m[2][1], m[2][2], m[2][3]);
        }
        return;
    }
    if (addrBase < 0x500) {
        const u32 off = addrBase - 0x400;
        if ((off % 3) != 0 || numWords != 9 || off / 3 >= 64) {
            gx_regs_report_once("XF normal matrix", addrBase);
            return;
        }
        Mtx m;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) m[r][c] = bits_to_float(words[r * 3 + c]);
            m[r][3] = 0.0f;
        }
        pc_gfx_load_nrm_mtx(m, off / 3);
        return;
    }
    if (addrBase < 0x600) { // post-transform texture matrices: filas de 4 floats
        const u32 row = (addrBase - 0x500) / 4;
        if ((addrBase % 4) != 0 || (numWords % 4) != 0 || row + numWords / 4 > 64) {
            gx_regs_report_once("XF post-tex matrix", addrBase);
            return;
        }
        state_touched();
        for (u32 r = 0; r < numWords / 4; ++r)
            for (u32 c = 0; c < 4; ++c) sPostRows[row + r][c] = bits_to_float(words[r * 4 + c]);
        ++sPostMtxGen;
        return;
    }
    if (addrBase < 0x680) { // lights: 16 words each
        for (u32 w = 0; w < numWords; ++w) {
            const u32 addr = addrBase + w;
            const u32 light = (addr - 0x600) >> 4;
            const u32 field = addr & 0xF;
            if (light >= 8) break;
            u8* raw = sXfLightRaw[light] + field * 4;
            if (field == 3) { // colour: bytes r,g,b,a as GX lays them out
                raw[0] = u8(words[w] >> 24); raw[1] = u8(words[w] >> 16);
                raw[2] = u8(words[w] >> 8);  raw[3] = u8(words[w]);
            } else {
                const float f = bits_to_float(words[w]);
                memcpy(raw, &f, sizeof(f));
            }
        }
        const u32 first = (addrBase - 0x600) >> 4;
        const u32 last = (addrBase + numWords - 1 - 0x600) >> 4;
        for (u32 l = first; l <= last && l < 8; ++l) pc_gfx_load_light(sXfLightRaw[l], 1u << l);
        return;
    }
    for (u32 w = 0; w < numWords; ++w) {
        const u32 addr = addrBase + w;
        const u32 hex = words[w];
        switch (addr) {
        case 0x1008: { // INVTXSPEC: whether the normal stream carries NBT
            const u32 nrm = (hex >> 2) & 3;
            if (nrm == 2 && sVtxDesc[GX_VA_NRM] != GX_NONE) {
                pc_gfx_set_vtx_desc(GX_VA_NBT, sVtxDesc[GX_VA_NRM]);
                pc_gfx_set_vtx_desc(GX_VA_NRM, GX_NONE);
            } else if (nrm == 1 && sVtxDesc[GX_VA_NBT] != GX_NONE) {
                pc_gfx_set_vtx_desc(GX_VA_NRM, sVtxDesc[GX_VA_NBT]);
                pc_gfx_set_vtx_desc(GX_VA_NBT, GX_NONE);
            }
            break;
        }
        case 0x1009: // NUMCHAN
        case 0x103F: // NUMTEXGENS: the shader keys off the configured stages
            break;
        case 0x1018: // MATRIXINDEX copies of the CP registers
            sCpMatIdxA = hex; cp_apply_tex_mtx_indices(); break;
        case 0x1019:
            sCpMatIdxB = hex; cp_apply_tex_mtx_indices(); break;
        case GX_XF_REG_AMBIENT0: case GX_XF_REG_AMBIENT1: {
            GfxChannel& ch = sChannels[addr - GX_XF_REG_AMBIENT0];
            ch.ambColor[0] = ((hex >> 24) & 0xFF) / 255.0f;
            ch.ambColor[1] = ((hex >> 16) & 0xFF) / 255.0f;
            ch.ambColor[2] = ((hex >> 8) & 0xFF) / 255.0f;
            ch.ambColor[3] = (hex & 0xFF) / 255.0f;
            break;
        }
        case GX_XF_REG_MATERIAL0: case GX_XF_REG_MATERIAL1: {
            GfxChannel& ch = sChannels[addr - GX_XF_REG_MATERIAL0];
            ch.matColor[0] = ((hex >> 24) & 0xFF) / 255.0f;
            ch.matColor[1] = ((hex >> 16) & 0xFF) / 255.0f;
            ch.matColor[2] = ((hex >> 8) & 0xFF) / 255.0f;
            ch.matColor[3] = (hex & 0xFF) / 255.0f;
            break;
        }
        case GX_XF_REG_COLOR0CNTRL: case GX_XF_REG_COLOR1CNTRL: {
            GfxChannel& ch = sChannels[addr - GX_XF_REG_COLOR0CNTRL];
            // GXSetChanCtrl packs these XF fields as follows: material source
            // in bit 0, enable in bit 1, ambient source in bit 6, diffuse in
            // bits 7-8, attenuation in bits 9-10 and the two halves of the
            // light mask in bits 2-5 / 11-14.
            decode_xf_channel_control(ch, hex, false);
            break;
        }
        case GX_XF_REG_ALPHA0CNTRL: case GX_XF_REG_ALPHA1CNTRL: {
            GfxChannel& ch = sChannels[addr - GX_XF_REG_ALPHA0CNTRL];
            decode_xf_channel_control(ch, hex, true);
            break;
        }
        case 0x1012: // DUALTEXTRANS
        case 0x1050 ... 0x1057: // POSTMTXINFO: matriz de post-transformación y normalizar
            pc_gfx_set_tex_coord_post(addr - 0x1050, (hex & 0x3F) + 64, (hex >> 8) & 1);
            break;
        case 0x1040 ... 0x1047:
            xf_apply_texgen(addr - 0x1040, hex);
            break;
        case 0x101A ... 0x101F: // viewport
        case 0x1020 ... 0x1026: // projection: both driven through the GX API on this port
            break;
        default:
            gx_regs_report_once("XF", addr);
            break;
        }
    }
}

// Indexed XF load (opcodes 0x20/0x28/0x30/0x38): copy `size` words from the
// matching CP index array (POS/NRM/TEX matrix or light arrays) at `index`.
static void handle_indexed_xf_load(u8 command, u32 header) {
    const u32 index = header >> 16;
    const u32 size = ((header >> 12) & 0xF) + 1;
    const u32 addr = header & 0xFFF;
    const u32 slot = 12 + ((command - GX_CMD_LOAD_INDX_A) >> 3);
    const VertexArrayState& array = sVtxArrays[GX_POS_MTX_ARRAY + (slot - 12)];
    {
        static int traced = 0;
        static const bool trace = getenv("PIKMIN_GXTRACE") != nullptr;
        if (trace && traced++ < 20)
            fprintf(stderr, "[GXidx] cmd=%02X index=%u size=%u addr=%03X slot=%u base=%p stride=%u\n", command, index, size, addr, slot, (const void*)array.base, array.stride);
    }
    if (!array.base || array.stride == 0) return;
    // These arrays are matrices the game computes at runtime (J3DSys::
    // mModelDrawMtx and friends), so they are host floats, unlike the
    // big-endian file data behind the vertex arrays.
    const u8* src = array.base + size_t(index) * array.stride;
    u32 words[16];
    memcpy(words, src, (size < 16 ? size : 16) * sizeof(u32));
    handle_xf_regs(addr, size < 16 ? size : 16, words);
}

// ─── Immediate command stream (GX_WRITE_* outside a primitive) ───
// J3D writes matrix loads, matrix-index registers and array bases straight
// into the FIFO between primitives. Reassemble those commands byte by byte.

static u8 sFifoCmd[80];
static u32 sFifoCmdLen = 0;
static u32 sFifoCmdNeed = 0;

static bool gx_regs_disabled() {
    static const bool off = getenv("PIKMIN_GXREGS_OFF") != nullptr;
    return off;
}

static void fifo_cmd_byte(u8 val) {
    if (gx_regs_disabled()) return;
    if (sFifoCmdLen == 0) {
        switch (val) {
        case GX_CMD_NOP: return;
        case GX_CMD_LOAD_CP_REG: sFifoCmdNeed = 6; break;
        case GX_CMD_LOAD_XF_REG: sFifoCmdNeed = 5; break; // header first, then the words
        case GX_CMD_LOAD_BP_REG: sFifoCmdNeed = 5; break;
        case GX_CMD_LOAD_INDX_A: case GX_CMD_LOAD_INDX_B: case GX_CMD_LOAD_INDX_C: case GX_CMD_LOAD_INDX_D:
            sFifoCmdNeed = 5; break;
        case GX_CMD_CALL_DL: sFifoCmdNeed = 9; break;
        case GX_CMD_INVL_VC: return;
        default:
            // A primitive header outside GXBegin (should not happen: GXBegin
            // goes through pc_gfx_begin), or garbage. Drop the byte.
            return;
        }
    }
    sFifoCmd[sFifoCmdLen++] = val;
    if (sFifoCmdLen < sFifoCmdNeed) return;
    const u8 cmd = sFifoCmd[0];
    const u8* p = sFifoCmd + 1;
    auto be32 = [](const u8* b) { return (u32(b[0]) << 24) | (u32(b[1]) << 16) | (u32(b[2]) << 8) | b[3]; };
    {
        static int traced = 0;
        static const bool trace = getenv("PIKMIN_GXTRACE") != nullptr;
        if (trace && traced++ < 3000) {
            fprintf(stderr, "[GXfifo]");
            for (u32 i = 0; i < sFifoCmdLen && i < 24; ++i) fprintf(stderr, " %02X", sFifoCmd[i]);
            fprintf(stderr, "\n");
        }
    }
    if (cmd == GX_CMD_LOAD_XF_REG) {
        const u32 header = be32(p);
        const u32 numWords = ((header >> 16) & 0xFFFF) + 1;
        if (sFifoCmdLen == 5) {
            sFifoCmdNeed = 5 + numWords * 4;
            if (sFifoCmdNeed > sizeof(sFifoCmd)) { sFifoCmdLen = 0; sFifoCmdNeed = 0; }
            return;
        }
        u32 words[16];
        for (u32 w = 0; w < numWords && w < 16; ++w) words[w] = be32(p + 4 + w * 4);
        handle_xf_regs(header & 0xFFFF, numWords < 16 ? numWords : 16, words);
    } else if (cmd == GX_CMD_LOAD_CP_REG) {
        handle_cp_reg(p[0], be32(p + 1));
    } else if (cmd == GX_CMD_LOAD_BP_REG) {
        handle_bp_reg(be32(p));
    } else if (cmd == GX_CMD_CALL_DL) {
        void* list = pc_host_from_gc_phys(be32(p));
        if (list) pc_gfx_call_display_list(list, be32(p + 4));
    } else {
        handle_indexed_xf_load(cmd, be32(p));
    }
    sFifoCmdLen = 0;
    sFifoCmdNeed = 0;
}

static void pc_gfx_call_display_list_impl(const void* list, u32 nbytes);

static bool mesh_cache_enabled() { return sMeshDecodeCacheEnabled && sMeshArenaReady; }

// The parse inputs of `mesh` are still what they were when it was built.
static bool mesh_signature_matches(const ResidentMesh& mesh, u32 nbytes) {
    if (mesh.nbytes != nbytes) return false;
    if (memcmp(mesh.desc, sVtxDesc, sizeof(sVtxDesc)) != 0) return false;
    for (int attr = 0; attr < GX_VA_MAX_ATTR; ++attr) {
        if (sVtxDesc[attr] == GX_INDEX8 || sVtxDesc[attr] == GX_INDEX16) {
            if (mesh.arrays[attr].base != sVtxArrays[attr].base
                || mesh.arrays[attr].stride != sVtxArrays[attr].stride) return false;
        }
    }
    for (int fmt = 0; fmt < GX_MAX_VTXFMT; ++fmt) {
        if (!(mesh.fmtMask & (1u << fmt))) continue;
        if (memcmp(mesh.fmt[fmt], sVtxFormats[fmt], sizeof(sVtxFormats[fmt])) != 0) return false;
    }
    return true;
}

static void mesh_arena_reset() {
    sResidentMeshes.clear();
    if (sMeshArenaUsed == 0) return;
    sMeshArenaUsed = 0;
    ++sMeshResets;
    // Draws already queued may still read the arena: the next upload waits
    // on this fence before writing over them. Level loads can afford it.
    if (glFenceSync_ptr) {
        if (sMeshArenaResetFence && glDeleteSync_ptr) glDeleteSync_ptr(sMeshArenaResetFence);
        sMeshArenaResetFence = glFenceSync_ptr(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
}

void pc_gfx_invalidate_resident_meshes(void) {
    if (!sResidentMeshes.empty()) mesh_arena_reset();
}

void pc_gfx_invalidate_cpu_range(const void* addr, size_t bytes) {
    if (sResidentMeshes.empty() || !addr || bytes == 0) return;
    const uintptr_t lo = uintptr_t(addr);
    const uintptr_t hi = lo + bytes;
    for (auto it = sResidentMeshes.begin(); it != sResidentMeshes.end();) {
        const ResidentMesh& m = it->second;
        if (m.lo < hi && lo < m.hi) it = sResidentMeshes.erase(it);
        else ++it;
    }
    // Arena space of dropped meshes is only reclaimed by a full reset.
}

// Copies the built vertices into the arena and registers the mesh. Returns
// false (and caches nothing) when the arena is full.
static bool mesh_upload(ResidentMesh& mesh, const PcGfxHostVector<Vertex>& verts) {
    const size_t bytes = verts.size() * sizeof(Vertex);
    if (bytes == 0 || sMeshArenaUsed + bytes > sMeshArenaCapacity) return false;
    if (sMeshArenaResetFence) {
        if (glClientWaitSync_ptr) glClientWaitSync_ptr(sMeshArenaResetFence, GL_SYNC_FLUSH_COMMANDS_BIT, ~GLuint64(0));
        if (glDeleteSync_ptr) glDeleteSync_ptr(sMeshArenaResetFence);
        sMeshArenaResetFence = nullptr;
    }
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sMeshArena);
    bool uploaded = false;
    // Fresh, never-drawn range: unsynchronised mapping is safe and, on
    // Adreno, the only upload that does not stall on the buffer's other
    // ranges (see the streaming ring above).
    if (vbo_upload_by_mapping() && glMapBufferRange_ptr && glUnmapBuffer_ptr) {
        void* dst = glMapBufferRange_ptr(GL_ARRAY_BUFFER, GLintptr(sMeshArenaUsed), GLsizeiptr(bytes),
                                         GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT);
        if (dst) {
            memcpy(dst, verts.data(), bytes);
            uploaded = glUnmapBuffer_ptr(GL_ARRAY_BUFFER) == GL_TRUE;
        }
    }
    if (!uploaded) glBufferSubData_ptr(GL_ARRAY_BUFFER, GLintptr(sMeshArenaUsed), GLsizeiptr(bytes), verts.data());
    glBindBuffer_ptr(GL_ARRAY_BUFFER, sVBO);
    mesh.firstVertex = GLint(sMeshArenaUsed / sizeof(Vertex));
    mesh.vertexCount = GLsizei(verts.size());
    sMeshArenaUsed += bytes;
    sResidentMeshes[mesh.list] = mesh;
    return true;
}

// Draws a cached list with the current material state. The streaming batch
// is flushed first (it was built for earlier state), then the state is
// programmed exactly as pc_gfx_end would, and the mesh is drawn from the
// arena. Skinned meshes use the palette; static ones the current matrix.
static void draw_resident_mesh(ResidentMesh& mesh) {
    const bool profiling = pc_tick_profiler_enabled();
    const double t0 = profiling ? submit_clock_ms() : 0.0;
    pc_gfx_flush_batch();
    const double t1 = profiling ? submit_clock_ms() : 0.0;
    sVerticesPretransformed = false;
    sVertexUsesPalette = mesh.skinned;
    sPaletteSlotsNeeded = mesh.paletteSlots;
    apply_draw_state(profiling, t1);
    const double t2 = profiling ? submit_clock_ms() : 0.0;
    if (profiling) sSubmitUniformMs += t2 - t1;
    glBindVertexArray_ptr(sMeshVAO);
    shadow_record_resident(mesh.firstVertex, mesh.vertexCount);
    glDrawArrays(GL_TRIANGLES, mesh.firstVertex, mesh.vertexCount);
    glBindVertexArray_ptr(GLuint(sStreamVAO));
    if (profiling) {
        sSubmitDrawMs += submit_clock_ms() - t2;
        ++sSubmitDraws;
        sSubmitVerts += uint64_t(mesh.vertexCount);
    }
    ++sPerfDraws;
    sPerfVertices += uint64_t(mesh.vertexCount);
    ++sMeshDrawsThisFrame;
    sMeshVertsThisFrame += uint64_t(mesh.vertexCount);
    mesh.lastUsedFrame = sFrameSerial;
    sVertexUsesPalette = false;
    sPaletteSlotsNeeded = kPaletteSlots;
    (void)t0;
}

void pc_gfx_call_display_list(const void* list, u32 nbytes) {
    sNativeWrittenThisFrame = true;
    sTexBakedMask = 0;
    if (sInPrimitive) pc_gfx_end(); // an immediate primitive left open without GXEnd
    // Two clock reads per display list (~1000 a frame): cheap enough, and it
    // is the one cost of renderall that nothing else was attributing.
    if (!pc_tick_profiler_enabled()) {
        pc_gfx_call_display_list_impl(list, nbytes);
        return;
    }
    const double t0 = submit_clock_ms();
    const double submitBefore = sSubmitUniformMs + sSubmitVboMs + sSubmitDrawMs;
    pc_gfx_call_display_list_impl(list, nbytes);
    const double submitAfter = sSubmitUniformMs + sSubmitVboMs + sSubmitDrawMs;
    sSubmitDlMs += (submit_clock_ms() - t0) - (submitAfter - submitBefore);
}

static void pc_gfx_call_display_list_impl(const void* list, u32 nbytes) {
    // On its own switch rather than the tick profiler's: tying it to
    // PIKMIN_TICK_STATS would put the cost back exactly when measuring, which
    // is the one time it must not be there. Shares the switch with the vertex
    // descriptor history, which exists only to be printed by this report.
    const bool profilingWildVerts = pc_gfx_gx_diagnostics_enabled();
    static bool reportedUnsupportedCommand = false;
    static bool reportedMalformedVertex = false;
    static bool reportedInvalidMatrix = false;
    if (!list || nbytes == 0) return;
    if (pc_gfx_trace_j3d()) fprintf(stderr, "[J3Dtrace] callDL %p %u bytes first=%02X\n", list, nbytes, *(const u8*)list);

    // Capture display list if capture is active
    if (sCaptureActive) {
        PcGfxHostAllocScope hostAllocScope;
        sPacketStore.captureDisplayList(list, nbytes);
    }

    // Resident mesh already built for this list and these inputs: draw it.
    // Listas cuyos arrays de vertices cambian una y otra vez (J3D deforma en
    // CPU con doble buffer): se reconstruian en cada tick y en los frames
    // intermedios se dibujaban por la ruta de la cache, con otra pose (Louie
    // duplicado en los cinematicos). Tras unos cambios se quedan en la ruta CPU.
    static PcGfxHostMap<const void*, u8> sMeshRebuilds;
    bool cacheCandidate = mesh_cache_enabled();
    if (cacheCandidate) {
        auto rebuilt = sMeshRebuilds.find(list);
        if (rebuilt != sMeshRebuilds.end() && rebuilt->second >= 3) cacheCandidate = false;
    }
    if (cacheCandidate) {
        auto found = sResidentMeshes.find(list);
        if (found != sResidentMeshes.end()) {
            if (mesh_signature_matches(found->second, nbytes)) {
                bp_resolve_textures(); // material registers may have changed since the mesh was built
                draw_resident_mesh(found->second);
                return;
            }
            sResidentMeshes.erase(found);
            u8& n = sMeshRebuilds[list];
            if (n < 255) ++n;
            if (n >= 3) cacheCandidate = false;
        }
    }
    // First sight of the list: draw it through the CPU path as always and,
    // in parallel, collect the model-space vertices for the arena. Anything
    // the parser meets that a static mesh cannot have turns `building` off.
    bool building = cacheCandidate && sMeshArenaUsed < sMeshArenaCapacity;
    ResidentMesh mesh;
    if (building) {
        mesh.list = list;
        mesh.nbytes = nbytes;
        mesh.skinned = sVtxDesc[GX_VA_PNMTXIDX] != GX_NONE;
        mesh.lo = uintptr_t(list);
        mesh.hi = uintptr_t(list) + nbytes;
        memcpy(mesh.desc, sVtxDesc, sizeof(sVtxDesc));
        memcpy(mesh.arrays, sVtxArrays, sizeof(sVtxArrays));
        sMeshBuild.clear();
        sMeshPrimModel.clear();
    }
    int meshMaxSlot = 0;
    auto meshNoteRange = [&](const void* lo, size_t bytes) {
        const uintptr_t a = uintptr_t(lo);
        if (a < mesh.lo) mesh.lo = a;
        if (a + bytes > mesh.hi) mesh.hi = a + bytes;
    };

    ++sPerfDisplayLists;
    sPerfDisplayListBytes += nbytes;
    const u8* cursor = static_cast<const u8*>(list);
    const u8* end = cursor + nbytes;
    bool pendingTriangleStrip = false;
    auto flushPendingStrip = [&]() {
        if (pendingTriangleStrip) {
            if (building) append_primitive(sMeshPrimModel, sCurrentPrimType, sMeshBuild);
            sMeshPrimModel.clear();
            pc_gfx_end();
            pendingTriangleStrip = false;
        }
    };

    {
        static int traced = 0;
        static const bool trace = getenv("PIKMIN_GXTRACE") != nullptr;
        if (trace && traced++ < 400) {
            fprintf(stderr, "[GXdl] %p %u bytes:", list, nbytes);
            for (u32 i = 0; i < nbytes && i < (nbytes == 192 ? 192u : 48u); ++i) fprintf(stderr, " %02X", cursor[i]);
            fprintf(stderr, "\n");
        }
    }
    while (cursor < end) {
        const u8 command = *cursor++;
        if (command == 0) continue; // 32-byte display-list padding.
        if (command == GX_CMD_NOP) continue;

        // ── Non-draw GP commands: interpret and keep parsing ──
        if (command == GX_CMD_LOAD_BP_REG) {
            building = false;
            flushPendingStrip();
            u32 hex;
            if (!read_be_u32(cursor, end, hex)) break;
            if (pc_gfx_trace_j3d()) {
                const u32 reg = hex >> 24;
                if (reg == 0x00 || (reg >= 0x28 && reg <= 0x2F) || (reg >= 0xC0 && reg <= 0xCF) || (reg >= 0x94 && reg <= 0x97))
                    fprintf(stderr, "[J3Dtrace]   dl=%p BP %02X=%06X\n", list, reg, hex & 0xFFFFFF);
            }
            handle_bp_reg(hex);
            continue;
        }
        if (command == GX_CMD_LOAD_XF_REG) {
            building = false;
            flushPendingStrip();
            u32 header;
            if (!read_be_u32(cursor, end, header)) break;
            const u32 numWords = ((header >> 16) & 0xFFFF) + 1;
            const u32 addrBase = header & 0xFFFF;
#ifdef PC_GFX_TRACE
            {
                static FILE* xf = nullptr;
                if (!xf) xf = fopen("/tmp/opencode/xfdl.log", "w");
                if (xf) {
                    fprintf(xf, "XF num=%u addr=%04X w0=%08X w1=%08X\n", numWords, addrBase,
                            cursor[0] << 24 | cursor[1] << 16 | cursor[2] << 8 | cursor[3],
                            cursor[4] << 24 | cursor[5] << 16 | cursor[6] << 8 | cursor[7]);
                    fflush(xf);
                }
            }
#endif
            if (end - cursor < ptrdiff_t(numWords * 4)) break;
            {
                u32 words[64];
                const u32 n = numWords < 64 ? numWords : 64;
                for (u32 w = 0; w < n; ++w) {
                    const u8* b = cursor + w * 4;
                    words[w] = (u32(b[0]) << 24) | (u32(b[1]) << 16) | (u32(b[2]) << 8) | b[3];
                }
                handle_xf_regs(addrBase, n, words);
            }
            cursor += numWords * 4;
            continue;
        }
        if (command == GX_CMD_CALL_DL) {
            building = false;
            flushPendingStrip();
            u32 addr, size;
            if (!read_be_u32(cursor, end, addr) || !read_be_u32(cursor, end, size)) break;
            if (void* nested = pc_host_from_gc_phys(addr)) pc_gfx_call_display_list(nested, size);
            continue;
        }
        if (command == GX_CMD_INVL_VC) continue;
        if (command == GX_CMD_LOAD_CP_REG) {
            building = false;
            flushPendingStrip();
            if (end - cursor < 5) break;
            const u8 addr = *cursor++;
            u32 value;
            if (!read_be_u32(cursor, end, value)) break;
            handle_cp_reg(addr, value);
            continue;
        }
        if (command >= GX_CMD_LOAD_INDX_A && command <= GX_CMD_LOAD_INDX_D + 0x07) {
            building = false;
            flushPendingStrip();
            u32 header;
            if (!read_be_u32(cursor, end, header)) break;
            handle_indexed_xf_load(u8(command & 0xF8), header);
            continue;
        }

        const u8 opcode = command & GX_OPCODE_MASK;
        const u8 format = command & GX_VAT_MASK;
        if (opcode < GX_CMD_DRAW_QUADS || opcode > GX_CMD_DRAW_POINTS) {
            ++sDlDesyncs;
            report_desync("unsupported opcode", list,
                          size_t(cursor - static_cast<const u8*>(list) - 1), nbytes);
            flushPendingStrip();
            if (!reportedUnsupportedCommand) {
                fprintf(stderr, "[PC GX] Unsupported display-list command 0x%02X at byte %zu/%u\n",
                        command, size_t(cursor - static_cast<const u8*>(list) - 1), nbytes);
                reportedUnsupportedCommand = true;
            }
            break;
        }

        u16 vertexCount = 0;
        if (!read_be_u16(cursor, end, vertexCount)) break;
        ++sPerfSourcePrimitives;
        const bool continueTriangleStrip = opcode == GX_TRIANGLESTRIP && pendingTriangleStrip;
        if (opcode == GX_LINES || opcode == GX_LINESTRIP || opcode == GX_POINTS) building = false;
        if (building) {
            mesh.fmtMask |= u8(1u << format);
            memcpy(mesh.fmt[format], sVtxFormats[format], sizeof(sVtxFormats[format]));
        }
        if (!continueTriangleStrip) {
            flushPendingStrip();
            pc_gfx_begin(static_cast<GXPrimitive>(opcode), static_cast<GXVtxFmt>(format), vertexCount);
            sMeshPrimModel.clear();
        } else {
            // Join independent strips using degenerate triangles. An extra
            // duplicate for odd-length strips preserves the winding parity of
            // the first real triangle in the following strip.
            const Vertex last = sVertexStream.back();
            if (sVertexStream.size() & 1) sVertexStream.push_back(last);
            sVertexStream.push_back(last);
            sVertexStream.reserve(sVertexStream.size() + vertexCount + 1);
            // The joined stream is one primitive as far as pc_gfx_end is
            // concerned: grow its expected count by the bridge and the new
            // strip (plus the duplicated first vertex), or it gets truncated
            // back to the first strip.
            sExpectedVerts = sVertexStream.size() + vertexCount + 1;
            if (building && !sMeshPrimModel.empty()) {
                const Vertex lastModel = sMeshPrimModel.back();
                if (sMeshPrimModel.size() & 1) sMeshPrimModel.push_back(lastModel);
                sMeshPrimModel.push_back(lastModel);
            }
        }
        sVerticesPretransformed = true;

        bool malformed = false;
        for (u16 vertex = 0; vertex < vertexCount && !malformed; ++vertex) {
            const u8* const vertexStart = cursor;
            Vertex v;
            v.x = 0.0f; v.y = 0.0f; v.z = 0.0f;
            v.nx = 0.0f; v.ny = 0.0f; v.nz = 1.0f;
            v.r = 1.0f; v.g = 1.0f; v.b = 1.0f; v.a = 1.0f;
            v.matrixSlot = 0.0f;
            v.texMtxPacked = 0.0f;
            for (int tc = 0; tc < 4; ++tc) {
                v.tex[tc][0] = 0.0f;
                v.tex[tc][1] = 0.0f;
            }
            u8 matrixId = static_cast<u8>(sCurrentPosMtxId);
            float rawX = 0.0f, rawY = 0.0f, rawZ = 0.0f;

            for (int attrNumber = GX_VA_PNMTXIDX; attrNumber <= GX_VA_TEX7; ++attrNumber) {
                GXAttr attr = static_cast<GXAttr>(attrNumber);
                GXAttrType desc = sVtxDesc[attr];
                if (desc == GX_NONE) {
                    // NBT replaces the normal attribute in the hardware stream.
                    if (attr == GX_VA_NRM && sVtxDesc[GX_VA_NBT] != GX_NONE) {
                        attr = GX_VA_NBT;
                        desc = sVtxDesc[attr];
                    } else {
                        continue;
                    }
                }

                if (attr <= GX_VA_TEX7MTXIDX) {
                    if (desc != GX_DIRECT || cursor >= end) { malformed = true; break; }
                    const u8 value = *cursor++;
                    if (attr >= GX_VA_TEX0MTXIDX && attr <= GX_VA_TEX2MTXIDX && value < 64) {
                        // Matriz de textura elegida por el vértice (reflejo de
                        // los modelos con esqueleto, p. ej. el casco).
                        static const float kPack[3] = { 1.0f, 128.0f, 16384.0f };
                        v.texMtxPacked += float(value + 1) * kPack[attr - GX_VA_TEX0MTXIDX];
                    }
                    if (attr == GX_VA_PNMTXIDX) {
                        matrixId = value;
                        v.matrixSlot = float(matrixId / 3);
                        if (matrixId < 64) sPerfPnMtxMask |= uint64_t(1) << matrixId;
                        sPerfPnMtxMax = std::max(sPerfPnMtxMax, matrixId);
                        if (matrixId >= 64) ++sBadMtxIdx;
                        if (matrixId >= 64 && !reportedInvalidMatrix) {
                            fprintf(stderr, "[PC GX] Invalid PNMTXIDX %u in display list\n", matrixId);
                            reportedInvalidMatrix = true;
                        }
                    }
                    continue;
                }

                const u8* element = nullptr;
                if (desc == GX_DIRECT) {
                    // Inline vertex data embedded in the display list itself.
                    const u8 inlineSize = attr_inline_size(attr, sVtxFormats[format][attr]);
                    if (inlineSize == 0 || end - cursor < inlineSize) { malformed = true; break; }
                    element = cursor;
                    cursor += inlineSize;
                } else {
                    u16 index = 0;
                    if (!read_attribute_index(desc, cursor, end, index)) {
                        malformed = true;
                        break;
                    }
                    const VertexArrayState& array = sVtxArrays[attr];
                    if (!array.base || array.stride == 0) continue;
                    element = array.base + size_t(index) * array.stride;
                    if (building) meshNoteRange(element, array.stride);
                    // Only the wild-vertex report below reads these, and this
                    // is the innermost loop in the whole renderer: three stores
                    // per vertex, hundreds of thousands of vertices a frame.
                    if (profilingWildVerts && attr == GX_VA_POS) {
                        sLastPosIndex  = index;
                        sLastPosBase   = array.base;
                        sLastPosStride = array.stride;
                    }
                }

                if (attr == GX_VA_POS) {
                    const VertexFormatState& fmtState = sVtxFormats[format][attr];
                    u8 compSize = get_comptype_size(fmtState.type);
                    float x = read_attr_float(element, fmtState.type, fmtState.frac);
                    float y = read_attr_float(element + compSize, fmtState.type, fmtState.frac);
                    float z = (fmtState.count == GX_POS_XYZ) ? read_attr_float(element + 2 * compSize, fmtState.type, fmtState.frac) : 0.0f;
                    
                    rawX = x; rawY = y; rawZ = z;
                    if (sVertexUsesPalette) { v.x = x; v.y = y; v.z = z; }
                    else transform_position(matrixId, x, y, z, v.x, v.y, v.z);
                    // The map is a few thousand units across; anything beyond
                    // this, or non-finite, did not come from real model data.
                    // Diagnostic, and it runs per vertex, so it must not run at
                    // all unless asked for: left ungated it cost the title
                    // screen more than half its frame rate.
                    if (profilingWildVerts
                        && (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)
                            || fabsf(v.x) > 1.0e6f || fabsf(v.y) > 1.0e6f || fabsf(v.z) > 1.0e6f)) {
                        ++sWildVerts;
                        // Separate the two possible sources: the model data we
                        // read, or the matrix we multiplied it by. Printing
                        // both says which one is garbage without guessing.
                        static int reports = 0;
                        if (reports < 6) {
                            ++reports;
                            const float* m = sPosMatrix[matrixId < 64 ? matrixId : 0];
                            fprintf(stderr,
                                    "[PC GX] WILD #%d mtx=%u in=(%g,%g,%g) out=(%g,%g,%g)\n",
                                    reports, unsigned(matrixId), x, y, z, v.x, v.y, v.z);
                            (void)m;
                            const VertexFormatState& pf = sVtxFormats[format][GX_VA_POS];
                            fprintf(stderr,
                                    "[PC GX] WILD #%d pos idx=%u base=%p stride=%u -> elem=%p (offset %zu)  fmt: count=%d type=%d frac=%u\n",
                                    reports, unsigned(sLastPosIndex), (const void*)sLastPosBase,
                                    unsigned(sLastPosStride),
                                    (const void*)(sLastPosBase + size_t(sLastPosIndex) * sLastPosStride),
                                    size_t(sLastPosIndex) * sLastPosStride,
                                    int(pf.count), int(pf.type), unsigned(pf.frac));
                            fprintf(stderr,
                                    "[PC GX] WILD #%d POS array last set on frame %llu (now %llu, %llu frames ago); set #%llu of %llu total\n",
                                    reports,
                                    (unsigned long long)sArraySetFrame[GX_VA_POS],
                                    (unsigned long long)sFrameSerial,
                                    (unsigned long long)(sFrameSerial - sArraySetFrame[GX_VA_POS]),
                                    (unsigned long long)sArraySetSerial[GX_VA_POS],
                                    (unsigned long long)sArraySetCounter);
                            // The array and the format are right, so the
                            // remaining suspect is the layout we are stepping
                            // through. Print the descriptor and the actual
                            // stream bytes for this vertex so the true stride
                            // can be read off by hand.
                            fprintf(stderr, "[PC GX] WILD #%d vtxdesc:", reports);
                            unsigned wstride = 0;
                            for (int a = GX_VA_PNMTXIDX; a <= GX_VA_TEX7; ++a) {
                                const GXAttrType d = sVtxDesc[a];
                                if (d == GX_NONE) continue;
                                const char* kind = d == GX_DIRECT ? "direct" : d == GX_INDEX8 ? "idx8"
                                                 : d == GX_INDEX16 ? "idx16" : "?";
                                unsigned bytes = d == GX_DIRECT
                                    ? attr_inline_size(static_cast<GXAttr>(a), sVtxFormats[format][a])
                                    : (d == GX_INDEX8 ? 1u : 2u);
                                wstride += bytes;
                                fprintf(stderr, " a%d=%s(%u)", a, kind, bytes);
                            }
                            fprintf(stderr, "  stride=%u  vat=%u  vert %u/%u at list byte %zu/%u\n",
                                    wstride, unsigned(format), unsigned(vertex), unsigned(vertexCount),
                                    size_t(vertexStart - static_cast<const u8*>(list)), nbytes);
                            fprintf(stderr, "[PC GX] WILD #%d stream:", reports);
                            for (int b = 0; b < 20 && vertexStart + b < end; ++b) {
                                fprintf(stderr, " %02X", vertexStart[b]);
                            }
                            fprintf(stderr, "\n");
                            if (reports == 1) {
                                pc_gfx_dump_vtx_desc_history("first wild vertex");
                                // The same list parses correctly at 30 Hz, so
                                // the descriptor is not wrong for the mesh --
                                // this list is reaching us from a draw path
                                // that never programmed one. Name that path
                                // instead of inferring it.
#if defined(_WIN32) || defined(__ANDROID__)
                                fprintf(stderr, "[PC GX] wild draw call path: "
                                                "unavailable on this platform\n");
#else
                                void* frames[16];
                                const int n = backtrace(frames, 16);
                                char** names = backtrace_symbols(frames, n);
                                fprintf(stderr, "[PC GX] wild draw call path:\n");
                                for (int f = 0; f < n; ++f) {
                                    fprintf(stderr, "    %s\n", names ? names[f] : "?");
                                }
                                free(names);
#endif
                            }
                            fprintf(stderr, "[PC GX] WILD #%d raw:", reports);
                            for (int b = 0; b < 12; ++b) fprintf(stderr, " %02X", element[b]);
                            fprintf(stderr, "\n");
                        }
                    }
                } else if (attr == GX_VA_CLR0 || attr == GX_VA_CLR1) {
                    const VertexFormatState& fmtState = sVtxFormats[format][attr];
                    u8 r, g, b, a;
                    read_attr_color(element, fmtState.type, r, g, b, a);
                    if (attr == GX_VA_CLR0) {
                        v.r = r / 255.0f;
                        v.g = g / 255.0f;
                        v.b = b / 255.0f;
                        v.a = a / 255.0f;
                    }
                } else if (attr == GX_VA_NRM || attr == GX_VA_NBT) {
                    const VertexFormatState& fmtState = sVtxFormats[format][attr];
                    u8 compSize = get_comptype_size(fmtState.type);
                    // Every normal encoding (XYZ, NBT, NBT3) stores the normal
                    // vector in the first three slots; NBT/NBT3 just append
                    // binormal/tangent data we can safely ignore.
                    v.nx = read_attr_float(element, fmtState.type, fmtState.frac);
                    v.ny = read_attr_float(element + compSize, fmtState.type, fmtState.frac);
                    v.nz = read_attr_float(element + 2 * compSize, fmtState.type, fmtState.frac);
                } else if (attr >= GX_VA_TEX0 && attr <= GX_VA_TEX7) {
                    const VertexFormatState& fmtState = sVtxFormats[format][attr];
                    u8 compSize = get_comptype_size(fmtState.type);
                    const int tc = int(attr) - int(GX_VA_TEX0);
                    // The parser must consume all eight GX attributes, while
                    // the current shader backend only exposes TEX0-TEX3.
                    if (tc < 4) {
                        v.tex[tc][0] = read_attr_float(element, fmtState.type, fmtState.frac);
                        v.tex[tc][1] = (fmtState.count == GX_TEX_ST)
                            ? read_attr_float(element + compSize, fmtState.type, fmtState.frac) : 0.0f;
                    }
                }
            }

            // Display-list vertices are transformed to view space on the CPU
            // so that a primitive can contain several PNMTXIDX values.  Their
            // normals must follow the same per-vertex matrix; applying one
            // global normal matrix later gives skinned joints unrelated light
            // directions (notably Olimar/Pikmin heads and articulated ships).
            if (building) {
                // Model-space twin of v for the arena, before any transform.
                // The palette indexes slots at id = slot*3; a list using any
                // other id keeps going through the CPU path.
                if (matrixId % 3 != 0 || matrixId >= 63) {
                    building = false;
                } else {
                    Vertex vm = v;
                    vm.x = rawX; vm.y = rawY; vm.z = rawZ;
                    vm.matrixSlot = float(matrixId / 3);
                    if (int(matrixId / 3) > meshMaxSlot) meshMaxSlot = int(matrixId / 3);
                    if (continueTriangleStrip && vertex == 0) sMeshPrimModel.push_back(vm);
                    sMeshPrimModel.push_back(vm);
                }
            }
            if (!sVertexUsesPalette) {
                // Reflejos (fuente NRM): en la consola la generación recibe la
                // normal original y la multiplica una vez por su matriz de
                // textura. Aquí la normal se transforma a vista justo debajo, y
                // multiplicarla otra vez en el shader la rotaba dos veces (el
                // casco de los capitanes, con franjas en vez de un brillo).
                for (int slot = 0; slot < 4; ++slot) {
                    const TexCoordGen& tg = sTexCoordGen[slot];
                    if (!tg.active || tg.src != GX_TG_NRM || tg.type >= GX_TG_BUMP0) continue;
                    const int packed = int(v.texMtxPacked);
                    const int perVertex = slot < 3 ? ((packed >> (7 * slot)) & 0x7F) - 1 : -1;
                    const u32 mid = (perVertex >= 0 && perVertex < 64) ? u32(perVertex) : tg.mtxIdx;
                    const float* m = sTexMatrices[mid < 64 ? mid : 0];
                    float r[3];
                    for (int row = 0; row < 3; ++row)
                        r[row] = m[row] * v.nx + m[4 + row] * v.ny + m[8 + row] * v.nz + m[12 + row];
                    if (tg.type == GX_TG_MTX3X4) r[2] = 1.0f; // este enum: MTX3X4 = 2x4 del hardware
                    if (tg.normalize) {
                        const float len = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
                        if (len > 1e-6f) { r[0] /= len; r[1] /= len; r[2] /= len; }
                    }
                    const u32 prow = (tg.postIdx >= 64 && tg.postIdx <= 125) ? tg.postIdx - 64 : 61;
                    float out[2];
                    for (int k = 0; k < 2; ++k) {
                        const float* pr = sPostRows[std::min<u32>(prow + k, 63)];
                        out[k] = pr[0] * r[0] + pr[1] * r[1] + pr[2] * r[2] + pr[3];
                    }
                    v.tex[slot][0] = out[0];
                    v.tex[slot][1] = out[1];
                    sTexBakedMask |= 1 << slot;
                }
                float nx, ny, nz;
                transform_normal(matrixId, v.nx, v.ny, v.nz, nx, ny, nz);
                v.nx = nx; v.ny = ny; v.nz = nz;
            }

            if (continueTriangleStrip && vertex == 0) sVertexStream.push_back(v);
            sVertexStream.push_back(v);
        }

        if (malformed) {
            building = false;
            ++sDlDesyncs;
            report_desync("truncated vertex", list,
                          size_t(cursor - static_cast<const u8*>(list)), nbytes);
            if (!reportedMalformedVertex) {
                fprintf(stderr, "[PC GX] Malformed vertex stream at byte %zu/%u (format %u)\n",
                        size_t(cursor - static_cast<const u8*>(list)), nbytes, format);
                reportedMalformedVertex = true;
            }
            sInPrimitive = false;
            sVertexStream.clear();
            pendingTriangleStrip = false;
            break;
        }
        if (opcode == GX_TRIANGLESTRIP) pendingTriangleStrip = true;
        else {
            if (building) append_primitive(sMeshPrimModel, sCurrentPrimType, sMeshBuild);
            sMeshPrimModel.clear();
            pc_gfx_end();
        }
    }
    flushPendingStrip();
    if (building && !sMeshBuild.empty()) {
        mesh.paletteSlots = meshMaxSlot + 1;
        const double uploadT0 = submit_clock_ms();
        mesh_upload(mesh, sMeshBuild);
        sMeshBuildMsThisFrame += submit_clock_ms() - uploadT0;
        ++sMeshBuildsThisFrame;
        sMeshBuildBytesThisFrame += uint32_t(sMeshBuild.size() * sizeof(Vertex));
    }
    sMeshBuild.clear();
    sMeshPrimModel.clear();
}

void pc_gfx_copy_disp(void* dest, GXBool clear) {
    pc_gfx_note_gl_state_change();
    (void)dest;
    /*
     * GXCopyDisp = EFB→XFB then optional EFB clear. On PC the FBO is the EFB
     * and there is no XFB; System::endFrame presents via VIWaitForRetrace.
     * Only clear here so we do not discard the just-drawn frame before present.
     */
    if (clear) {
        const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(sCopyClearColor[0], sCopyClearColor[1], sCopyClearColor[2], sCopyClearColor[3]);
#if PIKI_USE_GLES
        glClearDepthf(sCopyClearDepth);
#else
        glClearDepth(sCopyClearDepth);
#endif
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST);
    }
}

static u16 sTexCopySrc[4] = { 0, 0, 640, 480 };
static u16 sTexCopyDstW = 640, sTexCopyDstH = 480;
static u32 sTexCopyFmt = GX_TF_RGBA8;
void pc_gfx_set_tex_copy_src(u16 left, u16 top, u16 wd, u16 ht) {
    sTexCopySrc[0] = left; sTexCopySrc[1] = top; sTexCopySrc[2] = wd; sTexCopySrc[3] = ht;
}
void pc_gfx_set_tex_copy_dst(u16 wd, u16 ht, u32 fmt) {
    sTexCopyDstW = wd; sTexCopyDstH = ht; sTexCopyFmt = fmt;
}

// GXCopyTex: el EFB (nuestro framebuffer nativo) a una textura GL, en GPU.
// La textura queda asociada a la direccion de destino y pc_gfx_load_tex_obj la
// usa cuando un GXTexObj apunta a esa imagen (JUTTexture::capture: fondos
// capturados, pantalla de resultados, transiciones).
void pc_gfx_copy_tex(void* dest, GXBool clear) {
    // PIKMIN_NO_EFB_COPY=1: sin copia (diagnostico de fantasmas en cinematicas).
    static const bool disabled = getenv("PIKMIN_NO_EFB_COPY") != nullptr;
    if (disabled) return;
    if (!dest || !sNativeFramebufferReady || !glBlitFramebuffer_ptr || !glGenFramebuffers_ptr) return;
    const int w = sTexCopyDstW, h = sTexCopyDstH;
    if (w <= 0 || h <= 0) return;
    pc_gfx_flush_batch();
    pc_gfx_note_gl_state_change();

    PcEfbCopy& copy = sEfbCopies[reinterpret_cast<uintptr_t>(dest)];
    if (!copy.tex || copy.w != w || copy.h != h) {
        if (!copy.tex) glGenTextures(1, &copy.tex);
        glBindTexture(GL_TEXTURE_2D, copy.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        copy.w = w; copy.h = h;
    } else {
        glBindTexture(GL_TEXTURE_2D, copy.tex);
    }
    // Formatos de intensidad/un canal: se replica en RGBA como hace el decodificador.
    const u32 f = sTexCopyFmt;
    const bool intensity = f == GX_TF_I4 || f == GX_TF_I8 || f == GX_CTF_R4 || f == GX_CTF_R8
                        || f == GX_CTF_RA4 || f == GX_CTF_RA8 || f == GX_TF_IA4 || f == GX_TF_IA8;
    const bool opaque = f == GX_TF_RGB565;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, intensity ? GL_RED : GL_GREEN);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, intensity ? GL_RED : GL_BLUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, intensity ? GL_RED : (opaque ? GL_ONE : GL_ALPHA));
    glBindTexture(GL_TEXTURE_2D, 0);
    sBoundTextures[0] = 0;

    static GLuint copyFbo = 0;
    if (!copyFbo) glGenFramebuffers_ptr(1, &copyFbo);
    glBindFramebuffer_ptr(GL_DRAW_FRAMEBUFFER, copyFbo);
    glFramebufferTexture2D_ptr(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, copy.tex, 0);
    glBindFramebuffer_ptr(GL_READ_FRAMEBUFFER, sNativeFramebuffer);

    GLint sx, sy; GLsizei sw, sh;
    map_gx_rect(float(sTexCopySrc[0]), float(sTexCopySrc[1]), float(sTexCopySrc[2]), float(sTexCopySrc[3]), sx, sy, sw, sh);
    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    // Fila 0 de una textura GX es la de arriba; en GL el framebuffer empieza abajo.
    glBlitFramebuffer_ptr(sx, sy, sx + sw, sy + sh, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    glBindFramebuffer_ptr(GL_FRAMEBUFFER, sNativeFramebuffer);

    if (clear) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx, sy, sw, sh);
        glClearColor(sCopyClearColor[0], sCopyClearColor[1], sCopyClearColor[2], sCopyClearColor[3]);
#if PIKI_USE_GLES
        glClearDepthf(sCopyClearDepth);
#else
        glClearDepth(sCopyClearDepth);
#endif
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        invalidate_gl_pipeline_guards();
        pc_gfx_set_scissor(sGxSc[0], sGxSc[1], sGxSc[2], sGxSc[3]);
    }
    if (scissorWasEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
}

void pc_gfx_set_copy_clear(GXColor color, u32 clearZ) {
    sCopyClearColor[0] = color.r / 255.0f;
    sCopyClearColor[1] = color.g / 255.0f;
    sCopyClearColor[2] = color.b / 255.0f;
    sCopyClearColor[3] = color.a / 255.0f;
    sCopyClearDepth = (clearZ & 0x00ffffffu) / 16777215.0;
    // Keep the replay clear in sync with the authoritative clear color.
    sReplayClearColor[0] = sCopyClearColor[0];
    sReplayClearColor[1] = sCopyClearColor[1];
    sReplayClearColor[2] = sCopyClearColor[2];
    sReplayClearColor[3] = sCopyClearColor[3];
    sReplayClearDepth    = sCopyClearDepth;
}
