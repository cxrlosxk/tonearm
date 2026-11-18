// SPDX-License-Identifier: GPL-2.0-or-later
#include "compositor_cpu.hpp"

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

void executeOnCpu(Image &target, const std::vector<DrawOp> &ops)
{
	for (const DrawOp &op : ops) {
		if (op.kind == DrawOp::Kind::Image) {
			if (op.image && !op.image->empty())
				drawImageOp(target, op);
		} else {
			drawRoundRectOp(target, op);
		}
	}
}

} // namespace tonearm
