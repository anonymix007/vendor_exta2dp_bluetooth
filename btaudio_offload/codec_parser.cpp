/* Copyright (C) 2025 anonymix007
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Note: this code is inspired by bthost_ipc, but it is mostly reimplemented from scratch based on PAL expectations
 */

#define LOG_TAG "BTAudioOffloadParser"
#include <android-base/logging.h>
#include <android-base/properties.h>

#include "codec_parser.h"

namespace aidl::android::hardware::bluetooth::audio {

//void CodecParser::parseLeAudioConfiguration(const LeAudioConfiguration &leConfig);

void CodecParser::parseSbcConfiguration(const CodecConfiguration &codecConfig) {
    memset(&mSbcConfig, 0, sizeof(mSbcConfig));
    auto &sbcConfig = codecConfig.config.get<CodecConfiguration::CodecSpecific::sbcConfig>();

    switch (sbcConfig.sampleRateHz) {
        case 44100:
        case 48000:
            mSbcConfig.sampling_rate = sbcConfig.sampleRateHz;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid sample rate: " << sbcConfig.sampleRateHz;
            break;
    }

    switch (sbcConfig.channelMode) {
        case SbcChannelMode::MONO:
                mSbcConfig.channels = 0;
                break;
        case SbcChannelMode::DUAL:
                mSbcConfig.channels = 1;
                break;
        case SbcChannelMode::STEREO:
                mSbcConfig.channels = 2;
                break;
        case SbcChannelMode::JOINT_STEREO:
                mSbcConfig.channels = 3;
                break;
        default:
                LOG(ERROR) << __func__ << ": Invalid channel mode: " << toString(sbcConfig.channelMode);
                break;
    }

    switch (sbcConfig.blockLength) {
        case 4:
        case 8:
        case 12:
        case 16:
            mSbcConfig.blk_len = sbcConfig.blockLength;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid block length: " << sbcConfig.blockLength;
            break;
    }

    switch (sbcConfig.numSubbands) {
        case 4:
        case 8:
            mSbcConfig.subband = sbcConfig.numSubbands;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid number of subbands: " << sbcConfig.numSubbands;
            break;
    }

    // FIXME: Looks like the description in bt_bundle.h isn't correct, loudness is actually 1
    // bthost_ipc seems to use 1 and 2 here which isn't correct either
    switch (sbcConfig.allocMethod) {
        case SbcAllocMethod::ALLOC_MD_L:
            mSbcConfig.alloc = 1;
            break;
        case SbcAllocMethod::ALLOC_MD_S:
            mSbcConfig.alloc = 0;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid allocation method: " << toString(sbcConfig.allocMethod);
            break;
    }

    switch (sbcConfig.bitsPerSample) {
        case 16:
        case 24:
        case 32:
            mSbcConfig.bits_per_sample = sbcConfig.bitsPerSample;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid bits per sample: " << sbcConfig.bitsPerSample;
            break;
    }

    mSbcConfig.min_bitpool = sbcConfig.minBitpool;
    mSbcConfig.max_bitpool = sbcConfig.maxBitpool;
    LOG(DEBUG) << __func__ << ": bitpool: [" <<  mSbcConfig.min_bitpool << ", " << mSbcConfig.max_bitpool << "]";

    mSbcConfig.bitrate = codecConfig.encodedAudioBitrate;
    if (mSbcConfig.bitrate != std::clamp(mSbcConfig.bitrate, 1u, 0xFFFFFFu)) {
        mSbcConfig.bitrate = 328000;
    }

    LOG(INFO) << __func__ << ": in bitrate: " << codecConfig.encodedAudioBitrate << ", out bitrate: " << mSbcConfig.bitrate;
}

void CodecParser::parseAacConfiguration(const CodecConfiguration &codecConfig) {
    memset(&mAacConfig, 0, sizeof(mAacConfig));
    auto &aacConfig = codecConfig.config.get<CodecConfiguration::CodecSpecific::aacConfig>();

    mAacConfig.enc_mode = 0;      /*  AAC-LC  */
    mAacConfig.format_flag = 4;   /*   LATM   */

    switch (aacConfig.sampleRateHz) {
        case 44100:
        case 48000:
        case 88200:
        case 96000:
            mAacConfig.sampling_rate = aacConfig.sampleRateHz;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid sample rate: " << aacConfig.sampleRateHz;
            break;
    }

    switch (aacConfig.channelMode) {
        case ChannelMode::MONO:
            mAacConfig.channels = 1;
            break;
        case ChannelMode::STEREO:
            mAacConfig.channels = 2;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid channel mode: " << toString(aacConfig.channelMode);
            break;
    }

    LOG(INFO) << __func__ << ": AAC VBR " << (aacConfig.variableBitRateEnabled ? "enabled" : "disabled");
    mAacConfig.size_control_struct = 1;
    mAacConfig.frame_ctl_ptr = &mAacVbrConfig;
    mAacConfig.frame_ctl_ptr->ctl_type = BIT_RATE_MODE;
    mAacConfig.frame_ctl_ptr->ctl_value = !!aacConfig.variableBitRateEnabled;

    switch (aacConfig.bitsPerSample) {
        case 16:
        case 24:
        case 32:
            mAacConfig.bits_per_sample = aacConfig.bitsPerSample;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid bits per sample: " << aacConfig.bitsPerSample;
            break;
    }

    mAacConfig.frame_ctl.ctl_type = MTU_SIZE;
    mAacConfig.frame_ctl.ctl_value = codecConfig.peerMtu == 0 ? 1005 : std::min(codecConfig.peerMtu, 1005);
    LOG(INFO) << __func__ << ": MTU: " << mAacConfig.frame_ctl.ctl_value;

    mAacConfig.bitrate = codecConfig.encodedAudioBitrate;
    if (mAacConfig.bitrate != std::clamp(mAacConfig.bitrate, 1u, 0xFFFFFFu)) {
        mAacConfig.bitrate = 165000;
    }

#if 0 // TODO: do we need this for AOSP roms?
    mAacConfig.bitrate = std::min(mAacConfig.bitrate, (mtu - 12) * mAacConfig.sampling_rate / 128))
#endif

