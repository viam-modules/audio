#pragma once

#include <viam/sdk/components/audio_in.hpp>
#include <viam/sdk/common/audio.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include "portaudio.h"
#include "portaudio.hpp"
#include "audio_stream.hpp"
#include "audio_utils.hpp"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <tuple>

namespace microphone {
namespace vsdk = ::viam::sdk;

struct ActiveStreamConfig {
    std::string device_name;
    int sample_rate;
    int num_channels;
    double latency;

    bool operator==(const ActiveStreamConfig& other) const {
        return std::tie(device_name, sample_rate, num_channels, latency) ==
               std::tie(other.device_name, other.sample_rate, other.num_channels, other.latency);
    }

    bool operator!=(const ActiveStreamConfig& other) const {
        return !(*this == other);
    }
};

PaDeviceIndex findDeviceByName(const std::string& name, const audio::portaudio::PortAudioInterface& pa);


class Microphone final : public viam::sdk::AudioIn, public viam::sdk::Reconfigurable {
public:
    Microphone(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg,
               audio::portaudio::PortAudioInterface* pa = nullptr);

    ~Microphone();

    static std::vector<std::string> validate(viam::sdk::ResourceConfig cfg);

    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command);

    void get_audio(std::string const& codec,
                   std::function<bool(vsdk::AudioIn::audio_chunk&& chunk)> const& chunk_handler,
                   double const& duration_seconds,
                   int64_t const& previous_timestamp,
                   const viam::sdk::ProtoStruct& extra);

    viam::sdk::audio_properties get_properties(const viam::sdk::ProtoStruct& extra);
    std::vector<viam::sdk::GeometryConfig> get_geometries(const viam::sdk::ProtoStruct& extra);
    void reconfigure(const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg);

    // internal functions, public for testing
    void openStream(PaStream*& stream);
    void startStream(PaStream* stream);
    void shutdownStream(PaStream* stream);

    // Member variables
    std::string device_name_;
    PaDeviceIndex device_index_;
    int sample_rate_;
    int num_channels_;
    double latency_;
    static vsdk::Model model;

    // The mutex protects the stream, context, and the active streams counter
    std::mutex stream_ctx_mu_;
    PaStream* stream_;
    std::shared_ptr<audio::InputStreamContext> audio_context_;
    // This is null in production and used for testing to inject the mock portaudio functions
    const audio::portaudio::PortAudioInterface* pa_;
    // Count of active get_audio calls
    int active_streams_;
};


/**
 * PortAudio callback function - runs on real-time audio thread.
 *
 * CRITICAL: This function must not:
 * - Allocate memory (malloc/new)
 * - Access the file system
 * - Call any functions that may block
 * - Take unpredictable amounts of time to complete
 *
 * From PortAudio docs: Do not allocate memory, access the file system,
 * call library functions or call other functions from the stream callback
 * that may block or take an unpredictable amount of time to complete.
 */
int AudioCallback(const void *inputBuffer, void *outputBuffer,
                  unsigned long framesPerBuffer,
                  const PaStreamCallbackTimeInfo* timeInfo,
                  PaStreamCallbackFlags statusFlags,
                  void *userData);


} // namespace microphone
