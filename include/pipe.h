#ifndef PIPE_H
#define PIPE_H

#include <climits>
#include <filesystem>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "bytes.h"

// specialize template struct defined in C++ standard library
// https://en.cppreference.com/w/cpp/language/extending_std.html
namespace std {

} // namespace std

namespace coj {

class Pipe {
public:
    using size_type = Bytes::size_type;
    using value_type = Bytes::value_type;

    virtual ~Pipe() = default;

    virtual int fileno() const noexcept = 0;
};

struct [[nodiscard]] ReadResult {
    Bytes data;
    std::error_code error;
};

class IPipe : virtual public Pipe {
public:
    virtual ~IPipe() = default;

    virtual ReadResult Read(size_type size) noexcept = 0;
    virtual std::future<ReadResult> ReadAll(const std::atomic_bool *is_expired) noexcept = 0;
};

struct [[nodiscard]] WriteResult {
    Bytes::size_type bytes_written;
    std::error_code error;
};

class OPipe : virtual public Pipe {
public:
    virtual ~OPipe() = default;

    virtual WriteResult Write(Bytes data, size_type offset, size_type size) noexcept = 0;
    virtual std::future<WriteResult> WriteAll(Bytes data, size_type offset, const std::atomic_bool *is_expired) noexcept = 0;
};

class PipeOwner : virtual public Pipe {
public:
    ~PipeOwner() = default;

    PipeOwner(int fd);

    PipeOwner(PipeOwner&&) = default;
    PipeOwner& operator=(PipeOwner&&) = default;

    PipeOwner(const PipeOwner&) = delete;
    PipeOwner& operator=(const PipeOwner&) = default;

    int fileno() const noexcept override;

protected:
    std::mutex mutex_;

private:
    struct auto_close {
        void operator()(int* p) const {
            if (p && *p != -1) {
                ::close(*p);
                delete p;
            }
        }
    };

    std::unique_ptr<int, auto_close> fd_;
};

class IPipeImpl : public IPipe, public PipeOwner {
public:
    ~IPipeImpl() = default;

    using PipeOwner::PipeOwner;
    using PipeOwner::operator=;

    virtual ReadResult Read(size_type size) noexcept override;
    virtual std::future<ReadResult> ReadAll(const std::atomic_bool *is_expired = nullptr) noexcept override;

private:
    static constexpr int EINTR_RETRY_LIMIT = 100;
    static constexpr size_type BUF_SIZE = PIPE_BUF;
};

class OPipeImpl : public OPipe, public PipeOwner {
public:
    ~OPipeImpl() = default;

    using PipeOwner::PipeOwner;
    using PipeOwner::operator=;

    virtual WriteResult Write(Bytes data, size_type offset, size_type size) noexcept override;
    virtual std::future<WriteResult> WriteAll(Bytes data, size_type offset, const std::atomic_bool *is_expired = nullptr) noexcept override;

private:
    static constexpr int EINTR_RETRY_LIMIT = 100;
    static constexpr size_type BUF_SIZE = PIPE_BUF;
};

} // namespace coj

#endif