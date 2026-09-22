# macOS port

This fork adds macOS (Apple Silicon) support to Open Nectar. Upstream targets
Linux, Windows and Android; nothing here changes how those build.

Developed and tested on an M3 Pro running macOS with Xcode's toolchain and
Homebrew SDL2, against a USA disc image.

## Requirements

- Xcode or the Command Line Tools (provides AppleClang and the macOS SDK)
- CMake 3.16 or newer
- SDL2 and pkg-config: `brew install sdl2 pkg-config cmake`

`iconv` and OpenGL come from the system; nothing else is needed.

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(sysctl -n hw.ncpu)"
./build/bin/nectar-launcher
```

The launcher extracts the game data from your own disc image and then starts
the game. Run `nectar` directly only from the install directory: asset paths
resolve relative to the working directory, and a missing font asset will
crash on startup rather than degrade.

## What needed changing, and why

### Build system

CMake reports Xcode's toolchain as `AppleClang`, which is a distinct compiler
ID from `Clang`. The existing `$<CXX_COMPILER_ID:Clang>` generator expressions
match exactly, so on macOS the `-Wno-register` relaxation never applied and
every `register` in the decompiled Dolphin headers became a hard error under
C++17. The same trap silently dropped `-O3` from release builds. Both now list
`AppleClang` explicitly.

macOS has no X11, and the port's only X11 use is already guarded to Linux, so
the link line no longer pulls it in there. The disc-extraction library links
`iconv`, which on macOS lives outside libSystem.

### OpenGL headers

macOS keeps the desktop GL headers in OpenGL.framework as `<OpenGL/gl.h>` and
ships no `<GL/gl.h>`. Spelling it `<GL/gl.h>` did not merely fail to find the
system header: the include path carries `-I<repo>/pc_port`, and macOS's
filesystem is case-insensitive, so `<GL/gl.h>` matched the repository's own
`pc_port/gl/gl.h`, a wrapper that includes `pc_opengl.h` straight back. Its
guard was already defined, so it expanded to nothing, no error was raised, and
every GL type stayed undefined until a later header tripped over `GLint`.

Apple also uses its own function-pointer naming (`glGenBuffersProcPtr`), with
no `PFNGL*PROC` typedefs anywhere, so all 60 the port uses are declared in
`pc_opengl.h`, generated from Apple's own declarations so the signatures match
the SDK. `GL_COMPARE_REF_TO_TEXTURE` is aliased to the pre-3.0 spelling Apple
still carries.

### Rendering

macOS offers no Compatibility profile, only Legacy 2.1 or Core 3.2+, and the
backend's `#version 140` shaders need a Core context, so one is requested up
front rather than after a failed attempt.

Core profile forbids vertex specification and draws with vertex array object 0
bound, which the streaming vertex path had always relied on, so it now creates
and keeps its own.

Most importantly: `pc_gfx_present()` rebinds the offscreen render target on its
way out, so the buffer swap happened with a non-zero framebuffer bound. Other
platforms ignore this. macOS presents whatever the context is pointed at when
`flushBuffer` runs, so the window stayed black while every diagnostic reported
success — the frame really was in framebuffer 0, and GL raised no error. The
swap now binds framebuffer 0 and restores the previous binding afterwards.

### Paths and the filesystem

There is no `/proc` on macOS, so `readlink("/proc/self/exe")` fails and returns
an empty path. Both users — the launcher locating the game binary, and the game
locating its save directory — now use `_NSGetExecutablePath`.

APFS requires filenames to be valid UTF-8. The disc carries a few names left in
Shift-JIS, which Linux stores as raw bytes unchanged; on macOS the write simply
fails. These are now decoded through `iconv`, with a hex-escaping fallback that
cannot fail.

SDL's Cocoa message box passes text to `-[NSString stringWithUTF8String:]`,
which returns nil for invalid UTF-8, and hands that nil to AppKit unchecked —
turning a dialog that merely could not be shown into a crash of the launcher.
Text is sanitised before it gets there.

### Allocator

The project replaces global `operator new`/`delete` process-wide. On macOS that
also captures AppleMetalOpenGLRenderer, the C++ OpenGL-over-Metal shim, whose
matching `delete` does not reliably return through the override — so a pointer
offset past what `malloc` returned reached the system `free`, aborting with
"pointer being freed was not allocated" during context creation. macOS gets a
header-free, pointer-transparent implementation; the per-size-class statistics
are unavailable there as a result.

### Not a macOS issue

`Jac_NoteDemoSkipped` is defined in `src/jaudio/pikidemo.c`, which is only
compiled when `PIKMIN_NATIVE_JAUDIO` is on, while its caller in
`MoviePlayer::skipScene` is guarded by `PIKI_PC_PORT` and always built. The
default configuration therefore has a call with no definition. The declaration
also sat outside the header's `extern "C"` scope, so C++ callers looked for a
mangled symbol that the C definition never provides — which would have affected
the `ON` build too. Both are fixed, and the commit is kept separate so it can
be sent upstream.

## Known issues

- The settings layer resizes the window to the desktop resolution just after
  the GL context is created. macOS animates that resize, so the render target
  is torn down and rebuilt for several frames while the drawable size settles.
  Cosmetic, but wasteful; creating the window at the target size would be
  better.
- `dataDir/cinemas/titles/logoMotion/logo.dck` is missing from extraction and
  the boot sequence loops through the title preparation repeatedly.
- Movie playback is unimplemented upstream, so cutscenes produce no picture.
- A missing asset crashes rather than degrading: `System::init` dereferences
  the result of `loadTexture("consFont.bti")` without a null check. This is
  decomp-matched code and has been left alone.
