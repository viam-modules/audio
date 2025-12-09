#include "audio_stream.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace audio {

InputStreamContext::InputStreamContext(const vsdk::audio_info& audio_info, int buffer_duration_seconds)
    : AudioBuffer(audio_info, buffer_duration_seconds), stream_start_time(), first_sample_adc_time(0.0), first_callback_captured(false) {}

std::chrono::nanoseconds InputStreamContext::calculate_sample_timestamp(uint64_t sample_number) noexcept {
    // Convert sample_number to frame number (samples include all channels)
    uint64_t frame_number = sample_number / info.num_channels;
    uint64_t elapsed_ns = (frame_number * NANOSECONDS_PER_SECOND) / info.sample_rate_hz;

    auto elapsed_duration = std::chrono::nanoseconds(elapsed_ns);
    auto absolute_time = stream_start_time + elapsed_duration;

    return std::chrono::duration_cast<std::chrono::nanoseconds>(absolute_time.time_since_epoch());
    return std::chrono::duration_cast<std::chrono::nanoseconds>(absolute_time.time_since_epoch());
}

uint64_t InputStreamContext::get_sample_number_from_timestamp(int64_t timestamp) noexcept {
    auto stream_start_ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(stream_start_time);
    int64_t stream_start_timestamp_ns = stream_start_ns.time_since_epoch().count();

    int64_t elapsed_time_ns = timestamp - stream_start_timestamp_ns;
    double elapsed_seconds = static_cast<double>(elapsed_time_ns) / NANOSECONDS_PER_SECOND;
    uint64_t sample_number = static_cast<uint64_t>(elapsed_seconds * info.sample_rate_hz * info.num_channels);
    return sample_number;
}

OutputStreamContext::OutputStreamContext(const vsdk::audio_info& audio_info, int buffer_duration_seconds)
    : AudioBuffer(audio_info, buffer_duration_seconds), playback_position(0) {}

}  // namespace audio
