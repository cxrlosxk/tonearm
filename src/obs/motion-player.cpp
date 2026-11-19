// SPDX-License-Identifier: GPL-2.0-or-later
#include "motion-player.hpp"
#include "remote-motion-source.hpp"

#include <obs-module.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cmath>

namespace tonearm {

namespace {
constexpr double kMaxCaptureFps = 30.0; // plenty for cover loops; keeps GPU copies low
}

MotionPlayer::~MotionPlayer()
{
	teardown();
	if (texrender_) {
		obs_enter_graphics();
		gs_texrender_destroy(texrender_);
		texrender_ = nullptr;
		obs_leave_graphics();
	}
}

void MotionPlayer::teardown()
{
	std::unique_ptr<MotionResolver> resolver;
	obs_source_t *child = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		resolver = std::move(resolver_);
		child = child_;
		child_ = nullptr;
		loadedIdentity_.clear();
		loadedPath_.clear();
		wantedSerial_ = 0;
		wantedIdentity_.clear();
	}
	resolver.reset(); // joins the resolver thread (it may be opening media) outside the lock
	if (child)
		obs_source_release(child);
	++loadGeneration_;
	enabled_ = false;
}

void MotionPlayer::configure(const Style &style)
{
	const bool want = wantsMotion(style);
	pauseWithMusic_ = style.motionPause == MotionPause::WithMusic;
	if (want == enabled_.load() && (!want || style.motionFolder == folder_))
		return;

	teardown();
	folder_ = style.motionFolder;
	if (!want)
		return;

	obs_data_t *settings = obs_data_create();
	obs_data_set_bool(settings, "is_local_file", true);
	obs_data_set_string(settings, "local_file", "");
	obs_data_set_string(settings, "input", "");
	obs_data_set_bool(settings, "looping", true);
	obs_data_set_bool(settings, "restart_on_activate", false);
	obs_data_set_bool(settings, "close_when_inactive", false);
	obs_data_set_bool(settings, "clear_on_media_end", false);
	obs_data_set_bool(settings, "hw_decode", true);
	obs_data_set_bool(settings, "log_changes", false);
	obs_source_t *child = obs_source_create_private("ffmpeg_source", "Tonearm animated artwork", settings);
	obs_data_release(settings);
	if (!child) {
		blog(LOG_WARNING, "[tonearm] animated artwork unavailable: OBS media source could not be created");
		return;
	}
	obs_source_set_muted(child, true); // motion artwork is silent; never feed the mixer

	std::unique_ptr<MotionSource> motionSource;
#ifdef _WIN32
	if (folder_.empty())
		motionSource = std::make_unique<RemoteArtworkApiSource>();
#endif
	if (!motionSource)
		motionSource = std::make_unique<LocalFolderSource>(folder_);
	auto resolver = std::make_unique<MotionResolver>(std::move(motionSource),
							 [this](uint64_t serial, const std::optional<MotionAsset> &asset) { deliver(serial, asset); });
	{
		std::lock_guard<std::mutex> lock(mutex_);
		child_ = child;
		resolver_ = std::move(resolver);
	}
	requestedKey_.clear(); // graphics thread re-requests the current track
	enabled_ = true;
	blog(LOG_INFO, "[tonearm] animated artwork on (%s)", folder_.empty() ? "online lookup" : "local folder");
}

obs_source_t *MotionPlayer::childRef() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	return child_ ? obs_source_get_ref(child_) : nullptr;
}

// Resolver thread.
void MotionPlayer::deliver(uint64_t serial, const std::optional<MotionAsset> &asset)
{
	obs_source_t *child = nullptr;
	std::string identity;
	bool samePath = false;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (serial != wantedSerial_ || !child_)
			return; // obsolete answer
		child = obs_source_get_ref(child_);
		identity = wantedIdentity_;
		samePath = asset && asset->location == loadedPath_;
		if (!asset || !samePath)
			loadedIdentity_.clear(); // hide the old video before anything else happens
	}
	if (!child)
		return;

	if (!asset) {
		// No motion artwork for this release: stop decoding entirely.
		obs_source_media_stop(child);
		obs_source_output_video(child, nullptr);
		std::lock_guard<std::mutex> lock(mutex_);
		loadedPath_.clear();
	} else if (samePath) {
		// Same video (another track of the same album): keep it running seamlessly.
		std::lock_guard<std::mutex> lock(mutex_);
		if (serial == wantedSerial_)
			loadedIdentity_ = identity;
	} else {
		// Clear the previous frame so a stale cover can never be shown for this track,
		// then open the new file. Opening may block briefly; this is not the graphics thread.
		obs_source_output_video(child, nullptr);
		obs_data_t *settings = obs_data_create();
		// OBS's non-local "input" mode treats HLS as a live broadcast and
		// explicitly DISABLES looping, even for a finite video-on-demand playlist.
		// Genuine Apple motion artwork is a short VOD loop. FFmpeg accepts an
		// HTTPS HLS URL as its "local_file" path; this selects OBS's seekable,
		// looping media-file playback semantics while still fetching over HTTPS.
		obs_data_set_bool(settings, "is_local_file", true);
		obs_data_set_string(settings, "local_file", asset->location.c_str());
		obs_data_set_string(settings, "input", "");
		obs_source_update(child, settings);
		obs_data_release(settings);
		std::lock_guard<std::mutex> lock(mutex_);
		loadedPath_ = asset->location;
		if (serial == wantedSerial_)
			loadedIdentity_ = identity;
	}
	++loadGeneration_;
	obs_source_release(child);
}

