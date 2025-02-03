/* Copyright (C) 2025 anonymix007
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Note: this code is inspired by bthost_ipc, but it is mostly reimplemented from scratch based on PAL expectations
 */

#define LOG_TAG "BTAudioOffloadStream"
#include <android-base/logging.h>

#include <BluetoothAudioSessionControl.h>

#include "audio_stream.h"

using aidl::android::media::audio::common::toString;

namespace aidl::android::hardware::bluetooth::audio {

// a2dp_stream_common_init
void AudioStreamCommon::init(SessionType type) {
    state = AudioState::STANDBY;
    ack.status = ControlStatus::UNKNOWN;
    reconfig_pending = false;
    multicast = 0;
    num_conn_dev = 0;
    enc.set<AudioConfiguration::pcmConfig>(PcmConfiguration());
    dec.set<AudioConfiguration::pcmConfig>(PcmConfiguration());
    sink_latency = 0;
    ctrl_key = 0;
    session.type = type;
}

void AudioStreamCommon::updateStreamCodecConfig() {
    auto &audioConfig = BluetoothAudioSessionControl::GetAudioConfig(session.type);
    switch (session.type) {
        case SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
            enc.set<AudioConfiguration::a2dpConfig>(audioConfig.get<AudioConfiguration::a2dpConfig>());
            break;
        case SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
            enc.set<AudioConfiguration::leAudioConfig>(audioConfig.get<AudioConfiguration::leAudioConfig>());
            break;
        case SessionType::LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH:
            dec.set<AudioConfiguration::leAudioConfig>(audioConfig.get<AudioConfiguration::leAudioConfig>());
            break;
        case SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
            enc.set<AudioConfiguration::leAudioBroadcastConfig>(audioConfig.get<AudioConfiguration::leAudioBroadcastConfig>());
            break;
        default:
            break;
    }
}



void AudioStreamCommon::clearSuspendFlag() {
    if (state == AudioState::SUSPENDED) {
        state = AudioState::STOPPED;
    }
}

const AudioConfiguration *AudioStreamCommon::getConfig() {
    switch (session.type) {
        case SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
        case SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
        case SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
            return &enc;
        case SessionType::LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH:
            return &dec;
        default:
            return nullptr;
    }
}

constexpr auto kStackResponseTimeout = std::chrono::milliseconds(4500);
constexpr auto kAttemptDelay = std::chrono::milliseconds(200);
constexpr auto kFailureRetryAttemps = 10;
constexpr auto kInCallRetryAttemps = 5;

// audio_config_changed_handler
void AudioStreamHandler::handleChanges() {
    LOG(DEBUG) << __func__ << ": init";

    SessionType type;
    while (true) {
        {
            std::unique_lock<std::mutex> lk(mConfigMutex);
            LOG(DEBUG) << __func__ << ": waiting...";
            if (mReconfigQueue.empty()) {
                mConfigCond.wait(lk);
            }
            LOG(DEBUG) << __func__ << ": queue size: " << mReconfigQueue.size();
            if (mReconfigQueue.empty()) {
                continue;
            }
            type = mReconfigQueue[0].session;
        }

        auto &stream = getStream(type);
        LOG(DEBUG) << __func__ << ": session type: " << toString(type);
        LOG(DEBUG) << __func__ << ": reconfig pending: " << stream.reconfig_pending;

        if (!stream.reconfig_pending) {
            stream.updateStreamCodecConfig();
        }

        if (!mCb) {
            continue;
        }

        std::lock_guard<std::mutex> lk(mConfigMutex);
        if (mReconfigQueue.empty()) {
            mCb(static_cast<tSESSION_TYPE>(type), !stream.reconfig_pending);
        } else {
            LOG(DEBUG) << __func__ << ": calling reconfig cb with session " << (int) mReconfigQueue[0].session << ", status " << mReconfigQueue[0].status;
            mCb(static_cast<tSESSION_TYPE>(mReconfigQueue[0].session), mReconfigQueue[0].status);
            mReconfigQueue.erase(mReconfigQueue.begin());
        }
        LOG(DEBUG) << __func__ << ": reconfig cb called";
    }
}

//get_session_type
SessionType AudioStreamHandler::getSession(uint16_t control) {
    for (auto it = mStreams.begin(); it != mStreams.end(); ++it) {
        if (it->ctrl_key == control) {
            return it->session.type;
        }
    }
    return SessionType::UNKNOWN;
}

//stack_resp_cb
void AudioStreamHandler::handleControlResult(uint16_t cookie, bool start_resp, BluetoothAudioStatus status) {
    LOG(INFO) << __func__ << ": status: " << toString(status);
    SessionType type = getSession(cookie);
    if (type == SessionType::UNKNOWN) {
        LOG(ERROR) << __func__ << ": invalid cookie " << cookie;
        return;
    }
    auto &stream = getStream(type);
    std::unique_lock<std::mutex> guard(stream.ack.lock);
    stream.ack.status = aidl2ControlStatus(status);

    if (!stream.ack.recvd) {
        stream.ack.recvd = true;
        stream.ack.cond.notify_all();
    }

    // TODO: do we need to lock mConfigMutex here?
    if (stream.reconfig_pending && stream.ack.status == ControlStatus::SUCCESS) {
        stream.reconfig_pending = false;
        std::unique_lock<std::mutex> lk(mConfigMutex);
        mReconfigQueue.emplace_back(type, 1);
        LOG(INFO) << __func__ << ": calling mCb with state 1";
        mConfigCond.notify_all();
    } else if (stream.ack.status == ControlStatus::RECONFIGURATION) {
        stream.reconfig_pending = true;
        stream.state = AudioState::SUSPENDED;
        std::unique_lock<std::mutex> lk(mConfigMutex);
        mReconfigQueue.emplace_back(type, 1);
        LOG(INFO) << __func__ << ": calling mCb with state 0";
        mConfigCond.notify_all();
    }
}

//session_resp_cb
void AudioStreamHandler::handleSessionChange(uint16_t cookie) {
    LOG(INFO) << __func__;
    SessionType type = getSession(cookie);
    if (type == SessionType::UNKNOWN) {
        LOG(ERROR) << __func__ << ": invalid cookie " << cookie;
        return;
    }
    auto &stream = getStream(type);
    std::unique_lock<std::mutex> guard(stream.ack.lock);

    LOG(INFO) << __func__ << ": session ready: " << stream.session.ready;

    if (stream.session.ready || stream.state == AudioState::STARTED || stream.state == AudioState::STARTING) {
        stream.session.ready = false;
        stream.state = AudioState::STANDBY;
        stream.ack.status = ControlStatus::UNKNOWN;
        stream.reconfig_pending = false;
        LOG(INFO) << __func__ << ": session ended";
        deinitOffloadPort(type, false);
    } else if (!stream.session.ready) {
        stream.session.ready = true;
        stream.state = AudioState::STANDBY;
        stream.updateStreamCodecConfig();
    }

    if (!stream.ack.recvd) {
        stream.ack.recvd = true;
        stream.ack.cond.notify_all();
    }

    LOG(DEBUG) << __func__ << ": state: " << toString(stream.state);
}

//audio_configuration_changed_cb
void AudioStreamHandler::handleAudioConfigChange(uint16_t cookie) {
    LOG(INFO) << __func__;
    SessionType type = getSession(cookie);
    if (type == SessionType::UNKNOWN) {
        LOG(ERROR) << __func__ << ": invalid cookie " << cookie;
        return;
    }
    auto &stream = getStream(type);
    std::unique_lock<std::mutex> guard(stream.ack.lock);

    if (!stream.reconfig_pending) {
        /* Stream haven't started yet, copy the config and wait */
        stream.updateStreamCodecConfig();
    } else {
        /* Stream suspended for reconfiguration, process in the handler thread */
        std::unique_lock<std::mutex> lk(mConfigMutex);
        LOG(INFO) << __func__ << ": session type: " << toString(type);
        mReconfigQueue.emplace_back(type, 1);
        stream.reconfig_pending = false;
        if (type == SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH) {
            stream.state = AudioState::STANDBY;
        }
        mConfigCond.notify_all();
    }
}

//btapoffload_port_init
void AudioStreamHandler::initOffloadPort(SessionType type) {
    LOG(DEBUG) << __func__ << ": session type: " << toString(type);
    auto &stream = getStream(type);
    if (BluetoothAudioSessionControl::IsSessionReady(stream.session.type)) {
        stream.state = AudioState::STANDBY;
        stream.session.ready = true;
        stream.updateStreamCodecConfig();
    } else {
        LOG(ERROR) << __func__ << ": BluetoothProvider session is not available";
    }

    PortStatusCallbacks callbacks = {
        .control_result_cb_ = std::bind_front(&AudioStreamHandler::handleControlResult, this),
        .session_changed_cb_ = std::bind_front(&AudioStreamHandler::handleSessionChange, this),
        .audio_configuration_changed_cb_ = std::bind_front(&AudioStreamHandler::handleAudioConfigChange, this),
    };

    stream.ctrl_key = BluetoothAudioSessionControl::RegisterControlResultCback(stream.session.type, callbacks);
    LOG(INFO) << __func__ << ": Control: " << stream.ctrl_key;
}

//btapoffload_port_deinit
void AudioStreamHandler::deinitOffloadPort(SessionType type, bool unregister) {
    auto &stream = getStream(type);
    LOG(INFO) << __func__ << ": session: " << toString(type) << ", control: " << stream.ctrl_key;

    if (unregister) {
        BluetoothAudioSessionControl::UnregisterControlResultCback(stream.session.type, stream.ctrl_key);
    }
    stream.ctrl_key = 0;
}

//audio_stream_open
int AudioStreamHandler::openAudioStream(SessionType type) {
    auto &stream = getStream(type);
    if (stream.ctrl_key != 0 && stream.session.ready) {
        LOG(WARNING) << __func__ << ": session was already opened";
        return 0;
    }
    LOG(INFO) << ": trying AIDL sessions for type " << toString(type);

    stream.init(type);
    initOffloadPort(type);
    stream.session.ready = BluetoothAudioSessionControl::IsSessionReady(stream.session.type);
    if (stream.session.ready) {
        LOG(INFO) << __func__ << ": success";
        return 0;
    }
    deinitOffloadPort(type, true);
    LOG(WARNING) << __func__ << ": ignoring QC HIDL sessions";
    LOG(ERROR) << __func__ << ": failed";
    return -1;
}

//audio_stream_start
int AudioStreamHandler::startAudioStream(SessionType type) {
    auto &stream = getStream(type);
    LOG(INFO) << __func__ << ": session: " << toString(type) << ", state: " << toString(stream.state);

    size_t num_attempts = kFailureRetryAttemps;

    if (!stream.session.ready && BluetoothAudioSessionControl::IsSessionReady(type)) {
        LOG(DEBUG) << __func__ << ": session state was never updated, initializing";
        openAudioStream(type);
    }

    if (stream.session.ready) {
        LOG(INFO) << __func__ << ": state: " << toString(stream.state);

       if (stream.state == AudioState::SUSPENDED) {
           LOG(WARNING) << __func__ << ": stream suspended";
           return -1;
       } else if(stream.state == AudioState::STARTED) {
           LOG(WARNING) << __func__ << ": stream already started";
           return ControlStatus::SUCCESS;
       }

        std::unique_lock<std::mutex> lk(stream.ack.lock);
        stream.ack.recvd = false;
        stream.ack.status = ControlStatus::UNKNOWN;
        lk.unlock();

        if (!BluetoothAudioSessionControl::StartStream(type)) {
            LOG(ERROR) << __func__ << ": client has died";
            return -1;
        }

        lk.lock();
        stream.state = AudioState::STARTING;
        if (stream.ack.status == ControlStatus::UNKNOWN) {
            lk.unlock();
            waitForStackResponse(kStackResponseTimeout, type);
            lk.lock();
            LOG(DEBUG) << __func__ << ": status: " << toString(stream.ack.status);
        }

        stream.state = AudioState::STANDBY;
        switch (stream.ack.status) {
            case ControlStatus::SUCCESS:
                LOG(INFO) << __func__ << ": stream started successfully";
                stream.state = AudioState::STARTED;
                return ControlStatus::SUCCESS;

            case ControlStatus::INCALL_FAILURE:
                num_attempts = kInCallRetryAttemps;
            case ControlStatus::FAILURE:
                LOG(INFO) << __func__ << ": failed to start the stream: status: " << toString(stream.ack.status);
                break;

            case ControlStatus::RECONFIGURATION:
                stream.state = AudioState::SUSPENDED;
            case ControlStatus::UNSUPPORTED:
            case ControlStatus::DISCONNECT_IN_PROGRESS:
            case ControlStatus::UNKNOWN:
                LOG(INFO) << __func__ << ": failed to start the stream: status: " << toString(stream.ack.status);
                return stream.ack.status;
            default:
                LOG(INFO) << __func__ << ": failed to start the stream: unexpected status: " << toString(stream.ack.status);
                break;
        }

        if (type == SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH || type == SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH) {
            for (size_t i = 0; i < num_attempts; i++) {
                LOG(INFO) << __func__ << ": sleeping for " << kAttemptDelay;
                std::this_thread::sleep_for(kAttemptDelay);
                LOG(INFO) << __func__ << ": attempting to start stream";
                stream.ack.recvd = false;
                stream.ack.status = ControlStatus::UNKNOWN;
                lk.unlock();

                if (!BluetoothAudioSessionControl::StartStream(type)) {
                    LOG(ERROR) << __func__ << ": client has died";
                    return -1;
                }

                lk.lock();
                stream.state = AudioState::STARTING;

                if (stream.ack.status == ControlStatus::UNKNOWN) {
                    lk.unlock();
                    waitForStackResponse(kStackResponseTimeout, type);
                    lk.lock();
                    LOG(DEBUG) << __func__ << ": status: " << toString(stream.ack.status);
                }

                stream.state = AudioState::STANDBY;
                switch (stream.ack.status) {
                    case ControlStatus::SUCCESS:
                        LOG(INFO) << __func__ << ": stream started successfully";
                        stream.state = AudioState::STARTED;
                        return ControlStatus::SUCCESS;

                    case ControlStatus::INCALL_FAILURE:
                    case ControlStatus::FAILURE:
                        LOG(INFO) << __func__ << ": failed to start the stream: status: " << toString(stream.ack.status);
                        break;

                    case ControlStatus::RECONFIGURATION:
                        stream.state = AudioState::SUSPENDED;
                    case ControlStatus::UNSUPPORTED:
                    case ControlStatus::DISCONNECT_IN_PROGRESS:
                    case ControlStatus::UNKNOWN:
                        LOG(INFO) << __func__ << ": failed to start the stream: status: " << toString(stream.ack.status);
                        return stream.ack.status;
                    default:
                        LOG(INFO) << __func__ << ": failed to start the stream: unexpected status: " << toString(stream.ack.status);
                        break;
                }
            }
        }
        return stream.ack.status;
    } else {
        LOG(WARNING) << __func__ << ": session is not active";
        return -1;
    }
}

//audio_check_a2dp_ready_api
int AudioStreamHandler::checkAudioStream(SessionType type) {
    auto &stream = getStream(type);
    LOG(INFO) << __func__ << ": session: " << toString(type) << ", state: " << toString(stream.state) << ", ready: " << stream.session.ready;

    if (type != SessionType::UNKNOWN && !stream.session.ready) {
        LOG(INFO) << __func__ << ": session restarted, reinitializing port";
        openAudioStream(type);
    }
    return stream.session.ready;
}

//audio_stream_suspend
int AudioStreamHandler::suspendAudioStream(SessionType type) {
    auto &stream = getStream(type);
    LOG(INFO) << __func__ << ": session: " << toString(type) << ", state: " << toString(stream.state);

    if (stream.state == AudioState::SUSPENDED
        || ((stream.state == AudioState::STOPPED || stream.state == AudioState::STANDBY)
            && type != SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH)) {
        LOG(DEBUG) << __func__ << ": no need to suspend";
        return 0;
    }

    if (stream.session.ready) {
        stream.ack.recvd = false;
        stream.ack.status = ControlStatus::UNKNOWN;
        if (!BluetoothAudioSessionControl::SuspendStream(type)) {
            LOG(ERROR) << __func__ << ": client has died";
            return -1;
        }
        std::unique_lock<std::mutex> lk(stream.ack.lock);
        LOG(WARNING) << __func__ << ": ack status: " << toString(stream.ack.status);
        if (stream.ack.status == ControlStatus::UNKNOWN) {
            lk.unlock();
            waitForStackResponse(kStackResponseTimeout, type);
            lk.lock();
        }
        if (stream.ack.status == ControlStatus::SUCCESS) {
            LOG(DEBUG) << __func__ << ": suspended successfully!";
            stream.state = AudioState::SUSPENDED;
            return 0;
        } else {
            LOG(WARNING) << __func__ << ": failed to suspend";
            return -1;
        }
    } else {
        LOG(WARNING) << __func__ << ": session is not ready, cannot suspend";
        return -1;
    }
}

//audio_stream_stop
int AudioStreamHandler::stopAudioStream(SessionType type) {
    auto &stream = getStream(type);
    LOG(INFO) << __func__ << ": session: " << toString(type);

    switch (stream.state) {
        case AudioState::SUSPENDED:
        case AudioState::STANDBY:
        case AudioState::STOPPED:
            LOG(DEBUG) << __func__ << ": already stopped or in standby";
            return 0;
        default:
            break;
    }

    // TODO: free aac frame_ptr_ctl and abr_ptr_ctl
    // Probably do this in the (future) CodecParser class
    // Most likely in the main BluetoothAudioOffload class
    // Same with LE Audio/LC3?

    /*free(aac_codec.frame_ptr_ctl);
    aac_codec.frame_ptr_ctl = NULL;
    if (aac_codec.abr_ptr_ctl != NULL) {
      free(aac_codec.abr_ptr_ctl);
      aac_codec.abr_ptr_ctl = NULL;
    }*/

    if (stream.session.ready) {
        LOG(DEBUG) << __func__ << ": state: " << toString(stream.state);
        std::unique_lock<std::mutex> lk(stream.ack.lock);
        stream.ack.recvd = false;
        stream.ack.status = ControlStatus::UNKNOWN;
        lk.unlock();
        if (!BluetoothAudioSessionControl::SuspendStream(type)) {
            LOG(ERROR) << __func__ << ": client has died";
            return -1;
        }
        lk.lock();
        LOG(WARNING) << __func__ << ": ack status: " << toString(stream.ack.status);
        if (stream.ack.status == ControlStatus::UNKNOWN) {
            stream.state = AudioState::STOPPING;
            lk.unlock();
            waitForStackResponse(kStackResponseTimeout, type);
            lk.lock();
        }
        if (stream.ack.status == ControlStatus::SUCCESS) {
            LOG(DEBUG) << __func__ << ": suspended successfully!";
            stream.state = AudioState::SUSPENDED;
            return 0;
        } else {
            LOG(WARNING) << __func__ << ": failed to stop";
            return -1;
        }
    } else {
        LOG(WARNING) << __func__ << ": session is not ready, cannot stop";
        stream.state = AudioState::STOPPED;
        return -1;
    }

    stream.state = AudioState::STOPPED;
    return 0;
}

//audio_stream_close
int AudioStreamHandler::closeAudioStream(SessionType type) {
    auto &stream = getStream(type);
    LOG(INFO) << __func__ << ": session: " << toString(type);

    ControlStatus status = ControlStatus::UNKNOWN;

    // TODO: free aac frame_ptr_ctl and abr_ptr_ctl
    // Probably do this in the (future) CodecParser class
    // Most likely in the main BluetoothAudioOffload class
    // Same with LE Audio/LC3?

    /*free(aac_codec.frame_ptr_ctl);
    aac_codec.frame_ptr_ctl = NULL;
    if (aac_codec.abr_ptr_ctl != NULL) {
      free(aac_codec.abr_ptr_ctl);
      aac_codec.abr_ptr_ctl = NULL;
    }*/

    if (stream.state == AudioState::STARTED || stream.state == AudioState::STOPPING) {
        stream.state = AudioState::STOPPED;
        if (stream.session.ready) {
            std::unique_lock<std::mutex> lk(stream.ack.lock);
            stream.ack.status = ControlStatus::UNKNOWN;
            stream.ack.recvd = false;
            lk.unlock();
            LOG(WARNING) << __func__ << ": suspending audio stream";
            if (!BluetoothAudioSessionControl::SuspendStream(type)) {
                LOG(ERROR) << __func__ << ": client has died";
                return -1;
            }
            lk.lock();
            LOG(INFO) << __func__ << ": ack status: " << toString(stream.ack.status);
            if (stream.ack.status == ControlStatus::UNKNOWN) {
                lk.unlock();
                waitForStackResponse(kStackResponseTimeout, type);
                lk.lock();
            }
            status = stream.ack.status;
        }
    }

    deinitOffloadPort(type, true);

    if (status == ControlStatus::UNKNOWN) {
        LOG(ERROR) << __func__ << ": failed to get ack from the stack";
        return -1;
    }
    LOG(INFO) << __func__ << ": success!";
    return 1;
}

//audio_stream_get_supported_latency_modes_api
std::vector<AudioLatencyMode> AudioStreamHandler::getSupportedLatencyModes(SessionType type) {
    std::vector<AudioLatencyMode> latencyModes;
    auto &stream = getStream(type);
    LOG(INFO) << __func__ << ": session: " << toString(type);

    if (!stream.session.ready) {
        LOG(ERROR) << __func__ <<  ": session is not ready";
        return latencyModes;
    }

    if (type != SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH) {
        LOG(ERROR) << __func__ << ": only A2DP sessions are supported";
        return latencyModes;
    }

    std::vector<LatencyMode> modes = BluetoothAudioSessionControl::GetSupportedLatencyModes(type);
    latencyModes.reserve(latencyModes.size());

    for (auto mode : modes) {
        AudioLatencyMode latencyMode;
        switch (mode) {
            case LatencyMode::LOW_LATENCY:
                latencyMode = AudioLatencyMode::LOW;
                break;
            case LatencyMode::FREE:
                latencyMode = AudioLatencyMode::FREE;
                break;
            case LatencyMode::DYNAMIC_SPATIAL_AUDIO_SOFTWARE:
                latencyMode = AudioLatencyMode::DYNAMIC_SPATIAL_AUDIO_SOFTWARE;
                break;
            case LatencyMode::DYNAMIC_SPATIAL_AUDIO_HARDWARE:
                latencyMode = AudioLatencyMode::DYNAMIC_SPATIAL_AUDIO_HARDWARE;
                break;
            default:
                LOG(WARNING) << ": Unknown latency mode: " << toString(mode);
                continue;
        }
        latencyModes.push_back(latencyMode);
        LOG(DEBUG) << __func__ << ": Successfully converted Bluetooth latency mode " << toString(mode) << " to media mode " << toString(latencyMode);
    }

    if (latencyModes.empty()) {
        latencyModes.push_back(AudioLatencyMode::FREE);
    }

    return latencyModes;
}

//audio_stream_set_latency_mode_api
int AudioStreamHandler::setLatencyMode(SessionType type, AudioLatencyMode mode) {
    auto &stream = getStream(type);
    LOG(INFO) << __func__ << ": session: " << toString(type);

    if (!stream.session.ready) {
        LOG(ERROR) << __func__ <<  ": session is not ready";
        return -1;
    }

    if (type != SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH) {
        LOG(ERROR) << __func__ << ": only A2DP sessions are supported";
        return -1;
    }

    LatencyMode latencyMode;

    switch (mode) {
        case AudioLatencyMode::LOW:
            latencyMode = LatencyMode::LOW_LATENCY;
            break;
        case AudioLatencyMode::FREE:
            latencyMode = LatencyMode::FREE;
            break;
        case AudioLatencyMode::DYNAMIC_SPATIAL_AUDIO_SOFTWARE:
            latencyMode = LatencyMode::DYNAMIC_SPATIAL_AUDIO_SOFTWARE;
            break;
        case AudioLatencyMode::DYNAMIC_SPATIAL_AUDIO_HARDWARE:
            latencyMode = LatencyMode::DYNAMIC_SPATIAL_AUDIO_HARDWARE;
            break;
        default: /* should be unreachable */
            return -1;
    }

    BluetoothAudioSessionControl::SetLatencyMode(type, latencyMode);

    return 0;
}

//audio_get_a2dp_sink_latency
uint16_t AudioStreamHandler::getSinkLatency(SessionType type) {
    auto &stream = getStream(type);
    LOG(INFO) << __func__ << ": session: " << toString(type);

    if (stream.session.ready) {
        if (mAptxLexDelay != -1) {
            stream.sink_latency = mAptxLexDelay;
        } else {
            PresentationPosition remote_delay;
            BluetoothAudioSessionControl::GetPresentationPosition(type, remote_delay);
            stream.sink_latency = remote_delay.remoteDeviceAudioDelayNanos / 1000000; /* convert to milliseconds */
        }
    } else {
        stream.sink_latency = 0;
    }

    LOG(INFO) << __func__ << ": sink latency: " << stream.sink_latency;

    return stream.sink_latency;
}

//audio_handoff_triggered
void AudioStreamHandler::handoffTriggered() {
    LOG(INFO) << __func__;

    auto &stream = getStream(SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);

    if (stream.state != AudioState::STOPPED || stream.state != AudioState::STOPPING) {
        stream.state = AudioState::STOPPED;
    }
}

//clear_a2dpsuspend_flag
void AudioStreamHandler::clearSuspendFlag() {
    LOG(INFO) << __func__;

    getStream(SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH).clearSuspendFlag();
    getStream(SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH).clearSuspendFlag();
    getStream(SessionType::LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH).clearSuspendFlag();
}

//update_metadata
void AudioStreamHandler::updateMetadata(SessionType type, void *metadata) {
    LOG(DEBUG) << __func__ << ": session: " << toString(type);

    switch (type) {
        case SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
        case SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
        case SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH:
            LOG(DEBUG) << __func__ << ": session: " << toString(type) << ", source metadata, session ready: " << BluetoothAudioSessionControl::IsSessionReady(type);
            BluetoothAudioSessionControl::UpdateSourceMetadata(type, *reinterpret_cast<source_metadata *>(metadata));
            break;
        case SessionType::LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH:
            LOG(DEBUG) << __func__ << ": session: " << toString(type) << ", sink metadata, session ready: " << BluetoothAudioSessionControl::IsSessionReady(type);
            BluetoothAudioSessionControl::UpdateSinkMetadata(type, *reinterpret_cast<sink_metadata *>(metadata));
            break;
        default:
            break;
    }
}

//wait_for_stack_response
void AudioStreamHandler::waitForStackResponse(std::chrono::milliseconds timeout, SessionType type) {
    LOG(DEBUG) << __func__;
    auto &stream = getStream(type);
    std::unique_lock<std::mutex> lk(stream.ack.lock);

    if (!stream.session.ready) {
        LOG(ERROR) << __func__ << ": stack deinitialized";
    }

    LOG(WARNING) << __func__ << ": conditional wait: ack_recvd: " << stream.ack.recvd;

    if (!stream.ack.recvd) {
        stream.ack.cond.wait_for(lk, timeout);
    }

    if (stream.ack.recvd) {
        LOG(DEBUG) << __func__ << ": ack received";
    }
}

const AudioConfiguration &AudioStreamHandler::getStreamConfiguration(SessionType type) {
    auto *config = getStream(type).getConfig();
    CHECK(config != nullptr);
    return *config;
}

}
