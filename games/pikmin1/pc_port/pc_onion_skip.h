#pragma once

/**
 * @file pc_onion_skip.h
 * @brief Decisions for skipping an Onion cutscene.
 *
 * The first seed is not a movie key. The boot animation enters BootEmit, and
 * that is what creates it. A skip between Boot and that moment leaves the
 * Onion in BootInit; EmitCone then snaps it to Wait, so the recovery has to
 * happen first. Once the boot has moved on, the same check stays false and a
 * second seed is not created.
 *
 * The coloured cone is a separate model. While the Onion is buried or
 * landing, that model is scaled away and EmitCone grows it back. Skipping
 * cancels the grow. An Onion that is already open must be left alone: the
 * size saved at the start of a shrink is not meaningful then.
 */

inline bool pc_onion_skip_needs_first_seed(bool hasBootContainer, int stateId, int bootInitId)
{
	return hasBootContainer && stateId == bootInitId;
}

inline bool pc_onion_skip_restores_cone(bool closing, bool emitting, float scaleX)
{
	return closing || emitting || scaleX <= 0.001f;
}