    LOG(INFO) << __func__ << ": in bitrate: " << codecConfig.encodedAudioBitrate << ", out bitrate: " << mAacConfig.bitrate;

    mAacConfig.abr_size_control_struct = 1;
    mAacConfig.abr_ctl_ptr = &mAacAbrConfig;

    mAacAbrConfig.is_abr_enabled = ::android::base::GetBoolProperty("persist.vendor.qcom.bluetooth.aac_abr_support", false);
    mAacAbrConfig.level_to_bitrate_map.num_levels = MAX_ABR_QUALITY_LEVELS;
    LOG(DEBUG) <<__func__ << ": Bitrate map:";
    for (size_t i = 0; i < MAX_ABR_QUALITY_LEVELS; i++) {
        mAacAbrConfig.level_to_bitrate_map.bit_rate_level_map[i].link_quality_level = i + 1;
        mAacAbrConfig.level_to_bitrate_map.bit_rate_level_map[i].bitrate = std::clamp((i + 1) * mAacConfig.bitrate / MAX_ABR_QUALITY_LEVELS, 32000lu, 165000lu);
        LOG(DEBUG) << __func__ << ": " << (i + 1) << " -> " << mAacAbrConfig.level_to_bitrate_map.bit_rate_level_map[i].bitrate;
    }
}

template <typename T>
void CodecParser::parseAptxConfiguration(const CodecConfiguration &codecConfig, T &outConfig) {
    memset(&outConfig, 0, sizeof(outConfig));
    auto &aptxConfig = codecConfig.config.get<CodecConfiguration::CodecSpecific::aptxConfig>();

    switch (aptxConfig.sampleRateHz) {
        case 44100:
        case 48000:
        case 88200:
        case 96000:
            outConfig.sampling_rate = aptxConfig.sampleRateHz;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid sample rate: " << aptxConfig.sampleRateHz;
            break;
    }

    switch (aptxConfig.channelMode) {
        case ChannelMode::MONO:
            outConfig.channels = 1;
            break;
        case ChannelMode::STEREO:
            outConfig.channels = 2;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid channel mode: " << toString(aptxConfig.channelMode);
            break;
    }

    switch (aptxConfig.bitsPerSample) {
        case 16:
        case 24:
        case 32:
            outConfig.bits_per_sample = aptxConfig.bitsPerSample;;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid bits per sample: " << aptxConfig.bitsPerSample;
            break;
    }

    outConfig.bitrate =  std::clamp(codecConfig.encodedAudioBitrate, 0, 0xFFFFFF);
    LOG(INFO) << __func__ << ": in bitrate: " << codecConfig.encodedAudioBitrate << ", out bitrate: " << outConfig.bitrate;
}

