#pragma once

// Time within one original GameCube physics interval. Movement remains at the
// selected tick rate; only the kick/drift recurrence uses the original step.
// The owner advances this once per movement, not once per velocity pass.
class PcBallisticClock {
public:
	static constexpr float step = 1.0f / 30.0f;
	static bool enabled(float dt) { return dt > 0.0f && dt < step - 0.0000001f; }
	bool boundary() const { return mElapsed == 0.0f; }
	float slice(float duration) const
	{
		const float remaining = step - mElapsed;
		return duration < remaining ? duration : remaining;
	}
	void advance(float duration)
	{
		mElapsed += duration;
		if (mElapsed >= step - 0.0000001f) mElapsed = 0.0f;
	}
	float elapsed() const { return mElapsed; }
	void reset() { mElapsed = 0.0f; }
private:
	float mElapsed = 0.0f;
};
