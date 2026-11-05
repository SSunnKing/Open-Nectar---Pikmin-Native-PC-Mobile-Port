/**
 * @file pc_touch_heap_test.cpp
 * @brief The touch layer's lists must not live in a section heap (issue #238).
 *
 * Pikmin 2's global operator new (JKRHeap.cpp) allocates from
 * JKRHeap::sCurrentHeap. pc_touch.cpp keeps three file-scope lists that grow
 * during play (tracked panes, inline icons, help icons). If their buffers come
 * from the heap of the section that is current when they grow, the buffers die
 * with that section, and the next growth frees a block of a later section's
 * heap: JKRExpHeap::do_free from pc_touch_pane_hit on the sunset menu.
 *
 * This test links the real JKRHeap / JKRExpHeap and runs that lifecycle:
 * section heap A is made current, the touch lists grow, A is destroyed, heap B
 * is created in the same memory and made current, and the lists grow again.
 * Neither section heap may lose a byte to the touch layer, and B must still
 * pass its own consistency check. CTest sets PIKMIN_HEAP_CHECK_FREE=1, so a
 * free of a block B never handed out is printed ("not in used list") and
 * fails the test instead of corrupting B. Without it, the code before the fix
 * for #238 crashes here (SIGSEGV), as on the device.
 *
 * Nothing here draws or reads art: pc_touch_draw is never called.
 */

#include "pc_touch.h"

#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JUtility/JUTConsole.h"
#include "Dolphin/OS/OSAlloc.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <vector>

// ── Stubs for what pc_touch.cpp and JKRHeap/JKRExpHeap reference ─────────────
// None of these run in the paths this test calls, except
// pc_gfx_project_current (inline icons) and the OS mutex calls (heap locks).

#include "gl/pc_gfx.h"
#include "pc_window.h"
#include "pc_gyro.h"
#include "settings/pc_settings.h"
#include "settings/pc_p2_glass_assets.h"
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#include "stb_image.h"

unsigned pc_gfx_overlay_texture_create(int, int, const unsigned char*) { return 0; }
void pc_gfx_overlay_texture_destroy(unsigned) { }
void pc_gfx_overlay_begin(void) { }
void pc_gfx_overlay_sprite(unsigned, float, float, float, float, float, float, float, float, float) { }
void pc_gfx_overlay_end(void) { }
void pc_gfx_get_drawable_size(int* width, int* height)
{
	*width  = 1280;
	*height = 720;
}
// Identity projection: the inline icon lands where the text says it is.
bool pc_gfx_project_current(float x, float y, float, float* winX, float* winY)
{
	*winX = x;
	*winY = y;
	return true;
}

void pc_window_request_lockon_press(void) { }
void pc_window_request_firstperson_press(void) { }
void pc_window_request_charge_press(void) { }
extern "C" void pc_window_add_cursor_delta(float, float) { }
extern "C" void pc_window_add_touch_zoom(float) { }
extern "C" void pc_window_add_camera_drag(float) { }

void pc_settings_request_toggle(void) { }
void pc_settings_touch_buttons(unsigned short) { }
void pc_settings_touch_tap(float, float) { }
void pc_settings_touch_drag(float) { }
int pc_settings_get_gyro_enabled(void) { return 0; }
int pc_settings_get_lock_on(void) { return 0; }
int pc_settings_get_charge(void) { return 0; }
int pc_settings_get_first_person(void) { return 0; }
void pc_gyro_request_recenter(void) { }

bool pc_p2_load_bti_rgba(const char*, const char*, std::vector<unsigned char>&, int&, int&) { return false; }

extern "C" {
stbi_uc* stbi_load_from_memory(stbi_uc const*, int, int*, int*, int*, int) { return nullptr; }
void stbi_image_free(void*) { }
const char* stbi_failure_reason(void) { return "stub"; }
}

// The heaps' console reports go to stdout, so a broken heap shows in the log.
#define PC_TOUCH_HEAP_TEST_VPRINT(fmt)    \
	do {                                  \
		va_list args;                     \
		va_start(args, fmt);              \
		std::vprintf(fmt, args);          \
		va_end(args);                     \
	} while (0)
void JUTReportConsole(const char* message) { std::fputs(message, stdout); }
void JUTReportConsole_f(const char* fmt, ...) { PC_TOUCH_HEAP_TEST_VPRINT(fmt); }
void JUTWarningConsole(const char* message) { std::fputs(message, stdout); }
void JUTWarningConsole_f(const char* fmt, ...) { PC_TOUCH_HEAP_TEST_VPRINT(fmt); }
void OSPanic(const char* file, int line, const char* fmt, ...)
{
	std::printf("OSPanic %s:%d: ", file, line);
	PC_TOUCH_HEAP_TEST_VPRINT(fmt);
	std::abort();
}

