// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/text.hpp"

#include <filesystem>
#include <memory>

namespace testsupport {

// One text engine for the whole run, with the bundled fonts loaded.
inline tonearm::TextEngine &text()
{
	static std::unique_ptr<tonearm::TextEngine> engine = [] {
		auto e = tonearm::createTextEngine();
		const std::filesystem::path dir = std::filesystem::path(TONEARM_DATA_DIR) / "fonts";
		std::error_code ec;
		for (const auto &f : std::filesystem::directory_iterator(dir, ec)) {
			const auto ext = f.path().extension().string();
			if (ext == ".otf" || ext == ".ttf")
				e->addFontFile(f.path().string());
		}
		return e;
	}();
	return *engine;
}

} // namespace testsupport
