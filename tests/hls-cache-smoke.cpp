// SPDX-License-Identifier: GPL-2.0-or-later
// Windows CI smoke test: exercise the same finite-HLS cache used by Tonearm.
#include "obs/hls-cache.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
	if (argc != 2)
		return 2;
	std::atomic<bool> cancelled{false};
	const auto local = tonearm::cacheMotionHls(argv[1], cancelled);
	if (!local) {
		std::cerr << "HLS caching failed\n";
		return 1;
	}
	const std::filesystem::path manifest = std::filesystem::u8path(*local);
	std::ifstream stream(manifest);
	if (!stream) {
		std::cerr << "Missing local manifest\n";
		return 1;
	}
	size_t count = 0;
	std::string line;
	bool ended = false;
	while (std::getline(stream, line)) {
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line == "#EXT-X-ENDLIST")
			ended = true;
		if (!line.empty() && line.front() != '#') {
			++count;
			if (!std::filesystem::exists(manifest.parent_path() / std::filesystem::u8path(line))) {
				std::cerr << "Missing local HLS segment\n";
				return 1;
			}
		}
	}
	if (!ended || count < 1) {
		std::cerr << "Incomplete HLS VOD\n";
		return 1;
	}
	std::cout << "Cached complete local HLS video: segments=" << count << "\n";
	return 0;
}
