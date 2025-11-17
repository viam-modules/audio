#include "microphone.hpp"
#include "audio_utils.hpp"
#include "audio_stream.hpp"
#include <thread>

namespace microphone {

Microphone::Microphone(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg,
                       audio::portaudio::PortAudioInterface* pa)
    : viam::sdk::AudioIn(cfg.name()), stream_(nullptr), pa_(pa), active_streams_(0) {

    auto cfg_params = audio::utils::parseConfigAttributes(cfg);

    auto stream_params = audio::utils::setupStreamFromConfig(
        cfg_params,
        audio::utils::StreamDirection::Input,
        AudioCallback,
        pa_
    );

    // Create audio context with actual sample rate/channels from params
    vsdk::audio_info info{vsdk::audio_codecs::PCM_16, stream_params.sample_rate, stream_params.num_channels};
    int samples_per_chunk = stream_params.sample_rate * CHUNK_DURATION_SECONDS;
    auto new_audio_context = std::make_shared<AudioStreamContext>(info, samples_per_chunk);

    // Set user_data to point to the audio context
    stream_params.user_data = new_audio_context.get();

    // Set new configuration and start stream under lock
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        device_name_ = stream_params.device_name;
        device_index_ = stream_params.device_index;
        sample_rate_ = stream_params.sample_rate;
        num_channels_ = stream_params.num_channels;
        latency_ = stream_params.latency_seconds;
        audio_context_ = new_audio_context;

        audio::utils::restart_stream(stream_, stream_params, pa_);
    }
}

Microphone::~Microphone() {
       if (stream_) {
          PaError err = Pa_StopStream(stream_);
          if (err != paNoError) {
              VIAM_SDK_LOG(error) << "Failed to stop stream in destructor: "
                                 << Pa_GetErrorText(err);
          }

          err = Pa_CloseStream(stream_);
          if (err != paNoError) {
              VIAM_SDK_LOG(error) << "Failed to close stream in destructor: "
                                 << Pa_GetErrorText(err);
          }
    }
}

vsdk::Model Microphone::model("viam", "audio", "microphone");


std::vector<std::string> Microphone::validate(viam::sdk::ResourceConfig cfg) {
    auto attrs = cfg.attributes();

    if(attrs.count("device_name")) {
        if (!attrs["device_name"].is_a<std::string>()) {
            VIAM_SDK_LOG(error) << "[validate] device_name attribute must be a string";
            throw std::invalid_argument("device_name attribute must be a string");
        }
    }

    if(attrs.count("sample_rate")) {
        if (!attrs["sample_rate"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] sample_rate attribute must be a number";
            throw std::invalid_argument("sample_rate attribute must be a number");
        }
        double sample_rate = *attrs.at("sample_rate").get<double>();
        if (sample_rate <= 0) {
            VIAM_SDK_LOG(error) << "[validate] sample rate must be greater than zero";
            throw std::invalid_argument("sample rate must be greater than zero");
        }
    }
    if(attrs.count("num_channels")) {
        if (!attrs["num_channels"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] num_channels attribute must be a number";
            throw std::invalid_argument("num_channels attribute must be a number");
        }
        double num_channels = *attrs.at("num_channels").get<double>();
        if (num_channels <= 0) {
            VIAM_SDK_LOG(error) << "[validate] num_channels must be greater than zero";
            throw std::invalid_argument(" num_channels must be greater than zero");
        }

    }
    if(attrs.count("latency")) {
        if (!attrs["latency"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] latency attribute must be a number";
            throw std::invalid_argument("latency attribute must be a number");
        }
        double latency_ms = *attrs.at("latency").get<double>();
        if (latency_ms < 0) {
            VIAM_SDK_LOG(error) << "[validate] latency must be non-negative";
            throw std::invalid_argument("latency must be non-negative");
        }
    }
    return {};
}

