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

#include <QDialog>
#include <QList>
#include <QString>

#include "../config.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace encoders {

struct Choice {
	QString id;
	QString display_name;
};

// Available H.264 video encoders in the running OBS, best first (OPT-8).
QList<Choice> available();
QString bestDefault();

} // namespace encoders

// Per-output settings dialog (UX-3): one scrollable column in the order
// Name -> Scene -> Destination -> Video -> Audio, with inline validation.
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	SettingsDialog(const OutputConfig &cfg, bool allow_delete, QWidget *parent = nullptr);

	OutputConfig resultConfig() const;
	bool deleteRequested() const { return delete_requested_; }

private:
	void buildUi(bool allow_delete);
	void populateScenes();
	void populateEncoders();
	void applyConfig();
	void validate();
	void onServerPresetChanged(int index);
	void toggleKeyVisibility();

	OutputConfig cfg_;
	bool delete_requested_ = false;
	bool key_decrypt_failed_ = false;
	bool key_touched_ = false; // errors only after the user typed
	QString initial_key_;      // raw key, dialog lifetime only (SEC-2)

	QLineEdit *name_edit_ = nullptr;
	QComboBox *scene_combo_ = nullptr;
	QComboBox *server_preset_ = nullptr;
	QLineEdit *server_edit_ = nullptr;
	QLabel *server_hint_ = nullptr;
	QLineEdit *key_edit_ = nullptr;
	QToolButton *key_toggle_ = nullptr;
	QLabel *key_hint_ = nullptr;
	QComboBox *encoder_combo_ = nullptr;
	QComboBox *resolution_combo_ = nullptr;
	QSpinBox *vbitrate_spin_ = nullptr;
	QComboBox *atrack_combo_ = nullptr;
	QComboBox *abitrate_combo_ = nullptr;
	QCheckBox *enabled_check_ = nullptr;
	QPushButton *ok_button_ = nullptr;
	QToolButton *adv_toggle_ = nullptr;
	QWidget *advanced_host_ = nullptr; // video+audio, collapsed by default
};
