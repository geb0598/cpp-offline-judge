#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>

#include "coj/file_io.h"
#include "coj/file_descriptor.h"

namespace coj {

namespace {

void OpenPipe(FileDescriptor& read_fd, FileDescriptor& write_fd) {
    int p[2];
    ASSERT_NE(::pipe(p), -1) << "Failed to create pipe for test";
    read_fd = FileDescriptor(p[0]);
    write_fd = FileDescriptor(p[1]);
}

TEST(FileIoTest, ReadWrite_WithEmptyBuffer_ReturnsSuccessAndZeroBytes) {
    int fd = STDOUT_FILENO; 
    std::vector<std::byte> empty_buffer;

    auto read_result = Read(fd, std::span<std::byte>(empty_buffer));
    auto write_result = Write(fd, std::span<const std::byte>(empty_buffer));

    ASSERT_TRUE(read_result.has_value());
    EXPECT_EQ(read_result->status, IoStatus::Success);
    EXPECT_EQ(read_result->bytes, 0);
    ASSERT_TRUE(write_result.has_value());
    EXPECT_EQ(write_result->status, IoStatus::Success);
    EXPECT_EQ(write_result->bytes, 0);
}

TEST(FileIoTest, ReadWrite_WithValidPipe_TransfersDataCorrectly) {
    FileDescriptor read_fd, write_fd;
    OpenPipe(read_fd, write_fd);

    std::string message = "Hello, COJ!";
    auto write_span = std::as_bytes(std::span(message));
    std::vector<std::byte> read_buffer(256); 

    auto write_result = Write(write_fd.Get(), write_span);

    auto read_result = Read(read_fd.Get(), read_buffer);

    ASSERT_TRUE(write_result.has_value());
    EXPECT_EQ(write_result->status, IoStatus::Success);
    EXPECT_EQ(write_result->bytes, message.size());

    ASSERT_TRUE(read_result.has_value());
    EXPECT_EQ(read_result->status, IoStatus::Success);
    EXPECT_EQ(read_result->bytes, message.size());

    std::string read_message(
        reinterpret_cast<const char*>(read_buffer.data()), 
        read_result->bytes
    );
    EXPECT_EQ(read_message, message);
}

TEST(FileIoTest, Read_WhenWriteEndIsClosed_ReturnsEoF) {
    FileDescriptor read_fd, write_fd;
    OpenPipe(read_fd, write_fd);
    write_fd.Close(); 

    std::vector<std::byte> buffer(10);

    auto result = Read(read_fd.Get(), buffer);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, IoStatus::EoF);
    EXPECT_EQ(result->bytes, 0);
}

TEST(FileIoTest, Read_OnEmptyNonBlockingPipe_ReturnsWouldBlock) {
    FileDescriptor read_fd, write_fd;
    OpenPipe(read_fd, write_fd);

    int flags = ::fcntl(read_fd.Get(), F_GETFL, 0);
    ASSERT_NE(flags, -1);
    ASSERT_NE(::fcntl(read_fd.Get(), F_SETFL, flags | O_NONBLOCK), -1);

    std::vector<std::byte> buffer(10);

    auto result = Read(read_fd.Get(), buffer);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, IoStatus::WouldBlock);
    EXPECT_EQ(result->bytes, 0);
}

} // namespace 

} // namespace coj