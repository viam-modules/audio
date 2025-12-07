#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include "mp3_decoder.hpp"
#include "mp3_encoder.hpp"
#include "test_utils.hpp"

using namespace speaker;
using namespace microphone;

class MP3DecoderTest : public ::testing::Test {
protected:
    MP3DecoderContext decoder_ctx_;
    MP3EncoderContext encoder_ctx_;

    void TearDown() override {
        cleanup_mp3_decoder(decoder_ctx_);
        cleanup_mp3_encoder(encoder_ctx_);
    }

    // Helper to create test audio samples
    std::vector<int16_t> create_test_samples(int num_samples) {
        std::vector<int16_t> samples(num_samples);
        for (int i = 0; i < num_samples; i++) {
            // Create a simple sine-wave-like pattern for testing
            samples[i] = static_cast<int16_t>((i % 1000) * 32);
        }
        return samples;
    }

    // Helper to encode PCM samples to MP3 for testing
    std::vector<uint8_t> encode_to_mp3(const std::vector<int16_t>& samples, int sample_rate, int num_channels) {
        initialize_mp3_encoder(encoder_ctx_, sample_rate, num_channels);
        std::vector<uint8_t> encoded_data;
        encode_samples_to_mp3(encoder_ctx_, const_cast<int16_t*>(samples.data()), samples.size(), 0, encoded_data);
        flush_mp3_encoder(encoder_ctx_, encoded_data);
        return encoded_data;
    }
};

TEST_F(MP3DecoderTest, InitializeSucceeds) {
    ASSERT_NO_THROW(initialize_mp3_decoder(decoder_ctx_));
    EXPECT_NE(decoder_ctx_.decoder, nullptr);
}

TEST_F(MP3DecoderTest, InitializeSetsDefaultProperties) {
    initialize_mp3_decoder(decoder_ctx_);

    // Initially, sample rate and channels should be 0 until first decode
    EXPECT_EQ(decoder_ctx_.sample_rate, 0);
    EXPECT_EQ(decoder_ctx_.num_channels, 0);
}

TEST_F(MP3DecoderTest, DecodeMonoMP3) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    // Create test samples and encode them
    auto test_samples = create_test_samples(1152);
    auto encoded_data = encode_to_mp3(test_samples, sample_rate, num_channels);

    // Initialize decoder and decode
    initialize_mp3_decoder(decoder_ctx_);
    std::vector<uint8_t> decoded_data;

    ASSERT_NO_THROW(decode_mp3_to_pcm16(decoder_ctx_, encoded_data, decoded_data));

    // Verify decoder populated audio properties
    EXPECT_EQ(decoder_ctx_.sample_rate, sample_rate);
    EXPECT_EQ(decoder_ctx_.num_channels, num_channels);

    // Verify we got some decoded data
    EXPECT_FALSE(decoded_data.empty());

    // Verify the size is reasonable (should be approximately the same as input)
    int decoded_samples = decoded_data.size() / sizeof(int16_t);
    EXPECT_GT(decoded_samples, 0);
}

TEST_F(MP3DecoderTest, DecodeStereoMP3) {
    const int sample_rate = 44100;
    const int num_channels = 2;

    // Create test samples and encode them (stereo needs twice as many samples)
    auto test_samples = create_test_samples(1152 * 2);
    auto encoded_data = encode_to_mp3(test_samples, sample_rate, num_channels);

    // Initialize decoder and decode
    initialize_mp3_decoder(decoder_ctx_);
    std::vector<uint8_t> decoded_data;

    ASSERT_NO_THROW(decode_mp3_to_pcm16(decoder_ctx_, encoded_data, decoded_data));

    // Verify decoder populated audio properties
    EXPECT_EQ(decoder_ctx_.sample_rate, sample_rate);
    EXPECT_EQ(decoder_ctx_.num_channels, num_channels);

    // Verify we got some decoded data
    EXPECT_FALSE(decoded_data.empty());
}

TEST_F(MP3DecoderTest, DecodeMultipleFrames) {
    const int sample_rate = 48000;
    const int num_channels = 2;

    // Create multiple frames worth of data
    auto test_samples = create_test_samples(1152 * 4 * 2);  // 4 frames, stereo
    auto encoded_data = encode_to_mp3(test_samples, sample_rate, num_channels);

    initialize_mp3_decoder(decoder_ctx_);
    std::vector<uint8_t> decoded_data;

    ASSERT_NO_THROW(decode_mp3_to_pcm16(decoder_ctx_, encoded_data, decoded_data));

    EXPECT_FALSE(decoded_data.empty());
    EXPECT_EQ(decoder_ctx_.sample_rate, sample_rate);
    EXPECT_EQ(decoder_ctx_.num_channels, num_channels);
}

TEST_F(MP3DecoderTest, DecodeEmptyData) {
    initialize_mp3_decoder(decoder_ctx_);

    std::vector<uint8_t> empty_data;
    std::vector<uint8_t> decoded_data;

    // Should return without error but not decode anything
    ASSERT_NO_THROW(decode_mp3_to_pcm16(decoder_ctx_, empty_data, decoded_data));
    EXPECT_TRUE(decoded_data.empty());
}

TEST_F(MP3DecoderTest, DecodeWithoutInitialization) {
    auto test_samples = create_test_samples(1152);
    auto encoded_data = encode_to_mp3(test_samples, 48000, 1);

    std::vector<uint8_t> decoded_data;

    // Should throw because decoder is not initialized
    EXPECT_THROW(
        decode_mp3_to_pcm16(decoder_ctx_, encoded_data, decoded_data),
        std::runtime_error
    );
}

