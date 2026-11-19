// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "color.hpp"
#include "text.hpp"

#include <string>
#include <vector>

namespace tonearm {

// ---------------------------------------------------------------------------
// Design tokens. Every preset is composed from these scales so spacing, type,
// radii and timing stay consistent. Values are in logical pixels at a 1x scale
// and milliseconds; the layout engine scales them to the source size.
// ---------------------------------------------------------------------------
namespace tokens {
// 4 px spacing grid.
constexpr float space1 = 4, space2 = 8, space3 = 12, space4 = 16, space5 = 20, space6 = 24, space8 = 32, space10 = 40;

// Type scale (minor third, rounded to whole pixels).
constexpr float typeCaption = 11, typeSmall = 13, typeBody = 15, typeTitle = 18, typeHeadline = 22, typeDisplay = 30,
		typeHero = 40;

// Corner radii.
constexpr float radiusXs = 4, radiusSm = 8, radiusMd = 12, radiusLg = 18, radiusXl = 24;

// Motion durations in milliseconds.
constexpr float durQuick = 160, durStandard = 280, durEmphasis = 460, durTrack = 640, durAppear = 520;

// Stagger between elements of one transition.
constexpr float stagger = 50;
} // namespace tokens

enum class Composition { Glass, Minimal, Neon, Editorial };
enum class LayoutMode { CompactHorizontal, ExpandedHorizontal, Square, Vertical };
enum class TimeDisplay { Hidden, Elapsed, Remaining, Both };
enum class PausedBehavior { Keep, Dim, HideAfterDelay };
enum class IdleBehavior { Hide, ShowPlaceholder };
enum class AccentMode { FromArtwork, Custom };
enum class ArtworkMode { Static, AnimatedWhenAvailable };
enum class MotionOrientation { Automatic, Square, Portrait };
enum class MotionQuality { Balanced, Maximum, Smart };
enum class MotionPause { WithMusic, KeepLooping };

struct Style {
	std::string presetId = "glass";
	Composition composition = Composition::Glass;
	LayoutMode layout = LayoutMode::ExpandedHorizontal;

	// Surface
	float cornerRadius = tokens::radiusLg;   // logical px at 1x
	float surfaceOpacity = 1.0f;
	float backdropBlur = 1.0f;                // 0..1, glass only
	float shadowStrength = 0.55f;             // 0..1
	float borderOpacity = 0.14f;              // hairline highlight
	float glowStrength = 0.0f;                // neon
	float noise = 0.012f;                     // anti-banding grain
	bool showSurface = true;
	bool artworkBackground = true;            // derive surface colors from artwork
	Color surfaceColor = Color::hex("#14110f");

	// Artwork
	bool showArtwork = true;
	float artworkRadius = tokens::radiusMd;
	float artworkShadow = 0.6f;

	// Typography
	FontSpec titleFont{"Inter Display", 600, false, tokens::typeHeadline, -0.01f, false, false};
	FontSpec artistFont{"Inter", 500, false, tokens::typeBody, 0.0f, false, false};
	FontSpec albumFont{"Inter", 400, false, tokens::typeSmall, 0.0f, false, false};
	FontSpec timeFont{"Inter", 500, false, tokens::typeCaption, 0.02f, true, false};
	Color titleColor = Color::hex("#fbf8f4");
	Color artistColor = Color::hex("#fbf8f4cc");
	Color albumColor = Color::hex("#fbf8f48c");
	bool autoTextColor = true;                // keep text readable against artwork-derived surfaces

	// Content
	bool showAlbum = true;
	bool showProgress = true;
	TimeDisplay times = TimeDisplay::Both;
	bool showSourceApp = false;

	// Accent
	AccentMode accentMode = AccentMode::FromArtwork;
	Color accentColor = Color::hex("#f2b36b");

	// Progress
	float progressHeight = 4;
	float progressGlow = 0.0f;

	// Motion
	float motionDuration = 1.0f;              // multiplier
	float motionIntensity = 1.0f;             // scales travel distances, 0 = fades only
	bool reducedMotion = false;
	bool marquee = true;
	float marqueeSpeed = 36;                  // logical px per second
	float marqueePause = 2.4f;                // seconds held at the start of each loop

	// States
	PausedBehavior paused = PausedBehavior::Dim;
	float hideAfterPausedSec = 8;
	IdleBehavior idle = IdleBehavior::Hide;
	std::string placeholderText = "Nothing playing";
	bool sampleWhenIdle = false;              // show the sample track while configuring

	// Animated artwork (experimental, off by default). Not part of a preset's look.
	ArtworkMode artworkMode = ArtworkMode::Static;
	MotionOrientation motionOrientation = MotionOrientation::Automatic;
	MotionQuality motionQuality = MotionQuality::Smart;
	MotionPause motionPause = MotionPause::WithMusic;
	std::string motionFolder;                 // local motion artwork library

	// Source canvas size in OBS pixels.
	int width = 560;
	int height = 168;
};

struct PresetInfo {
	const char *id;
	const char *name;
};

const std::vector<PresetInfo> &presetList();

// Returns the preset with the given id ("glass", "minimal", "neon", "editorial").
// Unknown ids return Glass.
Style makePreset(const std::string &id, LayoutMode layout);

// Recommended canvas size for a layout in a given composition.
void defaultSize(Composition c, LayoutMode layout, int &w, int &h);

const char *layoutId(LayoutMode m);
LayoutMode layoutFromId(const std::string &id);
const char *compositionId(Composition c);
Composition compositionFromId(const std::string &id);

} // namespace tonearm
