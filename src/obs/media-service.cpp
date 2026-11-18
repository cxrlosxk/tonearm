// SPDX-License-Identifier: GPL-2.0-or-later
#include "media-service.hpp"

#include <obs-module.h>

namespace tonearm {

MediaService &MediaService::instance()
{
	static MediaService service;
	return service;
}

void MediaService::acquire()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (refs_++ == 0 && !provider_) {
		provider_ = createSystemProvider();
		provider_->start();
		blog(LOG_INFO, "[tonearm] media provider started");
	}
}

void MediaService::release()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (refs_ > 0)
		--refs_;
	// Keep the provider alive between scene switches; it is cheap while idle and
	// avoids re-enumerating sessions. It stops on module unload.
}

void MediaService::shutdown()
{
	std::unique_ptr<MediaProvider> p;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		p = std::move(provider_);
		refs_ = 0;
	}
	if (p) {
		p->stop();
		blog(LOG_INFO, "[tonearm] media provider stopped");
	}
}

std::shared_ptr<const MediaSnapshot> MediaService::snapshot()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!provider_)
		return std::make_shared<MediaSnapshot>();
	return provider_->snapshot();
}

} // namespace tonearm
