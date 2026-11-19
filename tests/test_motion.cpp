// SPDX-License-Identifier: GPL-2.0-or-later
// Animated artwork: release matching, the resolver thread, scene behavior and masking.
#include "check.hpp"
#include "rig.hpp"

#include "core/compositor_cpu.hpp"
#include "core/motion.hpp"
#include "media/motion_resolver.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

using namespace tonearm;
using namespace std::chrono_literals;

namespace {

Style animated(const char *preset, LayoutMode layout)
{
	Style s = makePreset(preset, layout);
	s.artworkMode = ArtworkMode::AnimatedWhenAvailable;
	s.motionFolder = "/motion";
	return s;
}

MotionLibrary library(std::initializer_list<const char *> names)
{
	MotionLibrary lib;
	std::vector<std::string> v(names.begin(), names.end());
	lib.build(v, "/lib");
	return lib;
}

std::optional<MotionAsset> find(const MotionLibrary &lib, const char *artist, const char *album,
				MotionShape pref = MotionShape::Square, const char *albumArtist = "")
{
	MotionQuery q;
	q.artist = artist;
	q.album = album;
	q.albumArtist = albumArtist;
	q.preferred = pref;
	return lib.find(q);
}

// Waits (bounded) until `pred` holds.
template <typename F> bool eventually(F pred, std::chrono::milliseconds limit = 2000ms)
{
	const auto end = std::chrono::steady_clock::now() + limit;
	while (std::chrono::steady_clock::now() < end) {
		if (pred())
			return true;
		std::this_thread::sleep_for(2ms);
	}
	return pred();
}

} // namespace

// --- Names --------------------------------------------------------------------

TEST("motion: names normalize case, accents, apostrophes and ampersands")
{
	CHECK(normalizeName("Beyoncé") == "beyonce");
	CHECK(normalizeName("Sigur Rós") == "sigur ros");
	CHECK(normalizeName("Don’t Stop  Me Now!") == "dont stop me now");
	CHECK(normalizeName("Simon & Garfunkel") == "simon and garfunkel");
	CHECK(normalizeName("  AC/DC ") == "ac dc");
	CHECK(normalizeName("Motörhead") == "motorhead");
	CHECK(normalizeName("Łona") == "lona");
	CHECK(normalizeName("宇多田ヒカル") == "宇多田ヒカル"); // other scripts are kept
}

TEST("motion: album titles split into base and edition qualifiers")
{
	const AlbumKey a = albumKey("Rumours (Super Deluxe)");
	CHECK(a.base == "rumours");
	CHECK(a.edition == (std::set<std::string>{"super", "deluxe"}));
	CHECK(albumKey("Kyoto - Single").edition == std::set<std::string>{"single"});
	CHECK(albumKey("Kyoto – Single").base == "kyoto"); // en dash
	CHECK(albumKey("Abbey Road (2019 Mix)").edition.count("2019"));
	CHECK(albumKey("OK Computer OKNOTOK 1997 2017").edition.empty());
	// Non-qualifier parentheticals are part of the title.
	CHECK(albumKey("(What's the Story) Morning Glory?").base == "whats the story morning glory");
	CHECK(albumKey("(What's the Story) Morning Glory?").edition.empty());
	// "Deluxe" and "Deluxe Edition" are the same release.
	CHECK(albumKey("Midnights (Deluxe Edition)") == albumKey("Midnights [Deluxe]"));
	CHECK(albumKey("Red (Taylor's Version)").edition == std::set<std::string>{"taylors"});
	CHECK(albumKey("Wish You Were Here - 2011 Remastered Version").edition == (std::set<std::string>{"2011", "remaster"}));
}

// --- Library matching -----------------------------------------------------------

TEST("motion: exact release matches, square by default")
{
	auto lib = library({"Phoebe Bridgers - Punisher.mp4", "Phoebe Bridgers - Punisher [tall].mp4"});
	auto m = find(lib, "Phoebe Bridgers", "Punisher");
	CHECK(m && m->shape == MotionShape::Square && m->location == "/lib/Phoebe Bridgers - Punisher.mp4");
	auto t = find(lib, "Phoebe Bridgers", "Punisher", MotionShape::Portrait);
	CHECK(t && t->shape == MotionShape::Portrait);
}