void CodecParser::parseAptxAdaptiveConfiguration(const CodecConfiguration &codecConfig) {
    memset(&mAptxAdConfig, 0, sizeof(mAptxAdConfig));
    auto &aptxAdaptiveConfig = codecConfig.config.get<CodecConfiguration::CodecSpecific::aptxAdaptiveConfig>();

    switch (aptxAdaptiveConfig.sampleRateHz) {
        case 44100:
            mAptxAdConfig.sampling_rate = APTX_AD_44_1;
            break;
        case 48000:
            mAptxAdConfig.sampling_rate = APTX_AD_48;
            break;
        case 96000:
            mAptxAdConfig.sampling_rate = APTX_AD_96;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid sample rate: " << aptxAdaptiveConfig.sampleRateHz;
            break;
    }

    switch (aptxAdaptiveConfig.channelMode) {
        case AptxAdaptiveChannelMode::MONO:
        case AptxAdaptiveChannelMode::TWS_STEREO:
        case AptxAdaptiveChannelMode::JOINT_STEREO:
        case AptxAdaptiveChannelMode::DUAL_MONO:
            mAptxAdConfig.channel_mode = static_cast<enc_aptx_ad_channel_mode>(aptxAdaptiveConfig.channelMode);
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid channel mode: " << toString(aptxAdaptiveConfig.channelMode);
            break;
    }

    switch (aptxAdaptiveConfig.bitsPerSample) {
        case 16:
        case 24:
        case 32:
            mAptxAdConfig.bits_per_sample = aptxAdaptiveConfig.bitsPerSample;;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid bits per sample: " << aptxAdaptiveConfig.bitsPerSample;
            break;
    }

    switch (aptxAdaptiveConfig.aptxMode) {
        case AptxMode::HIGH_QUALITY:
            mAptxAdConfig.encoder_mode = 0x1000;
            break;
        case AptxMode::LOW_LATENCY:
            mAptxAdConfig.encoder_mode = 0x2000;
            break;
        case AptxMode::ULTRA_LOW_LATENCY:
            mAptxAdConfig.encoder_mode = 0x4000;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid aptX AD mode: " << toString(aptxAdaptiveConfig.aptxMode);
            break;
    }

    mAptxAdConfig.input_mode = 0; /* aptX Adaptive TWS+ Stereo */

    mAptxAdConfig.min_sink_modeA = aptxAdaptiveConfig.sinkBufferingMs.minLowLatency;
    mAptxAdConfig.max_sink_modeA = aptxAdaptiveConfig.sinkBufferingMs.maxLowLatency;
    mAptxAdConfig.min_sink_modeB = aptxAdaptiveConfig.sinkBufferingMs.minHighQuality;
    mAptxAdConfig.max_sink_modeB = aptxAdaptiveConfig.sinkBufferingMs.maxHighQuality;
    mAptxAdConfig.min_sink_modeC = aptxAdaptiveConfig.sinkBufferingMs.minTws;
    mAptxAdConfig.max_sink_modeC = aptxAdaptiveConfig.sinkBufferingMs.maxTws;

    bool aptx_ad_r21 = ::android::base::GetBoolProperty("persist.vendor.qcom.bluetooth.aptxadaptiver2_1_support", false);

    LOG(INFO) << __func__ << ": aptX AD R2.1 " << (aptx_ad_r21 ? "supported" : "unsupported");

    if (aptx_ad_r21) {
        mAptxAdConfig.max_sink_modeA *= 2;
        mAptxAdConfig.max_sink_modeB *= 2;
        mAptxAdConfig.max_sink_modeC *= 2;
    }

    mAptxAdConfig.mtu = codecConfig.peerMtu;

    mAptxAdConfig.TTP_modeA_low = aptxAdaptiveConfig.ttp.lowLowLatency + 128;
    mAptxAdConfig.TTP_modeA_high = aptxAdaptiveConfig.ttp.highLowLatency + 128;
    mAptxAdConfig.TTP_modeB_low = aptxAdaptiveConfig.ttp.lowHighQuality + 128;
    mAptxAdConfig.TTP_modeB_high = aptxAdaptiveConfig.ttp.highHighQuality + 128;
    mAptxAdConfig.TTP_TWS_low = aptxAdaptiveConfig.ttp.lowTws + 128;
    mAptxAdConfig.TTP_TWS_high = aptxAdaptiveConfig.ttp.highTws + 128;
    mAptxAdConfig.fade_duration = aptxAdaptiveConfig.inputFadeDurationMs;

    CHECK(sizeof(mAptxAdConfig.sink_cap) == aptxAdaptiveConfig.aptxAdaptiveConfigStream.size());

    memcpy(mAptxAdConfig.sink_cap, aptxAdaptiveConfig.aptxAdaptiveConfigStream.data(), sizeof(mAptxAdConfig.sink_cap));

    if (aptx_ad_r21) {
        mAptxAdConfig.encoder_mode |= (mAptxAdConfig.sink_cap[0] << 8) | (mAptxAdConfig.sink_cap[1]);
    }

    LOG(DEBUG) << __func__ << ": aptX Adaptive sampleRate:" << mAptxAdConfig.sampling_rate;
    LOG(DEBUG) << __func__ << ": aptX Adaptive channelMode:" << mAptxAdConfig.channel_mode;
    LOG(DEBUG) << std::hex;
    LOG(DEBUG) << __func__ << ": aptX Adaptive min_sink_buffering_LL: " << mAptxAdConfig.min_sink_modeA;
    LOG(DEBUG) << __func__ << ": aptX Adaptive max_sink_buffering_LL: " << mAptxAdConfig.max_sink_modeA;
    LOG(DEBUG) << __func__ << ": aptX Adaptive min_sink_buffering_HQ: " << mAptxAdConfig.min_sink_modeB;
    LOG(DEBUG) << __func__ << ": aptX Adaptive max_sink_buffering_HQ: " << mAptxAdConfig.max_sink_modeB;
    LOG(DEBUG) << __func__ << ": aptX Adaptive min_sink_buffering_TWS: " << mAptxAdConfig.min_sink_modeC;
    LOG(DEBUG) << __func__ << ": aptX Adaptive max_sink_buffering_TWS: " << mAptxAdConfig.max_sink_modeC;
    LOG(DEBUG) << __func__ << ": aptX Adaptive ttp_ll_0: " << mAptxAdConfig.TTP_modeA_low;
    LOG(DEBUG) << __func__ << ": aptX Adaptive ttp_ll_1: " << mAptxAdConfig.TTP_modeA_high;
    LOG(DEBUG) << __func__ << ": aptX Adaptive ttp_hq_0: " << mAptxAdConfig.TTP_modeB_low;
    LOG(DEBUG) << __func__ << ": aptX Adaptive ttp_hq_1: " << mAptxAdConfig.TTP_modeB_high;
    LOG(DEBUG) << __func__ << ": aptX Adaptive ttp_tws_0: " << mAptxAdConfig.TTP_TWS_low;
    LOG(DEBUG) << __func__ << ": aptX Adaptive ttp_tws_1: " << mAptxAdConfig.TTP_TWS_high;
    LOG(DEBUG) << std::dec;
    LOG(DEBUG) << __func__ << ": aptX Adaptive MTU: " << mAptxAdConfig.mtu;
    LOG(DEBUG) << __func__ << ": aptX Adaptive Bits Per Sample: " << mAptxAdConfig.bits_per_sample;
    LOG(DEBUG) << __func__ << ": aptX Adaptive Mode: " << mAptxAdConfig.encoder_mode;
    LOG(DEBUG) << __func__ << ": aptX Adaptive Input mode: " << mAptxAdConfig.input_mode;
    LOG(DEBUG) << __func__ << ": aptX Adaptive input_fade_duration: " << mAptxAdConfig.fade_duration;
}

