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

#include "virtual-output.hpp"

#include "key-protect.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QMetaObject>
#include <QThreadPool>

#include <algorithm>
#include <cstring>

static QString mtext(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

VirtualOutput::VirtualOutput(const OutputConfig &cfg, QObject *parent) : QObject(parent), cfg_(cfg) {}

VirtualOutput::~VirtualOutput()
{
	hardShutdown();
}

void VirtualOutput::setConfig(const OutputConfig &cfg)
{
	if (isActive())
		return;
	cfg_ = cfg;
}

uint64_t VirtualOutput::totalBytes() const
{
	std::lock_guard<std::mutex> lock(*chain_mutex_);
	if (!output_)
		return 0;
	return obs_output_get_total_bytes(output_);
}

bool VirtualOutput::sceneExists() const
{
	if (cfg_.scene_name.empty())
		return false;
	obs_source_t *src = obs_get_source_by_name(cfg_.scene_name.c_str());
	if (!src)
		return false;
	bool is_scene = obs_source_is_scene(src);
	obs_source_release(src);
	return is_scene;
}

bool VirtualOutput::boundSceneRemoved() const
{
	std::lock_guard<std::mutex> lock(*chain_mutex_);
	return scene_ && obs_source_removed(scene_);
}

bool VirtualOutput::syncSceneNameFromSource()
{
	std::lock_guard<std::mutex> lock(*chain_mutex_);
	if (!scene_ || obs_source_removed(scene_))
		return false;
	const char *name = obs_source_get_name(scene_);
	if (!name || cfg_.scene_name == name)
		return false;
	obs_log(LOG_INFO, "output '%s': bound scene renamed '%s' -> '%s'", cfg_.name.c_str(), cfg_.scene_name.c_str(),
		name);
	cfg_.scene_name = name;
	return true;
}

void VirtualOutput::setState(State state, const QString &message)
{
	state_ = state;
	if (state == State::Error)
		last_error_ = message;
	else if (state == State::Live || state == State::Idle)
		last_error_.clear();
	emit stateChanged(state, message);
}

bool VirtualOutput::start()
{
	if (isActive() || starting_)
		return false;

	if (!sceneExists()) {
		setState(State::Error, mtext("Error.SceneMissing").arg(QString::fromStdString(cfg_.scene_name)));
		return false;
	}

	auto raw_key = keyprotect::unprotect(cfg_.key_protected);
	if (!raw_key) {
		// config copied from another user account (T-23)
		setState(State::Error, mtext("Error.KeyNeedsReentry"));
		return false;
	}

	obs_log(LOG_INFO, "output '%s': starting (scene '%s', encoder '%s')", cfg_.name.c_str(),
		cfg_.scene_name.c_str(), cfg_.encoder_id.c_str());

	pending_stop_ = false;
	starting_ = true;
	setState(State::Connecting);

	// OPT-4: never block the UI thread on connect/DNS. The generation
	// token lets hardShutdown() cancel this task even while it is still
	// queued in the pool.
	uint64_t gen = generation_->fetch_add(1) + 1;
	QThreadPool::globalInstance()->start(
		[this, mtx = chain_mutex_, genp = generation_, gen, key = std::move(*raw_key)]() mutable {
			std::lock_guard<std::mutex> lock(*mtx);
			if (genp->load() != gen)
				return; // cancelled; `this` may be gone
			buildAndStartWorker(std::move(key));
		});
	return true;
}

static obs_data_t *make_video_encoder_settings(const OutputConfig &cfg)
{
	obs_data_t *s = obs_data_create();
	obs_data_set_int(s, "bitrate", cfg.video_bitrate);
	obs_data_set_int(s, "keyint_sec", 2);
	obs_data_set_string(s, "rate_control", "CBR");
	if (cfg.encoder_id.find("x264") != std::string::npos)
		obs_data_set_string(s, "preset", "veryfast");
	else if (cfg.encoder_id.find("nvenc") != std::string::npos)
		obs_data_set_string(s, "preset2", "p5");
	return s;
}

// Runs on a worker thread with *chain_mutex_ already locked and the
// generation verified by the caller. Builds the full chain in the order
// required by the spec; on any failure rolls back everything it created and
// reports the failing step. All result reporting is queued back to the UI
// thread while the mutex is still held, so hardShutdown() can never observe
// a half-built chain or miss a late completion.
void VirtualOutput::buildAndStartWorker(std::string raw_key)
{
	auto fail = [this](const QString &message, const QString &detail) {
		// roll back partial chain (NFR-5), reverse order
		disconnectOutputSignals();
		if (output_) {
			obs_output_release(output_);
			output_ = nullptr;
		}
		if (service_) {
			obs_service_release(service_);
			service_ = nullptr;
		}
		if (venc_) {
			obs_encoder_release(venc_);
			venc_ = nullptr;
		}
		if (aenc_) {
			obs_encoder_release(aenc_);
			aenc_ = nullptr;
		}
		if (view_) {
			if (video_)
				obs_view_remove(view_);
			obs_view_set_source(view_, 0, nullptr);
			obs_view_destroy(view_);
			view_ = nullptr;
			video_ = nullptr;
		}
		if (scene_) {
			obs_source_release(scene_);
			scene_ = nullptr;
		}
		QMetaObject::invokeMethod(this, "onStartFailed", Qt::QueuedConnection, Q_ARG(QString, message),
					  Q_ARG(QString, detail));
	};

	// 1. resolve scene
	scene_ = obs_get_source_by_name(cfg_.scene_name.c_str());
	if (!scene_ || !obs_source_is_scene(scene_)) {
		fail(mtext("Error.SceneMissing").arg(QString::fromStdString(cfg_.scene_name)), QString());
		return;
	}

	// 2. independent view rendering the bound scene
	view_ = obs_view_create();
	obs_view_set_source(view_, 0, scene_);

	// 3. video mix for the view; FPS/format inherited from main video
	//    settings (FR-3.2), only the output size may differ
	obs_video_info ovi;
	if (!obs_get_video_info(&ovi)) {
		fail(mtext("Error.ViewFail"), QStringLiteral("no video info"));
		return;
	}
	if (cfg_.out_width > 0 && cfg_.out_height > 0) {
		ovi.output_width = (uint32_t)cfg_.out_width;
		ovi.output_height = (uint32_t)cfg_.out_height;
	} else {
		// canvas passthrough, no extra scaling (OPT-2)
		ovi.output_width = ovi.base_width;
		ovi.output_height = ovi.base_height;
	}
	video_ = obs_view_add2(view_, &ovi);
	if (!video_) {
		fail(mtext("Error.ViewFail"), QString());
		return;
	}

	std::string base = "mss_" + cfg_.name;

	// 4. video encoder — OPT-3: every VirtualOutput owns its encoders.
	// OBS encoders are bound to a single output once started; do NOT
	// "optimize" by sharing encoder instances between outputs.
	obs_data_t *vs = make_video_encoder_settings(cfg_);
	venc_ = obs_video_encoder_create(cfg_.encoder_id.c_str(), (base + "_venc").c_str(), vs, nullptr);
	obs_data_release(vs);
	if (!venc_) {
		fail(mtext("Error.EncoderFail").arg(QString::fromStdString(cfg_.encoder_id)), QString());
		return;
	}
	obs_encoder_set_video(venc_, video_);

	// 5. audio encoder on the selected mixer track (FR-4.2)
	obs_data_t *as = obs_data_create();
	obs_data_set_int(as, "bitrate", cfg_.audio_bitrate);
	aenc_ = obs_audio_encoder_create("ffmpeg_aac", (base + "_aenc").c_str(), as, (size_t)(cfg_.audio_track - 1),
					 nullptr);
	obs_data_release(as);
	if (!aenc_) {
		fail(mtext("Error.EncoderFail").arg(QStringLiteral("ffmpeg_aac")), QString());
		return;
	}
	obs_encoder_set_audio(aenc_, obs_get_audio());

	// 6. service + output
	obs_data_t *ss = obs_data_create();
	obs_data_set_string(ss, "server", cfg_.server.c_str());
	obs_data_set_string(ss, "key", raw_key.c_str());
	obs_data_set_bool(ss, "use_auth", false);
	service_ = obs_service_create("rtmp_custom", (base + "_service").c_str(), ss, nullptr);
	obs_data_release(ss);
	// SEC-2: wipe the raw key from this thread's memory immediately
	std::fill(raw_key.begin(), raw_key.end(), '\0');
	raw_key.clear();
	if (!service_) {
		fail(mtext("Error.ServiceFail"), QString());
		return;
	}

	output_ = obs_output_create("rtmp_output", (base + "_output").c_str(), nullptr, nullptr);
	if (!output_) {
		fail(mtext("Error.OutputFail"), QString());
		return;
	}

	// FR-5.4 automatic reconnect
	obs_output_set_reconnect_settings(output_, 25, 2);
	obs_output_set_video_encoder(output_, venc_);
	obs_output_set_audio_encoder(output_, aenc_, 0);
	obs_output_set_service(output_, service_);

	// 7. signals, then start
	connectOutputSignals();

	if (!obs_output_start(output_)) {
		const char *err = obs_output_get_last_error(output_);
		fail(mtext("Error.StartFail"), err ? QString::fromUtf8(err) : QString());
		return;
	}

	QMetaObject::invokeMethod(this, "onStartSucceeded", Qt::QueuedConnection);
}

void VirtualOutput::stop()
{
	if (starting_) {
		// chain is still being built; stop as soon as it is up
		pending_stop_ = true;
		return;
	}

	if (!output_ || !isActive()) {
		if (state_ != State::Idle && state_ != State::Error)
			setState(State::Idle);
		return;
	}

	obs_log(LOG_INFO, "output '%s': stopping", cfg_.name.c_str());

	// The task locks the chain mutex, so it can never race teardown, and
	// the generation check makes it a no-op if the chain was torn down or
	// restarted before the pool got to it.
	uint64_t gen = generation_->load();
	QThreadPool::globalInstance()->start([this, mtx = chain_mutex_, genp = generation_, gen]() {
		std::lock_guard<std::mutex> lock(*mtx);
		if (genp->load() != gen || !output_)
			return;
		if (obs_output_active(output_)) {
			obs_output_stop(output_);
		} else {
			// still connecting: obs_output_stop() is a
			// no-op before data capture begins, so force
			// it — this interrupts the connect thread and
			// still emits the "stop" signal
			obs_output_force_stop(output_);
		}
	});
	// state transition happens in onStopped() via the "stop" signal
}

void VirtualOutput::hardShutdown()
{
	// cancel any queued-but-not-yet-run pool tasks, then wait for a
	// running one (tasks report their result while holding the mutex, so
	// nothing can arrive after this)
	generation_->fetch_add(1);
	std::lock_guard<std::mutex> lock(*chain_mutex_);
	starting_ = false;
	pending_stop_ = false;

	if (output_) {
		if (obs_output_active(output_)) {
			obs_output_stop(output_); // graceful; release waits
		} else {
			// in-flight connect: force-stop interrupts the
			// connect thread so the release below does not block
			// OBS exit for the whole TCP timeout (T-22)
			obs_output_force_stop(output_);
		}
	}

	teardown();
	state_ = State::Idle;
}

void VirtualOutput::hardStop()
{
	bool was_visible = state_ != State::Idle;
	hardShutdown();
	if (was_visible)
		setState(State::Idle);
}

// assumes chain_mutex_ held (or single-threaded context); FR-5.5 order
void VirtualOutput::teardown()
{
	disconnectOutputSignals();

	if (output_) {
		obs_output_release(output_);
		output_ = nullptr;
	}
	if (service_) {
		obs_service_release(service_);
		service_ = nullptr;
	}
	if (venc_) {
		obs_encoder_release(venc_);
		venc_ = nullptr;
	}
	if (aenc_) {
		obs_encoder_release(aenc_);
		aenc_ = nullptr;
	}
	if (view_) {
		if (video_)
			obs_view_remove(view_);
		obs_view_set_source(view_, 0, nullptr);
		obs_view_destroy(view_);
		view_ = nullptr;
		video_ = nullptr;
	}
	if (scene_) {
		obs_source_release(scene_);
		scene_ = nullptr;
	}
}

void VirtualOutput::connectOutputSignals()
{
	if (!output_ || signals_connected_)
		return;
	signal_handler_t *sh = obs_output_get_signal_handler(output_);
	signal_handler_connect(sh, "start", sig_start, this);
	signal_handler_connect(sh, "stop", sig_stop, this);
	signal_handler_connect(sh, "reconnect", sig_reconnect, this);
	signal_handler_connect(sh, "reconnect_success", sig_reconnect_success, this);
	signals_connected_ = true;
}

void VirtualOutput::disconnectOutputSignals()
{
	if (!output_ || !signals_connected_)
		return;
	signal_handler_t *sh = obs_output_get_signal_handler(output_);
	signal_handler_disconnect(sh, "start", sig_start, this);
	signal_handler_disconnect(sh, "stop", sig_stop, this);
	signal_handler_disconnect(sh, "reconnect", sig_reconnect, this);
	signal_handler_disconnect(sh, "reconnect_success", sig_reconnect_success, this);
	signals_connected_ = false;
}

/* ---- OBS signal callbacks: arbitrary threads, marshal to UI (NFR-2) ---- */

void VirtualOutput::sig_start(void *data, calldata_t *)
{
	auto *self = static_cast<VirtualOutput *>(data);
	QMetaObject::invokeMethod(self, "onLive", Qt::QueuedConnection);
}

void VirtualOutput::sig_stop(void *data, calldata_t *cd)
{
	auto *self = static_cast<VirtualOutput *>(data);
	int code = (int)calldata_int(cd, "code");
	auto *output = (obs_output_t *)calldata_ptr(cd, "output");
	const char *err = output ? obs_output_get_last_error(output) : nullptr;
	QMetaObject::invokeMethod(self, "onStopped", Qt::QueuedConnection, Q_ARG(int, code),
				  Q_ARG(QString, err ? QString::fromUtf8(err) : QString()));
}

void VirtualOutput::sig_reconnect(void *data, calldata_t *)
{
	auto *self = static_cast<VirtualOutput *>(data);
	QMetaObject::invokeMethod(self, "onReconnecting", Qt::QueuedConnection);
}

void VirtualOutput::sig_reconnect_success(void *data, calldata_t *)
{
	auto *self = static_cast<VirtualOutput *>(data);
	QMetaObject::invokeMethod(self, "onReconnected", Qt::QueuedConnection);
}

/* ---------------- UI-thread continuations ---------------- */

void VirtualOutput::onStartFailed(const QString &message, const QString &detail)
{
	starting_ = false;
	pending_stop_ = false;
	obs_log(LOG_WARNING, "output '%s': start failed: %s (%s)", cfg_.name.c_str(), message.toUtf8().constData(),
		detail.toUtf8().constData());
	QString msg = message;
	if (!detail.isEmpty())
		msg += QStringLiteral("\n") + detail;
	setState(State::Error, msg);
}

void VirtualOutput::onStartSucceeded()
{
	starting_ = false;
	if (pending_stop_) {
		pending_stop_ = false;
		stop();
	}
}

void VirtualOutput::onLive()
{
	if (state_ == State::Connecting || state_ == State::Reconnecting) {
		obs_log(LOG_INFO, "output '%s': live", cfg_.name.c_str());
		setState(State::Live);
	}
}

static QString stop_code_message(int code)
{
	switch (code) {
	case OBS_OUTPUT_BAD_PATH:
		return mtext("Error.InvalidKeyOrServer");
	case OBS_OUTPUT_CONNECT_FAILED:
		return mtext("Error.ConnectFailed");
	case OBS_OUTPUT_INVALID_STREAM:
		return mtext("Error.InvalidKeyOrServer");
	case OBS_OUTPUT_DISCONNECTED:
		return mtext("Error.Disconnected");
	case OBS_OUTPUT_UNSUPPORTED:
		return mtext("Error.Unsupported");
	case OBS_OUTPUT_NO_SPACE:
		return mtext("Error.NoSpace");
	case OBS_OUTPUT_ENCODE_ERROR:
		return mtext("Error.EncodeError");
	default:
		return mtext("Error.Generic");
	}
}

void VirtualOutput::onStopped(int code, const QString &detail)
{
	{
		std::lock_guard<std::mutex> lock(*chain_mutex_);
		teardown();
	}
	starting_ = false;
	pending_stop_ = false;

	if (code == OBS_OUTPUT_SUCCESS) {
		obs_log(LOG_INFO, "output '%s': stopped", cfg_.name.c_str());
		setState(State::Idle);
	} else {
		obs_log(LOG_WARNING, "output '%s': stopped with error %d", cfg_.name.c_str(), code);
		QString msg = stop_code_message(code);
		if (!detail.isEmpty())
			msg += QStringLiteral("\n") + detail;
		setState(State::Error, msg);
	}
}

void VirtualOutput::onReconnecting()
{
	if (isActive()) {
		obs_log(LOG_INFO, "output '%s': reconnecting", cfg_.name.c_str());
		setState(State::Reconnecting);
	}
}

void VirtualOutput::onReconnected()
{
	if (isActive()) {
		obs_log(LOG_INFO, "output '%s': reconnected", cfg_.name.c_str());
		setState(State::Live);
	}
}
