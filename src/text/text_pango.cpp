// SPDX-License-Identifier: GPL-2.0-or-later
// Pango/Cairo text backend. Used on Linux for tests, headless renders and documentation.
#include "core/text.hpp"

#include <cairo.h>
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace tonearm {

namespace {

class PangoTextEngine final : public TextEngine {
public:
	PangoTextEngine() { recreate(); }
	~PangoTextEngine() override
	{
		if (ctx_)
			g_object_unref(ctx_);
		if (map_)
			g_object_unref(map_);
	}

	bool addFontFile(const std::string &path) override
	{
		const bool ok = FcConfigAppFontAddFile(FcConfigGetCurrent(), reinterpret_cast<const FcChar8 *>(path.c_str())) == FcTrue;
		if (ok)
			recreate();
		return ok;
	}

	std::vector<std::string> families() override
	{
		std::vector<std::string> out;
		PangoFontFamily **fams = nullptr;
		int n = 0;
		pango_font_map_list_families(map_, &fams, &n);
		for (int i = 0; i < n; ++i)
			out.emplace_back(pango_font_family_get_name(fams[i]));
		g_free(fams);
		std::sort(out.begin(), out.end());
		return out;
	}

	TextMetrics measure(const std::string &utf8, const FontSpec &font) override
	{
		PangoLayout *layout = makeLayout(utf8, font, 0, false);
		TextMetrics m = metricsOf(layout);
		g_object_unref(layout);
		return m;
	}

	TextRun render(const std::string &utf8, const FontSpec &font, const Color &color, float maxWidth, bool ellipsize, int pad) override
	{
		TextRun run;
		run.pad = pad;
		PangoLayout *layout = makeLayout(utf8, font, maxWidth, ellipsize);
		run.metrics = metricsOf(layout);
		run.truncated = pango_layout_is_ellipsized(layout);

		PangoRectangle ink, logical;
		pango_layout_get_pixel_extents(layout, &ink, &logical);
		const int w = static_cast<int>(std::ceil(run.metrics.width)) + pad * 2 + 2;
		const int h = static_cast<int>(std::ceil(run.metrics.lineHeight)) + pad * 2;
		cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
		cairo_t *cr = cairo_create(surf);
		cairo_font_options_t *fo = fontOptions();
		pango_cairo_context_set_font_options(pango_layout_get_context(layout), fo);
		pango_layout_context_changed(layout);
		cairo_font_options_destroy(fo);

		cairo_set_source_rgba(cr, color.r, color.g, color.b, color.a);
		cairo_move_to(cr, pad - logical.x, pad - logical.y);
		pango_cairo_show_layout(cr, layout);
		cairo_destroy(cr);
		cairo_surface_flush(surf);

		// Cairo ARGB32 is premultiplied and stored as BGRA bytes on little-endian hosts.
		run.image = Image(w, h);
		const unsigned char *src = cairo_image_surface_get_data(surf);
		const int stride = cairo_image_surface_get_stride(surf);
		for (int y = 0; y < h; ++y)
			std::memcpy(run.image.row(y), src + static_cast<size_t>(y) * stride, static_cast<size_t>(w) * 4);
		cairo_surface_destroy(surf);
		g_object_unref(layout);
		return run;
	}

private:
	static cairo_font_options_t *fontOptions()
	{
		cairo_font_options_t *fo = cairo_font_options_create();
		cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
		cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_NONE);
		cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_OFF);
		return fo;
	}

	void recreate()
	{
		if (ctx_)
			g_object_unref(ctx_);
		if (map_)
			g_object_unref(map_);
		map_ = pango_cairo_font_map_new();
		ctx_ = pango_font_map_create_context(map_);
		cairo_font_options_t *fo = fontOptions();
		pango_cairo_context_set_font_options(ctx_, fo);
		cairo_font_options_destroy(fo);
		pango_cairo_context_set_resolution(ctx_, 96.0);
	}

	PangoLayout *makeLayout(const std::string &utf8, const FontSpec &font, float maxWidth, bool ellipsize)
	{
		PangoLayout *layout = pango_layout_new(ctx_);
		std::string text = utf8;
		if (font.uppercase) {
			gchar *up = g_utf8_strup(utf8.c_str(), -1);
			text = up;
			g_free(up);
		}
		// Single line: newlines from metadata become spaces.
		std::replace(text.begin(), text.end(), '\n', ' ');
		std::replace(text.begin(), text.end(), '\r', ' ');
		pango_layout_set_text(layout, text.c_str(), -1);
		pango_layout_set_single_paragraph_mode(layout, TRUE);

		PangoFontDescription *desc = pango_font_description_new();
		pango_font_description_set_family(desc, font.family.c_str());
		pango_font_description_set_weight(desc, static_cast<PangoWeight>(std::clamp(font.weight, 100, 900)));
		pango_font_description_set_style(desc, font.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
		pango_font_description_set_absolute_size(desc, font.sizePx * PANGO_SCALE);
		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);

		PangoAttrList *attrs = pango_attr_list_new();
		if (font.trackingEm != 0)
			pango_attr_list_insert(attrs, pango_attr_letter_spacing_new(static_cast<int>(font.trackingEm * font.sizePx * PANGO_SCALE)));
		if (font.tabularNumbers)
			pango_attr_list_insert(attrs, pango_attr_font_features_new("tnum=1"));
		pango_layout_set_attributes(layout, attrs);
		pango_attr_list_unref(attrs);

		if (maxWidth > 0 && ellipsize) {
			pango_layout_set_width(layout, static_cast<int>(maxWidth * PANGO_SCALE));
			pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		}
		return layout;
	}

	static TextMetrics metricsOf(PangoLayout *layout)
	{
		PangoRectangle ink, logical;
		pango_layout_get_extents(layout, &ink, &logical);
		TextMetrics m;
		m.width = static_cast<float>(logical.width) / PANGO_SCALE;
		m.lineHeight = static_cast<float>(logical.height) / PANGO_SCALE;
		m.ascent = static_cast<float>(pango_layout_get_baseline(layout)) / PANGO_SCALE;
		m.descent = m.lineHeight - m.ascent;
		return m;
	}

	PangoFontMap *map_ = nullptr;
	PangoContext *ctx_ = nullptr;
};

} // namespace

std::unique_ptr<TextEngine> createTextEngine()
{
	return std::make_unique<PangoTextEngine>();
}

} // namespace tonearm
