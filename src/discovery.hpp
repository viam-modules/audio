#pragma once
#include <memory>

#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include <viam/sdk/services/discovery.hpp>

namespace discovery {

class AudioDiscovery : public viam::sdk::Discovery {
   public:
    explicit AudioDiscovery(viam::sdk::Dependencies dependencies, viam::sdk::ResourceConfig configuration);
    std::vector<viam::sdk::ResourceConfig> discover_resources(const viam::sdk::ProtoStruct& extra) override;
    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    static viam::sdk::Model model;

   private:
};
}  // namespace discovery
