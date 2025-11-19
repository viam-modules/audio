#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include "mp3_encoder.hpp"

using namespace microphone;

class MP3EncoderTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        instance_ = std::make_unique<viam::sdk::Instance>();
    }
    void TearDown() override {
        instance_.reset();
    }
private:
    std::unique_ptr<viam::sdk::Instance> instance_;
};
class MP3EncoderTest : public ::testing::Test {
protected:
    MP3EncoderContext ctx_;

    void TearDown() override {
        cleanup_mp3_encoder(ctx_);
    }

    // Helper to create test audio samples (simple sine-like wave)
    std::vector<int16_t> create_test_samples(int num_samples) {
        std::vector<int16_t> samples(num_samples);
        for (int i = 0; i < num_samples; i++) {
            samples[i] = i;
        }
        return samples;
    }
};

TEST_F(MP3EncoderTest, InitializeSucceeds) {
    ASSERT_NO_THROW(initialize_mp3_encoder(ctx_, 48000, 2));

    EXPECT_NE(ctx_.encoder, nullptr);
    EXPECT_EQ(ctx_.sample_rate, 48000);
    EXPECT_EQ(ctx_.num_channels, 2);
    EXPECT_TRUE(ctx_.buffer.empty());
}

// Test encoding small amount of samples (less than one MP3 frame)
TEST_F(MP3EncoderTest, EncodeIncompleteMp3Frame) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    // This is less than 1152 samples needed for one MP3 frame
    auto samples = create_test_samples(500 * 2);
    std::vector<uint8_t> output;

    buffer_and_encode_samples(ctx_, samples.data(), samples.size(), 0, output);

    // Should buffer the samples but not produce output yet
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(ctx_.buffer.size(), samples.size());
}

// Test encoding exactly one MP3 frame
TEST_F(MP3EncoderTest, EncodeOneCompleteMp3Frame) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    // Create exactly 1152 frames * 2 channels = 2304 samples
    auto samples = create_test_samples(1152 * 2);
    std::vector<uint8_t> output;

    buffer_and_encode_samples(ctx_, samples.data(), samples.size(), 0, output);

    // Buffer should be empty (all samples consumed into one frame)
    EXPECT_TRUE(ctx_.buffer.empty());

    // MP3 encoder may not output yet due to lookahead buffering
    // Send more frames to force output
    auto more_samples = create_test_samples(1152 * 4 * 2);
    buffer_and_encode_samples(ctx_, more_samples.data(), more_samples.size(), samples.size(), output);
    EXPECT_FALSE(output.empty());
}
TEST_F(MP3EncoderTest, EncodeMultipleMp3Frames) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    // Create 3.5 frames worth of data
    // 1152 * 3.5 = 4032 frames * 2 channels = 8064 samples
    auto samples = create_test_samples(4032 * 2);
    std::vector<uint8_t> output;

    buffer_and_encode_samples(ctx_, samples.data(), samples.size(), 0, output);
    EXPECT_FALSE(output.empty());
    // Buffer should contain the leftover 0.5 frame
    EXPECT_EQ(ctx_.buffer.size(), 576 * 2);
}

TEST_F(MP3EncoderTest, AccumulateAcrossMultipleCalls) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    std::vector<uint8_t> output;

    // First call: 500 samples (not enough for a frame)
    auto samples1 = create_test_samples(500 * 2);
    buffer_and_encode_samples(ctx_, samples1.data(), samples1.size(), 0, output);
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(ctx_.buffer.size(), 500 * 2);

    // Second call: 700 more samples (total = 1200, enough for 1 frame)
    auto samples2 = create_test_samples(700 * 2);
    buffer_and_encode_samples(ctx_, samples2.data(), samples2.size(), 500 * 2, output);

    // MP3 encoder uses lookahead buffering - may not output immediately
    // Just verify buffer state is correct (1200 - 1152 = 48 samples left)
    EXPECT_EQ(ctx_.buffer.size(), 48 * 2);

    // Send more frames to force output from lookahead buffer
    auto samples3 = create_test_samples(1152 * 5 * 2);  // 5 more frames
    buffer_and_encode_samples(ctx_, samples3.data(), samples3.size(), 1200 * 2, output);
    EXPECT_FALSE(output.empty()) << "Should have MP3 output after sending multiple frames";
}

