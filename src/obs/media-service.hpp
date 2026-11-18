// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media/provider.hpp"

#include <memory>
#include <mutex>

namespace tonearm {

// One system media provider shared by every Tonearm source. Started when the
// first source appears, stopped when the module unloads.
class MediaService {
public:
	static MediaService &instance();

	void acquire();
	void release();
	void shutdown(); // module unload: stop regardless of references

	std::shared_ptr<const MediaSnapshot> snapshot();

private:
	std::mutex mutex_;
	std::unique_ptr<MediaProvider> provider_;
	int refs_ = 0;
};

} // namespace tonearm
