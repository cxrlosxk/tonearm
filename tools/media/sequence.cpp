// SPDX-License-Identifier: GPL-2.0-or-later
// tonearm-sequence: renders a scripted sequence of frames with Tonearm's real
// Scene, rasterizer and CPU reference compositor, for documentation media.
//
// It is the same harness as tools/tonearm-render, extended with a timeline so a
// single continuous run can hold several track changes, pauses and preset or
// layout switches. Nothing here is compiled into the plugin.
//
// Script (one command per line, '#' starts a comment):
//   preset <id>            layout <id>          scale <f>       margin <px>
//   fps <n>                size <w>x<h>         reduced-motion
//   at <t> track <n> <pos> [paused] [noart]     set the media at time t
//   at <t> style <preset> <layout>              switch preset/layout at time t
//   record <t0> <t1> <outdir>                   write frames for t0 <= t < t1
#include "core/compositor_cpu.hpp"
#include "core/demo.hpp"
#include "core/scene.hpp"
#include "media/provider.hpp"
#include "../tonearm-render/png.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace tonearm;

namespace {

struct Event {
	double t = 0;
	bool isStyle = false;
	int track = 0;
	double pos = 0;
	PlaybackStatus status = PlaybackStatus::Playing;
	bool art = true;
	std::string preset, layout;
};

struct Script {
	std::string preset = "glass", layout = "expanded";
	float scale = 1.0f;
	int margin = 40, fps = 30, width = 0, height = 0;
	bool reduced = false;
	std::vector<Event> events;
	double t0 = 0, t1 = 0;
	std::string outdir = "frames";
};

Script parse(const std::string &path)
{
	Script s;
	std::ifstream in(path);
	std::string line;
	while (std::getline(in, line)) {
		if (auto h = line.find('#'); h != std::string::npos)
			line.resize(h);
		std::istringstream ls(line);
		std::string k;
		if (!(ls >> k))
			continue;
		if (k == "preset")
			ls >> s.preset;
		else if (k == "layout")
			ls >> s.layout;
		else if (k == "scale")
			ls >> s.scale;
		else if (k == "margin")
			ls >> s.margin;
		else if (k == "fps")
			ls >> s.fps;
		else if (k == "reduced-motion")
			s.reduced = true;
		else if (k == "size") {
			std::string v;
			ls >> v;
			std::sscanf(v.c_str(), "%dx%d", &s.width, &s.height);
		} else if (k == "record")
			ls >> s.t0 >> s.t1 >> s.outdir;
		else if (k == "at") {
			Event e;
			std::string what;
			ls >> e.t >> what;
			if (what == "style") {
				e.isStyle = true;
				ls >> e.preset >> e.layout;
			} else {
				ls >> e.track >> e.pos;
				std::string flag;
				while (ls >> flag) {
					if (flag == "paused")
						e.status = PlaybackStatus::Paused;
					else if (flag == "noart")
						e.art = false;
				}
			}
			s.events.push_back(e);
		}
	}
	std::stable_sort(s.events.begin(), s.events.end(), [](const Event &a, const Event &b) { return a.t < b.t; });
	return s;
}

void loadFonts(TextEngine &te)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::path(TONEARM_DATA_DIR) / "fonts";
	for (const auto &e : fs::directory_iterator(dir)) {
		const auto ext = e.path().extension().string();
		if (ext == ".otf" || ext == ".ttf")
			te.addFontFile(e.path().string());
	}
}

Style makeStyle(const Script &s, const std::string &preset, const std::string &layout)
{
	Style style = makePreset(preset, layoutFromId(layout));
	if (s.width > 0 && s.height > 0) {
		style.width = s.width;
		style.height = s.height;
	}
	style.width = static_cast<int>(std::lround(style.width * s.scale));
	style.height = static_cast<int>(std::lround(style.height * s.scale));
	style.reducedMotion = s.reduced;
	return style;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: tonearm-sequence script.txt\n");
		return 2;
	}
	const Script s = parse(argv[1]);
	Style style = makeStyle(s, s.preset, s.layout);
	Scene scene(style);
	DemoProvider provider(0);
	auto text = createTextEngine();
	loadFonts(*text);
	const auto base = Clock::now();
	auto at = [&](double t) { return base + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(t)); };

	// The output canvas keeps the size of the largest style so frames line up.
	const int margin = static_cast<int>(std::lround(s.margin * s.scale));
	int canvasW = style.width, canvasH = style.height;
	for (const auto &e : s.events)
		if (e.isStyle) {
			const Style st = makeStyle(s, e.preset, e.layout);
			canvasW = std::max(canvasW, st.width);
			canvasH = std::max(canvasH, st.height);
		}

	std::filesystem::create_directories(s.outdir);
	const double dt = 1.0 / std::max(1, s.fps);
	size_t next = 0;
	int frame = 0;
	for (int i = 0;; ++i) {
		const double t = i * dt;
		if (t >= s.t1 - 1e-9)
			break;
		while (next < s.events.size() && s.events[next].t <= t + 1e-9) {
			const Event &e = s.events[next++];
			if (e.isStyle) {
				style = makeStyle(s, e.preset, e.layout);
				scene.setStyle(style);
			} else {
				provider.setNow(at(e.t));
				provider.set(e.track, e.status, e.pos, e.art);
			}
		}
		auto snap = provider.snapshot();
		SceneInput in;
		in.provider = snap->state;
		in.session = snap->sessions.empty() ? nullptr : &snap->sessions[0];
		in.now = at(t);
		scene.update(t, in);
		if (auto req = scene.layerRequest()) {
			scene.provideLayers(req->key, rasterizeLayers(req->style, req->content, *text));
			scene.update(t, in);
		}
		if (auto req = scene.timeRequest())
			scene.provideTimes(req->key, rasterizeTimes(req->style, *req->set, req->elapsed, req->remaining, *text));
		if (t + 1e-9 < s.t0)
			continue;
		std::vector<DrawOp> ops;
		scene.buildFrame(t, ops);
		Image img(canvasW + margin * 2, canvasH + margin * 2);
		const float ox = static_cast<float>(margin + (canvasW - style.width) / 2);
		const float oy = static_cast<float>(margin + (canvasH - style.height) / 2);
		for (auto &op : ops) {
			op.dst = op.dst.translated(ox, oy);
			op.rect = op.rect.translated(ox, oy);
			op.clipRect = op.clipRect.translated(ox, oy);
		}
		executeOnCpu(img, ops);
		char name[64];
		std::snprintf(name, sizeof name, "/f%05d.png", frame++);
		writePng(img, s.outdir + name);
	}
	std::printf("%d frames\n", frame);
	return 0;
}
