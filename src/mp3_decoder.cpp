#include "mp3_decoder.hpp"
#include <cstring>
#include <stdexcept>
#include <viam/sdk/common/utils.hpp>

namespace speaker {

// Helper to skip ID3v2 tags at the beginning of MP3 data
static size_t skip_id3v2_tag(const std::vector<uint8_t>& data) {
    // Check for ID3v2 tag (starts with "ID3")
    if (data.size() < 10 || data[0] != 'I' || data[1] != 'D' || data[2] != '3') {
        return 0;  // No ID3 tag
    }

    // ID3v2 size is stored in bytes 6-9 as a synchsafe integer (28 bits)
    // Each byte uses only 7 bits, MSB is always 0
    size_t tag_size = ((data[6] & 0x7F) << 21) | ((data[7] & 0x7F) << 14) | ((data[8] & 0x7F) << 7) | (data[9] & 0x7F);

    // Total size is tag_size + 10 byte header
    size_t total_size = tag_size + 10;

    VIAM_SDK_LOG(debug) << "Skipping ID3v2 tag: " << total_size << " bytes";
    return total_size;
}

MP3DecoderContext::MP3DecoderContext() : sample_rate(0), num_channels(0) {
    CleanupPtr<hip_decode_exit> hip(hip_decode_init());
    if (!hip) {
        VIAM_SDK_LOG(error) << "Failed to initialize MP3 decoder";
        throw std::runtime_error("Failed to initialize MP3 decoder");
    }

    decoder = std::move(hip);

    VIAM_SDK_LOG(info) << "MP3 decoder initialized";
}

MP3DecoderContext::~MP3DecoderContext() {
    VIAM_SDK_LOG(debug) << "MP3DecoderContext destructor called";
    decoder.reset();
    VIAM_SDK_LOG(debug) << "MP3 decoder cleaned up";
    sample_rate = 0;
    num_channels = 0;
}

// Helper to append samples to output buffer
static void append_samples(std::vector<uint8_t>& output_data,
                           const std::vector<int16_t>& pcm_l,
                           const std::vector<int16_t>& pcm_r,
                           int sample_count,
                           int num_channels) {
    // Bounds check to prevent buffer overflow
    if (sample_count > static_cast<int>(pcm_l.size()) || sample_count > static_cast<int>(pcm_r.size())) {
        VIAM_SDK_LOG(error) << "sample_count " << sample_count << " exceeds buffer size (pcm_left=" << pcm_l.size()
                            << ", pcm_right=" << pcm_r.size() << ")";
        throw std::runtime_error("sample_count exceeds pcm data buffersize: audio file too long");
    }
    size_t current_size = output_data.size();
    size_t samples_to_add = sample_count * num_channels;
    output_data.resize(current_size + samples_to_add * sizeof(int16_t));

    int16_t* output_ptr = reinterpret_cast<int16_t*>(output_data.data() + current_size);

    if (num_channels != 1 && num_channels != 2) {
        VIAM_SDK_LOG(error) << "invalid num channels: " << num_channels;
        throw std::invalid_argument("invalid num channels");
    }

    for (int i = 0; i < sample_count; i++) {
        *output_ptr++ = pcm_l[i];
        if (num_channels == 2) {
            *output_ptr++ = pcm_r[i];
        }
    }
}

void decode_mp3_to_pcm16(MP3DecoderContext& ctx, const std::vector<uint8_t>& encoded_data, std::vector<uint8_t>& output_data) {
    if (!ctx.decoder) {
        VIAM_SDK_LOG(error) << "decode_mp3_to_pcm16: MP3 decoder not initialized";
        throw std::runtime_error("decode_mp3_to_pcm16: MP3 decoder not initialized");
    }

    if (encoded_data.empty()) {
        VIAM_SDK_LOG(debug) << "decode_mp3_to_pcm16: no data to decode";
        return;
    }

    // Skip ID3 tag if present
    size_t offset = skip_id3v2_tag(encoded_data);
    if (offset >= encoded_data.size()) {
        VIAM_SDK_LOG(error) << "MP3 data contains only ID3 tag, no audio frames";
        throw std::runtime_error("No MP3 audio data found");
    }

    VIAM_SDK_LOG(debug) << "Decoding " << (encoded_data.size() - offset) << " bytes of MP3 data (offset: " << offset << ")";

    // Buffers for decoded PCM samples
    const size_t BUFFER_SIZE = 500000;  // Samples per channel
    std::vector<int16_t> pcm_left(BUFFER_SIZE);
    std::vector<int16_t> pcm_right(BUFFER_SIZE);

    mp3data_struct mp3data;
    memset(&mp3data, 0, sizeof(mp3data));

    const unsigned char* mp3_ptr = encoded_data.data() + offset;
    size_t encoded_data_length = encoded_data.size() - offset;

    int decoded_samples = hip_decode_headers(ctx.decoder.get(),
                                             const_cast<unsigned char*>(mp3_ptr),  // input buffer
                                             encoded_data_length,
                                             pcm_left.data(),
                                             pcm_right.data(),
                                             &mp3data);

    VIAM_SDK_LOG(debug) << "hip_decode_headers returned: " << decoded_samples << " samples";

    if (decoded_samples < 0) {
        VIAM_SDK_LOG(error) << "Error decoding MP3 data";
        throw std::runtime_error("MP3 decoding error");
    }

    // Get audio properties from header (even if no samples decoded yet)
    if (ctx.sample_rate == 0 && mp3data.samplerate != 0) {
        ctx.sample_rate = mp3data.samplerate;
        ctx.num_channels = mp3data.stereo;
        VIAM_SDK_LOG(debug) << "MP3 audio properties: " << ctx.sample_rate << "Hz, " << ctx.num_channels << " channels";
    }

    if (decoded_samples == 0) {
        // The audio data size was too small, and all of it is still buffered.
        // We need to flush to get the data
        VIAM_SDK_LOG(debug) << "No decoded frames returned, proceeding to flush";
    } else {
        // Ensure we have valid properties before appending
        if (ctx.num_channels == 0) {
            VIAM_SDK_LOG(error) << "Cannot append samples: num_channels not set";
            throw std::runtime_error("MP3 properties not extracted before appending samples");
        }
        append_samples(output_data, pcm_left, pcm_right, decoded_samples, ctx.num_channels);
    }

    // Flush decoder - repeatedly call with nullptr until no more samples
    int flush_count = 0;
    while (true) {
        int decoded_samples = hip_decode_headers(ctx.decoder.get(), nullptr, 0, pcm_left.data(), pcm_right.data(), &mp3data);

        if (decoded_samples < 0) {
            VIAM_SDK_LOG(error) << "MP3 decoder failed to flush";
            throw std::runtime_error("MP3 decoder failed to flush");
        } else if (decoded_samples == 0) {
            VIAM_SDK_LOG(debug) << "flush returned zero samples, breaking loop";
            break;
        } else {
            if (ctx.sample_rate == 0 && mp3data.samplerate != 0) {
                ctx.sample_rate = mp3data.samplerate;
                ctx.num_channels = mp3data.stereo;
                VIAM_SDK_LOG(debug) << "MP3 audio properties from flush: " << ctx.sample_rate << "Hz, " << ctx.num_channels << " channels";
            }

            // Ensure we have valid properties before appending
            if (ctx.num_channels == 0) {
                VIAM_SDK_LOG(error) << "Cannot append samples: num_channels not set";
                throw std::runtime_error("MP3 properties not extracted before appending samples");
            }

            append_samples(output_data, pcm_left, pcm_right, decoded_samples, ctx.num_channels);
            flush_count++;
        }
    }

    VIAM_SDK_LOG(debug) << "Flushed " << flush_count << " additional frames";

    if (output_data.empty()) {
        VIAM_SDK_LOG(error) << "No audio data was decoded from MP3";
        throw std::runtime_error("No audio data was decoded");
    }

    // Ensure we extracted valid audio properties
    if (ctx.sample_rate == 0 || ctx.num_channels == 0) {
        VIAM_SDK_LOG(error) << "Failed to extract MP3 audio properties (sample_rate=" << ctx.sample_rate
                            << ", num_channels=" << ctx.num_channels << ")";
        throw std::runtime_error("Failed to extract MP3 audio properties");
    }

    VIAM_SDK_LOG(debug) << "Total decoded: " << (output_data.size() / sizeof(int16_t) / ctx.num_channels) << " frames ("
                        << output_data.size() << " bytes)";
}

}  // namespace speaker
