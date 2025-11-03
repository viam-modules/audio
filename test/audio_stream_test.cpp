#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>
#include "audio_stream.hpp"

using namespace microphone;
using namespace viam::sdk;

class AudioStreamContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a basic audio context for testing
        audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 1};  // mono, 44.1kHz
        samples_per_chunk_ = 4410;  // 100ms chunks
        context_ = std::make_unique<AudioStreamContext>(info, samples_per_chunk_);
    }

    void TearDown() override {
        context_.reset();
    }

    // Helper to create a test chunk
    AudioIn::audio_chunk CreateTestChunk(int sequence, int64_t start_ns = 0) {
        AudioIn::audio_chunk chunk;
        chunk.start_timestamp_ns = std::chrono::nanoseconds(start_ns);
        chunk.end_timestamp_ns = std::chrono::nanoseconds(start_ns + 100000000); // +100ms
        chunk.info = context_->info;

        // Create some dummy audio data (100 samples of int16)
        chunk.audio_data.resize(100 * sizeof(int16_t));
        int16_t* samples = reinterpret_cast<int16_t*>(chunk.audio_data.data());
        for (int i = 0; i < 100; i++) {
            samples[i] = static_cast<int16_t>(i * 100 + sequence);
        }

        return chunk;
    }

    std::unique_ptr<AudioStreamContext> context_;
    size_t samples_per_chunk_;
};


TEST_F(AudioStreamContextTest, StereoContextCreation) {
    audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, 2};  // stereo
    size_t samples_per_chunk = 4410;  // 100ms chunks

    AudioStreamContext ctx(info, samples_per_chunk);

    // Verify context is created with correct properties
    EXPECT_EQ(ctx.info.num_channels, 2);
    EXPECT_EQ(ctx.info.sample_rate_hz, 44100);
    EXPECT_EQ(ctx.samples_per_chunk, 4410);
    EXPECT_TRUE(ctx.is_recording.load());
}


TEST_F(AudioStreamContextTest, CircularBufferStartsAtZero) {
    // Circular buffer should start with write position at 0
    EXPECT_EQ(context_->get_write_position(), 0);
}

TEST_F(AudioStreamContextTest, WriteAndReadSamples) {
    // Write some test samples to circular buffer
    std::vector<int16_t> test_samples = {100, 200, 300, 400, 500};

    for (auto sample : test_samples) {
        context_->write_sample(sample);
    }

    // Verify samples were written
    EXPECT_EQ(context_->get_write_position(), test_samples.size());

    // Read samples back (start from position 0)
    std::vector<int16_t> read_buffer(test_samples.size());
    uint64_t read_pos = 0;
    int samples_read = context_->read_samples(read_buffer.data(), test_samples.size(), read_pos);

    EXPECT_EQ(samples_read, test_samples.size());
    EXPECT_EQ(read_pos, test_samples.size());  // Position should have advanced
    EXPECT_EQ(read_buffer, test_samples);
}

TEST_F(AudioStreamContextTest, MultipleReadersIndependent) {
    // Write samples to circular buffer
    const int num_samples = 100;
    for (int i = 0; i < num_samples; i++) {
        context_->write_sample(static_cast<int16_t>(i));
    }

    EXPECT_EQ(context_->get_write_position(), num_samples);

    // Reader 1: Reads all samples
    std::vector<int16_t> buffer1(num_samples);
    uint64_t read_pos1 = 0;
    int samples_read1 = context_->read_samples(buffer1.data(), num_samples, read_pos1);
    EXPECT_EQ(samples_read1, num_samples);
    EXPECT_EQ(read_pos1, num_samples);

    // Reader 2: Can also read the same samples (independent position)
    std::vector<int16_t> buffer2(num_samples);
    uint64_t read_pos2 = 0;
    int samples_read2 = context_->read_samples(buffer2.data(), num_samples, read_pos2);
    EXPECT_EQ(samples_read2, num_samples);
    EXPECT_EQ(read_pos2, num_samples);

    // Both readers got the same data
    EXPECT_EQ(buffer1, buffer2);
}


TEST_F(AudioStreamContextTest, ReadPartialSamples) {
    // Write 100 samples
    const int num_samples = 100;
    for (int i = 0; i < num_samples; i++) {
        context_->write_sample(static_cast<int16_t>(i));
    }

    // Read only 50 samples
    std::vector<int16_t> buffer(50);
    uint64_t read_pos = 0;
    int samples_read = context_->read_samples(buffer.data(), 50, read_pos);

    EXPECT_EQ(samples_read, 50);
    EXPECT_EQ(read_pos, 50);  // Position advanced to 50

    // Read remaining 50 (continue from position 50)
    samples_read = context_->read_samples(buffer.data(), 50, read_pos);
    EXPECT_EQ(samples_read, 50);
    EXPECT_EQ(read_pos, 100);  // Position now at 100
}