void MotionPlayer::tick(const MediaSession *session, bool showing, MotionShape preferred)
{
	if (!enabled_)
		return;
	obs_source_t *child = childRef();
	if (!child)
		return;

	// New track (or new preferred shape): ask the resolver. It cancels older work.
	if (session && session->track.hasMetadata()) {
		const std::string identity = session->track.identity();
		const std::string key = identity + (preferred == MotionShape::Portrait ? "\x1fp" : "\x1fs");
		if (key != requestedKey_) {
			requestedKey_ = key;
			MotionQuery q;
			q.artist = session->track.artist;
			q.albumArtist = session->track.albumArtist;
			q.album = session->track.album;
			q.title = session->track.title;
			q.preferred = preferred;
			std::lock_guard<std::mutex> lock(mutex_);
			wantedSerial_ = ++serial_;
			wantedIdentity_ = identity;
			if (loadedIdentity_ != identity)
				loadedIdentity_.clear();
			if (resolver_)
				resolver_->request(wantedSerial_, q);
		}
	}

	// Pause decoding when the music pauses (if asked) and whenever nobody can see it.
	const bool musicPaused = session && session->status == PlaybackStatus::Paused;
	const bool wantPaused = !showing || (pauseWithMusic_ && musicPaused);
	const uint64_t gen = loadGeneration_.load();
	if (wantPaused != appliedPaused_ || gen != appliedGeneration_) {
		const obs_media_state st = obs_source_media_get_state(child);
		if (st == OBS_MEDIA_STATE_PLAYING || st == OBS_MEDIA_STATE_PAUSED) {
			obs_source_media_play_pause(child, wantPaused);
			appliedPaused_ = wantPaused;
			appliedGeneration_ = gen;
		}
	}
	obs_source_release(child);
}

MotionFrame MotionPlayer::frame() const
{
	MotionFrame f;
	if (!enabled_)
		return f;
	obs_source_t *child = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (!child_ || loadedIdentity_.empty())
			return f;
		f.identity = loadedIdentity_;
		child = obs_source_get_ref(child_);
	}
	if (!child)
		return f;
	const obs_media_state st = obs_source_media_get_state(child);
	f.ready = (st == OBS_MEDIA_STATE_PLAYING || st == OBS_MEDIA_STATE_PAUSED) && obs_source_get_width(child) > 0 &&
		  obs_source_get_height(child) > 0 && haveTexture_;
	obs_source_release(child);
	return f;
}

gs_texture_t *MotionPlayer::prepare(const std::vector<DrawOp> &ops, const Rect &box, double t)
{
	if (!enabled_) {
		haveTexture_ = false;
		return nullptr;
	}
	// Only copy frames while a video is loaded for the shown track. The scene asks for
	// the video op only after `frame().ready`, which needs a first copy, so capture
	// whenever something is loaded, and draw only when the op is present.
	bool loaded;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		loaded = child_ && !loadedIdentity_.empty();
	}
	if (!loaded) {
		haveTexture_ = false;
		return nullptr;
	}
	const bool wanted = std::any_of(ops.begin(), ops.end(), [](const DrawOp &op) { return op.kind == DrawOp::Kind::Video; });
	obs_source_t *child = childRef();
	if (!child)
		return nullptr;
	const uint32_t vw = obs_source_get_width(child), vh = obs_source_get_height(child);
	if (vw == 0 || vh == 0 || box.empty()) {
		obs_source_release(child);
		haveTexture_ = false;
		return nullptr;
	}

	// Copy at the artwork's size (never above the video's), cropped to fill the box.
	const float scale = std::min(1.0f, std::min(static_cast<float>(vw) / box.w, static_cast<float>(vh) / box.h));
	const uint32_t tw = std::max(1u, static_cast<uint32_t>(std::lround(box.w * std::max(scale, 0.25f))));
	const uint32_t th = std::max(1u, static_cast<uint32_t>(std::lround(box.h * std::max(scale, 0.25f))));
	const bool sizeChanged = tw != texW_ || th != texH_;
	const uint64_t gen = loadGeneration_.load();
	if (gen != capturedGeneration_)
		haveTexture_ = false; // a different video was loaded: never show the old frame
	// Before the first frame is shown, capture once so the scene can start fading in;
	// after that, copy only while the video is actually drawn, at most 30 times a second.
	const bool due = !haveTexture_ ||
			 (wanted && (sizeChanged || t - lastCapture_ >= 1.0 / kMaxCaptureFps - 1e-4 || t < lastCapture_));
	if (due) {
		if (!texrender_)
			texrender_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		gs_texrender_reset(texrender_);
		if (texrender_ && gs_texrender_begin(texrender_, tw, th)) {
			struct vec4 clear;
			vec4_zero(&clear);
			gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
			const CropRect c = coverCrop(static_cast<int>(vw), static_cast<int>(vh), box.w, box.h);
			gs_ortho(c.x, c.x + c.w, c.y, c.y + c.h, -100.0f, 100.0f);
			gs_blend_state_push();
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
			obs_source_video_render(child);
			gs_blend_state_pop();
			gs_texrender_end(texrender_);
			texW_ = tw;
			texH_ = th;
			lastCapture_ = t;
			haveTexture_ = true;
			capturedGeneration_ = gen;
		}
	}
	obs_source_release(child);
	return wanted && haveTexture_ ? gs_texrender_get_texture(texrender_) : nullptr;
}

} // namespace tonearm
