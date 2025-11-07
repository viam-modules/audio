#include "discovery.hpp"
#include "portaudio.hpp"
#include "microphone.hpp"
#include <iostream>
#include <sstream>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/services/discovery.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>

namespace discovery {

namespace vsdk = ::viam::sdk;
vsdk::Model AudioDiscovery::model = vsdk::Model("viam", "audio", "discovery");

AudioDiscovery::AudioDiscovery(vsdk::Dependencies dependencies, vsdk::ResourceConfig configuration)
    : Discovery(configuration.name()) {}



std::vector<vsdk::ResourceConfig> AudioDiscovery::discover_resources(const vsdk::ProtoStruct& extra) {
    std::vector<vsdk::ResourceConfig> configs;


    int numDevices = Pa_GetDeviceCount();


    if (numDevices <= 0) {
        VIAM_RESOURCE_LOG(warn) << "No audio devices found during discovery";
        return {};
    }

    VIAM_SDK_LOG(info) << "Discovery found " << numDevices << "audio devices";

    bool input;
    bool output;
    for (int i = 0; i < numDevices; i++) {
          input = false;
          output = false;
          const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
          std::string device_name = info->name;
          double sample_rate = info->defaultSampleRate;
          int num_input_channels = 0;
          int num_output_channels = 0;

          if (info->maxInputChannels > 0) {
                input = true;
                num_input_channels = info->maxInputChannels;
          } else if(info->maxOutputChannels > 0) {
                output = true;
                num_output_channels = info->maxOutputChannels;
          }

           if (input) {
                std::stringstream deviceInfoString;
                deviceInfoString << "Microphone " << (i + 1) << " - Name: " << device_name << ", default sample rate:  " << sample_rate
                                    << ", max channels: " << num_input_channels;
                VIAM_RESOURCE_LOG(info) << deviceInfoString.str();
           }
           if (output) {
               std::stringstream deviceInfoString;
                deviceInfoString << "Speaker " << (i + 1) << " - Name: " << device_name << ", default sample rate:  " << sample_rate
                                    << ", max channels: " << num_output_channels;
                VIAM_RESOURCE_LOG(info) << deviceInfoString.str();
           }

            if (input) {
                vsdk::ProtoStruct attributes;
                attributes.emplace("device_name", device_name);
                attributes.emplace("sample_rate", sample_rate);
                attributes.emplace("num_channels", num_input_channels);

                std::stringstream name;
                name << "microphone-" << i + 1;


                vsdk::ResourceConfig config(
                    "audio-in", std::move(name.str()), "viam", attributes, "rdk:component:audio_in", microphone::Microphone::model, vsdk::log_level::info);
                configs.push_back(config);
            }
            if (output) {
                vsdk::ProtoStruct attributes;
                attributes.emplace("device_name", device_name);
                attributes.emplace("sample_rate", sample_rate);
                attributes.emplace("num_channels", num_output_channels);


                std::stringstream name;
                name << "speaker-" << i + 1;
                    vsdk::ResourceConfig config(
                    "audio-out", std::move(name.str()), "viam", attributes, "rdk:component:audio_out", microphone::Microphone::model, vsdk::log_level::info);
                configs.push_back(config);
            }
    }

    return configs;
}

vsdk::ProtoStruct AudioDiscovery::do_command(const vsdk::ProtoStruct& command) {
    VIAM_RESOURCE_LOG(error) << "do_command not implemented";
    return vsdk::ProtoStruct{};
}


} // namespace discovery
