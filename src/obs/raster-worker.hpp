// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/scene.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace tonearm {

// Rasterizes layers off the graphics thread. Requests are "latest wins": a newer
// request replaces a pending one, so a burst of metadata updates costs one raster.
class RasterWorker {
public:
	explicit RasterWorker(std::string fontDir);
	~RasterWorker();
	RasterWorker(const RasterWorker &) = delete;
	RasterWorker &operator=(const RasterWorker &) = delete;

	void submit(const LayerRequest &req);
	void submit(const TimeRequest &req);

	// Results, collected on the graphics thread.
	struct LayerResult {
		uint64_t key;
		std::shared_ptr<LayerSet> set;
	};
	struct TimeResult {
		uint64_t key;
		TimeLayers times;
	};
	std::optional<LayerResult> takeLayers();
	std::optional<TimeResult> takeTimes();

	uint64_t lastSubmittedLayerKey() const { return lastLayerKey_; }
	uint64_t lastSubmittedTimeKey() const { return lastTimeKey_; }

private:
	void run();

	std::string fontDir_;
	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable cv_;
	bool stop_ = false;
	std::optional<LayerRequest> pendingLayer_;
	std::optional<TimeRequest> pendingTime_;
	std::optional<LayerResult> doneLayer_;
	std::optional<TimeResult> doneTime_;
	uint64_t lastLayerKey_ = 0, lastTimeKey_ = 0; // touched only by the submitting thread
};

} // namespace tonearm
