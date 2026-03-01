#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

#include "coj/process.h"

namespace coj {

struct CompileResult {
    bool is_successful = false;
    std::optional<std::filesystem::path> exec_path;
    std::string output;
};

class Compiler {
public:
    virtual ~Compiler() = default;

    [[nodiscard]] virtual std::expected<CompileResult, std::error_code> Compile(
        const std::filesystem::path& source_path,
        const std::filesystem::path& exec_dir 
    ) = 0; 
};

class CppCompiler : public Compiler {
public:
    explicit CppCompiler(std::string compiler_path = "/usr/bin/g++") : command_(compiler_path) {}

    CppCompiler& Arg(std::string arg) {
        command_.Arg(std::move(arg));
        return *this;
    }

    CppCompiler& Args(const std::vector<std::string>& args) {
        command_.Args(args);
        return *this;
    }

    CppCompiler& Limits(const process::ResourceLimits& limits) {
        command_.Limits(limits);
        return *this;
    }

    [[nodiscard]] virtual std::expected<CompileResult, std::error_code> Compile(
        const std::filesystem::path& source_path,
        const std::filesystem::path& exec_dir 
    ) override;

private:
    process::Command command_;
};

} // namespace coj