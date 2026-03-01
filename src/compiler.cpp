#include "coj/compiler.h"
#include "coj/file_io.h"

namespace coj {

std::expected<CompileResult, std::error_code> CppCompiler::Compile(const std::filesystem::path &source_path, const std::filesystem::path &exec_dir) {
    std::filesystem::path exec_path = exec_dir / "main";

    command_.Arg(source_path.string())
        .Arg("-o")
        .Arg(exec_path.string())
        .Stdout(process::Stdio::Null())
        .Stderr(process::Stdio::Piped());

    auto child_res = command_.Spawn();
    if (!child_res.has_value()) {
        return std::unexpected(child_res.error());
    }

    CompileResult result;

    if (child_res->stderr_pipe.has_value()) {
        auto output = ReadAllAsString(child_res->stderr_pipe->Get());
        if (output.has_value()) {
            result.output = std::move(*output);
        } else {
            return std::unexpected(output.error());
        }
    }

    auto wait_res = child_res->Wait();
    if (!wait_res.has_value()) {
        return std::unexpected(wait_res.error());
    }

    result.is_successful = wait_res->Success();

    if (result.is_successful) {
        result.exec_path = exec_path;
    } 

    return result;
}

} // namespace coj