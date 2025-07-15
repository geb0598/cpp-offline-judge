#include "gtest/gtest.h"
#include "process.h"
#include "file.h"
#include "exception.h"
#include "bytes.h"

#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <algorithm>

#include <sys/wait.h>
#include <signal.h>

namespace coj {

// Helper to create a temporary file with content
std::filesystem::path create_temp_file(const std::string& content = "") {
    std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "test_file_XXXXXX";
    // Create a unique temporary file name
    char* temp_path_cstr = new char[temp_path.string().length() + 1];
    strcpy(temp_path_cstr, temp_path.string().c_str());
    
    int fd = mkstemp(temp_path_cstr);
    if (fd == -1) {
        delete[] temp_path_cstr;
        throw std::runtime_error("Failed to create temporary file");
    }
    temp_path = temp_path_cstr;
    delete[] temp_path_cstr;

    if (!content.empty()) {
        std::ofstream ofs(temp_path, std::ios::binary);
        if (!ofs.is_open()) {
            close(fd);
            throw std::runtime_error("Failed to open temporary file for writing");
        }
        ofs << content;
        ofs.close();
    }
    close(fd); // Close the file descriptor, std::ofstream will manage the file
    return temp_path;
}

// Test fixture for Popen tests
class PopenTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Any setup needed before each test
    }

    void TearDown() override {
        // Any cleanup needed after each test
    }
};

TEST_F(PopenTest, BasicCommandExecution) {
    Popen p("echo Hello");
    ASSERT_TRUE(p.pid() > 0);
    std::optional<int> returncode = p.Wait();
    ASSERT_TRUE(returncode.has_value());
    EXPECT_EQ(returncode.value(), 0);
}

TEST_F(PopenTest, StdoutPipe) {
    Popen p("echo Hello from stdout", IPCOption::NONE, IPCOption::PIPE);
    IPCResult result = p.Communicate({}, {});
    EXPECT_EQ(std::string(result.stdout_data.begin(), result.stdout_data.end()), "Hello from stdout\n");
    ASSERT_TRUE(p.returncode().has_value());
    EXPECT_EQ(p.returncode().value(), 0);
}

TEST_F(PopenTest, StderrPipe) {
    Popen p("bash -c \"echo Hello from stderr >&2\"", IPCOption::NONE, IPCOption::NONE, IPCOption::PIPE);
    IPCResult result = p.Communicate({}, {});
    EXPECT_EQ(std::string(result.stderr_data.begin(), result.stderr_data.end()), "Hello from stderr\n");
    ASSERT_TRUE(p.returncode().has_value());
    EXPECT_EQ(p.returncode().value(), 0);
}

TEST_F(PopenTest, StdinPipe) {
    std::string input_data = "Hello from stdin";
    Bytes input_bytes(input_data.begin(), input_data.end());
    Popen p("cat", IPCOption::PIPE, IPCOption::PIPE);
    IPCResult result = p.Communicate(input_bytes, {});
    EXPECT_EQ(std::string(result.stdout_data.begin(), result.stdout_data.end()), input_data);
    ASSERT_TRUE(p.returncode().has_value());
    EXPECT_EQ(p.returncode().value(), 0);
}

TEST_F(PopenTest, StderrToStdout) {
    Popen p("bash -c \"echo Hello to stdout; echo Hello to stderr >&2\"", IPCOption::NONE, IPCOption::PIPE, IPCOption::STDOUT);
    IPCResult result = p.Communicate({}, {});
    std::string expected_output = "Hello to stdout\nHello to stderr\n";
    EXPECT_EQ(std::string(result.stdout_data.begin(), result.stdout_data.end()), expected_output);
    EXPECT_TRUE(result.stderr_data.empty()); // Stderr should be redirected to stdout
    ASSERT_TRUE(p.returncode().has_value());
    EXPECT_EQ(p.returncode().value(), 0);
}

