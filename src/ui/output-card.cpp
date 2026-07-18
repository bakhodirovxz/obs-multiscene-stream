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

#include "output-card.hpp"

#include "settings-dialog.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QAction>
#include <QComboBox>
#include <QHBoxLayout>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

static QString mtext(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* ---------------- StatusDot ---------------- */

// UX-2.1 status colors; the only hardcoded colors in the plugin (UX-5.1)
static QColor status_color(VirtualOutput::State state)
{
	switch (state) {
	case VirtualOutput::State::Connecting:
		return QColor(0xE6, 0xC2, 0x29); // yellow
	case VirtualOutput::State::Live:
		return QColor(0x2E, 0xCC, 0x71); // green
	case VirtualOutput::State::Reconnecting:
		return QColor(0xE6, 0x7E, 0x22); // orange
	case VirtualOutput::State::Error:
		return QColor(0xE7, 0x4C, 0x3C); // red
	case VirtualOutput::State::Idle:
	default:
		return QColor(0x80, 0x80, 0x80); // gray
	}
}

static QString status_text(VirtualOutput::State state)
{
	switch (state) {
	case VirtualOutput::State::Connecting:
		return mtext("State.Connecting");
	case VirtualOutput::State::Live:
		return mtext("State.Live");
	case VirtualOutput::State::Reconnecting:
		return mtext("State.Reconnecting");
	case VirtualOutput::State::Error:
		return mtext("State.Error");
	case VirtualOutput::State::Idle:
	default:
		return mtext("State.Idle");
	}
}

// stylesheet for the status text label — same five status colors as the dot
// (UX-2.1); color is never the only signal, the text itself names the state
static QString status_css(VirtualOutput::State state)
{
	switch (state) {
	case VirtualOutput::State::Connecting:
		return QStringLiteral("color: #E6C229;");
	case VirtualOutput::State::Live:
		return QStringLiteral("color: #2ECC71;");
	case VirtualOutput::State::Reconnecting:
		return QStringLiteral("color: #E67E22;");
	case VirtualOutput::State::Error:
		return QStringLiteral("color: #E74C3C;");
	case VirtualOutput::State::Idle:
	default:
		return QString();
	}
}

StatusDot::StatusDot(QWidget *parent) : QWidget(parent)
{
	setFixedSize(14, 14);
	pulse_timer_.setInterval(500);
	connect(&pulse_timer_, &QTimer::timeout, this, [this]() {
		pulse_phase_ = !pulse_phase_;
		update();
	});
}

void StatusDot::setStatus(VirtualOutput::State state)
{
	state_ = state;
	bool pulse = state == VirtualOutput::State::Connecting || state == VirtualOutput::State::Reconnecting;
	if (pulse && !pulse_timer_.isActive())
		pulse_timer_.start();
	else if (!pulse)
		pulse_timer_.stop();
	update();
}

void StatusDot::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	QColor c = status_color(state_);
	bool pulse = state_ == VirtualOutput::State::Connecting || state_ == VirtualOutput::State::Reconnecting;
	if (pulse && pulse_phase_)
		c.setAlpha(90);
	p.setPen(Qt::NoPen);
	p.setBrush(c);
	p.drawEllipse(rect().adjusted(2, 2, -2, -2));
}

/* ---------------- ElidedLabel ---------------- */

ElidedLabel::ElidedLabel(QWidget *parent) : QLabel(parent)
{
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
}

void ElidedLabel::setFullText(const QString &text)
{
	full_text_ = text;
	setToolTip(text);
	updateGeometry();
	update();
}

QSize ElidedLabel::sizeHint() const
{
	QFontMetrics fm = fontMetrics();
	return QSize(fm.horizontalAdvance(full_text_), fm.height());
}

QSize ElidedLabel::minimumSizeHint() const
{
	QFontMetrics fm = fontMetrics();
	return QSize(fm.height(), fm.height());
}

void ElidedLabel::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	QFontMetrics fm = fontMetrics();
	QString elided = fm.elidedText(full_text_, Qt::ElideRight, width());
	QRect r = contentsRect();
	p.drawText(r, (int)(alignment() | Qt::AlignVCenter), elided);
}

/* ---------------- OutputCard ---------------- */

