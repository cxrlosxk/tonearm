// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Animated ("motion") artwork: matching a playing track to a motion artwork asset.
//
// Tonearm never guesses. A motion asset is used only when the normalized artist and
// album match exactly, including any edition qualifiers (Deluxe, Remastered, Single,
// a year, ...). Anything less certain keeps the still cover.

#include "style.hpp"

#include <optional>
#include <set>
#include <string>
#include <vector>

namespace tonearm {

// True when the style asks for animated artwork and nothing overrides it.
bool wantsMotion(const Style &s);

// Smart uses an efficient codec for compact artwork cards, preserving full
// HEVC resolution whenever the artwork is large enough to benefit visually.
MotionQuality effectiveMotionQuality(MotionQuality selection, float artworkMaxDimension);

// Whether another video frame should be copied into the OBS overlay texture.
// Deliberately supports 60 FPS source media without skipping every other frame.
bool shouldCaptureMotionFrame(double currentSec, double previousSec, double maxFps = 60.0);

// Lowercase, accent-folded, punctuation-free form used for comparisons.
std::string normalizeName(const std::string &s);

struct AlbumKey {
	std::string base;              // normalized album title without edition qualifiers
	std::set<std::string> edition; // normalized qualifier words, e.g. {"deluxe"} or {"2011", "remaster"}
	bool operator==(const AlbumKey &o) const { return base == o.base && edition == o.edition; }
};

// Splits an album title into its base and edition qualifiers.
// "Rumours (Super Deluxe)" -> base "rumours", edition {"super", "deluxe"}.
// "Kyoto - Single"          -> base "kyoto",   edition {"single"}.
AlbumKey albumKey(const std::string &album);

// Artist names an album credit may be listed under: the full credit, and its first
// listed artist ("Phoebe Bridgers & Bo Burnham" -> also "phoebe bridgers").
std::vector<std::string> artistKeys(const std::string &artist);

enum class MotionShape { Square, Portrait };

struct MotionAsset {
	std::string location;          // file path (or URL for future sources)
	MotionShape shape = MotionShape::Square;
};

struct MotionQuery {
	std::string artist, albumArtist, album, title;
	MotionShape preferred = MotionShape::Square;
	MotionQuality quality = MotionQuality::Maximum;
};

// Region of a w x h video frame that fills a dstW x dstH box without distortion
// (centered "cover" crop). Returned in frame pixels.
struct CropRect {
	float x = 0, y = 0, w = 0, h = 0;
};
CropRect coverCrop(int frameW, int frameH, float dstW, float dstH);

// The shape that suits an artwork box best, given the user's preference.
MotionShape preferredShape(MotionOrientation o, float artWidth, float artHeight);

// Index of a local motion artwork folder. File names follow
//   "Artist - Album.mp4"           square artwork
//   "Artist - Album [tall].mp4"    portrait (3:4) artwork
// Supported containers are those OBS's media source plays: mp4, m4v, mov, webm, mkv, m3u8.
class MotionLibrary {
public:
	void build(const std::vector<std::string> &fileNames, const std::string &folder);
	std::optional<MotionAsset> find(const MotionQuery &q) const;
	size_t size() const { return entries_.size(); }

	// Parses one file name. Exposed for tests.
	struct Entry {
		std::vector<std::string> artistCandidates; // normalized, one per possible " - " split
		std::vector<AlbumKey> albumCandidates;     // parallel to artistCandidates
		MotionShape shape = MotionShape::Square;
		std::string path;
	};
	static std::optional<Entry> parse(const std::string &fileName, const std::string &folder);

private:
	std::vector<Entry> entries_;
};

} // namespace tonearm
