#pragma once
/**
 * @file pc_mac_keys.h
 * @brief Turn off the macOS press-and-hold accent popup.
 *
 * Holding a key on macOS opens the accent menu. SDL's own Cocoa backend only
 * registerDefaults ApplePressAndHoldEnabled, and a system sdl2-compat build
 * does not do that (the same note as SDL_StopTextInput in pc_window). The
 * process sets the preference itself. Other platforms do nothing.
 *
 * The Objective-C headers are not included. They define inline functions, so
 * they cannot live inside this function, and their BOOL clashes with the
 * GameCube `typedef int BOOL` already visible from Dolphin/pad.h. The three
 * runtime symbols are declared here and resolved from Foundation.
 *
 * The Pikmin 1 launcher has the same helper. Keep the two copies in step.
 */

#if defined(__APPLE__)
extern "C" {
typedef struct objc_object PcMacObject;
typedef PcMacObject* PcMacId;
typedef struct objc_selector* PcMacSel;
typedef struct objc_class* PcMacClass;
// Apple's current SDK declares this with no prototype so callers cast it.
void objc_msgSend(void);
PcMacClass objc_getClass(const char* name);
PcMacSel sel_registerName(const char* name);
}
#endif

inline void pc_mac_disable_press_and_hold(void)
{
#if defined(__APPLE__)
	const PcMacClass defaultsClass = objc_getClass("NSUserDefaults");
	const PcMacClass stringClass   = objc_getClass("NSString");
	if (!defaultsClass || !stringClass)
		return;
	using MsgId   = PcMacId (*)(PcMacId, PcMacSel);
	using MakeStr = PcMacId (*)(PcMacId, PcMacSel, const char*);
	using SetBool = void (*)(PcMacId, PcMacSel, signed char, PcMacId);
	const PcMacId defaults = reinterpret_cast<MsgId>(objc_msgSend)(
	    reinterpret_cast<PcMacId>(defaultsClass), sel_registerName("standardUserDefaults"));
	const PcMacId key = reinterpret_cast<MakeStr>(objc_msgSend)(
	    reinterpret_cast<PcMacId>(stringClass), sel_registerName("stringWithUTF8String:"),
	    "ApplePressAndHoldEnabled");
	if (!defaults || !key)
		return;
	reinterpret_cast<SetBool>(objc_msgSend)(
	    defaults, sel_registerName("setBool:forKey:"), static_cast<signed char>(0), key);
#endif
}
