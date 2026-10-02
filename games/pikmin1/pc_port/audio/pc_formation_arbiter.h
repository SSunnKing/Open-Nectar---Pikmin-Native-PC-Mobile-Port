#ifndef PC_FORMATION_ARBITER_H
#define PC_FORMATION_ARBITER_H

#include <cstdint>

/*
 * The C-stick ("formation" / swarm) sound is one voice. Both audio engines
 * keep a single global state for it: JAudio's Jac_Orima_Formation owns one
 * track and one on/off flag, and the legacy audio_stubs.cpp copy owns one
 * on/off flag. It is written once per frame by every captain.
 *
 * With one captain that is exactly right. With local co-op there are two
 * captains, and the idle one calls it with (0, 0) every frame. One captain
 * pushing the stick and one idle then starts the sound on one call and stops
 * it on the next, every tick, and which captain "wins" depends on the order
 * the navis happen to update in.
 *
 * This keeps the latest stick per captain and forwards the most deflected
 * one, so the engine sees a single steady request. It is a pure value type:
 * it never calls into the audio engine, the caller forwards the result. It
 * hands back a stick on every call (not only when the answer changes), so a
 * direct Jac_Orima_Formation(0, 0) from elsewhere, which bypasses this, still
 * stops the sound and the next call simply asks for it again.
 *
 * A captain that stops calling (the navi update skips the call while it is
 * dead or in the sunset demo) would otherwise leave its last stick in force
 * forever. Each slot is stamped with the frame it was written on and is
 * ignored once it is older than PC_FORMATION_STALE_FRAMES.
 */

/// Captains tracked separately. An index outside 0..N-1 is forwarded as-is.
#define PC_FORMATION_MAX_CAPTAINS (4)

/// A slot not refreshed for more than this many frames is ignored. A live
/// captain calls every frame, and one that updates later in the frame than
/// another is a frame behind it at most, so two frames of slack is plenty.
#define PC_FORMATION_STALE_FRAMES (2u)

struct PcFormationStick {
    std::int32_t x;
    std::int32_t y;
};

class PcFormationArbiter {
public:
    PcFormationArbiter() { reset(); }

    void reset() {
        for (int i = 0; i < PC_FORMATION_MAX_CAPTAINS; ++i) mSlots[i] = Slot();
    }

    /// Records this captain's stick for `frame` and returns the stick to hand
    /// the audio engine: the most deflected of the live captains, or (0, 0)
    /// when none of them is pushing. `frame` only has to be monotonic; the
    /// comparison is by unsigned difference, so it may wrap.
    PcFormationStick submit(int captain, std::int32_t x, std::int32_t y, std::uint32_t frame) {
        if (captain < 0 || captain >= PC_FORMATION_MAX_CAPTAINS) return PcFormationStick{ x, y };

        Slot& own = mSlots[captain];
        own.x = x;
        own.y = y;
        own.frame = frame;
        own.used = true;

        PcFormationStick best = { 0, 0 };
        std::int64_t bestMag = 0;
        for (int i = 0; i < PC_FORMATION_MAX_CAPTAINS; ++i) {
            const Slot& slot = mSlots[i];
            if (!slot.used || static_cast<std::uint32_t>(frame - slot.frame) > PC_FORMATION_STALE_FRAMES) continue;
            const std::int64_t mag = magnitudeSquared(slot.x, slot.y);
            // Strictly greater, so a tie goes to the lower captain index and
            // the result does not depend on which captain called last.
            if (mag > bestMag) {
                bestMag = mag;
                best = PcFormationStick{ slot.x, slot.y };
            }
        }
        return best;
    }

    /// How the engines measure a stick: each axis bounded to +-127, then the
    /// length. Squared here, since only the ordering matters.
    static std::int64_t magnitudeSquared(std::int32_t x, std::int32_t y) {
        x = clampAxis(x);
        y = clampAxis(y);
        return static_cast<std::int64_t>(x) * x + static_cast<std::int64_t>(y) * y;
    }

private:
    struct Slot {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::uint32_t frame = 0;
        bool used = false;
    };

    static std::int32_t clampAxis(std::int32_t v) { return v < -127 ? -127 : (v > 127 ? 127 : v); }

    Slot mSlots[PC_FORMATION_MAX_CAPTAINS];
};

#endif
