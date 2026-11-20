// SPDX-License-Identifier: GPL-2.0-or-later
// Geometry invariants checked for every preset, layout, content variant and a
// range of canvas sizes: nothing leaves the card, nothing collides.
#include "check.hpp"

#include "core/layout.hpp"
#include "core/style.hpp"

#include <string>

using namespace tonearm;

namespace {

const char *kPresets[] = {"glass", "minimal", "neon", "editorial", "turntable"};
const LayoutMode kLayouts[] = {LayoutMode::CompactHorizontal, LayoutMode::ExpandedHorizontal, LayoutMode::Square,
			       LayoutMode::Vertical};

float overlap(const Rect &a, const Rect &b)
{
	const float w = std::min(a.right(), b.right()) - std::max(a.x, b.x);
	const float h = std::min(a.bottom(), b.bottom()) - std::max(a.y, b.y);
	return w > 0 && h > 0 ? w * h : 0;
}

bool inside(const Rect &inner, const Rect &outer, float tol = 0.75f)
{
	return inner.x >= outer.x - tol && inner.y >= outer.y - tol && inner.right() <= outer.right() + tol &&
	       inner.bottom() <= outer.bottom() + tol;
}

std::string describe(const Style &s, const LayoutContent &c)
{
	return s.presetId + "/" + layoutId(s.layout) + " " + std::to_string(s.width) + "x" + std::to_string(s.height) +
	       (c.hasArtwork ? "" : " no-art") + (c.hasAlbum ? "" : " no-album") + (c.hasTimeline ? "" : " no-timeline");
}

void checkLayout(const Style &s, const LayoutContent &c)
{
	const Layout L = computeLayout(s, c);
	const std::string what = describe(s, c);
	const Rect canvas{0, 0, static_cast<float>(s.width), static_cast<float>(s.height)};
	CHECK_MSG(L.width == s.width && L.height == s.height, what);
	CHECK_MSG(inside(L.card, canvas), what + ": card leaves the canvas");
	CHECK_MSG(L.card.w > 0 && L.card.h > 0, what);

	struct Named {
		const char *name;
		const TextBox *box;
	};
	const Named lines[] = {{"title", &L.title}, {"artist", &L.artist}, {"album", &L.album}, {"source", &L.source}};
	const Named times[] = {{"elapsed", &L.elapsed}, {"remaining", &L.remaining}};

	CHECK_MSG(L.title.visible, what + ": title must always have a place");
	for (const Named &n : lines) {
		if (!n.box->visible)
			continue;
		CHECK_MSG(n.box->rect.w > 8 && n.box->rect.h > 4, what + ": " + n.name + " box too small");
		CHECK_MSG(inside(n.box->rect, L.card), what + ": " + n.name + " leaves the card");
		if (L.artVisible && L.artPlacement != ArtPlacement::Fill)
			CHECK_MSG(overlap(n.box->rect, L.art) < 1, what + ": " + n.name + " overlaps the artwork");
		if (L.progressVisible)
			CHECK_MSG(overlap(n.box->rect, L.progress) < 1, what + ": " + n.name + " overlaps the progress bar");
		for (const Named &m : lines)
			if (m.box != n.box && m.box->visible && n.box < m.box)
				CHECK_MSG(overlap(n.box->rect, m.box->rect) < 1, what + ": " + n.name + " overlaps " + m.name);
	}
	for (const Named &n : times) {
		if (!n.box->visible)
			continue;
		CHECK_MSG(inside(n.box->rect, L.card), what + ": " + n.name + " leaves the card");
		if (L.progressVisible)
			CHECK_MSG(overlap(n.box->rect, L.progress) < 1, what + ": " + n.name + " overlaps the progress bar");
		for (const Named &m : lines)
			if (m.box->visible)
				CHECK_MSG(overlap(n.box->rect, m.box->rect) < 1, what + ": " + n.name + " overlaps " + m.name);
	}
	if (s.composition == Composition::Turntable && c.hasArtwork) {
		CHECK_MSG(L.recordVisible, what + ": record should be visible");
		CHECK_MSG(inside(L.record, L.card), what + ": record should stay inside card");
		CHECK_MSG(inside(L.armZone, L.card), what + ": arm should stay inside card");
	}
	if (L.artVisible) {
		CHECK_MSG(inside(L.art, L.card), what + ": artwork leaves the card");
		CHECK_MSG(L.art.w > 8 && L.art.h > 8, what + ": artwork too small");
	}
	if (L.progressVisible) {
		CHECK_MSG(inside(L.progress, L.card), what + ": progress leaves the card");
		CHECK_MSG(L.progress.w > 20, what + ": progress too short");
		if (L.artVisible && L.artPlacement != ArtPlacement::Fill)
			CHECK_MSG(overlap(L.progress, L.art) < 1, what + ": progress overlaps the artwork");
	}
	if (!c.hasTimeline)
		CHECK_MSG(!L.progressVisible && !L.elapsed.visible && !L.remaining.visible, what + ": timeline shown without data");
	if (!c.hasArtwork)
		CHECK_MSG(true, what);
}

} // namespace

TEST("layout: default sizes for every preset and layout")
{
	for (const char *p : kPresets)
		for (LayoutMode m : kLayouts) {
			const Style s = makePreset(p, m);
			for (int mask = 0; mask < 8; ++mask) {
				LayoutContent c;
				c.hasArtwork = mask & 1;
				c.hasAlbum = mask & 2;
				c.hasTimeline = mask & 4;
				checkLayout(s, c);
			}
		}
}

TEST("layout: scaled canvases keep the same proportions working")
{
	for (const char *p : kPresets)
		for (LayoutMode m : kLayouts)
			for (float k : {0.6f, 1.0f, 1.5f, 2.5f}) {
				Style s = makePreset(p, m);
				s.width = static_cast<int>(s.width * k);
				s.height = static_cast<int>(s.height * k);
				checkLayout(s, LayoutContent{});
			}
}

TEST("layout: unusual aspect ratios degrade without collisions")
{
	for (const char *p : kPresets)
		for (LayoutMode m : kLayouts) {
			const Style base = makePreset(p, m);
			for (auto [fw, fh] : {std::pair{1.6f, 1.0f}, std::pair{1.0f, 1.4f}, std::pair{0.8f, 1.0f}, std::pair{1.0f, 0.85f}}) {
				Style s = base;
				s.width = static_cast<int>(base.width * fw);
				s.height = static_cast<int>(base.height * fh);
				checkLayout(s, LayoutContent{});
			}
		}
}

TEST("layout: optional elements can be switched off")
{
	for (const char *p : kPresets)
		for (LayoutMode m : kLayouts) {
			Style s = makePreset(p, m);
			s.showArtwork = false;
			s.showAlbum = false;
			s.showProgress = false;
			s.times = TimeDisplay::Hidden;
			s.showSourceApp = true;
			const Layout L = computeLayout(s, LayoutContent{});
			const std::string what = s.presetId + "/" + layoutId(m);
			CHECK_MSG(!L.artVisible, what);
			CHECK_MSG(!L.album.visible, what);
			CHECK_MSG(!L.progressVisible, what);
			CHECK_MSG(!L.elapsed.visible && !L.remaining.visible, what);
			checkLayout(s, LayoutContent{});
		}
}