TEST("motion: a missing shape falls back to the other one")
{
	auto lib = library({"Lorde - Melodrama [tall].mov"});
	auto m = find(lib, "Lorde", "Melodrama", MotionShape::Square);
	CHECK(m && m->shape == MotionShape::Portrait);
}

TEST("motion: editions never stand in for each other")
{
	auto lib = library({"Taylor Swift - Midnights.mp4", "Fleetwood Mac - Rumours (Super Deluxe).mp4", "Pink Floyd - Wish You Were Here.mp4",
			    "Phoebe Bridgers - Kyoto - Single.mp4"});
	CHECK(find(lib, "Taylor Swift", "Midnights"));
	CHECK(!find(lib, "Taylor Swift", "Midnights (3am Edition)"));
	CHECK(!find(lib, "Taylor Swift", "Midnights (Deluxe)"));
	CHECK(!find(lib, "Fleetwood Mac", "Rumours"));
	CHECK(find(lib, "Fleetwood Mac", "Rumours [Super Deluxe Edition]"));
	CHECK(!find(lib, "Pink Floyd", "Wish You Were Here (2011 Remastered Version)"));
	// A single is not its album, and vice versa.
	CHECK(find(lib, "Phoebe Bridgers", "Kyoto - Single"));
	CHECK(!find(lib, "Phoebe Bridgers", "Kyoto"));
}

TEST("motion: similar titles and other artists do not match")
{
	auto lib = library({"Radiohead - Kid A.mp4", "Radiohead - OK Computer.mp4"});
	CHECK(!find(lib, "Radiohead", "Kid A Mnesia"));
	CHECK(!find(lib, "Radiohead", "Kid"));
	CHECK(!find(lib, "Radiohead Tribute Band", "OK Computer"));
	CHECK(!find(lib, "Radiohead", "")); // no album: no confident match
	CHECK(!find(lib, "", "OK Computer"));
	CHECK(find(lib, "RADIOHEAD", "ok computer"));
}

TEST("motion: featured artists and album artists still match the primary artist")
{
	auto lib = library({"Phoebe Bridgers - Punisher.mp4", "Various Artists - Barbie The Album.mp4"});
	CHECK(find(lib, "Phoebe Bridgers & Conor Oberst", "Punisher"));
	CHECK(find(lib, "Phoebe Bridgers feat. Conor Oberst", "Punisher"));
	CHECK(find(lib, "Dua Lipa", "Barbie The Album", MotionShape::Square, "Various Artists"));
	CHECK(!find(lib, "Conor Oberst", "Punisher"));
}

TEST("motion: two files claiming one release are treated as ambiguous")
{
	auto lib = library({"Bjork - Homogenic.mp4", "Björk - Homogenic.webm"});
	CHECK(!find(lib, "Björk", "Homogenic"));
}

TEST("motion: names with dashes, shape tags and unsupported files")
{
	auto lib = library({"Jay-Z - The Blueprint 3.mp4", "Sufjan Stevens - Carrie - Lowell.m4v", "Lorde - Pure Heroine [3x4].mkv",
			    "Lorde - Solar Power.gif", "notes.txt", "NoSeparator.mp4"});
	CHECK(lib.size() == 3);
	CHECK(find(lib, "Jay-Z", "The Blueprint 3"));
	CHECK(find(lib, "Sufjan Stevens", "Carrie - Lowell"));
	auto p = find(lib, "Lorde", "Pure Heroine");
	CHECK(p && p->shape == MotionShape::Portrait);
	CHECK(!find(lib, "Lorde", "Solar Power"));
}