void Microphone::reconfigure(const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg) {
    VIAM_SDK_LOG(info) << "[reconfigure] Microphone reconfigure start";

    try {
        //
        // Warn if reconfiguring with active streams
        // Changing the sample rate or number of channels mid stream
        // might cause issues client side, clients need to be actively
        // checking the audioinfo for changes. Changing these parameters
        // may also cause a small gap in audio.
        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            if (active_streams_ > 0) {
                VIAM_SDK_LOG(info) << "[reconfigure] Reconfiguring with " << active_streams_
                                   << " active stream(s). See README for reconfiguration considerations.";
            }
        }


        auto cfg_params = audio::utils::parseConfigAttributes(cfg);

        auto params = audio::utils::setupStreamFromConfig(
            cfg_params,
            audio::utils::StreamDirection::Input,
            AudioCallback,
            pa_
        );

        // Create audio context with actual sample rate/channels from params
        vsdk::audio_info info{vsdk::audio_codecs::PCM_16, params.sample_rate, params.num_channels};
        int samples_per_chunk = params.sample_rate * audio::CHUNK_DURATION_SECONDS;
        auto new_audio_context = std::make_shared<audio::AudioStreamContext>(info, samples_per_chunk);

        // Set user_data to point to the audio context
        params.user_data = new_audio_context.get();

        // Set new configuration and restart stream under lock
        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            device_name_ = params.device_name;
            device_index_ = params.device_index;
            sample_rate_ = params.sample_rate;
            num_channels_ = params.num_channels;
            latency_ = params.latency_seconds;
            audio_context_ = new_audio_context;

            audio::utils::restart_stream(stream_, params, pa_);
        }
        VIAM_SDK_LOG(info) << "[reconfigure] Reconfigure completed successfully";
    } catch (const std::exception& e) {
        VIAM_SDK_LOG(error) << "[reconfigure] Reconfigure failed: " << e.what();
        throw;
    }
}

viam::sdk::ProtoStruct Microphone::do_command(const viam::sdk::ProtoStruct& command) {
    VIAM_SDK_LOG(error) << "do_command not implemented";
    return viam::sdk::ProtoStruct();
}

