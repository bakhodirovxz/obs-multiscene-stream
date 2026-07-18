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

#include <QObject>
#include <QTimer>

#include <obs-frontend-api.h>

#include <vector>

#include "virtual-output.hpp"

// Owns all VirtualOutput objects and the persisted configuration.
// Lives on the Qt UI thread for the whole OBS session.
class OutputManager : public QObject {
	Q_OBJECT

public:
	explicit OutputManager(QObject *parent = nullptr);
	~OutputManager() override;

	const std::vector<VirtualOutput *> &outputs() const { return outputs_; }
	bool canAddOutput() const { return (int)outputs_.size() < MAX_OUTPUTS; }
	VirtualOutput *addOutput(const OutputConfig &cfg);
	// Stops the output first if it is live (FR-1.4), then deletes it.
	void removeOutput(VirtualOutput *vo);

	void startAll(); // enabled outputs only (FR-5.2)
	void stopAll();
	int liveCount() const;

	void scheduleSave(); // debounced 1 s (FR-6.5)
	void saveNow();

	void onFrontendEvent(enum obs_frontend_event event);

	// Unique default name like "Output 2" for a new output.
	std::string nextDefaultName() const;

signals:
	void outputAdded(VirtualOutput *vo);
	void outputRemoved(VirtualOutput *vo);
	// scene list or scene collection changed; cards must re-validate
	void scenesChanged();

private:
	void loadConfig(); // FR-6.4: on FINISHED_LOADING
	void shutdown();   // FR-5.5: on EXIT
	void handleSceneListChanged();
	void finishRemoval(VirtualOutput *vo);

	std::vector<VirtualOutput *> outputs_;
	QTimer save_timer_;
	bool loaded_ = false;
	bool shutting_down_ = false;
};