constexpr quality_level_to_bitrate_info kLdacBitrateMap44k1 = {
    .num_levels = MAX_ABR_QUALITY_LEVELS,
    .bit_rate_level_map = {
        {
            .link_quality_level = 5,
            .bitrate = 909000,
        },
        {
            .link_quality_level = 4,
            .bitrate = 606000,
        },
        {
            .link_quality_level = 3,
            .bitrate = 452000,
        },
        {
            .link_quality_level = 2,
            .bitrate = 363000,
        },
        {
            .link_quality_level = 1,
            .bitrate = 303000,
        },
    },
};

constexpr quality_level_to_bitrate_info kLdacBitrateMap48k = {
    .num_levels = MAX_ABR_QUALITY_LEVELS,
    .bit_rate_level_map = {
        {
            .link_quality_level = 5,
            .bitrate = 990000,
        },
        {
            .link_quality_level = 4,
            .bitrate = 660000,
        },
        {
            .link_quality_level = 3,
            .bitrate = 492000,
        },
        {
            .link_quality_level = 2,
            .bitrate = 396000,
        },
        {
            .link_quality_level = 1,
            .bitrate = 330000,
        },
    },
};

void CodecParser::parseLdacConfiguration(const CodecConfiguration &codecConfig) {
    memset(&mLdacConfig, 0, sizeof(mLdacConfig));
    auto &ldacConfig = codecConfig.config.get<CodecConfiguration::CodecSpecific::ldacConfig>();

    switch (ldacConfig.sampleRateHz) {
        case 44100:
        case 48000:
        case 88200:
        case 96000:
        case 176400:
        case 192000:
           mLdacConfig.sampling_rate = ldacConfig.sampleRateHz;
        default:
            LOG(ERROR) << __func__ << ": Invalid sample rate: " << ldacConfig.sampleRateHz;
            break;
    }

    switch (ldacConfig.channelMode) {
        case LdacChannelMode::STEREO:
            mLdacConfig.channel_mode = 1;
            break;
        case LdacChannelMode::DUAL:
            mLdacConfig.channel_mode = 2;
            break;
        case LdacChannelMode::MONO:
            mLdacConfig.channel_mode = 4;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid channel mode: " << toString(ldacConfig.channelMode);
            break;
    }

    LOG(DEBUG) << __func__ << ": Channel mode: " << mLdacConfig.channel_mode;

    mLdacConfig.mtu = codecConfig.peerMtu == 0 ? 663 : std::min(codecConfig.peerMtu, 663);
    mLdacConfig.bit_rate = std::clamp(codecConfig.encodedAudioBitrate, 0, 0xFFFFFF);

    switch (ldacConfig.bitsPerSample) {
        case 16:
        case 24:
        case 32:
            mLdacConfig.bits_per_sample = ldacConfig.bitsPerSample;
            break;
        default:
            LOG(ERROR) << __func__ << ": Invalid bits per sample: " << ldacConfig.bitsPerSample;
            break;
    }
    LOG(DEBUG) << __func__ << ": Bits per sample: " << mLdacConfig.bits_per_sample;

    mLdacConfig.is_abr_enabled = (ldacConfig.qualityIndex == LdacQualityIndex::ABR);
    LOG(DEBUG) << __func__ << ": LDAC is in " << (mLdacConfig.is_abr_enabled ? "ABR" : "CBR") << " mode";

    switch (ldacConfig.sampleRateHz) {
        case 44100:
        case 88200:
            mLdacConfig.level_to_bitrate_map = kLdacBitrateMap44k1;
            if (mLdacConfig.is_abr_enabled) {
                mLdacConfig.bit_rate = 909000;
            }
            break;
        case 48000:
        case 96000:
            mLdacConfig.level_to_bitrate_map = kLdacBitrateMap48k;
            if (mLdacConfig.is_abr_enabled) {
                mLdacConfig.bit_rate = 990000;
            }
            break;
    }

    LOG(DEBUG) << __func__ << ": bitrate: " << mLdacConfig.bit_rate;
}

