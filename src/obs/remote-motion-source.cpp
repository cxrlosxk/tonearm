// SPDX-License-Identifier: GPL-2.0-or-later
#include "remote-motion-source.hpp"
#include "hls-cache.hpp"
#include "motion-index.hpp"
#include <obs-module.h>
#include "media/catalog_match.hpp"

#ifdef _WIN32

#include <obs-data.h>

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tonearm {
namespace {

struct HttpHandle {
	HINTERNET handle = nullptr;
	explicit HttpHandle(HINTERNET value) : handle(value) {}
	~HttpHandle() { if (handle) WinHttpCloseHandle(handle); }
	HttpHandle(const HttpHandle &) = delete;
	HttpHandle &operator=(const HttpHandle &) = delete;
	explicit operator bool() const { return handle != nullptr; }
	operator HINTERNET() const { return handle; }
};

std::string percentEncode(const std::string &text)
{
	static constexpr char hex[] = "0123456789ABCDEF";
	std::string result;
	for (unsigned char c : text) {
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			result += static_cast<char>(c);
		} else {
			result += '%';
			result += hex[c >> 4];
			result += hex[c & 15];
		}
	}
	return result;
}

// Don't allow a network service to feed arbitrary local files or unrelated URLs
// into OBS's FFmpeg decoder.
bool trustedVideoUrl(const std::string &url)
{
	if (url.size() < 12 || url.size() > 4096 || url.compare(0, 8, "https://") != 0)
		return false;
	const size_t stop = url.find_first_of("/?#", 8);
	std::string hostname = url.substr(8, stop == std::string::npos ? std::string::npos : stop - 8);
	if (hostname.empty() || hostname.find_first_of("@:\\") != std::string::npos)
		return false;
	std::transform(hostname.begin(), hostname.end(), hostname.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	auto allowed = [&](const std::string &domain) {
		return hostname == domain ||
		       (hostname.size() > domain.size() &&
			hostname.compare(hostname.size() - domain.size(), domain.size(), domain) == 0 &&
			hostname[hostname.size() - domain.size() - 1] == '.');
	};
	return allowed("apple.com") || allowed("mzstatic.com");
}

std::string getJson(const wchar_t *hostname, const std::string &path,
		    const std::atomic<bool> &cancelled, DWORD *responseStatus)
{
	if (responseStatus)
		*responseStatus = 0;
	if (cancelled)
		return {};
	const std::wstring widePath(path.begin(), path.end());

	HttpHandle session(WinHttpOpen(L"Tonearm/0.2 (OBS Studio)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
				      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session)
		return {};
	// A failed service must never leave the plugin stuck or block the graphics thread.
	WinHttpSetTimeouts(session, 2500, 2500, 4500, 4500);
	HttpHandle connection(WinHttpConnect(session, hostname, INTERNET_DEFAULT_HTTPS_PORT, 0));
	if (!connection || cancelled)
		return {};
	HttpHandle request(WinHttpOpenRequest(connection, L"GET", widePath.c_str(), nullptr, WINHTTP_NO_REFERER,
					    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
	if (!request || cancelled)
		return {};
	if (!WinHttpSendRequest(request, L"Accept: application/json\r\n", static_cast<DWORD>(-1),
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || cancelled)
		return {};
	if (!WinHttpReceiveResponse(request, nullptr) || cancelled)
		return {};
	DWORD status = 0, size = sizeof(status);
	if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX))
		return {};
	if (responseStatus)
		*responseStatus = status;
	if (status != 200)
		return {};

	std::string body;
	for (;;) {
		if (cancelled)
			return {};
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available))
			return {};
		if (available == 0)
			break;
		if (available > 65536 || body.size() + available > 65536)
			return {};
		std::string chunk(available, '\0');
		DWORD read = 0;
		if (!WinHttpReadData(request, chunk.data(), available, &read))
			return {};
		if (!read)
			break;
		body.append(chunk.data(), read);
	}
	return body;
}

std::string getArtworkJson(const std::string &artist, const std::string &album,
			   const std::atomic<bool> &cancelled, DWORD *responseStatus)
{
	const std::string path = "/api/v1/artwork/search?artist=" + percentEncode(artist) +
				 "&album=" + percentEncode(album);
	return getJson(L"artwork.m8tec.top", path, cancelled, responseStatus);
}

// Track performers and soundtrack composers often differ (e.g. Halle versus
// Alan Menken & Howard Ashman). Never accept the artist mismatch just because
// two soundtrack titles look similar. Validate both the official album credit
// and the exact track/performer using Apple's public iTunes Search/Lookup API.
bool verifySoundtrackPerformer(const MotionQuery &q, const std::string &motionArtist,
			      const std::string &motionAlbum, const std::atomic<bool> &cancelled)
{
	if (cancelled || q.title.empty() || q.artist.empty() ||
	    normalizeName(q.album).find("soundtrack") == std::string::npos ||
	    normalizeName(q.album) != normalizeName(motionAlbum))
		return false;
	DWORD status = 0;
	const std::string catalogPath = "/search?term=" + percentEncode(q.album) +
					"&entity=album&country=US&limit=35";
	const std::string jsonText = getJson(L"itunes.apple.com", catalogPath, cancelled, &status);
	if (status != 200 || jsonText.empty() || cancelled)
		return false;
	obs_data_t *root = obs_data_create_from_json(jsonText.c_str());
	if (!root)
		return false;
	std::vector<CatalogAlbum> candidates;
	obs_data_array_t *albums = obs_data_get_array(root, "results");
	if (albums) {
		const size_t limit = std::min<size_t>(obs_data_array_count(albums), 35);
		for (size_t i = 0; i < limit; ++i) {
			obs_data_t *item = obs_data_array_item(albums, i);
			if (!item)
				continue;
			const std::string officialArtist = obs_data_get_string(item, "artistName");
			const std::string officialAlbum = obs_data_get_string(item, "collectionName");
			const auto rawId = obs_data_get_int(item, "collectionId");
			obs_data_release(item);
			if (rawId <= 0 || normalizeName(officialAlbum) != normalizeName(q.album) ||
			    normalizeName(officialArtist) != normalizeName(motionArtist))
				continue;
			candidates.push_back({static_cast<std::uint64_t>(rawId), officialArtist, officialAlbum});
			if (candidates.size() >= 4)
				break;
		}
		obs_data_array_release(albums);
	}
	obs_data_release(root);
	// Catalog verification is bounded so switching tracks never starts a long
	// sequence of public API calls.
	for (const auto &album : candidates) {
		if (cancelled)
			return false;
		const auto path = "/lookup?id=" + std::to_string(album.id) + "&entity=song&limit=200";
		status = 0;
		const std::string tracks = getJson(L"itunes.apple.com", path, cancelled, &status);
		if (status != 200 || tracks.empty() || cancelled)
			continue;
		obs_data_t *catalog = obs_data_create_from_json(tracks.c_str());
		if (!catalog)
			continue;
		bool verified = false;
		obs_data_array_t *items = obs_data_get_array(catalog, "results");
		if (items) {
			const size_t count = std::min<size_t>(obs_data_array_count(items), 200);
			for (size_t i = 0; i < count && !verified; ++i) {
				obs_data_t *item = obs_data_array_item(items, i);
				if (!item)
					continue;
				const auto trackAlbumId = obs_data_get_int(item, "collectionId");
				const std::string artist = obs_data_get_string(item, "artistName");
				const std::string title = obs_data_get_string(item, "trackName");
				obs_data_release(item);
				if (trackAlbumId == static_cast<int64_t>(album.id))
					verified = verifiedSoundtrackTrack(q, album, motionArtist, motionAlbum, artist, title);
			}
			obs_data_array_release(items);
		}
		obs_data_release(catalog);
		if (verified) {
			blog(LOG_INFO, "[tonearm] soundtrack artist credit verified through official iTunes track list");
			return true;
		}
	}
	return false;
}

// The service's artist/album search has a long-lived negative cache and may
// incorrectly return 404 for real albums. Public iTunes Search can identify
// their catalog IDs without any Apple Music session credentials; the independent
// artwork service also exposes an album-URL lookup route.
std::optional<MotionAsset> resolveWithCatalogId(const MotionQuery &q, const std::atomic<bool> &cancelled)
{
	struct Candidate {
		std::uint64_t id;
		int priority;
	};
	std::vector<Candidate> candidates;
	auto scanItunes = [&](const std::string &album) {
		const std::string term = q.artist + " " + album;
		const std::string path = "/search?term=" + percentEncode(term) + "&entity=album&country=US&limit=25";
		DWORD status = 0;
		const std::string jsonText = getJson(L"itunes.apple.com", path, cancelled, &status);
		if (status != 200 || jsonText.empty() || cancelled)
			return;
		obs_data_t *root = obs_data_create_from_json(jsonText.c_str());
		if (!root)
			return;
		obs_data_array_t *items = obs_data_get_array(root, "results");
		if (items) {
			const size_t n = obs_data_array_count(items);
			for (size_t i = 0; i < n; ++i) {
				obs_data_t *item = obs_data_array_item(items, i);
				if (!item)
					continue;
				const std::string artist = obs_data_get_string(item, "artistName");
				const std::string title = obs_data_get_string(item, "collectionName");
				const auto rawId = obs_data_get_int(item, "collectionId");
				obs_data_release(item);
				if (rawId <= 0 || !catalogArtistMatches(q, artist) ||
				    !compatibleMotionAlbumTitle(q.album, title))
					continue;
				const int priority = albumKey(q.album) == albumKey(title) ? 0 : 1;
				const auto id = static_cast<std::uint64_t>(rawId);
				if (std::find_if(candidates.begin(), candidates.end(),
						 [&](const Candidate &c) { return c.id == id; }) == candidates.end())
					candidates.push_back({id, priority});
			}
			obs_data_array_release(items);
		}
		obs_data_release(root);
	};

	scanItunes(q.album);
	const std::string base = motionBaseAlbumQuery(q.album);
	if (candidates.empty() && !base.empty() && !cancelled)
		scanItunes(base);
	std::stable_sort(candidates.begin(), candidates.end(),
			 [](const Candidate &a, const Candidate &b) { return a.priority < b.priority; });

	// Try a handful of matching catalog releases, never a fuzzy first hit.
	// Verify the API's actual returned artist + album again before using media.
	size_t lookedUp = 0;
	for (const auto &candidate : candidates) {
		if (cancelled || ++lookedUp > 4)
			break;
		const std::string publicUrl = "https://music.apple.com/album/" + std::to_string(candidate.id);
		const std::string path = "/api/v1/artwork/url?url=" + percentEncode(publicUrl);
		DWORD status = 0;
		const auto body = getJson(L"artwork.m8tec.top", path, cancelled, &status);
		if (cancelled || status == 429 || status == 503 || status == 0)
			break;
		if (body.empty())
			continue;
		obs_data_t *json = obs_data_create_from_json(body.c_str());
		if (!json)
			continue;
		const std::string foundArtist = obs_data_get_string(json, "artist");
		const std::string foundAlbum = obs_data_get_string(json, "album");
		const std::string square = obs_data_get_string(json, "url");
		const std::string tall = obs_data_get_string(json, "url_tall");
		obs_data_release(json);

		if (!compatibleMotionAlbumTitle(q.album, foundAlbum) ||
		    (!catalogArtistMatches(q, foundArtist) &&
		     !verifySoundtrackPerformer(q, foundArtist, foundAlbum, cancelled)))
			continue;
		const bool portrait = q.preferred == MotionShape::Portrait;
		const std::string chosen = portrait && trustedVideoUrl(tall) ? tall :
					   trustedVideoUrl(square) ? square : trustedVideoUrl(tall) ? tall : "";
		if (chosen.empty())
			continue;
		auto cached = cacheMotionHls(chosen, cancelled, q.quality);
		if (cached) {
			blog(LOG_INFO, "[tonearm] animated artwork found via public album ID fallback");
			MotionAsset asset{*cached, chosen == tall ? MotionShape::Portrait : MotionShape::Square};
			rememberMotionForAlbum(q, asset);
			return asset;
		}
		break; // Avoid downloading the same failed source multiple times.
	}
	return std::nullopt;
}
} // namespace

std::optional<MotionAsset> RemoteArtworkApiSource::resolve(const MotionQuery &q, const std::atomic<bool> &cancelled)
{
	if (q.artist.empty() || q.album.empty() || cancelled)
		return std::nullopt;
	// A confirmed local album match should display immediately on replay.
	// Avoid the external metadata service entirely when we already own a
	// complete, validated motion video for this precise release and shape.
	if (auto indexed = cachedMotionForAlbum(q)) {
		blog(LOG_INFO, "[tonearm] instant animated album cache hit");
		return indexed;
	}

	struct Lookup {
		std::string artist, album;
	};
	std::vector<Lookup> attempts;
	auto add = [&](const std::string &artist, const std::string &album) {
		if (artist.empty() || album.empty())
			return;
		for (const auto &item : attempts)
			if (normalizeName(item.artist) == normalizeName(artist) &&
			    normalizeName(item.album) == normalizeName(album))
				return;
		attempts.push_back({artist, album});
	};

	// The service can negatively cache the exact artist/album search for a
	// month. Prefer the album (not track title) so a title mismatch cannot
	// poison its cache, then try the album artist / track artist separately.
	const std::string albumArtist = q.albumArtist.empty() ? q.artist : q.albumArtist;
	const std::string base = motionBaseAlbumQuery(q.album);
	add(albumArtist, q.album);
	add(q.artist, q.album);
	if (!base.empty()) {
		add(albumArtist, base);
		add(q.artist, base);
	}
	const auto performerKeys = artistKeys(q.artist);
	if (performerKeys.size() > 1 && attempts.size() < 5)
		add(performerKeys.back(), base.empty() ? q.album : base);

	size_t checked = 0;
	for (const auto &search : attempts) {
		if (cancelled)
			return std::nullopt;
		DWORD status = 0;
		++checked;
		const std::string body = getArtworkJson(search.artist, search.album, cancelled, &status);
		if (cancelled)
			return std::nullopt;
		if (status == 429 || status == 503 || status == 0) {
			blog(LOG_INFO, "[tonearm] motion lookup temporarily unavailable (HTTP %lu); using still cover",
			     static_cast<unsigned long>(status));
			break; // Respect provider rate limits and transient failures.
		}
		if (body.empty())
			continue;
		obs_data_t *json = obs_data_create_from_json(body.c_str());
		if (!json)
			continue;
		const std::string responseArtist = obs_data_get_string(json, "artist");
		const std::string responseAlbum = obs_data_get_string(json, "album");
		const std::string square = obs_data_get_string(json, "url");
		const std::string tall = obs_data_get_string(json, "url_tall");
		obs_data_release(json);

		if (!compatibleMotionAlbumTitle(q.album, responseAlbum) ||
		    (!catalogArtistMatches(q, responseArtist) &&
		     !verifySoundtrackPerformer(q, responseArtist, responseAlbum, cancelled))) {
			blog(LOG_INFO, "[tonearm] rejected ambiguous motion artwork candidate; exact release not confirmed");
			continue;
		}
		const bool portrait = q.preferred == MotionShape::Portrait;
		const std::string chosen = portrait && trustedVideoUrl(tall) ? tall :
					   trustedVideoUrl(square) ? square : trustedVideoUrl(tall) ? tall : "";
		if (chosen.empty())
			continue;
		auto cached = cacheMotionHls(chosen, cancelled, q.quality);
		if (cached) {
			blog(LOG_INFO, "[tonearm] animated artwork ready (query %zu/%zu, %s rendition)",
			     checked, attempts.size(), q.quality == MotionQuality::Maximum ? "maximum" : "balanced");
			MotionAsset asset{*cached, chosen == tall ? MotionShape::Portrait : MotionShape::Square};
			rememberMotionForAlbum(q, asset);
			return asset;
		}
		if (!cancelled)
			blog(LOG_WARNING, "[tonearm] motion URL found but video cache/decoder preparation failed");
		// Alternate artist spellings for this same album won't repair a broken
		// video URL. Don't repeatedly download the same large asset.
		break;
	}
	if (!cancelled) {
		const auto catalog = resolveWithCatalogId(q, cancelled);
		if (catalog)
			return catalog;
	}
	blog(LOG_INFO, "[tonearm] no compatible animated artwork resolved (%zu metadata queries); using still cover", checked);
	return std::nullopt;
}

} // namespace tonearm
#endif
