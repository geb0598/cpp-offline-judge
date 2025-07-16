#include <mutex>
#include <iostream> // TODO: Remove

#include <fcntl.h>

#include "exception.h"
#include "pipe.h"

namespace coj {

PipeOwner::PipeOwner(int fd) : fd_(new int(fd)) {
    int flags;
    if ((flags = ::fcntl(fileno(), F_GETFL, NULL)) == -1)
        throw OSError("Failed to get flags from file descriptor: " + std::to_string(fileno()));
    if (::fcntl(fileno(), F_SETFL, flags | O_NONBLOCK) == -1)
        throw OSError("Failed to set non-block flag at file descriptor: " + std::to_string(fileno()));
}

int PipeOwner::fileno() const noexcept { return *fd_; }

ReadResult IPipeImpl::Read(size_type size) noexcept {
    ReadResult result {
        .data {Bytes(size)},
        .error {IOErrc::IO_OK}
    };
    if (size == 0) return result;

    int eintr_cnt = 0;
    size_type total_bytes = 0;
    while (total_bytes < size) {
        size_type bytes_to_read = size - total_bytes;
        ssize_t bytes_read;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bytes_read = read(fileno(), result.data.data() + total_bytes, bytes_to_read);
        }
        if (bytes_read < 0) {
            result.error = GetLastErrorCode();
            if (result.error == std::errc::interrupted && ++eintr_cnt <= EINTR_RETRY_LIMIT) 
                continue;
            break;
        } else if (bytes_read == 0) {
            result.error = IOErrc::IO_EOF;
            break;
        } else {
            total_bytes += static_cast<size_type>(bytes_read);
        }
    }
    result.data.resize(total_bytes);
    return result;
}

std::future<ReadResult> IPipeImpl::ReadAll(const std::atomic_bool *is_expired) noexcept {
    return std::async(std::launch::async, [this, is_expired]() {
        ReadResult result {
            .data {},
            .error {IOErrc::IO_OK}
        };

        while (true) {
            if (is_expired && is_expired->load()) break;

            auto [data, error] = Read(BUF_SIZE);
            if (error == IOErrc::IO_OK) {
                result.data.insert(result.data.end(), data.begin(), data.end());
            } else if (error == IOErrc::IO_EOF) {
                result.data.insert(result.data.end(), data.begin(), data.end());
                break;
            } else if (error == IOErrc::IO_INVALID_ARG) {
                result.error = error;
                break;
            } else if (
                error == std::errc::interrupted || 
                error == std::errc::resource_unavailable_try_again || 
                error == std::errc::operation_would_block) {
                result.data.insert(result.data.end(), data.begin(), data.end()); // TODO: issue 기록!
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
            } else {
                result.error = error;
                break;
            }
        }

        return result;
    });
}

WriteResult OPipeImpl::Write(Bytes data, size_type offset, size_type size) noexcept {
    if (offset + size > data.size()) {
        return {0, IOErrc::IO_INVALID_ARG};
    }
 
    WriteResult result {
        .bytes_written {0},
        .error {IOErrc::IO_OK}
    };
   if (size == 0) return result;

    int eintr_cnt = 0;
    size_type total_bytes = 0;
    while (total_bytes < size) {
        size_type bytes_to_write = size - total_bytes;
        ssize_t bytes_written;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            bytes_written = write(fileno(), data.data() + offset + total_bytes, bytes_to_write);
        }
        if (bytes_written < 0) {
            result.error = GetLastErrorCode();
            if (result.error == std::errc::interrupted && ++eintr_cnt > EINTR_RETRY_LIMIT)
                continue; 
            break;
        } else {
            total_bytes += static_cast<size_type>(bytes_written);
        } 
    }
    result.bytes_written = total_bytes;
    return result;
}

std::future<WriteResult> OPipeImpl::WriteAll(Bytes data, size_type offset, const std::atomic_bool *is_expired) noexcept {
    // copy data? tradeoff?
    return std::async(std::launch::async, [this, data, offset, is_expired]() {
        WriteResult result {
            .bytes_written {0},
            .error {IOErrc::IO_OK}
        };

        if (data.empty()) return result;

        while (result.bytes_written < data.size() - offset) {
            if (is_expired && is_expired->load()) break;
            
            size_type bytes_to_write = std::min(BUF_SIZE, data.size() - offset - result.bytes_written);
            auto [bytes_written, error] = Write(data, offset + result.bytes_written, bytes_to_write);
            if (error == IOErrc::IO_OK) {
                result.bytes_written += bytes_written;
            } else if (error == IOErrc::IO_INVALID_ARG) {
                result.error = error;
                break;
            } else if (
                error == std::errc::interrupted ||
                error == std::errc::resource_unavailable_try_again ||
                error == std::errc::operation_would_block) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                result.error = error;
                break;
            }
        }

        return result;
    });
}


} // namespace coj
