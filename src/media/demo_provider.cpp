// SPDX-License-Identifier: GPL-2.0-or-later
#include "provider.hpp"

#include "core/demo.hpp"

namespace tonearm {

DemoProvider::DemoProvider(double trackSeconds) : trackSeconds_(trackSeconds) {}

void DemoProvider::start()
{
	set(0, PlaybackStatus::Playing, 0);
}

void DemoProvider::set(int index, PlaybackStatus status, double positionSec, bool withArtwork)
{
	MediaSession s;
	s.appId = "Tonearm.Sample";
	s.appName = "Sample";
	s.status = status;
	s.track = demoTrack(index, 512, withArtwork);
	s.timeline.known = true;
	s.timeline.durationSec = trackSeconds_ > 0 ? trackSeconds_ : demoDuration(index);
	s.timeline.positionSec = positionSec;
	s.timeline.sampledAt = now_.time_since_epoch().count() ? now_ : Clock::now();
	MediaSnapshot snap;
	snap.state = ProviderState::Ready;
	snap.systemCurrentAppId = s.appId;
	snap.sessions.push_back(std::move(s));
	publish(std::move(snap));
}

} // namespace tonearm
