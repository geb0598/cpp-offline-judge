#include "gtest/gtest.h"
#include "exception.h"
#include <string>
#include <system_error>
#include <chrono>
#include <optional>

namespace coj {

TEST(OSErrorTest, BuildMessage) {
    std::error_code ec(2, std::system_category());
    std::string what_arg = "test message";
    OSError err(ec, what_arg);
    std::string expected_message = "[Errno 2] " + ec.message() + ": " + what_arg;
    EXPECT_STREQ(err.what(), expected_message.c_str());
}

TEST(OSErrorTest, ConstructorWithErrorCode) {
    std::error_code ec(1, std::system_category());
    OSError err(ec, "Additional info");
    EXPECT_EQ(err.code(), ec);
}

TEST(TimeoutExpiredTest, ConstructorAndAccessors) {
    std::string cmd = "test_command";
    std::chrono::duration<double> timeout(10.5);
    Bytes stdout_bytes = {'s', 't', 'd', 'o', 'u', 't'};
    Bytes stderr_bytes = {'s', 't', 'd', 'e', 'r', 'r'};

    TimeoutExpired err(cmd, timeout, std::nullopt, stdout_bytes, stderr_bytes);

    EXPECT_EQ(err.cmd(), cmd);
    EXPECT_EQ(err.timeout(), timeout);
    EXPECT_TRUE(err.std_out().has_value());
    EXPECT_EQ(err.std_out().value(), stdout_bytes);
    EXPECT_TRUE(err.std_err().has_value());
    EXPECT_EQ(err.std_err().value(), stderr_bytes);
}

TEST(TimeoutExpiredTest, WhatMessage) {
    std::string cmd = "another_command";
    std::chrono::duration<double> timeout(5.0);

    TimeoutExpired err(cmd, timeout);

    std::string expected_message = "Command 'another_command' timed out after 5.000000 seconds";
    std::string actual_message = err.what();
    
    // We will check for the prefix and suffix of the what() message, 
    // as the exact float representation might vary slightly.
    EXPECT_TRUE(actual_message.find("Command 'another_command' timed out after") != std::string::npos);
    EXPECT_TRUE(actual_message.find("seconds") != std::string::npos);
}

} // namespace coj