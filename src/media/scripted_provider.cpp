// SPDX-License-Identifier: GPL-2.0-or-later
// A scripted player for testing the plugin inside a real OBS without a music
// app: it replays the situations that matter (seek, pause, resume, a long title,
// missing artwork, a second player taking over, the player closing) on a loop.
// Enabled only by the TONEARM_SCRIPTED_MEDIA environment variable.
#include "provider.hpp"

#include "core/demo.hpp"

#include <condition_variable>
#include <thread>

namespace tonearm {

namespace {

class ScriptedProvider final : public MediaProvider {
public:
	~ScriptedProvider() override { stop(); }

	void start() override
	{
		if (!thread_.joinable())
			thread_ = std::thread([this] { run(); });
	}

	void stop() override
	{
		{
			std::lock_guard<std::mutex> l(mutex_);
			stop_ = true;
		}
		cv_.notify_all();
		if (thread_.joinable())
			thread_.join();
	}

private:
	struct Player {
		const char *appId;
		int track = 0;
		PlaybackStatus status = PlaybackStatus::Playing;
		double pos = 0;
		bool art = true;
		bool present = true;
	};

	MediaSession toSession(const Player &p, Clock::time_point now) const
	{
		MediaSession s;
		s.appId = p.appId;
		s.appName = friendlyAppName(p.appId);
		s.status = p.status;
		s.track = demoTrack(p.track, 512, p.art);
		s.timeline.known = true;
		s.timeline.durationSec = demoDuration(p.track);
		s.timeline.positionSec = p.pos;
		s.timeline.sampledAt = now;
		return s;
	}

	void emit(const std::vector<Player> &players, const char *current)
	{
		MediaSnapshot snap;
		snap.state = ProviderState::Ready;
		snap.systemCurrentAppId = current;
		const auto now = Clock::now();
		for (const Player &p : players)
			if (p.present)
				snap.sessions.push_back(toSession(p, now));
		publish(std::move(snap));
	}

	bool sleepFor(double seconds)
	{
		std::unique_lock<std::mutex> l(mutex_);
		return !cv_.wait_for(l, std::chrono::duration<double>(seconds), [&] { return stop_; });
	}

	void run()
	{
		const char *apple = "AppleInc.AppleMusicWin_nzyj5cx40ttba!App";
		const char *spotify = "Spotify.exe";
		for (;;) {
			Player a{apple, 0, PlaybackStatus::Playing, 20};
			Player b{spotify, 2, PlaybackStatus::Paused, 64};
			b.present = false;
			emit({a, b}, apple);                               // 0s: playing
			if (!sleepFor(6)) return;
			a.pos = 26 + 150;                                   // 6s: seek forward
			emit({a, b}, apple);
			if (!sleepFor(4)) return;
			a.pos = 180; a.status = PlaybackStatus::Paused;     // 10s: pause
			emit({a, b}, apple);
			if (!sleepFor(4)) return;
			a.status = PlaybackStatus::Playing;                 // 14s: resume
			emit({a, b}, apple);
			if (!sleepFor(4)) return;
			a.track = 3; a.pos = 0;                             // 18s: next track, long title
			emit({a, b}, apple);
			if (!sleepFor(7)) return;
			a.track = 1; a.pos = 0; a.art = false;              // 25s: track without artwork
			emit({a, b}, apple);
			if (!sleepFor(5)) return;
			a.status = PlaybackStatus::Paused;                  // 30s: another player starts
			b.present = true; b.status = PlaybackStatus::Playing;
			emit({a, b}, spotify);
			if (!sleepFor(6)) return;
			b.present = false; a.present = false;               // 36s: both players close
			emit({a, b}, "");
			if (!sleepFor(5)) return;
			a.present = true; a.track = 4; a.pos = 5; a.art = true; a.status = PlaybackStatus::Playing;
			emit({a, b}, apple);                                // 41s: playing again
			if (!sleepFor(7)) return;
		}
	}

	std::mutex mutex_;
	std::condition_variable cv_;
	bool stop_ = false;
	std::thread thread_;
};

} // namespace

std::unique_ptr<MediaProvider> createScriptedProvider()
{
	return std::make_unique<ScriptedProvider>();
}

} // namespace tonearm
