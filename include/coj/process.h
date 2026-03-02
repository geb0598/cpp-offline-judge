#pragma once

#include "coj/file_descriptor.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

namespace coj {

namespace process {

class Stdio {
public:
    enum class Type {
        Inherit,
        Piped,
        Null,
        File
    };

    static Stdio Inherit() { return Stdio(Type::Inherit); }
    static Stdio Piped() { return Stdio(Type::Piped); }
    static Stdio Null() { return Stdio(Type::Null); }
    static Stdio From(FileDescriptor fd) { return Stdio(std::move(fd)); }

    Type GetType() const { return type_; }

    FileDescriptor TakeFd() {
        if (type_ == Type::File) {
            type_ = Type::Inherit;
            return std::move(fd_);
        }

        return FileDescriptor{};
    }

private:
    explicit Stdio(Type type) : type_(type) {}
    explicit Stdio(FileDescriptor fd) : type_(Type::File), fd_(std::move(fd)) {}

    Type type_ = Type::Inherit;
    FileDescriptor fd_;
};

class ExitStatus {
public:
    friend class Child;

    [[nodiscard]] bool Success() const noexcept {
        return WIFEXITED(status_) && WEXITSTATUS(status_) == 0;
    }

    [[nodiscard]] std::optional<int> Code() const noexcept {
        if (WIFEXITED(status_)) {
            return WEXITSTATUS(status_);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<int> Signal() const noexcept {
        if (WIFSIGNALED(status_)) {
            return WTERMSIG(status_);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::chrono::milliseconds GetCpuTime() const noexcept {
        long utime = usage_.ru_utime.tv_sec * 1000 + usage_.ru_utime.tv_usec / 1000;
        long stime = usage_.ru_stime.tv_sec * 1000 + usage_.ru_stime.tv_usec / 1000;
        return std::chrono::milliseconds(utime + stime);
    }

    [[nodiscard]] size_t GetMaxMemoryKb() const noexcept {
        return static_cast<size_t>(usage_.ru_maxrss);
    }

private:
    ExitStatus(int status, ::rusage usage) : status_(status), usage_(usage) {}

    int status_ = 0;
    ::rusage usage_ = {};
};

class Child {
public:
    static constexpr pid_t INVALID_PID = -1;

    explicit Child(pid_t pid) : pid_(pid) {}

    Child(const Child& other) = delete;
    Child& operator=(const Child& other) = delete;

    Child(Child&& other) noexcept
        : pid_(std::exchange(other.pid_, INVALID_PID)),
          stdin_pipe(std::move(other.stdin_pipe)),
          stdout_pipe(std::move(other.stdout_pipe)),
          stderr_pipe(std::move(other.stderr_pipe)) {}

    Child& operator=(Child&& other) noexcept {
        if (this != &other) {
            pid_ = std::exchange(other.pid_, INVALID_PID);
            stdin_pipe = std::move(other.stdin_pipe);
            stdout_pipe = std::move(other.stdout_pipe);
            stderr_pipe = std::move(other.stderr_pipe);
        }
        return *this;
    }

    ~Child() {
        if (IsValid()) {
            Kill();
            ::wait4(pid_, nullptr, WNOHANG, nullptr);
        }
    }

    void Kill() const noexcept {
        if (IsValid()) {
            ::kill(pid_, SIGKILL);
        }
    }

    [[nodiscard]] std::expected<ExitStatus, std::error_code> Wait();

    [[nodiscard]] std::expected<std::optional<ExitStatus>, std::error_code> TryWait();

    template <typename Rep, typename Period>
    [[nodiscard]] std::expected<ExitStatus, std::error_code> WaitWithTimeout(std::chrono::duration<Rep, Period> timeout) {
        auto start_time = std::chrono::steady_clock::now();

        while (true) {
            auto result = TryWait();

            if (!result.has_value()) {
                return std::unexpected(result.error());
            } else if (result.value().has_value()) {
                return result.value().value();
            }

            auto now = std::chrono::steady_clock::now();

            if (now - start_time > timeout) {
                Kill();
                return Wait();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    bool IsValid() const noexcept { return pid_ > 0; }

    pid_t GetPid() const noexcept { return pid_; }

    std::optional<FileDescriptor> stdin_pipe;
    std::optional<FileDescriptor> stdout_pipe;
    std::optional<FileDescriptor> stderr_pipe;

private:
    pid_t pid_;
};

struct ResourceLimits {
    std::optional<rlim_t> cpu_time_sec;

    std::optional<rlim_t> memory_bytes;

    std::optional<rlim_t> file_size_bytes;

    std::optional<rlim_t> process_count;
};

class Command {
public:
    explicit Command(std::filesystem::path program) : program_(std::move(program)) {}

    Command& Arg(std::string arg) {
        args_.push_back(std::move(arg));
        return *this;
    }

    Command& Args(const std::vector<std::string>& args) {
        args_.insert(args_.end(), args.begin(), args.end());
        return *this;
    }

    Command& CurrentDir(std::filesystem::path dir) {
        cwd_ = std::move(dir);
        return *this;
    }

    Command& Env(std::string key, std::string value) {
        envs_[std::move(key)] = std::move(value); 
        removed_envs_.erase(key);
        return *this;
    }

    Command& EnvRemove(std::string key) {
        envs_.erase(key);
        removed_envs_.insert(std::move(key));
        return *this;
    }

    Command& EnvClear() {
        envs_.clear();
        removed_envs_.clear();
        is_env_cleared_ = true;
        return *this;
    }

    Command& Stdin(Stdio cfg) {
        stdin_cfg_ = std::move(cfg);
        return *this;
    }

    Command& Stdout(Stdio cfg) {
        stdout_cfg_ = std::move(cfg);
        return *this;
    }

    Command& Stderr(Stdio cfg) {
        stderr_cfg_ = std::move(cfg);
        return *this;
    }

    Command& Limits(const ResourceLimits& limits) {
        limits_ = limits;
        return *this;
    }

    std::expected<Child, std::error_code> Spawn();

private:
    std::filesystem::path program_;
    std::vector<std::string> args_;

    std::optional<std::filesystem::path> cwd_;

    std::unordered_map<std::string, std::string> envs_;
    std::unordered_set<std::string> removed_envs_;
    bool is_env_cleared_ = false;

    Stdio stdin_cfg_ = Stdio::Inherit();
    Stdio stdout_cfg_ = Stdio::Inherit();
    Stdio stderr_cfg_ = Stdio::Inherit();

    ResourceLimits limits_;
};

} // namespace process

} // namespace coj