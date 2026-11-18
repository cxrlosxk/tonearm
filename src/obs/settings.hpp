// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/media.hpp"
#include "core/style.hpp"

#include <obs-module.h>

#include <string>

namespace tonearm {

void settingsDefaults(obs_data_t *settings);
Style styleFromSettings(obs_data_t *settings);
SelectionPolicy policyFromSettings(obs_data_t *settings);

// Writes every visual value of a preset into the settings (used when the user picks a preset).
void applyPreset(obs_data_t *settings, const std::string &preset, const std::string &layout, bool resetSize);

// Preset files: a JSON document with the visual settings only.
bool exportPreset(obs_data_t *settings, const char *path);
bool importPreset(obs_data_t *settings, const char *path, std::string &error);

struct PropertiesContext {
	obs_source_t *source = nullptr;
	std::string detectedSummary; // human readable list of players seen right now
};

obs_properties_t *buildProperties(PropertiesContext *ctx);

} // namespace tonearm
