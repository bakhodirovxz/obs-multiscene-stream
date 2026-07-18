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

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "output-manager.hpp"
#include "ui/dock-widget.hpp"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static OutputManager *manager = nullptr;

static void frontend_event_callback(enum obs_frontend_event event, void *)
{
	if (manager)
		manager->onFrontendEvent(event);
}

bool obs_module_load(void)
{
	manager = new OutputManager();

	// The dock is registered at module load so the saved dock layout is
	// restored (T-3); the config itself loads on FINISHED_LOADING because
	// scenes are not available yet here (FR-6.4).
	auto *dock = new DockWidget(manager);
	if (!obs_frontend_add_dock_by_id("obs-multiscene-stream-dock", obs_module_text("Dock.Title"), dock)) {
		obs_log(LOG_ERROR, "failed to register dock");
		delete dock;
	}

	obs_frontend_add_event_callback(frontend_event_callback, nullptr);

	obs_log(LOG_INFO, "loaded version %s", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(frontend_event_callback, nullptr);

	// outputs are already torn down by the EXIT event; this only frees
	// memory (and is a safe no-op fallback if EXIT never fired)
	delete manager;
	manager = nullptr;

	obs_log(LOG_INFO, "plugin unloaded");
}
