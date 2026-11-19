// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#ifdef _WIN32
#include "core/style.hpp"
#include <atomic>
#include <optional>
#include <string>

namespace tonearm {
// Stage an unencrypted finite HTTPS HLS animation in a private per-user cache.
// Returns a local .m3u8 path only after every segment was downloaded successfully.
// Runs only on the existing resolver worker thread, never OBS's graphics thread.
std::optional<std::string> cacheMotionHls(const std::string &playlistUrl, const std::atomic<bool> &cancelled,
					MotionQuality quality = MotionQuality::Maximum);
}
#endif
