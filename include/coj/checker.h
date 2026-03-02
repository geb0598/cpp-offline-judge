#pragma once

#include <expected>
#include <filesystem>
#include <optional>

namespace coj {

enum class CheckResult {
    Accepted,
    WrongAnswer
};

struct CheckConfig {
    std::filesystem::path output_path;
    std::filesystem::path answer_path;

    std::optional<double> epsilon;
};

[[nodiscard]] std::expected<CheckResult, std::error_code> Check(const CheckConfig& config);

} // namespace coj