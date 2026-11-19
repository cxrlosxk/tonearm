// SPDX-License-Identifier: GPL-2.0-or-later
#include "compositor_cpu.hpp"

#include "motion.hpp"

#include <algorithm>
#include <cmath>

namespace tonearm {

static void drawImageOp(Image &target, const DrawOp &op)
{
	const Image &src = *op.image;
	const int dw = std::max(1, static_cast<int>(std::lround(op.dst.w)));
	const int dh = std::max(1, static_cast<int>(std::lround(op.dst.h)));
	Image scaled = (dw == src.width() && dh == src.height()) ? src : resize(src, dw, dh);
	const int ox = static_cast<int>(std::lround(op.dst.x));
	const int oy = static_cast<int>(std::lround(op.dst.y));

	if (op.clip) {
		// Apply the clip rect with soft horizontal edges, in target coordinates.
		for (int y = 0; y < scaled.height(); ++y) {
			const float ty = oy + y + 0.5f;
			uint8_t *p = scaled.row(y);
			for (int x = 0; x < scaled.width(); ++x, p += 4) {
				const float tx = ox + x + 0.5f;
				float m = 1.0f;
				if (ty < op.clipRect.y || ty > op.clipRect.bottom() || tx < op.clipRect.x || tx > op.clipRect.right()) {
					m = 0;
				} else {
					if (op.fadeLeft > 0)
						m *= std::clamp((tx - op.clipRect.x) / op.fadeLeft, 0.0f, 1.0f);
					if (op.fadeRight > 0)
						m *= std::clamp((op.clipRect.right() - tx) / op.fadeRight, 0.0f, 1.0f);
				}
				if (m >= 1.0f)
					continue;
				for (int c = 0; c < 4; ++c)
					p[c] = static_cast<uint8_t>(p[c] * m + 0.5f);
			}
		}
	}
	composite(target, scaled, ox, oy, op.opacity);
}

static void drawRoundRectOp(Image &target, const DrawOp &op)
{
	if (op.color.a <= 0 || op.rect.empty())
		return;
	if (op.softness <= 0) {
		fillRoundRect(target, op.rect, op.radius, Paint::solid(op.color));
		return;
	}
	const int pad = static_cast<int>(std::ceil(op.softness * 2)) + 2;
	Image tmp(static_cast<int>(std::ceil(op.rect.w)) + pad * 2, static_cast<int>(std::ceil(op.rect.h)) + pad * 2);
	fillRoundRect(tmp, {static_cast<float>(pad), static_cast<float>(pad), op.rect.w, op.rect.h}, op.radius, Paint::solid(op.color));
	blur(tmp, op.softness);
	composite(target, tmp, static_cast<int>(std::lround(op.rect.x)) - pad, static_cast<int>(std::lround(op.rect.y)) - pad);
}

static float sdRoundRect(float px, float py, float hx, float hy, float r)
{
	const float qx = std::fabs(px) - hx + r, qy = std::fabs(py) - hy + r;
	const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
	return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

static float roundRectCoverage(const Rect &r, float radius, float px, float py)
{
	const float rr = std::min(radius, std::min(r.w, r.h) * 0.5f);
	const float d = sdRoundRect(px - r.cx(), py - r.cy(), r.w * 0.5f, r.h * 0.5f, rr);
	return std::clamp(0.5f - d, 0.0f, 1.0f);
}

float videoCoverage(const DrawOp &op, float px, float py)
{
	float a = roundRectCoverage(op.dst, op.radius, px, py);
	if (!op.clipRect.empty())
		a *= roundRectCoverage(op.clipRect, op.clipRadius, px, py);
	if (op.featherEdge != 0 && op.feather > 0) {
		const float d = op.featherEdge == 1 ? op.dst.right() - px : op.dst.bottom() - py;
		const float t = std::clamp(d / op.feather, 0.0f, 1.0f);
		a *= t * t * (3 - 2 * t);
	}
	return a;
}

static void drawVideoOp(Image &target, const DrawOp &op, const Image &frame)
{
	if (frame.empty() || op.opacity <= 0 || op.dst.empty())
		return;
	const CropRect crop = coverCrop(frame.width(), frame.height(), op.dst.w, op.dst.h);
	const int x0 = std::max(0, static_cast<int>(std::floor(op.dst.x)));
	const int y0 = std::max(0, static_cast<int>(std::floor(op.dst.y)));
	const int x1 = std::min(target.width(), static_cast<int>(std::ceil(op.dst.right())));
	const int y1 = std::min(target.height(), static_cast<int>(std::ceil(op.dst.bottom())));
	for (int y = y0; y < y1; ++y) {
		uint8_t *d = target.row(y) + x0 * 4;
		for (int x = x0; x < x1; ++x, d += 4) {
			const float px = x + 0.5f, py = y + 0.5f;
			const float m = videoCoverage(op, px, py) * op.opacity;
			if (m <= 0.0f)
				continue;
			// Nearest sample of the cropped frame (the GPU filters linearly).
			const float u = (px - op.dst.x) / op.dst.w, v = (py - op.dst.y) / op.dst.h;
			const int sx = std::clamp(static_cast<int>(crop.x + u * crop.w), 0, frame.width() - 1);
			const int sy = std::clamp(static_cast<int>(crop.y + v * crop.h), 0, frame.height() - 1);
			const uint8_t *s = frame.row(sy) + sx * 4;
			const float sa = s[3] / 255.0f * m;
			const float k = std::clamp(op.color.a, 0.0f, 1.0f);
			const float tint[3] = {op.color.b * 255.0f * s[3] / 255.0f, op.color.g * 255.0f * s[3] / 255.0f, op.color.r * 255.0f * s[3] / 255.0f};
			for (int c = 0; c < 4; ++c) {
				const float v = c < 3 ? s[c] * (1.0f - k) + tint[c] * k : s[c];
				d[c] = static_cast<uint8_t>(std::clamp(v * m + d[c] * (1.0f - sa), 0.0f, 255.0f) + 0.5f);
			}
		}
	}
}

void executeOnCpu(Image &target, const std::vector<DrawOp> &ops, const Image *video)
{
	for (const DrawOp &op : ops) {
		if (op.kind == DrawOp::Kind::Image) {
			if (op.image && !op.image->empty() && (!op.needsVideo || video))
				drawImageOp(target, op);
		} else if (op.kind == DrawOp::Kind::Video) {
			if (video)
				drawVideoOp(target, op, *video);
		} else {
			drawRoundRectOp(target, op);
		}
	}
}

} // namespace tonearm
