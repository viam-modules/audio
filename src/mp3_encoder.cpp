#include <cstring>
#include <stdexcept>
#include "mp3_encoder.hpp"

namespace microphone {

// Helper function to convert LAME error codes to readable strings
static const char* lame_error_to_string(int error_code) {
    switch (error_code) {
        case LAME_GENERICERROR:
            return "LAME generic error";
        case LAME_NOMEM:
            return "LAME no memory error: out of memory";
        case LAME_BADBITRATE:
            return "invalid bit rate";
        case LAME_BADSAMPFREQ:
            return "invalid sample rate";
        case LAME_INTERNALERROR:
            return "LAME internal error";
        default:
            return "Unknown LAME error";
    }
}

// Helper function to encode samples with proper error handling
static void encode_samples(MP3EncoderContext& ctx,
                                   const std::vector<int16_t>& samples,
                                   std::vector<uint8_t>& output_data) {
    std::vector<int16_t> left_samples, right_samples;
    const int16_t* left_channel = nullptr;
    const int16_t* right_channel = nullptr;
    int num_samples_per_channel = 0;

    if (ctx.num_channels == 2) {
        // Stereo: deinterleave samples into separate left/right buffers
        deinterleave_samples(samples, left_samples, right_samples);
        left_channel = left_samples.data();
        right_channel = right_samples.data();
        num_samples_per_channel = static_cast<int>(left_samples.size());
    } else {
        // Mono: use samples directly (no deinterleaving needed)
        left_channel = samples.data();
        num_samples_per_channel = static_cast<int>(left_samples.size());
    }

    // Allocate output buffer: largest size is 1.25 * num_samples + 7200 (from LAME docs)
    int mp3buf_size = static_cast<int>(1.25 * num_samples_per_channel + 7200);
    size_t old_size = output_data.size();
    output_data.resize(old_size + mp3buf_size);

    int bytes_written = lame_encode_buffer(
        ctx.encoder.get(),
        left_channel,
        right_channel,
        num_samples_per_channel,
        output_data.data() + old_size,
        mp3buf_size
    );

    if (bytes_written < 0) {
        VIAM_SDK_LOG(error) << "LAME encoding error: "
                            << lame_error_to_string(bytes_written) << " (code: " << bytes_written << ")";
        throw std::runtime_error("LAME encoding error");
    }

    // Resize to actual bytes written
    output_data.resize(old_size + bytes_written);
}

void initialize_mp3_encoder(MP3EncoderContext& ctx, int sample_rate, int num_channels) {
    ctx.sample_rate = sample_rate;
    ctx.num_channels = num_channels;

    CleanupPtr<lame_close> lame(lame_init());
    if (!lame) {
        VIAM_SDK_LOG(error) << "Failed to initialize MP3 encoder";
        throw std::runtime_error("Failed to initialize MP3 encodor");
    }

    ctx.encoder = std::move(lame);

    // Configure encoder
    lame_set_in_samplerate(ctx.encoder.get(), sample_rate);
    lame_set_num_channels(ctx.encoder.get(), num_channels);
    // 192 kbps bit rate - how many bits of audio used to represent one second of audio
    // higher bitrate = better quality,larger file size
    lame_set_brate(ctx.encoder.get(), 192);
    // 2 = high quality (0=best, 9=worst).
    // higher quality = slower
    lame_set_quality(ctx.encoder.get(), 2);


    int init_result = lame_init_params(ctx.encoder.get());
    if (init_result < 0) {
        VIAM_SDK_LOG(error) << "Failed to initialize MP3 encoder parameters: "
                            << lame_error_to_string(init_result) << " (code: " << init_result << ")";
        throw std::runtime_error("Failed to initialize MP3 encoder parameters");
    }

    VIAM_SDK_LOG(info) << "MP3 encoder initialized: " << sample_rate
                       << "Hz, " << num_channels << " channels, 192kbps CBR";
}

void buffer_and_encode_samples(MP3EncoderContext& ctx,
                               const int16_t* samples,
                               int sample_count,
                               uint64_t chunk_start_position,
                               std::vector<uint8_t>& output_data) {
    if (!ctx.encoder) {
        VIAM_SDK_LOG(error) << "MP3 encoder not initialized";
        throw std::runtime_error("MP3 encoder not initialized");
    }

    // mp3 standard frame size is 1152 samples per channel
    const int samples_per_frame = 1152 * ctx.num_channels;

    // Track position of first sample if buffer is empty
    if (ctx.buffer.empty()) {
        ctx.buffer_start_position = chunk_start_position;
    }

    ctx.buffer.insert(ctx.buffer.end(), samples, samples + sample_count);

    // Encode as many complete frames as we have buffered
    while (ctx.buffer.size() >= samples_per_frame) {
        // Extract one frame worth of samples from buffer
        std::vector<int16_t> frame_samples(
            ctx.buffer.begin(),
            ctx.buffer.begin() + samples_per_frame
        );

        encode_samples(ctx, frame_samples, output_data);

        // Remove encoded samples from buffer and update position tracking
        ctx.buffer.erase(ctx.buffer.begin(), ctx.buffer.begin() + samples_per_frame);
        ctx.buffer_start_position += samples_per_frame;
        ctx.total_samples_encoded += samples_per_frame;
    }
}

void flush_mp3_encoder(MP3EncoderContext& ctx, std::vector<uint8_t>& output_data) {
    if (!ctx.encoder) {
        VIAM_SDK_LOG(error) << "flush_mp3_encoder: encoder is null";
        throw std::invalid_argument("flush_mp3_encoder: encoder is null");
    }

    // First, encode any remaining buffered samples (even if incomplete frame)
    if (!ctx.buffer.empty()) {
        int remaining_samples = ctx.buffer.size() / ctx.num_channels;
        VIAM_SDK_LOG(debug) << "Encoding " << remaining_samples << " remaining buffered samples before flush";
        encode_samples(ctx, ctx.buffer, output_data);
        ctx.buffer.clear();
    }

    // flush LAME's internal buffers to get any remaining encoded data
    // from mp3lame docs: 'mp3buf' should be at least 7200 bytes long to hold all possible emitted data.
    std::vector<uint8_t> mp3_buffer(7200);
    int flushed_bytes = lame_encode_flush(ctx.encoder.get(), mp3_buffer.data(), mp3_buffer.size());
    if (flushed_bytes < 0) {
        VIAM_SDK_LOG(error) << "LAME flush error: "
                            << lame_error_to_string(flushed_bytes) << " (code: " << flushed_bytes << ")";
        throw std::runtime_error("LAME encoding error during final flush");
    } else if (flushed_bytes > 0) {
        VIAM_SDK_LOG(debug) << "MP3 encoder flushed " << flushed_bytes << " bytes from internal buffers";
        output_data.insert(output_data.end(), mp3_buffer.begin(), mp3_buffer.begin() + flushed_bytes);
    }
}

void cleanup_mp3_encoder(MP3EncoderContext& ctx) {
    // Reset smart pointers
    ctx.encoder.reset();

    // Clear state
    ctx.buffer.clear();
    ctx.sample_rate = 0;
    ctx.num_channels = 0;
    ctx.buffer_start_position = 0;
    ctx.total_samples_encoded = 0;
}

// Takes a vector of interleaved samples and seperates them into seperate vectors
// for left and right channels
void deinterleave_samples(const std::vector<int16_t> &interleaved,
    std::vector<int16_t> &left,
    std::vector<int16_t> &right
) noexcept {
    int num_frames = interleaved.size() / 2;
    left.resize(num_frames);
    right.resize(num_frames);

    for (int i = 0; i < num_frames; i++) {
        left[i] = interleaved[2*i];
        right[i] = interleaved[2*i + 1];
    }

}

} // namespace microphone
