#include <gtest/gtest.h>

#include "coj/file_descriptor.h"

namespace coj {

namespace {

bool IsOpen(int fd) {
    return ::fcntl(fd, F_GETFD) != -1; 
}

TEST(FileDescriptorTest, MoveAssignment_ToAlreadyValidObject_ClosesOriginalFd) {
    int fd1 = ::open("/dev/null", O_RDONLY);
    int fd2 = ::open("/dev/null", O_RDONLY);
    ASSERT_TRUE(IsOpen(fd1));
    ASSERT_TRUE(IsOpen(fd2));

    FileDescriptor wrapper1(fd1);
    FileDescriptor wrapper2(fd2);

    wrapper1 = std::move(wrapper2);

    EXPECT_FALSE(IsOpen(fd1));
    EXPECT_EQ(wrapper1.Get(), fd2);
    EXPECT_FALSE(wrapper2.IsValid());
}

TEST(FileDescriptorTest, Release_OnValidObject_ReturnsFdAndInvalidatesWithoutClosing) {
    int raw_fd = ::open("/dev/null", O_RDONLY);
    FileDescriptor wrapper(raw_fd);

    int released_fd = wrapper.Release();

    EXPECT_EQ(released_fd, raw_fd);
    EXPECT_FALSE(wrapper.IsValid());
    EXPECT_TRUE(IsOpen(released_fd));

    ::close(released_fd);
}

TEST(OpenTest, ValidPath_ReturnsExpectedWithValidFileDescriptor) {
    const char* valid_path = "/dev/null";

    auto result = Open(valid_path, O_RDONLY | O_CLOEXEC);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->IsValid());
    EXPECT_TRUE(IsOpen(result.value().Get()));
}

TEST(OpenTest, InvalidPath_ReturnsUnexpectedWithEnoentError) {
    const char* invalid_path = "/this/path/absolutely/does/not/exist";

    auto result = Open(invalid_path, O_RDONLY);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().value(), ENOENT);
}

} // namespace

} // namespace coj