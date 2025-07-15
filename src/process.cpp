#include <chrono>
#include <iostream>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <spawn.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/wait.h>

#include "exception.h"
#include "process.h"

#include <filesystem>

extern char **environ;

namespace coj {

IPCSource::IPCSource(const std::filesystem::path& file) 
    : option(IPCOption::FILE), source(std::make_unique<IFileImpl>(file)) {}

IPCSource::IPCSource(IPCOption option) : option(option) {
    switch (option) {
        case IPCOption::NONE: { break; }
        case IPCOption::PIPE: {
            int pipe_fd[2];
            if (::pipe(pipe_fd) == -1)
                throw OSError("Failed to open pipe");
            pipe_reader = std::make_shared<IFileImpl>(pipe_fd[0]);
            pipe_writer = std::make_shared<OFileImpl>(pipe_fd[1]);
            break;
        }
        default: { throw std::invalid_argument("Invalid IPC option for standard input"); }
    }
}


IPCDestination::IPCDestination(const std::filesystem::path& file)
    : option(IPCOption::FILE), destination(std::make_unique<OFileImpl>(file)) {}

IPCDestination::IPCDestination(IPCOption option) : option(option) {
    switch (option) {
        case IPCOption::NONE: { break; }
        case IPCOption::PIPE: {
            int pipe_fd[2];
            if (::pipe(pipe_fd) == -1)
                throw OSError("Failed to open pipe");
            pipe_reader = std::make_shared<IFileImpl>(pipe_fd[0]);
            pipe_writer = std::make_shared<OFileImpl>(pipe_fd[1]);
            break;
        }
        case IPCOption::STDOUT:  { break; }
        case IPCOption::DEVNULL: {
            int fd = ::open("/dev/null", O_WRONLY);
            if (fd == -1)
                throw OSError("Failed open dev/null");
            destination = std::make_unique<OFileImpl>(fd);
            break;
        }
        default: { throw std::invalid_argument("Invalid IPC option for standard error"); }
    }
}

Popen::~Popen() {
    if (!Poll()) {
        Terminate();
        try {
            Wait(Seconds(5.0));
        } catch (const TimeoutExpired& e) {
            try {
                Kill();
            } catch (...) {
                std::cerr << "[Popen] Failed to kill subprocess after timeout in destructor\n";
            }
        } catch (...) {
            std::cerr << "[Popen] Unexpected error while waiting for subprocess in destructor\n";
        }
    }
}

Popen::Popen (
        const std::string& args, 
        IPCSource std_in, 
        IPCDestination std_out, 
        IPCDestination std_err
) : args_(Tokenize(args)), std_in_(std::move(std_in)), std_out_(std::move(std_out)), std_err_(std::move(std_err)) {
    if (args.empty())
        throw std::invalid_argument("empty args is not allowed");
    ::posix_spawn_file_actions_t actions; 
    ::posix_spawn_file_actions_init(&actions);

    switch (std_in_.option) {
        case IPCOption::NONE: break;
        case IPCOption::FILE:
            ::posix_spawn_file_actions_adddup2(&actions, std_in_.source->fileno(), STDIN_FILENO); 
            ::posix_spawn_file_actions_addclose(&actions, std_in_.source->fileno());
            break;
        case IPCOption::PIPE:
            ::posix_spawn_file_actions_adddup2(&actions, std_in_.pipe_reader->fileno(), STDIN_FILENO);
            ::posix_spawn_file_actions_addclose(&actions, std_in_.pipe_reader->fileno());
            ::posix_spawn_file_actions_addclose(&actions, std_in_.pipe_writer->fileno());
            break;
        default: throw std::invalid_argument("Invalid IPC option for standard input");
    }

    switch (std_out_.option) {
        case IPCOption::NONE: break;
        case IPCOption::FILE:
        case IPCOption::DEVNULL:
            ::posix_spawn_file_actions_adddup2(&actions, std_out_.destination->fileno(), STDOUT_FILENO); 
            ::posix_spawn_file_actions_addclose(&actions, std_out_.destination->fileno());
            break;
        case IPCOption::PIPE:
            ::posix_spawn_file_actions_adddup2(&actions, std_out_.pipe_writer->fileno(), STDOUT_FILENO);
            ::posix_spawn_file_actions_addclose(&actions, std_out_.pipe_reader->fileno());
            ::posix_spawn_file_actions_addclose(&actions, std_out_.pipe_writer->fileno());
            break;
        default: throw std::invalid_argument("Invalid IPC option for standard output");
    }

    switch (std_err_.option) {
        case IPCOption::NONE: break;
        case IPCOption::FILE:
        case IPCOption::DEVNULL:
            ::posix_spawn_file_actions_adddup2(&actions, std_err_.destination->fileno(), STDERR_FILENO); 
            ::posix_spawn_file_actions_addclose(&actions, std_err_.destination->fileno()); 
            break;
        case IPCOption::PIPE:
            ::posix_spawn_file_actions_adddup2(&actions, std_err_.pipe_writer->fileno(), STDERR_FILENO);
            ::posix_spawn_file_actions_addclose(&actions, std_err_.pipe_reader->fileno());
            ::posix_spawn_file_actions_addclose(&actions, std_err_.pipe_writer->fileno());
            break;
        case IPCOption::STDOUT:
            /*
            if      (std_out_.option == IPCOption::FILE || std_out_.option == IPCOption::DEVNULL)
                ::posix_spawn_file_actions_adddup2(&actions, std_out_.destination->fileno(), STDERR_FILENO); 
            else if (std_out_.option == IPCOption::PIPE)
                ::posix_spawn_file_actions_adddup2(&actions, std_out_.pipe_writer->fileno(), STDERR_FILENO);
            */
            ::posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
            break;
        default: throw std::invalid_argument("Invalid IPC option for standard error");
    }

    c_args();
    int status = ::posix_spawnp(&pid_, args_[0].c_str(), &actions, NULL, c_args(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    if (status != 0)
        throw OSError("Failed to spawn process");

    // close unnessary files
    std_in_.source.reset(); // source file
    std_in_.pipe_reader.reset(); // input pipe read-end

    std_out_.destination.reset(); // destination file of output
    std_out_.pipe_writer.reset(); // output pipe write-end

    std_err_.destination.reset(); // destination file of error
    std_err_.pipe_writer.reset(); // error pipe write-end
}

std::optional<int> Popen::Poll() {
    if (returncode_.has_value())
        return returncode();
    
    int status;
    ::rusage usage;
    int pid = ::wait4(pid_, &status, WNOHANG, &usage);

    if (pid == -1) {
        throw OSError("Failed to wait process");
    } else if (pid == pid_) {
        set_returncode(status);
        usage_ = usage; 
    }

    return returncode();
}

std::optional<int> Popen::Wait(std::optional<Seconds> timeout) {
    if (!timeout.has_value()) {
        while (!Poll())
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return returncode();
    } else {
        auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout.value()) {
            std::cerr << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << "sec\n";
            if (Poll())
                return returncode();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        throw TimeoutExpired(args(), timeout.value());
    }
}

// Original impl: Do write first, then Wait(timeout)
// This would cause blocking because input is large enough the buffer is full, then child process can't write
// So, parent process keep waiting
// To solve the problem, introduce multithreading for read/write
IPCResult Popen::Communicate(const Bytes& input, std::optional<Seconds> timeout) {
    std::atomic<bool> is_expired(false);

    std::cerr << "Start Communication\n";

    std::future<void> std_in_writer;
    if (!input.empty()) {
        if (std_in_.pipe_writer == nullptr)
            throw std::invalid_argument("Cannot write to closed pipe");
        std_in_writer = std::async(std::launch::async, [this, &input, &is_expired]() {
            size_type bytes_written = 0;
            while (bytes_written < input.size()) {
                if (is_expired.load()) {
                    cache_.bytes_written = bytes_written;
                    return;
                }
                size_type bytes_to_write 
                    = std::min(static_cast<size_type>(BUFSIZ), input.size() - bytes_written);
                std_in_.pipe_writer->Write(input, bytes_written, bytes_to_write);
                bytes_written += bytes_to_write;
            }
            std_in_.pipe_writer.reset();
            cache_.bytes_written = bytes_written;
        });
    }

    std::future<void> std_out_reader;
    if (std_out_.pipe_reader != nullptr) {
        std_out_reader = std::async(std::launch::async, [this, &input, &is_expired] {
            while (!std_out_.pipe_reader->is_eof()) {
                if (is_expired.load()) return;
                Bytes chunk = std_out_.pipe_reader->Read(BUFSIZ);
                if (!chunk.empty())
                    cache_.stdout_data.insert(cache_.stdout_data.end(), chunk.begin(), chunk.end());
            }
            std_out_.pipe_reader.reset();
        });
    }

    std::future<void> std_err_reader;
    if (std_err_.pipe_reader != nullptr) {
        std_err_reader = std::async(std::launch::async, [this, &input, &is_expired] {
            while (!std_err_.pipe_reader->is_eof()) {
                if (is_expired.load()) return;
                Bytes chunk = std_err_.pipe_reader->Read(BUFSIZ);
                if (!chunk.empty())
                    cache_.stderr_data.insert(cache_.stderr_data.end(), chunk.begin(), chunk.end());
            }
            std_err_.pipe_reader.reset();
        });
    }

    try {
        std::cerr << "Start Wait\n";
        Wait(timeout); // May throw TimeoutExpired
    } catch (const TimeoutExpired& e) {
        std::cerr << "Timeout!\n";
        is_expired.store(true);

        if (std_in_writer.valid()) std_in_writer.wait();
        std::cerr << "STDIN!\n";
        if (std_out_reader.valid()) std_out_reader.wait();
        std::cerr << "STDOUT!\n";
        if (std_err_reader.valid()) std_err_reader.wait();
        std::cerr << "STDERR!\n";

        std::cerr << "Thread End!\n";

        throw TimeoutExpired (
            "Timeout while communicating with process",
            timeout.value(),
            cache_.bytes_written,
            cache_.stdout_data,
            cache_.stderr_data
        );
    }

    if (std_in_writer.valid()) std_in_writer.wait();
    if (std_out_reader.valid()) std_out_reader.wait();
    if (std_err_reader.valid()) std_err_reader.wait();

    IPCResult result(std::move(cache_));

    cache_.bytes_written = 0;
    cache_.stdout_data.clear();
    cache_.stderr_data.clear();

    return result;
}

void Popen::SendSignal(int signal) {
    if (!returncode() && ::kill(pid_, signal) == -1)
        throw OSError("Failed to kill process");
}

void Popen::Terminate() { SendSignal(SIGTERM); }
void Popen::Kill() { SendSignal(SIGKILL); }

std::string Popen::args() const {
    std::string buffer;
    for (size_t i = 0; i < args_.size(); ++i) {
        buffer += args_[i];
        if (i != args_.size() - 1)
            buffer += ' ';
    }
    return buffer;
}

pid_t Popen::pid() const { return pid_; }
std::optional<int> Popen::returncode() const { return returncode_; }
std::optional<::rusage> Popen::usage() const { return usage_; }

auto Popen::std_in() const -> PipeHandle { return std_in_.pipe_writer; }
auto Popen::std_out() const -> PipeHandle { return std_out_.pipe_reader; }
auto Popen::std_err() const -> PipeHandle { return std_err_.pipe_reader; }

// TODO: Argument parser should be improved
//       Check details in CommandWithSpecialCharacters of test/src/process.cpp
std::vector<std::string> Popen::Tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::string current_token;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];

        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        } else if (std::isspace(c) && !in_single_quote && !in_double_quote) {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
        } else {
            current_token += c;
        }
    }

    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }
    return tokens;
}

char *const * Popen::c_args() {
    if (!c_args_.empty())
        return c_args_.data();
    c_args_.reserve(args_.size());
    for (auto& arg : args_) {
        c_args_.push_back(const_cast<char*>(arg.c_str()));
    } 
    c_args_.push_back(nullptr);
    return c_args_.data();
}

void Popen::set_returncode(int status) {
    if (WIFSIGNALED(status))
        returncode_ = -WTERMSIG(status);
    else if (WIFEXITED(status))
        returncode_ = WEXITSTATUS(status);
    else throw std::runtime_error("Invalid returncode detected");
}

} // namespace coj