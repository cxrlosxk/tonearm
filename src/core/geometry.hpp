// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tonearm {

struct Vec2 {
	float x = 0, y = 0;
};

struct Rect {
	float x = 0, y = 0, w = 0, h = 0;

	float right() const { return x + w; }
	float bottom() const { return y + h; }
	float cx() const { return x + w * 0.5f; }
	float cy() const { return y + h * 0.5f; }
	bool empty() const { return w <= 0.0f || h <= 0.0f; }

	Rect inset(float d) const { return {x + d, y + d, std::max(0.0f, w - 2 * d), std::max(0.0f, h - 2 * d)}; }
	Rect inset(float dx, float dy) const { return {x + dx, y + dy, std::max(0.0f, w - 2 * dx), std::max(0.0f, h - 2 * dy)}; }
	Rect translated(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
	Rect scaled(float s) const { return {x * s, y * s, w * s, h * s}; }
	Rect scaledAboutCenter(float s) const
	{
		const float nw = w * s, nh = h * s;
		return {cx() - nw * 0.5f, cy() - nh * 0.5f, nw, nh};
	}
};

inline float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

inline Rect lerp(const Rect &a, const Rect &b, float t)
{
	return {lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.w, b.w, t), lerp(a.h, b.h, t)};
}

// Snap to the device pixel grid so text layers stay crisp.
inline float snap(float v)
{
	return std::round(v);
}

inline Rect snap(const Rect &r)
{
	const float x0 = std::round(r.x), y0 = std::round(r.y);
	return {x0, y0, std::round(r.x + r.w) - x0, std::round(r.y + r.h) - y0};
}

} // namespace tonearm
