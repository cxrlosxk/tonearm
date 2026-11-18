// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "image.hpp"
#include "scene.hpp"

#include <vector>

namespace tonearm {

// Reference implementation of the draw list on the CPU. The OBS GPU compositor
// mirrors these semantics in data/effects/tonearm.effect. Used for tests and the
// documentation renders.
void executeOnCpu(Image &target, const std::vector<DrawOp> &ops);

} // namespace tonearm
