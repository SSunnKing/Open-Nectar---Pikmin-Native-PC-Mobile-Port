#pragma once

#include <cctype>
#include <cstddef>

// Android's EventHub merges a Bluetooth gamepad with the touchpad and motion
// nodes Linux split apart, and keeps whichever name was registered first
// ("Wireless Controller Motion Sensors", "Wireless Controller Touchpad").
// Android_AddJoystick stores that device's button mask in GUID bytes 12-13,
// little-endian. SDL_CONTROLLER_BUTTON_A..Y are bits 0..3. A real motion-only
// node has none of those bits and must stay closed: its gravity axis looks
// like a held stick and walks the menu.

inline bool pc_android_guid_has_face_buttons(const unsigned char guid[16])
{
	if (!guid)
		return false;
	const unsigned buttons = static_cast<unsigned>(guid[12]) | (static_cast<unsigned>(guid[13]) << 8);
	const unsigned face = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3);
	return (buttons & face) != 0;
}

inline bool pc_pad_name_contains(const char* haystack, const char* needle)
{
	if (!haystack || !needle || !needle[0])
		return false;
	for (const char* p = haystack; *p; ++p) {
		std::size_t i = 0;
		for (; needle[i]; ++i) {
			const unsigned char c = static_cast<unsigned char>(p[i]);
			if (!c)
				return false;
			if (std::tolower(c) != static_cast<unsigned char>(needle[i]))
				break;
		}
		if (!needle[i])
			return true;
	}
	return false;
}

inline bool pc_pad_name_is_auxiliary(const char* name)
{
	return pc_pad_name_contains(name, "motion") || pc_pad_name_contains(name, "touchpad")
	    || pc_pad_name_contains(name, "accelerometer") || pc_pad_name_contains(name, "gyro");
}

// `android` is true only on Android. Desktop keeps skipping every auxiliary
// name, which is how a split DualSense motion device stays closed.
inline bool pc_pad_is_auxiliary_device(const char* name, bool android, const unsigned char guid[16])
{
	if (!pc_pad_name_is_auxiliary(name))
		return false;
	if (android && pc_android_guid_has_face_buttons(guid))
		return false;
	return true;
}
