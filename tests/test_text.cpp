// SPDX-License-Identifier: GPL-2.0-or-later
// Checks on the platform text backend (Pango on Linux, DirectWrite on Windows).
#include "check.hpp"
#include "support.hpp"

#include <algorithm>
#include <string>

using namespace tonearm;

namespace {
// Strongest alpha in each row of a rendered run.
std::vector<int> rowPeaks(const Image &img)
{
	std::vector<int> peaks(static_cast<size_t>(img.height()), 0);
	for (int y = 0; y < img.height(); ++y) {
		const uint8_t *r = img.row(y);
		for (int x = 0; x < img.width(); ++x)
			peaks[static_cast<size_t>(y)] = std::max<int>(peaks[static_cast<size_t>(y)], r[x * 4 + 3]);
	}
	return peaks;
}

// True when the ink of a lone "i" has a lighter band between the dot and the stem.
bool dotSeparated(const Image &img)
{
	const auto peaks = rowPeaks(img);
	const int top = *std::max_element(peaks.begin(), peaks.end());
	const int ink = top / 2, gap = top * 6 / 10;
	size_t y = 0;
	while (y < peaks.size() && peaks[y] < ink)
		++y; // above the dot
	while (y < peaks.size() && peaks[y] >= ink)
		++y; // through the dot
	bool sawGap = false;
	while (y < peaks.size() && peaks[y] < ink) {
		sawGap = sawGap || peaks[y] < gap;
		++y;
	}
	return sawGap && y < peaks.size(); // a gap, then the stem
}
} // namespace

TEST("text: the dot of an i stays apart from its stem at overlay sizes")
{
	// Vertical hinting can snap Inter's i dot onto the stem, so "Alicia" reads "Allcia".
	FontSpec plain;
	plain.sizePx = 16;
	CHECK_MSG(!dotSeparated(testsupport::text().render("l", plain, Color{1, 1, 1, 1}, 0, false, 0).image),
		  "the check must tell an l from an i");
	for (const char *family : {"Inter", "IBM Plex Sans"}) {
		for (int weight : {400, 500, 600}) {
			for (int px = 13; px <= 28; ++px) {
				FontSpec f;
				f.family = family;
				f.weight = weight;
				f.sizePx = static_cast<float>(px);
				const TextRun run = testsupport::text().render("i", f, Color{1, 1, 1, 1}, 0, false, 0);
				CHECK_MSG(dotSeparated(run.image), std::string(family) + " " + std::to_string(weight) + " at " +
									   std::to_string(px) + " px");
			}
		}
	}
}
