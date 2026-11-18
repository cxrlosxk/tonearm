// SPDX-License-Identifier: GPL-2.0-or-later
#include "media-service.hpp"
#include "tonearm-source.hpp"

#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("tonearm", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Tonearm: a now playing overlay for music from Apple Music, Spotify and other Windows media players.";
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return "Tonearm";
}

bool obs_module_load(void)
{
	tonearm::registerTonearmSource();
	blog(LOG_INFO, "[tonearm] loaded version %s", TONEARM_VERSION);
	return true;
}

void obs_module_unload(void)
{
	tonearm::MediaService::instance().shutdown();
	blog(LOG_INFO, "[tonearm] unloaded");
}
