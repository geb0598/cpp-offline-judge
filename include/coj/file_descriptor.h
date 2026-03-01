#pragma once

#include <fcntl.h>
#include <unistd.h>

#include <expected>
#include <filesystem>

namespace coj {

class FileDescriptor {
public:
    static constexpr int INVALID_FILE_DESCRIPTOR = -1;

    constexpr FileDescriptor() noexcept : fd_(INVALID_FILE_DESCRIPTOR) {}
    
    explicit constexpr FileDescriptor(int fd) noexcept : fd_(fd) {}

    FileDescriptor(const FileDescriptor& other) = delete;
    FileDescriptor& operator=(const FileDescriptor& other) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept : fd_(other.Release()) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            Close();
            fd_ = other.Release();
        }
        return *this;
    }

    ~FileDescriptor() { Close(); }

    explicit operator bool() const noexcept { return IsValid(); }

    void Close() noexcept {
        if (IsValid()) {
            ::close(fd_);
            fd_ = INVALID_FILE_DESCRIPTOR;
        }
    }

    int Release() noexcept {
        int tmp = fd_;
        fd_ = INVALID_FILE_DESCRIPTOR;
        return tmp;
    }

    bool IsValid() const noexcept { return fd_ >= 0; }

    int Get() const noexcept { return fd_; }

private:
    int fd_;
};

inline std::expected<FileDescriptor, std::error_code> Open(const std::filesystem::path& path, int flags, mode_t mode = 0644) {
    while (true) {
        int fd = ::open(path.c_str(), flags, mode);

        if (fd >= 0) {
            return FileDescriptor(fd);
        } else if (errno == EINTR) {
            continue;
        } else {
            return std::unexpected(std::error_code(errno, std::generic_category()));
        }
    }
}

} // namespace coj