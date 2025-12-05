#pragma once

#include <lame/lame.h>
#include <cstdint>
#include <vector>
#include <memory>
#include "audio_utils.hpp"

namespace speaker {

using audio::utils::CleanupPtr;

struct MP3DecoderContext {
    CleanupPtr<hip_decode_exit> decoder = nullptr;

    int sample_rate = 0;
    int num_channels = 0;
};

void initialize_mp3_decoder(MP3DecoderContext& ctx);
void decode_mp3_to_pcm16(
    MP3DecoderContext& ctx,
    const std::vector<uint8_t>& encoded_data,
    std::vector<uint8_t>& output_data);
void cleanup_mp3_decoder(MP3DecoderContext& ctx);

}  // namespace speaker