void CodecParser::parseLc3Configuration(const LeAudioCodecConfiguration &leAudioCodecConfig, lc3_cfg_t &outConfig) {
    auto &lc3Config = leAudioCodecConfig.get<LeAudioCodecConfiguration::lc3Config>();

    outConfig.sampling_freq = lc3Config.samplingFrequencyHz;
    outConfig.max_octets_per_frame = lc3Config.octetsPerFrame;
    outConfig.frame_duration = lc3Config.frameDurationUs;
    outConfig.bit_depth = lc3Config.pcmBitDepth;
    outConfig.num_blocks = lc3Config.blocksPerSdu;

    /* LC3 doesn't have vendor-specific data */
    memset(outConfig.vendor_specific, 0, sizeof(outConfig.vendor_specific));
}

void CodecParser::parseLc3Configuration(const LeAudioCodecConfiguration &leAudioCodecConfig, lc3_encoder_cfg_t &outConfig) {
    parseLc3Configuration(leAudioCodecConfig, outConfig.toAirConfig);

    /* Encoder-specific values */
    outConfig.toAirConfig.mode = 1; // FIXME: should this be 0?
    outConfig.toAirConfig.api_version = 0x00000021;
    outConfig.toAirConfig.default_q_level = 0;
}

void CodecParser::parseLc3Configuration(const LeAudioCodecConfiguration &leAudioCodecConfig, lc3_decoder_cfg_t &outConfig) {
    parseLc3Configuration(leAudioCodecConfig, outConfig.fromAirConfig);

    /* Decoder-specific values */
    outConfig.fromAirConfig.mode = 0;
    outConfig.fromAirConfig.api_version = 0x00000021;
    outConfig.fromAirConfig.default_q_level = 1;
}

void CodecParser::parseLeAudioBroadcastConfiguration(const LeAudioBroadcastConfiguration &leAudioBroadcastConfig) {
    LOG(DEBUG) << __func__ << ": Broadcast session";

    if (mLc3BroadcastConfig.enc_cfg.streamMapOut != nullptr) {
        delete[] mLc3BroadcastConfig.enc_cfg.streamMapOut;
    }

    memset(&mLc3BroadcastConfig, 0, sizeof(mLc3BroadcastConfig));
    mLc3BroadcastConfig.is_enc_config_set = true;

    auto &lc3_enc_config = mLc3BroadcastConfig.enc_cfg;
    const auto &lc3StreamMap = leAudioBroadcastConfig.streamMap;

    parseLc3Configuration(lc3StreamMap[0].leAudioCodecConfig, lc3_enc_config.toAirConfig);

    /* Broadcast-specific values */
    lc3_enc_config.toAirConfig.mode = 2; // FIXME: should this be 1?
    lc3_enc_config.toAirConfig.api_version = 0x00000021;
    lc3_enc_config.toAirConfig.default_q_level = 3;

    lc3_enc_config.stream_map_size = lc3StreamMap.size();
    lc3_enc_config.streamMapOut = new lc3_stream_map_t[lc3_enc_config.stream_map_size];

    for (size_t i = 0; i < lc3_enc_config.stream_map_size; i++) {
        lc3_enc_config.streamMapOut[i].audio_location = lc3StreamMap[i].audioChannelAllocation;
        lc3_enc_config.streamMapOut[i].stream_id = lc3StreamMap[i].streamHandle;
        lc3_enc_config.streamMapOut[i].direction = 0;
    }

    LOG(DEBUG) << __func__ << ": Broadcast rate: " << lc3_enc_config.toAirConfig.sampling_freq;
    LOG(DEBUG) << __func__ << ": Broadcast octets: " << lc3_enc_config.toAirConfig.max_octets_per_frame;
    LOG(DEBUG) << __func__ << ": Broadcast frame duration: " << lc3_enc_config.toAirConfig.frame_duration;
    LOG(DEBUG) << __func__ << ": Broadcast num blocks: " << lc3_enc_config.toAirConfig.num_blocks;
}