void Microphone::get_audio(std::string const& codec,
                           std::function<bool(vsdk::AudioIn::audio_chunk&& chunk)> const& chunk_handler,
                           double const& duration_seconds,
                           int64_t const& previous_timestamp,
                           const viam::sdk::ProtoStruct& extra) {

    //TODO: get audio starting from prev timestamp

    // Validate codec is supported
    if (codec != vsdk::audio_codecs::PCM_16) {
        VIAM_SDK_LOG(error) << "Unsupported codec: " + codec +
            ". Supported codecs: pcm16";
        throw std::invalid_argument("Unsupported codec: " + codec +
            ". Supported codecs: pcm16");
    }

    VIAM_SDK_LOG(info) << "get_audio called with codec: " << codec;

    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        active_streams_++;
    }

    // Set duration timer
    auto start_time = std::chrono::steady_clock::time_point();
    auto end_time = std::chrono::steady_clock::time_point::max();
    bool timer_started = false;

    uint64_t sequence = 0;

    // Track which context we're reading from to detect any device config changes
    std::shared_ptr<audio::AudioStreamContext> stream_context;
    uint64_t read_position = 0;

    // Initialize read position to current write position to get most recent audio
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        if (audio_context_) {
            read_position = audio_context_->get_write_position();
            stream_context = audio_context_;
        }
    }

    // Get sample rate and channels - will be updated if context changes
    int stream_sample_rate = 0;
    int stream_num_channels = 0;
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        stream_sample_rate = sample_rate_;
        stream_num_channels = num_channels_;
    }
    int samples_per_chunk = (stream_sample_rate * audio::CHUNK_DURATION_SECONDS) * stream_num_channels;
    if (samples_per_chunk <= 0){
        std::ostringstream buffer;
        buffer << "calculated invalid samples_per_chunk: " << samples_per_chunk <<
        " with sample rate: " << stream_sample_rate << " num channels: " << stream_num_channels
        << " chunk duration seconds: " << CHUNK_DURATION_SECONDS;
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }

    while (std::chrono::steady_clock::now() < end_time) {
        // Check if audio_context_ changed (device reconfigured)
        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);

            // Detect context change (device reconfigured)
            if (audio_context_ != stream_context) {
                if (stream_context != nullptr) {
                    VIAM_SDK_LOG(info) << "Detected stream change (device reconfigure)";

                    // Update sample rate and channels from new config
                    stream_sample_rate = sample_rate_;
                    stream_num_channels = num_channels_;
                    samples_per_chunk = (stream_sample_rate * CHUNK_DURATION_SECONDS) * stream_num_channels;
                }
                // Switch to new context and reset read position
                stream_context = audio_context_;
                read_position = stream_context->get_write_position();
                // Brief gap in audio, but stream continues
            }
        }

        // Check if we have enough samples for a full chunk
        uint64_t write_pos = stream_context->get_write_position();
        uint64_t available_samples = write_pos - read_position;

        // Wait until we have a full chunk worth of samples
        if (available_samples < samples_per_chunk) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::vector<int16_t> temp_buffer(samples_per_chunk);
        uint64_t chunk_start_position = read_position;
        // Read exactly one chunk worth of samples
        int samples_read = stream_context->read_samples(temp_buffer.data(), samples_per_chunk, read_position);

        if (samples_read < samples_per_chunk) {
            // Shouldn't happen since we checked available_samples, but to be safe
            VIAM_SDK_LOG(warn) << "Read fewer samples than expected: " << samples_read << " vs " << samples_per_chunk;
            continue;
        }

        vsdk::AudioIn::audio_chunk chunk;
        chunk.audio_data.resize(samples_read * sizeof(int16_t));
        std::memcpy(chunk.audio_data.data(), temp_buffer.data(), samples_read * sizeof(int16_t));

        chunk.info.codec = codec;
        chunk.info.sample_rate_hz = stream_sample_rate;
        chunk.info.num_channels = stream_num_channels;
        chunk.sequence_number = sequence++;

        // Calculate timestamps based on sample position in stream
        chunk.start_timestamp_ns = calculate_sample_timestamp(
            *stream_context,
            chunk_start_position
        );
        chunk.end_timestamp_ns = calculate_sample_timestamp(
            *stream_context,
            chunk_start_position + samples_read
        );

        // Start duration timer after first chunk arrives
        if (!timer_started && duration_seconds > 0) {
            start_time = std::chrono::steady_clock::now();
            end_time = start_time + std::chrono::milliseconds(static_cast<int64_t>(duration_seconds * 1000));
            timer_started = true;
        }

        if (!chunk_handler(std::move(chunk))) {
            // if the chunk callback returned false, the stream has ended
            VIAM_RESOURCE_LOG(info) << "Chunk handler returned false, stopping";
            {
                std::lock_guard<std::mutex> lock(stream_ctx_mu_);
                active_streams_--;
            }
            return;
        }
    }

    VIAM_SDK_LOG(info) << "get_audio stream completed";

    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        active_streams_--;
    }
}

viam::sdk::audio_properties Microphone::get_properties(const viam::sdk::ProtoStruct& extra){
    viam::sdk::audio_properties props;

    props.supported_codecs = {
        vsdk::audio_codecs::PCM_16
    };
    std::lock_guard<std::mutex> lock(stream_ctx_mu_);
    props.sample_rate_hz = sample_rate_;
    props.num_channels = num_channels_;

    return props;
}

std::vector<viam::sdk::GeometryConfig> Microphone::get_geometries(const viam::sdk::ProtoStruct& extra) {
    throw std::runtime_error("get_geometries is unimplemented");
}



