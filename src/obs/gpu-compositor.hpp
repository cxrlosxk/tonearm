// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/scene.hpp"

#include <graphics/graphics.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace tonearm {

// Executes a Scene draw list with libobs graphics. Must be used from the graphics
// thread (video_render); textures live until they go unused for a few frames.
class GpuCompositor {
public:
	GpuCompositor() = default;
	~GpuCompositor();
	GpuCompositor(const GpuCompositor &) = delete;
	GpuCompositor &operator=(const GpuCompositor &) = delete;

	bool init(const char *effectPath); // call inside graphics context
	void render(const std::vector<DrawOp> &ops);
	void releaseAll();                  // call inside graphics context

	size_t textureCount() const { return textures_.size(); }

private:
	struct Entry {
		gs_texture_t *tex = nullptr;
		uint64_t lastFrame = 0;
	};

	gs_texture_t *textureFor(const DrawOp &op);
	void drawImage(const DrawOp &op, gs_texture_t *tex);
	void drawRoundRect(const DrawOp &op);
	void collect();

	gs_effect_t *effect_ = nullptr;
	gs_technique_t *techLayer_ = nullptr, *techRect_ = nullptr;
	gs_eparam_t *pImage_ = nullptr, *pOpacity_ = nullptr, *pDst_ = nullptr, *pClip_ = nullptr, *pFade_ = nullptr,
		    *pUseClip_ = nullptr, *pColor_ = nullptr, *pRectSize_ = nullptr, *pHalf_ = nullptr, *pRadius_ = nullptr,
		    *pSoftness_ = nullptr;
	std::unordered_map<uint64_t, Entry> textures_;
	uint64_t frame_ = 0;
};

} // namespace tonearm
