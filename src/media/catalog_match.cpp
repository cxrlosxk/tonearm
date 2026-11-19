// SPDX-License-Identifier: GPL-2.0-or-later
#include "catalog_match.hpp"

#include <algorithm>
#include <vector>

namespace tonearm {

bool catalogArtistMatches(const MotionQuery &nowPlaying, const std::string &catalogArtist)
{
	std::vector<std::string> acceptableArtists = artistKeys(nowPlaying.artist);
	for (const auto &artist : artistKeys(nowPlaying.albumArtist)) {
		if (std::find(acceptableArtists.begin(), acceptableArtists.end(), artist) == acceptableArtists.end())
			acceptableArtists.push_back(artist);
	}
	if (acceptableArtists.empty() || catalogArtist.empty())
		return false;

	// A full official credit may consist of multiple performers, whereas SMTC
	// reports just the track's performer. Check each COMPLETE credited name,
	// never a substring, and never split inside an artist's name.
	std::vector<std::string> officialCredits{normalizeName(catalogArtist)};
	std::string part;
	const auto pushPart = [&]() {
		const std::string key = normalizeName(part);
		if (!key.empty())
			officialCredits.push_back(key);
		part.clear();
	};
	for (size_t i = 0; i < catalogArtist.size();) {
		const bool ampersand = catalogArtist.compare(i, 3, " & ") == 0;
		const bool andWord = catalogArtist.compare(i, 5, " and ") == 0;
		const bool comma = catalogArtist[i] == ',' || catalogArtist[i] == ';';
		if (ampersand || andWord || comma) {
			pushPart();
			i += ampersand ? 3 : andWord ? 5 : 1;
		} else {
			part.push_back(catalogArtist[i++]);
		}
	}
	pushPart();
	for (const auto &credit : officialCredits)
		if (std::find(acceptableArtists.begin(), acceptableArtists.end(), credit) != acceptableArtists.end())
			return true;
	return false;
}

std::optional<std::uint64_t> exactCatalogAlbumId(const MotionQuery &nowPlaying, const std::vector<CatalogAlbum> &candidates)
{
	const AlbumKey desired = albumKey(nowPlaying.album);
	if (desired.base.empty())
		return std::nullopt;

	std::optional<std::uint64_t> uniqueId;
	for (const auto &album : candidates) {
		if (album.id == 0 || !(albumKey(album.title) == desired))
			continue;
		if (!catalogArtistMatches(nowPlaying, album.artist))
			continue;
		if (uniqueId && *uniqueId != album.id)
			return std::nullopt; // Multiple releases: do not risk displaying the wrong artwork.
		uniqueId = album.id;
	}
	return uniqueId;
}

} // namespace tonearm
