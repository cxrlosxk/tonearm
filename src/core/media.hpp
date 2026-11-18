// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "image.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tonearm {

enum class PlaybackStatus { Unknown, Stopped, Paused, Playing };

using Clock = std::chrono::steady_clock;

// Timeline as reported by the player. Position is extrapolated locally while playing,
// because SMTC players only push timeline updates on seeks and state changes.
struct Timeline {
	bool known = false;             // false when the player reports no duration
	double durationSec = 0;
	double positionSec = 0;         // position at `sampledAt`
	double rate = 1.0;
	Clock::time_point sampledAt{};

	double positionAt(Clock::time_point now, bool playing) const;
};

struct TrackInfo {
	std::string title;
	std::string artist;
	std::string album;
	std::string albumArtist;
	ImagePtr artwork;           // decoded, premultiplied; may be null
	uint64_t artworkHash = 0;   // content hash, used to skip re-extraction

	bool hasMetadata() const { return !title.empty() || !artist.empty(); }
	// Identity for transition purposes; artwork-only updates are not a new track.
	std::string identity() const { return title + "\x1f" + artist + "\x1f" + album; }
};

struct MediaSession {
	std::string appId;          // e.g. "AppleInc.AppleMusicWin_nzyj5cx40ttba!App", "Spotify.exe"
	std::string appName;        // friendly name derived from appId
	PlaybackStatus status = PlaybackStatus::Unknown;
	TrackInfo track;
	Timeline timeline;
	uint64_t version = 0;       // bumps on any change
};

enum class ProviderState { Starting, Ready, Unavailable };

// Everything a source needs, delivered as one immutable value.
struct MediaSnapshot {
	ProviderState state = ProviderState::Starting;
	std::vector<MediaSession> sessions;
	std::string systemCurrentAppId; // what Windows considers the current session
	uint64_t version = 0;
};

// Known players. Matching is by substring of the app id, case-insensitive.
enum class AppKind { AppleMusic, Spotify, Browser, OtherPlayer };
AppKind classifyApp(const std::string &appId);
std::string friendlyAppName(const std::string &appId);

// Cleans up player-specific metadata quirks.
// Apple Music for Windows reports "Artist — Album" in the artist field with an empty album.
void normalizeMetadata(TrackInfo &track, const std::string &appId);

// Chooses which session to display.
struct SelectionPolicy {
	enum class Mode { Automatic, PreferList, OnlyList } mode = Mode::Automatic;
	std::vector<std::string> priority;   // app keywords, highest priority first ("applemusic", "spotify")
	bool allowBrowsers = false;          // browsers report ads and videos too, so they are opt-in
};

// Returns index into snapshot.sessions or -1. `previousAppId` keeps the choice sticky
// so two paused players don't flicker between each other.
int selectSession(const MediaSnapshot &snap, const SelectionPolicy &policy, const std::string &previousAppId);

// Lower-case keyword aliases for an app id ("applemusic", "spotify", "chrome", ...).
std::string appKeyword(const std::string &appId);

uint64_t hashBytes(const uint8_t *data, size_t len);

} // namespace tonearm
