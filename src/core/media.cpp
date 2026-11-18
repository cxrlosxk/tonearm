// SPDX-License-Identifier: GPL-2.0-or-later
#include "media.hpp"

#include <algorithm>
#include <cctype>

namespace tonearm {

double Timeline::positionAt(Clock::time_point now, bool playing) const
{
	if (!known)
		return 0;
	double pos = positionSec;
	if (playing && sampledAt.time_since_epoch().count() != 0) {
		const double dt = std::chrono::duration<double>(now - sampledAt).count();
		if (dt > 0)
			pos += dt * rate;
	}
	return std::clamp(pos, 0.0, durationSec > 0 ? durationSec : pos);
}

namespace {

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

bool contains(const std::string &hay, const char *needle)
{
	return hay.find(needle) != std::string::npos;
}

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
		++a;
	while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
		--b;
	return s.substr(a, b - a);
}

struct KnownApp {
	const char *needle;
	const char *keyword;
	const char *name;
	AppKind kind;
};

// Order matters: more specific needles first.
const KnownApp kApps[] = {
	{"applemusic", "applemusic", "Apple Music", AppKind::AppleMusic},
	{"itunes", "itunes", "iTunes", AppKind::AppleMusic},
	{"spotify", "spotify", "Spotify", AppKind::Spotify},
	{"tidal", "tidal", "TIDAL", AppKind::OtherPlayer},
	{"deezer", "deezer", "Deezer", AppKind::OtherPlayer},
	{"amazon music", "amazonmusic", "Amazon Music", AppKind::OtherPlayer},
	{"amazonmobilellc.amazonmusic", "amazonmusic", "Amazon Music", AppKind::OtherPlayer},
	{"cider", "cider", "Cider", AppKind::OtherPlayer},
	{"foobar2000", "foobar2000", "foobar2000", AppKind::OtherPlayer},
	{"musicbee", "musicbee", "MusicBee", AppKind::OtherPlayer},
	{"vlc", "vlc", "VLC", AppKind::OtherPlayer},
	{"zunemusic", "mediaplayer", "Media Player", AppKind::OtherPlayer},
	{"microsoft.media", "mediaplayer", "Media Player", AppKind::OtherPlayer},
	{"youtube music", "ytmusic", "YouTube Music", AppKind::OtherPlayer},
	{"ytmdesktop", "ytmusic", "YouTube Music", AppKind::OtherPlayer},
	{"msedge", "edge", "Microsoft Edge", AppKind::Browser},
	{"chrome", "chrome", "Google Chrome", AppKind::Browser},
	{"firefox", "firefox", "Firefox", AppKind::Browser},
	{"308046b0af4a39cb", "firefox", "Firefox", AppKind::Browser},
	{"opera", "opera", "Opera", AppKind::Browser},
	{"brave", "brave", "Brave", AppKind::Browser},
	{"vivaldi", "vivaldi", "Vivaldi", AppKind::Browser},
	{"arc", "arc", "Arc", AppKind::Browser},
};

const KnownApp *findApp(const std::string &appId)
{
	const std::string id = lower(appId);
	for (const KnownApp &app : kApps) {
		if (contains(id, app.needle)) {
			// "arc" is short, so require it to stand alone in the executable name.
			if (std::string(app.needle) == "arc" && id != "arc.exe" && !contains(id, "thebrowsercompany"))
				continue;
			return &app;
		}
	}
	return nullptr;
}

} // namespace

AppKind classifyApp(const std::string &appId)
{
	const KnownApp *app = findApp(appId);
	return app ? app->kind : AppKind::OtherPlayer;
}

std::string appKeyword(const std::string &appId)
{
	if (const KnownApp *app = findApp(appId))
		return app->keyword;
	std::string id = lower(appId);
	const size_t bang = id.find('!');
	if (bang != std::string::npos)
		id = id.substr(0, bang);
	const size_t underscore = id.find('_');
	if (underscore != std::string::npos)
		id = id.substr(0, underscore);
	if (id.size() > 4 && id.compare(id.size() - 4, 4, ".exe") == 0)
		id.resize(id.size() - 4);
	const size_t dot = id.rfind('.');
	if (dot != std::string::npos && dot + 1 < id.size())
		id = id.substr(dot + 1);
	return id;
}

std::string friendlyAppName(const std::string &appId)
{
	if (const KnownApp *app = findApp(appId))
		return app->name;
	std::string kw = appKeyword(appId);
	if (!kw.empty())
		kw[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(kw[0])));
	return kw.empty() ? std::string("Media player") : kw;
}

void normalizeMetadata(TrackInfo &track, const std::string &appId)
{
	track.title = trim(track.title);
	track.artist = trim(track.artist);
	track.album = trim(track.album);
	track.albumArtist = trim(track.albumArtist);

	if (classifyApp(appId) == AppKind::AppleMusic && track.album.empty()) {
		// Apple Music joins artist and album with an em dash (U+2014, UTF-8 E2 80 94).
		static const std::string kSep = " \xE2\x80\x94 ";
		const size_t pos = track.artist.rfind(kSep);
		if (pos != std::string::npos && pos > 0) {
			track.album = trim(track.artist.substr(pos + kSep.size()));
			track.artist = trim(track.artist.substr(0, pos));
		}
	}
	if (track.artist.empty() && !track.albumArtist.empty())
		track.artist = track.albumArtist;
}

int selectSession(const MediaSnapshot &snap, const SelectionPolicy &policy, const std::string &previousAppId)
{
	const auto &s = snap.sessions;
	auto eligible = [&](const MediaSession &m) {
		if (!policy.allowBrowsers && classifyApp(m.appId) == AppKind::Browser) {
			// A browser explicitly listed by the user is still allowed.
			const std::string kw = appKeyword(m.appId);
			if (std::find(policy.priority.begin(), policy.priority.end(), kw) == policy.priority.end())
				return false;
		}
		if (policy.mode == SelectionPolicy::Mode::OnlyList) {
			const std::string kw = appKeyword(m.appId);
			return std::find(policy.priority.begin(), policy.priority.end(), kw) != policy.priority.end();
		}
		return true;
	};
	auto rank = [&](const MediaSession &m) -> int {
		if (policy.mode == SelectionPolicy::Mode::Automatic)
			return 0;
		const std::string kw = appKeyword(m.appId);
		auto it = std::find(policy.priority.begin(), policy.priority.end(), kw);
		return it == policy.priority.end() ? static_cast<int>(policy.priority.size()) : static_cast<int>(it - policy.priority.begin());
	};

	int best = -1;
	long bestScore = -1;
	for (size_t i = 0; i < s.size(); ++i) {
		const MediaSession &m = s[i];
		if (!eligible(m) || !m.track.hasMetadata())
			continue;
		long score = 0;
		// Playing beats everything else; then the user's priority; then stickiness; then Windows' choice.
		if (m.status == PlaybackStatus::Playing)
			score += 1000000;
		score += (100 - std::min(99, rank(m))) * 1000;
		if (!previousAppId.empty() && m.appId == previousAppId)
			score += 500;
		if (m.appId == snap.systemCurrentAppId)
			score += 100;
		if (m.status == PlaybackStatus::Paused)
			score += 10;
		if (score > bestScore) {
			bestScore = score;
			best = static_cast<int>(i);
		}
	}
	return best;
}

uint64_t hashBytes(const uint8_t *data, size_t len)
{
	// FNV-1a 64-bit.
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < len; ++i) {
		h ^= data[i];
		h *= 1099511628211ull;
	}
	return h;
}

} // namespace tonearm
