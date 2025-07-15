#include <fcntl.h>

#include "exception.h"
#include "file.h"

namespace coj {

FileImpl::FileImpl(std::FILE *fp) : fp_(fp) {}
FileImpl::FileImpl(int fd, const std::string& mode) {
    FILE* fp = ::fdopen(fd, mode.c_str()); // TODO: portability
    if (fp == nullptr)
        throw std::system_error(GetLastErrorCode(), "Failed to open file descriptor");
    fp_.reset(fp);
}
FileImpl::FileImpl(const std::filesystem::path& file, const std::string& mode) {
    if (!std::filesystem::exists(file))
        throw std::invalid_argument("File '" + file.string() + "' doesn't exist");
    FILE* fp = std::fopen(file.c_str(), mode.c_str());
    if (fp == nullptr)
        throw std::system_error(GetLastErrorCode(), "Failed to open file '" + file.string() + "'");
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

IFile::IFile(std::FILE* fp) : FileImpl(fp) {}
IFile::IFile(int fd) : FileImpl(fd, "r") {}
IFile::IFile(const std::filesystem::path& file) : FileImpl(file, "r") {}

bool IFile::is_readable() const noexcept { return true; }
bool IFile::is_writable() const noexcept { return false; }

Bytes IFile::Read(size_type size) {
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
                throw std::system_error(GetLastErrorCode(), "Error occurred while reading from the stream");
            }
        }
    }
    buffer.resize(total_bytes);
    return buffer;
}

auto IFile::Write(const Bytes& data, size_type size) -> size_type {
    throw std::runtime_error("Try writing to read-only stream");
}

OFile::OFile(std::FILE* fp) : FileImpl(fp) {}
OFile::OFile(int fd) : FileImpl(fd, "w") {}
OFile::OFile(const std::filesystem::path& file) : FileImpl(file, "w") {}

bool OFile::is_readable() const noexcept { return false; }
bool OFile::is_writable() const noexcept { return true; }

Bytes OFile::Read(size_type size) {
    throw std::runtime_error("Try reading from write-only stream");
}

auto OFile::Write(const Bytes& data, size_type size) -> size_type {
    if (!is_opened())
        throw std::runtime_error("Attempted to write from the closed stream");

    size_type total_bytes = 0;
    while (total_bytes < size) {
        size_type bytes_to_write = size - total_bytes;
        size_type bytes_written = std::fwrite(data.data() + total_bytes, sizeof(value_type), bytes_to_write, filepointer());
        total_bytes += bytes_written;
        if (bytes_written < bytes_to_write) {
            if (is_error()) {
                throw std::system_error(GetLastErrorCode(), "Error occurred while writing to the stream");
            }
        }
    }
    fflush(filepointer());
    return total_bytes;
}

// read data from input to buffer and write to output until whole input is transferred to output
File::size_type Communicate(std::shared_ptr<IFile> input, std::shared_ptr<OFile> output) {
    File::size_type total_bytes = 0;
    while (true) {
        Bytes read_bytes = input->Read(BUFSIZ);
        if (read_bytes.empty()) {
            break;
        }
        output->Write(read_bytes, read_bytes.size());
        total_bytes += read_bytes.size();
    }
    return total_bytes;
}

std::future<File::size_type> AsyncCommunicate(std::shared_ptr<IFile> input, std::shared_ptr<OFile> output) {
    return std::async(std::launch::async, Communicate, input, output);
}

} // namespace coj