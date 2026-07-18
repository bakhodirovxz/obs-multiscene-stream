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

#include "dock-widget.hpp"

#include "output-card.hpp"
#include "settings-dialog.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>

static QString mtext(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

DockWidget::DockWidget(OutputManager *manager, QWidget *parent) : QWidget(parent), manager_(manager)
{
	setMinimumWidth(260); // UX-1.4: usable at narrow widths

	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(4, 4, 4, 4);
	outer->setSpacing(4);

	// empty state (UX-1.2): title + one-line explanation + big button
	empty_widget_ = new QWidget(this);
	auto *empty_layout = new QVBoxLayout(empty_widget_);
	empty_layout->setContentsMargins(16, 8, 16, 8);
	empty_layout->setSpacing(8);
	empty_layout->addStretch(1);
	auto *empty_label = new QLabel(mtext("Empty.Title"), empty_widget_);
	empty_label->setAlignment(Qt::AlignCenter);
	QFont title_font = empty_label->font();
	title_font.setBold(true);
	title_font.setPointSizeF(title_font.pointSizeF() + 1.5);
	empty_label->setFont(title_font);
	empty_layout->addWidget(empty_label);
	auto *empty_hint = new QLabel(mtext("Empty.Hint"), empty_widget_);
	empty_hint->setAlignment(Qt::AlignCenter);
	empty_hint->setWordWrap(true);
	empty_hint->setForegroundRole(QPalette::PlaceholderText);
	empty_layout->addWidget(empty_hint);
	auto *empty_add = new QPushButton(mtext("Empty.AddButton"), empty_widget_);
	empty_add->setMinimumHeight(32);
	connect(empty_add, &QPushButton::clicked, this, &DockWidget::onAddOutput);
	auto *center_row = new QHBoxLayout();
	center_row->addStretch(1);
	center_row->addWidget(empty_add, 2);
	center_row->addStretch(1);
	empty_layout->addLayout(center_row);
	empty_layout->addStretch(1);
	outer->addWidget(empty_widget_, 1);

	// card list
	scroll_ = new QScrollArea(this);
	scroll_->setWidgetResizable(true);
	scroll_->setFrameShape(QFrame::NoFrame);
	scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	auto *list_host = new QWidget(scroll_);
	cards_layout_ = new QVBoxLayout(list_host);
	cards_layout_->setContentsMargins(0, 0, 0, 0);
	cards_layout_->setSpacing(4);
	cards_layout_->addStretch(1);
	scroll_->setWidget(list_host);
	outer->addWidget(scroll_, 1);

	// fixed footer (UX-1.3)
	auto *separator = new QFrame(this);
	separator->setFrameShape(QFrame::HLine);
	separator->setFrameShadow(QFrame::Sunken);
	outer->addWidget(separator);

	auto *footer = new QHBoxLayout();
	footer->setSpacing(4);
	add_button_ = new QPushButton(QStringLiteral("+ ") + mtext("Button.AddShort"), this);
	add_button_->setToolTip(mtext("Button.Add"));
	footer->addWidget(add_button_, 1);
	start_all_ = new QPushButton(mtext("Button.StartAll"), this);
	start_all_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
	start_all_->setToolTip(mtext("Tooltip.StartAll"));
	footer->addWidget(start_all_, 1);
	stop_all_ = new QPushButton(mtext("Button.StopAll"), this);
	stop_all_->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
	stop_all_->setToolTip(mtext("Tooltip.StopAll"));
	footer->addWidget(stop_all_, 1);
	outer->addLayout(footer);

	connect(add_button_, &QPushButton::clicked, this, &DockWidget::onAddOutput);
	connect(start_all_, &QPushButton::clicked, this, &DockWidget::onStartAll);
	connect(stop_all_, &QPushButton::clicked, this, &DockWidget::onStopAll);

	connect(manager_, &OutputManager::outputAdded, this, &DockWidget::addCard);
	connect(manager_, &OutputManager::outputRemoved, this, &DockWidget::removeCard);
	connect(manager_, &OutputManager::scenesChanged, this, [this]() {
		for (OutputCard *card : cards_)
			card->refreshScenes();
		updateFooter(); // startability may have changed with scenes
	});

	for (VirtualOutput *vo : manager_->outputs())
		addCard(vo);

	updateEmptyState();
	updateFooter();
}

void DockWidget::addCard(VirtualOutput *vo)
{
	auto *card = new OutputCard(manager_, vo, this);
	cards_.push_back(card);
	// keep the trailing stretch at the end
	cards_layout_->insertWidget(cards_layout_->count() - 1, card);
	connect(vo, &VirtualOutput::stateChanged, this,
		[this](VirtualOutput::State, const QString &) { updateFooter(); });
	updateEmptyState();
	updateFooter();
}

void DockWidget::removeCard(VirtualOutput *vo)
{
	auto it = std::find_if(cards_.begin(), cards_.end(), [vo](OutputCard *card) { return card->output() == vo; });
	if (it == cards_.end())
		return;
	OutputCard *card = *it;
	cards_.erase(it);
	cards_layout_->removeWidget(card);
	card->deleteLater();
	updateEmptyState();
	updateFooter();
}

void DockWidget::updateEmptyState()
{
	bool empty = cards_.empty();
	empty_widget_->setVisible(empty);
	scroll_->setVisible(!empty);
}

void DockWidget::updateFooter()
{
	add_button_->setEnabled(manager_->canAddOutput());
	add_button_->setToolTip(manager_->canAddOutput() ? QString() : mtext("Tooltip.MaxOutputs").arg(MAX_OUTPUTS));

	bool any_startable = false;
	bool any_active = false;
	for (VirtualOutput *vo : manager_->outputs()) {
		if (vo->isActive())
			any_active = true;
		else if (vo->config().enabled && vo->sceneExists())
			any_startable = true;
	}
	start_all_->setEnabled(any_startable);
	stop_all_->setEnabled(any_active);
}

OutputConfig DockWidget::defaultConfig() const
{
	OutputConfig cfg;
	cfg.name = manager_->nextDefaultName();
	cfg.encoder_id = encoders::bestDefault().toStdString(); // OPT-8

	obs_source_t *current = obs_frontend_get_current_scene();
	if (current) {
		const char *name = obs_source_get_name(current);
		if (name)
			cfg.scene_name = name;
		obs_source_release(current);
	}
	return cfg;
}

void DockWidget::onAddOutput()
{
	if (!manager_->canAddOutput())
		return;

	SettingsDialog dialog(defaultConfig(), false, this);
	if (dialog.exec() == QDialog::Accepted)
		manager_->addOutput(dialog.resultConfig());
}

void DockWidget::onStartAll()
{
	manager_->startAll();
}

void DockWidget::onStopAll()
{
	int live = manager_->liveCount();
	if (live >= 2) {
		// UX-1.3: confirm only when 2+ outputs are live
		QMessageBox::StandardButton answer = QMessageBox::question(this, mtext("Confirm.StopAllTitle"),
									   mtext("Confirm.StopAllText").arg(live),
									   QMessageBox::Yes | QMessageBox::No,
									   QMessageBox::No);
		if (answer != QMessageBox::Yes)
			return;
	}
	manager_->stopAll();
}
