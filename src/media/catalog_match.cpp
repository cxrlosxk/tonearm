// SPDX-License-Identifier: GPL-2.0-or-later
#include "catalog_match.hpp"

#include <algorithm>
#include <vector>

namespace tonearm {

std::optional<std::uint64_t> exactCatalogAlbumId(const MotionQuery &nowPlaying, const std::vector<CatalogAlbum> &candidates)
{
	const AlbumKey desired = albumKey(nowPlaying.album);
	if (desired.base.empty())
		return std::nullopt;

	std::vector<std::string> acceptableArtists = artistKeys(nowPlaying.artist);
	for (const auto &artist : artistKeys(nowPlaying.albumArtist)) {
		if (std::find(acceptableArtists.begin(), acceptableArtists.end(), artist) == acceptableArtists.end())
			acceptableArtists.push_back(artist);
	}
	if (acceptableArtists.empty())
		return std::nullopt;

	std::optional<std::uint64_t> uniqueId;
	for (const auto &album : candidates) {
		if (album.id == 0 || !(albumKey(album.title) == desired))
			continue;
		const std::string catalogArtist = normalizeName(album.artist);
		if (std::find(acceptableArtists.begin(), acceptableArtists.end(), catalogArtist) == acceptableArtists.end())
			continue;
		if (uniqueId && *uniqueId != album.id)
			return std::nullopt; // Multiple releases: do not risk displaying the wrong artwork.
		uniqueId = album.id;
	}
	return uniqueId;
}

} // namespace tonearm
