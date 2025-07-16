#include <gtest/gtest.h>

#include <memory>
#include <numeric>
#include <vector>
#include <thread>
#include <csignal>
#include <cstddef>
#include <iostream>

#include <unistd.h>

#include "pipe.h"
#include "utils.h"
#include "exception.h"

void handle_sigpipe(int signal) {
    // Do nothing
}

using namespace coj;

class PipeTest : public ::testing::Test {
protected:
    void SetUp() override {
        struct sigaction sa;
        sa.sa_handler = handle_sigpipe;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGPIPE, &sa, NULL);

        int fds[2];
        if (::pipe(fds) == -1) {
            FAIL() << "Failed to create pipe";
        }
        read_pipe = std::make_unique<IPipeImpl>(fds[0]);
        write_pipe = std::make_unique<OPipeImpl>(fds[1]);
    }

    void TearDown() override {
        read_pipe.reset();
        write_pipe.reset();
    }

    std::unique_ptr<IPipeImpl> read_pipe;
    std::unique_ptr<OPipeImpl> write_pipe;
};

// Test basic read and write operations.
TEST_F(PipeTest, SimpleReadWrite) {
    Bytes data_to_write = { 'h', 'e', 'l', 'l', 'o' };
    auto write_result = write_pipe->Write(data_to_write, 0, data_to_write.size());
    ASSERT_FALSE(write_result.error);
    ASSERT_EQ(write_result.bytes_written, data_to_write.size());

    auto read_result = read_pipe->Read(data_to_write.size());
    ASSERT_FALSE(read_result.error);
    ASSERT_EQ(read_result.data, data_to_write);
}

// Test writing and reading zero bytes.
TEST_F(PipeTest, ReadWriteZeroBytes) {
    Bytes data_to_write = { 'h', 'e', 'l', 'l', 'o' };
    auto write_result = write_pipe->Write(data_to_write, 0, 0);
    ASSERT_FALSE(write_result.error);
    ASSERT_EQ(write_result.bytes_written, 0);

    auto read_result = read_pipe->Read(0);
    ASSERT_FALSE(read_result.error);
    ASSERT_TRUE(read_result.data.empty());
}

// Test the ReadAll method.
TEST_F(PipeTest, ReadAll) {
    Bytes data_to_write = { 't', 'e', 's', 't', ' ', 'd', 'a', 't', 'a' };
    
    std::atomic_bool is_expired = false;
    auto read_future = read_pipe->ReadAll(&is_expired);

    auto write_result = write_pipe->Write(data_to_write, 0, data_to_write.size());
    ASSERT_FALSE(write_result.error);
    
    write_pipe.reset(); // Close the write end, sending EOF to the read end.

    auto read_result = read_future.get();
    ASSERT_FALSE(read_result.error);
    ASSERT_EQ(read_result.data, data_to_write);
}

// Test the WriteAll method.
TEST_F(PipeTest, WriteAll) {
    Bytes data_to_write(2048, 'a'); // 2KB of data
    
    std::atomic_bool is_expired = false;
    auto write_future = write_pipe->WriteAll(data_to_write, 0, &is_expired);

    Bytes received_data;
    received_data.reserve(data_to_write.size());
    while (received_data.size() < data_to_write.size()) {
        auto read_result = read_pipe->Read(1024);
        if (!read_result.error) {
            received_data.insert(received_data.end(), read_result.data.begin(), read_result.data.end());
        } else if (read_result.error == std::errc::resource_unavailable_try_again || read_result.error == std::errc::operation_would_block) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else if (read_result.error == IOErrc::IO_EOF) {
            break;
        } else {
            FAIL() << "Read error: " << read_result.error.message();
        }
    }

    auto write_result = write_future.get();
    ASSERT_FALSE(write_result.error);
    ASSERT_EQ(write_result.bytes_written, data_to_write.size());
    ASSERT_EQ(received_data, data_to_write);
}

// Test that ReadAll can be expired.
TEST_F(PipeTest, ReadAllExpired) {
    std::atomic_bool is_expired = false;
    auto read_future = read_pipe->ReadAll(&is_expired);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    is_expired = true;

    auto read_result = read_future.get();
    ASSERT_FALSE(read_result.error);
    ASSERT_TRUE(read_result.data.empty());
}

