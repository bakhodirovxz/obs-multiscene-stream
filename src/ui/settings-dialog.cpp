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

#include "settings-dialog.hpp"

#include "../key-protect.hpp"

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPoint>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

static QString mtext(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

/* ---------------- encoder enumeration (FR-3.3, OPT-8) ---------------- */

namespace encoders {

static int preference_rank(const QString &id)
{
	static const char *order[] = {
		"obs_nvenc_h264_tex", "jim_nvenc",        "obs_nvenc_h264", "obs_qsv11_v2",
		"obs_qsv11",          "h264_texture_amf", "amd_amf_h264",   "obs_x264",
	};
	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
		if (id == QLatin1String(order[i]))
			return (int)i;
	}
	return 100;
}

QList<Choice> available()
{
	QList<Choice> list;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
			continue;
		const char *codec = obs_get_encoder_codec(id);
		if (!codec || strcmp(codec, "h264") != 0)
			continue;
		uint32_t caps = obs_get_encoder_caps(id);
		if (caps & (OBS_ENCODER_CAP_DEPRECATED | OBS_ENCODER_CAP_INTERNAL))
			continue;
		const char *name = obs_encoder_get_display_name(id);
		list.append({QString::fromUtf8(id), name ? QString::fromUtf8(name) : QString::fromUtf8(id)});
	}
	std::sort(list.begin(), list.end(), [](const Choice &a, const Choice &b) {
		int ra = preference_rank(a.id);
		int rb = preference_rank(b.id);
		if (ra != rb)
			return ra < rb;
		return a.id < b.id;
	});
	return list;
}

QString bestDefault()
{
	QList<Choice> list = available();
	return list.isEmpty() ? QStringLiteral("obs_x264") : list.front().id;
}

} // namespace encoders

/* ---------------- server presets (UX-3.2) ---------------- */

struct ServerPreset {
	const char *label;
	const char *url;
};

static const ServerPreset kServerPresets[] = {
	{"YouTube (RTMPS)", "rtmps://a.rtmps.youtube.com:443/live2"},
	{"YouTube (RTMP)", "rtmp://a.rtmp.youtube.com/live2"},
	{"Twitch", "rtmp://live.twitch.tv/app"},
};

/* ---------------- dialog ---------------- */

SettingsDialog::SettingsDialog(const OutputConfig &cfg, bool allow_delete, QWidget *parent) : QDialog(parent), cfg_(cfg)
{
	setWindowTitle(mtext("Settings.Title"));
	setModal(true);

	auto raw = keyprotect::unprotect(cfg_.key_protected);
	if (raw) {
		initial_key_ = QString::fromStdString(*raw);
	} else {
		key_decrypt_failed_ = true; // T-23
	}

	buildUi(allow_delete);
	populateScenes();
	populateEncoders();
	applyConfig();
	validate();
}

// small, theme-neutral caption under a field; guides first-time users.
// PlaceholderText follows the active OBS theme, staying readable in both
// dark and light modes (unlike palette(mid), which goes black on Yami).
static QLabel *make_caption(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	label->setWordWrap(true);
	label->setForegroundRole(QPalette::PlaceholderText);
	QFont f = label->font();
	f.setPointSizeF(f.pointSizeF() - 1.0);
	label->setFont(f);
	return label;
}