TEST_F(MP3EncoderTest, FlushEncoder) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    auto samples = create_test_samples(1152 * 5 * 2);
    std::vector<uint8_t> output;
    buffer_and_encode_samples(ctx_, samples.data(), samples.size(), 0, output);

    // Flush should retrieve any buffered packets from the encoder
    std::vector<uint8_t> flush_output;
    flush_mp3_encoder(ctx_, flush_output);
    EXPECT_FALSE(flush_output.empty());
}

TEST_F(MP3EncoderTest, CleanupEncoder) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    EXPECT_NE(ctx_.encoder, nullptr);

    cleanup_mp3_encoder(ctx_);

    EXPECT_EQ(ctx_.encoder, nullptr);
    EXPECT_EQ(ctx_.sample_rate, 0);
    EXPECT_EQ(ctx_.num_channels, 0);
    EXPECT_TRUE(ctx_.buffer.empty());
}

TEST_F(MP3EncoderTest, EncodeWithoutInitialization) {
    auto samples = create_test_samples(1152 * 2);
    std::vector<uint8_t> output;

    // Should throw because encoder is not initialized
    EXPECT_THROW(
        buffer_and_encode_samples(ctx_, samples.data(), samples.size(), 0, output),
        std::runtime_error
    );
}

// Test position tracking
TEST_F(MP3EncoderTest, PositionTrackingWithBuffering) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    std::vector<uint8_t> output;

    // First chunk: 500 samples starting at position 0
    auto samples1 = create_test_samples(500 * 2);
    buffer_and_encode_samples(ctx_, samples1.data(), samples1.size(), 0, output);

    // Buffer should start at position 0
    EXPECT_EQ(ctx_.buffer_start_position, 0);
    EXPECT_EQ(ctx_.buffer.size(), 500 * 2);

    // Second chunk: 700 samples starting at position 1000
    auto samples2 = create_test_samples(700 * 2);
    buffer_and_encode_samples(ctx_, samples2.data(), samples2.size(), 500 * 2, output);

    // One frame (1152 samples) should have been encoded, leaving 48 samples
    // Buffer start should now be at 1152 * 2 = 2304
    EXPECT_EQ(ctx_.buffer_start_position, 1152 * 2);
    EXPECT_EQ(ctx_.buffer.size(), 48 * 2);
    EXPECT_EQ(ctx_.total_samples_encoded, 1152 * 2);
}

TEST_F(MP3EncoderTest, PositionTrackingMultipleFrames) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    std::vector<uint8_t> output;

    // Encode 3 complete frames
    auto samples = create_test_samples(1152 * 3 * 2);
    buffer_and_encode_samples(ctx_, samples.data(), samples.size(), 0, output);

    // All frames encoded, buffer should be empty
    EXPECT_TRUE(ctx_.buffer.empty());
    EXPECT_EQ(ctx_.total_samples_encoded, 1152 * 3 * 2);
}

// Test edge cases
TEST_F(MP3EncoderTest, EncodeEmptyInput) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    std::vector<uint8_t> output;
    std::vector<int16_t> empty_samples;

    // Should handle empty input gracefully
    EXPECT_NO_THROW(
        buffer_and_encode_samples(ctx_, empty_samples.data(), 0, 0, output)
    );
    EXPECT_TRUE(output.empty());
    EXPECT_TRUE(ctx_.buffer.empty());
}

TEST_F(MP3EncoderTest, EncodeSingleSample) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    std::vector<uint8_t> output;
    auto samples = create_test_samples(2);  // 1 frame, 2 channels

    buffer_and_encode_samples(ctx_, samples.data(), samples.size(), 0, output);

    // Should buffer the single sample
    EXPECT_TRUE(output.empty());
    EXPECT_EQ(ctx_.buffer.size(), 2);
}

