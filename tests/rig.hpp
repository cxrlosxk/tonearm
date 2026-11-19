// SPDX-License-Identifier: GPL-2.0-or-later
// Drives a Scene the way the OBS source does, with synchronous rasterization.
#pragma once

#include "support.hpp"

#include "core/demo.hpp"
#include "core/scene.hpp"

#include <set>

struct Rig {
	tonearm::Style style;
	tonearm::Scene scene;
	tonearm::MediaSession session;
	bool hasSession = true;
	tonearm::Clock::time_point base = tonearm::Clock::now();
	std::vector<tonearm::DrawOp> ops;
	int layerRasters = 0;
	tonearm::MotionFrame motion; // what the host reports about animated artwork

	explicit Rig(const tonearm::Style &s) : style(s), scene(s)
	{
		session.appId = "AppleInc.AppleMusicWin_nzyj5cx40ttba!App";
		session.appName = "Apple Music";
		setTrack(0, tonearm::PlaybackStatus::Playing, 30, 0);
	}

	tonearm::Clock::time_point at(double t) const
	{
		return base + std::chrono::duration_cast<tonearm::Clock::duration>(std::chrono::duration<double>(t));
	}

	void setTrack(int index, tonearm::PlaybackStatus st, double pos, double t, bool art = true)
	{
		session.track = tonearm::demoTrack(index, 256, art);
		setTimeline(st, pos, t, tonearm::demoDuration(index));
	}

	void setTimeline(tonearm::PlaybackStatus st, double pos, double t, double duration)
	{
		session.status = st;
		session.timeline.known = true;
		session.timeline.durationSec = duration;
		session.timeline.positionSec = pos;
		session.timeline.sampledAt = at(t);
		++session.version;
	}

	void step(double t)
	{
		tonearm::SceneInput in;
		in.provider = tonearm::ProviderState::Ready;
		in.session = hasSession ? &session : nullptr;
		in.now = at(t);
		in.motion = motion;
		scene.update(t, in);
		if (auto req = scene.layerRequest()) {
			scene.provideLayers(req->key, tonearm::rasterizeLayers(req->style, req->content, testsupport::text()));
			++layerRasters;
			scene.update(t, in);
		}
		if (auto req = scene.timeRequest())
			scene.provideTimes(req->key,
					   tonearm::rasterizeTimes(req->style, *req->set, req->elapsed, req->remaining, testsupport::text()));
		ops.clear();
		scene.buildFrame(t, ops);
	}

	// Steps at 60 fps from `from` to `to` inclusive.
	void run(double from, double to)
	{
		for (double t = from; t <= to + 1e-9; t += 1.0 / 60.0)
			step(t);
	}

	std::set<uint64_t> imageIds() const
	{
		std::set<uint64_t> ids;
		for (const auto &op : ops)
			if (op.kind == tonearm::DrawOp::Kind::Image && op.opacity > 0.01f)
				ids.insert(op.imageId);
		return ids;
	}

	float totalOpacity() const
	{
		float sum = 0;
		for (const auto &op : ops)
			if (op.kind == tonearm::DrawOp::Kind::Image)
				sum += op.opacity;
		return sum;
	}

	// Width of the progress fill, or -1 when there is no progress bar in the frame.
	float progressFill() const
	{
		const tonearm::DrawOp *track = nullptr, *fill = nullptr;
		for (const auto &op : ops) {
			if (op.kind != tonearm::DrawOp::Kind::RoundRect || op.softness > 0)
				continue;
			if (!track)
				track = &op;
			else if (std::fabs(op.rect.y - track->rect.y) < 0.5f)
				fill = &op;
		}
		if (!track)
			return -1;
		return fill ? fill->rect.w / track->rect.w : 0;
	}
};
