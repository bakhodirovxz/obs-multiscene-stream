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

#pragma once

#include <string>
#include <vector>

#ifndef MSS_MAX_OUTPUTS
#define MSS_MAX_OUTPUTS 4
#endif

inline constexpr int MAX_OUTPUTS = MSS_MAX_OUTPUTS;

struct OutputConfig {
	std::string name;
	std::string scene_name;
	std::string server;        // rtmp(s)://...
	std::string key_protected; // DPAPI blob (base64) on Windows, obfuscated
				   // elsewhere; never the raw key (SEC-1/SEC-2)
	std::string encoder_id = "obs_x264";
	int video_bitrate = 6000; // kbps
	int audio_bitrate = 160;  // kbps
	int audio_track = 1;      // 1-6
	int out_width = 0;        // 0 = inherit canvas
	int out_height = 0;
	bool enabled = true;
};

namespace config {

std::string file_path();

bool load(std::vector<OutputConfig> &outputs);
bool save(const std::vector<OutputConfig> &outputs);

} // namespace config