TEST("motion: cover crop fills the box without distortion")
{
	CropRect c = coverCrop(1080, 1440, 300, 300); // portrait video into a square box
	CHECK(std::fabs(c.w - 1080) < 0.01f && std::fabs(c.h - 1080) < 0.01f && std::fabs(c.y - 180) < 0.01f && c.x == 0);
	c = coverCrop(2048, 2048, 400, 300); // square video into a wide box
	CHECK(std::fabs(c.w - 2048) < 0.01f && std::fabs(c.h - 1536) < 0.01f && std::fabs(c.y - 256) < 0.01f);
	c = coverCrop(1080, 1440, 300, 400); // same aspect: whole frame
	CHECK(c.x == 0 && c.y == 0 && c.w == 1080 && std::fabs(c.h - 1440) < 0.01f);
}

TEST("motion: automatic orientation follows the artwork box")
{
	CHECK(preferredShape(MotionOrientation::Automatic, 200, 200) == MotionShape::Square);
	CHECK(preferredShape(MotionOrientation::Automatic, 300, 400) == MotionShape::Portrait);
	CHECK(preferredShape(MotionOrientation::Square, 300, 400) == MotionShape::Square);
	CHECK(preferredShape(MotionOrientation::Portrait, 400, 400) == MotionShape::Portrait);
}

TEST("motion: off by default, and Reduce motion or hidden artwork turn it off")
{
	Style s = makePreset("glass", LayoutMode::ExpandedHorizontal);
	CHECK(s.artworkMode == ArtworkMode::Static);
	CHECK(!wantsMotion(s));
	s.artworkMode = ArtworkMode::AnimatedWhenAvailable;
	CHECK(wantsMotion(s));
	s.reducedMotion = true;
	CHECK(!wantsMotion(s));
	s.reducedMotion = false;
	s.showArtwork = false;
	CHECK(!wantsMotion(s));
}

// --- Resolver ---------------------------------------------------------------------

namespace {

// A source whose answers and timing tests control.
class FakeSource : public MotionSource {
public:
	std::mutex m;
	std::condition_variable cv;
	bool block = false;          // hold lookups until released (or cancelled)
	bool throwNext = false;
	std::atomic<int> calls{0}, cancelledSeen{0};
	std::set<std::string> available; // albums with motion artwork

	std::optional<MotionAsset> resolve(const MotionQuery &q, const std::atomic<bool> &cancelled) override
	{
		++calls;
		// Like a real source, notice cancellation promptly (poll, as a socket timeout would).
		std::unique_lock<std::mutex> lock(m);
		for (int i = 0; i < 600 && block && !cancelled.load(); ++i)
			cv.wait_for(lock, 5ms);
		if (cancelled) {
			++cancelledSeen;
			return std::nullopt;
		}
		if (throwNext) {
			throwNext = false;
			throw std::runtime_error("network down");
		}
		if (available.count(q.album))
			return MotionAsset{"/v/" + q.album + ".mp4", MotionShape::Square};
		return std::nullopt;
	}
	const char *name() const override { return "fake"; }
	void release()
	{
		{
			std::lock_guard<std::mutex> lock(m);
			block = false;
		}
		cv.notify_all();
	}
};

struct Delivered {
	std::mutex m;
	std::vector<std::pair<uint64_t, std::string>> items; // serial, location ("" = none)
	void add(uint64_t s, const std::optional<MotionAsset> &a)
	{
		std::lock_guard<std::mutex> lock(m);
		items.emplace_back(s, a ? a->location : "");
	}
	size_t size()
	{
		std::lock_guard<std::mutex> lock(m);
		return items.size();
	}
	std::pair<uint64_t, std::string> last()
	{
		std::lock_guard<std::mutex> lock(m);
		return items.back();
	}
};

MotionQuery query(const char *album)
{
	MotionQuery q;
	q.artist = "Artist";
	q.album = album;
	return q;
}

} // namespace