TEST_F(AudioStreamContextTest, ConcurrentWriteAndRead) {
    std::atomic<bool> stop{false};
    std::atomic<int> read_total{0};

    const int total_samples = 1000;

    // Producer thread (simulates RT audio callback)
    std::thread producer([&]() {
        for (int i = 0; i < total_samples; i++) {
            context_->write_sample(static_cast<int16_t>(i));
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    });

    // Consumer thread (simulates get_audio)
    std::thread consumer([&]() {
        std::vector<int16_t> buffer(100);
        uint64_t my_read_pos = 0;

        // Keep reading until stopped AND all samples consumed
        while (!stop.load() || my_read_pos < context_->get_write_position()) {
            uint64_t write_pos = context_->get_write_position();
            uint64_t available = write_pos - my_read_pos;

            if (available > 0) {
                int to_read = std::min(available, static_cast<uint64_t>(100));
                int samples_read = context_->read_samples(buffer.data(), to_read, my_read_pos);
                read_total += samples_read;
            } else {
                // No samples available yet, wait a bit
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }
    });

    producer.join();
    stop = true;
    consumer.join();

    // All samples should have been read
    EXPECT_EQ(read_total.load(), total_samples);
}

TEST_F(AudioStreamContextTest, RecordingFlagCanBeToggled) {
    EXPECT_TRUE(context_->is_recording.load());

    context_->is_recording.store(false);
    EXPECT_FALSE(context_->is_recording.load());

    context_->is_recording.store(true);
    EXPECT_TRUE(context_->is_recording.load());
}

TEST_F(AudioStreamContextTest, ReadMoreThanAvailable) {
    // Write only 50 samples
    const int num_samples = 50;
    for (int i = 0; i < num_samples; i++) {
        context_->write_sample(static_cast<int16_t>(i));
    }

    // Try to read 100 samples
    std::vector<int16_t> buffer(100);
    uint64_t read_pos = 0;
    int samples_read = context_->read_samples(buffer.data(), 100, read_pos);

    // Should only get the 50 available samples
    EXPECT_EQ(samples_read, 50);
    EXPECT_EQ(read_pos, 50);
}

TEST_F(AudioStreamContextTest, MultipleSmallReads) {
    // Write 100 samples
    for (int i = 0; i < 100; i++) {
        context_->write_sample(static_cast<int16_t>(i));
    }

    // Read in multiple small chunks
    std::vector<int16_t> buffer(10);
    uint64_t read_pos = 0;
    int total_read = 0;

    for (int i = 0; i < 10; i++) {
        int samples_read = context_->read_samples(buffer.data(), 10, read_pos);
        EXPECT_EQ(samples_read, 10);
        total_read += samples_read;

        // Verify the data is correct
        for (int j = 0; j < 10; j++) {
            EXPECT_EQ(buffer[j], i * 10 + j);
        }
    }

    EXPECT_EQ(total_read, 100);
    EXPECT_EQ(read_pos, 100);
}

TEST_F(AudioStreamContextTest, CalculateSampleTimestamp) {
    // Set up the baseline time
    context_->first_sample_adc_time = 1000.0;
    context_->stream_start_time = std::chrono::system_clock::now();
    context_->first_callback_captured.store(true);
    context_->total_samples_written.store(0);

    auto baseline_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        context_->stream_start_time.time_since_epoch()
    ).count();

    // Test timestamp for sample 0
    auto timestamp1 = calculate_sample_timestamp(context_.get(), 0);
    EXPECT_EQ(timestamp1.count(), baseline_ns);

    // Test timestamp for sample at 1 second (44100 samples at 44.1kHz)
    auto timestamp2 = calculate_sample_timestamp(context_.get(), 44100);
    EXPECT_NEAR(timestamp2.count(), baseline_ns + 1'000'000'000, 1000);  // ~1 second

    // Test timestamp for sample at 0.5 seconds (22050 samples)
    auto timestamp3 = calculate_sample_timestamp(context_.get(), 22050);
    EXPECT_NEAR(timestamp3.count(), baseline_ns + 500'000'000, 1000);  // ~0.5 seconds
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
