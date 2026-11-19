// SPDX-License-Identifier: GPL-2.0-or-later
#include "hls-cache.hpp"

#ifdef _WIN32

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace tonearm {
namespace {
namespace fs = std::filesystem;

struct HttpHandle {
	HINTERNET h = nullptr;
	explicit HttpHandle(HINTERNET value) : h(value) {}
	~HttpHandle() { if (h) WinHttpCloseHandle(h); }
	operator HINTERNET() const { return h; }
	explicit operator bool() const { return h != nullptr; }
	HttpHandle(const HttpHandle &) = delete;
	HttpHandle &operator=(const HttpHandle &) = delete;
};

constexpr size_t kMaxManifest = 256 * 1024;
constexpr size_t kMaxSegment = 20 * 1024 * 1024;
constexpr size_t kMaxEntireAlbum = 100 * 1024 * 1024;
constexpr size_t kMaxSegments = 128;
std::mutex cacheMutex;
void cacheTrace(const char *stage) {
  char flag[2]{};
  if (GetEnvironmentVariableA("TONEARM_HLS_DIAGNOSTICS", flag, 2))
    std::fprintf(stderr, "[Tonearm HLS diagnostic] %s\n", stage);
}

bool appleCdnUrl(const std::string &url)
{
	if (url.size() < 12 || url.size() > 4096 || url.rfind("https://", 0) != 0)
		return false;
	auto end = url.find_first_of("/?#", 8);
	std::string host = url.substr(8, end == std::string::npos ? std::string::npos : end - 8);
	if (host.find_first_of("@:\\") != std::string::npos)
		return false;
	std::transform(host.begin(), host.end(), host.begin(),
		       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	const auto match = [&](const char *domain) {
		const std::string d(domain);
		return host == d || (host.size() > d.size() &&
				    host.compare(host.size() - d.size(), d.size(), d) == 0 &&
				    host[host.size() - d.size() - 1] == '.');
	};
	return match("apple.com") || match("mzstatic.com");
}

std::optional<std::string> absoluteUrl(const std::string &base, std::string child)
{
	if (child.empty() || child.find_first_of("\r\n") != std::string::npos)
		return std::nullopt;
	if (child.rfind("https://", 0) == 0)
		return appleCdnUrl(child) ? std::optional<std::string>{child} : std::nullopt;
	if (child.rfind("http:", 0) == 0)
		return std::nullopt;
	if (child.rfind("//", 0) == 0)
		return appleCdnUrl("https:" + child) ? std::optional<std::string>{"https:" + child} : std::nullopt;
	const size_t originEnd = base.find('/', 8);
	if (originEnd == std::string::npos)
		return std::nullopt;
	std::string origin = base.substr(0, originEnd);
	std::string path;
	if (child[0] == '/') {
		path = origin + child;
	} else {
		std::string basePath = base.substr(0, base.find_first_of("?#"));
		auto slash = basePath.rfind('/');
		if (slash == std::string::npos || slash < originEnd)
			return std::nullopt;
		path = basePath.substr(0, slash + 1) + child;
	}
	// URLs are always used for GET, not as disk paths. Reject traversal.
	if (path.find("/../") != std::string::npos || path.find("/./") != std::string::npos)
		return std::nullopt;
	return appleCdnUrl(path) ? std::optional<std::string>{path} : std::nullopt;
}

bool fetch(const std::string &url, size_t cap, std::string &body, const std::atomic<bool> &cancelled)
{
	if (!appleCdnUrl(url) || cancelled)
		return false;
	std::wstring wide(url.begin(), url.end());
	URL_COMPONENTS parts{};
	parts.dwStructSize = sizeof(parts);
	parts.dwSchemeLength = static_cast<DWORD>(-1);
	parts.dwHostNameLength = static_cast<DWORD>(-1);
	parts.dwUrlPathLength = static_cast<DWORD>(-1);
	parts.dwExtraInfoLength = static_cast<DWORD>(-1);
	if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
		return false;
	const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
	std::wstring requestPath(parts.lpszUrlPath, parts.dwUrlPathLength);
	if (parts.lpszExtraInfo)
		requestPath.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);

	HttpHandle session(WinHttpOpen(L"Tonearm/0.2 (OBS Studio HLS cache)",
				      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
				      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
	if (!session)
		return false;
	WinHttpSetTimeouts(session, 4000, 4000, 7000, 7000);
	HttpHandle connection(WinHttpConnect(session, host.c_str(), parts.nPort, 0));
	if (!connection)
		return false;
	HttpHandle request(WinHttpOpenRequest(connection, L"GET", requestPath.c_str(), nullptr,
					    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
	if (!request || !WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
					    WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || cancelled ||
	    !WinHttpReceiveResponse(request, nullptr))
		return false;
	DWORD status = 0;
	DWORD n = sizeof(status);
	if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
				 WINHTTP_HEADER_NAME_BY_INDEX, &status, &n, WINHTTP_NO_HEADER_INDEX) || status != 200)
		return false;

	body.clear();
	for (;;) {
		if (cancelled)
			return false;
		DWORD available = 0;
		if (!WinHttpQueryDataAvailable(request, &available))
			return false;
		if (!available)
			return true;
		if (available > cap || body.size() > cap - available)
			return false;
		const size_t previous = body.size();
		body.resize(previous + available);
		DWORD received = 0;
		if (!WinHttpReadData(request, body.data() + previous, available, &received) || received == 0)
			return false;
		body.resize(previous + received);
	}
}

std::vector<std::string> lines(const std::string &s)
{
	std::istringstream in(s);
	std::vector<std::string> out;
	std::string line;
	while (std::getline(in, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		out.push_back(std::move(line));
	}
	return out;
}

std::optional<std::string> quotedUri(const std::string &tag)
{
	const auto begin = tag.find("URI=\"");
	if (begin == std::string::npos)
		return std::nullopt;
	const auto start = begin + 5;
	const auto end = tag.find('"', start);
	if (end == std::string::npos)
		return std::nullopt;
	return tag.substr(start, end - start);
}

std::vector<std::string> chooseMediaPlaylists(const std::string &master, const std::string &url, MotionQuality quality)
{
	struct Variant {
		std::string url;
		int area = 0, bandwidth = 0;
		bool h264 = true;
	};
	std::vector<Variant> variants;
	const auto rows = lines(master);
	for (size_t i = 0; i + 1 < rows.size(); ++i) {
		const auto &line = rows[i];
		// EXCLUDE #EXT-X-I-FRAME-STREAM-INF. Those higher-resolution trick-play
		// thumbnails contain sparse keyframes, not continuously moving artwork.
		if (line.rfind("#EXT-X-STREAM-INF:", 0) != 0)
			continue;
		const bool h264 = line.find("avc1.") != std::string::npos;
		const bool hevc = line.find("hvc1.") != std::string::npos;
		if (!h264 && !(quality == MotionQuality::Maximum && hevc))
			continue;
		const auto next = absoluteUrl(url, rows[i + 1]);
		if (!next)
			continue;
		int width = 0, height = 0, bitrate = 0;
		auto pos = line.find("RESOLUTION=");
		if (pos != std::string::npos)
			sscanf_s(line.c_str() + pos, "RESOLUTION=%dx%d", &width, &height);
		pos = line.find("BANDWIDTH=");
		if (pos != std::string::npos)
			sscanf_s(line.c_str() + pos, "BANDWIDTH=%d", &bitrate);
		const int limit = quality == MotionQuality::Maximum ? 2160 : 1080;
		if (width < 1 || height < 1 || width > limit || height > limit)
			continue;
		variants.push_back(Variant{*next, width * height, bitrate, h264});
	}
	std::stable_sort(variants.begin(), variants.end(), [](const Variant &a, const Variant &b) {
		if (a.area != b.area)
			return a.area > b.area;
		if (a.h264 != b.h264)
			return a.h264; // same-size H.264 is more widely compatible.
		return a.bandwidth > b.bandwidth;
	});
	std::vector<std::string> result;
	for (const auto &v : variants)
		if (std::find(result.begin(), result.end(), v.url) == result.end())
			result.push_back(v.url);
	return result;
}

std::string utf8Path(const fs::path &p)
{
	const auto value = p.u8string();
	return {value.begin(), value.end()};
}

std::string extensionFor(const std::string &url)
{
	std::string path = url.substr(0, url.find_first_of("?#"));
	const auto slash = path.rfind('/');
	const auto dot = path.rfind('.');
	if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
		std::string ext = path.substr(dot);
		if (ext == ".ts" || ext == ".m4s" || ext == ".mp4" || ext == ".aac")
			return ext;
	}
	return ".mp4";
}

bool save(const fs::path &path, const std::string &data)
{
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out.write(data.data(), static_cast<std::streamsize>(data.size()));
	return out.good();
}

// Apple's video-on-demand playlists commonly describe BYTE RANGES of a single
// complete fragmented MP4: a small init segment followed by video fragments.
// Reading that MP4 as an ordinary local file is more reliable than asking OBS's
// HLS demuxer to juggle range requests and seeking across remote fragments.
std::optional<std::string> cacheSingleMp4(const std::string &media, const std::string &playlistUrl,
					  const fs::path &dir, const std::atomic<bool> &cancelled)
{
	const auto rows = lines(media);
	if (media.find("#EXT-X-ENDLIST") == std::string::npos ||
	    media.find("#EXT-X-KEY:") != std::string::npos)
		return std::nullopt;

	std::optional<std::string> sameFile;
	size_t segments = 0;
	bool byteranges = false;
	for (const auto &row : rows) {
		if (row.rfind("#EXT-X-MAP:", 0) == 0) {
			const auto uri = quotedUri(row);
			if (!uri)
				return std::nullopt;
			const auto full = absoluteUrl(playlistUrl, *uri);
			if (!full)
				return std::nullopt;
			if (sameFile && *sameFile != *full)
				return std::nullopt;
			sameFile = *full;
		} else if (row.rfind("#EXT-X-BYTERANGE:", 0) == 0) {
			byteranges = true;
		} else if (!row.empty() && row[0] != '#') {
			const auto full = absoluteUrl(playlistUrl, row);
			if (!full || (sameFile && *sameFile != *full))
				return std::nullopt;
			sameFile = *full;
			++segments;
		}
	}
	if (!sameFile || !byteranges || segments < 1 || segments > kMaxSegments ||
	    extensionFor(*sameFile) != ".mp4")
		return std::nullopt;

	std::string mp4;
	if (!fetch(*sameFile, kMaxEntireAlbum, mp4, cancelled)) {
		cacheTrace("failed to download single Apple MP4");
		return std::nullopt;
	}
	// fMP4 video starts with an ISO BMFF file-type box; do not cache HTML,
	// an error response, or a random binary mislabeled as an MP4.
	if (mp4.size() < 4096 || mp4.compare(4, 4, "ftyp") != 0) {
		cacheTrace("single Apple MP4 response missing ftyp");
		return std::nullopt;
	}
	if (!save(dir / L"artwork.mp4", mp4) || !save(dir / L".complete", "1"))
		return std::nullopt;
	cacheTrace("cached entire finite Apple MP4");
	return utf8Path(dir / L"artwork.mp4");
}

std::optional<std::string> createCache(const std::string &media, const std::string &url, const fs::path &dir,
				       const std::atomic<bool> &cancelled)
{
	const auto rows = lines(media);
	if (rows.empty() || rows[0] != "#EXTM3U" || media.find("#EXT-X-ENDLIST") == std::string::npos) {
		cacheTrace("media playlist missing ENDLIST");
		return std::nullopt;
	} // Only finite VOD artwork can loop reliably.
	if (media.find("#EXT-X-KEY:") != std::string::npos ||
	    media.find("#EXT-X-SESSION-KEY:") != std::string::npos ||
	    media.find("#EXT-X-STREAM-INF:") != std::string::npos) {
		cacheTrace("unsupported encrypted or master playlist");
		return std::nullopt;
	} // Never decrypt media or accidentally cache a master.

	std::string rewritten;
	size_t total = 0, count = 0;
	bool sawVideo = false;
	for (const auto &row : rows) {
		if (cancelled)
			return std::nullopt;
		if (row.rfind("#EXT-X-MAP:", 0) == 0) {
			auto name = quotedUri(row);
			if (!name)
				return std::nullopt;
			auto remote = absoluteUrl(url, *name);
			if (!remote)
				return std::nullopt;
			std::string data;
			if (!fetch(*remote, kMaxSegment, data, cancelled) ||
			    data.size() > kMaxEntireAlbum - total)
				return std::nullopt;
			total += data.size();
			std::string local = "init" + extensionFor(*remote);
			if (!save(dir / fs::u8path(local), data))
				return std::nullopt;
			std::string changed = row;
			changed.replace(changed.find("URI=\"") + 5, name->size(), local);
			rewritten += changed + "\n";
		} else if (row.empty() || row[0] == '#') {
			// Unknown instructions referencing remote keys must not be forwarded.
			if (row.find("URI=") != std::string::npos && row.rfind("#EXT-X-MAP:", 0) != 0)
				return std::nullopt;
			rewritten += row + "\n";
		} else {
			if (++count > kMaxSegments)
				return std::nullopt;
			auto remote = absoluteUrl(url, row);
			if (!remote)
				return std::nullopt;
			std::string data;
			if (!fetch(*remote, kMaxSegment, data, cancelled) ||
			    data.size() > kMaxEntireAlbum - total)
				return std::nullopt;
			total += data.size();
			std::ostringstream name;
			name << "segment-" << std::setfill('0') << std::setw(3) << count << extensionFor(*remote);
			if (!save(dir / fs::u8path(name.str()), data))
				return std::nullopt;
			rewritten += name.str() + "\n";
			sawVideo = true;
		}
	}
	if (!sawVideo || !save(dir / L"artwork.m3u8", rewritten) ||
	    !save(dir / L".complete", "1"))
		return std::nullopt;
	return utf8Path(dir / L"artwork.m3u8");
}

std::wstring cacheKey(const std::string &url)
{
	uint64_t h = 1469598103934665603ULL;
	for (unsigned char c : url) {
		h ^= c;
		h *= 1099511628211ULL;
	}
	std::wostringstream out;
	out << std::hex << std::setw(16) << std::setfill(L'0') << h;
	return out.str();
}
} // namespace

std::optional<std::string> cacheMotionHls(const std::string &playlistUrl, const std::atomic<bool> &cancelled, MotionQuality quality)
{
	if (!appleCdnUrl(playlistUrl) || cancelled)
		return std::nullopt;
	// Avoid collisions between two Tonearm overlays requesting the same album.
	std::lock_guard<std::mutex> lock(cacheMutex);
	wchar_t localAppData[32768]{};
	const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, 32768);
	if (!size || size >= 32768)
		return std::nullopt;
	std::error_code ec;
	const fs::path root = fs::path(localAppData) / L"Tonearm" / L"MotionCache";
	fs::create_directories(root, ec);
	if (ec)
		return std::nullopt;

	// Quality variants must not reuse a previous lower-resolution cached file.
	const auto key = cacheKey(playlistUrl + (quality == MotionQuality::Maximum ? "|max" : "|balanced"));
	const fs::path finished = root / key;
	const fs::path mp4Dest = finished / L"artwork.mp4";
	const fs::path playlistDest = finished / L"artwork.m3u8";
	if (fs::exists(finished / L".complete", ec)) {
		if (fs::exists(mp4Dest, ec))
			return utf8Path(mp4Dest);
		if (fs::exists(playlistDest, ec))
			return utf8Path(playlistDest);
	}

	const fs::path building = root / (key + L"-building");
	auto fail = [&]() -> std::optional<std::string> {
		std::error_code ignored;
		fs::remove_all(building, ignored);
		return std::nullopt;
	};
	std::string master;
	if (!fetch(playlistUrl, kMaxManifest, master, cancelled)) {
		cacheTrace("master GET failed");
		return fail();
	}
	cacheTrace("master loaded");

	const auto candidates = master.find("#EXT-X-STREAM-INF:") != std::string::npos ?
				chooseMediaPlaylists(master, playlistUrl, quality) :
				std::vector<std::string>{playlistUrl};
	if (candidates.empty()) {
		cacheTrace("no compatible full-motion video variants");
		return fail();
	}
	// Quality first, then fall back to less demanding streams if an ultra
	// rendition is unavailable or exceeds the per-video 100 MB safety cap.
	for (const auto &mediaUrl : candidates) {
		if (cancelled)
			return fail();
		ec.clear();
		fs::remove_all(building, ec);
		ec.clear();
		fs::create_directories(building, ec);
		if (ec)
			return fail();

		std::string media;
		if (mediaUrl == playlistUrl)
			media = master;
		else if (!fetch(mediaUrl, kMaxManifest, media, cancelled))
			continue;

		const auto mp4 = cacheSingleMp4(media, mediaUrl, building, cancelled);
		bool ready = static_cast<bool>(mp4);
		if (!ready && media.find("#EXT-X-BYTERANGE:") == std::string::npos)
			ready = static_cast<bool>(createCache(media, mediaUrl, building, cancelled));
		if (!ready)
			continue;

		ec.clear();
		fs::remove_all(finished, ec);
		ec.clear();
		fs::rename(building, finished, ec);
		if (ec) {
			cacheTrace("failed to finalize cache directory");
			return fail();
		}
		cacheTrace("complete high-fidelity local video available");
		return utf8Path(mp4 ? mp4Dest : playlistDest);
	}
	cacheTrace("no video quality rendition downloaded successfully");
	return fail();
}

} // namespace tonearm
#endif
