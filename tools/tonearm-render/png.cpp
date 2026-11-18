// SPDX-License-Identifier: GPL-2.0-or-later
#include "png.hpp"

#include <cairo.h>
#include <cstring>

namespace tonearm {

bool writePng(const Image &img, const std::string &path)
{
	const int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, img.width());
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, img.width(), img.height());
	unsigned char *dst = cairo_image_surface_get_data(s);
	for (int y = 0; y < img.height(); ++y)
		std::memcpy(dst + static_cast<size_t>(y) * stride, img.row(y), static_cast<size_t>(img.width()) * 4);
	cairo_surface_mark_dirty(s);
	const bool ok = cairo_surface_write_to_png(s, path.c_str()) == CAIRO_STATUS_SUCCESS;
	cairo_surface_destroy(s);
	return ok;
}

bool readPng(const std::string &path, Image &out)
{
	cairo_surface_t *s = cairo_image_surface_create_from_png(path.c_str());
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(s);
		return false;
	}
	cairo_surface_t *argb = s;
	if (cairo_image_surface_get_format(s) != CAIRO_FORMAT_ARGB32) {
		argb = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cairo_image_surface_get_width(s), cairo_image_surface_get_height(s));
		cairo_t *cr = cairo_create(argb);
		cairo_set_source_surface(cr, s, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
	}
	cairo_surface_flush(argb);
	const int w = cairo_image_surface_get_width(argb), h = cairo_image_surface_get_height(argb);
	const int stride = cairo_image_surface_get_stride(argb);
	out = Image(w, h);
	const unsigned char *src = cairo_image_surface_get_data(argb);
	for (int y = 0; y < h; ++y)
		std::memcpy(out.row(y), src + static_cast<size_t>(y) * stride, static_cast<size_t>(w) * 4);
	if (argb != s)
		cairo_surface_destroy(argb);
	cairo_surface_destroy(s);
	return true;
}

} // namespace tonearm
