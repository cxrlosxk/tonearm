// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Finds motion artwork for the playing track off the graphics thread.
//
// A MotionSource answers "is there motion artwork for this release?". The resolver
// runs sources on its own thread, keeps only the newest request (older ones are
// cancelled), caches answers (including "no"), and never runs two lookups for the
// same release at once. Results are delivered through a callback on the resolver
// thread, where the host may do slow work such as opening the media.

#include "core/motion.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace tonearm {

class MotionSource {
public:
	virtual ~MotionSource() = default;
	// May block (disk, network). Must return promptly once `cancelled` becomes true.
	virtual std::optional<MotionAsset> resolve(const MotionQuery &q, const std::atomic<bool> &cancelled) = 0;
	virtual const char *name() const = 0;
};

// Motion artwork files in a local folder (see MotionLibrary for the naming scheme).
// No network access. The folder listing is cached and refreshed at most every few seconds.
class LocalFolderSource final : public MotionSource {
public:
	explicit LocalFolderSource(std::string folder, std::chrono::milliseconds rescanEvery = std::chrono::seconds(10));
	std::optional<MotionAsset> resolve(const MotionQuery &q, const std::atomic<bool> &cancelled) override;
	const char *name() const override { return "local folder"; }

private:
	void rescanIfStale();
	std::string folder_;
	std::chrono::milliseconds rescanEvery_;
	std::chrono::steady_clock::time_point scannedAt_{};
	bool scanned_ = false;
	MotionLibrary library_;
};

class MotionResolver {
public:
	// Called on the resolver thread with the request serial and the answer.
	using Deliver = std::function<void(uint64_t serial, const std::optional<MotionAsset> &asset)>;

	MotionResolver(std::unique_ptr<MotionSource> source, Deliver deliver,
		       std::chrono::milliseconds negativeTtl = std::chrono::seconds(30),
		       std::chrono::milliseconds positiveTtl = std::chrono::minutes(20));
	~MotionResolver(); // cancels and joins

	MotionResolver(const MotionResolver &) = delete;
	MotionResolver &operator=(const MotionResolver &) = delete;

	// Newest request wins; a pending or running older request is cancelled.
	void request(uint64_t serial, const MotionQuery &q);
	// Forget cached answers (e.g. when the library may have changed).
	void clearCache();

	// Diagnostics for tests.
	int lookups() const { return lookups_.load(); }

private:
	void run();
	static std::string cacheKey(const MotionQuery &q);

	std::unique_ptr<MotionSource> source_;
	Deliver deliver_;
	std::chrono::milliseconds negativeTtl_;
	std::chrono::milliseconds positiveTtl_;

	std::mutex mutex_;
	std::condition_variable cv_;
	bool stop_ = false;
	bool hasPending_ = false;
	uint64_t pendingSerial_ = 0;
	MotionQuery pending_;
	std::atomic<bool> cancelCurrent_{false};
	std::string runningKey_;

	struct Cached {
		std::optional<MotionAsset> asset;
		std::chrono::steady_clock::time_point at;
	};
	std::map<std::string, Cached> cache_; // guarded by mutex_, bounded
	std::atomic<int> lookups_{0};
	std::thread thread_;
};

} // namespace tonearm
