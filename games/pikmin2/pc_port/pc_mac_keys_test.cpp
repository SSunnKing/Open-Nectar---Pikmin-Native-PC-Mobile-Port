/**
 * @file pc_mac_keys_test.cpp
 * @brief The press-and-hold helper compiles and is a no-op off macOS.
 *
 * On macOS it sets ApplePressAndHoldEnabled. This host has no AppKit, so the
 * call must simply return.
 */

#include "pc_mac_keys.h"

int main()
{
	pc_mac_disable_press_and_hold();
	return 0;
}
