// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#define T_(s) obs_module_text(s)

namespace tonearm {

namespace {

// Keys that make up the look of a source. Exported presets contain only these.
const char *kStyleKeys[] = {
	"preset", "layout", "width", "height", "corner_radius", "surface_opacity", "shadow", "blur", "border", "glow",
	"show_artwork", "show_album", "show_progress", "time_display", "show_source_app", "accent_mode", "accent_color",
	"surface_from_artwork", "surface_color", "auto_contrast", "custom_text_colors", "title_color", "artist_color",
	"album_color", "title_family", "body_family", "text_scale", "motion_speed", "motion_intensity", "reduced_motion",
	"marquee", "marquee_speed", "paused_behavior", "hide_delay", "idle_behavior", "idle_text",
};

const char *timeId(TimeDisplay t)
{
	switch (t) {
	case TimeDisplay::Hidden:
		return "hidden";
	case TimeDisplay::Elapsed:
		return "elapsed";
	case TimeDisplay::Remaining:
		return "remaining";
	case TimeDisplay::Both:
		return "both";
	}
	return "both";
}

TimeDisplay timeFrom(const char *s)
{
	if (!s)
		return TimeDisplay::Both;
	if (!strcmp(s, "hidden"))
		return TimeDisplay::Hidden;
	if (!strcmp(s, "elapsed"))
		return TimeDisplay::Elapsed;
	if (!strcmp(s, "remaining"))
		return TimeDisplay::Remaining;
	return TimeDisplay::Both;
}

long long pct(float v)
{
	return static_cast<long long>(std::lround(v * 100.0f));
}

void writeStyle(obs_data_t *d, const Style &s, bool resetSize)
{
	obs_data_set_string(d, "preset", s.presetId.c_str());
	obs_data_set_string(d, "layout", layoutId(s.layout));
	if (resetSize) {
		obs_data_set_int(d, "width", s.width);
		obs_data_set_int(d, "height", s.height);
	}
	obs_data_set_int(d, "corner_radius", 100);
	obs_data_set_int(d, "surface_opacity", pct(s.surfaceOpacity));
	obs_data_set_int(d, "shadow", pct(s.shadowStrength));
	obs_data_set_int(d, "blur", pct(s.backdropBlur));
	obs_data_set_int(d, "border", pct(s.borderOpacity));
	obs_data_set_int(d, "glow", pct(s.glowStrength));
	obs_data_set_bool(d, "show_artwork", s.showArtwork);
	obs_data_set_bool(d, "show_album", s.showAlbum);
	obs_data_set_bool(d, "show_progress", s.showProgress);
	obs_data_set_string(d, "time_display", timeId(s.times));
	obs_data_set_string(d, "accent_mode", s.accentMode == AccentMode::Custom ? "custom" : "artwork");
	obs_data_set_int(d, "accent_color", s.accentColor.toObs());
	obs_data_set_bool(d, "surface_from_artwork", s.artworkBackground);
	obs_data_set_int(d, "surface_color", s.surfaceColor.toObs());
	obs_data_set_bool(d, "custom_text_colors", false);
	obs_data_set_int(d, "title_color", s.titleColor.toObs());
	obs_data_set_int(d, "artist_color", s.artistColor.toObs());
	obs_data_set_int(d, "album_color", s.albumColor.toObs());
	obs_data_set_string(d, "title_family", "");
	obs_data_set_string(d, "body_family", "");
	obs_data_set_int(d, "text_scale", 100);
}

void addList(obs_properties_t *g, const char *key, const char *label, std::initializer_list<std::pair<const char *, const char *>> items)
{
	obs_property_t *p = obs_properties_add_list(g, key, label, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const auto &it : items)
		obs_property_list_add_string(p, T_(it.second), it.first);
}

void addFamilyList(obs_properties_t *g, const char *key, const char *label)
{
	obs_property_t *p = obs_properties_add_list(g, key, label, OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(p, T_("Font.PresetDefault"), "");
	for (const char *f : {"Inter", "Inter Display", "IBM Plex Sans", "IBM Plex Mono", "EB Garamond"})
		obs_property_list_add_string(p, f, f);
}

obs_properties_t *root(obs_properties_t *props)
{
	while (obs_properties_t *parent = obs_properties_get_parent(props))
		props = parent;
	return props;
}

bool onPresetOrLayoutChanged(void *priv, obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	(void)priv;
	props = root(props);
	syncPreset(settings);
	const char *preset = obs_data_get_string(settings, "preset");
	// Preset-specific controls
	const bool glass = !strcmp(preset, "glass") || !strcmp(preset, "editorial");
	obs_property_set_visible(obs_properties_get(props, "blur"), glass);
	obs_property_set_visible(obs_properties_get(props, "glow"), !strcmp(preset, "neon"));
	return true;
}

bool onToggles(void *, obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	props = root(props);
	obs_property_set_visible(obs_properties_get(props, "accent_color"), !strcmp(obs_data_get_string(settings, "accent_mode"), "custom"));
	const bool custom = obs_data_get_bool(settings, "custom_text_colors");
	for (const char *k : {"title_color", "artist_color", "album_color"})
		obs_property_set_visible(obs_properties_get(props, k), custom);
	obs_property_set_visible(obs_properties_get(props, "surface_color"), !obs_data_get_bool(settings, "surface_from_artwork"));
	obs_property_set_visible(obs_properties_get(props, "hide_delay"), !strcmp(obs_data_get_string(settings, "paused_behavior"), "hide"));
	obs_property_set_visible(obs_properties_get(props, "idle_text"), !strcmp(obs_data_get_string(settings, "idle_behavior"), "message"));
	obs_property_set_visible(obs_properties_get(props, "marquee_speed"), obs_data_get_bool(settings, "marquee"));
	const bool list = strcmp(obs_data_get_string(settings, "source_mode"), "auto") != 0;
	obs_property_set_visible(obs_properties_get(props, "source_list"), list);
	return true;
}

bool onResetSize(obs_properties_t *, obs_property_t *, void *priv)
{
	auto *ctx = static_cast<PropertiesContext *>(priv);
	obs_data_t *s = obs_source_get_settings(ctx->source);
	int w = 0, h = 0;
	const Style st = makePreset(obs_data_get_string(s, "preset"), layoutFromId(obs_data_get_string(s, "layout")));
	w = st.width;
	h = st.height;
	obs_data_set_int(s, "width", w);
	obs_data_set_int(s, "height", h);
	obs_source_update(ctx->source, s);
	obs_data_release(s);
	return true;
}

bool onExport(obs_properties_t *, obs_property_t *, void *priv)
{
	auto *ctx = static_cast<PropertiesContext *>(priv);
	obs_data_t *s = obs_source_get_settings(ctx->source);
	const char *path = obs_data_get_string(s, "export_path");
	if (path && *path) {
		if (exportPreset(s, path))
			blog(LOG_INFO, "[tonearm] exported preset to %s", path);
		else
			blog(LOG_WARNING, "[tonearm] could not export preset to %s", path);
	}
	obs_data_release(s);
	return false;
}

bool onImport(obs_properties_t *, obs_property_t *, void *priv)
{
	auto *ctx = static_cast<PropertiesContext *>(priv);
	obs_data_t *s = obs_source_get_settings(ctx->source);
	const char *path = obs_data_get_string(s, "import_path");
	bool changed = false;
	if (path && *path) {
		std::string err;
		if (importPreset(s, path, err)) {
			obs_source_update(ctx->source, s);
			changed = true;
		} else {
			blog(LOG_WARNING, "[tonearm] could not import preset %s: %s", path, err.c_str());
		}
	}
	obs_data_release(s);
	return changed;
}

bool onRefresh(obs_properties_t *, obs_property_t *, void *)
{
	return true; // rebuilds the properties, which re-reads detected players
}

} // namespace

void applyPreset(obs_data_t *settings, const std::string &preset, const std::string &layout, bool resetSize)
{
	const Style s = makePreset(preset, layoutFromId(layout));
	writeStyle(settings, s, resetSize);
	obs_data_set_string(settings, "applied_preset", preset.c_str());
	obs_data_set_string(settings, "applied_layout", layout.c_str());
}

bool syncPreset(obs_data_t *settings)
{
	const std::string preset = obs_data_get_string(settings, "preset");
	const std::string layout = obs_data_get_string(settings, "layout");
	const std::string appliedPreset = obs_data_get_string(settings, "applied_preset");
	const std::string appliedLayout = obs_data_get_string(settings, "applied_layout");
	if (preset == appliedPreset && layout == appliedLayout)
		return false;
	// Changing the layout resets the size; changing only the preset keeps the user's size.
	applyPreset(settings, preset, layout, layout != appliedLayout);
	return true;
}

void settingsDefaults(obs_data_t *d)
{
	const Style s = makePreset("glass", LayoutMode::ExpandedHorizontal);
	obs_data_set_default_string(d, "preset", "glass");
	obs_data_set_default_string(d, "layout", "expanded");
	obs_data_set_default_string(d, "applied_preset", "glass");
	obs_data_set_default_string(d, "applied_layout", "expanded");
	obs_data_set_default_int(d, "width", s.width);
	obs_data_set_default_int(d, "height", s.height);
	obs_data_set_default_int(d, "corner_radius", 100);
	obs_data_set_default_int(d, "surface_opacity", pct(s.surfaceOpacity));
	obs_data_set_default_int(d, "shadow", pct(s.shadowStrength));
	obs_data_set_default_int(d, "blur", pct(s.backdropBlur));
	obs_data_set_default_int(d, "border", pct(s.borderOpacity));
	obs_data_set_default_int(d, "glow", pct(s.glowStrength));
	obs_data_set_default_bool(d, "show_artwork", true);
	obs_data_set_default_bool(d, "show_album", true);
	obs_data_set_default_bool(d, "show_progress", true);
	obs_data_set_default_string(d, "time_display", "both");
	obs_data_set_default_bool(d, "show_source_app", false);
	obs_data_set_default_string(d, "accent_mode", "artwork");
	obs_data_set_default_int(d, "accent_color", s.accentColor.toObs());
	obs_data_set_default_bool(d, "surface_from_artwork", true);
	obs_data_set_default_int(d, "surface_color", s.surfaceColor.toObs());
	obs_data_set_default_bool(d, "auto_contrast", true);
	obs_data_set_default_bool(d, "custom_text_colors", false);
	obs_data_set_default_int(d, "title_color", s.titleColor.toObs());
	obs_data_set_default_int(d, "artist_color", s.artistColor.toObs());
	obs_data_set_default_int(d, "album_color", s.albumColor.toObs());
	obs_data_set_default_string(d, "title_family", "");
	obs_data_set_default_string(d, "body_family", "");
	obs_data_set_default_int(d, "text_scale", 100);
	obs_data_set_default_int(d, "motion_speed", 100);
	obs_data_set_default_int(d, "motion_intensity", 100);
	obs_data_set_default_bool(d, "reduced_motion", false);
	obs_data_set_default_bool(d, "marquee", true);
	obs_data_set_default_int(d, "marquee_speed", static_cast<long long>(s.marqueeSpeed));
	obs_data_set_default_string(d, "paused_behavior", "dim");
	obs_data_set_default_int(d, "hide_delay", 8);
	obs_data_set_default_string(d, "idle_behavior", "hide");
	obs_data_set_default_string(d, "idle_text", T_("Idle.DefaultText"));
	obs_data_set_default_bool(d, "sample_when_idle", false);
	obs_data_set_default_string(d, "source_mode", "auto");
	obs_data_set_default_bool(d, "allow_browsers", false);
}

Style styleFromSettings(obs_data_t *d)
{
	const std::string preset = obs_data_get_string(d, "preset");
	Style s = makePreset(preset, layoutFromId(obs_data_get_string(d, "layout")));
	s.width = static_cast<int>(std::clamp<long long>(obs_data_get_int(d, "width"), 64, 4096));
	s.height = static_cast<int>(std::clamp<long long>(obs_data_get_int(d, "height"), 32, 4096));
	s.cornerRadius *= static_cast<float>(obs_data_get_int(d, "corner_radius")) / 100.0f;
	s.artworkRadius *= static_cast<float>(obs_data_get_int(d, "corner_radius")) / 100.0f;
	s.surfaceOpacity = static_cast<float>(obs_data_get_int(d, "surface_opacity")) / 100.0f;
	s.showSurface = s.surfaceOpacity > 0.001f;
	s.shadowStrength = static_cast<float>(obs_data_get_int(d, "shadow")) / 100.0f;
	s.backdropBlur = static_cast<float>(obs_data_get_int(d, "blur")) / 100.0f;
	s.borderOpacity = static_cast<float>(obs_data_get_int(d, "border")) / 100.0f;
	s.glowStrength = static_cast<float>(obs_data_get_int(d, "glow")) / 100.0f;
	if (s.composition == Composition::Neon)
		s.progressGlow = std::min(1.0f, s.glowStrength * 1.2f);
	s.showArtwork = obs_data_get_bool(d, "show_artwork");
	s.showAlbum = obs_data_get_bool(d, "show_album");
	s.showProgress = obs_data_get_bool(d, "show_progress");
	s.times = timeFrom(obs_data_get_string(d, "time_display"));
	s.showSourceApp = obs_data_get_bool(d, "show_source_app");
	s.accentMode = strcmp(obs_data_get_string(d, "accent_mode"), "custom") == 0 ? AccentMode::Custom : AccentMode::FromArtwork;
	if (s.accentMode == AccentMode::Custom)
		s.accentColor = Color::fromObs(static_cast<uint32_t>(obs_data_get_int(d, "accent_color")));
	s.artworkBackground = obs_data_get_bool(d, "surface_from_artwork");
	s.surfaceColor = Color::fromObs(static_cast<uint32_t>(obs_data_get_int(d, "surface_color")));
	s.autoTextColor = obs_data_get_bool(d, "auto_contrast");
	if (obs_data_get_bool(d, "custom_text_colors")) {
		s.titleColor = Color::fromObs(static_cast<uint32_t>(obs_data_get_int(d, "title_color")));
		s.artistColor = Color::fromObs(static_cast<uint32_t>(obs_data_get_int(d, "artist_color")));
		s.albumColor = Color::fromObs(static_cast<uint32_t>(obs_data_get_int(d, "album_color")));
	}
	const std::string titleFamily = obs_data_get_string(d, "title_family");
	const std::string bodyFamily = obs_data_get_string(d, "body_family");
	if (!titleFamily.empty())
		s.titleFont.family = titleFamily;
	if (!bodyFamily.empty()) {
		s.artistFont.family = bodyFamily;
		s.albumFont.family = bodyFamily;
		s.timeFont.family = bodyFamily;
	}
	const float scale = std::clamp(static_cast<float>(obs_data_get_int(d, "text_scale")) / 100.0f, 0.5f, 2.0f);
	for (FontSpec *f : {&s.titleFont, &s.artistFont, &s.albumFont, &s.timeFont})
		f->sizePx *= scale;

	const float speed = std::clamp(static_cast<float>(obs_data_get_int(d, "motion_speed")) / 100.0f, 0.25f, 4.0f);
	s.motionDuration = 1.0f / speed;
	s.motionIntensity = std::clamp(static_cast<float>(obs_data_get_int(d, "motion_intensity")) / 100.0f, 0.0f, 2.0f);
	s.reducedMotion = obs_data_get_bool(d, "reduced_motion");
	s.marquee = obs_data_get_bool(d, "marquee");
	s.marqueeSpeed = static_cast<float>(std::clamp<long long>(obs_data_get_int(d, "marquee_speed"), 5, 400));

	const char *pb = obs_data_get_string(d, "paused_behavior");
	s.paused = !strcmp(pb, "keep") ? PausedBehavior::Keep : (!strcmp(pb, "hide") ? PausedBehavior::HideAfterDelay : PausedBehavior::Dim);
	s.hideAfterPausedSec = static_cast<float>(std::clamp<long long>(obs_data_get_int(d, "hide_delay"), 0, 3600));
	s.idle = !strcmp(obs_data_get_string(d, "idle_behavior"), "message") ? IdleBehavior::ShowPlaceholder : IdleBehavior::Hide;
	s.placeholderText = obs_data_get_string(d, "idle_text");
	s.sampleWhenIdle = obs_data_get_bool(d, "sample_when_idle");
	return s;
}

SelectionPolicy policyFromSettings(obs_data_t *d)
{
	SelectionPolicy p;
	const char *mode = obs_data_get_string(d, "source_mode");
	p.mode = !strcmp(mode, "prefer") ? SelectionPolicy::Mode::PreferList
		 : !strcmp(mode, "only") ? SelectionPolicy::Mode::OnlyList
					 : SelectionPolicy::Mode::Automatic;
	p.allowBrowsers = obs_data_get_bool(d, "allow_browsers");
	obs_data_array_t *arr = obs_data_get_array(d, "source_list");
	const size_t n = arr ? obs_data_array_count(arr) : 0;
	for (size_t i = 0; i < n; ++i) {
		obs_data_t *item = obs_data_array_item(arr, i);
		std::string v = obs_data_get_string(item, "value");
		obs_data_release(item);
		// Accept "Apple Music", "applemusic", "Spotify.exe": normalize to the keyword form.
		std::string kw;
		for (char ch : v)
			if (!isspace(static_cast<unsigned char>(ch)))
				kw.push_back(static_cast<char>(tolower(static_cast<unsigned char>(ch))));
		if (kw.size() > 4 && kw.compare(kw.size() - 4, 4, ".exe") == 0)
			kw.resize(kw.size() - 4);
		if (!kw.empty())
			p.priority.push_back(appKeyword(kw).empty() ? kw : appKeyword(kw));
	}
	obs_data_array_release(arr);
	return p;
}

bool exportPreset(obs_data_t *settings, const char *path)
{
	obs_data_t *out = obs_data_create();
	obs_data_set_string(out, "format", "tonearm-preset");
	obs_data_set_int(out, "version", 1);
	obs_data_t *values = obs_data_create();
	for (const char *k : kStyleKeys) {
		obs_data_item_t *item = obs_data_item_byname(settings, k);
		if (!item) {
			continue;
		}
		switch (obs_data_item_gettype(item)) {
		case OBS_DATA_STRING:
			obs_data_set_string(values, k, obs_data_get_string(settings, k));
			break;
		case OBS_DATA_NUMBER:
			obs_data_set_int(values, k, obs_data_get_int(settings, k));
			break;
		case OBS_DATA_BOOLEAN:
			obs_data_set_bool(values, k, obs_data_get_bool(settings, k));
			break;
		default:
			break;
		}
		obs_data_item_release(&item);
	}
	obs_data_set_obj(out, "settings", values);
	const bool ok = obs_data_save_json_pretty_safe(out, path, "tmp", "bak");
	obs_data_release(values);
	obs_data_release(out);
	return ok;
}

bool importPreset(obs_data_t *settings, const char *path, std::string &error)
{
	obs_data_t *in = obs_data_create_from_json_file_safe(path, "bak");
	if (!in) {
		error = "the file could not be read as JSON";
		return false;
	}
	if (strcmp(obs_data_get_string(in, "format"), "tonearm-preset") != 0) {
		obs_data_release(in);
		error = "the file is not a Tonearm preset";
		return false;
	}
	obs_data_t *values = obs_data_get_obj(in, "settings");
	if (values) {
		for (const char *k : kStyleKeys) {
			obs_data_item_t *item = obs_data_item_byname(values, k);
			if (!item)
				continue;
			switch (obs_data_item_gettype(item)) {
			case OBS_DATA_STRING:
				obs_data_set_string(settings, k, obs_data_get_string(values, k));
				break;
			case OBS_DATA_NUMBER:
				obs_data_set_int(settings, k, obs_data_get_int(values, k));
				break;
			case OBS_DATA_BOOLEAN:
				obs_data_set_bool(settings, k, obs_data_get_bool(values, k));
				break;
			default:
				break;
			}
			obs_data_item_release(&item);
		}
		// Imported values are the user's own; don't let the preset callback overwrite them.
		obs_data_set_string(settings, "applied_preset", obs_data_get_string(settings, "preset"));
		obs_data_set_string(settings, "applied_layout", obs_data_get_string(settings, "layout"));
		obs_data_release(values);
	}
	obs_data_release(in);
	return true;
}

obs_properties_t *buildProperties(PropertiesContext *ctx)
{
	obs_properties_t *props = obs_properties_create();

	// --- Look ---------------------------------------------------------------
	obs_properties_t *look = obs_properties_create();
	obs_property_t *preset = obs_properties_add_list(look, "preset", T_("Preset"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	for (const PresetInfo &p : presetList())
		obs_property_list_add_string(preset, T_((std::string("Preset.") + p.id).c_str()), p.id);
	obs_property_set_modified_callback2(preset, onPresetOrLayoutChanged, ctx);
	addList(look, "layout", T_("Layout"),
		{{"compact", "Layout.Compact"}, {"expanded", "Layout.Expanded"}, {"square", "Layout.Square"}, {"vertical", "Layout.Vertical"}});
	obs_property_set_modified_callback2(obs_properties_get(look, "layout"), onPresetOrLayoutChanged, ctx);
	obs_properties_add_int(look, "width", T_("Width"), 64, 4096, 1);
	obs_properties_add_int(look, "height", T_("Height"), 32, 4096, 1);
	obs_properties_add_button2(look, "reset_size", T_("ResetSize"), onResetSize, ctx);
	obs_properties_add_bool(look, "show_artwork", T_("ShowArtwork"));
	obs_properties_add_bool(look, "show_album", T_("ShowAlbum"));
	obs_properties_add_bool(look, "show_progress", T_("ShowProgress"));
	addList(look, "time_display", T_("TimeDisplay"),
		{{"both", "Time.Both"}, {"elapsed", "Time.Elapsed"}, {"remaining", "Time.Remaining"}, {"hidden", "Time.Hidden"}});
	obs_properties_add_bool(look, "show_source_app", T_("ShowSourceApp"));
	obs_properties_add_group(props, "group_look", T_("Group.Look"), OBS_GROUP_NORMAL, look);

	// --- Surface ------------------------------------------------------------
	obs_properties_t *surface = obs_properties_create();
	obs_property_t *p = obs_properties_add_int_slider(surface, "corner_radius", T_("CornerRadius"), 0, 200, 5);
	obs_property_int_set_suffix(p, "%");
	p = obs_properties_add_int_slider(surface, "surface_opacity", T_("SurfaceOpacity"), 0, 100, 1);
	obs_property_int_set_suffix(p, "%");
	p = obs_properties_add_int_slider(surface, "shadow", T_("Shadow"), 0, 100, 1);
	obs_property_int_set_suffix(p, "%");
	p = obs_properties_add_int_slider(surface, "blur", T_("Blur"), 0, 100, 1);
	obs_property_int_set_suffix(p, "%");
	p = obs_properties_add_int_slider(surface, "border", T_("Border"), 0, 100, 1);
	obs_property_int_set_suffix(p, "%");
	p = obs_properties_add_int_slider(surface, "glow", T_("Glow"), 0, 100, 1);
	obs_property_int_set_suffix(p, "%");
	obs_properties_add_group(props, "group_surface", T_("Group.Surface"), OBS_GROUP_NORMAL, surface);

	// --- Color --------------------------------------------------------------
	obs_properties_t *color = obs_properties_create();
	addList(color, "accent_mode", T_("Accent"), {{"artwork", "Accent.Artwork"}, {"custom", "Accent.Custom"}});
	obs_property_set_modified_callback2(obs_properties_get(color, "accent_mode"), onToggles, ctx);
	obs_properties_add_color(color, "accent_color", T_("AccentColor"));
	p = obs_properties_add_bool(color, "surface_from_artwork", T_("SurfaceFromArtwork"));
	obs_property_set_modified_callback2(p, onToggles, ctx);
	obs_properties_add_color(color, "surface_color", T_("SurfaceColor"));
	p = obs_properties_add_bool(color, "auto_contrast", T_("AutoContrast"));
	obs_property_set_long_description(p, T_("AutoContrast.Tip"));
	p = obs_properties_add_bool(color, "custom_text_colors", T_("CustomTextColors"));
	obs_property_set_modified_callback2(p, onToggles, ctx);
	obs_properties_add_color_alpha(color, "title_color", T_("TitleColor"));
	obs_properties_add_color_alpha(color, "artist_color", T_("ArtistColor"));
	obs_properties_add_color_alpha(color, "album_color", T_("AlbumColor"));
	obs_properties_add_group(props, "group_color", T_("Group.Color"), OBS_GROUP_NORMAL, color);

	// --- Type ---------------------------------------------------------------
	obs_properties_t *type = obs_properties_create();
	addFamilyList(type, "title_family", T_("TitleFont"));
	addFamilyList(type, "body_family", T_("BodyFont"));
	p = obs_properties_add_int_slider(type, "text_scale", T_("TextScale"), 50, 200, 5);
	obs_property_int_set_suffix(p, "%");
	obs_properties_add_group(props, "group_type", T_("Group.Type"), OBS_GROUP_NORMAL, type);

	// --- Motion -------------------------------------------------------------
	obs_properties_t *motion = obs_properties_create();
	p = obs_properties_add_int_slider(motion, "motion_speed", T_("MotionSpeed"), 25, 300, 5);
	obs_property_int_set_suffix(p, "%");
	p = obs_properties_add_int_slider(motion, "motion_intensity", T_("MotionIntensity"), 0, 200, 5);
	obs_property_int_set_suffix(p, "%");
	p = obs_properties_add_bool(motion, "reduced_motion", T_("ReducedMotion"));
	obs_property_set_long_description(p, T_("ReducedMotion.Tip"));
	p = obs_properties_add_bool(motion, "marquee", T_("Marquee"));
	obs_property_set_modified_callback2(p, onToggles, ctx);
	p = obs_properties_add_int_slider(motion, "marquee_speed", T_("MarqueeSpeed"), 10, 160, 2);
	obs_property_int_set_suffix(p, " px/s");
	obs_properties_add_group(props, "group_motion", T_("Group.Motion"), OBS_GROUP_NORMAL, motion);

	// --- Behavior -----------------------------------------------------------
	obs_properties_t *behavior = obs_properties_create();
	addList(behavior, "paused_behavior", T_("WhenPaused"), {{"dim", "Paused.Dim"}, {"keep", "Paused.Keep"}, {"hide", "Paused.Hide"}});
	obs_property_set_modified_callback2(obs_properties_get(behavior, "paused_behavior"), onToggles, ctx);
	p = obs_properties_add_int(behavior, "hide_delay", T_("HideDelay"), 0, 600, 1);
	obs_property_int_set_suffix(p, " s");
	addList(behavior, "idle_behavior", T_("WhenIdle"), {{"hide", "Idle.Hide"}, {"message", "Idle.Message"}});
	obs_property_set_modified_callback2(obs_properties_get(behavior, "idle_behavior"), onToggles, ctx);
	obs_properties_add_text(behavior, "idle_text", T_("IdleText"), OBS_TEXT_DEFAULT);
	p = obs_properties_add_bool(behavior, "sample_when_idle", T_("SampleWhenIdle"));
	obs_property_set_long_description(p, T_("SampleWhenIdle.Tip"));
	obs_properties_add_group(props, "group_behavior", T_("Group.Behavior"), OBS_GROUP_NORMAL, behavior);

	// --- Sources ------------------------------------------------------------
	obs_properties_t *sources = obs_properties_create();
	addList(sources, "source_mode", T_("SourceMode"), {{"auto", "SourceMode.Auto"}, {"prefer", "SourceMode.Prefer"}, {"only", "SourceMode.Only"}});
	obs_property_set_modified_callback2(obs_properties_get(sources, "source_mode"), onToggles, ctx);
	obs_properties_add_editable_list(sources, "source_list", T_("SourceList"), OBS_EDITABLE_LIST_TYPE_STRINGS, nullptr, nullptr);
	p = obs_properties_add_bool(sources, "allow_browsers", T_("AllowBrowsers"));
	obs_property_set_long_description(p, T_("AllowBrowsers.Tip"));
	p = obs_properties_add_text(sources, "detected", ctx->detectedSummary.c_str(), OBS_TEXT_INFO);
	obs_properties_add_button2(sources, "refresh", T_("Refresh"), onRefresh, ctx);
	obs_properties_add_group(props, "group_sources", T_("Group.Sources"), OBS_GROUP_NORMAL, sources);

	// --- Presets ------------------------------------------------------------
	obs_properties_t *files = obs_properties_create();
	obs_properties_add_path(files, "export_path", T_("ExportPath"), OBS_PATH_FILE_SAVE, "Tonearm preset (*.json)", nullptr);
	obs_properties_add_button2(files, "export", T_("Export"), onExport, ctx);
	obs_properties_add_path(files, "import_path", T_("ImportPath"), OBS_PATH_FILE, "Tonearm preset (*.json)", nullptr);
	obs_properties_add_button2(files, "import", T_("Import"), onImport, ctx);
	obs_properties_add_group(props, "group_files", T_("Group.Files"), OBS_GROUP_NORMAL, files);

	return props;
}

} // namespace tonearm
