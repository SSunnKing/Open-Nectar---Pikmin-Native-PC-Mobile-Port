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
 * The Pikmin 1 launcher has the same helper. Keep the two copies in step.
 */

inline void pc_mac_disable_press_and_hold(void)
{
#if defined(__APPLE__)
	// Objective-C from C++. Foundation is present in every Mac app that links SDL.
#include <objc/message.h>
#include <objc/runtime.h>
	Class defaultsClass = objc_getClass("NSUserDefaults");
	Class stringClass   = objc_getClass("NSString");
	if (!defaultsClass || !stringClass)
		return;
	using MsgId    = id (*)(id, SEL);
	using MakeStr  = id (*)(id, SEL, const char*);
	using SetBool  = void (*)(id, SEL, BOOL, id);
	id defaults = reinterpret_cast<MsgId>(objc_msgSend)(
	    (id)defaultsClass, sel_registerName("standardUserDefaults"));
	id key = reinterpret_cast<MakeStr>(objc_msgSend)(
	    (id)stringClass, sel_registerName("stringWithUTF8String:"), "ApplePressAndHoldEnabled");
	if (!defaults || !key)
		return;
	reinterpret_cast<SetBool>(objc_msgSend)(
	    defaults, sel_registerName("setBool:forKey:"), NO, key);
#endif
}
