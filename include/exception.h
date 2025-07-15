#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <sstream>
#include <system_error>

#include "bytes.h"

namespace coj {

inline std::error_code GetLastErrorCode() noexcept {
#ifdef _WIN32
    return std::error_code(::GetLastError(), std::system_category());
#else
    return std::error_code(errno, std::system_category());
#endif
}

class OSError : public std::system_error {
public:
    virtual ~OSError() = default;

    OSError(int ev, const std::error_category& ecat, const std::string& what_arg = "")
        : std::system_error(ev, ecat, ""), message_(build_message(ev, ecat, what_arg)) {}

    OSError(std::error_code ec, const std::string& what_arg = "")
        : std::system_error(ec, ""), message_(build_message(ec.value(), ec.category(), what_arg)) {}

    explicit OSError(const std::string& what_arg)
        : std::system_error(GetLastErrorCode(), ""), message_(build_message(GetLastErrorCode(), what_arg)) {}

    const char* what() const noexcept override {
        return message_.c_str();
    }

private:
    std::string message_;
    static std::string build_message(int ev, const std::error_category& cat, const std::string& prefix) {
        std::ostringstream oss;
        oss << "[Errno " << ev << "] " << cat.message(ev);
        if (!prefix.empty()) {
            oss << ": " << prefix;
        }
        return oss.str();
    }

    static std::string build_message(std::error_code ec, const std::string& prefix) {
        return build_message(ec.value(), ec.category(), prefix);
    }
};

class TimeoutExpired : public std::runtime_error {
public:
    TimeoutExpired (
        std::string cmd,
        std::chrono::duration<double> timeout,
        std::optional<Bytes::size_type> bytes_written = std::nullopt,
        std::optional<Bytes> std_out = std::nullopt,
        std::optional<Bytes> std_err = std::nullopt
    )
        : std::runtime_error(build_message(cmd, timeout)),
          cmd_(std::move(cmd)),
          timeout_(timeout),
          bytes_written_(bytes_written),
          output_(std::move(std_out)),
          stderr_(std::move(std_err)) {}

    const std::string& cmd() const noexcept { return cmd_; }
    std::chrono::duration<double> timeout() const noexcept { return timeout_; }
    std::optional<Bytes::size_type> bytes_written() const noexcept { return bytes_written_; }
    const std::optional<Bytes>& std_out() const noexcept { return output_; }
    const std::optional<Bytes>& std_err() const noexcept { return stderr_; }

private:
    std::string cmd_;
    std::chrono::duration<double> timeout_;
    std::optional<Bytes::size_type> bytes_written_;
    std::optional<Bytes> output_;
    std::optional<Bytes> stderr_;

    static std::string build_message(const std::string& cmd, std::chrono::duration<double> timeout) {
        std::ostringstream oss;
        oss << "Command '" << cmd << "' timed out after " << timeout.count() << " seconds";
        return oss.str();
    }
};

} // namespace coj

#endif