// Test different configurations
TEST_F(MP3EncoderTest, InitializeMonoChannel) {
    ASSERT_NO_THROW(initialize_mp3_encoder(ctx_, 48000, 1));

    EXPECT_EQ(ctx_.num_channels, 1);
    EXPECT_EQ(ctx_.sample_rate, 48000);

    // Encode one frame of mono audio
    auto samples = create_test_samples(1152);
    std::vector<uint8_t> output;
    buffer_and_encode_samples(ctx_, samples.data(), samples.size(), 0, output);

    EXPECT_TRUE(ctx_.buffer.empty());
}

TEST_F(MP3EncoderTest, InitializeDifferentSampleRates) {
    // Test 44.1kHz
    ASSERT_NO_THROW(initialize_mp3_encoder(ctx_, 44100, 2));
    EXPECT_EQ(ctx_.sample_rate, 44100);
    cleanup_mp3_encoder(ctx_);

    // Test 16kHz
    ASSERT_NO_THROW(initialize_mp3_encoder(ctx_, 16000, 2));
    EXPECT_EQ(ctx_.sample_rate, 16000);
    cleanup_mp3_encoder(ctx_);

    // Test 8kHz
    ASSERT_NO_THROW(initialize_mp3_encoder(ctx_, 8000, 1));
    EXPECT_EQ(ctx_.sample_rate, 8000);
    EXPECT_EQ(ctx_.num_channels, 1);
}

// Test flushing with buffered samples
TEST_F(MP3EncoderTest, FlushWithBufferedSamples) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    // Encode less than one frame so samples remain in buffer
    auto samples = create_test_samples(500 * 2);
    std::vector<uint8_t> output;
    buffer_and_encode_samples(ctx_, samples.data(), samples.size(), 0, output);

    EXPECT_EQ(ctx_.buffer.size(), 500 * 2);

    // Flush should process remaining packets
    std::vector<uint8_t> flush_output;
    flush_mp3_encoder(ctx_, flush_output);

    EXPECT_EQ(ctx_.buffer.size(), 0);
}

TEST_F(MP3EncoderTest, FlushAfterMultipleEncodings) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    std::vector<uint8_t> output;

    // Encode multiple frames
    for (int i = 0; i < 5; i++) {
        auto samples = create_test_samples(1152 * 2);
        buffer_and_encode_samples(ctx_, samples.data(), samples.size(), i * 1152 * 2, output);
    }

    size_t output_before_flush = output.size();

    std::vector<uint8_t> flush_output;
    flush_mp3_encoder(ctx_, flush_output);

    // Should get some additional data from flushing
    EXPECT_FALSE(flush_output.empty());
}


TEST_F(MP3EncoderTest, MultipleCleanupCalls) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    cleanup_mp3_encoder(ctx_);
    EXPECT_EQ(ctx_.encoder, nullptr);

}


TEST_F(MP3EncoderTest, FlushUninitializedEncoder) {
    std::vector<uint8_t> output;

    EXPECT_THROW(
        flush_mp3_encoder(ctx_, output),
        std::invalid_argument
    );
}

  TEST_F(MP3EncoderTest, DeinterleaveSamples) {
      std::vector<int16_t> interleaved = {1, 2, 3, 4, 5, 6, 7, 8};
      std::vector<int16_t> left, right;

      deinterleave_samples(interleaved, left, right);

      EXPECT_EQ(left.size(), 4);
      EXPECT_EQ(right.size(), 4);
      EXPECT_EQ(left[0], 1);
      EXPECT_EQ(right[0], 2);
      EXPECT_EQ(left[1], 3);
      EXPECT_EQ(right[1], 4);
  }


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new MP3EncoderTestEnvironment);
  return RUN_ALL_TESTS();
}
