/**
 * A DualShock 4 on Android is one merged device whose name may still say
 * "Motion Sensors" or "Touchpad". That device has face buttons and must be
 * opened. The split motion node on desktop has the same kind of name and no
 * face buttons, and must stay closed.
 */

#include "pc_pad_filter.h"

#include <cstdio>
#include <cstring>

namespace {

int sFailures = 0;

void expect(bool actual, bool expected, const char* what)
{
	if (actual != expected) {
		std::printf("FAIL: %s -> got %s, expected %s\n", what, actual ? "true" : "false", expected ? "true" : "false");
		++sFailures;
	}
}

void guid_with_buttons(unsigned char guid[16], unsigned buttons)
{
	std::memset(guid, 0, 16);
	guid[12] = static_cast<unsigned char>(buttons & 0xff);
	guid[13] = static_cast<unsigned char>((buttons >> 8) & 0xff);
}

} // namespace

int main()
{
	unsigned char none[16];
	unsigned char face[16];
	unsigned char shoulders[16];
	guid_with_buttons(none, 0);
	guid_with_buttons(face, 0x000f);
	guid_with_buttons(shoulders, 0x0600); // bits 9 and 10, no face buttons

	expect(pc_pad_is_auxiliary_device(nullptr, false, none), false, "missing name");
	expect(pc_pad_is_auxiliary_device("Wireless Controller", false, none), false, "plain gamepad, desktop");
	expect(pc_pad_is_auxiliary_device("Wireless Controller", true, none), false, "plain gamepad, android");

	expect(pc_pad_is_auxiliary_device("Wireless Controller Motion Sensors", false, face), true,
	       "desktop still skips a motion node that happens to carry a guid");
	expect(pc_pad_is_auxiliary_device("DualSense Wireless Controller Motion Sensors", false, none), true,
	       "linux dualsense motion node");
	expect(pc_pad_is_auxiliary_device("Wireless Controller Touchpad", false, none), true, "desktop touchpad");
	expect(pc_pad_is_auxiliary_device("phone gyro", false, none), true, "gyro substring");

	expect(pc_pad_is_auxiliary_device("Wireless Controller Motion Sensors", true, face), false,
	       "android merged gamepad kept the motion name");
	expect(pc_pad_is_auxiliary_device("Wireless Controller Touchpad", true, face), false,
	       "android merged gamepad kept the touchpad name");
	expect(pc_pad_is_auxiliary_device("Wireless Controller Motion Sensors", true, none), true,
	       "android motion-only node");
	expect(pc_pad_is_auxiliary_device("Wireless Controller Motion Sensors", true, shoulders), true,
	       "shoulders without face buttons are not a gamepad");
	expect(pc_android_guid_has_face_buttons(face), true, "face bits");
	expect(pc_android_guid_has_face_buttons(none), false, "empty mask");

	if (sFailures) {
		std::printf("%d failure(s)\n", sFailures);
		return 1;
	}
	std::printf("pc_pad_filter_test ok\n");
	return 0;
}