TEST("resolver: delivers found and missing artwork, and caches both")
{
	auto *src = new FakeSource;
	src->available = {"Has Motion"};
	Delivered d;
	MotionResolver r(std::unique_ptr<MotionSource>(src), [&](uint64_t s, const std::optional<MotionAsset> &a) { d.add(s, a); });
	r.request(1, query("Has Motion"));
	CHECK(eventually([&] { return d.size() == 1; }));
	CHECK(d.last() == std::make_pair(uint64_t{1}, std::string("/v/Has Motion.mp4")));
	r.request(2, query("No Motion"));
	CHECK(eventually([&] { return d.size() == 2; }));
	CHECK(d.last().second.empty());
	// Repeats are answered from the cache without another lookup.
	r.request(3, query("Has Motion"));
	r.request(4, query("No Motion"));
	CHECK(eventually([&] { return d.size() >= 3 && d.last().first == 4; }));
	CHECK(src->calls == 2);
}

TEST("resolver: rapid track switching cancels obsolete lookups and answers only the newest")
{
	auto *src = new FakeSource;
	src->available = {"A", "B", "C"};
	src->block = true;
	Delivered d;
	MotionResolver r(std::unique_ptr<MotionSource>(src), [&](uint64_t s, const std::optional<MotionAsset> &a) { d.add(s, a); });
	r.request(1, query("A"));
	CHECK(eventually([&] { return src->calls == 1; }));
	r.request(2, query("B")); // cancels A while it is running
	r.request(3, query("C")); // replaces B before it starts
	CHECK(eventually([&] { return src->cancelledSeen >= 1 && src->calls >= 2; }));
	src->release();
	CHECK(eventually([&] { return d.size() >= 1 && d.last().first == 3; }));
	std::this_thread::sleep_for(30ms);
	CHECK(d.size() == 1);
	CHECK(d.last().second == "/v/C.mp4");
	CHECK(src->calls == 2); // B never ran
}

TEST("resolver: the same release requested twice runs one lookup")
{
	auto *src = new FakeSource;
	src->available = {"A"};
	src->block = true;
	Delivered d;
	MotionResolver r(std::unique_ptr<MotionSource>(src), [&](uint64_t s, const std::optional<MotionAsset> &a) { d.add(s, a); });
	r.request(1, query("A"));
	CHECK(eventually([&] { return src->calls == 1; }));
	r.request(2, query("A")); // e.g. the next track of the same album
	src->release();
	CHECK(eventually([&] { return d.size() >= 1; }));
	std::this_thread::sleep_for(30ms);
	CHECK(src->calls == 1);
	CHECK(d.last().first == 2);
}

TEST("resolver: tracks with the same album artist share a motion lookup")
{
	auto *src = new FakeSource;
	src->available = {"Soundtrack"};
	src->block = true;
	Delivered d;
	MotionResolver r(std::unique_ptr<MotionSource>(src),
		[&](uint64_t serial, const std::optional<MotionAsset> &a) { d.add(serial, a); });
	MotionQuery first = query("Soundtrack");
	first.artist = "Performer One";
	first.albumArtist = "Compilation Cast";
	r.request(1, first);
	CHECK(eventually([&] { return src->calls == 1; }));
	MotionQuery second = first;
	second.artist = "Performer Two";
	second.title = "Next Track";
	r.request(2, second);
	src->release();
	CHECK(eventually([&] { return d.size() >= 1; }));
	CHECK(src->calls == 1);
	CHECK(d.last().first == 2);
}

TEST("resolver: a failing source falls back to no artwork and keeps working")
{
	auto *src = new FakeSource;
	src->available = {"A"};
	src->throwNext = true;
	Delivered d;
	MotionResolver r(std::unique_ptr<MotionSource>(src), [&](uint64_t s, const std::optional<MotionAsset> &a) { d.add(s, a); },
			 0ms /* no negative caching */);
	r.request(1, query("A"));
	CHECK(eventually([&] { return d.size() == 1; }));
	CHECK(d.last().second.empty());
	r.request(2, query("A"));
	CHECK(eventually([&] { return d.size() == 2; }));
	CHECK(d.last().second == "/v/A.mp4");
}

