#include "mp3_decoder.hpp"
#include <viam/sdk/common/utils.hpp>
#include <cstring>
#include <stdexcept>

namespace speaker{

// Helper function to convert MP3 decoder error codes to readable strings
static const std::string hip_decode_error_to_string(int error_code) {
    switch (error_code) {
        case -1:
            return "MP3 decoder: error decoding data";
        case -2:
            return "MP3 decoder: invalid MP3 stream";
        default:
            return "Unknown MP3 decoder error";
    }
}

void initialize_mp3_decoder(MP3DecoderContext& ctx) {
    CleanupPtr<hip_decode_exit> hip(hip_decode_init());
    if (!hip) {
        VIAM_SDK_LOG(error) << "Failed to initialize MP3 decoder";
        throw std::runtime_error("Failed to initialize MP3 decoder");
    }

    ctx.decoder = std::move(hip);

    VIAM_SDK_LOG(debug) << "MP3 decoder initialized";
}

void decode_mp3_to_pcm16(
    MP3DecoderContext& ctx,
    const std::vector<uint8_t>& encoded_data,
    std::vector<uint8_t>& output_data) {

    if (!ctx.decoder) {
        VIAM_SDK_LOG(error) << "decode_mp3_to_pcm16: MP3 decoder not initialized";
        throw std::runtime_error("decode_mp3_to_pcm16: MP3 decoder not initialized");
    }

    if (encoded_data.empty()) {
        VIAM_SDK_LOG(debug) << "decode_mp3_to_pcm16: no data to decode";
        return;
    }

    // Buffers for decoded PCM samples
    const size_t BUFFER_SIZE = 8192;  // Samples per channel
    std::vector<int16_t> pcm_l(BUFFER_SIZE);
    std::vector<int16_t> pcm_r(BUFFER_SIZE);

    mp3data_struct mp3data;
    memset(&mp3data, 0, sizeof(mp3data));


    // Feed data to decoder in chunks
    const size_t CHUNK_SIZE = 4096;  // Feed 4KB at a time
    size_t offset = 0;


    std::vector<uint8_t> audio_data(encoded_data.start(), encoded_data.end(), encoded_data.size());

    while (offset < audio_data.size()) {
        uint32_t remaining = audio_data.size() - offset;
        uint32_t data_len = std::min(CHUNK_SIZE, remaining);

        // This returns at most one frame with mp3 header data
        int decoded_samples = hip_decode1_headers(
            ctx.decoder.get(),
            mp3_data.data() + offset,
            data_len,
            pcm_l.data(),
            pcm_r.data(),
            &mp3data);

        if (decoded_samples < 0) {
            VIAM_SDK_LOG(error) << "Error decoding MP3 data: " << hip_decode_error_to_string(decoded_samples)
                                << " (code: " << decoded_samples << ")";
            throw std::runtime_error("MP3 decoding error");
        }

        // Get audio properties from first successful decode
        if (decoded_samples > 0 && ctx.sample_rate == 0) {
            ctx.sample_rate = mp3data.samplerate;
            ctx.num_channels = mp3data.stereo;

            VIAM_SDK_LOG(debug) << "MP3 audio properties: " << ctx.sample_rate << "Hz, "
                                << ctx.num_channels << " channels";
        }

        // Append decoded samples to output (interleaved for stereo)
        if (decoded_samples > 0) {
            uint32_t current_size = output_data.size();
            uint32_t samples_to_add = decoded_samples * ctx.num_channels;
            output_data.resize(current_size + samples_to_add * sizeof(int16_t));

            int16_t* output_ptr = reinterpret_cast<int16_t*>(output_data.data() + current_size);

            for (int i = 0; i < decoded_samples; i++) {
                *output_ptr++ = pcm_l[i];
                if (ctx.num_channels == 2) {
                    *output_ptr++ = pcm_r[i];
                }
            }
        }

        offset += chunk;
    }

    // Flush decoder - repeatedly call with null/0 until no more samples
    while (true) {
        int decoded_samples = hip_decode1_headers(
            ctx.decoder.get(),
            nullptr,
            0,
            pcm_l.data(),
            pcm_r.data(),
            &mp3data);

        if (decoded_samples <= 0) {
            break;
        }

        // Set properties if not set yet (edge case)
        if (ctx.sample_rate == 0) {
            ctx.sample_rate = mp3data.samplerate;
            ctx.num_channels = mp3data.stereo;
        }

        // Append flushed samples
        size_t current_size = output_data.size();
        size_t samples_to_add = decoded_samples * ctx.num_channels;
        output_data.resize(current_size + samples_to_add * sizeof(int16_t));

        int16_t* output_ptr = reinterpret_cast<int16_t*>(output_data.data() + current_size);

        for (int i = 0; i < decoded_samples; i++) {
            *output_ptr++ = pcm_l[i];
            if (ctx.num_channels == 2) {
                *output_ptr++ = pcm_r[i];
            }
        }
    }

    if (output_data.empty()) {
        VIAM_SDK_LOG(error) << "No audio data was decoded from MP3";
        throw std::runtime_error("No audio data was decoded");
    }

    VIAM_SDK_LOG(debug) << "Decoded " << output_data.size() / sizeof(int16_t) / ctx.num_channels
                        << " frames (" << output_data.size() << " bytes)";
}

void cleanup_mp3_decoder(MP3DecoderContext& ctx) {
    ctx.decoder.reset();
    ctx.sample_rate = 0;
    ctx.num_channels = 0;
    ctx.initialized = false;
}

}  // namespace speaker
