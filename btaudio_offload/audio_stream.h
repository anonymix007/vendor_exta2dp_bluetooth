/* Copyright (C) 2025 anonymix007
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Note: this code is inspired by bthost_ipc, but it is mostly reimplemented from scratch based on PAL expectations
 */

#pragma once

#include <array>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

#include <aidl/android/hardware/bluetooth/audio/AudioConfiguration.h>
#include <aidl/android/hardware/bluetooth/audio/BluetoothAudioStatus.h>
#include <aidl/android/hardware/bluetooth/audio/SessionType.h>

#include <aidl/android/media/audio/common/AudioLatencyMode.h>

#include "btaudio_offload.h"

using aidl::android::media::audio::common::AudioLatencyMode;

namespace aidl::android::hardware::bluetooth::audio {

enum AudioState {
    STANDBY,
    STARTING,
    STARTED,
    STOPPING,
    STOPPED,
    SUSPENDED,
};

inline std::string toString(AudioState state) {
    switch (state) {
        case AudioState::STANDBY:   return "STANDBY";
        case AudioState::STARTING:  return "STARTING";
        case AudioState::STARTED:   return "STARTED";
        case AudioState::STOPPING:  return "STOPPING";
        case AudioState::STOPPED:   return "STOPPED";
        case AudioState::SUSPENDED: return "SUSPENDED";
        default:                    return "UNKNOWN";
    }
}

enum ControlStatus {
    SUCCESS,
    UNSUPPORTED,
    FAILURE,
    PENDING,
    INCALL_FAILURE,
    DISCONNECT_IN_PROGRESS,
    DISCONNECTED,
    UNKNOWN,
    RECONFIGURATION,
};

inline std::string toString(ControlStatus status) {
    switch (status) {
        case ControlStatus::SUCCESS:                return "SUCCESS";
        case ControlStatus::UNSUPPORTED:            return "UNSUPPORTED";
        case ControlStatus::FAILURE:                return "FAILURE";
        case ControlStatus::PENDING:                return "PENDING";
        case ControlStatus::INCALL_FAILURE:         return "INCALL_FAILURE";
        case ControlStatus::DISCONNECT_IN_PROGRESS: return "DISCONNECT_IN_PROGRESS";
        case ControlStatus::DISCONNECTED:           return "DISCONNECTED";
        case ControlStatus::UNKNOWN:                return "UNKNOWN";
        case ControlStatus::RECONFIGURATION:        return "RECONFIGURATION";
        default:                                    return "UNKNOWN";
    }
}

inline ControlStatus aidl2ControlStatus(BluetoothAudioStatus status) {
    switch (status) {
        case BluetoothAudioStatus::UNKNOWN:
            return ControlStatus::UNKNOWN;
        case BluetoothAudioStatus::SUCCESS:
            return ControlStatus::SUCCESS;
        case BluetoothAudioStatus::UNSUPPORTED_CODEC_CONFIGURATION:
            return ControlStatus::UNSUPPORTED;
        case BluetoothAudioStatus::FAILURE:
            return ControlStatus::FAILURE;
        case BluetoothAudioStatus::RECONFIGURATION:
            return ControlStatus::RECONFIGURATION;
        default:
            return ControlStatus::UNKNOWN;
    }
}

struct SessionReconfig {
  SessionType session;
  uint8_t status;
};

struct AudioStreamCommon {
    struct {
        std::mutex lock;
        uint8_t recvd;
        std::condition_variable cond;
        ControlStatus status;
    } ack;

    AudioState state;
    bool reconfig_pending;

    uint8_t multicast;
    uint8_t num_conn_dev;

    AudioConfiguration enc;
    AudioConfiguration dec;

    uint16_t sink_latency;
    uint16_t ctrl_key;

    struct {
        bool ready;
        SessionType type;
    } session;

    void init(SessionType type);
    void updateStreamCodecConfig();
    void clearSuspendFlag();
    const AudioConfiguration *getConfig();
};

class AudioStreamHandler {
public:
  AudioStreamHandler() : mConfigThread(&AudioStreamHandler::handleChanges, this) {}

  inline bool isRunning() { return mConfigThread.joinable(); }
  inline void start() { mConfigThread.detach(); }

  int openAudioStream(SessionType type);
  int startAudioStream(SessionType type);
  int checkAudioStream(SessionType type);
  int suspendAudioStream(SessionType type);
  int stopAudioStream(SessionType type);
  int closeAudioStream(SessionType type);

  std::vector<AudioLatencyMode> getSupportedLatencyModes(SessionType type);
  int setLatencyMode(SessionType type, AudioLatencyMode mode);

  uint16_t getSinkLatency(SessionType type);

  void handoffTriggered();
  void clearSuspendFlag();

  void updateMetadata(SessionType type, void *metadata);

  void registerReconfigureCallback(reconfig_cb_t cb) { mCb = cb; }
  void unregisterReconfigureCallback(reconfig_cb_t /*cb*/) { mCb = nullptr; }

  const AudioConfiguration &getStreamConfiguration(SessionType type);

  inline std::optional<const AudioConfiguration> getStreamEncoderConfiguration(SessionType type) {
      if (type == SessionType::LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH) {
          return getStreamConfiguration(SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
      } else {
          return std::nullopt;
      }
  }

  /* call with -1 if current codec is not aptX Adaptive R4 aka LEX */
  void setAptxLexDelay(int delay) { mAptxLexDelay = delay; }
private:
  std::array<AudioStreamCommon, std::to_underlying(SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH) + 1> mStreams;

  int mAptxLexDelay;

  reconfig_cb_t mCb = nullptr;
  std::thread mConfigThread;
  std::mutex mConfigMutex;
  std::condition_variable mConfigCond;
  std::vector<SessionReconfig> mReconfigQueue;

  inline AudioStreamCommon &getStream(SessionType type) { return mStreams[std::to_underlying(type)]; }
  inline SessionType getSession(uint16_t control);

  void initOffloadPort(SessionType type);
  void deinitOffloadPort(SessionType type, bool unregister);

  void waitForStackResponse(std::chrono::milliseconds timeout, SessionType type);

  void handleChanges();

  void handleControlResult(uint16_t cookie, bool start_resp, BluetoothAudioStatus status);
  void handleSessionChange(uint16_t cookie);
  void handleAudioConfigChange(uint16_t cookie);
};

}
