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

#include <QElapsedTimer>
#include <QFrame>
#include <QLabel>
#include <QTimer>
#include <QWidget>

#include "../output-manager.hpp"
#include "../virtual-output.hpp"

class QComboBox;
class QPushButton;
class QToolButton;

// Small colored status circle (UX-2.1). Pulses while connecting.
class StatusDot : public QWidget {
	Q_OBJECT

public:
	explicit StatusDot(QWidget *parent = nullptr);
	void setStatus(VirtualOutput::State state);

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	VirtualOutput::State state_ = VirtualOutput::State::Idle;
	QTimer pulse_timer_;
	bool pulse_phase_ = false;
};

// QLabel that elides its text and exposes the full value as tooltip
// (UX-1.4). Plain subclass; no extra signals.
class ElidedLabel : public QLabel {
	Q_OBJECT

public:
	explicit ElidedLabel(QWidget *parent = nullptr);
	void setFullText(const QString &text);

	// the label never calls setText(), so QLabel's own hints would
	// collapse to zero height — report the painted text metrics instead
	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	QString full_text_;
};

// One compact output row (UX-1.1):
// [dot] [name + status / scene combo] [Start/Stop] [gear]
class OutputCard : public QFrame {
	Q_OBJECT

public:
	OutputCard(OutputManager *manager, VirtualOutput *output, QWidget *parent = nullptr);

	VirtualOutput *output() const { return output_; }

public slots:
	void refreshScenes();

private:
	void onStateChanged(VirtualOutput::State state, const QString &message);
	void onStartStopClicked();
	void openSettings();
	void confirmDelete();
	void showContextMenu(const QPoint &pos);
	void updateLiveStats();
	void applyConfigToUi();
	void updateControlsForState();
	bool isUnbound() const;

	OutputManager *manager_;
	VirtualOutput *output_;

	StatusDot *dot_ = nullptr;
	ElidedLabel *name_label_ = nullptr;
	ElidedLabel *status_label_ = nullptr;
	QLabel *warn_icon_ = nullptr;
	QComboBox *scene_combo_ = nullptr;
	QPushButton *start_stop_ = nullptr;
	QToolButton *gear_ = nullptr;

	QTimer stats_timer_;
	QTimer debounce_timer_;
	QElapsedTimer live_clock_;
	uint64_t last_bytes_ = 0;
};
