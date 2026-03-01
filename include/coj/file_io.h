#pragma once

#include <span>
#include <system_error>

#include <unistd.h>

namespace coj {

enum class IoStatus {
    Success,
    EoF,
    WouldBlock,
    Error
};

struct [[nodiscard]] ReadResult {
    IoStatus status;
    size_t bytes;
    std::error_code error;
};

struct [[nodiscard]] WriteResult {
    IoStatus status;
    size_t bytes;
    std::error_code error;
};

static constexpr int MAX_INTERRUPT_RETRY = 10;

inline ReadResult Read(int fd, std::span<std::byte> buffer) {
    if (buffer.empty()) {
        return {
            .status { IoStatus::Success }, 
            .bytes  { 0 }, 
            .error  { }
        };
    }

    int interrupt_count = 0;

    while (true) {
        ssize_t bytes_read = ::read(fd, buffer.data(), buffer.size_bytes());

        if (bytes_read > 0) {
            return {
                .status { IoStatus::Success },
                .bytes  { static_cast<size_t>(bytes_read) },
                .error  { }
            };
        } else if (bytes_read == 0) {
            return {
                .status { IoStatus::EoF },
                .bytes  { 0 },
                .error  { }
            };
        } else {
            if (errno == EINTR) {
                if (++interrupt_count < MAX_INTERRUPT_RETRY) {
                    continue;
                }
                return {
                    .status { IoStatus::Error },
                    .bytes  { 0 },
                    .error  { std::error_code(errno, std::generic_category()) }
                };
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return {
                    .status { IoStatus::WouldBlock },
                    .bytes  { 0 },
                    .error  { }
                };
            } else {
                return {
                    .status { IoStatus::Error },
                    .bytes  { 0 },
                    .error  { std::error_code(errno, std::generic_category()) }
                };
            }
        }
    }
}

inline WriteResult Write(int fd, std::span<const std::byte> buffer) {
    if (buffer.empty()) {
        return {
            .status { IoStatus::Success },
            .bytes  { 0 },
            .error  { }
        };
    }

    int interrupt_count = 0;
    size_t total_bytes = 0;

    while (total_bytes < buffer.size()) {
        ssize_t bytes_written = ::write(fd, buffer.data() + total_bytes, buffer.size() - total_bytes);

        if (bytes_written >= 0) {
            total_bytes += bytes_written;
            interrupt_count = 0;
        } else {
            if (errno == EINTR) {
                if (++interrupt_count <= MAX_INTERRUPT_RETRY) {
                    continue;
                }
                if (total_bytes > 0) {
                    return {
                        .status { IoStatus::Success },
                        .bytes  { total_bytes },
                        .error  { }
                    };
                } else {
                    return {
                        .status { IoStatus::Error },
                        .bytes  { 0 },
                        .error  { std::error_code(errno, std::generic_category()) }
                    };
                }
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (total_bytes > 0) {
                    return {
                        .status { IoStatus::Success },
                        .bytes  { total_bytes },
                        .error  { }
                    };
                } else {
                    return {
                        .status { IoStatus::WouldBlock },
                        .bytes  { 0 },
                        .error  { }
                    };
                }
            } else {
                if (total_bytes > 0) {
                    return {
                        .status { IoStatus::Success },
                        .bytes  { total_bytes },
                        .error  { }
                    };
                } else {
                    return {
                        .status { IoStatus::Error },
                        .bytes  { 0 },
                        .error  { std::error_code(errno, std::generic_category()) }
                    };
                }
            }
        }
    }

    return {
        .status { IoStatus::Success },
        .bytes  { total_bytes },
        .error  { }
    };
}

} // namespace coj