void Microphone::openStream(PaStream*& stream) {
    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa_ ? *pa_ : real_pa;

    VIAM_SDK_LOG(debug) << "Opening stream for device '" << device_name_
                         << "' (index " << device_index_ << ")"
                         << " with sample rate: " << sample_rate_
                         << ", channels: " << num_channels_;

    // Setup stream parameters
    PaStreamParameters params;
    params.device = device_index_;
    params.channelCount = num_channels_;
    params.sampleFormat = paInt16;
    params.suggestedLatency = latency_;
    params.hostApiSpecificStreamInfo = nullptr;  // Must be NULL if not used

    PaError err = audio_interface.isFormatSupported(&params, nullptr, sample_rate_);
    if (err != paNoError) {
        std::ostringstream buffer;
        buffer << "Audio format not supported by device '" << device_name_
               << "' (index " << device_index_ << "): "
               << Pa_GetErrorText(err) << "\n"
               << "Requested configuration:\n"
               << "  - Sample rate: " << sample_rate_ << " Hz\n"
               << "  - Channels: " << num_channels_ << "\n"
               << "  - Format: 16-bit PCM\n"
               << "  - Latency: " << latency_ << " seconds";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }

    VIAM_SDK_LOG(info) << "Opening stream for device '" << device_name_
                       << "' (index " << device_index_ << ")"
                       << " with sample rate " << sample_rate_
                       << " and latency " << params.suggestedLatency << " seconds";

    err = audio_interface.openStream(
        &stream,
        &params,              // input params
        NULL,                 // output params
        sample_rate_,
        paFramesPerBufferUnspecified, // let portaudio pick the frames per buffer
        paNoFlag,            // stream flags - enable default clipping behavior
        AudioCallback,
        audio_context_.get() // user data to pass through to callback
    );

    if (err != paNoError) {
        std::ostringstream buffer;
        buffer << "Failed to open audio stream for device '" << device_name_
               << "' (index " << device_index_ << "): "
               << Pa_GetErrorText(err)
               << " (sample_rate=" << sample_rate_
               << ", channels=" << num_channels_
               << ", latency=" << params.suggestedLatency << "s)";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }
}

void Microphone::startStream(PaStream* stream) {
    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa_ ? *pa_ : real_pa;

    PaError err = audio_interface.startStream(stream);
    if (err != paNoError) {
        audio_interface.closeStream(stream);
        VIAM_SDK_LOG(error) << "Failed to start audio stream: " << Pa_GetErrorText(err);
        throw std::runtime_error(std::string("Failed to start audio stream: ") + Pa_GetErrorText(err));
    }
}

PaDeviceIndex findDeviceByName(const std::string& name, const audio::portaudio::PortAudioInterface& pa) {
    int deviceCount = pa.getDeviceCount();
     if (deviceCount < 0) {
          return paNoDevice;
      }

      for (PaDeviceIndex i = 0; i< deviceCount; i++) {
        const PaDeviceInfo* info = pa.getDeviceInfo(i);
         if (!info) {
            VIAM_SDK_LOG(warn) << "could not get device info for device index " << i << ", skipping";
            continue;
        }

        if (name == info->name) {
            // input and output devices can have the same name so check that it has input channels.
            if (info->maxInputChannels > 0) {
                return i;
            }
        }
    }
    return paNoDevice;

}


/**
 * PortAudio callback function - runs on real-time audio thread.
 *  This function must not:
 * - Allocate memory (malloc/new)
 * - Access the file system
 * - Call any functions that may block
 * - Take unpredictable amounts of time to complete
 *
 */
// outputBuffer used for playback of audio - unused for microphone
int AudioCallback(const void *inputBuffer, void *outputBuffer,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void *userData)
{
    if (!userData) {
        // something wrong, stop stream
        return paAbort;
    }
    audio::InputStreamContext* ctx = static_cast<audio::InputStreamContext*>(userData);

    if (!ctx) {
        // something wrong, stop stream
        return paAbort;
    }

    if (inputBuffer == nullptr) {
        return paContinue;
    }

    const int16_t* input = static_cast<const int16_t*>(inputBuffer);

    // First callback: establish anchor between PortAudio time and wall-clock time
    if (!ctx->first_callback_captured.load()) {
        // the inputBufferADCTime describes the time when the
        // first sample of the input buffer was captured,
        // synced with the clock of the device
        ctx->first_sample_adc_time = timeInfo->inputBufferAdcTime;
        ctx->stream_start_time = std::chrono::system_clock::now();
        ctx->first_callback_captured.store(true);
    }

    int total_samples = framesPerBuffer * ctx->info.num_channels;

    for (int i = 0; i < total_samples; ++i) {
        ctx->write_sample(input[i]);
    }

    return paContinue;
}



} // namespace microphone
