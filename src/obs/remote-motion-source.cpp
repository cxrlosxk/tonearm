// SPDX-License-Identifier: GPL-2.0-or-later
#include "remote-motion-source.hpp"
#include "hls-cache.hpp"
#include <obs-module.h>
#include "media/catalog_match.hpp"

#ifdef _WIN32

#include <obs-data.h>

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
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

std::string getArtworkJson(const std::string &artist, const std::string &album, const std::string &title,
			   const std::atomic<bool> &cancelled)
{
	if (cancelled)
		return {};
	std::string path = "/api/v1/artwork/search?artist=" + percentEncode(artist) +
			   "&album=" + percentEncode(album);
	if (!title.empty())
		path += "&title=" + percentEncode(title);
	const std::wstring widePath(path.begin(), path.end());

	HttpHandle session(WinHttpOpen(L"Tonearm/0.2 (OBS Studio)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
				      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session)
		return {};
	// A failed service must never leave the plugin stuck or block the graphics thread.
	WinHttpSetTimeouts(session, 2500, 2500, 4500, 4500);
	HttpHandle connection(WinHttpConnect(session, L"artwork.m8tec.top", INTERNET_DEFAULT_HTTPS_PORT, 0));
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
				 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) || status != 200)
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
} // namespace

std::optional<MotionAsset> RemoteArtworkApiSource::resolve(const MotionQuery &q, const std::atomic<bool> &cancelled)
{
	if (q.artist.empty() || q.album.empty() || cancelled)
		return std::nullopt;

	const std::string artist = q.albumArtist.empty() ? q.artist : q.albumArtist;
	const std::string body = getArtworkJson(artist, q.album, q.title, cancelled);
	if (body.empty() || cancelled)
		return std::nullopt;

	obs_data_t *json = obs_data_create_from_json(body.c_str());
	if (!json)
		return std::nullopt;
	const std::string responseArtist = obs_data_get_string(json, "artist");
	const std::string responseAlbum = obs_data_get_string(json, "album");
	const std::string square = obs_data_get_string(json, "url");
	const std::string tall = obs_data_get_string(json, "url_tall");
	obs_data_release(json);

	// This service uses fuzzy searching. Enforce exact release identity locally.
	if (!(albumKey(responseAlbum) == albumKey(q.album)))
		return std::nullopt;
	if (!catalogArtistMatches(q, responseArtist))
		return std::nullopt;

	const bool portrait = q.preferred == MotionShape::Portrait;
	const std::string chosen = portrait && trustedVideoUrl(tall) ? tall :
				   trustedVideoUrl(square) ? square : trustedVideoUrl(tall) ? tall : "";
	if (chosen.empty())
		return std::nullopt;
	// Apple's HLS masters are delivered reliably, but OBS may stall when FFmpeg
	// decodes the moving playlist over HTTP. Cache the complete finite VOD
	// locally first, then give OBS its own normal file-backed HLS playlist.
	// Never pass an incomplete download or a remote URL to the video renderer.
	auto cached = cacheMotionHls(chosen, cancelled);
	if (!cached) {
		blog(LOG_WARNING, "[tonearm] remote animation could not be cached; using still cover");
		return std::nullopt;
	}
	blog(LOG_INFO, "[tonearm] cached genuine animated artwork for offline decoding");
	return MotionAsset{*cached, chosen == tall ? MotionShape::Portrait : MotionShape::Square};
}

} // namespace tonearm
#endif
