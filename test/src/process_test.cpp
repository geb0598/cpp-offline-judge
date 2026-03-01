#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "coj/file_io.h"
#include "coj/process.h"

namespace coj {

namespace process {

namespace {

using namespace std::chrono_literals;

std::string ReadAll(int fd) {
    std::string result;
    std::vector<std::byte> buffer(1024);
    while (true) {
        auto read_res = Read(fd, buffer);
        if (read_res.status == IoStatus::EoF) {
            break;
        }
        if (read_res.status == IoStatus::Success && read_res.bytes > 0) {
            result.append(reinterpret_cast<const char*>(buffer.data()), read_res.bytes);
        } else if (read_res.status == IoStatus::Error) {
            break;
        }
    }
    return result;
}

TEST(ProcessTest, Spawn_WithBinTrue_ReturnsSuccessExitStatus) {
    Command cmd("/bin/true");

    auto child_res = cmd.Spawn();
    ASSERT_TRUE(child_res.has_value());
    
    auto wait_res = child_res.value().Wait();

    ASSERT_TRUE(wait_res.has_value());
    EXPECT_TRUE(wait_res.value().Success());
    EXPECT_EQ(wait_res.value().Code().value_or(-1), 0);
}

TEST(ProcessTest, Spawn_WithBinFalse_ReturnsFailedExitStatus) {
    Command cmd("/bin/false");

    auto child_res = cmd.Spawn();
    ASSERT_TRUE(child_res.has_value());
    
    auto wait_res = child_res.value().Wait();

    ASSERT_TRUE(wait_res.has_value());
    EXPECT_FALSE(wait_res.value().Success());
    EXPECT_EQ(wait_res.value().Code().value_or(0), 1);
}

TEST(ProcessTest, Spawn_WithNonExistentProgram_FailsAndReturnsEnoent) {
    Command cmd("/path/to/absolutely/non_existent_binary");

    auto child_res = cmd.Spawn();

    ASSERT_FALSE(child_res.has_value());
    EXPECT_EQ(child_res.error().value(), ENOENT);
}

TEST(ProcessTest, Spawn_WithEchoAndStdoutPiped_CapturesCorrectOutput) {
    Command cmd("/bin/echo");
    cmd.Arg("Hello COJ System!")
       .Stdout(Stdio::Piped());

    auto child_res = cmd.Spawn();
    ASSERT_TRUE(child_res.has_value());
    auto& child = child_res.value();

    ASSERT_TRUE(child.stdout_pipe.has_value());
    std::string output = ReadAll(child.stdout_pipe->Get());
    
    (void)child.Wait();

    EXPECT_EQ(output, "Hello COJ System!\n");
}

TEST(ProcessTest, Spawn_WithCatAndPipedIo_EchoesInputToOutput) {
    Command cmd("/bin/cat");
    cmd.Stdin(Stdio::Piped())
       .Stdout(Stdio::Piped());

    auto child_res = cmd.Spawn();
    ASSERT_TRUE(child_res.has_value());
    auto& child = child_res.value();
    
    ASSERT_TRUE(child.stdin_pipe.has_value());
    ASSERT_TRUE(child.stdout_pipe.has_value());

    std::string input_data = "Input Data For Cat";
    auto input_span = std::as_bytes(std::span(input_data));

    (void)Write(child.stdin_pipe->Get(), input_span);
    child.stdin_pipe->Close(); 

    std::string output = ReadAll(child.stdout_pipe->Get());
    
    (void)child.Wait();

    EXPECT_EQ(output, input_data);
}

TEST(ProcessTest, Spawn_WithEnvClearAndSet_OutputsOnlySetVariables) {
    Command cmd("/usr/bin/env");
    cmd.EnvClear()
       .Env("COJ_MAGIC_KEY", "777")
       .Stdout(Stdio::Piped());

    auto child_res = cmd.Spawn();
    ASSERT_TRUE(child_res.has_value());
    auto& child = child_res.value();

    std::string output = ReadAll(child.stdout_pipe->Get());
    (void)child.Wait();

    EXPECT_EQ(output, "COJ_MAGIC_KEY=777\n");
}

TEST(ProcessTest, WaitWithTimeout_OnHangingProcess_KillsProcessAndReturnsSigkill) {
    Command cmd("/bin/sleep");
    cmd.Arg("10");

    auto child_res = cmd.Spawn();
    ASSERT_TRUE(child_res.has_value());
    auto& child = child_res.value();

    auto wait_res = child.WaitWithTimeout(100ms);

    ASSERT_TRUE(wait_res.has_value());
    EXPECT_FALSE(wait_res.value().Success());
    
    EXPECT_EQ(wait_res.value().Signal().value_or(0), SIGKILL);
}

TEST(ProcessTest, Spawn_WithCpuLimit_KillsProcessWithSigXcpuOrSigkill) {
    Command cmd("/bin/sh");
    cmd.Arg("-c").Arg("while true; do :; done");

    ResourceLimits limits;
    limits.cpu_time_sec = 1; 
    cmd.Limits(limits);

    auto child_res = cmd.Spawn();
    ASSERT_TRUE(child_res.has_value());
    
    auto wait_res = child_res.value().Wait();

    ASSERT_TRUE(wait_res.has_value());
    EXPECT_FALSE(wait_res.value().Success());
    
    auto sig = wait_res.value().Signal();
    ASSERT_TRUE(sig.has_value());
    EXPECT_TRUE(sig.value() == SIGXCPU || sig.value() == SIGKILL) 
        << "Expected SIGXCPU or SIGKILL, but got: " << sig.value();
}

TEST(ProcessTest, Spawn_WithFileSizeLimit_KillsProcessWithSigXfsz) {
    char tmp_template[] = "/tmp/coj_fsize_XXXXXX";
    int tmp_fd = ::mkstemp(tmp_template);
    ASSERT_NE(tmp_fd, -1) << "Failed to create a unique temporary file";
    
    ::close(tmp_fd); 

    Command cmd("/bin/dd");
    cmd.Arg("if=/dev/zero")
       .Arg(std::string("of=") + tmp_template)
       .Arg("bs=1M")
       .Arg("count=5");

    ResourceLimits limits;
    limits.file_size_bytes = 1 * 1024 * 1024;
    cmd.Limits(limits);

    auto child_res = cmd.Spawn();
    ASSERT_TRUE(child_res.has_value());
    
    auto wait_res = child_res.value().Wait();

    ::unlink(tmp_template);

    ASSERT_TRUE(wait_res.has_value());
    EXPECT_FALSE(wait_res.value().Success());

    auto sig = wait_res.value().Signal();
    ASSERT_TRUE(sig.has_value()) << "Process was not killed by a signal!";
    EXPECT_EQ(sig.value(), SIGXFSZ);
}

TEST(ProcessTest, Spawn_WithMemoryLimit_ProcessFailsToAllocateMemory) {
    Command cmd("/usr/bin/python3");
    cmd.Arg("-c").Arg("a = 'x' * 100000000");

    ResourceLimits limits;
    limits.memory_bytes = 20 * 1024 * 1024; 
    cmd.Limits(limits);

    auto child_res = cmd.Spawn();
    
    if (!child_res.has_value() && child_res.error().value() == ENOENT) {
        GTEST_SKIP() << "Python3 is not installed; skipping memory limit test.";
    }
    
    ASSERT_TRUE(child_res.has_value());
    auto wait_res = child_res.value().Wait();

    ASSERT_TRUE(wait_res.has_value());
    EXPECT_FALSE(wait_res.value().Success());

    bool killed_by_signal = wait_res.value().Signal().has_value();
    bool failed_with_code = wait_res.value().Code().value_or(0) != 0;
    
    EXPECT_TRUE(killed_by_signal || failed_with_code) 
        << "Process should have failed due to out of memory";
}

} // namespace

} // namespace process

} // namespace coj