OutputCard::OutputCard(OutputManager *manager, VirtualOutput *output, QWidget *parent)
	: QFrame(parent),
	  manager_(manager),
	  output_(output)
{
	setFrameShape(QFrame::StyledPanel);

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(10, 8, 10, 10);
	outer->setSpacing(5);

	// header: [dot] [name] [gear]
	auto *header = new QHBoxLayout();
	header->setSpacing(7);
	dot_ = new StatusDot(this);
	header->addWidget(dot_);
	name_label_ = new ElidedLabel(this);
	QFont bold = name_label_->font();
	bold.setBold(true);
	name_label_->setFont(bold);
	header->addWidget(name_label_, 1);
	gear_ = new QToolButton(this);
	gear_->setText(QStringLiteral("⚙"));
	QFont gear_font = gear_->font();
	gear_font.setPointSizeF(gear_font.pointSizeF() + 2.0);
	gear_->setFont(gear_font);
	gear_->setToolTip(mtext("Button.Settings"));
	gear_->setAutoRaise(true);
	header->addWidget(gear_);
	outer->addLayout(header);

	// status line, indented under the name
	auto *status_row = new QHBoxLayout();
	status_row->setContentsMargins(21, 0, 0, 0);
	status_label_ = new ElidedLabel(this);
	status_row->addWidget(status_label_, 1);
	outer->addLayout(status_row);

	// scene row: "Scene:" [⚠] [combo]
	auto *scene_row = new QHBoxLayout();
	scene_row->setContentsMargins(21, 0, 0, 0);
	scene_row->setSpacing(6);
	auto *scene_caption = new QLabel(mtext("Settings.Scene") + QStringLiteral(":"), this);
	scene_row->addWidget(scene_caption);
	warn_icon_ = new QLabel(this);
	warn_icon_->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxWarning).pixmap(14, 14));
	warn_icon_->setVisible(false);
	scene_row->addWidget(warn_icon_);
	scene_combo_ = new QComboBox(this);
	scene_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	scene_combo_->setMinimumContentsLength(6);
	scene_row->addWidget(scene_combo_, 1);
	outer->addLayout(scene_row);

	// primary action: full-width, easy to hit even in a narrow dock
	start_stop_ = new QPushButton(this);
	start_stop_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
	start_stop_->setText(mtext("Button.Start"));
	start_stop_->setMinimumHeight(30);
	outer->addWidget(start_stop_);

	stats_timer_.setInterval(2000); // UX-2.2
	connect(&stats_timer_, &QTimer::timeout, this, &OutputCard::updateLiveStats);

	debounce_timer_.setSingleShot(true);
	debounce_timer_.setInterval(1000); // UX-4.3
	connect(&debounce_timer_, &QTimer::timeout, this, [this]() { updateControlsForState(); });

	connect(start_stop_, &QPushButton::clicked, this, &OutputCard::onStartStopClicked);
	connect(gear_, &QToolButton::clicked, this, &OutputCard::openSettings);
	// right-click menu: the only delete path that also works while the
	// output is live (FR-1.4 — it is stopped gracefully first)
	setContextMenuPolicy(Qt::CustomContextMenu);
	connect(this, &QWidget::customContextMenuRequested, this, &OutputCard::showContextMenu);
	connect(output_, &VirtualOutput::stateChanged, this, &OutputCard::onStateChanged);
	connect(scene_combo_, &QComboBox::activated, this, [this](int) {
		QVariant data = scene_combo_->currentData(Qt::UserRole);
		output_->config().scene_name = data.isValid() ? data.toString().toStdString()
							      : scene_combo_->currentText().toStdString();
		manager_->scheduleSave();
		updateControlsForState();
	});

	applyConfigToUi();
	refreshScenes();
	onStateChanged(output_->state(), QString());
}

bool OutputCard::isUnbound() const
{
	return !output_->sceneExists();
}

void OutputCard::applyConfigToUi()
{
	name_label_->setFullText(QString::fromStdString(output_->config().name));
}

void OutputCard::refreshScenes()
{
	QSignalBlocker blocker(scene_combo_);
	scene_combo_->clear();

	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		const char *name = obs_source_get_name(list.sources.array[i]);
		if (name)
			scene_combo_->addItem(QString::fromUtf8(name));
	}
	obs_frontend_source_list_free(&list);

	QString bound = QString::fromStdString(output_->config().scene_name);
	int idx = scene_combo_->findText(bound);
	if (idx >= 0) {
		scene_combo_->setCurrentIndex(idx);
	} else if (!bound.isEmpty()) {
		// FR-2.3 / UX-2.4: unbound warning entry
		scene_combo_->insertItem(0, QStringLiteral("⚠ ") + bound);
		scene_combo_->setItemData(0, bound, Qt::UserRole);
		scene_combo_->setCurrentIndex(0);
	}

	updateControlsForState();
}

