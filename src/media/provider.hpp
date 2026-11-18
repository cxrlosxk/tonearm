// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/media.hpp"

#include <functional>
#include <memory>
#include <mutex>

namespace tonearm {

// A source of media sessions. Providers do their work on their own threads and
// publish immutable snapshots; readers never block on the operating system.
class MediaProvider {
public:
	virtual ~MediaProvider() = default;
	virtual void start() = 0;
	virtual void stop() = 0;

	std::shared_ptr<const MediaSnapshot> snapshot() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return snapshot_;
	}

protected:
	void publish(MediaSnapshot snap)
	{
		auto next = std::make_shared<MediaSnapshot>(std::move(snap));
		std::lock_guard<std::mutex> lock(mutex_);
		next->version = (snapshot_ ? snapshot_->version : 0) + 1;
		snapshot_ = std::move(next);
	}

private:
	mutable std::mutex mutex_;
	std::shared_ptr<const MediaSnapshot> snapshot_ = std::make_shared<MediaSnapshot>();
};

// System Media Transport Controls on Windows; on other platforms a provider that
// reports itself as unavailable.
std::unique_ptr<MediaProvider> createSystemProvider();

// Replays common player situations on a loop, for testing inside OBS without a
// music app. Selected by the TONEARM_SCRIPTED_MEDIA environment variable.
std::unique_ptr<MediaProvider> createScriptedProvider();

// Simulated player cycling through the fictional sample tracks.
class DemoProvider final : public MediaProvider {
public:
	explicit DemoProvider(double trackSeconds = 0);
	void start() override;
	void stop() override {}

	// Deterministic control for tests and renders.
	void set(int trackIndex, PlaybackStatus status, double positionSec, bool withArtwork = true);
	void setNow(Clock::time_point now) { now_ = now; }

private:
	Clock::time_point now_{};
	double trackSeconds_;
};

} // namespace tonearm
