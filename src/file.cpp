#include <fcntl.h>

#include "exception.h"
#include "file.h"

namespace coj {

FileImpl::FileImpl(std::FILE *fp) : fp_(fp) {}
FileImpl::FileImpl(int fd, const std::string& mode) {
    FILE* fp = ::fdopen(fd, mode.c_str()); // TODO: portability
    if (fp == nullptr)
        throw OSError("Failed to open file descriptor");
    fp_.reset(fp);
}
FileImpl::FileImpl(const std::filesystem::path& file, const std::string& mode) {
    if (!std::filesystem::exists(file))
        throw std::invalid_argument("File '" + file.string() + "' doesn't exist");
    FILE* fp = std::fopen(file.c_str(), mode.c_str());
    if (fp == nullptr)
        throw OSError("Failed to open file '" + file.string() + "'");
    fp_.reset(fp);
}

int FileImpl::fileno() const noexcept {
    int fd = ::fileno(fp_.get()); // TODO: portability
    return fd;
}

FILE* FileImpl::filepointer() const noexcept {
    return fp_.get();
}

bool FileImpl::is_opened() const noexcept {
    return fp_ != nullptr;
}

bool FileImpl::is_eof() const noexcept {
    return std::feof(fp_.get());
}

bool FileImpl::is_error() const noexcept {
    return std::ferror(fp_.get());
}

IFileImpl::IFileImpl(std::FILE* fp) : FileImpl(fp) {}
IFileImpl::IFileImpl(int fd) : FileImpl(fd, "r") {}
IFileImpl::IFileImpl(const std::filesystem::path& file) : FileImpl(file, "r") {}

bool IFileImpl::is_readable() const noexcept { return true; }
bool IFileImpl::is_writable() const noexcept { return false; }

Bytes IFileImpl::Read(size_type size) {
    if (!is_opened())
        throw std::runtime_error("Attempted to read from the closed stream");
    Bytes buffer(size);
    size_type total_bytes = 0;
    while (total_bytes < buffer.size()) {
        size_type bytes_to_read = buffer.size() - total_bytes;
        size_type bytes_read = std::fread(buffer.data() + total_bytes, sizeof(value_type), bytes_to_read, filepointer());
        total_bytes += bytes_read;
        if (bytes_read < bytes_to_read) {
            if (is_eof()) {
                break;
            } else {
                throw OSError("Error occurred while reading from the stream");
            }
        }
    }
    buffer.resize(total_bytes);
    return buffer;
}

auto IFileImpl::Write(const Bytes& data, size_type offset, size_type size) -> size_type {
    throw std::runtime_error("Try writing to read-only stream");
}

OFileImpl::OFileImpl(std::FILE* fp) : FileImpl(fp) {}
OFileImpl::OFileImpl(int fd) : FileImpl(fd, "w") {}
OFileImpl::OFileImpl(const std::filesystem::path& file) : FileImpl(file, "w") {}

bool OFileImpl::is_readable() const noexcept { return false; }
bool OFileImpl::is_writable() const noexcept { return true; }

Bytes OFileImpl::Read(size_type size) {
    throw std::runtime_error("Try reading from write-only stream");
}

auto OFileImpl::Write(const Bytes& data, size_type offset, size_type size) -> size_type {
    if (!is_opened())
        throw std::runtime_error("Attempted to write from the closed stream");

    size_type total_bytes = 0;
    while (total_bytes < size) {
        size_type bytes_to_write = size - total_bytes;
        size_type bytes_written = std::fwrite(data.data() + offset + total_bytes, sizeof(value_type), bytes_to_write, filepointer());
        total_bytes += bytes_written;
        if (bytes_written < bytes_to_write) {
            if (is_error()) {
                throw OSError("Write error on file stream");
            }
        }
    }
    fflush(filepointer());
    return total_bytes;
}

} // namespace coj