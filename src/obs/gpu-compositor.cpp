// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu-compositor.hpp"

#include <obs-module.h>
#include <graphics/matrix4.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cmath>

namespace tonearm {

GpuCompositor::~GpuCompositor()
{
	if (effect_ || !textures_.empty()) {
		obs_enter_graphics();
		releaseAll();
		obs_leave_graphics();
	}
}

bool GpuCompositor::init(const char *effectPath)
{
	if (effect_)
		return true;
	char *errors = nullptr;
	effect_ = gs_effect_create_from_file(effectPath, &errors);
	if (!effect_) {
		blog(LOG_ERROR, "[tonearm] failed to load effect %s: %s", effectPath, errors ? errors : "(no details)");
		bfree(errors);
		return false;
	}
	bfree(errors);
	techLayer_ = gs_effect_get_technique(effect_, "Layer");
	techRect_ = gs_effect_get_technique(effect_, "RoundRect");
	pImage_ = gs_effect_get_param_by_name(effect_, "image");
	pOpacity_ = gs_effect_get_param_by_name(effect_, "opacity");
	pDst_ = gs_effect_get_param_by_name(effect_, "dst_rect");
	pClip_ = gs_effect_get_param_by_name(effect_, "clip_rect");
	pFade_ = gs_effect_get_param_by_name(effect_, "fade");
	pUseClip_ = gs_effect_get_param_by_name(effect_, "use_clip");
	pColor_ = gs_effect_get_param_by_name(effect_, "color");
	pRectSize_ = gs_effect_get_param_by_name(effect_, "rect_size");
	pHalf_ = gs_effect_get_param_by_name(effect_, "half_size");
	pRadius_ = gs_effect_get_param_by_name(effect_, "radius");
	pSoftness_ = gs_effect_get_param_by_name(effect_, "softness");
	techVideo_ = gs_effect_get_technique(effect_, "Video");
	pVRadius_ = gs_effect_get_param_by_name(effect_, "v_radius");
	pVClip_ = gs_effect_get_param_by_name(effect_, "v_clip");
	pVClipRadius_ = gs_effect_get_param_by_name(effect_, "v_clip_radius");
	pVFeather_ = gs_effect_get_param_by_name(effect_, "v_feather");
	pVideoNeedsSrgbEncode_ = gs_effect_get_param_by_name(effect_, "video_needs_srgb_encode");
	return techLayer_ && techRect_;
}

void GpuCompositor::releaseAll()
{
	for (auto &kv : textures_)
		gs_texture_destroy(kv.second.tex);
	textures_.clear();
	if (effect_) {
		gs_effect_destroy(effect_);
		effect_ = nullptr;
	}
}

gs_texture_t *GpuCompositor::textureFor(const DrawOp &op)
{
	auto it = textures_.find(op.imageId);
	if (it != textures_.end()) {
		it->second.lastFrame = frame_;
		return it->second.tex;
	}
	const Image &img = *op.image;
	const uint8_t *data = img.data();
	gs_texture_t *tex = gs_texture_create(static_cast<uint32_t>(img.width()), static_cast<uint32_t>(img.height()), GS_BGRA, 1, &data, 0);
	if (!tex)
		return nullptr;
	textures_[op.imageId] = {tex, frame_};
	return tex;
}

void GpuCompositor::collect()
{
	// Layers are replaced on track changes; drop textures nobody drew for a few frames.
	for (auto it = textures_.begin(); it != textures_.end();) {
		if (frame_ - it->second.lastFrame > 3) {
			gs_texture_destroy(it->second.tex);
			it = textures_.erase(it);
		} else {
			++it;
		}
	}
}

void GpuCompositor::drawImage(const DrawOp &op, gs_texture_t *tex)
{
	gs_effect_set_texture(pImage_, tex);
	gs_effect_set_float(pOpacity_, op.opacity);
	struct vec4 dst;
	vec4_set(&dst, op.dst.x, op.dst.y, op.dst.w, op.dst.h);
	gs_effect_set_vec4(pDst_, &dst);
	gs_effect_set_float(pUseClip_, op.clip ? 1.0f : 0.0f);
	// Always set every parameter: libobs warns about (and skips) parameters that
	// were never given a value, even when the shader branch ignores them.
	struct vec4 clip;
	struct vec2 fade;
	if (op.clip) {
		vec4_set(&clip, op.clipRect.x, op.clipRect.y, op.clipRect.right(), op.clipRect.bottom());
		vec2_set(&fade, op.fadeLeft, op.fadeRight);
	} else {
		vec4_zero(&clip);
		vec2_zero(&fade);
	}
	gs_effect_set_vec4(pClip_, &clip);
	gs_effect_set_vec2(pFade_, &fade);

	gs_matrix_push();
	if (std::fabs(op.rotation) > 0.0001f) {
		const float ox = op.dst.w * op.rotationOrigin.x;
		const float oy = op.dst.h * op.rotationOrigin.y;
		gs_matrix_translate3f(op.dst.x + ox, op.dst.y + oy, 0.0f);
		gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, op.rotation);
		gs_matrix_translate3f(-ox, -oy, 0.0f);
	} else {
		gs_matrix_translate3f(op.dst.x, op.dst.y, 0.0f);
	}
	gs_technique_begin(techLayer_);
	gs_technique_begin_pass(techLayer_, 0);
	gs_draw_sprite(tex, 0, static_cast<uint32_t>(std::max(1.0f, std::round(op.dst.w))),
		       static_cast<uint32_t>(std::max(1.0f, std::round(op.dst.h))));
	gs_technique_end_pass(techLayer_);
	gs_technique_end(techLayer_);
	gs_matrix_pop();
}

