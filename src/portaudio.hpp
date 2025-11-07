#pragma once

#include "portaudio.h"
#include <ostream>
#include <viam/sdk/config/resource.hpp>


namespace audio {
namespace portaudio {

class PortAudioInterface {
public:
    virtual PaError initialize() = 0;
    virtual PaDeviceIndex getDefaultInputDevice() = 0;
    virtual PaDeviceIndex getDefaultOutputDevice() = 0;
    virtual const PaDeviceInfo* getDeviceInfo(PaDeviceIndex device) = 0;
    virtual PaError openStream(PaStream** stream, const PaStreamParameters* inputParameters,
                               const PaStreamParameters* outputParameters, double sampleRate,
                               unsigned long framesPerBuffer, PaStreamFlags streamFlags,
                               PaStreamCallback* streamCallback, void* userData) = 0;
    virtual PaError startStream(PaStream *stream) = 0;
    virtual PaError terminate() = 0;
    virtual PaError stopStream(PaStream *stream) = 0;
    virtual PaError closeStream(PaStream *stream) = 0;
    virtual PaDeviceIndex getDeviceCount() = 0;
    virtual const PaStreamInfo* getStreamInfo(PaStream *stream) = 0;
    virtual ~PortAudioInterface() = default;
};

// Wrapper around portAudio functions so they can be mocked.
class RealPortAudio : public PortAudioInterface {
public:
    PaError initialize() override {
        return Pa_Initialize();
    }

    PaDeviceIndex getDefaultInputDevice() override {
        return Pa_GetDefaultInputDevice();
    }

    PaDeviceIndex getDefaultOutputDevice() override {
        return Pa_GetDefaultOutputDevice();
    }

    const PaDeviceInfo* getDeviceInfo(PaDeviceIndex device) override {
        return Pa_GetDeviceInfo(device);
    }

    PaError openStream(PaStream** stream, const PaStreamParameters* inputParameters,
                       const PaStreamParameters* outputParameters, double sampleRate,
                       unsigned long framesPerBuffer, PaStreamFlags streamFlags,
                       PaStreamCallback* streamCallback, void* userData) override {
        return Pa_OpenStream(stream, inputParameters, outputParameters, sampleRate,
                           framesPerBuffer, streamFlags, streamCallback, userData);
    }

      PaError startStream(PaStream *stream) override {
         return Pa_StartStream(stream);
      }

      PaError terminate() override {
         return Pa_Terminate();
      }


      PaError stopStream(PaStream *stream) override {
         return Pa_StopStream(stream);
      }

      PaError closeStream(PaStream *stream) override {
         return Pa_CloseStream(stream);
      }

      PaDeviceIndex getDeviceCount() override {
         return Pa_GetDeviceCount();
      }

      const PaStreamInfo* getStreamInfo(PaStream *stream) override {
         return Pa_GetStreamInfo(stream);
      }

};


inline void startPortAudio(audio::portaudio::PortAudioInterface* pa = nullptr) {
    audio::portaudio::RealPortAudio real_pa;
    audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    PaError err = audio_interface.initialize();
    if (err != 0) {
        std::ostringstream buffer;
        buffer << "failed to initialize PortAudio library: " << Pa_GetErrorText(err);
        throw std::runtime_error(buffer.str());
    }

    int numDevices = Pa_GetDeviceCount();
    VIAM_SDK_LOG(info) << "Available devices:";

      for (int i = 0; i < numDevices; i++) {
          const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
          if (info->maxInputChannels > 0) {
              VIAM_SDK_LOG(info) << info->name << " default sample rate: " << info->defaultSampleRate
              << "max input channels: " << info->maxInputChannels;
          }
         if (info->maxOutputChannels > 0) {
              VIAM_SDK_LOG(info) << info->name << " default sample rate: " << info->defaultSampleRate
              << "max input channels: " << info->maxOutputChannels;
          }
      }
}

} // namespace portaudio
} // namespace audio

