// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Opt-in remote motion-artwork provider for Windows.
// Queries the published public API at artwork.m8tec.top for genuine Apple Music
// HLS video artwork; no tokens, user accounts, or Apple credentials are stored.

#include "media/motion_resolver.hpp"

#ifdef _WIN32
namespace tonearm {
class RemoteArtworkApiSource final : public MotionSource {
public:
	std::optional<MotionAsset> resolve(const MotionQuery &q, const std::atomic<bool> &cancelled) override;
	const char *name() const override { return "online animated artwork"; }
};
} // namespace tonearm
#endif
