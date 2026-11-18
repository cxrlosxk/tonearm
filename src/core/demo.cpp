// SPDX-License-Identifier: GPL-2.0-or-later
#include "demo.hpp"

#include <cmath>

namespace tonearm {

namespace {

struct DemoEntry {
	const char *title;
	const char *artist;
	const char *album;
	double duration;
};

const DemoEntry kDemo[] = {
	{"Slow Light Over the Harbour", "Marlow Vance", "Coastline Sessions", 247},
	{"Paper Satellites", "The Quiet Arcade", "Static Bloom", 213},
	{"Mitternacht", "Ilse Okafor", "Night Transit", 296},
	{"Everything I Never Said on the Last Train Home (Extended Version)", "Juniper & the Low Tides", "Platform Nine", 384},
	{"\xE5\xA4\x9C\xE6\x98\x8E\xE3\x81\x91\xE3\x81\xAE\xE3\x82\xB7\xE3\x82\xB0\xE3\x83\x8A\xE3\x83\xAB", // 夜明けのシグナル
	 "\xE6\xB0\xB4\xE9\x87\x8E\xE3\x83\x8F\xE3\x83\xAB",                                           // 水野ハル
	 "Signal", 201},
};

constexpr int kCount = sizeof(kDemo) / sizeof(kDemo[0]);

void circle(Image &img, float cx, float cy, float r, const Color &c)
{
	fillRoundRect(img, {cx - r, cy - r, r * 2, r * 2}, r, Paint::solid(c));
}

Image coverSunset(int n)
{
	Image img(n, n);
	const float N = static_cast<float>(n);
	fillRoundRect(img, {0, 0, N, N}, 0,
		      Paint::linear({0, 0}, {0, N},
				    {{0, Color::hex("#1d1440")}, {0.45f, Color::hex("#7a2a62")}, {0.75f, Color::hex("#e2604a")}, {1, Color::hex("#f6b56b")}}));
	// Sun with horizontal cuts.
	Image sun(n, n);
	circle(sun, N * 0.5f, N * 0.56f, N * 0.27f,
	       Color::hex("#ffd27a"));
	fillRoundRect(sun, {0, 0, N, N}, 0,
		      Paint::linear({0, N * 0.3f}, {0, N * 0.83f}, {{0, Color::hex("#ffe29a00")}, {1, Color::hex("#ff7a5a66")}}));
	for (int i = 0; i < 6; ++i) {
		const float y = N * (0.6f + i * 0.04f);
		const float hgt = N * (0.006f + i * 0.004f);
		for (int yy = static_cast<int>(y); yy < static_cast<int>(y + hgt) && yy < n; ++yy)
			std::fill(sun.row(yy), sun.row(yy) + sun.stride(), 0);
	}
	composite(img, sun, 0, 0);
	fillRoundRect(img, {0, N * 0.83f, N, N * 0.17f}, 0, Paint::linear({0, N * 0.83f}, {0, N}, {{0, Color::hex("#2b1638")}, {1, Color::hex("#120a1f")}}));
	addNoise(img, 0.04f, 11);
	return img;
}

Image coverTeal(int n)
{
	Image img(n, n);
	const float N = static_cast<float>(n);
	img.fill(Color::hex("#0f2f33"));
	circle(img, N * 0.36f, N * 0.4f, N * 0.3f, Color::hex("#5fd3b8cc"));
	circle(img, N * 0.64f, N * 0.6f, N * 0.26f, Color::hex("#ff7f66b3"));
	circle(img, N * 0.7f, N * 0.28f, N * 0.08f, Color::hex("#f4f1e6"));
	fillRoundRect(img, {N * 0.1f, N * 0.86f, N * 0.8f, N * 0.012f}, 0, Paint::solid(Color::hex("#f4f1e6aa")));
	addNoise(img, 0.035f, 23);
	return img;
}

Image coverNight(int n)
{
	Image img(n, n);
	const float N = static_cast<float>(n);
	img.fill(Color::hex("#07070f"));
	fillRoundRect(img, {0, 0, N, N}, 0,
		      Paint::radial({N * 0.5f, N * 0.62f}, N * 0.6f, {{0, Color::hex("#c03aff99")}, {0.5f, Color::hex("#3b1a8a55")}, {1, Color::hex("#07070f00")}}));
	Image ring(n, n);
	strokeRoundRect(ring, {N * 0.26f, N * 0.36f, N * 0.48f, N * 0.48f}, N * 0.24f, N * 0.012f, Paint::solid(Color::hex("#ff6ad5")));
	Image ringGlow = ring;
	blur(ringGlow, N * 0.03f);
	composite(img, ringGlow, 0, 0);
	composite(img, ringGlow, 0, 0);
	composite(img, ring, 0, 0);
	for (int i = 0; i < 40; ++i) {
		const float x = std::fmod(i * 97.13f, 1.0f) * N, y = std::fmod(i * 41.7f + 0.3f, 0.5f) * N;
		circle(img, std::fmod(i * 0.6180339f, 1.0f) * N, std::fmod(i * 0.3819f, 0.55f) * N, N * 0.0025f, Color{1, 1, 1, 0.6f});
		(void)x;
		(void)y;
	}
	addNoise(img, 0.03f, 37);
	return img;
}

Image coverSwiss(int n)
{
	Image img(n, n);
	const float N = static_cast<float>(n);
	img.fill(Color::hex("#ece5d8"));
	fillRoundRect(img, {N * 0.12f, N * 0.12f, N * 0.5f, N * 0.62f}, 0, Paint::solid(Color::hex("#d8402f")));
	fillRoundRect(img, {N * 0.44f, N * 0.52f, N * 0.44f, N * 0.08f}, 0, Paint::solid(Color::hex("#1b1a18")));
	circle(img, N * 0.72f, N * 0.3f, N * 0.12f, Color::hex("#1b1a18"));
	fillRoundRect(img, {N * 0.12f, N * 0.84f, N * 0.28f, N * 0.02f}, 0, Paint::solid(Color::hex("#1b1a18")));
	addNoise(img, 0.03f, 41);
	return img;
}

Image coverDawn(int n)
{
	Image img(n, n);
	const float N = static_cast<float>(n);
	fillRoundRect(img, {0, 0, N, N}, 0,
		      Paint::linear({0, 0}, {N, N}, {{0, Color::hex("#ffb7c5")}, {0.5f, Color::hex("#c9b8ff")}, {1, Color::hex("#8fd3ff")}}));
	for (int i = 0; i < 5; ++i)
		strokeRoundRect(img, {N * (0.2f + i * 0.04f), N * (0.2f + i * 0.04f), N * (0.6f - i * 0.08f), N * (0.6f - i * 0.08f)},
				N * (0.3f - i * 0.04f), N * 0.004f, Paint::solid(Color{1, 1, 1, 0.55f}));
	addNoise(img, 0.025f, 53);
	return img;
}

} // namespace

int demoTrackCount()
{
	return kCount;
}

double demoDuration(int index)
{
	return kDemo[((index % kCount) + kCount) % kCount].duration;
}

Image demoArtwork(int index, int size)
{
	switch (((index % kCount) + kCount) % kCount) {
	case 0:
		return coverSunset(size);
	case 1:
		return coverTeal(size);
	case 2:
		return coverNight(size);
	case 3:
		return coverSwiss(size);
	default:
		return coverDawn(size);
	}
}

TrackInfo demoTrack(int index, int artSize, bool withArtwork)
{
	const DemoEntry &e = kDemo[((index % kCount) + kCount) % kCount];
	TrackInfo t;
	t.title = e.title;
	t.artist = e.artist;
	t.album = e.album;
	if (withArtwork) {
		auto img = std::make_shared<Image>(demoArtwork(index, artSize));
		t.artworkHash = hashBytes(img->data(), static_cast<size_t>(img->stride()) * img->height());
		t.artwork = std::move(img);
	}
	return t;
}

} // namespace tonearm
