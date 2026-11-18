// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "color.hpp"
#include "geometry.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace tonearm {

// Premultiplied BGRA8 image, the native layout for both OBS (GS_BGRA) and Windows imaging.
class Image {
public:
	Image() = default;
	Image(int w, int h);

	int width() const { return w_; }
	int height() const { return h_; }
	bool empty() const { return w_ <= 0 || h_ <= 0; }
	uint8_t *data() { return px_.data(); }
	const uint8_t *data() const { return px_.data(); }
	int stride() const { return w_ * 4; }
	uint8_t *row(int y) { return px_.data() + static_cast<size_t>(y) * stride(); }
	const uint8_t *row(int y) const { return px_.data() + static_cast<size_t>(y) * stride(); }

	void clear();
	void fill(const Color &c);

	// Builds an image from straight-alpha RGBA8 (e.g. decoded PNG data).
	static Image fromRgbaStraight(int w, int h, const uint8_t *rgba, int stride);
	// Same for straight BGRA8.
	static Image fromBgraStraight(int w, int h, const uint8_t *bgra, int stride);

private:
	int w_ = 0, h_ = 0;
	std::vector<uint8_t> px_;
};

using ImagePtr = std::shared_ptr<const Image>;

// --- Raster operations. All work in premultiplied space and are deterministic. ---

// High quality resample (area averaging when shrinking, bilinear when enlarging).
Image resize(const Image &src, int w, int h);

// Crops the centered region matching the target aspect ratio, then resizes ("cover" fit).
Image coverFit(const Image &src, int w, int h);

// Approximate gaussian blur using three box passes. Radius in pixels.
void blur(Image &img, float radius);

// Draws `src` onto `dst` at integer offset with the given opacity (source-over).
void composite(Image &dst, const Image &src, int x, int y, float opacity = 1.0f);

// Multiplies every pixel by a mask computed from a rounded rectangle covering the image.
void applyRoundedMask(Image &img, float radius);

// Paint primitives used by layer rasterization.
struct GradientStop {
	float pos;
	Color color;
};

struct Paint {
	enum class Kind { Solid, Linear, Radial } kind = Kind::Solid;
	Color color;
	std::vector<GradientStop> stops;
	Vec2 p0, p1;    // linear: start/end; radial: center and (radius, unused)
	float radius = 0;

	static Paint solid(const Color &c)
	{
		Paint p;
		p.color = c;
		return p;
	}
	static Paint linear(Vec2 a, Vec2 b, std::vector<GradientStop> s)
	{
		Paint p;
		p.kind = Kind::Linear;
		p.p0 = a;
		p.p1 = b;
		p.stops = std::move(s);
		return p;
	}
	static Paint radial(Vec2 c, float r, std::vector<GradientStop> s)
	{
		Paint p;
		p.kind = Kind::Radial;
		p.p0 = c;
		p.radius = r;
		p.stops = std::move(s);
		return p;
	}
	Color sample(float x, float y) const;
};

// Fills an anti-aliased rounded rectangle (signed distance based).
void fillRoundRect(Image &dst, const Rect &r, float radius, const Paint &paint, float opacity = 1.0f);

// Strokes the inside edge of a rounded rectangle.
void strokeRoundRect(Image &dst, const Rect &r, float radius, float width, const Paint &paint, float opacity = 1.0f);

// Renders a soft drop shadow of a rounded rectangle.
void dropShadow(Image &dst, const Rect &r, float radius, float blurRadius, float offsetY, const Color &color);

// Adds a soft glow (blurred, tinted copy) of `src` underneath it into `dst`.
void glow(Image &dst, const Image &src, int x, int y, float radius, const Color &tint, float strength);

// Fine film grain / dither noise to avoid gradient banding. Deterministic.
void addNoise(Image &img, float amount, uint32_t seed = 1337u);

// Average color of an image region (straight alpha output).
Color averageColor(const Image &img);

// Multiplies all pixels by alpha in 0..1.
void multiplyAlpha(Image &img, float a);

// Adjusts saturation in place (1 = unchanged) and brightness multiplier.
void adjustColor(Image &img, float saturation, float brightness);

} // namespace tonearm
