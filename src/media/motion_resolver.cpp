// SPDX-License-Identifier: GPL-2.0-or-later
#include "motion_resolver.hpp"

#include <filesystem>
#include <system_error>
#include <vector>

namespace tonearm {

namespace fs = std::filesystem;

namespace {
constexpr size_t kMaxCacheEntries = 512;
constexpr size_t kMaxFolderEntries = 20000; // a motion library is small; refuse to walk huge folders
} // namespace

// --- LocalFolderSource --------------------------------------------------------

LocalFolderSource::LocalFolderSource(std::string folder, std::chrono::milliseconds rescanEvery)
	: folder_(std::move(folder)), rescanEvery_(rescanEvery)
{
}

void LocalFolderSource::rescanIfStale()
{
	const auto now = std::chrono::steady_clock::now();
	if (scanned_ && now - scannedAt_ < rescanEvery_)
		return;
	scanned_ = true;
	scannedAt_ = now;
	std::vector<std::string> names;
	std::error_code ec;
	const fs::path dir(std::u8string(folder_.begin(), folder_.end()));
	for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
		std::error_code fec;
		if (!it->is_regular_file(fec))
			continue;
		const auto u8 = it->path().filename().u8string();
		names.emplace_back(u8.begin(), u8.end());
		if (names.size() >= kMaxFolderEntries)
			break;
	}
	library_.build(names, folder_);
}

std::optional<MotionAsset> LocalFolderSource::resolve(const MotionQuery &q, const std::atomic<bool> &cancelled)
{
	if (folder_.empty() || cancelled)
		return std::nullopt;
	rescanIfStale();
	if (cancelled)
		return std::nullopt;
	return library_.find(q);
}

// --- MotionResolver -----------------------------------------------------------

MotionResolver::MotionResolver(std::unique_ptr<MotionSource> source, Deliver deliver, std::chrono::milliseconds negativeTtl,
			       std::chrono::milliseconds positiveTtl)
	: source_(std::move(source)), deliver_(std::move(deliver)), negativeTtl_(negativeTtl), positiveTtl_(positiveTtl)
{
	thread_ = std::thread([this] { run(); });
}

MotionResolver::~MotionResolver()
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		stop_ = true;
		cancelCurrent_ = true;
	}
	cv_.notify_all();
	if (thread_.joinable())
		thread_.join();
}

std::string MotionResolver::cacheKey(const MotionQuery &q)
{
	return normalizeName(q.artist) + "\x1f" + normalizeName(q.albumArtist) + "\x1f" + q.album + "\x1f" + (q.preferred == MotionShape::Portrait ? "p" : "s");
}

void MotionResolver::request(uint64_t serial, const MotionQuery &q)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		const std::string key = cacheKey(q);
		// A lookup for this very release is already running: let it finish and deliver
		// under the new serial instead of starting a duplicate.
		if (!runningKey_.empty() && runningKey_ == key) {
			pendingSerial_ = serial;
			pending_ = q;
			hasPending_ = true;
			cv_.notify_all();
			return;
		}
		if (!runningKey_.empty())
			cancelCurrent_ = true; // the track changed: the running lookup is obsolete
		pendingSerial_ = serial;
		pending_ = q;
		hasPending_ = true;
	}
	cv_.notify_all();
}

void MotionResolver::clearCache()
{
	std::lock_guard<std::mutex> lock(mutex_);
	cache_.clear();
}

void MotionResolver::run()
{
	std::unique_lock<std::mutex> lock(mutex_);
	for (;;) {
		cv_.wait(lock, [&] { return stop_ || hasPending_; });
		if (stop_)
			return;
		const uint64_t serial = pendingSerial_;
		const MotionQuery q = pending_;
		hasPending_ = false;
		const std::string key = cacheKey(q);

		// Cache hit (positive answers are kept; negative ones expire).
		auto it = cache_.find(key);
		if (it != cache_.end() && std::chrono::steady_clock::now() - it->second.at <
					(it->second.asset ? positiveTtl_ : negativeTtl_)) {
			const auto asset = it->second.asset;
			lock.unlock();
			deliver_(serial, asset);
			lock.lock();
			continue;
		}

		runningKey_ = key;
		cancelCurrent_ = false;
		lock.unlock();
		++lookups_;
		std::optional<MotionAsset> asset;
		try {
			asset = source_->resolve(q, cancelCurrent_);
		} catch (...) {
			asset.reset(); // a failing source must never take the overlay down
		}
		lock.lock();
		runningKey_.clear();
		if (cancelCurrent_ && !stop_) {
			cancelCurrent_ = false;
			continue; // obsolete: a newer request is pending
		}
		if (cache_.size() >= kMaxCacheEntries)
			cache_.clear();
		cache_[key] = {asset, std::chrono::steady_clock::now()};
		// If the same release was re-requested while running, answer the newest serial.
		uint64_t deliverSerial = serial;
		if (hasPending_ && cacheKey(pending_) == key) {
			deliverSerial = pendingSerial_;
			hasPending_ = false;
		}
		if (stop_)
			return;
		lock.unlock();
		deliver_(deliverSerial, asset);
		lock.lock();
	}
}

} // namespace tonearm
