#ifndef PROCESS_H
#define PROCESS_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <sys/resource.h>

#include "bytes.h"
#include "exception.h"
#include "file.h"

namespace coj {

enum class IPCOption { NONE, FILE, PIPE, STDOUT, DEVNULL };

struct IPCSource {
    IPCSource(const std::filesystem::path& file);
    IPCSource(IPCOption option);
    IPCSource(IPCSource&&) = default;

    IPCSource(const IPCSource&) = delete;
    IPCSource& operator=(const IPCSource&) = delete;
    IPCSource& operator=(IPCSource&&) = delete;

    IPCOption option;
    std::shared_ptr<Pipe> pipe_reader;
    std::shared_ptr<Pipe> pipe_writer;
    std::unique_ptr<Pipe> source;
};

struct IPCDestination {
    IPCDestination(const std::filesystem::path& file);
    IPCDestination(IPCOption option);
    IPCDestination(IPCDestination&&) = default;

    IPCDestination(const IPCDestination&) = delete;
    IPCDestination& operator=(const IPCDestination&) = delete;
    IPCDestination& operator=(IPCDestination&&) = delete;

    IPCOption option;
    std::shared_ptr<Pipe> pipe_reader;
    std::shared_ptr<Pipe> pipe_writer;
    std::unique_ptr<Pipe> destination;
};

struct IPCResult {
    Bytes::size_type bytes_written;
    Bytes stdout_data;
    Bytes stderr_data;
};

class Popen {
public:
    using value_type = Bytes::value_type;
    using size_type = Bytes::size_type;
    using Seconds = std::chrono::duration<double>;
    using PipeHandle = std::weak_ptr<Pipe>;

    ~Popen();
    Popen (
        const std::string& args, 
        IPCSource std_in = IPCOption::NONE, 
        IPCDestination std_out = IPCOption::NONE, 
        IPCDestination std_err = IPCOption::NONE
    );
    Popen(const Popen&) = delete;
    Popen(Popen&&) = delete;
    Popen& operator=(const Popen&) = delete;
    Popen& operator=(Popen&&) = delete;

    std::optional<int> Poll();
    // When output pipe of subprocess is full, print is blocking. Then, wait call might also blocked
    // Use communicate to escape from that situation
    std::optional<int> Wait(std::optional<Seconds> timeout = std::nullopt);

    // Output might be lost when timeout expired
    // TODO: use cache to store output not to be lost
    IPCResult Communicate(const Bytes& input, std::optional<Seconds> timeout = std::nullopt);

    void SendSignal(int signal);
    void Terminate();
    void Kill();

    std::string args() const;
    pid_t pid() const;
    std::optional<int> returncode() const;
    std::optional<::rusage> usage() const;

    PipeHandle std_in() const;
    PipeHandle std_out() const;
    PipeHandle std_err() const;

private:
    std::vector<std::string> Tokenize(const std::string& s);

    char* const* c_args();

    void set_returncode(int status);

    std::vector<std::string> args_;
    std::vector<char*> c_args_;
    pid_t pid_;
    std::optional<int> returncode_;
    std::optional<::rusage> usage_;

    IPCSource std_in_;
    IPCDestination std_out_;
    IPCDestination std_err_;
    IPCResult cache_;
};

} // namespace coj

#endif