void SettingsDialog::buildUi(bool allow_delete)
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(10, 10, 10, 10);
	outer->setSpacing(8);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget(scroll);
	auto *column = new QVBoxLayout(content);
	column->setContentsMargins(2, 2, 8, 2);
	column->setSpacing(12);

	// 1. What to stream
	auto *what_group = new QGroupBox(mtext("Settings.Group.What"), content);
	auto *top_form = new QFormLayout(what_group);
	top_form->setVerticalSpacing(8);
	name_edit_ = new QLineEdit(what_group);
	name_edit_->setMaxLength(64);
	name_edit_->setPlaceholderText(mtext("Settings.NamePlaceholder"));
	top_form->addRow(mtext("Settings.Name"), name_edit_);

	scene_combo_ = new QComboBox(what_group);
	top_form->addRow(mtext("Settings.Scene"), scene_combo_);
	top_form->addRow(QString(), make_caption(mtext("Settings.SceneCaption"), what_group));
	column->addWidget(what_group);

	// 2. Where to stream
	auto *dest_group = new QGroupBox(mtext("Settings.Group.Where"), content);
	auto *dest_form = new QFormLayout(dest_group);
	dest_form->setVerticalSpacing(8);

	server_preset_ = new QComboBox(dest_group);
	for (const ServerPreset &p : kServerPresets)
		server_preset_->addItem(QString::fromUtf8(p.label), QString::fromUtf8(p.url));
	server_preset_->addItem(mtext("Settings.ServerPreset.Custom"), QString());
	dest_form->addRow(mtext("Settings.Server"), server_preset_);

	server_edit_ = new QLineEdit(dest_group);
	server_edit_->setPlaceholderText(QStringLiteral("rtmps://..."));
	dest_form->addRow(QString(), server_edit_);

	server_hint_ = new QLabel(dest_group);
	server_hint_->setWordWrap(true);
	server_hint_->setVisible(false);
	dest_form->addRow(QString(), server_hint_);

	auto *key_row = new QHBoxLayout();
	key_edit_ = new QLineEdit(dest_group);
	key_edit_->setEchoMode(QLineEdit::Password); // SEC-3
	key_edit_->setMaxLength(512);
	key_row->addWidget(key_edit_, 1);
	key_toggle_ = new QToolButton(dest_group);
	key_toggle_->setText(mtext("Settings.Key.Show"));
	key_toggle_->setCheckable(true);
	key_row->addWidget(key_toggle_);
	dest_form->addRow(mtext("Settings.Key"), key_row);

	key_hint_ = new QLabel(dest_group);
	key_hint_->setWordWrap(true);
	key_hint_->setVisible(false);
	dest_form->addRow(QString(), key_hint_);

	dest_form->addRow(QString(), make_caption(mtext("Settings.KeyCaption"), dest_group));

	column->addWidget(dest_group);

	// 3. Advanced — collapsed by default: a first-time user only needs
	// scene + destination, the defaults already give a working stream
	adv_toggle_ = new QToolButton(content);
	adv_toggle_->setText(mtext("Settings.Advanced"));
	adv_toggle_->setCheckable(true);
	adv_toggle_->setChecked(false);
	adv_toggle_->setArrowType(Qt::RightArrow);
	adv_toggle_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	adv_toggle_->setAutoRaise(true);
	column->addWidget(adv_toggle_);

	advanced_host_ = new QWidget(content);
	auto *adv_column = new QVBoxLayout(advanced_host_);
	adv_column->setContentsMargins(0, 0, 0, 0);
	adv_column->setSpacing(12);

	auto *video_group = new QGroupBox(mtext("Settings.Video"), advanced_host_);
	auto *video_form = new QFormLayout(video_group);
	video_form->setVerticalSpacing(8);

	encoder_combo_ = new QComboBox(video_group);
	video_form->addRow(mtext("Settings.Encoder"), encoder_combo_);

	resolution_combo_ = new QComboBox(video_group);
	resolution_combo_->addItem(mtext("Settings.Resolution.Canvas"), QPoint(0, 0));
	resolution_combo_->addItem(QStringLiteral("1920x1080"), QPoint(1920, 1080));
	resolution_combo_->addItem(QStringLiteral("1280x720"), QPoint(1280, 720));
	resolution_combo_->addItem(QStringLiteral("854x480"), QPoint(854, 480));
	resolution_combo_->setToolTip(mtext("Tooltip.Resolution"));
	video_form->addRow(mtext("Settings.Resolution"), resolution_combo_);

	vbitrate_spin_ = new QSpinBox(video_group);
	vbitrate_spin_->setRange(500, 20000);
	vbitrate_spin_->setSingleStep(500);
	vbitrate_spin_->setSuffix(QStringLiteral(" kbps"));
	vbitrate_spin_->setToolTip(mtext("Tooltip.Keyframe"));
	video_form->addRow(mtext("Settings.Bitrate"), vbitrate_spin_);

	adv_column->addWidget(video_group);

	auto *audio_group = new QGroupBox(mtext("Settings.Audio"), advanced_host_);
	auto *audio_form = new QFormLayout(audio_group);
	audio_form->setVerticalSpacing(8);

	atrack_combo_ = new QComboBox(audio_group);
	for (int i = 1; i <= 6; i++)
		atrack_combo_->addItem(QString::number(i), i);
	atrack_combo_->setToolTip(mtext("Tooltip.AudioTrack"));
	audio_form->addRow(mtext("Settings.AudioTrack"), atrack_combo_);

	abitrate_combo_ = new QComboBox(audio_group);
	for (int rate : {96, 128, 160, 192, 320})
		abitrate_combo_->addItem(QString::number(rate), rate);
	audio_form->addRow(mtext("Settings.AudioBitrate"), abitrate_combo_);

	adv_column->addWidget(audio_group);

	enabled_check_ = new QCheckBox(mtext("Settings.Enabled"), advanced_host_);
	adv_column->addWidget(enabled_check_);

	advanced_host_->setVisible(false);
	column->addWidget(advanced_host_);

	connect(adv_toggle_, &QToolButton::toggled, this, [this](bool on) {
		advanced_host_->setVisible(on);
		adv_toggle_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
	});

	column->addStretch(1);
	scroll->setWidget(content);
	outer->addWidget(scroll, 1);

	// Buttons
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	ok_button_ = buttons->button(QDialogButtonBox::Ok);
	ok_button_->setText(mtext("Button.Save"));
	buttons->button(QDialogButtonBox::Cancel)->setText(mtext("Button.Cancel"));
	if (allow_delete) {
		QPushButton *del = buttons->addButton(mtext("Button.Delete"), QDialogButtonBox::DestructiveRole);
		connect(del, &QPushButton::clicked, this, [this]() {
			delete_requested_ = true;
			reject();
		});
	}
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	outer->addWidget(buttons);

	// live validation (UX-3.3)
	connect(name_edit_, &QLineEdit::textChanged, this, [this]() { validate(); });
	connect(server_edit_, &QLineEdit::textChanged, this, [this]() { validate(); });
	connect(key_edit_, &QLineEdit::textEdited, this, [this]() { key_touched_ = true; });
	connect(key_edit_, &QLineEdit::textChanged, this, [this]() { validate(); });
	connect(server_preset_, &QComboBox::currentIndexChanged, this, &SettingsDialog::onServerPresetChanged);
	connect(key_toggle_, &QToolButton::toggled, this, [this]() { toggleKeyVisibility(); });

	setMinimumWidth(400);
	resize(480, 520);
}

