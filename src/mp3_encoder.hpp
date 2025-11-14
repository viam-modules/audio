#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
}

#include <vector>
#include <cstdint>
#include <boost/callable_traits.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include <tuple>
#include <type_traits>

namespace microphone {
namespace vsdk = ::viam::sdk;


// Generic cleanup wrapper for functions with custom deleters
template <auto cleanup_fp>
struct Cleanup {
    using pointer_type = std::tuple_element_t<0, boost::callable_traits::args_t<decltype(cleanup_fp)>>;
    using value_type = std::remove_pointer_t<pointer_type>;

    void operator()(pointer_type p) {
        if (p != nullptr) {
            cleanup_fp(p);
        }
    }
};

template <auto cleanup_fp>
using CleanupPtr = std::unique_ptr<typename Cleanup<cleanup_fp>::value_type, Cleanup<cleanup_fp>>;

// FFmpeg cleanup functions uses double pointers, so we need wrappers for CleanupPtr
inline void avcodec_context_cleanup(AVCodecContext* ctx) {
    avcodec_free_context(&ctx);
}

inline void avframe_cleanup(AVFrame* frame) {
    av_frame_free(&frame);
}

inline void avpacket_cleanup(AVPacket* pkt) {
    av_packet_free(&pkt);
}

struct MP3EncoderContext {
    CleanupPtr<avcodec_context_cleanup> ffmpeg_ctx = nullptr;
    CleanupPtr<avframe_cleanup> frame = nullptr;
    std::vector<int16_t> buffer;  // Buffer for incomplete frames

    int sample_rate = 0;
    int num_channels = 0;

    // Track the stream sample number where buffered data starts
    uint64_t buffer_start_position = 0;
    // Track total samples sent to encoder
    uint64_t total_samples_encoded = 0;
};


void initialize_mp3_encoder(MP3EncoderContext& ctx, int sample_rate, int num_channels);
void encode_mp3_samples(MP3EncoderContext& ctx,
                        const int16_t* samples,
                        int sample_count,
                        uint64_t chunk_start_position,
                        std::vector<uint8_t>& output_data);
void flush_mp3_encoder(MP3EncoderContext& ctx, std::vector<uint8_t>& output_data);
void cleanup_mp3_encoder(MP3EncoderContext& ctx);

} // namespace microphone