void GpuCompositor::drawRoundRect(const DrawOp &op)
{
	if (op.color.a <= 0.0f || op.rect.empty())
		return;
	// Expand the quad so soft glows have room to fall off.
	const float grow = op.softness > 0 ? op.softness * 2.0f : 1.0f;
	const float qw = op.rect.w + grow * 2, qh = op.rect.h + grow * 2;

	struct vec4 c;
	vec4_set(&c, op.color.r * op.color.a, op.color.g * op.color.a, op.color.b * op.color.a, op.color.a);
	gs_effect_set_vec4(pColor_, &c);
	struct vec2 size, half;
	vec2_set(&size, qw, qh);
	vec2_set(&half, op.rect.w * 0.5f, op.rect.h * 0.5f);
	gs_effect_set_vec2(pRectSize_, &size);
	gs_effect_set_vec2(pHalf_, &half);
	gs_effect_set_float(pRadius_, std::min(op.radius, std::min(op.rect.w, op.rect.h) * 0.5f));
	gs_effect_set_float(pSoftness_, op.softness);

	gs_matrix_push();
	gs_matrix_translate3f(op.rect.x - grow, op.rect.y - grow, 0.0f);
	gs_technique_begin(techRect_);
	gs_technique_begin_pass(techRect_, 0);
	gs_draw_sprite(nullptr, 0, static_cast<uint32_t>(std::ceil(qw)), static_cast<uint32_t>(std::ceil(qh)));
	gs_technique_end_pass(techRect_);
	gs_technique_end(techRect_);
	gs_matrix_pop();
}

void GpuCompositor::drawVideo(const DrawOp &op, gs_texture_t *tex, bool needsSrgbEncode)
{
	if (!techVideo_ || op.opacity <= 0.0f || op.dst.empty())
		return;
	gs_effect_set_texture(pImage_, tex);
	gs_effect_set_float(pOpacity_, op.opacity);
	if (pVideoNeedsSrgbEncode_)
		gs_effect_set_float(pVideoNeedsSrgbEncode_, needsSrgbEncode ? 1.0f : 0.0f);
	struct vec4 dst, clip;
	vec4_set(&dst, op.dst.x, op.dst.y, op.dst.w, op.dst.h);
	gs_effect_set_vec4(pDst_, &dst);
	vec4_set(&clip, op.clipRect.x, op.clipRect.y, op.clipRect.right(), op.clipRect.bottom());
	gs_effect_set_vec4(pVClip_, &clip);
	gs_effect_set_float(pVRadius_, op.radius);
	gs_effect_set_float(pVClipRadius_, op.clipRadius);
	struct vec2 feather;
	vec2_set(&feather, static_cast<float>(op.featherEdge), op.feather);
	gs_effect_set_vec2(pVFeather_, &feather);
	struct vec4 tint;
	vec4_set(&tint, op.color.r, op.color.g, op.color.b, op.color.a);
	gs_effect_set_vec4(pColor_, &tint);

	gs_matrix_push();
	gs_matrix_translate3f(op.dst.x, op.dst.y, 0.0f);
	gs_technique_begin(techVideo_);
	gs_technique_begin_pass(techVideo_, 0);
	gs_draw_sprite(tex, 0, static_cast<uint32_t>(std::max(1.0f, std::round(op.dst.w))),
		       static_cast<uint32_t>(std::max(1.0f, std::round(op.dst.h))));
	gs_technique_end_pass(techVideo_);
	gs_technique_end(techVideo_);
	gs_matrix_pop();
}

void GpuCompositor::render(const std::vector<DrawOp> &ops, gs_texture_t *video, bool videoNeedsSrgbEncode)
{
	++frame_;
	if (!effect_)
		return;

	// Layers are premultiplied and authored in sRGB space, like the CPU reference.
	const bool previousSrgb = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(false);
	gs_blend_state_push();
	gs_blend_function_separate(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	for (const DrawOp &op : ops) {
		if (op.kind == DrawOp::Kind::Image) {
			if (!op.image || op.image->empty() || op.opacity <= 0.0f || (op.needsVideo && !video))
				continue;
			if (gs_texture_t *tex = textureFor(op))
				drawImage(op, tex);
		} else if (op.kind == DrawOp::Kind::Video) {
			if (video)
				drawVideo(op, video, videoNeedsSrgbEncode);
		} else {
			drawRoundRect(op);
		}
	}

	gs_blend_state_pop();
	gs_enable_framebuffer_srgb(previousSrgb);
	collect();
}

} // namespace tonearm