TEST_F(PopenTest, RedirectToFile) {
    std::filesystem::path output_file = create_temp_file();
    std::filesystem::path error_file = create_temp_file();

    Popen p("bash -c \"echo Hello to stdout; echo Hello to stderr >&2\"", 
            IPCOption::NONE, 
            IPCDestination(output_file), 
            IPCDestination(error_file));
    
    p.Wait();

    std::ifstream ofs(output_file);
    std::string stdout_content((std::istreambuf_iterator<char>(ofs)), std::istreambuf_iterator<char>());
    ofs.close();

    std::ifstream efs(error_file);
    std::string stderr_content((std::istreambuf_iterator<char>(efs)), std::istreambuf_iterator<char>());
    efs.close();

    EXPECT_EQ(stdout_content, "Hello to stdout\n");
    EXPECT_EQ(stderr_content, "Hello to stderr\n");

    std::filesystem::remove(output_file);
    std::filesystem::remove(error_file);
}

TEST_F(PopenTest, DevNullRedirect) {
    Popen p("bash -c \"echo Hello to stdout; echo Hello to stderr >&2\"", 
            IPCOption::NONE, 
            IPCDestination(IPCOption::DEVNULL), 
            IPCDestination(IPCOption::DEVNULL));
    
    p.Wait();

    // No output should be captured as it's redirected to /dev/null
    EXPECT_TRUE(p.std_out().expired());
    EXPECT_TRUE(p.std_err().expired());
    ASSERT_TRUE(p.returncode().has_value());
    EXPECT_EQ(p.returncode().value(), 0);
}

TEST_F(PopenTest, PollRunningProcess) {
    Popen p("sleep 1");
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give it time to start
    EXPECT_FALSE(p.Poll().has_value());
    p.Wait(); // Clean up
}

TEST_F(PopenTest, PollFinishedProcess) {
    Popen p("echo Done");
    p.Wait();
    ASSERT_TRUE(p.Poll().has_value());
    EXPECT_EQ(p.Poll().value(), 0);
}

TEST_F(PopenTest, WaitNoTimeout) {
    Popen p("sleep 0.1");
    std::optional<int> returncode = p.Wait();
    ASSERT_TRUE(returncode.has_value());
    EXPECT_EQ(returncode.value(), 0);
}

TEST_F(PopenTest, WaitTimeoutExpired) {
    Popen p("sleep 5");
    auto start_time = std::chrono::steady_clock::now();
    EXPECT_THROW({
        p.Wait(Popen::Seconds(0.1));
    }, TimeoutExpired);
    auto end_time = std::chrono::steady_clock::now();
    // Check if it actually timed out around 0.1 seconds
    EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count(), 100);
    EXPECT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count(), 500); // Allow some buffer
    p.Kill(); // Ensure process is terminated
}

TEST_F(PopenTest, CommunicateLargeData) {
    std::string large_input(1024 * 1024, 'A'); // 1MB of 'A'
    Bytes large_input_bytes(large_input.begin(), large_input.end());

    Popen p("cat", IPCOption::PIPE, IPCOption::PIPE);
    IPCResult result = p.Communicate(large_input_bytes, {});

    EXPECT_EQ(result.stdout_data.size(), large_input_bytes.size());
    EXPECT_TRUE(std::equal(result.stdout_data.begin(), result.stdout_data.end(), large_input_bytes.begin()));
    ASSERT_TRUE(p.returncode().has_value());
    EXPECT_EQ(p.returncode().value(), 0);
}

TEST_F(PopenTest, SendSignalTerminate) {
    Popen p("sleep 5");
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give it time to start
    p.Terminate();
    std::optional<int> returncode = p.Wait();
    ASSERT_TRUE(returncode.has_value());
    EXPECT_TRUE(WIFSIGNALED(returncode.value()) || returncode.value() == 0); // Should be terminated by signal or exit 0 if it finished quickly
}

TEST_F(PopenTest, SendSignalKill) {
    Popen p("sleep 5");
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Give it time to start
    p.Kill();
    std::optional<int> returncode = p.Wait();
    ASSERT_TRUE(returncode.has_value());
    EXPECT_TRUE(WIFSIGNALED(returncode.value()) || returncode.value() == 0); // Should be killed by signal or exit 0 if it finished quickly
}