/*audio_format_t CodecParser::parseAudioFormat(const AudioConfiguration &config) {
    switch (config.getTag()) {
        case AudioConfiguration::pcmConfig:
            return AUDIO_FORMAT_PCM;
        case AudioConfiguration::a2dpConfig: {
            auto &a2dpConfig = config.get<AudioConfiguration::a2dpConfig>();
            switch (a2dpConfig.codecType) {
                case CodecType::SBC:
                    return AUDIO_FORMAT_SBC;
                case CodecType::AAC:
                    return AUDIO_FORMAT_AAC;
                case CodecType::APTX:
                    return AUDIO_FORMAT_APTX;
                case CodecType::APTX_HD:
                    return AUDIO_FORMAT_APTX_HD;
                case CodecType::LDAC:
                    return AUDIO_FORMAT_LDAC;
                case CodecType::LC3:
                    return AUDIO_FORMAT_LC3;
                case CodecType::APTX_ADAPTIVE:
                    return AUDIO_FORMAT_APTX_ADAPTIVE;
                case CodecType::OPUS:
                    return AUDIO_FORMAT_OPUS;
                default:
                    LOG(ERROR) << __func__ << ": Unknown codec config: " << config.toString();
                    return AUDIO_FORMAT_INVALID;
            }
        }
        case AudioConfiguration::leAudioConfig: {
            auto &leAudioConfig = config.get<AudioConfiguration::leAudioConfig>();

            switch(leAudioConfig.codecType) {
                case CodecType::LC3:
                    return AUDIO_FORMAT_LC3;
                case CodecType::VENDOR: {
                    LOG(INFO) << __func__ << ": LE Audio vendor codec found";
                    std::optional<LeAudioVendorConfiguration> leVendorConfig;
                    leAudioConfig.leAudioCodecConfig.get<LeAudioCodecConfiguration::vendorConfig>().extension.getParcelable(&leVendorConfig);
                    LOG(INFO) << __func__ << ": LE Audio vendor config: " << leVendorConfig->toString();
                    switch (leVendorConfig->vendorCodecType) {
                        case VendorCodecType::LC3Q:
                            return AUDIO_FORMAT_LC3;
                        case VendorCodecType::APTX_ADAPTIVE_R3:
                            return VX_AUDIO_FORMAT_APTX_ADAPTIVE_QLEA;
                        case VendorCodecType::APTX_ADAPTIVE_R4:
                            return AUDIO_FORMAT_APTX_ADAPTIVE_R4;
                    }
                }
                default:
                    LOG(ERROR) << __func__ << ": Unknown codec config: " << config.toString();
                    return AUDIO_FORMAT_INVALID;
            }
        }
        case AudioConfiguration::leAudioBroadcastConfig:
            return AUDIO_FORMAT_LC3;
        default:
            LOG(ERROR) << __func__ << ": Unknown codec config: " << config.toString();
            return AUDIO_FORMAT_INVALID;
    }
}*/

void CodecParser::parseLeAudioVendorConfiguration(const LeAudioVendorConfiguration &leVendorConfig, lc3_cfg_t &outConfig) {
    outConfig.sampling_freq = leVendorConfig.samplingFrequencyHz;
    outConfig.max_octets_per_frame = leVendorConfig.octetsPerFrame;
    outConfig.frame_duration = leVendorConfig.frameDurationUs;
    outConfig.bit_depth = leVendorConfig.pcmBitDepth;
    outConfig.num_blocks = leVendorConfig.blocksPerSdu;

    /* Copy vendor-specific configuration */
    memcpy(outConfig.vendor_specific, leVendorConfig.codecSpecificData.data(), sizeof(outConfig.vendor_specific));
}

void CodecParser::parseLeAudioVendorConfiguration(const LeAudioVendorConfiguration &leVendorConfig, lc3_encoder_cfg_t &outConfig) {
    parseLeAudioVendorConfiguration(leVendorConfig, outConfig.toAirConfig);

    /* Encoder-specific values */
    outConfig.toAirConfig.mode = 1; // FIXME: should this be 0?
    outConfig.toAirConfig.api_version = 0x00000021;
    outConfig.toAirConfig.default_q_level = 0;
}

void CodecParser::parseLeAudioVendorConfiguration(const LeAudioVendorConfiguration &leVendorConfig, lc3_decoder_cfg_t &outConfig) {
    parseLeAudioVendorConfiguration(leVendorConfig, outConfig.fromAirConfig);

    /* Decoder-specific values */
    outConfig.fromAirConfig.mode = 0;
    outConfig.fromAirConfig.api_version = 0x00000021;
    outConfig.fromAirConfig.default_q_level = 1;
}