// Test that WriteAll can be expired.
TEST_F(PipeTest, WriteAllExpired) {
    Bytes data_to_write(1024 * 1024, 'b'); // 1MB
    std::atomic_bool is_expired = false;

    auto write_future = write_pipe->WriteAll(data_to_write, 0, &is_expired);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    is_expired = true;

    auto write_result = write_future.get();
    ASSERT_FALSE(write_result.error);
    ASSERT_GT(write_result.bytes_written, 0);
    ASSERT_LT(write_result.bytes_written, data_to_write.size());

    // Drain the pipe
    Bytes received_data;
    received_data.reserve(write_result.bytes_written);
    while(received_data.size() < write_result.bytes_written) {
        auto result = read_pipe->Read(4096);
        if (!result.error) {
            received_data.insert(received_data.end(), result.data.begin(), result.data.end());
        } else if (result.error == IOErrc::IO_EOF) {
            break;
        } else if (result.error != std::errc::resource_unavailable_try_again && result.error != std::errc::operation_would_block) {
            FAIL() << "Drain failed with error: " << result.error.message();
        }
    }
    ASSERT_EQ(received_data.size(), write_result.bytes_written);
}

// Test concurrent writes to the pipe.
TEST_F(PipeTest, ConcurrentWrite) {
    const int num_threads = 5;
    const int num_chars_per_thread = 1000;

    auto read_future = read_pipe->ReadAll(nullptr);

    std::vector<std::future<WriteResult>> write_futures;
    for (int i = 0; i < num_threads; ++i) {
        Bytes data(num_chars_per_thread, static_cast<unsigned char>(i));
        write_futures.push_back(write_pipe->WriteAll(data, 0, nullptr));
    }

    size_t total_written = 0;
    for (auto& f : write_futures) {
        auto result = f.get();
        ASSERT_FALSE(result.error);
        total_written += result.bytes_written;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    write_pipe.reset(); // Close write end to signal EOF

    auto read_result = read_future.get();
    ASSERT_FALSE(read_result.error);

    ASSERT_EQ(total_written, num_threads * num_chars_per_thread);
    ASSERT_EQ(read_result.data.size(), total_written);
    
    std::vector<int> counts(num_threads, 0);
    for(unsigned char c : read_result.data) {
        counts[c]++;
    }

    for(int i=0; i<num_threads; ++i) {
        ASSERT_EQ(counts[i], num_chars_per_thread);
    }
}

// Test that ReadAll is unblocked when the write end of the pipe is closed.
TEST_F(PipeTest, ReadAllUnblockedByWritePipeClose) {
    std::atomic_bool is_expired = false;
    auto read_future = read_pipe->ReadAll(&is_expired);
    
    write_pipe.reset(); // Close the write end of the pipe

    auto read_result = read_future.get();
    ASSERT_FALSE(read_result.error);
    ASSERT_TRUE(read_result.data.empty());
}

// Test that WriteAll is unblocked when the read end of the pipe is closed.
TEST_F(PipeTest, WriteAllUnblockedByReadPipeClose) {
    Bytes data_to_write(10000, 'c');
    std::atomic_bool is_expired = false;
    auto write_future = write_pipe->WriteAll(data_to_write, 0, &is_expired);

    read_pipe.reset(); // Close the read end of the pipe

    auto write_result = write_future.get();
    ASSERT_TRUE(write_result.error);
    ASSERT_EQ(write_result.error.value(), EPIPE);
}

// Fuzzing test with random data and sizes.
TEST_F(PipeTest, FuzzTest) {
    const int num_iterations = 100;
    const int max_data_size = 4096;
    auto& random = utils::RandomGenerator::get_instance();

    for (int i = 0; i < num_iterations; ++i) {
        size_t data_size = random.get_int(1, max_data_size);
        Bytes data_to_write = random.get_bytes(data_size);
        std::cout << "FuzzTest iteration " << i << ": testing with " << data_size << " bytes." << std::endl;

        auto write_result = write_pipe->Write(data_to_write, 0, data_to_write.size());
        ASSERT_FALSE(write_result.error);
        ASSERT_EQ(write_result.bytes_written, data_to_write.size());

        Bytes received_data;
        received_data.reserve(data_to_write.size());
        while(received_data.size() < data_to_write.size()) {
            auto read_result = read_pipe->Read(data_to_write.size() - received_data.size());
            ASSERT_FALSE(read_result.error);
            received_data.insert(received_data.end(), read_result.data.begin(), read_result.data.end());
        }
        ASSERT_EQ(received_data, data_to_write);
    }
}

// Test transferring a large amount of data.
TEST_F(PipeTest, LargeDataTransfer) {
    const size_t data_size = 10 * 1024 * 1024; // 10MB
    Bytes data_to_write = utils::RandomGenerator::get_instance().get_bytes(data_size);
    std::cout << "Testing large data transfer of " << data_size << " bytes." << std::endl;

    auto read_future = read_pipe->ReadAll(nullptr);
    auto write_future = write_pipe->WriteAll(data_to_write, 0, nullptr);

    auto write_result = write_future.get();
    ASSERT_FALSE(write_result.error);
    ASSERT_EQ(write_result.bytes_written, data_size);

    write_pipe.reset(); // Close write end to signal EOF

    auto read_result = read_future.get();
    ASSERT_FALSE(read_result.error);
    ASSERT_EQ(read_result.data, data_to_write);
}