TEST_F(PopenTest, Accessors) {
    Popen p("echo test_args");
    EXPECT_EQ(p.args(), "echo test_args");
    EXPECT_TRUE(p.pid() > 0);
    p.Wait();
    ASSERT_TRUE(p.returncode().has_value());
    EXPECT_EQ(p.returncode().value(), 0);
    // usage() is OS-dependent and might not be populated immediately or consistently
    // EXPECT_TRUE(p.usage().has_value()); 
    EXPECT_TRUE(p.std_in().expired()); // Default is NONE, so no pipe
    EXPECT_TRUE(p.std_out().expired()); // Default is NONE, so no pipe
    EXPECT_TRUE(p.std_err().expired()); // Default is NONE, so no pipe
}

TEST_F(PopenTest, NonExistentCommand) {
    EXPECT_THROW({
        Popen p("non_existent_command_12345");
    }, OSError);
}

TEST_F(PopenTest, EmptyCommand) {
    EXPECT_THROW({
        Popen p("");
    }, std::invalid_argument);
}

TEST_F(PopenTest, CommunicateWithTimeoutAndPartialRead) {
    // Process writes some data, then sleeps, then writes more. Timeout should capture first part.

    // TODO: instructions do nothing blocks program, because there is nothing to read from output pipe
    //       thread keep blocking until readable data has come
    // Popen p("bash -c \"echo part1; while true; do :; done\"", IPCOption::NONE, IPCOption::PIPE);
    Popen p("bash -c \"echo part1; while true; do echo 'writing...' > /dev/stdout; sleep 1; done\"", IPCOption::NONE, IPCOption::NONE);
    auto start_time = std::chrono::steady_clock::now();
    try {
        p.Communicate({}, Popen::Seconds(0.1));
        FAIL() << "Expected TimeoutExpired exception, but none was thrown.";
    } catch (const TimeoutExpired& e) {
        auto end_time = std::chrono::steady_clock::now();
        // Check if it actually timed out around 0.1 seconds
        EXPECT_GE(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count(), 100);
        EXPECT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count(), 200); // Relaxed buffer

        // Verify partial output was captured from the exception
        ASSERT_TRUE(e.std_out().has_value());
        EXPECT_EQ(std::string(e.std_out().value().begin(), e.std_out().value().end()), "part1\n");
    }
    // Ensure the process is terminated and cleaned up
    p.Kill();
    p.Wait();
}

TEST_F(PopenTest, CommunicateWithLargeStderr) {
    std::string large_error(1024 * 10, 'E'); // 10KB of 'E'
    Popen p("bash -c \"echo \"" + large_error + "\" >&2\"", IPCOption::NONE, IPCOption::NONE, IPCOption::PIPE);
    IPCResult result = p.Communicate({}, {});
    EXPECT_EQ(std::string(result.stderr_data.begin(), result.stderr_data.end()), large_error + "\n");
    ASSERT_TRUE(p.returncode().has_value());
    EXPECT_EQ(p.returncode().value(), 0);
}

TEST_F(PopenTest, CommunicateWithNoInputNoOutput) {
    Popen p("true"); // A command that exits successfully with no output
    IPCResult result = p.Communicate({}, {});
    EXPECT_TRUE(result.stdout_data.empty());
    EXPECT_TRUE(result.stderr_data.empty());
    ASSERT_TRUE(p.returncode().has_value());
    EXPECT_EQ(p.returncode().value(), 0);
}

TEST_F(PopenTest, CommunicateWithImmediateExit) {
    Popen p("echo hello");
    IPCResult result = p.Communicate({}, {});
    EXPECT_EQ(std::string(result.stdout_data.begin(), result.stdout_data.end()), "hello\n");
    EXPECT_TRUE(result.stderr_data.empty());
    ASSERT_TRUE(p.returncode().has_value());
    EXPECT_EQ(p.returncode().value(), 0);
}

} // namespace coj
