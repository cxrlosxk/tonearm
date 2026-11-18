// SPDX-License-Identifier: GPL-2.0-or-later
#include "check.hpp"

#include "core/color.hpp"
#include "core/demo.hpp"
#include "core/image.hpp"

#include <random>

using namespace tonearm;

TEST("color: hex parsing and formatting round-trip")
{
	CHECK(Color::hex("#ff8000").toHex() == "#ff8000");
	CHECK(Color::hex("#11223344").toHex() == "#11223344");
	const Color c = Color::hex("#336699");
	CHECK_NEAR(c.r, 0x33 / 255.0, 1e-6);
	CHECK_NEAR(c.b, 0x99 / 255.0, 1e-6);
	CHECK_NEAR(c.a, 1.0, 1e-6);
}

TEST("color: OBS ABGR packing round-trips")
{
	const uint32_t v = 0x80112233; // a=0x80 b=0x11 g=0x22 r=0x33
	const Color c = Color::fromObs(v);
	CHECK_NEAR(c.r, 0x33 / 255.0, 1e-6);
	CHECK_NEAR(c.b, 0x11 / 255.0, 1e-6);
	CHECK(c.toObs() == v);
}

TEST("color: Oklab conversion is reversible across sRGB")
{
	std::mt19937 rng(7);
	std::uniform_real_distribution<float> d(0, 1);
	float worst = 0;
	for (int i = 0; i < 2000; ++i) {
		const Color c{d(rng), d(rng), d(rng), 1};
		const Color back = fromOklab(toOklab(c));
		worst = std::max({worst, std::fabs(back.r - c.r), std::fabs(back.g - c.g), std::fabs(back.b - c.b)});
	}
	CHECK_MSG(worst < 0.5f / 255.0f, "max channel error " + std::to_string(worst * 255) + "/255");
}

TEST("color: WCAG contrast matches reference values")
{
	CHECK_NEAR(contrastRatio(Color::hex("#ffffff"), Color::hex("#000000")), 21.0, 0.01);
	CHECK_NEAR(contrastRatio(Color::hex("#777777"), Color::hex("#ffffff")), 4.48, 0.02);
	CHECK_NEAR(contrastRatio(Color::hex("#000000"), Color::hex("#000000")), 1.0, 1e-6);
}

TEST("color: ensureContrast always reaches the requested ratio")
{
	std::mt19937 rng(11);
	std::uniform_real_distribution<float> d(0, 1);
	for (int i = 0; i < 3000; ++i) {
		const Color fg{d(rng), d(rng), d(rng), 1};
		const Color bg{d(rng), d(rng), d(rng), 1};
		const Color out = ensureContrast(fg, bg, 4.5f);
		if (contrastRatio(out, bg) < 4.49f) {
			CHECK_MSG(false, fg.toHex() + " on " + bg.toHex() + " -> " + out.toHex());
			break;
		}
	}
	// Already readable colors are left alone.
	const Color white = Color::hex("#ffffff");
	CHECK(ensureContrast(white, Color::hex("#101010"), 4.5f) == white);
}

TEST("color: palette extraction finds a vivid accent and a dark surface tone")
{
	for (int i = 0; i < demoTrackCount(); ++i) {
		const Image art = demoArtwork(i, 256);
		const Palette p = extractPalette(art);
		CHECK(p.valid);
		const Oklch accent = toOklch(toOklab(p.accent));
		const Oklch deep = toOklch(toOklab(p.deep));
		CHECK_MSG(deep.L < accent.L, "artwork " + std::to_string(i));
		CHECK_MSG(deep.L < 0.4f, "deep tone should work as a card surface, artwork " + std::to_string(i));
	}
}

TEST("color: palette extraction copes with flat and transparent artwork")
{
	Image gray(64, 64);
	gray.fill(Color::hex("#808080"));
	const Palette p = extractPalette(gray);
	CHECK(p.valid);

	Image clear(64, 64);
	clear.clear();
	const Palette q = extractPalette(clear);
	(void)q; // must not crash; validity is up to the extractor
	const Palette f = fallbackPalette(Color::hex("#3366ff"));
	CHECK(f.valid);
}
