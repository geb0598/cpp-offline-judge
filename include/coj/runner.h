#pragma once

#include "coj/process.h"

namespace coj {

enum class RunStatus {
    Success,
    RuntimeError,
    TimeLimit,
    MemoryLimit,
    OutputLimit
};

struct RunLimits {
    std::chrono::milliseconds cpu_time;
    size_t memory_kb;
};

struct RunConfig {
    std::filesystem::path exec_path;
    std::filesystem::path input_path;
    std::filesystem::path output_path;
    std::filesystem::path work_dir;

    RunLimits soft_limits;
    process::ResourceLimits hard_limits;
};

struct RunResult {
    RunStatus status;
    process::ExitStatus exit_status;
};

[[nodiscard]] std::expected<RunResult, std::error_code> Run(const RunConfig& config); 

} // namespace coj