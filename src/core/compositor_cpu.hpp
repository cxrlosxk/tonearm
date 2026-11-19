// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "image.hpp"
#include "scene.hpp"

#include <vector>

namespace tonearm {

// Reference implementation of the draw list on the CPU. The OBS GPU compositor
// mirrors these semantics in data/effects/tonearm.effect. Used for tests and the
// documentation renders.
// `video` is the current animated artwork frame for Video ops (premultiplied BGRA); when
// null, Video ops are skipped, as the GPU compositor does without a decoded frame.
void executeOnCpu(Image &target, const std::vector<DrawOp> &ops, const Image *video = nullptr);

// Coverage (0..1) of a Video op at a target pixel center. Mirrors PSVideo in tonearm.effect.
float videoCoverage(const DrawOp &op, float px, float py);

} // namespace tonearm
