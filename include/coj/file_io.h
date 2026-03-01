#pragma once

#include <expected>
#include <span>
#include <system_error>
#include <vector>

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
};

struct [[nodiscard]] WriteResult {
    IoStatus status;
    size_t bytes;
};

static constexpr int MAX_INTERRUPT_RETRY = 10;

[[nodiscard]] inline std::expected<ReadResult, std::error_code> Read(int fd, std::span<std::byte> buffer) {
    if (buffer.empty()) {
        return ReadResult {
            .status { IoStatus::Success }, 
            .bytes  { 0 }, 
        };
    }

    int interrupt_count = 0;

    while (true) {
        ssize_t bytes_read = ::read(fd, buffer.data(), buffer.size_bytes());

        if (bytes_read > 0) {
            return ReadResult {
                .status { IoStatus::Success },
                .bytes  { static_cast<size_t>(bytes_read) },
            };
        } else if (bytes_read == 0) {
            return ReadResult {
                .status { IoStatus::EoF },
                .bytes  { 0 },
            };
        } else {
            if (errno == EINTR) {
                if (++interrupt_count < MAX_INTERRUPT_RETRY) {
                    continue;
                }
                return std::unexpected(std::error_code(errno, std::generic_category()));
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return ReadResult {
                    .status { IoStatus::WouldBlock },
                    .bytes  { 0 },
                };
            } else {
                return std::unexpected(std::error_code(errno, std::generic_category())); 
            }
        }
    }
}

inline std::expected<std::vector<std::byte>, std::error_code> ReadAll(int fd) {
    std::vector<std::byte> total_buf;
    std::vector<std::byte> chunk_buf(4096);

    while (true) {
        auto read_result = Read(fd, chunk_buf);

        if (read_result.has_value()) {
            if (read_result->status == IoStatus::Success) {
                total_buf.insert(total_buf.end(), chunk_buf.begin(), chunk_buf.begin() + read_result->bytes);
            } else if (read_result->status == IoStatus::EoF) {
                break;
            } else if (read_result->status == IoStatus::WouldBlock) {
                break;
            }
        } else {
            return std::unexpected(read_result.error());
        }
    }

    return total_buf;
}

inline std::expected<std::string, std::error_code> ReadAllAsString(int fd) {
    return ReadAll(fd).and_then([](std::vector<std::byte> bytes) {
        return std::expected<std::string, std::error_code>{
            std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())
        };
    });
}

[[nodiscard]] inline std::expected<WriteResult, std::error_code> Write(int fd, std::span<const std::byte> buffer) {
    if (buffer.empty()) {
        return WriteResult {
            .status { IoStatus::Success },
            .bytes  { 0 },
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
                    return WriteResult {
                        .status { IoStatus::Success },
                        .bytes  { total_bytes },
                    };
                } else {
                    return std::unexpected(std::error_code(errno, std::generic_category()));
                }
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                if (total_bytes > 0) {
                    return WriteResult {
                        .status { IoStatus::Success },
                        .bytes  { total_bytes },
                    };
                } else {
                    return WriteResult {
                        .status { IoStatus::WouldBlock },
                        .bytes  { 0 },
                    };
                }
            } else {
                if (total_bytes > 0) {
                    return WriteResult {
                        .status { IoStatus::Success },
                        .bytes  { total_bytes },
                    };
                } else {
                    return std::unexpected(std::error_code(errno, std::generic_category()));
                }
            }
        }
    }

    return WriteResult {
        .status { IoStatus::Success },
        .bytes  { total_bytes },
    };
}

} // namespace coj