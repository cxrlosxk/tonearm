// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media.hpp"

#include <vector>

namespace tonearm {

// Fictional sample tracks with procedurally generated cover art. Used by the
// "preview with a sample track" option, tests and documentation renders.
// None of these are real releases, and the artwork is generated, not sampled.
int demoTrackCount();
TrackInfo demoTrack(int index, int artSize = 512, bool withArtwork = true);
double demoDuration(int index);
Image demoArtwork(int index, int size);

} // namespace tonearm