TEST("resolver: shutting down mid-lookup cancels promptly")
{
	auto *src = new FakeSource;
	src->block = true;
	const auto start = std::chrono::steady_clock::now();
	{
		MotionResolver r(std::unique_ptr<MotionSource>(src), [](uint64_t, const std::optional<MotionAsset> &) {});
		r.request(1, query("A"));
		eventually([&] { return src->calls == 1; });
	}
	CHECK(std::chrono::steady_clock::now() - start < 1s);
}

TEST("resolver: local folder source reads a real directory")
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / "tonearm-motion-test";
	fs::remove_all(dir);
	fs::create_directories(dir);
	std::ofstream(dir / "Phoebe Bridgers - Punisher.mp4") << "x";
	std::ofstream(dir / "ignore.txt") << "x";
	LocalFolderSource src(dir.string(), 0ms);
	std::atomic<bool> cancel{false};
	MotionQuery q;
	q.artist = "Phoebe Bridgers";
	q.album = "Punisher";
	auto m = src.resolve(q, cancel);
	CHECK(m && m->location.find("Punisher.mp4") != std::string::npos);
	q.album = "Stranger in the Alps";
	CHECK(!src.resolve(q, cancel));
	// Files added later are picked up on the next scan.
	std::ofstream(dir / "Phoebe Bridgers - Stranger in the Alps.mp4") << "x";
	CHECK(src.resolve(q, cancel));
	LocalFolderSource missing((dir / "nope").string(), 0ms);
	CHECK(!missing.resolve(q, cancel));
	fs::remove_all(dir);
}

// --- Scene ------------------------------------------------------------------------

namespace {
int videoOps(const Rig &r)
{
	int n = 0;
	for (const auto &op : r.ops)
		n += op.kind == DrawOp::Kind::Video && op.opacity > 0.001f;
	return n;
}
float videoOpacity(const Rig &r)
{
	for (const auto &op : r.ops)
		if (op.kind == DrawOp::Kind::Video)
			return op.opacity;
	return 0;
}
} // namespace

TEST("scene: animated artwork fades in over the still cover once a frame is ready")
{
	Rig r(animated("glass", LayoutMode::ExpandedHorizontal));
	r.run(0, 1);
	CHECK(videoOps(r) == 0); // nothing loaded yet: still cover only
	r.motion.identity = r.scene.shownIdentity();
	r.motion.ready = true;
	r.run(1.0, 1.12);
	const float early = videoOpacity(r);
	CHECK(early > 0 && early < 0.5f);
	r.run(1.0, 2.0);
	CHECK(videoOps(r) == 1);
	CHECK(videoOpacity(r) > 0.99f);
	// Draw order: the still artwork, then the video, then its overlay (scrim/stroke).
	size_t artAt = 0, videoAt = 0;
	const auto &L = *r.scene.currentLayers();
	for (size_t i = 0; i < r.ops.size(); ++i) {
		if (r.ops[i].kind == DrawOp::Kind::Image && r.ops[i].imageId == L.art.id)
			artAt = i;
		if (r.ops[i].kind == DrawOp::Kind::Video)
			videoAt = i;
	}
	CHECK(artAt > 0 && videoAt == artAt + 1);
}

TEST("scene: video for another track is never shown")
{
	Rig r(animated("minimal", LayoutMode::ExpandedHorizontal));
	r.run(0, 1);
	r.motion.identity = "some other track";
	r.motion.ready = true;
	r.run(1, 2);
	CHECK(videoOps(r) == 0);
}

TEST("scene: a track change drops the old video at once and the new one fades in")
{
	Rig r(animated("neon", LayoutMode::ExpandedHorizontal));
	r.run(0, 1);
	r.motion = {r.scene.shownIdentity(), true};
	r.run(1, 2);
	CHECK(videoOps(r) == 1);
	r.setTrack(1, PlaybackStatus::Playing, 0, 2.0);
	r.step(2.0 + 1.0 / 60);
	CHECK(videoOps(r) == 0); // host still reports the old identity
	r.motion = {r.scene.shownIdentity(), true};
	r.run(2.05, 3.5);
	CHECK(videoOps(r) == 1);
}

