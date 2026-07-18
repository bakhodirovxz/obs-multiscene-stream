/*
obs-multiscene-stream
Copyright (C) 2026 obs-multiscene-stream contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "config.hpp"

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

#include <algorithm>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace config {

std::string file_path()
{
	char *path = obs_module_config_path("config.json");
	if (!path)
		return {};
	std::string result = path;
	bfree(path);
	return result;
}

static void ensure_config_dir()
{
	char *dir = obs_module_config_path("");
	if (!dir)
		return;
	os_mkdirs(dir);
	bfree(dir);
}

static OutputConfig output_from_data(obs_data_t *item)
{
	OutputConfig cfg;
	cfg.name = obs_data_get_string(item, "name");
	cfg.scene_name = obs_data_get_string(item, "scene_name");
	cfg.server = obs_data_get_string(item, "server");
	cfg.key_protected = obs_data_get_string(item, "key_protected");
	const char *enc = obs_data_get_string(item, "encoder_id");
	if (enc && *enc)
		cfg.encoder_id = enc;
	cfg.video_bitrate = (int)obs_data_get_int(item, "video_bitrate");
	cfg.audio_bitrate = (int)obs_data_get_int(item, "audio_bitrate");
	cfg.audio_track = (int)obs_data_get_int(item, "audio_track");
	cfg.out_width = (int)obs_data_get_int(item, "out_width");
	cfg.out_height = (int)obs_data_get_int(item, "out_height");
	cfg.enabled = obs_data_get_bool(item, "enabled");

	cfg.video_bitrate = std::clamp(cfg.video_bitrate, 500, 20000);
	cfg.audio_bitrate = std::clamp(cfg.audio_bitrate, 96, 320);
	cfg.audio_track = std::clamp(cfg.audio_track, 1, 6);
	if (cfg.out_width < 0 || cfg.out_height < 0) {
		cfg.out_width = 0;
		cfg.out_height = 0;
	}
	return cfg;
}

static obs_data_t *output_to_data(const OutputConfig &cfg)
{
	obs_data_t *item = obs_data_create();
	obs_data_set_string(item, "name", cfg.name.c_str());
	obs_data_set_string(item, "scene_name", cfg.scene_name.c_str());
	obs_data_set_string(item, "server", cfg.server.c_str());
	obs_data_set_string(item, "key_protected", cfg.key_protected.c_str());
	obs_data_set_string(item, "encoder_id", cfg.encoder_id.c_str());
	obs_data_set_int(item, "video_bitrate", cfg.video_bitrate);
	obs_data_set_int(item, "audio_bitrate", cfg.audio_bitrate);
	obs_data_set_int(item, "audio_track", cfg.audio_track);
	obs_data_set_int(item, "out_width", cfg.out_width);
	obs_data_set_int(item, "out_height", cfg.out_height);
	obs_data_set_bool(item, "enabled", cfg.enabled);
	return item;
}

bool load(std::vector<OutputConfig> &outputs)
{
	outputs.clear();

	std::string path = file_path();
	if (path.empty())
		return false;

	obs_data_t *root = obs_data_create_from_json_file_safe(path.c_str(), "bak");
	if (!root)
		return false; // first run: no config yet

	obs_data_array_t *arr = obs_data_get_array(root, "outputs");
	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count && (int)i < MAX_OUTPUTS; i++) {
			obs_data_t *item = obs_data_array_item(arr, i);
			if (!item)
				continue;

			OutputConfig cfg = output_from_data(item);
			// defaults for fields absent in older configs
			if (cfg.video_bitrate == 500 && !obs_data_has_user_value(item, "video_bitrate"))
				cfg.video_bitrate = 6000;
			if (!obs_data_has_user_value(item, "audio_bitrate"))
				cfg.audio_bitrate = 160;
			if (!obs_data_has_user_value(item, "audio_track"))
				cfg.audio_track = 1;
			if (!obs_data_has_user_value(item, "enabled"))
				cfg.enabled = true;
			outputs.push_back(cfg);
			obs_data_release(item);
		}
		obs_data_array_release(arr);
	}

	obs_data_release(root);
	obs_log(LOG_INFO, "loaded %d output(s) from config", (int)outputs.size());
	return true;
}

bool save(const std::vector<OutputConfig> &outputs)
{
	std::string path = file_path();
	if (path.empty())
		return false;

	ensure_config_dir();

	obs_data_t *root = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();

	for (const OutputConfig &cfg : outputs) {
		obs_data_t *item = output_to_data(cfg);
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}

	obs_data_set_array(root, "outputs", arr);
	obs_data_array_release(arr);

	// atomic write: .tmp then rename (OPT-5)
	bool ok = obs_data_save_json_safe(root, path.c_str(), "tmp", "bak");
	obs_data_release(root);

	if (!ok) {
		obs_log(LOG_WARNING, "failed to save config to %s", path.c_str());
		return false;
	}

#ifndef _WIN32
	chmod(path.c_str(), 0600); // SEC-7
#endif
	return true;
}

} // namespace config