void SettingsDialog::populateScenes()
{
	scene_combo_->clear();

	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		const char *name = obs_source_get_name(list.sources.array[i]);
		if (name)
			scene_combo_->addItem(QString::fromUtf8(name));
	}
	obs_frontend_source_list_free(&list);

	// keep a missing bound scene visible so the user sees what broke
	QString bound = QString::fromStdString(cfg_.scene_name);
	if (!bound.isEmpty() && scene_combo_->findText(bound) < 0) {
		scene_combo_->insertItem(0, QStringLiteral("⚠ ") + bound + QStringLiteral(" (") +
						    mtext("Warn.Unbound") + QStringLiteral(")"));
		scene_combo_->setItemData(0, bound, Qt::UserRole);
	}
}

void SettingsDialog::populateEncoders()
{
	encoder_combo_->clear();
	const QList<encoders::Choice> list = encoders::available();
	for (const encoders::Choice &c : list)
		encoder_combo_->addItem(c.display_name, c.id);
	if (encoder_combo_->count() == 0)
		encoder_combo_->addItem(QStringLiteral("x264"), QStringLiteral("obs_x264"));
}

void SettingsDialog::applyConfig()
{
	name_edit_->setText(QString::fromStdString(cfg_.name));

	QString bound = QString::fromStdString(cfg_.scene_name);
	int scene_idx = scene_combo_->findText(bound);
	if (scene_idx < 0 && scene_combo_->count() > 0 && scene_combo_->itemData(0, Qt::UserRole).toString() == bound)
		scene_idx = 0;
	if (scene_idx >= 0)
		scene_combo_->setCurrentIndex(scene_idx);

	QString server = QString::fromStdString(cfg_.server);
	int preset_idx = server_preset_->count() - 1; // Custom
	if (server.isEmpty()) {
		// new output: preselect the recommended RTMPS preset so the
		// user only has to pick a scene and paste a key (UX-3.4)
		preset_idx = 0;
		server = server_preset_->itemData(0).toString();
	} else {
		for (int i = 0; i < server_preset_->count() - 1; i++) {
			if (server_preset_->itemData(i).toString() == server) {
				preset_idx = i;
				break;
			}
		}
	}
	server_preset_->setCurrentIndex(preset_idx);
	server_edit_->setText(server);
	server_edit_->setEnabled(preset_idx == server_preset_->count() - 1);

	key_edit_->setText(initial_key_);
	if (key_decrypt_failed_) {
		key_hint_->setText(mtext("Hint.KeyReentry"));
		key_hint_->setVisible(true);
	}

	int enc_idx = encoder_combo_->findData(QString::fromStdString(cfg_.encoder_id));
	encoder_combo_->setCurrentIndex(enc_idx >= 0 ? enc_idx : 0);

	QPoint res(cfg_.out_width, cfg_.out_height);
	int res_idx = 0;
	for (int i = 0; i < resolution_combo_->count(); i++) {
		if (resolution_combo_->itemData(i).toPoint() == res) {
			res_idx = i;
			break;
		}
	}
	resolution_combo_->setCurrentIndex(res_idx);

	vbitrate_spin_->setValue(cfg_.video_bitrate);

	int track_idx = atrack_combo_->findData(cfg_.audio_track);
	atrack_combo_->setCurrentIndex(track_idx >= 0 ? track_idx : 0);

	int abr_idx = abitrate_combo_->findData(cfg_.audio_bitrate);
	abitrate_combo_->setCurrentIndex(abr_idx >= 0 ? abr_idx : 2);

	enabled_check_->setChecked(cfg_.enabled);
}

