// SPDX-License-Identifier: GPL-2.0-or-later
#include "color.hpp"
#include "image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tonearm {

namespace {
constexpr float kPi = 3.14159265358979f;

int hexDigit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}
} // namespace

Color Color::fromRgba8(uint32_t v)
{
	return {((v >> 24) & 0xff) / 255.0f, ((v >> 16) & 0xff) / 255.0f, ((v >> 8) & 0xff) / 255.0f, (v & 0xff) / 255.0f};
}

Color Color::fromObs(uint32_t v)
{
	return {(v & 0xff) / 255.0f, ((v >> 8) & 0xff) / 255.0f, ((v >> 16) & 0xff) / 255.0f, ((v >> 24) & 0xff) / 255.0f};
}

Color Color::hex(const char *s)
{
	if (!s)
		return {};
	if (*s == '#')
		++s;
	const size_t n = std::strlen(s);
	if (n != 6 && n != 8)
		return {};
	uint32_t v = 0;
	for (size_t i = 0; i < n; ++i) {
		const int d = hexDigit(s[i]);
		if (d < 0)
			return {};
		v = (v << 4) | static_cast<uint32_t>(d);
	}
	if (n == 6)
		v = (v << 8) | 0xffu;
	return fromRgba8(v);
}

static uint32_t to8(float v)
{
	return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

uint32_t Color::toObs() const
{
	return (to8(a) << 24) | (to8(b) << 16) | (to8(g) << 8) | to8(r);
}

std::string Color::toHex() const
{
	char buf[16];
	if (to8(a) == 255)
		std::snprintf(buf, sizeof buf, "#%02x%02x%02x", to8(r), to8(g), to8(b));
	else
		std::snprintf(buf, sizeof buf, "#%02x%02x%02x%02x", to8(r), to8(g), to8(b), to8(a));
	return buf;
}

Color Color::mix(const Color &o, float t) const
{
	return {r + (o.r - r) * t, g + (o.g - g) * t, b + (o.b - b) * t, a + (o.a - a) * t};
}

float srgbToLinear(float c)
{
	return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c)
{
	c = std::clamp(c, 0.0f, 1.0f);
	return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

Oklab toOklab(const Color &c)
{
	const float r = srgbToLinear(c.r), g = srgbToLinear(c.g), b = srgbToLinear(c.b);
	const float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
	const float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
	const float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
	return {0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
		1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
		0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s};
}

Color fromOklab(const Oklab &lab, float alpha)
{
	const float l_ = lab.L + 0.3963377774f * lab.a + 0.2158037573f * lab.b;
	const float m_ = lab.L - 0.1055613458f * lab.a - 0.0638541728f * lab.b;
	const float s_ = lab.L - 0.0894841775f * lab.a - 1.2914855480f * lab.b;
	const float l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
	return {linearToSrgb(4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s),
		linearToSrgb(-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s),
		linearToSrgb(-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s), alpha};
}

Oklch toOklch(const Oklab &lab)
{
	return {lab.L, std::sqrt(lab.a * lab.a + lab.b * lab.b), std::atan2(lab.b, lab.a)};
}

Oklab fromOklch(const Oklch &lch)
{
	return {lch.L, lch.C * std::cos(lch.h), lch.C * std::sin(lch.h)};
}

float relativeLuminance(const Color &c)
{
	return 0.2126f * srgbToLinear(c.r) + 0.7152f * srgbToLinear(c.g) + 0.0722f * srgbToLinear(c.b);
}

float contrastRatio(const Color &fg, const Color &bg)
{
	const float a = relativeLuminance(fg), b = relativeLuminance(bg);
	return (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f);
}

Color ensureContrast(const Color &fg, const Color &bg, float minRatio)
{
	if (contrastRatio(fg, bg) >= minRatio)
		return fg;
	Oklch lch = toOklch(toOklab(fg));
	const bool goLighter = relativeLuminance(bg) < 0.18f;
	for (int i = 0; i < 40; ++i) {
		lch.L = std::clamp(lch.L + (goLighter ? 0.025f : -0.025f), 0.0f, 1.0f);
		lch.C *= 0.97f; // reduce chroma slightly so we stay inside sRGB gamut
		Color c = fromOklab(fromOklch(lch), fg.a);
		if (contrastRatio(c, bg) >= minRatio)
			return c;
	}
	return goLighter ? Color{1, 1, 1, fg.a} : Color{0, 0, 0, fg.a};
}

namespace {

struct Sample {
	Oklab lab;
	float weight;
};

float dist2(const Oklab &a, const Oklab &b)
{
	const float dL = a.L - b.L, da = a.a - b.a, db = a.b - b.b;
	return dL * dL + da * da + db * db;
}

Oklab clampToUsable(Oklab lab, float minL, float maxL)
{
	lab.L = std::clamp(lab.L, minL, maxL);
	return lab;
}

} // namespace

Palette extractPalette(const Image &art)
{
	Palette pal;
	if (art.empty())
		return pal;

	// Work on a small thumbnail: 24x24 is enough for color statistics and is cheap.
	const Image thumb = resize(art, 24, 24);
	std::vector<Sample> samples;
	samples.reserve(24 * 24);
	for (int y = 0; y < thumb.height(); ++y) {
		const uint8_t *p = thumb.row(y);
		for (int x = 0; x < thumb.width(); ++x, p += 4) {
			const float a = p[3] / 255.0f;
			if (a < 0.5f)
				continue;
			Color c{p[2] / 255.0f / a, p[1] / 255.0f / a, p[0] / 255.0f / a, 1};
			samples.push_back({toOklab(c), 1.0f});
		}
	}
	if (samples.empty())
		return pal;

	// Deterministic k-means with farthest-point initialization.
	constexpr int K = 5;
	std::array<Oklab, K> centers{};
	centers[0] = samples[samples.size() / 2].lab;
	int k = 1;
	for (; k < K; ++k) {
		float best = -1;
		size_t bestIdx = 0;
		for (size_t i = 0; i < samples.size(); ++i) {
			float d = 1e9f;
			for (int j = 0; j < k; ++j)
				d = std::min(d, dist2(samples[i].lab, centers[j]));
			if (d > best) {
				best = d;
				bestIdx = i;
			}
		}
		if (best < 1e-5f)
			break;
		centers[k] = samples[bestIdx].lab;
	}

	std::array<float, K> weights{};
	std::vector<int> assign(samples.size(), 0);
	for (int iter = 0; iter < 8; ++iter) {
		for (size_t i = 0; i < samples.size(); ++i) {
			float best = 1e9f;
			for (int j = 0; j < k; ++j) {
				const float d = dist2(samples[i].lab, centers[j]);
				if (d < best) {
					best = d;
					assign[i] = j;
				}
			}
		}
		std::array<Oklab, K> sum{};
		weights.fill(0);
		for (size_t i = 0; i < samples.size(); ++i) {
			const int j = assign[i];
			sum[j].L += samples[i].lab.L;
			sum[j].a += samples[i].lab.a;
			sum[j].b += samples[i].lab.b;
			weights[j] += samples[i].weight;
		}
		for (int j = 0; j < k; ++j) {
			if (weights[j] > 0)
				centers[j] = {sum[j].L / weights[j], sum[j].a / weights[j], sum[j].b / weights[j]};
		}
	}

	// Dominant: largest cluster.
	int dom = 0;
	for (int j = 1; j < k; ++j)
		if (weights[j] > weights[dom])
			dom = j;

	// Accent: score clusters by chroma, with a mild population term and a penalty for extremes of lightness.
	const float total = static_cast<float>(samples.size());
	int acc = dom;
	float bestScore = -1;
	for (int j = 0; j < k; ++j) {
		if (weights[j] <= 0)
			continue;
		const Oklch lch = toOklch(centers[j]);
		const float pop = std::sqrt(weights[j] / total);
		const float lightPenalty = 1.0f - std::min(1.0f, std::fabs(lch.L - 0.68f) * 1.4f);
		const float score = lch.C * (0.35f + pop) * (0.3f + lightPenalty);
		if (score > bestScore) {
			bestScore = score;
			acc = j;
		}
	}

	// Secondary: farthest remaining cluster from the accent with some population.
	int sec = dom == acc ? -1 : dom;
	float far = -1;
	for (int j = 0; j < k; ++j) {
		if (j == acc || weights[j] < total * 0.04f)
			continue;
		const float d = dist2(centers[j], centers[acc]);
		if (d > far) {
			far = d;
			sec = j;
		}
	}
	if (sec < 0)
		sec = dom;

	Oklch accent = toOklch(centers[acc]);
	// Make the accent readable on dark surfaces and vivid without neon clipping.
	accent.L = std::clamp(accent.L, 0.62f, 0.84f);
	if (accent.C < 0.04f) {
		// Monochrome artwork: use a neutral, slightly warm accent instead of inventing a hue.
		accent.C = 0.02f;
		accent.h = 1.2f;
		accent.L = 0.86f;
	} else {
		accent.C = std::min(accent.C * 1.15f, 0.20f);
	}

	Oklch deep = toOklch(centers[dom]);
	deep.L = std::clamp(deep.L * 0.42f, 0.12f, 0.24f);
	deep.C = std::min(deep.C, 0.06f);

	pal.dominant = fromOklab(clampToUsable(centers[dom], 0.08f, 0.92f));
	pal.accent = fromOklab(fromOklch(accent));
	pal.secondary = fromOklab(clampToUsable(centers[sec], 0.10f, 0.90f));
	pal.deep = fromOklab(fromOklch(deep));
	pal.valid = true;
	return pal;
}

Palette fallbackPalette(const Color &seed)
{
	Palette pal;
	Oklch base = toOklch(toOklab(seed));
	Oklch accent = base;
	accent.L = std::clamp(accent.L, 0.66f, 0.82f);
	Oklch deep = base;
	deep.L = 0.17f;
	deep.C = std::min(deep.C, 0.03f);
	Oklch sec = base;
	sec.h += kPi / 5.0f;
	sec.L = 0.45f;
	pal.accent = fromOklab(fromOklch(accent));
	pal.dominant = fromOklab(fromOklch({0.32f, std::min(base.C, 0.05f), base.h}));
	pal.secondary = fromOklab(fromOklch(sec));
	pal.deep = fromOklab(fromOklch(deep));
	pal.valid = true;
	return pal;
}

} // namespace tonearm
