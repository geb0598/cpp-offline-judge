#include <chrono>

#include "coj/runner.h"

namespace coj {

using namespace std::chrono;

std::expected<RunResult, std::error_code> Run(const RunConfig &config) {
    process::Command command(config.exec_path.string());

    auto input_fd_res = Open(config.input_path.c_str(), O_RDONLY);
    if (!input_fd_res.has_value()) {
        return std::unexpected(input_fd_res.error());
    }

    auto output_fd_res = Open(config.output_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC);
    if (!output_fd_res.has_value()) {
        return std::unexpected(output_fd_res.error());
    }

    command.Stdin(process::Stdio::From(std::move(*input_fd_res)))
        .Stdout(process::Stdio::From(std::move(*output_fd_res)))
        .Stderr(process::Stdio::Null())
        .Limits(config.hard_limits);

    auto child_res = command.Spawn();
    if (!child_res.has_value()) {
        return std::unexpected(child_res.error());
    }

    seconds timeout = ceil<seconds>(config.soft_limits.cpu_time) + 1s;
    if (config.hard_limits.cpu_time_sec.has_value()) {
        timeout = seconds(config.hard_limits.cpu_time_sec.value()) + 1s;
    }

    auto wait_res = child_res->WaitWithTimeout(timeout);
    if (!wait_res.has_value()) {
        return std::unexpected(wait_res.error());
    }

    const auto& exit_status = wait_res.value();

    RunStatus status = RunStatus::Success;

    if (!exit_status.Success()) {
        if (exit_status.Signal().has_value()) {
            int sig = exit_status.Signal().value();
            if (sig == SIGKILL || sig == SIGXCPU) {
                status = RunStatus::TimeLimit;
            } else if (sig == SIGXFSZ) {
                status = RunStatus::OutputLimit;
            } else {
                status = RunStatus::RuntimeError;
            }
        } else {
            status = RunStatus::RuntimeError;
        }
    } else {
        if (exit_status.GetCpuTime() > config.soft_limits.cpu_time) {
            status = RunStatus::TimeLimit;
        } else if (exit_status.GetMaxMemoryKb() > config.soft_limits.memory_kb) {
            status = RunStatus::MemoryLimit;
        }
    } 

    return RunResult {
        .status = status,
        .exit_status = exit_status
    };
}

} // namespace coj