void CodecParser::parseAptxAdaptiveR4Configuration(const LeAudioVendorConfiguration &leVendorConfig, lc3_cfg_t &outConfig) {
    LOG(DEBUG) << __func__ << ": aptX Adaptive R4 codec found";

    outConfig.sampling_freq = leVendorConfig.samplingFrequencyHz;
    outConfig.max_octets_per_frame = 0x05DC0000 | (leVendorConfig.octetsPerFrame);
    outConfig.frame_duration = leVendorConfig.frameDurationUs;
    outConfig.num_blocks = 0;
    outConfig.bit_depth = 24;

    outConfig.api_version = 0;
    outConfig.default_q_level = 0;

    /* Copy vendor-specific configuration */
    memcpy(outConfig.vendor_specific, leVendorConfig.codecSpecificData.data(), sizeof(outConfig.vendor_specific));

    outConfig.mode = fromU8x4(&leVendorConfig.codecSpecificData[16]);
    LOG(DEBUG) << __func__ << ": aptX Adaptive R4 mode: " << outConfig.mode;
}

lc3_stream_map_t *CodecParser::parseLeAudioStreamMapConfiguration(const LeAudioConfiguration &leAudioConfig, int direction) {
    lc3_stream_map_t *map = new lc3_stream_map_t[leAudioConfig.streamMap.size()];

    for (size_t i = 0; i < leAudioConfig.streamMap.size(); i++) {
        map[i].audio_location = leAudioConfig.streamMap[i].audioChannelAllocation;
        map[i].stream_id = i; // leAudioConfig.streamMap[i].streamHandle;
        map[i].direction = 0;
    }

    return map;
}


template <typename T>
codec_format_t CodecParser::parseLeAudioConfiguration(const LeAudioConfiguration &leAudioConfig, T &outConfig) {
    switch(leAudioConfig.codecType) {
        case CodecType::LC3:
            parseLc3Configuration(leAudioConfig.leAudioCodecConfig, outConfig);
            return CODEC_TYPE_LC3;
        case CodecType::VENDOR: {
            abort();
            LOG(INFO) << __func__ << ": LE Audio vendor codec found";
            std::optional<LeAudioVendorConfiguration> leVendorConfig;
            leAudioConfig.leAudioCodecConfig.get<LeAudioCodecConfiguration::vendorConfig>().extension.getParcelable(&leVendorConfig);
            CHECK(leVendorConfig.has_value());
            LOG(INFO) << __func__ << ": LE Audio vendor config: " << leVendorConfig->toString();
            switch (leVendorConfig->vendorCodecType) {
                case VendorCodecType::LC3Q:
                    parseLeAudioVendorConfiguration(leVendorConfig.value(), outConfig);
                    return CODEC_TYPE_LC3;
                case VendorCodecType::APTX_ADAPTIVE_R3:
                    parseLeAudioVendorConfiguration(leVendorConfig.value(), outConfig);
                    return CODEC_TYPE_APTX_AD_QLEA;
                case VendorCodecType::APTX_ADAPTIVE_R4:
                    parseAptxAdaptiveR4Configuration(leVendorConfig.value(), outConfig);
                    return CODEC_TYPE_APTX_AD_R4;
            }
        }
        default:
            LOG(ERROR) << __func__ << ": Unknown codec config: " << leAudioConfig.toString();
            return CODEC_TYPE_INVALID;
    }
}


