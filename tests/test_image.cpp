// SPDX-License-Identifier: GPL-2.0-or-later
#include "check.hpp"

#include "core/image.hpp"

using namespace tonearm;

namespace {
struct Px {
	int b, g, r, a;
};
Px at(const Image &img, int x, int y)
{
	const uint8_t *p = img.row(y) + x * 4;
	return {p[0], p[1], p[2], p[3]};
}
bool premultipliedValid(const Image &img)
{
	for (int y = 0; y < img.height(); ++y)
		for (int x = 0; x < img.width(); ++x) {
			const Px p = at(img, x, y);
			if (p.r > p.a || p.g > p.a || p.b > p.a)
				return false;
		}
	return true;
}
} // namespace

TEST("image: fill stores premultiplied BGRA")
{
	Image img(4, 4);
	img.fill(Color{1, 0.5f, 0, 0.5f});
	const Px p = at(img, 1, 1);
	CHECK(p.a == 128 || p.a == 127);
	CHECK(p.r <= p.a);
	CHECK(p.b == 0);
	CHECK(premultipliedValid(img));
}

TEST("image: straight-alpha import premultiplies")
{
	const uint8_t rgba[4] = {255, 255, 255, 64};
	const Image img = Image::fromRgbaStraight(1, 1, rgba, 4);
	const Px p = at(img, 0, 0);
	CHECK(p.a == 64);
	CHECK(p.r == 64 && p.g == 64 && p.b == 64);
}

TEST("image: resize and blur keep a solid color solid")
{
	Image img(97, 61);
	img.fill(Color::hex("#3a6ea5"));
	const Px ref = at(img, 0, 0);
	for (const Image &out : {resize(img, 40, 25), resize(img, 300, 190), coverFit(img, 50, 50)}) {
		const Px p = at(out, out.width() / 2, out.height() / 2);
		CHECK(std::abs(p.r - ref.r) <= 1 && std::abs(p.g - ref.g) <= 1 && std::abs(p.b - ref.b) <= 1);
	}
	Image blurred = img;
	blur(blurred, 12);
	const Px c = at(blurred, 48, 30);
	CHECK(std::abs(c.r - ref.r) <= 1 && std::abs(c.a - 255) <= 1);
}

TEST("image: rounded mask clears corners and keeps the middle")
{
	Image img(100, 60);
	img.fill(Color::hex("#ffffff"));
	applyRoundedMask(img, 20);
	CHECK(at(img, 0, 0).a == 0);
	CHECK(at(img, 99, 59).a == 0);
	CHECK(at(img, 50, 30).a == 255);
	CHECK(at(img, 50, 0).a > 200); // straight edges stay solid
	CHECK(premultipliedValid(img));
}

TEST("image: compositing opaque over anything replaces it")
{
	Image dst(10, 10);
	dst.fill(Color::hex("#ff0000"));
	Image src(4, 4);
	src.fill(Color::hex("#0000ff"));
	composite(dst, src, 3, 3);
	CHECK(at(dst, 4, 4).b == 255 && at(dst, 4, 4).r == 0);
	CHECK(at(dst, 0, 0).r == 255);
	// Clipped placement must not write out of bounds.
	composite(dst, src, -2, 8);
	composite(dst, src, 9, -3);
	CHECK(at(dst, 0, 9).b == 255);
}

TEST("image: rounded rect fill is anti-aliased and premultiplied")
{
	Image img(80, 40);
	img.clear();
	fillRoundRect(img, Rect{10.5f, 5.5f, 60, 30}, 8, Paint::solid(Color::hex("#ffcc00")), 0.8f);
	CHECK(at(img, 40, 20).a > 190 && at(img, 40, 20).a < 215);
	CHECK(at(img, 2, 2).a == 0);
	bool partial = false;
	for (int x = 0; x < 80; ++x) {
		const int a = at(img, x, 20).a;
		partial |= a > 0 && a < 190;
	}
	CHECK_MSG(partial, "edge pixels should have partial coverage");
	CHECK(premultipliedValid(img));
}

TEST("image: drop shadow and glow stay premultiplied")
{
	Image img(120, 80);
	img.clear();
	dropShadow(img, Rect{20, 15, 80, 40}, 10, 14, 6, Color{0, 0, 0, 0.5f});
	CHECK(at(img, 60, 40).a > 0);
	CHECK(premultipliedValid(img));
	Image dot(10, 10);
	dot.fill(Color::hex("#ffffff"));
	glow(img, dot, 55, 35, 8, Color::hex("#ff00ff"), 0.8f);
	CHECK(premultipliedValid(img));
}
