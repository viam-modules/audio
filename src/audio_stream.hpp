#pragma once

#include <viam/sdk/components/audio_in.hpp>
#include <viam/sdk/common/audio.hpp>
#include "portaudio.h"
#include <chrono>
#include <vector>
#include <mutex>
#include <atomic>

namespace audio {

namespace vsdk = ::viam::sdk;

constexpr int BUFFER_DURATION_SECONDS = 10;  // How much audio history to keep in buffer
constexpr double CHUNK_DURATION_SECONDS = 0.1;   // 100ms chunks (10 chunks per second)
constexpr uint64_t NANOSECONDS_PER_SECOND = 1000000000ULL;

// Base class for audio buffering - lock-free circular buffer with atomic operations
// Can be used by both input (microphone) and output (speaker) components
class AudioBuffer {
public:
    AudioBuffer(const vsdk::audio_info& audio_info, int buffer_duration_seconds);
    virtual ~AudioBuffer() = default;

    // Writes an audio sample to the audio buffer
    void write_sample(int16_t sample) noexcept;

    // Read sample_count samples from the circular buffer starting at the inputted position
    int read_samples(int16_t* buffer, int sample_count, uint64_t& position) noexcept;

    uint64_t get_write_position() const noexcept;

    void reset() noexcept;

    vsdk::audio_info info;

protected:
    std::unique_ptr<std::atomic<int16_t>[]> audio_buffer;
    int buffer_capacity;
    std::atomic<uint64_t> total_samples_written;
};

// InputStreamContext manages a circular buffer of audio for microphone input
// Extends AudioBuffer with timestamp tracking for accurate audio capture metadata
class InputStreamContext : public AudioBuffer {
public:
    InputStreamContext(const vsdk::audio_info& audio_info,
                      int samples_per_chunk,
                      int buffer_duration_seconds = BUFFER_DURATION_SECONDS);

    int samples_per_chunk;
    std::chrono::system_clock::time_point stream_start_time;
    double first_sample_adc_time;
    std::atomic<bool> first_callback_captured;
};

// Alias for backwards compatibility during refactoring
using AudioStreamContext = InputStreamContext;

// OutputStreamContext manages a circular buffer of audio for speaker output
// Extends AudioBuffer with playback position tracking
class OutputStreamContext : public AudioBuffer {
public:
    OutputStreamContext(const vsdk::audio_info& audio_info,
                       int buffer_duration_seconds = BUFFER_DURATION_SECONDS);

    std::atomic<uint64_t> playback_position;
};

std::chrono::nanoseconds calculate_sample_timestamp(
    const AudioStreamContext& ctx,
    uint64_t sample_number);


} // namespace audio
