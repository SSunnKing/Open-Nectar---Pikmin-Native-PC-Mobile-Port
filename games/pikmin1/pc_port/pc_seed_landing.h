#ifndef PC_SEED_LANDING_H
#define PC_SEED_LANDING_H

/**
 * @brief Whether bury should drop a sprout onto the ground before freezing it.
 *
 * @param ticksPer30Hz System::pcTicksPer30Hz(). Gameplay sets the frame clamp
 *        from fpsMode: 2 at 30 FPS (1 tick, the GameCube step), 1 at 60 (2
 *        ticks) and 0 at 120 (4 ticks). The smaller steps change the bounce
 *        enough that the sprout is still in the air when bury starts. The
 *        30 Hz step is the original landing and is left where the bounce
 *        put it.
 */
inline bool pc_seed_landing_snaps_to_ground(int ticksPer30Hz)
{
	return ticksPer30Hz > 1;
}

/**
 * @brief Whether bury should push a sprout off its ground triangle and let it fall again.
 *
 * @param faceNormalY The ground triangle's face normal Y, not the collision
 *        normal. Map collision records a triangle as ground when the
 *        collision normal's Y is above 0.6, and an edge of a steep face can
 *        pass that while the face itself is a wall. The sprout then plants
 *        in mid-air (upstream issue #65, the cliff by the Blue Onion in the
 *        Forest Navel). A face whose normal Y is 0.5 or more stays planted.
 */
inline bool pc_seed_landing_pushes_off_steep_face(float faceNormalY)
{
	return faceNormalY < 0.5f;
}

#endif // PC_SEED_LANDING_H