TEST("scene: losing the video (decode error, file gone) fades back to the still cover")
{
	Rig r(animated("glass", LayoutMode::Square));
	r.run(0, 1);
	r.motion = {r.scene.shownIdentity(), true};
	r.run(1, 2);
	CHECK(videoOps(r) == 1);
	r.motion.ready = false;
	r.run(2, 3);
	CHECK(videoOps(r) == 0);
	CHECK(r.scene.videoMix() == 0);
}

TEST("scene: static mode and Reduce motion never draw video")
{
	for (int variant = 0; variant < 2; ++variant) {
		Style s = animated("editorial", LayoutMode::ExpandedHorizontal);
		if (variant == 0)
			s.artworkMode = ArtworkMode::Static;
		else
			s.reducedMotion = true;
		Rig r(s);
		r.run(0, 1);
		r.motion = {r.scene.shownIdentity(), true};
		r.run(1, 2);
		CHECK(videoOps(r) == 0);
		CHECK(!r.scene.currentLayers()->artOverlay.valid());
	}
}

namespace {
float videoTint(const Rig &r)
{
	for (const auto &op : r.ops)
		if (op.kind == DrawOp::Kind::Video)
			return op.color.a;
	return -1;
}
} // namespace

TEST("scene: pausing dims the video with a tint, never by letting the still cover show through")
{
	Rig r(animated("glass", LayoutMode::ExpandedHorizontal));
	r.run(0, 1);
	r.motion = {r.scene.shownIdentity(), true};
	r.run(1, 2);
	const float playing = videoOpacity(r);
	r.setTimeline(PlaybackStatus::Paused, 32, 2.0, demoDuration(0));
	CHECK(videoTint(r) < 0.01f);
	r.run(2, 3.5);
	CHECK(videoOps(r) == 1);
	CHECK(std::fabs(videoOpacity(r) - playing) < 0.02f); // still opaque over the cover
	CHECK(videoTint(r) > 0.3f);                           // dimmed like the still cover (38%)
	r.setTimeline(PlaybackStatus::Playing, 32, 3.5, demoDuration(0));
	r.run(3.5, 5);
	CHECK(videoTint(r) < 0.01f);
}

TEST("scene: placeholders and tracks without artwork space get no video")
{
	Style s = animated("glass", LayoutMode::ExpandedHorizontal);
	s.idle = IdleBehavior::ShowPlaceholder;
	Rig r(s);
	r.hasSession = false;
	r.run(0, 1);
	r.motion = {r.scene.shownIdentity(), true};
	r.run(1, 2);
	CHECK(videoOps(r) == 0);
}

// --- Masking in every preset and layout ---------------------------------------------

namespace {

Image solidFrame(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
	Image img(w, h);
	for (int y = 0; y < h; ++y) {
		uint8_t *p = img.row(y);
		for (int x = 0; x < w; ++x, p += 4) {
			p[0] = b;
			p[1] = g;
			p[2] = r;
			p[3] = 255;
		}
	}
	return img;
}

bool near(const uint8_t *p, int b, int g, int r, int tol = 6)
{
	return std::abs(p[0] - b) <= tol && std::abs(p[1] - g) <= tol && std::abs(p[2] - r) <= tol;
}

} // namespace