TEST_F(MP3DecoderTest, DecodeInvalidMP3Data) {
    initialize_mp3_decoder(decoder_ctx_);

    // Create some random invalid data
    std::vector<uint8_t> invalid_data = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA};
    std::vector<uint8_t> decoded_data;

    // Should throw or handle gracefully
    EXPECT_THROW(
        decode_mp3_to_pcm16(decoder_ctx_, invalid_data, decoded_data),
        std::runtime_error
    );
}

TEST_F(MP3DecoderTest, CleanupDecoder) {
    initialize_mp3_decoder(decoder_ctx_);

    EXPECT_NE(decoder_ctx_.decoder, nullptr);

    cleanup_mp3_decoder(decoder_ctx_);

    EXPECT_EQ(decoder_ctx_.decoder, nullptr);
    EXPECT_EQ(decoder_ctx_.sample_rate, 0);
    EXPECT_EQ(decoder_ctx_.num_channels, 0);
}

TEST_F(MP3DecoderTest, DecodeConsecutiveChunks) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    initialize_mp3_decoder(decoder_ctx_);

    // Decode first chunk
    auto samples1 = create_test_samples(1152);
    auto encoded1 = encode_to_mp3(samples1, sample_rate, num_channels);
    std::vector<uint8_t> decoded1;
    decode_mp3_to_pcm16(decoder_ctx_, encoded1, decoded1);

    EXPECT_FALSE(decoded1.empty());
    EXPECT_EQ(decoder_ctx_.sample_rate, sample_rate);

    // Cleanup and reinitialize for second chunk
    cleanup_mp3_decoder(decoder_ctx_);
    initialize_mp3_decoder(decoder_ctx_);

    // Decode second chunk
    auto samples2 = create_test_samples(2304);
    auto encoded2 = encode_to_mp3(samples2, sample_rate, num_channels);
    std::vector<uint8_t> decoded2;
    decode_mp3_to_pcm16(decoder_ctx_, encoded2, decoded2);

    EXPECT_FALSE(decoded2.empty());
}

TEST_F(MP3DecoderTest, DecodeDifferentSampleRates) {
    // Test 44.1kHz
    {
        initialize_mp3_decoder(decoder_ctx_);
        auto samples = create_test_samples(1152);
        auto encoded = encode_to_mp3(samples, 44100, 1);
        std::vector<uint8_t> decoded;
        decode_mp3_to_pcm16(decoder_ctx_, encoded, decoded);
        EXPECT_EQ(decoder_ctx_.sample_rate, 44100);
        cleanup_mp3_decoder(decoder_ctx_);
    }

    // Test 16kHz
    {
        initialize_mp3_decoder(decoder_ctx_);
        auto samples = create_test_samples(1152);
        auto encoded = encode_to_mp3(samples, 16000, 1);
        std::vector<uint8_t> decoded;
        decode_mp3_to_pcm16(decoder_ctx_, encoded, decoded);
        EXPECT_EQ(decoder_ctx_.sample_rate, 16000);
        cleanup_mp3_decoder(decoder_ctx_);
    }

    // Test 8kHz
    {
        initialize_mp3_decoder(decoder_ctx_);
        auto samples = create_test_samples(1152);
        auto encoded = encode_to_mp3(samples, 8000, 1);
        std::vector<uint8_t> decoded;
        decode_mp3_to_pcm16(decoder_ctx_, encoded, decoded);
        EXPECT_EQ(decoder_ctx_.sample_rate, 8000);
        cleanup_mp3_decoder(decoder_ctx_);
    }
}

TEST_F(MP3DecoderTest, DecodeOutputIsInterleavedForStereo) {
    const int sample_rate = 48000;
    const int num_channels = 2;

    auto test_samples = create_test_samples(1152 * 2);
    auto encoded_data = encode_to_mp3(test_samples, sample_rate, num_channels);

    initialize_mp3_decoder(decoder_ctx_);
    std::vector<uint8_t> decoded_data;
    decode_mp3_to_pcm16(decoder_ctx_, encoded_data, decoded_data);

    EXPECT_FALSE(decoded_data.empty());

    // For stereo, decoded data should be interleaved (L, R, L, R, ...)
    // So the number of samples should be divisible by num_channels
    int total_samples = decoded_data.size() / sizeof(int16_t);
    EXPECT_EQ(total_samples % num_channels, 0);
}

TEST_F(MP3DecoderTest, RoundTripEncodeDecode) {
    const int sample_rate = 48000;
    const int num_channels = 1;
    const int num_samples = 1152 * 3;  // 3 frames

    // Create original samples
    auto original_samples = create_test_samples(num_samples);

    // Encode to MP3
    auto encoded_data = encode_to_mp3(original_samples, sample_rate, num_channels);
    EXPECT_FALSE(encoded_data.empty());

    // Decode back to PCM
    initialize_mp3_decoder(decoder_ctx_);
    std::vector<uint8_t> decoded_data;
    decode_mp3_to_pcm16(decoder_ctx_, encoded_data, decoded_data);

    // Verify we got data back
    EXPECT_FALSE(decoded_data.empty());

    // Convert decoded bytes to samples
    const int16_t* decoded_samples = reinterpret_cast<const int16_t*>(decoded_data.data());
    int decoded_sample_count = decoded_data.size() / sizeof(int16_t);

    // Due to encoder delay and MP3 lossy compression, decoded count may differ slightly
    // But should be in the same ballpark
    EXPECT_GT(decoded_sample_count, num_samples * 0.8);  // At least 80% of original
    EXPECT_LT(decoded_sample_count, num_samples * 1.5);  // At most 150% of original
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
    return RUN_ALL_TESTS();
}
