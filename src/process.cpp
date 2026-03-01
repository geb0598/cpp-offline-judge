#include <dirent.h>

#include "coj/file_io.h"
#include "coj/process.h"

namespace coj {

namespace process {

std::expected<Child, std::error_code> Command::Spawn() {
    std::optional<FileDescriptor> parent_stdin_pipe; 
    std::optional<FileDescriptor> parent_stdout_pipe;
    std::optional<FileDescriptor> parent_stderr_pipe;

    FileDescriptor child_stdin_fd;
    FileDescriptor child_stdout_fd;
    FileDescriptor child_stderr_fd;

    if (stdin_cfg_.GetType() == Stdio::Type::Piped) {
        int p[2];
        if (::pipe2(p, O_CLOEXEC) == -1) {
            return std::unexpected(std::error_code(errno, std::generic_category()));
        }
        parent_stdin_pipe.emplace(p[1]);
        child_stdin_fd = FileDescriptor(p[0]);
    } else if (stdin_cfg_.GetType() == Stdio::Type::Null) {
        auto open_result = Open("/dev/null", O_RDONLY | O_CLOEXEC);
        if (!open_result.has_value()) {
            return std::unexpected(open_result.error());
        }
        child_stdin_fd = std::move(open_result.value());
    } else if (stdin_cfg_.GetType() == Stdio::Type::File) {
        child_stdin_fd = stdin_cfg_.TakeFd();
    }

    if (stdout_cfg_.GetType() == Stdio::Type::Piped) {
        int p[2];
        if (::pipe2(p, O_CLOEXEC) == -1) {
            return std::unexpected(std::error_code(errno, std::generic_category()));
        }
        parent_stdout_pipe.emplace(p[0]);
        child_stdout_fd = FileDescriptor(p[1]);
    } else if (stdout_cfg_.GetType() == Stdio::Type::Null) {
        auto open_result = Open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (!open_result.has_value()) {
            return std::unexpected(open_result.error());
        }
        child_stdout_fd = std::move(open_result.value());
    } else if (stdout_cfg_.GetType() == Stdio::Type::File) {
        child_stdout_fd = stdout_cfg_.TakeFd();
    }

    if (stderr_cfg_.GetType() == Stdio::Type::Piped) {
        int p[2];
        if (::pipe2(p, O_CLOEXEC) == -1) {
            return std::unexpected(std::error_code(errno, std::generic_category()));
        }
        parent_stderr_pipe.emplace(p[0]);
        child_stderr_fd = FileDescriptor(p[1]);
    } else if (stderr_cfg_.GetType() == Stdio::Type::Null) {
        auto open_result = Open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (!open_result.has_value()) {
            return std::unexpected(open_result.error());
        }
        child_stderr_fd = std::move(open_result.value());
    } else if (stderr_cfg_.GetType() == Stdio::Type::File) {
        child_stderr_fd = stderr_cfg_.TakeFd();
    }

    std::vector<char*> argv_ptrs;
    argv_ptrs.push_back(const_cast<char*>(program_.c_str()));
    for (const auto& arg : args_) {
        argv_ptrs.push_back(const_cast<char*>(arg.c_str()));
    }
    argv_ptrs.push_back(nullptr);

    std::vector<std::string> env_strings;
    std::unordered_map<std::string, std::string> env_map;

    if (!is_env_cleared_ && environ != nullptr) {
        for (char **env = environ; *env != nullptr; ++env) {
            std::string env_str(*env);
            auto pos = env_str.find('=');
            if (pos != std::string::npos) {
                std::string key = env_str.substr(0, pos);
                if (removed_envs_.find(key) == removed_envs_.end()) {
                    env_map[key] = env_str.substr(pos + 1);
                }
            }
        }
    }

    for (const auto& [key, value] : envs_) {
        env_map[key] = value;
    }

    for (const auto& [key, value] : env_map) {
        env_strings.push_back(key + "=" + value);
    }

    std::vector<char*> env_ptrs;
    for (const auto& env_str : env_strings) {
        env_ptrs.push_back(const_cast<char*>(env_str.c_str()));
    }
    env_ptrs.push_back(nullptr);

    int err_p[2];
    if (::pipe2(err_p, O_CLOEXEC) == -1) {
        return std::unexpected(std::error_code(errno, std::generic_category()));
    }

    FileDescriptor err_read(err_p[0]);
    FileDescriptor err_write(err_p[1]);

    pid_t pid = ::fork();

    if (pid < 0) {
        return std::unexpected(std::error_code(errno, std::generic_category()));
    } else if (pid == 0) {
        err_read.Close();

        bool is_successful = true;

        if (child_stdin_fd.IsValid() && child_stdin_fd.Get() != STDIN_FILENO) {
            if (::dup2(child_stdin_fd.Get(), STDIN_FILENO) == -1) {
                is_successful = false;
            }
        }
        if (child_stdout_fd.IsValid() && child_stdout_fd.Get() != STDOUT_FILENO) {
            if (::dup2(child_stdout_fd.Get(), STDOUT_FILENO) == -1) {
                is_successful = false;
            }
        }
        if (child_stderr_fd.IsValid() && child_stderr_fd.Get() != STDERR_FILENO) {
            if (::dup2(child_stderr_fd.Get(), STDERR_FILENO) == -1) {
                is_successful = false;
            }
        }

        if (cwd_.has_value()) {
            if (::chdir(cwd_->c_str()) == -1) {
                is_successful = false;
            }
        }

        if (limits_.cpu_time_sec.has_value()) {
            rlimit rl;
            rl.rlim_cur = limits_.cpu_time_sec.value();
            rl.rlim_max = limits_.cpu_time_sec.value() + 1;
            if (::setrlimit(RLIMIT_CPU, &rl) == -1) {
                is_successful = false;
            }
        }

        if (limits_.memory_bytes.has_value()) {
            rlimit rl;
            rl.rlim_cur = limits_.memory_bytes.value();
            rl.rlim_max = limits_.memory_bytes.value();
            if (::setrlimit(RLIMIT_AS, &rl) == -1) {
                is_successful = false;
            }
        }

        if (limits_.file_size_bytes.has_value()) {
            rlimit rl;
            rl.rlim_cur = limits_.file_size_bytes.value();
            rl.rlim_max = limits_.file_size_bytes.value();
            if (::setrlimit(RLIMIT_FSIZE, &rl) == -1) {
                is_successful = false;
            }
        }

        if (limits_.process_count.has_value()) {
            rlimit rl;
            rl.rlim_cur = limits_.process_count.value();
            rl.rlim_max = limits_.process_count.value();
            if (::setrlimit(RLIMIT_NPROC, &rl) == -1) {
                is_successful = false;
            }
        }

        if (is_successful) {
            int err_fd = err_write.Get();
            bool is_closed = false;

        #ifdef SYS_close_range
            int left_res = 0;
            if (err_fd > 3) {
                left_res = ::close_range(3, err_fd - 1, 0);
            }

            int right_res = ::close_range(err_fd + 1, ~0U, 0);

            if (left_res == 0 && right_res == 0) {
                is_closed = true;
            }
        #endif

            if (!is_closed) {
                long max_fd = ::sysconf(_SC_OPEN_MAX);
                if (max_fd < 0) {
                    max_fd = _POSIX_OPEN_MAX;
                }
                for (int fd = 3; fd < max_fd; ++fd) {
                    if (fd != err_write.Get()) {
                        ::close(fd);
                    }
                }
            }

            ::execvpe(program_.c_str(), argv_ptrs.data(), env_ptrs.data());
        }

        int err = errno;
        (void)Write(err_write.Get(), std::as_bytes(std::span(&err, 1)));

        ::_exit(EXIT_FAILURE);
    } 

    err_write.Close();

    int child_err = 0;
    auto read_result = Read(err_read.Get(), std::as_writable_bytes(std::span(&child_err, 1)));

    if (read_result.status == IoStatus::Success && read_result.bytes > 0) {
        ::waitpid(pid, nullptr, 0);
        return std::unexpected(std::error_code(child_err, std::generic_category()));
    }

    Child child(pid);
    child.stdin_pipe = std::move(parent_stdin_pipe);
    child.stdout_pipe = std::move(parent_stdout_pipe);
    child.stderr_pipe = std::move(parent_stderr_pipe);

    return child;
}

std::expected<ExitStatus, std::error_code> Child::Wait() {
    if (!IsValid()) {
        return std::unexpected(std::error_code(ECHILD, std::generic_category()));
    }

    int status = 0;
    ::rusage usage = {};
    if (::wait4(pid_, &status, 0, &usage) == -1) {
        return std::unexpected(std::error_code(errno, std::generic_category()));
    }

    pid_ = INVALID_PID;
    return ExitStatus(status, usage);
}

std::expected<std::optional<ExitStatus>, std::error_code> Child::TryWait() {
    if (!IsValid()) {
        return std::unexpected(std::error_code(ECHILD, std::generic_category()));
    }

    int status = 0;
    ::rusage usage = {};
    pid_t result = ::wait4(pid_, &status, WNOHANG, &usage);

    if (result == 0) {
        return std::nullopt;
    } else if (result == -1) {
        return std::unexpected(std::error_code(errno, std::generic_category()));
    }

    pid_ = INVALID_PID;
    return ExitStatus(status, usage);
}

} // namespace process

} // namespace coj