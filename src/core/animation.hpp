// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cmath>

namespace tonearm {

// CSS-style cubic bezier timing function with control points (x1, y1) and (x2, y2).
class CubicBezier {
public:
	constexpr CubicBezier(float x1, float y1, float x2, float y2) : x1_(x1), y1_(y1), x2_(x2), y2_(y2) {}

	float operator()(float t) const
	{
		if (t <= 0)
			return 0;
		if (t >= 1)
			return 1;
		// Newton-Raphson on x(u) = t, falling back to bisection.
		float u = t;
		for (int i = 0; i < 6; ++i) {
			const float x = sample(x1_, x2_, u) - t;
			const float d = slope(x1_, x2_, u);
			if (std::fabs(x) < 1e-5f)
				return sample(y1_, y2_, u);
			if (std::fabs(d) < 1e-6f)
				break;
			u -= x / d;
		}
		float lo = 0, hi = 1;
		u = t;
		for (int i = 0; i < 24; ++i) {
			const float x = sample(x1_, x2_, u);
			if (std::fabs(x - t) < 1e-5f)
				break;
			if (x < t)
				lo = u;
			else
				hi = u;
			u = (lo + hi) * 0.5f;
		}
		return sample(y1_, y2_, u);
	}

private:
	static float sample(float a, float b, float u)
	{
		const float iu = 1 - u;
		return 3 * iu * iu * u * a + 3 * iu * u * u * b + u * u * u;
	}
	static float slope(float a, float b, float u)
	{
		const float iu = 1 - u;
		return 3 * iu * iu * a + 6 * iu * u * (b - a) + 3 * u * u * (1 - b);
	}

	float x1_, y1_, x2_, y2_;
};

namespace easing {
// Calm deceleration for things arriving on screen.
inline const CubicBezier enter{0.16f, 1.0f, 0.3f, 1.0f};
// Quick acceleration for things leaving.
inline const CubicBezier exit{0.4f, 0.0f, 1.0f, 1.0f};
// General purpose for property changes.
inline const CubicBezier standard{0.2f, 0.0f, 0.0f, 1.0f};
// Symmetric, used for crossfades.
inline const CubicBezier inOut{0.65f, 0.0f, 0.35f, 1.0f};
} // namespace easing

// Progress of an animation that started at `start` seconds, with optional delay, in [0, 1].
inline float phase(double now, double start, double delaySec, double durationSec)
{
	if (durationSec <= 0)
		return now >= start + delaySec ? 1.0f : 0.0f;
	const double t = (now - start - delaySec) / durationSec;
	return t <= 0 ? 0.0f : (t >= 1 ? 1.0f : static_cast<float>(t));
}

// Critically damped smoothing toward a target; frame-rate independent.
inline float approach(float current, float target, float dt, float halfLifeSec)
{
	if (halfLifeSec <= 0)
		return target;
	const float k = 1.0f - std::exp2(-dt / halfLifeSec);
	return current + (target - current) * k;
}

} // namespace tonearm
