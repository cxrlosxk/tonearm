// SPDX-License-Identifier: GPL-2.0-or-later
#include "check.hpp"

#include "core/media.hpp"

using namespace tonearm;

namespace {
const char *kAppleMusic = "AppleInc.AppleMusicWin_nzyj5cx40ttba!App";
const char *kSpotify = "Spotify.exe";
const char *kChrome = "Chrome";

MediaSession session(const char *app, PlaybackStatus st, const char *title = "Song")
{
	MediaSession m;
	m.appId = app;
	m.appName = friendlyAppName(app);
	m.status = st;
	m.track.title = title;
	m.track.artist = "Artist";
	return m;
}
} // namespace

TEST("media: timeline extrapolates while playing and holds while paused")
{
	Timeline t;
	t.known = true;
	t.durationSec = 200;
	t.positionSec = 30;
	t.sampledAt = Clock::now();
	const auto later = t.sampledAt + std::chrono::milliseconds(2500);
	CHECK_NEAR(t.positionAt(later, true), 32.5, 1e-6);
	CHECK_NEAR(t.positionAt(later, false), 30.0, 1e-6);
	t.rate = 2.0;
	CHECK_NEAR(t.positionAt(later, true), 35.0, 1e-6);
	// Never runs past the end, never reports a position for unknown timelines.
	CHECK_NEAR(t.positionAt(t.sampledAt + std::chrono::hours(1), true), 200.0, 1e-6);
	t.known = false;
	CHECK_NEAR(t.positionAt(later, true), 0.0, 1e-6);
}

TEST("media: Apple Music artist and album are split")
{
	TrackInfo t;
	t.title = "  Night Drive ";
	t.artist = "Lena Hart \xE2\x80\x94 Afterglow (Deluxe)";
	normalizeMetadata(t, kAppleMusic);
	CHECK(t.title == "Night Drive");
	CHECK(t.artist == "Lena Hart");
	CHECK(t.album == "Afterglow (Deluxe)");
}

TEST("media: the Apple Music split never applies to other players or real album fields")
{
	TrackInfo spotify;
	spotify.artist = "Lena Hart \xE2\x80\x94 Afterglow";
	normalizeMetadata(spotify, kSpotify);
	CHECK(spotify.artist == "Lena Hart \xE2\x80\x94 Afterglow");
	CHECK(spotify.album.empty());

	TrackInfo apple;
	apple.artist = "A \xE2\x80\x94 B";
	apple.album = "Real Album";
	normalizeMetadata(apple, kAppleMusic);
	CHECK(apple.artist == "A \xE2\x80\x94 B");

	TrackInfo onlyAlbumArtist;
	onlyAlbumArtist.title = "x";
	onlyAlbumArtist.albumArtist = "Various";
	normalizeMetadata(onlyAlbumArtist, kSpotify);
	CHECK(onlyAlbumArtist.artist == "Various");
}

TEST("media: apps are recognised from their Windows ids")
{
	CHECK(classifyApp(kAppleMusic) == AppKind::AppleMusic);
	CHECK(classifyApp(kSpotify) == AppKind::Spotify);
	CHECK(classifyApp(kChrome) == AppKind::Browser);
	CHECK(classifyApp("308046B0AF4A39CB") == AppKind::Browser); // Firefox
	CHECK(classifyApp("Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic") == AppKind::OtherPlayer);
	CHECK(classifyApp("Search.exe") == AppKind::OtherPlayer); // contains "arc" but is not Arc
	CHECK(friendlyAppName(kAppleMusic) == "Apple Music");
	CHECK(friendlyAppName(kSpotify) == "Spotify");
	CHECK(appKeyword(kAppleMusic) == "applemusic");
	CHECK(appKeyword("Contoso.Player_abc123!App") == "player");
	CHECK(appKeyword("SomePlayer.exe") == "someplayer");
}

TEST("media: a playing session wins over a paused one")
{
	MediaSnapshot s;
	s.sessions = {session(kAppleMusic, PlaybackStatus::Paused), session(kSpotify, PlaybackStatus::Playing)};
	CHECK(selectSession(s, {}, "") == 1);
	s.systemCurrentAppId = kAppleMusic;
	CHECK(selectSession(s, {}, kAppleMusic) == 1);
}

TEST("media: when nothing plays, priority, then stickiness, then Windows decide")
{
	MediaSnapshot s;
	s.sessions = {session(kAppleMusic, PlaybackStatus::Paused), session(kSpotify, PlaybackStatus::Paused)};
	SelectionPolicy prefer;
	prefer.mode = SelectionPolicy::Mode::PreferList;
	prefer.priority = {"spotify", "applemusic"};
	CHECK(selectSession(s, prefer, kAppleMusic) == 1);

	CHECK(selectSession(s, {}, kAppleMusic) == 0); // sticky
	s.systemCurrentAppId = kSpotify;
	CHECK(selectSession(s, {}, "") == 1);           // Windows' current session
}

TEST("media: browsers are ignored unless allowed or listed")
{
	MediaSnapshot s;
	s.sessions = {session(kChrome, PlaybackStatus::Playing)};
	CHECK(selectSession(s, {}, "") == -1);
	SelectionPolicy allow;
	allow.allowBrowsers = true;
	CHECK(selectSession(s, allow, "") == 0);
	SelectionPolicy listed;
	listed.mode = SelectionPolicy::Mode::PreferList;
	listed.priority = {"chrome"};
	CHECK(selectSession(s, listed, "") == 0);
}

TEST("media: only-list mode excludes everything else")
{
	MediaSnapshot s;
	s.sessions = {session(kSpotify, PlaybackStatus::Playing), session(kAppleMusic, PlaybackStatus::Paused)};
	SelectionPolicy only;
	only.mode = SelectionPolicy::Mode::OnlyList;
	only.priority = {"applemusic"};
	CHECK(selectSession(s, only, "") == 1);
	only.priority = {"tidal"};
	CHECK(selectSession(s, only, "") == -1);
}

TEST("media: sessions without metadata are skipped")
{
	MediaSnapshot s;
	s.sessions = {session(kSpotify, PlaybackStatus::Playing, "")};
	s.sessions[0].track.artist.clear();
	CHECK(selectSession(s, {}, "") == -1);
	CHECK(selectSession(MediaSnapshot{}, {}, "") == -1);
}

TEST("media: content hash distinguishes artwork bytes")
{
	const uint8_t a[] = {1, 2, 3, 4};
	const uint8_t b[] = {1, 2, 3, 5};
	CHECK(hashBytes(a, 4) == hashBytes(a, 4));
	CHECK(hashBytes(a, 4) != hashBytes(b, 4));
}
