// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/image.hpp"

#include <string>

namespace tonearm {
bool writePng(const Image &img, const std::string &path);
bool readPng(const std::string &path, Image &out);
} // namespace tonearm
