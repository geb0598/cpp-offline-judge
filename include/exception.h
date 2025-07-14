#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <system_error>

namespace coj {

std::error_code GetLastErrorCode() noexcept {
#ifdef _WIN32
    return std::error_code(GetLastErrorCode(), std::system_category());
#else
    return std::error_code(errno, std::system_category());
#endif
}

} // namespace coj

#endif