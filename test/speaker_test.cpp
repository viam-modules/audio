#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/config/resource.hpp>
#include "speaker.hpp"
#include "test_utils.hpp"

using namespace viam::sdk;
using namespace audio;

class SpeakerTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override { instance_ = std::make_unique<viam::sdk::Instance>(); }

  void TearDown() override { instance_.reset(); }

private:
  std::unique_ptr<viam::sdk::Instance> instance_;
};

class SpeakerTest: public test_utils::AudioTestBase {
protected:
    void SetUp() override {
        AudioTestBase::SetUp();

        test_name_ = "test_audioout";
        test_deps_ = Dependencies{};

        auto attributes = ProtoStruct{};
        test_config_ = std::make_unique<ResourceConfig>(
            "rdk:component:audioout", "", test_name_, attributes, "",
            Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info
        );

        SetupDefaultPortAudioBehavior();
    }

    std::string test_name_;
    Dependencies test_deps_;
    std::unique_ptr<ResourceConfig> test_config_;
};


TEST_F(SpeakerTest, ValidateWithValidConfig) {
  auto attributes = ProtoStruct{};

  ResourceConfig valid_config(
      "rdk:component:audioout", "", test_name_, attributes, "",
      Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info);

  EXPECT_NO_THROW({
    auto result = speaker::Speaker::validate(valid_config);
    EXPECT_TRUE(result.empty());
  });
}

TEST_F(SpeakerTest, ValidateWithValidOptionalAttributes) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = test_name_;
  attributes["latency"] = 1.0;

  ResourceConfig valid_config(
      "rdk:component:speaker", "", test_name_, attributes, "",
      Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info);

  EXPECT_NO_THROW({
    auto result = speaker::Speaker::validate(valid_config);
    EXPECT_TRUE(result.empty());
  });
}

TEST_F(SpeakerTest, ValidateWithDeviceNameNotString) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = 2;
  attributes["latency"] = 1.0;

  ResourceConfig invalid_config(
      "rdk:component:speaker", "", test_name_, attributes, "",
      Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info);

  EXPECT_THROW({
    speaker::Speaker::validate(invalid_config); },
    std::invalid_argument);
}


TEST_F(SpeakerTest, ValidateWithLatencyNotDouble) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = test_name_;
  attributes["latency"] = "2";

  ResourceConfig invalid_config(
      "rdk:component:speaker", "", test_name_, attributes, "",
      Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info);

  EXPECT_THROW({
    speaker::Speaker::validate(invalid_config); },
    std::invalid_argument);
}


TEST_F(SpeakerTest, GetPropertiesReturnsCorrectValues) {
    int sample_rate = 48000;
    int num_channels = 2;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
        Model("viam", "audio", "speaker"),
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};

    speaker::Speaker speaker(deps, config, mock_pa_.get());

    ProtoStruct extra{};
    auto props = speaker.get_properties(extra);

    EXPECT_EQ(props.sample_rate_hz, sample_rate);
    EXPECT_EQ(props.num_channels, num_channels);
    ASSERT_EQ(props.supported_codecs.size(), 1);
    EXPECT_EQ(props.supported_codecs[0], viam::sdk::audio_codecs::PCM_16);

}



int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new SpeakerTestEnvironment);
  return RUN_ALL_TESTS();
}


using namespace viam::sdk;
using namespace audio;