TEST("mask: video stays inside the artwork area in every preset and layout")
{
	const char *presets[] = {"glass", "minimal", "neon", "editorial"};
	const LayoutMode layouts[] = {LayoutMode::CompactHorizontal, LayoutMode::ExpandedHorizontal, LayoutMode::Square, LayoutMode::Vertical};
	const Image frame = solidFrame(240, 320, 255, 0, 255); // magenta portrait frame
	for (const char *preset : presets) {
		for (LayoutMode layout : layouts) {
			const std::string where = std::string(preset) + "/" + layoutId(layout);
			Rig r(animated(preset, layout));
			Rig still_rig(animated(preset, layout)); // same timing, never gets a video
			r.run(0, 1.2);
			still_rig.run(0, 1.2);
			const auto &L = *r.scene.currentLayers();
			const auto &m = L.videoMask;
			CHECK_MSG(m.valid, where);
			if (!m.valid)
				continue;
			CHECK_MSG(m.box.x >= L.layout.card.x - 1 && m.box.right() <= L.layout.card.right() + 1 && m.box.y >= L.layout.card.y - 1 &&
					  m.box.bottom() <= L.layout.card.bottom() + 1,
				  where);
			CHECK_MSG((std::string(preset) == "editorial") == (m.featherEdge != 0), where);

			// Render the static frame and the animated frame at the same moment and compare.
			r.motion = {r.scene.shownIdentity(), true};
			r.run(1.2, 2.5);
			still_rig.run(1.2, 2.5);
			Image still(r.style.width, r.style.height);
			executeOnCpu(still, still_rig.ops);
			Image moving(r.style.width, r.style.height);
			executeOnCpu(moving, r.ops, &frame);
			Image fallback(r.style.width, r.style.height);
			executeOnCpu(fallback, r.ops, nullptr); // no decoded frame: identical to still

			int outsideChanged = 0, fallbackChanged = 0;
			for (int y = 0; y < still.height(); ++y) {
				for (int x = 0; x < still.width(); ++x) {
					const uint8_t *a = still.row(y) + x * 4, *b = moving.row(y) + x * 4, *f = fallback.row(y) + x * 4;
					const bool inside = x + 0.5f >= m.box.x - 1 && x + 0.5f <= m.box.right() + 1 && y + 0.5f >= m.box.y - 1 &&
							    y + 0.5f <= m.box.bottom() + 1;
					// Same draw list with and without the frame: outside the artwork, bit-identical.
					const bool differs = f[0] != b[0] || f[1] != b[1] || f[2] != b[2] || f[3] != b[3];
					if (!inside && differs)
						++outsideChanged;
					// Without a frame it looks like the still artwork (separate rasters differ only by grain).
					const int grain = std::max({std::abs(a[0] - f[0]), std::abs(a[1] - f[1]), std::abs(a[2] - f[2]), std::abs(a[3] - f[3])});
					if (grain > 8)
						++fallbackChanged;
				}
			}
			CHECK_MSG(outsideChanged == 0, where + ": " + std::to_string(outsideChanged) + " px changed outside the artwork");
			// Without a decoded frame the result is exactly the still artwork.
			CHECK_MSG(fallbackChanged == 0, where + ": " + std::to_string(fallbackChanged) + " px differ without a frame");

			// The middle of the artwork (above any scrim) shows the video.
			const int cx = static_cast<int>(m.box.cx());
			const int cy = static_cast<int>(m.box.y + m.box.h * (L.layout.scrimVisible ? 0.2f : 0.5f));
			const int fx = m.featherEdge == 1 ? static_cast<int>(m.box.x + m.box.w * 0.25f) : cx;
			const int fy = m.featherEdge == 2 ? static_cast<int>(m.box.y + m.box.h * 0.25f) : cy;
			CHECK_MSG(near(moving.row(fy) + fx * 4, 255, 0, 255, 12), where + ": artwork centre should show the video");

			// Rounded corners: the very corner pixel of an inset cover is not video.
			if (m.radius > 3 && m.featherEdge == 0) {
				const uint8_t *corner = moving.row(static_cast<int>(m.box.y)) + static_cast<int>(m.box.x) * 4;
				CHECK_MSG(!near(corner, 255, 0, 255, 40), where + ": corner should be masked");
			}
			// Editorial: the far side of the feather is the untouched surface.
			if (m.featherEdge == 1) {
				const int x = static_cast<int>(m.box.right()) - 1, y = static_cast<int>(m.box.cy());
				const uint8_t *a = still.row(y) + x * 4, *b = moving.row(y) + x * 4;
				CHECK_MSG(std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]) < 12, where + ": feather edge");
			}
		}
	}
}
