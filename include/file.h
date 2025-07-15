#ifndef FILE_H
#define FILE_H

#include <filesystem>
#include <future>
#include <thread>
#include <vector>

#include "bytes.h"

// specialize template struct defined in C++ standard library
// https://en.cppreference.com/w/cpp/language/extending_std.html
namespace std {

template<>
struct default_delete<FILE> {
    constexpr default_delete() noexcept = default;

    void operator()(FILE* ptr) const { 
        if (ptr != nullptr)
            std::fclose(ptr); 
    }
};

} // namespace std

namespace coj {

class File {
public:
    using size_type = Bytes::size_type;
    using value_type = Bytes::value_type;

    virtual ~File() = default;

    virtual int fileno() const noexcept = 0;
    virtual FILE* filepointer() const noexcept = 0;

    virtual bool is_opened() const noexcept = 0;
    virtual bool is_readable() const noexcept = 0;
    virtual bool is_writable() const noexcept = 0;

    virtual bool is_eof() const noexcept = 0;
    virtual bool is_error() const noexcept = 0;

    virtual Bytes Read(size_type size) = 0;
    virtual size_type Write(const Bytes& data, size_type offset, size_type size) = 0;
};

class FileImpl : public File {
public:
    using File::size_type;
    using File::value_type;

    virtual ~FileImpl() = default;

    FileImpl(std::FILE* fp);
    FileImpl(int fd, const std::string& mode);
    FileImpl(const std::filesystem::path& file, const std::string& mode);

    FileImpl(FileImpl&&) noexcept = default;
    FileImpl& operator=(FileImpl&&) noexcept = default;

    FileImpl(const FileImpl&) = delete;
    FileImpl& operator=(const FileImpl&) = delete;

    virtual int fileno() const noexcept final;
    virtual FILE* filepointer() const noexcept final;

    virtual bool is_opened() const noexcept final;
    virtual bool is_eof() const noexcept final;
    virtual bool is_error() const noexcept final;

private:
    std::unique_ptr<std::FILE> fp_;
};

class IFileImpl : public FileImpl {
public:
    using FileImpl::size_type;
    using FileImpl::value_type;

    ~IFileImpl() = default;

    IFileImpl(std::FILE* fp);
    IFileImpl(int fd);
    IFileImpl(const std::filesystem::path& file);

    IFileImpl(IFileImpl&&) noexcept = default;
    IFileImpl& operator=(IFileImpl&&) noexcept = default;

    IFileImpl(const IFileImpl&) = delete;
    IFileImpl& operator=(const IFileImpl&) = delete;

    virtual bool is_readable() const noexcept override;
    virtual bool is_writable() const noexcept override;

    virtual Bytes Read(size_type size) override;
    virtual size_type Write(const Bytes& data, size_type offset, size_type size) override;
};

class OFileImpl : public FileImpl {
public:
    using FileImpl::size_type;
    using FileImpl::value_type;

    ~OFileImpl() = default;

    OFileImpl(std::FILE* fp);
    OFileImpl(int fd);
    OFileImpl(const std::filesystem::path& file);

    OFileImpl(OFileImpl&&) noexcept = default;
    OFileImpl& operator=(OFileImpl&&) noexcept = default;

    OFileImpl(const OFileImpl&) = delete;
    OFileImpl& operator=(const OFileImpl&) = delete;

    virtual bool is_readable() const noexcept override;
    virtual bool is_writable() const noexcept override;

    virtual Bytes Read(size_type size) override;
    virtual size_type Write(const Bytes& data, size_type offset, size_type size) override;
};

File::size_type Communicate(std::shared_ptr<IFileImpl> input, std::shared_ptr<OFileImpl> output);

std::future<File::size_type> AsyncCommunicate(std::shared_ptr<IFileImpl> input, std::shared_ptr<OFileImpl> output);

} // namespace coj

#endif