std::pair<codec_format_t, void *> CodecParser::parseAudioConfiguration(const AudioConfiguration &config, std::optional<const AudioConfiguration> extraConfig) {
    LOG(DEBUG) << __func__ << ": " << config.toString();
    switch (config.getTag()) {
        case AudioConfiguration::pcmConfig:
            return std::make_pair(CODEC_TYPE_PCM, nullptr);
        case AudioConfiguration::a2dpConfig: {
            CHECK(!extraConfig.has_value());
            auto &a2dpConfig = config.get<AudioConfiguration::a2dpConfig>();
            switch (a2dpConfig.codecType) {
                case CodecType::SBC:
                    parseSbcConfiguration(a2dpConfig);
                    return std::make_pair(CODEC_TYPE_SBC, &mSbcConfig);
                case CodecType::AAC:
                    parseAacConfiguration(a2dpConfig);
                    return std::make_pair(CODEC_TYPE_AAC, &mAacConfig);
                case CodecType::APTX:
                    parseAptxClassicConfiguration(a2dpConfig);
                    return std::make_pair(CODEC_TYPE_APTX, &mAptxConfig);
                case CodecType::APTX_HD:
                    parseAptxHdConfiguration(a2dpConfig);
                    return std::make_pair(CODEC_TYPE_APTX_HD, &mAptxHdConfig);
                case CodecType::LDAC:
                    parseLdacConfiguration(a2dpConfig);
                    return std::make_pair(CODEC_TYPE_LDAC, &mLdacConfig);
                case CodecType::APTX_ADAPTIVE:
                    parseAptxAdaptiveConfiguration(a2dpConfig);
                    return std::make_pair(CODEC_TYPE_APTX_AD, &mAptxAdConfig);
                default:
                    LOG(ERROR) << __func__ << ": Unknown codec config: " << config.toString();
                    return std::make_pair(CODEC_TYPE_INVALID, nullptr);
            }
        }
        case AudioConfiguration::leAudioConfig: {
            auto &leAudioConfig = config.get<AudioConfiguration::leAudioConfig>();

            if (extraConfig.has_value()) {
                auto format = parseLeAudioConfiguration(leAudioConfig, mLc3DecConfig.dec_cfg);
                mLc3DecConfig.is_dec_config_set = format != CODEC_TYPE_INVALID;

                mLc3DecConfig.dec_cfg.decoder_output_channel = 2;
                mLc3DecConfig.dec_cfg.stream_map_size = leAudioConfig.streamMap.size();
                if (mLc3DecConfig.dec_cfg.streamMapIn != nullptr) {
                    delete[] mLc3DecConfig.dec_cfg.streamMapIn;
                }
                mLc3DecConfig.dec_cfg.streamMapIn = parseLeAudioStreamMapConfiguration(leAudioConfig, 1);

                const auto &extraLeConfig = extraConfig->get<AudioConfiguration::leAudioConfig>();

                auto extraFormat = parseLeAudioConfiguration(extraLeConfig, mLc3DecConfig.enc_cfg);
                mLc3DecConfig.is_enc_config_set = extraFormat != CODEC_TYPE_INVALID;

                mLc3DecConfig.enc_cfg.stream_map_size = extraLeConfig.streamMap.size();
                if (mLc3DecConfig.enc_cfg.streamMapOut != nullptr) {
                    delete[] mLc3DecConfig.enc_cfg.streamMapOut;
                }
                mLc3DecConfig.enc_cfg.streamMapOut = parseLeAudioStreamMapConfiguration(extraLeConfig, 0);

                if (format != extraFormat) {
                    LOG(ERROR) << __func__ << ": Different codec types for encoder and decoder" << config.toString();
                    return std::make_pair(CODEC_TYPE_INVALID, &mLc3BroadcastConfig);
                }
                return std::make_pair(format, &mLc3DecConfig);
            } else {
                auto format = parseLeAudioConfiguration(leAudioConfig, mLc3EncConfig.enc_cfg);
                mLc3EncConfig.is_enc_config_set = format != CODEC_TYPE_INVALID;
                mLc3EncConfig.is_dec_config_set = false;

                mLc3EncConfig.enc_cfg.stream_map_size = leAudioConfig.streamMap.size();
                if (mLc3EncConfig.enc_cfg.streamMapOut != nullptr) {
                    delete[] mLc3EncConfig.enc_cfg.streamMapOut;
                }
                mLc3EncConfig.enc_cfg.streamMapOut = parseLeAudioStreamMapConfiguration(leAudioConfig, 0);

                return std::make_pair(format, &mLc3EncConfig);
            }
        }
        case AudioConfiguration::leAudioBroadcastConfig:
            CHECK(!extraConfig.has_value());
            parseLeAudioBroadcastConfiguration(config.get<AudioConfiguration::leAudioBroadcastConfig>());
            return std::make_pair(CODEC_TYPE_LC3, &mLc3BroadcastConfig);
        default:
            LOG(ERROR) << __func__ << ": Unknown codec config: " << config.toString();
            return std::make_pair(CODEC_TYPE_INVALID, nullptr);
    }
}

std::optional<int> CodecParser::parseAptxAdaptiveR4Delay(const AudioConfiguration &config) {
    LOG(DEBUG) << __func__ << ": " << config.toString();

    if (config.getTag() != AudioConfiguration::leAudioConfig) {
        LOG(ERROR) << __func__ << ": Invalid aptX Adaptive R4 codec config tag: " << toString(config.getTag());
        return std::nullopt;
    }

    auto &leAudioConfig = config.get<AudioConfiguration::leAudioConfig>();
    if (leAudioConfig.codecType != CodecType::VENDOR) {
        LOG(ERROR) << __func__ << ": Wrong codec type: " << toString(leAudioConfig.codecType);
        return std::nullopt;
    }

    std::optional<LeAudioVendorConfiguration> leVendorConfig;
    leAudioConfig.leAudioCodecConfig.get<LeAudioCodecConfiguration::vendorConfig>().extension.getParcelable(&leVendorConfig);
    if (leVendorConfig->vendorCodecType != VendorCodecType::APTX_ADAPTIVE_R4) {
        LOG(ERROR) << __func__ << ": Wrong vendor codec type: " << toString(leVendorConfig->vendorCodecType);
        return std::nullopt;
    }

    if (leVendorConfig->codecSpecificData.size() < 22) {
        LOG(ERROR) << __func__ << ": Codec-specific data is too short to contain delay: " << leVendorConfig->toString();
        return std::nullopt;
    }
    uint16_t delay = (leVendorConfig->codecSpecificData[20] << 8) | leVendorConfig->codecSpecificData[21];
    LOG(DEBUG) << __func__ << ": aptX Adaptive R4 delay: " << delay;
    return std::make_optional(delay);
}

}
