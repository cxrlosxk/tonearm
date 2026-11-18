// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tonearm {

class Image;

// Straight (non-premultiplied) sRGB color with components in 0..1.
struct Color {
	float r = 0, g = 0, b = 0, a = 1;

	static Color fromRgba8(uint32_t rgba);         // 0xRRGGBBAA
	static Color fromObs(uint32_t abgr);           // OBS stores colors as 0xAABBGGRR
	static Color hex(const char *s);               // "#rrggbb" or "#rrggbbaa"
	uint32_t toObs() const;
	std::string toHex() const;

	Color withAlpha(float alpha) const { return {r, g, b, alpha}; }
	Color mix(const Color &o, float t) const;
	bool operator==(const Color &o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
	bool operator!=(const Color &o) const { return !(*this == o); }
};

// Perceptual color space used for palette work.
struct Oklab {
	float L = 0, a = 0, b = 0;
};

struct Oklch {
	float L = 0, C = 0, h = 0; // h in radians
};

float srgbToLinear(float c);
float linearToSrgb(float c);
Oklab toOklab(const Color &c);
Color fromOklab(const Oklab &lab, float alpha = 1.0f);
Oklch toOklch(const Oklab &lab);
Oklab fromOklch(const Oklch &lch);

// WCAG 2.x relative luminance and contrast ratio.
float relativeLuminance(const Color &c);
float contrastRatio(const Color &fg, const Color &bg);

// Lightens or darkens `fg` in Oklab until it reaches `minRatio` against `bg`.
Color ensureContrast(const Color &fg, const Color &bg, float minRatio);

struct Palette {
	Color dominant;   // largest perceptual cluster, used for backdrops
	Color accent;     // most vivid usable color, used for progress and highlights
	Color secondary;  // second distinct color, used for gradient endpoints
	Color deep;       // dark tone derived from the artwork for card surfaces
	bool valid = false;
};

// Extracts a palette from artwork. Deterministic for the same input.
Palette extractPalette(const Image &art);

// A tasteful fallback palette when there is no artwork.
Palette fallbackPalette(const Color &seed);

} // namespace tonearm
