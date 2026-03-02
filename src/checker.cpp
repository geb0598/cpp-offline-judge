#include <charconv>
#include <fstream>

#include "coj/checker.h"

namespace coj {

namespace {

std::expected<double, std::error_code> ParseFloat(const std::string& str) {
    auto first = str.data();
    auto last = str.data() + str.size();
    double result;
    auto [ptr, ec] = std::from_chars(first, last, result);

    if (ec == std::errc() && ptr == last) {
        return result;
    } else {
        return std::unexpected(std::make_error_code(ec));
    }
}

bool IsFloatEqual(double a, double b, double epsilon) {
    double diff = std::abs(a - b);

    if (diff <= epsilon) {
        return true;
    }

    double max_abs = std::max(std::abs(a), std::abs(b));
    return diff <= epsilon * max_abs;
}

} // namespace

std::expected<CheckResult, std::error_code> Check(const CheckConfig &config) {
    std::ifstream answer_file(config.answer_path);
    if (!answer_file.is_open()) {
        return std::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
    }

    std::ifstream output_file(config.output_path);
    if (!output_file.is_open()) {
        return CheckResult::WrongAnswer;
    }

    std::string a_tok;
    std::string o_tok;

    while (answer_file >> a_tok) {
        if (!(output_file >> o_tok)) {
            return CheckResult::WrongAnswer;
        }

        if (config.epsilon.has_value()) {
            auto a_res = ParseFloat(a_tok);
            auto o_res = ParseFloat(o_tok);

            if (a_res.has_value() && o_res.has_value()) {
                if (!IsFloatEqual(a_res.value(), o_res.value(), config.epsilon.value())) {
                    return CheckResult::WrongAnswer;
                }
                continue;
            }
        }

        if (a_tok != o_tok) {
            return CheckResult::WrongAnswer;
        }
    }

    if (output_file >> o_tok) {
        return CheckResult::WrongAnswer;
    }
    
    return CheckResult::Accepted;
}

} // namespace coj