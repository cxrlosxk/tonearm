// SPDX-License-Identifier: GPL-2.0-or-later
#include "check.hpp"

#include <cstring>

int main(int argc, char **argv)
{
	const char *filter = argc > 1 ? argv[1] : nullptr;
	int ran = 0, failedCases = 0;
	for (const auto &c : check::registry()) {
		if (filter && !std::strstr(c.name, filter))
			continue;
		const int before = check::failures();
		c.fn();
		++ran;
		if (check::failures() != before) {
			++failedCases;
			std::fprintf(stderr, "FAIL %s\n", c.name);
		}
	}
	std::printf("%d of %d tests passed\n", ran - failedCases, ran);
	std::fflush(stdout);
	return failedCases == 0 ? 0 : 1;
}
