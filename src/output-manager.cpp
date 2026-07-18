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

#include "output-manager.hpp"

#include "config.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>

OutputManager::OutputManager(QObject *parent) : QObject(parent)
{
	save_timer_.setSingleShot(true);
	save_timer_.setInterval(1000);
	connect(&save_timer_, &QTimer::timeout, this, &OutputManager::saveNow);
}

OutputManager::~OutputManager()
{
	for (VirtualOutput *vo : outputs_) {
		vo->hardShutdown();
		delete vo;
	}
	outputs_.clear();
}

VirtualOutput *OutputManager::addOutput(const OutputConfig &cfg)
{
	if (!canAddOutput())
		return nullptr;

	auto *vo = new VirtualOutput(cfg, this);
	outputs_.push_back(vo);
	obs_log(LOG_INFO, "output '%s' created", cfg.name.c_str());
	emit outputAdded(vo);
	scheduleSave();
	return vo;
}

void OutputManager::removeOutput(VirtualOutput *vo)
{
	auto it = std::find(outputs_.begin(), outputs_.end(), vo);
	if (it == outputs_.end())
		return;

	if (vo->isActive()) {
		// FR-1.4: graceful stop first, delete once idle
		connect(vo, &VirtualOutput::stateChanged, this,
			[this, vo](VirtualOutput::State state, const QString &) {
				if (state == VirtualOutput::State::Idle || state == VirtualOutput::State::Error)
					finishRemoval(vo);
			});
		vo->stop();
	} else {
		finishRemoval(vo);
	}
}

void OutputManager::finishRemoval(VirtualOutput *vo)
{
	auto it = std::find(outputs_.begin(), outputs_.end(), vo);
	if (it == outputs_.end())
		return; // already removed

	outputs_.erase(it);
	obs_log(LOG_INFO, "output '%s' removed", vo->config().name.c_str());
	emit outputRemoved(vo);
	vo->disconnect(this);
	vo->deleteLater();
	scheduleSave();
}

void OutputManager::startAll()
{
	for (VirtualOutput *vo : outputs_) {
		if (vo->config().enabled && !vo->isActive() && vo->sceneExists())
			vo->start();
	}
}

void OutputManager::stopAll()
{
	for (VirtualOutput *vo : outputs_) {
		if (vo->isActive())
			vo->stop();
	}
}

int OutputManager::liveCount() const
{
	int n = 0;
	for (VirtualOutput *vo : outputs_) {
		if (vo->state() == VirtualOutput::State::Live || vo->state() == VirtualOutput::State::Reconnecting)
			n++;
	}
	return n;
}

std::string OutputManager::nextDefaultName() const
{
	for (int i = 1; i <= MAX_OUTPUTS + 1; i++) {
		QString candidate = QString::fromUtf8(obs_module_text("Output.DefaultName")).arg(i);
		std::string name = candidate.toStdString();
		bool taken = std::any_of(outputs_.begin(), outputs_.end(),
					 [&name](VirtualOutput *vo) { return vo->config().name == name; });
		if (!taken)
			return name;
	}
	return "Output";
}

void OutputManager::scheduleSave()
{
	if (!loaded_ || shutting_down_)
		return;
	save_timer_.start();
}

void OutputManager::saveNow()
{
	if (!loaded_)
		return;
	save_timer_.stop();

	std::vector<OutputConfig> configs;
	configs.reserve(outputs_.size());
	for (VirtualOutput *vo : outputs_)
		configs.push_back(vo->config());
	config::save(configs);
}

void OutputManager::loadConfig()
{
	if (loaded_)
		return;

	std::vector<OutputConfig> configs;
	config::load(configs);
	loaded_ = true;

	for (const OutputConfig &cfg : configs) {
		if (!canAddOutput())
			break;
		auto *vo = new VirtualOutput(cfg, this);
		outputs_.push_back(vo);
		emit outputAdded(vo);
	}
}

void OutputManager::shutdown()
{
	if (shutting_down_)
		return;
	shutting_down_ = true;

	saveNow();

	obs_log(LOG_INFO, "shutting down %d output(s)", (int)outputs_.size());
	for (VirtualOutput *vo : outputs_)
		vo->hardStop();
}

void OutputManager::handleSceneListChanged()
{
	for (VirtualOutput *vo : outputs_) {
		if (!vo->isActive())
			continue;
		if (vo->boundSceneRemoved()) {
			// FR-2.3: the bound scene was actually deleted
			obs_log(LOG_WARNING, "output '%s': bound scene deleted, stopping", vo->config().name.c_str());
			vo->stop();
		} else if (vo->syncSceneNameFromSource()) {
			// merely renamed — the held source keeps rendering,
			// follow the new name instead of killing the stream
			scheduleSave();
		}
	}
	emit scenesChanged();
}

void OutputManager::onFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		loadConfig();
		emit scenesChanged();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		handleSceneListChanged();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		// T-11: outputs must not keep streaming scenes from a
		// collection that no longer exists
		stopAll();
		handleSceneListChanged();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		// the collection is being unloaded right now, on this thread:
		// an async graceful stop would release the scene refs only
		// after the switch, keeping a zombie scene graph rendering.
		// Tear down synchronously instead.
		for (VirtualOutput *vo : outputs_)
			vo->hardStop();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		shutdown();
		break;
	default:
		break;
	}
}
