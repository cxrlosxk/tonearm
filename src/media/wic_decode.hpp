// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/image.hpp"

#include <cstdint>
#include <vector>

namespace tonearm {

// Decodes PNG/JPEG/BMP/etc. with Windows Imaging Component into a premultiplied
// BGRA image no larger than maxSide on either axis. COM must be initialized on
// the calling thread. Returns an empty image on failure.
Image decodeWithWic(const std::vector<uint8_t> &bytes, int maxSide);

} // namespace tonearm
