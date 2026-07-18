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
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>

#include <obs.h>

#include "config.hpp"

// One user-defined stream: scene -> obs_view -> video mix -> encoders ->
// rtmp_custom service -> rtmp_output. The whole chain is built lazily on
// start() and fully destroyed after stop (OPT-1).
//
// Threading model (NFR-2 / OPT-4): all public methods must be called on the
// Qt UI thread. start()/stop() bodies run on a worker thread; OBS signal
// callbacks (arbitrary threads) marshal back via queued invocations.
class VirtualOutput : public QObject {
	Q_OBJECT

public:
	enum class State { Idle, Connecting, Live, Reconnecting, Error };
	Q_ENUM(State)

	explicit VirtualOutput(const OutputConfig &cfg, QObject *parent = nullptr);
	~VirtualOutput() override;

	OutputConfig &config() { return cfg_; }
	const OutputConfig &config() const { return cfg_; }
	void setConfig(const OutputConfig &cfg);

	State state() const { return state_; }
	bool isActive() const { return state_ != State::Idle && state_ != State::Error; }
	QString lastError() const { return last_error_; }
	uint64_t totalBytes() const;

	// True if the bound scene currently exists in the scene collection.
	bool sceneExists() const;

	// True if the scene source held by a running chain was deleted from
	// the collection (as opposed to merely renamed).
	bool boundSceneRemoved() const;

	// Follows a rename of the bound scene while streaming: updates
	// cfg_.scene_name from the held source. Returns true if it changed.
	bool syncSceneNameFromSource();

	bool start(); // false when preconditions fail (already active,
		      // missing scene, undecryptable key)
	void stop();

	// Synchronous teardown for OBS exit / destruction (FR-5.5). Safe to
	// call in any state; does not emit stateChanged.
	void hardShutdown();

	// hardShutdown + stateChanged(Idle) — for scene-collection switches,
	// where the outputs must be torn down before the sources go away but
	// the cards must still update.
	void hardStop();

signals:
	void stateChanged(VirtualOutput::State state, const QString &message);

private:
	void buildAndStartWorker(std::string raw_key);
	Q_INVOKABLE void onStartFailed(const QString &message, const QString &detail);
	Q_INVOKABLE void onStartSucceeded();
	Q_INVOKABLE void onLive();
	Q_INVOKABLE void onStopped(int code, const QString &detail);
	Q_INVOKABLE void onReconnecting();
	Q_INVOKABLE void onReconnected();

	void setState(State state, const QString &message = QString());
	void teardown(); // strict reverse-order cleanup, idempotent (FR-5.5)
	void connectOutputSignals();
	void disconnectOutputSignals();

	static void sig_start(void *data, calldata_t *cd);
	static void sig_stop(void *data, calldata_t *cd);
	static void sig_reconnect(void *data, calldata_t *cd);
	static void sig_reconnect_success(void *data, calldata_t *cd);

	OutputConfig cfg_;
	State state_ = State::Idle;
	QString last_error_;
	bool pending_stop_ = false;
	bool starting_ = false;
	bool signals_connected_ = false;

	// Shared with queued pool tasks: a task locks the mutex, then checks
	// that generation_ still matches the value captured when it was
	// posted. hardShutdown()/start() bump the generation, so a stale
	// queued task returns without touching this object — closing the
	// window where a task could run after teardown or destruction.
	std::shared_ptr<std::mutex> chain_mutex_ = std::make_shared<std::mutex>();
	std::shared_ptr<std::atomic<uint64_t>> generation_ = std::make_shared<std::atomic<uint64_t>>(0);

	obs_source_t *scene_ = nullptr;
	obs_view_t *view_ = nullptr;
	video_t *video_ = nullptr; // owned by view_
	obs_encoder_t *venc_ = nullptr;
	obs_encoder_t *aenc_ = nullptr;
	obs_service_t *service_ = nullptr;
	obs_output_t *output_ = nullptr;
};
