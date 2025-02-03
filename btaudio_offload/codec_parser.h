/* Copyright (C) 2025 anonymix007
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Note: this code is inspired by bthost_ipc, but it is mostly reimplemented from scratch based on PAL expectations
 */

#pragma once

#include <utility>

#include <aidl/android/hardware/bluetooth/audio/AudioConfiguration.h>

#include <aidl/vendor/qti/hardware/bluetooth/audio/LeAudioVendorConfiguration.h>
#include <aidl/vendor/qti/hardware/bluetooth/audio/VendorCodecType.h>

#include <system/audio.h>

#include <bt_intf.h>
#include <bt_aptx.h>
#include <bt_ble.h>
#include <bt_bundle.h>

namespace aidl::android::hardware::bluetooth::audio {

constexpr inline uint32_t fromU8x4(const uint8_t *ptr) {
    return (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | (ptr[3]);
}

using aidl::vendor::qti::hardware::bluetooth::audio::LeAudioVendorConfiguration;
using aidl::vendor::qti::hardware::bluetooth::audio::VendorCodecType;

class CodecParser {
public:
  /* extraConfig contains encoding configurations for the LE Audio decoding sessions */
  std::pair<codec_format_t, void *> parseAudioConfiguration(const AudioConfiguration &config, std::optional<const AudioConfiguration> encoderConfig = std::nullopt);
  std::optional<int> parseAptxAdaptiveR4Delay(const AudioConfiguration &config);
private:
  template <typename T>
  codec_format_t parseLeAudioConfiguration(const LeAudioConfiguration &leAudioConfig, T &outConfig);

  void parseLeAudioBroadcastConfiguration(const LeAudioBroadcastConfiguration &config);

  lc3_stream_map_t *parseLeAudioStreamMapConfiguration(const LeAudioConfiguration &leAudioConfig, int direction);

  /* aptX Adaptive R4 / LEX (XPAN) */
  void parseAptxAdaptiveR4Configuration(const LeAudioVendorConfiguration &leVendorConfig, lc3_cfg_t &outConfig);

  inline void parseAptxAdaptiveR4Configuration(const LeAudioVendorConfiguration &leVendorConfig, lc3_encoder_cfg_t &outConfig) {
      parseAptxAdaptiveR4Configuration(leVendorConfig, outConfig.toAirConfig);
  }

  inline void parseAptxAdaptiveR4Configuration(const LeAudioVendorConfiguration &leVendorConfig, lc3_decoder_cfg_t &outConfig) {
      parseAptxAdaptiveR4Configuration(leVendorConfig, outConfig.fromAirConfig);
  }

  /* LC3Q and aptX Adaptive R3 / LE */
  void parseLeAudioVendorConfiguration(const LeAudioVendorConfiguration &leVendorConfig, lc3_cfg_t &outConfig);
  void parseLeAudioVendorConfiguration(const LeAudioVendorConfiguration &leVendorConfig, lc3_encoder_cfg_t &outConfig);
  void parseLeAudioVendorConfiguration(const LeAudioVendorConfiguration &leVendorConfig, lc3_decoder_cfg_t &outConfig);

  void parseLc3Configuration(const LeAudioCodecConfiguration &leAudioCodecConfig, lc3_cfg_t &outConfig);
  void parseLc3Configuration(const LeAudioCodecConfiguration &leAudioCodecConfig, lc3_encoder_cfg_t &outConfig);
  void parseLc3Configuration(const LeAudioCodecConfiguration &leAudioCodecConfig, lc3_decoder_cfg_t &outConfig);

  void parseSbcConfiguration(const CodecConfiguration &codecConfig);
  void parseAacConfiguration(const CodecConfiguration &codecConfig);

  template <typename T>
  void parseAptxConfiguration(const CodecConfiguration &codecConfig, T &outConfig);

  inline void parseAptxClassicConfiguration(const CodecConfiguration &codecConfig) {
      parseAptxConfiguration(codecConfig, mAptxConfig);
  }

  inline void parseAptxHdConfiguration(const CodecConfiguration &codecConfig) {
      parseAptxConfiguration(codecConfig, mAptxHdConfig);
  }

  void parseAptxAdaptiveConfiguration(const CodecConfiguration &codecConfig);
  void parseLdacConfiguration(const CodecConfiguration &codecConfig);

  /* A2DP codecs */
  audio_sbc_encoder_config_t mSbcConfig = {};
  audio_aac_encoder_config_t mAacConfig = {};

  /* A2DP Vendor codecs */
  audio_aptx_encoder_config_t    mAptxConfig = {};
  audio_aptx_hd_encoder_config_t mAptxHdConfig = {};
  audio_aptx_ad_encoder_config_t mAptxAdConfig = {};
  audio_ldac_encoder_config_t    mLdacConfig = {};

  /* LE Audio codecs */
  audio_lc3_codec_cfg_t mLc3EncConfig = {};
  audio_lc3_codec_cfg_t mLc3DecConfig = {};
  audio_lc3_codec_cfg_t mLc3BroadcastConfig = {};

  /* Internal */
  aac_frame_size_control_t mAacVbrConfig = {};
  aac_abr_control_t mAacAbrConfig = {};
};

}
