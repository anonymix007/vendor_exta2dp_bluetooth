/* Copyright (C) 2025 anonymix007
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Note: this code is inspired by bthost_ipc, but it is mostly reimplemented from scratch based on PAL expectations
 */

#define LOG_TAG "BTAudioOffloadAIDL"
#include <android-base/logging.h>
#include <android-base/properties.h>

#include "btaudio_offload.h"
#include "audio_stream.h"
#include "codec_parser.h"

extern "C" binder_status_t createIBluetoothAudioProviderFactory();

namespace aidl::android::hardware::bluetooth::audio {

static_assert(SESSION_UNKNOWN == (int8_t) SessionType::UNKNOWN);
static_assert(A2DP_SOFTWARE_ENCODING_DATAPATH == (uint8_t) SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH);
static_assert(A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH == (uint8_t) SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
static_assert(HEARING_AID_SOFTWARE_ENCODING_DATAPATH == (uint8_t) SessionType::HEARING_AID_SOFTWARE_ENCODING_DATAPATH);
static_assert(LE_AUDIO_SOFTWARE_ENCODING_DATAPATH == (uint8_t) SessionType::LE_AUDIO_SOFTWARE_ENCODING_DATAPATH);
static_assert(LE_AUDIO_SOFTWARE_DECODED_DATAPATH == (uint8_t) SessionType::LE_AUDIO_SOFTWARE_DECODING_DATAPATH);
static_assert(LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH == (uint8_t) SessionType::LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
static_assert(LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH == (uint8_t) SessionType::LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH);
static_assert(LE_AUDIO_BROADCAST_SOFTWARE_ENCODING_DATAPATH == (uint8_t) SessionType::LE_AUDIO_BROADCAST_SOFTWARE_ENCODING_DATAPATH);
static_assert(LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH == (uint8_t) SessionType::LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH);

static_assert(CTRL_ACK_SUCCESS == ControlStatus::SUCCESS);
static_assert(CTRL_ACK_UNSUPPORTED == ControlStatus::UNSUPPORTED);
static_assert(CTRL_ACK_FAILURE == ControlStatus::FAILURE);
static_assert(CTRL_ACK_PENDING == ControlStatus::PENDING);
static_assert(CTRL_ACK_INCALL_FAILURE == ControlStatus::INCALL_FAILURE);
static_assert(CTRL_ACK_DISCONNECT_IN_PROGRESS == ControlStatus::DISCONNECT_IN_PROGRESS);
static_assert(CTRL_SKT_DISCONNECTED == ControlStatus::DISCONNECTED);
static_assert(CTRL_ACK_UNKNOWN == ControlStatus::UNKNOWN);
static_assert(CTRL_ACK_RECONFIGURATION == ControlStatus::RECONFIGURATION);

class BluetoothAudioOffload {
public:
  void preInit();

  int openAudioStream(SessionType type);
  int startAudioStream(SessionType type);
  int checkAudioStream(SessionType type);
  int suspendAudioStream(SessionType type);
  int stopAudioStream(SessionType type);
  int closeAudioStream(SessionType type);

  int getSupportedLatencyModes(SessionType type, size_t *num_modes, size_t max_num_modes, uint32_t *modes);
  int setLatencyMode(SessionType type, uint32_t mode);

  uint16_t getSinkLatency(SessionType type);

  void clearSuspendFlag();
  void handoffTriggered();

  void updateMetadata(SessionType type, void *metadata);

  void registerReconfigureCallback(reconfig_cb_t cb);
  void unregisterReconfigureCallback(reconfig_cb_t cb);

  void *getCodecConfig(SessionType type, uint8_t */* mcast */, uint8_t */* num_dev */, audio_format_t *format);
private:
  std::mutex mApiLock;
  AudioStreamHandler mAudioHandler;
  CodecParser mParser;
};

void BluetoothAudioOffload::preInit() {
    std::lock_guard<std::mutex> guard(mApiLock);
    createIBluetoothAudioProviderFactory();
    if (!mAudioHandler.isRunning()) {
        mAudioHandler.start();
        LOG(DEBUG) << "config handler thread started";
    } else {
        LOG(WARNING) << "config handler thread already exists";
    }
}

int BluetoothAudioOffload::openAudioStream(SessionType type) {
    std::lock_guard<std::mutex> guard(mApiLock);
    return mAudioHandler.openAudioStream(type);
}

int BluetoothAudioOffload::startAudioStream(SessionType type) {
    std::lock_guard<std::mutex> guard(mApiLock);
    return mAudioHandler.startAudioStream(type);
}

int BluetoothAudioOffload::checkAudioStream(SessionType type) {
    std::lock_guard<std::mutex> guard(mApiLock);
    return mAudioHandler.checkAudioStream(type);
}

int BluetoothAudioOffload::suspendAudioStream(SessionType type) {
    std::lock_guard<std::mutex> guard(mApiLock);
    return mAudioHandler.suspendAudioStream(type);
}

int BluetoothAudioOffload::stopAudioStream(SessionType type) {
    std::lock_guard<std::mutex> guard(mApiLock);
    return mAudioHandler.stopAudioStream(type);
}

int BluetoothAudioOffload::closeAudioStream(SessionType type) {
    std::lock_guard<std::mutex> guard(mApiLock);
    return mAudioHandler.closeAudioStream(type);
}

uint16_t BluetoothAudioOffload::getSinkLatency(SessionType type) {
    std::lock_guard<std::mutex> guard(mApiLock);
    return mAudioHandler.getSinkLatency(type);
}

void BluetoothAudioOffload::handoffTriggered() {
    std::lock_guard<std::mutex> guard(mApiLock);
    return mAudioHandler.handoffTriggered();
}

void BluetoothAudioOffload::clearSuspendFlag() {
    std::lock_guard<std::mutex> guard(mApiLock);
    return mAudioHandler.clearSuspendFlag();
}

void BluetoothAudioOffload::registerReconfigureCallback(reconfig_cb_t cb) {
    std::lock_guard<std::mutex> guard(mApiLock);
    mAudioHandler.registerReconfigureCallback(cb);
}

void BluetoothAudioOffload::unregisterReconfigureCallback(reconfig_cb_t cb) {
    std::lock_guard<std::mutex> guard(mApiLock);
    mAudioHandler.unregisterReconfigureCallback(cb);
}

void BluetoothAudioOffload::updateMetadata(SessionType type, void *metadata) {
    std::lock_guard<std::mutex> guard(mApiLock);
    mAudioHandler.updateMetadata(type, metadata);
}

int BluetoothAudioOffload::getSupportedLatencyModes(SessionType type, size_t *num_modes, size_t max_num_modes, uint32_t *modes) {
    std::lock_guard<std::mutex> guard(mApiLock);
    std::vector<AudioLatencyMode> latencyModes = mAudioHandler.getSupportedLatencyModes(type);
    if (latencyModes.empty()) {
        return -1;
    }

    const size_t n = std::min(max_num_modes, latencyModes.size());
    for (size_t i = 0; i < n; i++) {
        /* These are either AudioLatencyMode or audio_latency_mode_t for AIDL and HIDL Audio HALs respectively, but values are the same */
        modes[i] = std::to_underlying(latencyModes[i]);
    }
    *num_modes = n;

    return 0;
}

int BluetoothAudioOffload::setLatencyMode(SessionType type, uint32_t mode) {
    std::lock_guard<std::mutex> guard(mApiLock);
    switch (mode) {
        case std::to_underlying(AudioLatencyMode::LOW):
        case std::to_underlying(AudioLatencyMode::FREE):
        case std::to_underlying(AudioLatencyMode::DYNAMIC_SPATIAL_AUDIO_SOFTWARE):
        case std::to_underlying(AudioLatencyMode::DYNAMIC_SPATIAL_AUDIO_HARDWARE):
            return mAudioHandler.setLatencyMode(type, static_cast<AudioLatencyMode>(mode));
        default:
            LOG(ERROR) << __func__ << ": Unknown latency mode " << mode;
            return -1;
    }
}

void *BluetoothAudioOffload::getCodecConfig(SessionType type, uint8_t */* mcast */, uint8_t */* num_dev */, audio_format_t *format) {
    std::lock_guard<std::mutex> guard(mApiLock);

    const auto &aidlConf = mAudioHandler.getStreamConfiguration(type);
    const auto &extraConf = mAudioHandler.getStreamEncoderConfiguration(type);
    const auto &[fmt, conf] = mParser.parseAudioConfiguration(aidlConf, extraConf);

    if (fmt == CODEC_TYPE_APTX_AD_R4) {
        auto delay = mParser.parseAptxAdaptiveR4Delay(aidlConf);
        if (delay.has_value()) {
            mAudioHandler.setAptxLexDelay(delay.value());
        } else {
            LOG(ERROR) << __func__ << ": failed to parse aptX Adaptive R4 delay";
            mAudioHandler.setAptxLexDelay(-1);
        }
    } else {
        mAudioHandler.setAptxLexDelay(-1);
    }

    *format = static_cast<audio_format_t>(fmt);
    return conf;
}

}

static aidl::android::hardware::bluetooth::audio::BluetoothAudioOffload sBtOffload;

using aidl::android::hardware::bluetooth::audio::SessionType;

extern "C" void bt_audio_pre_init(void) {
    sBtOffload.preInit();
}

/* New LE Audio-aware versions */

extern "C" int audio_stream_open_api(tSESSION_TYPE session_type) {
    return sBtOffload.openAudioStream(static_cast<SessionType>(session_type));
}

extern "C" int audio_start_stream_api(tSESSION_TYPE session_type) {
    return sBtOffload.startAudioStream(static_cast<SessionType>(session_type));
}

extern "C" int audio_check_a2dp_ready_api(tSESSION_TYPE session_type) {
    return sBtOffload.checkAudioStream(static_cast<SessionType>(session_type));
}

extern "C" void *audio_get_codec_config_api(tSESSION_TYPE session_type, uint8_t *mcast, uint8_t *num_dev, audio_format_t *type) {
    return sBtOffload.getCodecConfig(static_cast<SessionType>(session_type), mcast, num_dev, type);
}

extern "C" int audio_suspend_stream_api(const tSESSION_TYPE session_type) {
    return sBtOffload.suspendAudioStream(static_cast<SessionType>(session_type));
}

extern "C" int audio_stop_stream_api(const tSESSION_TYPE session_type) {
    return sBtOffload.stopAudioStream(static_cast<SessionType>(session_type));
}

extern "C" int audio_stream_close_api(const tSESSION_TYPE session_type) {
    return sBtOffload.closeAudioStream(static_cast<SessionType>(session_type));
}

extern "C" uint16_t audio_sink_get_a2dp_latency_api(tSESSION_TYPE session_type) {
    return sBtOffload.getSinkLatency(static_cast<SessionType>(session_type));
}

/* Old A2DP-only versions */

extern "C" int audio_stream_open(void) {
    return audio_stream_open_api(A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
}

extern "C" int audio_start_stream(void) {
    return audio_start_stream_api(A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
}

extern "C" int audio_stream_start(void) {
    return audio_start_stream();
}

extern "C" int audio_check_a2dp_ready(void) {
    return audio_check_a2dp_ready_api(A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
}

extern "C" void *audio_get_codec_config(uint8_t *mcast, uint8_t *num_dev, audio_format_t *format) {
    return audio_get_codec_config_api(A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH, mcast, num_dev, format);
}

extern "C" int audio_stream_suspend(void) {
    return audio_suspend_stream_api(A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
}

extern "C" int audio_suspend_stream(void) {
    return audio_stream_suspend();
}

extern "C" int audio_stream_stop(void) {
    return audio_stop_stream_api(A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
}

extern "C" int audio_stop_stream(void) {
    return audio_stream_stop();
}

extern "C" int audio_stream_close(void) {
    return audio_stream_close_api(A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
}

extern "C" uint16_t audio_sink_get_a2dp_latency(void) {
    return audio_sink_get_a2dp_latency_api(A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH);
}

extern "C" uint16_t audio_get_a2dp_sink_latency(void) {
    return audio_sink_get_a2dp_latency();
}

/* Callbacks */

extern "C" void register_reconfig_cb(reconfig_cb_t cb) {
    sBtOffload.registerReconfigureCallback(cb);
}

extern "C" void unregister_reconfig_cb(reconfig_cb_t cb) {
    sBtOffload.unregisterReconfigureCallback(cb);
}

/* Misc */

extern "C" void audio_handoff_triggered(void) {
    sBtOffload.handoffTriggered();
}

extern "C" void clear_a2dpsuspend_flag(void) {
    sBtOffload.clearSuspendFlag();
}

extern "C" bool audio_is_scrambling_enabled(void) {
    bool enabled = android::base::GetBoolProperty("persist.vendor.qcom.bluetooth.scram.enabled", false);
    LOG(DEBUG) << __func__ << ": Scrambling is " << (enabled ? "enabled" : "disabled");
    return enabled;
}

/* Metadata and latency */

extern "C" void update_metadata(tSESSION_TYPE session_type, void *metadata) {
    sBtOffload.updateMetadata(static_cast<SessionType>(session_type), metadata);
}

extern "C" int audio_stream_get_supported_latency_modes_api(tSESSION_TYPE session_type, size_t *num_modes, size_t max_num_modes, uint32_t *modes) {
    return sBtOffload.getSupportedLatencyModes(static_cast<SessionType>(session_type), num_modes, max_num_modes, modes);
}

extern "C" int audio_stream_set_latency_mode_api(tSESSION_TYPE session_type, uint32_t mode) {
    return sBtOffload.setLatencyMode(static_cast<SessionType>(session_type), mode);
}

/* Unimplemented sink (decoder) functions */

extern "C" int audio_sink_stream_start(void) {
    return -1;
}

extern "C" int audio_sink_stream_stop(void) {
    return -1;
}

extern "C" int audio_sink_stream_suspend(void) {
    return -1;
}

extern "C" int audio_sink_start_stream(void) {
    return audio_sink_stream_start();
}

extern "C" int audio_sink_stop_stream(void) {
    return audio_sink_stream_stop();
}

extern "C" int audio_sink_suspend_stream(void) {
    return audio_sink_stream_suspend();
}
