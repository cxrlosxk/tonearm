// SPDX-License-Identifier: GPL-2.0-or-later
#include "style.hpp"

#include <algorithm>
#include <cmath>

namespace tonearm {

using namespace tokens;

const std::vector<PresetInfo> &presetList()
{
	static const std::vector<PresetInfo> list = {
		{"glass", "Glass"},
		{"minimal", "Minimal"},
		{"neon", "Neon"},
		{"editorial", "Editorial"},
	};
	return list;
}

const char *layoutId(LayoutMode m)
{
	switch (m) {
	case LayoutMode::CompactHorizontal:
		return "compact";
	case LayoutMode::ExpandedHorizontal:
		return "expanded";
	case LayoutMode::Square:
		return "square";
	case LayoutMode::Vertical:
		return "vertical";
	}
	return "expanded";
}

LayoutMode layoutFromId(const std::string &id)
{
	if (id == "compact")
		return LayoutMode::CompactHorizontal;
	if (id == "square")
		return LayoutMode::Square;
	if (id == "vertical")
		return LayoutMode::Vertical;
	return LayoutMode::ExpandedHorizontal;
}

const char *compositionId(Composition c)
{
	switch (c) {
	case Composition::Glass:
		return "glass";
	case Composition::Minimal:
		return "minimal";
	case Composition::Neon:
		return "neon";
	case Composition::Editorial:
		return "editorial";
	}
	return "glass";
}

Composition compositionFromId(const std::string &id)
{
	if (id == "minimal")
		return Composition::Minimal;
	if (id == "neon")
		return Composition::Neon;
	if (id == "editorial")
		return Composition::Editorial;
	return Composition::Glass;
}

void defaultSize(Composition c, LayoutMode layout, int &w, int &h)
{
	switch (layout) {
	case LayoutMode::CompactHorizontal:
		w = c == Composition::Minimal ? 420 : 460;
		h = c == Composition::Minimal ? 88 : 100;
		if (c == Composition::Editorial) {
			w = 480;
			h = 108;
		}
		break;
	case LayoutMode::ExpandedHorizontal:
		w = 600;
		h = c == Composition::Editorial ? 212 : 196;
		if (c == Composition::Minimal)
			h = 164;
		break;
	case LayoutMode::Square:
		w = h = 360;
		break;
	case LayoutMode::Vertical:
		w = 300;
		h = c == Composition::Editorial ? 500 : 460;
		break;
	}
}

static Style glass()
{
	Style s;
	s.presetId = "glass";
	s.composition = Composition::Glass;
	s.cornerRadius = radiusLg;
	s.backdropBlur = 1.0f;
	s.shadowStrength = 0.55f;
	s.borderOpacity = 0.16f;
	s.artworkRadius = radiusMd;
	s.artworkShadow = 0.65f;
	s.titleFont = {"Inter Display", 600, false, typeHeadline, -0.012f, false, false};
	s.artistFont = {"Inter", 500, false, typeBody, -0.004f, false, false};
	s.albumFont = {"Inter", 400, false, typeSmall, 0.0f, false, false};
	s.timeFont = {"Inter", 500, false, typeCaption, 0.02f, true, false};
	s.titleColor = Color::hex("#fdfbf8");
	s.artistColor = Color::hex("#fdfbf8d0");
	s.albumColor = Color::hex("#fdfbf890");
	s.progressHeight = 4;
	s.noise = 0.014f;
	return s;
}

static Style minimal()
{
	Style s;
	s.presetId = "minimal";
	s.composition = Composition::Minimal;
	s.cornerRadius = radiusSm;
	s.surfaceOpacity = 0.9f;
	s.backdropBlur = 0;
	s.shadowStrength = 0.25f;
	s.borderOpacity = 0.0f;
	s.artworkBackground = false;
	s.surfaceColor = Color::hex("#0f0f0f");
	s.artworkRadius = radiusXs;
	s.artworkShadow = 0.0f;
	s.titleFont = {"Inter", 500, false, typeTitle, -0.008f, false, false};
	s.artistFont = {"Inter", 400, false, typeSmall, 0.0f, false, false};
	s.albumFont = {"Inter", 400, false, typeSmall, 0.0f, false, false};
	s.timeFont = {"Inter", 400, false, typeCaption, 0.03f, true, false};
	s.titleColor = Color::hex("#f4f4f4");
	s.artistColor = Color::hex("#f4f4f499");
	s.albumColor = Color::hex("#f4f4f466");
	s.accentMode = AccentMode::Custom;
	s.accentColor = Color::hex("#f4f4f4");
	s.progressHeight = 2;
	s.motionIntensity = 0.5f;
	s.noise = 0;
	s.times = TimeDisplay::Remaining;
	return s;
}

static Style neon()
{
	Style s;
	s.presetId = "neon";
	s.composition = Composition::Neon;
	s.cornerRadius = radiusMd + 2;
	s.backdropBlur = 0.0f;
	s.shadowStrength = 0.5f;
	s.borderOpacity = 0.8f;
	s.glowStrength = 0.65f;
	s.surfaceColor = Color::hex("#07070d");
	s.artworkRadius = radiusSm;
	s.artworkShadow = 0.0f;
	s.titleFont = {"IBM Plex Sans", 600, false, typeHeadline - 1, 0.0f, false, false};
	s.artistFont = {"IBM Plex Sans", 500, false, typeBody, 0.01f, false, false};
	s.albumFont = {"IBM Plex Sans", 400, false, typeSmall, 0.01f, false, false};
	s.timeFont = {"IBM Plex Mono", 500, false, typeCaption, 0.02f, true, false};
	s.titleColor = Color::hex("#f6f4ff");
	s.artistColor = Color::hex("#f6f4ffcc");
	s.albumColor = Color::hex("#f6f4ff80");
	s.progressHeight = 3;
	s.progressGlow = 0.8f;
	s.noise = 0.01f;
	return s;
}

static Style editorial()
{
	Style s;
	s.presetId = "editorial";
	s.composition = Composition::Editorial;
	s.cornerRadius = radiusLg + 2;
	s.backdropBlur = 0.35f;
	s.shadowStrength = 0.6f;
	s.borderOpacity = 0.08f;
	s.artworkRadius = 0; // full-bleed artwork follows the card's own corners
	s.artworkShadow = 0.0f;
	s.titleFont = {"EB Garamond", 400, false, typeDisplay, -0.01f, false, false};
	s.artistFont = {"Inter", 600, false, typeCaption, 0.1f, false, true};
	s.albumFont = {"EB Garamond", 400, true, typeBody + 1, 0.0f, false, false};
	s.timeFont = {"Inter", 500, false, typeCaption, 0.04f, true, false};
	s.titleColor = Color::hex("#fbf6ee");
	s.artistColor = Color::hex("#fbf6eed9");
	s.albumColor = Color::hex("#fbf6eea6");
	s.progressHeight = 2;
	s.noise = 0.018f;
	return s;
}

Style makePreset(const std::string &id, LayoutMode layout)
{
	Style s;
	if (id == "minimal")
		s = minimal();
	else if (id == "neon")
		s = neon();
	else if (id == "editorial")
		s = editorial();
	else
		s = glass();
	s.layout = layout;
	defaultSize(s.composition, layout, s.width, s.height);

	// Small layouts drop secondary information instead of shrinking it into illegibility.
	if (layout == LayoutMode::CompactHorizontal) {
		s.showAlbum = false;
		s.times = TimeDisplay::Hidden;
		s.titleFont.sizePx = std::round(s.titleFont.sizePx * 0.82f);
		s.artistFont.sizePx = std::round(s.artistFont.sizePx * 0.92f);
		s.progressHeight = std::min(s.progressHeight, 3.0f);
	}
	return s;
}

} // namespace tonearm
