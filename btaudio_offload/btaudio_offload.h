/* Copyright (C) 2025 anonymix007
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Note: this code is inspired by bthost_ipc, but it is mostly reimplemented from scratch based on PAL expectations
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <system/audio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SESSION_UNKNOWN,
    A2DP_SOFTWARE_ENCODING_DATAPATH,
    A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH,
    HEARING_AID_SOFTWARE_ENCODING_DATAPATH,
    LE_AUDIO_SOFTWARE_ENCODING_DATAPATH,
    LE_AUDIO_SOFTWARE_DECODED_DATAPATH,
    LE_AUDIO_HARDWARE_OFFLOAD_ENCODING_DATAPATH,
    LE_AUDIO_HARDWARE_OFFLOAD_DECODING_DATAPATH,
    LE_AUDIO_BROADCAST_SOFTWARE_ENCODING_DATAPATH,
    LE_AUDIO_BROADCAST_HARDWARE_OFFLOAD_ENCODING_DATAPATH,
} tSESSION_TYPE;

typedef enum {
    CTRL_ACK_SUCCESS,
    CTRL_ACK_UNSUPPORTED,
    CTRL_ACK_FAILURE,
    CTRL_ACK_PENDING,
    CTRL_ACK_INCALL_FAILURE,
    CTRL_ACK_DISCONNECT_IN_PROGRESS,
    CTRL_SKT_DISCONNECTED,
    CTRL_ACK_UNKNOWN,
    CTRL_ACK_RECONFIGURATION,
} tCTRL_ACK;

void bt_audio_pre_init(void);

int audio_stream_open_api(tSESSION_TYPE session_type);
int audio_stream_close_api(tSESSION_TYPE session_type);

int audio_stream_get_supported_latency_modes_api(tSESSION_TYPE session_type, size_t *num_modes, size_t max_num_modes, uint32_t *modes);
int audio_stream_set_latency_mode_api(tSESSION_TYPE session_type, uint32_t mode);

int audio_start_stream_api(tSESSION_TYPE session_type);
int audio_stop_stream_api(tSESSION_TYPE session_type);

int audio_suspend_stream_api(tSESSION_TYPE session_type);
void *audio_get_codec_config_api(tSESSION_TYPE session_type, uint8_t *multicast_status, uint8_t *num_dev, audio_format_t *codec_format);
int audio_check_a2dp_ready_api(tSESSION_TYPE session_type);

void audio_handoff_triggered(void);

void clear_a2dpsuspend_flag(void);

bool audio_is_scrambling_enabled(void);
void update_metadata(tSESSION_TYPE session_type, void *metadata);

int audio_start_stream(void);
int audio_stop_stream(void);
int audio_suspend_stream(void);

int audio_sink_start_stream(void);
int audio_sink_stop_stream(void);
int audio_sink_suspend_stream(void);

int audio_sink_stream_start(void);
int audio_sink_stream_stop(void);
int audio_sink_stream_suspend(void);

int audio_stream_open(void);
int audio_stream_close(void);
int audio_stream_start(void);
int audio_stream_stop(void);
int audio_stream_suspend(void);

void* audio_get_codec_config(uint8_t *multicast_status, uint8_t *num_dev, audio_format_t *codec_format);
int audio_check_a2dp_ready(void);

uint16_t audio_get_a2dp_sink_latency(void);
uint16_t audio_sink_get_a2dp_latency(void);
uint16_t audio_sink_get_a2dp_latency_api(tSESSION_TYPE session_type);

typedef int (*reconfig_cb_t)(tSESSION_TYPE session_type, int status);

void register_reconfig_cb(reconfig_cb_t cb);
void unregister_reconfig_cb(reconfig_cb_t cb);

#ifdef __cplusplus
}
#endif
