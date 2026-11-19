// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Strict, offline Apple/iTunes album identity matching.
// A network client may supply candidates from the documented iTunes Search API.
// This module does not make requests, acquire tokens, or retrieve artwork.

#include "core/motion.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tonearm {

struct CatalogAlbum {
	std::uint64_t id = 0;
	std::string artist;
	std::string title;
};

// Only returns an ID when artist and album (including edition) match exactly
// after normalization, and precisely one distinct catalog ID matches.
// Deliberately abstains when Apple lists multiple indistinguishable releases.
std::optional<std::uint64_t> exactCatalogAlbumId(const MotionQuery &nowPlaying, const std::vector<CatalogAlbum> &candidates);

// Match a playing track against an official album credit that lists several
// artists (e.g. "Wicked Movie Cast, Cynthia Erivo & Ariana Grande").
// The official album title must still be checked separately.
bool catalogArtistMatches(const MotionQuery &nowPlaying, const std::string &catalogArtist);

} // namespace tonearm
