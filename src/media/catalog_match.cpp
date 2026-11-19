// SPDX-License-Identifier: GPL-2.0-or-later
#include "catalog_match.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace tonearm {

std::string motionBaseAlbumQuery(const std::string &album)
{
	const auto original = albumKey(album);
	if (original.base.empty())
		return {};
	// Bracket/dash edition designations are already understood by albumKey().
	for (const char open : {'(', '['}) {
		const auto pos = album.rfind(open);
		if (pos != std::string::npos && pos > 0) {
			std::string shortTitle = album.substr(0, pos);
			while (!shortTitle.empty() && std::isspace(static_cast<unsigned char>(shortTitle.back())))
				shortTitle.pop_back();
			if (albumKey(shortTitle).base == original.base && !shortTitle.empty())
				return shortTitle;
		}
	}
	const auto dash = album.rfind(" - ");
	if (dash != std::string::npos && dash > 0) {
		const std::string prefix = album.substr(0, dash);
		if (albumKey(prefix).base == original.base)
			return prefix;
	}
	// Some Apple release titles use a plain, non-bracketed suffix, e.g.
	// "eternal sunshine deluxe: brighter days ahead".
	const std::string normalized = normalizeName(album);
	for (const std::string marker : {" deluxe ", " deluxe", " expanded ", " anniversary edition "}) {
		const auto at = normalized.find(marker);
		if (at != std::string::npos && at >= 3)
			return normalized.substr(0, at);
	}
	return {};
}

bool compatibleMotionAlbumTitle(const std::string &requested, const std::string &returned)
{
	const AlbumKey want = albumKey(requested);
	const AlbumKey got = albumKey(returned);
	if (want.base.empty() || got.base.empty())
		return false;
	if (want.base == got.base)
		return want.edition == got.edition || want.edition.empty() || got.edition.empty();
	const std::string wantBaseQuery = motionBaseAlbumQuery(requested);
	const std::string gotBaseQuery = motionBaseAlbumQuery(returned);
	// Accept only clearly marked generic promo suffixes, not similar albums.
	return (!wantBaseQuery.empty() && albumKey(wantBaseQuery).base == got.base && got.edition.empty()) ||
	       (!gotBaseQuery.empty() && albumKey(gotBaseQuery).base == want.base && want.edition.empty());
}

bool verifiedSoundtrackTrack(const MotionQuery &nowPlaying, const CatalogAlbum &officialAlbum,
                             const std::string &motionArtist, const std::string &motionAlbum,
                             const std::string &trackArtist, const std::string &trackTitle)
{
	const std::string album = normalizeName(nowPlaying.album);
	if (album.find("soundtrack") == std::string::npos || nowPlaying.title.empty() ||
	    nowPlaying.artist.empty() || officialAlbum.id == 0)
		return false;
	// Require the exact same soundtrack edition, not merely a shared base title.
	if (album != normalizeName(officialAlbum.title) || album != normalizeName(motionAlbum))
		return false;
	if (normalizeName(motionArtist) != normalizeName(officialAlbum.artist) ||
	    normalizeName(trackArtist) != normalizeName(nowPlaying.artist) ||
	    normalizeName(trackTitle) != normalizeName(nowPlaying.title))
		return false;
	return true;
}

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