void SettingsDialog::onServerPresetChanged(int index)
{
	bool custom = index == server_preset_->count() - 1;
	server_edit_->setEnabled(custom);
	if (!custom)
		server_edit_->setText(server_preset_->itemData(index).toString());
	else
		server_edit_->setFocus();
	validate();
}

void SettingsDialog::toggleKeyVisibility()
{
	bool show = key_toggle_->isChecked();
	key_edit_->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
	key_toggle_->setText(show ? mtext("Settings.Key.Hide") : mtext("Settings.Key.Show"));
}

static bool has_control_chars(const QString &s)
{
	for (const QChar &c : s) {
		if (c.unicode() < 0x20)
			return true;
	}
	return false;
}

void SettingsDialog::validate()
{
	static const QRegularExpression scheme_re(QStringLiteral("^rtmps?://\\S+"),
						  QRegularExpression::CaseInsensitiveOption);
	static const QString error_style = QStringLiteral("color: #E74C3C;");

	bool valid = true;

	// server (SEC-4, SEC-5)
	QString server = server_edit_->text().trimmed();
	QString server_hint;
	bool server_error = false;
	if (has_control_chars(server)) {
		server_hint = mtext("Hint.ControlChars");
		server_error = true;
	} else if (!scheme_re.match(server).hasMatch()) {
		server_hint = mtext("Hint.ServerScheme");
		server_error = true;
	} else if (server.startsWith(QStringLiteral("rtmp://"), Qt::CaseInsensitive)) {
		server_hint = mtext("Hint.ServerRtmps"); // advisory only
	}
	server_hint_->setText(server_hint);
	server_hint_->setStyleSheet(server_error ? error_style : QString());
	server_hint_->setVisible(!server_hint.isEmpty());
	valid = valid && !server_error;

	// key (SEC-5)
	QString key = key_edit_->text().trimmed();
	QString key_hint;
	bool key_error = false;
	if (key.isEmpty()) {
		key_error = true;
		if (key_decrypt_failed_)
			key_hint = mtext("Hint.KeyReentry");
		else if (key_touched_)
			key_hint = mtext("Hint.KeyEmpty");
		// pristine empty field: the caption below already explains
		// what to paste — no red text before the user even typed
	} else if (key.size() > 512) {
		key_hint = mtext("Hint.KeyTooLong");
		key_error = true;
	} else if (has_control_chars(key)) {
		key_hint = mtext("Hint.ControlChars");
		key_error = true;
	}
	key_hint_->setText(key_hint);
	key_hint_->setStyleSheet(key_error ? error_style : QString());
	key_hint_->setVisible(!key_hint.isEmpty());
	valid = valid && !key_error;

	if (name_edit_->text().trimmed().isEmpty())
		valid = false;

	ok_button_->setEnabled(valid);
}

OutputConfig SettingsDialog::resultConfig() const
{
	OutputConfig cfg = cfg_;
	cfg.name = name_edit_->text().trimmed().toStdString();

	QVariant scene_data = scene_combo_->currentData(Qt::UserRole);
	cfg.scene_name = scene_data.isValid() ? scene_data.toString().toStdString()
					      : scene_combo_->currentText().toStdString();

	cfg.server = server_edit_->text().trimmed().toStdString();

	QString key = key_edit_->text().trimmed();
	cfg.key_protected = keyprotect::protect(key.toStdString());

	cfg.encoder_id = encoder_combo_->currentData().toString().toStdString();
	QPoint res = resolution_combo_->currentData().toPoint();
	cfg.out_width = res.x();
	cfg.out_height = res.y();
	cfg.video_bitrate = vbitrate_spin_->value();
	cfg.audio_track = atrack_combo_->currentData().toInt();
	cfg.audio_bitrate = abitrate_combo_->currentData().toInt();
	cfg.enabled = enabled_check_->isChecked();
	return cfg;
}