// The root heap is built over host memory below, not over the OS arena.
void* OSGetArenaHi(void) { return nullptr; }
void* OSGetArenaLo(void) { return nullptr; }
void OSSetArenaHi(void*) { }
void OSSetArenaLo(void*) { }
void* OSInitAlloc(void* arenaStart, void*, int) { return arenaStart; }

// Single-threaded test: the heap mutexes have nothing to guard.
void OSInitMutex(OSMutex*) { }
void OSLockMutex(OSMutex*) { }
void OSUnlockMutex(OSMutex*) { }
extern "C" void pc_os_release_sync_range(const void*, const void*) { }
extern "C" void pc_os_release_mutex_range(const void*, const void*) { }
extern "C" void* pcSceneOverlapping(void*, unsigned) { return nullptr; }

// ── Test ─────────────────────────────────────────────────────────────────────
namespace {

int sFailures = 0;

void check(bool condition, const char* what)
{
	if (!condition) {
		std::printf("FAIL: %s\n", what);
		++sFailures;
	}
}

// Distinct pane identities: the touch layer only compares the pointers.
char sPanes[64];
int sNextPane = 0;
float sNextIcon = 0.0f;

void grow_tracked_panes(int count)
{
	for (int i = 0; i < count; ++i) {
		// First question about a pane: the layer starts tracking it.
		check(!pc_touch_pane_hit(&sPanes[sNextPane++], 0.5f, 0.5f, 0.0f), "an untracked pane is not hit");
	}
}

void grow_inline_icons(int count)
{
	for (int i = 0; i < count; ++i) {
		sNextIcon += 40.0f; // far apart, so no icon is folded into another
		pc_touch_mark_inline_icon('a', sNextIcon, 100.0f, 10.0f);
	}
}

void grow_help_icons(int count)
{
	for (int i = 0; i < count; ++i) {
		sNextIcon += 40.0f;
		pc_touch_mark_help_icon('b', sNextIcon, 200.0f, 10.0f);
	}
}

// Grows one touch list while @p section is current and reports any bytes the
// section heap lost to it.
void grow_in_section(JKRHeap* section, void (*grow)(int), int count, const char* what)
{
	const u32 before = section->getTotalFreeSize();
	grow(count);
	const u32 after = section->getTotalFreeSize();
	if (after != before) {
		std::printf("  %s took %u bytes from section heap %p\n", what, (unsigned)(before - after), (void*)section);
	}
	check(after == before, what);
}

void grow_all_in_section(JKRHeap* section, int count)
{
	grow_in_section(section, grow_tracked_panes, count, "tracked panes stay out of the section heap");
	grow_in_section(section, grow_inline_icons, count, "inline icons stay out of the section heap");
	grow_in_section(section, grow_help_icons, count, "help icons stay out of the section heap");
}

} // namespace

int main()
{
	// do_free writes its backtrace straight to fd 1: keep printf in step.
	std::setvbuf(stdout, nullptr, _IONBF, 0);

	// Root heap over host memory, as JKRExpHeap::createRoot does over the arena.
	const u32 kRootSize = 1u << 20;
	void* arena         = std::malloc(kRootSize);
	JKRExpHeap* root    = new (arena) JKRExpHeap((u8*)arena + 0x100, kRootSize - 0x100, nullptr, false);
	JKRHeap::sRootHeap  = root;
	// Outside a section nothing is current, so the test's own allocations use
	// malloc (operator new's fallback) and leave the heaps alone.
	JKRHeap::sCurrentHeap = nullptr;

	const u32 kSectionSize = 64u << 10;

	// Section A (e.g. the day): the lists grow while its heap is current.
	JKRExpHeap* sectionA = JKRExpHeap::create(kSectionSize, root, false);
	check(sectionA != nullptr, "section heap A is created");
	if (!sectionA) return 1;
	sectionA->becomeCurrentHeap();
	grow_all_in_section(sectionA, 10);

	// Section change: A is destroyed and B takes over the same memory.
	void* const aStart = sectionA->getStartAddr();
	sectionA->destroy();
	JKRHeap::sCurrentHeap = nullptr;
	JKRExpHeap* sectionB  = JKRExpHeap::create(kSectionSize, root, false);
	check(sectionB != nullptr, "section heap B is created");
	if (!sectionB) return 1;
	if (sectionB->getStartAddr() != aStart) std::printf("  note: heap B did not reuse heap A's memory\n");
	sectionB->becomeCurrentHeap();

	// Sunset menu: more panes are asked about. Past the lists' capacity this
	// reallocates them and frees the old buffers.
	grow_all_in_section(sectionB, 20);
	check(sectionB->check(), "section heap B is consistent after the touch lists grew");

	JKRHeap::sCurrentHeap = nullptr;
	sectionB->destroy();

	if (sFailures) {
		std::printf("%d failure(s)\n", sFailures);
		return 1;
	}
	std::printf("pc_touch heap test: OK\n");
	return 0;
}
