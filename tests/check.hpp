// SPDX-License-Identifier: GPL-2.0-or-later
// A deliberately tiny test harness: no dependencies, readable failures.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace check {

struct Case {
	const char *name;
	std::function<void()> fn;
};

inline std::vector<Case> &registry()
{
	static std::vector<Case> cases;
	return cases;
}

inline int &failures()
{
	static int n = 0;
	return n;
}

struct Register {
	Register(const char *name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void fail(const char *file, int line, const std::string &what)
{
	++failures();
	std::fprintf(stderr, "  %s:%d: %s\n", file, line, what.c_str());
}

} // namespace check

#define TA_CONCAT2(a, b) a##b
#define TA_CONCAT(a, b) TA_CONCAT2(a, b)
#define TEST(name)                                                                       \
	static void TA_CONCAT(test_, __LINE__)();                                        \
	static check::Register TA_CONCAT(reg_, __LINE__)(name, TA_CONCAT(test_, __LINE__)); \
	static void TA_CONCAT(test_, __LINE__)()

#define CHECK(cond)                                              \
	do {                                                     \
		if (!(cond))                                     \
			check::fail(__FILE__, __LINE__, #cond); \
	} while (0)

#define CHECK_MSG(cond, msg)                                                          \
	do {                                                                          \
		if (!(cond))                                                          \
			check::fail(__FILE__, __LINE__, std::string(#cond) + " — " + (msg)); \
	} while (0)

#define CHECK_NEAR(a, b, eps)                                                                              \
	do {                                                                                               \
		const double va_ = (a), vb_ = (b);                                                         \
		if (!(std::fabs(va_ - vb_) <= (eps)))                                                      \
			check::fail(__FILE__, __LINE__,                                                    \
				    std::string(#a " ≈ " #b ": ") + std::to_string(va_) + " vs " + std::to_string(vb_)); \
	} while (0)
