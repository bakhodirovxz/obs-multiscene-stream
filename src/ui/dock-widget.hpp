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

#include <QWidget>

#include <vector>

#include "../output-manager.hpp"

class OutputCard;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

// Main dock panel (FR-7): vertical list of output cards + footer with
// Add / Start All / Stop All. Shows a friendly empty state (UX-1.2).
class DockWidget : public QWidget {
	Q_OBJECT

public:
	explicit DockWidget(OutputManager *manager, QWidget *parent = nullptr);

private:
	void addCard(VirtualOutput *vo);
	void removeCard(VirtualOutput *vo);
	void onAddOutput();
	void onStartAll();
	void onStopAll();
	void updateEmptyState();
	void updateFooter();
	OutputConfig defaultConfig() const;

	OutputManager *manager_;
	std::vector<OutputCard *> cards_;

	QScrollArea *scroll_ = nullptr;
	QVBoxLayout *cards_layout_ = nullptr;
	QWidget *empty_widget_ = nullptr;
	QPushButton *add_button_ = nullptr;
	QPushButton *start_all_ = nullptr;
	QPushButton *stop_all_ = nullptr;
};
