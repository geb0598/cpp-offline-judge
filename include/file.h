#ifndef FILE_H
#define FILE_H

#include <filesystem>
#include <future>
#include <thread>
#include <vector>

// specialize template struct defined in C++ standard library
// https://en.cppreference.com/w/cpp/language/extending_std.html
namespace std {

template<>
struct default_delete<FILE> {
    constexpr default_delete() noexcept = default;

    void operator()(FILE* ptr) const { std::fclose(ptr); }
};

} // namespace std

namespace coj {

using Bytes = std::vector<char>;

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
    virtual size_type Write(const Bytes& data, size_type size) = 0;
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

class IFile : public FileImpl {
public:
    using FileImpl::size_type;
    using FileImpl::value_type;

    ~IFile() = default;

    IFile(std::FILE* fp);
    IFile(int fd);
    IFile(const std::filesystem::path& file);

    IFile(IFile&&) noexcept = default;
    IFile& operator=(IFile&&) noexcept = default;

    IFile(const IFile&) = delete;
    IFile& operator=(const IFile&) = delete;

    virtual bool is_readable() const noexcept override;
    virtual bool is_writable() const noexcept override;

    virtual Bytes Read(size_type size) override;
    virtual size_type Write(const Bytes& data, size_type size) override;
};

class OFile : public FileImpl {
public:
    using FileImpl::size_type;
    using FileImpl::value_type;

    ~OFile() = default;

    OFile(std::FILE* fp);
    OFile(int fd);
    OFile(const std::filesystem::path& file);

    OFile(OFile&&) noexcept = default;
    OFile& operator=(OFile&&) noexcept = default;

    OFile(const OFile&) = delete;
    OFile& operator=(const OFile&) = delete;

    virtual bool is_readable() const noexcept override;
    virtual bool is_writable() const noexcept override;

    virtual Bytes Read(size_type size) override;
    virtual size_type Write(const Bytes& data, size_type size) override;
};

File::size_type Communicate(std::shared_ptr<IFile> input, std::shared_ptr<OFile> output);

std::future<File::size_type> AsyncCommunicate(std::shared_ptr<IFile> input, std::shared_ptr<OFile> output);

} // namespace coj

#endif