void OutputCard::updateControlsForState()
{
	VirtualOutput::State state = output_->state();
	bool active = output_->isActive();
	bool unbound = isUnbound();

	warn_icon_->setVisible(unbound);
	warn_icon_->setToolTip(mtext("Tooltip.UnboundStart"));

	// FR-2.4 / FR-7.5 / UX-4.2
	scene_combo_->setEnabled(!active);
	scene_combo_->setToolTip(active ? mtext("Tooltip.StopToChange") : QString());
	gear_->setEnabled(!active);
	gear_->setToolTip(active ? mtext("Tooltip.StopToChange") : mtext("Button.Settings"));

	if (active) {
		start_stop_->setText(mtext("Button.Stop"));
		start_stop_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
		start_stop_->setEnabled(!debounce_timer_.isActive());
		start_stop_->setToolTip(QString());
	} else {
		start_stop_->setText(mtext("Button.Start"));
		start_stop_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
		bool blocked = unbound || debounce_timer_.isActive();
		start_stop_->setEnabled(!blocked);
		start_stop_->setToolTip(unbound ? mtext("Tooltip.UnboundStart") : QString());
	}

	dot_->setStatus(state);
}

void OutputCard::onStateChanged(VirtualOutput::State state, const QString &message)
{
	if (state == VirtualOutput::State::Live) {
		if (!stats_timer_.isActive()) {
			live_clock_.start();
			last_bytes_ = output_->totalBytes();
			stats_timer_.start();
		}
		updateLiveStats();
	} else if (state != VirtualOutput::State::Reconnecting) {
		stats_timer_.stop();
	}

	if (state == VirtualOutput::State::Error && !message.isEmpty()) {
		// UX-2.3: one-liner on the card, technical detail in tooltip
		QString first_line = message.section(QLatin1Char('\n'), 0, 0);
		status_label_->setFullText(status_text(state) + QStringLiteral(" — ") + first_line);
		status_label_->setToolTip(message);
	} else if (state != VirtualOutput::State::Live) {
		status_label_->setFullText(status_text(state));
	}
	status_label_->setStyleSheet(status_css(state));

	updateControlsForState();
}

void OutputCard::updateLiveStats()
{
	if (output_->state() != VirtualOutput::State::Live)
		return;

	uint64_t bytes = output_->totalBytes();
	uint64_t delta = bytes >= last_bytes_ ? bytes - last_bytes_ : 0;
	last_bytes_ = bytes;
	int kbps = (int)(delta * 8 / 2000); // 2 s interval

	qint64 secs = live_clock_.elapsed() / 1000;
	QString elapsed = QStringLiteral("%1:%2:%3")
				  .arg(secs / 3600, 2, 10, QLatin1Char('0'))
				  .arg((secs / 60) % 60, 2, 10, QLatin1Char('0'))
				  .arg(secs % 60, 2, 10, QLatin1Char('0'));

	status_label_->setFullText(mtext("Live.Stats").arg(mtext("State.Live")).arg(elapsed).arg(kbps));
}

void OutputCard::onStartStopClicked()
{
	debounce_timer_.start();
	start_stop_->setEnabled(false);

	if (output_->isActive())
		output_->stop();
	else
		output_->start();
}

void OutputCard::confirmDelete()
{
	// UX-4.1: confirmation names the output; a live output is stopped
	// gracefully before removal (FR-1.4)
	QString name = QString::fromStdString(output_->config().name);
	bool live = output_->isActive();
	QString text = live ? mtext("Confirm.DeleteLiveText").arg(name) : mtext("Confirm.DeleteText").arg(name);
	QMessageBox::StandardButton answer = QMessageBox::question(this, mtext("Confirm.DeleteTitle"), text,
								   QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
	if (answer == QMessageBox::Yes)
		manager_->removeOutput(output_);
}

void OutputCard::showContextMenu(const QPoint &pos)
{
	QMenu menu(this);
	QAction *settings = menu.addAction(mtext("Button.Settings"));
	settings->setEnabled(!output_->isActive());
	QAction *del = menu.addAction(mtext("Button.Delete"));
	QAction *chosen = menu.exec(mapToGlobal(pos));
	if (chosen == settings)
		openSettings();
	else if (chosen == del)
		confirmDelete();
}

void OutputCard::openSettings()
{
	SettingsDialog dialog(output_->config(), true, this);
	int result = dialog.exec();

	if (dialog.deleteRequested()) {
		confirmDelete();
		return;
	}

	if (result == QDialog::Accepted && !output_->isActive()) {
		output_->setConfig(dialog.resultConfig());
		manager_->scheduleSave();
		applyConfigToUi();
		refreshScenes();
	}
}
