#ifndef PC_SWARM_H
#define PC_SWARM_H

#include <cmath>

// Convert a world-space cursor offset into the local C-stick direction used
// by Navi::makeCStick. Returns false when the cursor is at the captain.
inline bool pc_swarm_cursor_direction(float cursorX, float cursorZ, float cameraYaw, float* outX, float* outZ)
{
	const float length = std::sqrt(cursorX * cursorX + cursorZ * cursorZ);
	if (length <= 0.0001f || !outX || !outZ)
		return false;
	const float worldYaw = std::atan2(cursorX, cursorZ);
	const float localYaw = worldYaw - cameraYaw;
	*outX = std::sin(localYaw);
	*outZ = std::cos(localYaw);
	return true;
}

#endif
