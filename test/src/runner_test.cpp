#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>

#include "coj/compiler.h"
#include "coj/runner.h"

namespace coj {

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

class RunnerTest : public ::testing::Test {
protected:
    fs::path sandbox_dir_;
    CppCompiler compiler_;

    void SetUp() override {
        sandbox_dir_ = fs::temp_directory_path() / ("coj_runner_test_" + std::to_string(std::time(nullptr)));
        fs::create_directories(sandbox_dir_);
        compiler_.Arg("-O2").Arg("-Wall").Arg("-std=c++23");
    }

    void TearDown() override {
        fs::remove_all(sandbox_dir_);
    }

    fs::path CreateAndCompile(const std::string& name, const std::string& code) {
        fs::path source_path = sandbox_dir_ / (name + ".cpp");
        std::ofstream(source_path) << code;

        fs::path output_dir = sandbox_dir_ / name;
        fs::create_directories(output_dir);

        auto result = compiler_.Compile(source_path, output_dir);
        EXPECT_TRUE(result.has_value() && result->is_successful) << "Test setup failed: Compilation error\n" << result->output;
        
        return result->exec_path.value();
    }

    fs::path CreateInputFile(const std::string& name, const std::string& content) {
        fs::path input_path = sandbox_dir_ / (name + ".in");
        std::ofstream(input_path) << content;
        return input_path;
    }

    RunConfig GetBaseConfig(const fs::path& exec, const fs::path& input) {
        return RunConfig{
            .exec_path = exec,
            .input_path = input,
            .output_path = sandbox_dir_ / "output.out",
            .work_dir = sandbox_dir_,
            .soft_limits = {
                .cpu_time = 1000ms,         
                .memory_kb = 64 * 1024      
            },
            .hard_limits = {
                .cpu_time_sec = 2,
                .memory_bytes = 128 * 1024 * 1024,
                .file_size_bytes = 1 * 1024 * 1024 
            }
        };
    }
};

TEST_F(RunnerTest, Run_NormalExecution_ReturnsSuccess) {
    std::string code = R"(
        #include <iostream>
        int main() {
            int a, b;
            if (std::cin >> a >> b) {
                std::cout << (a + b) << "\n";
            }
            return 0;
        }
    )";
    auto exec = CreateAndCompile("normal", code);
    auto input = CreateInputFile("normal", "10 20");
    auto config = GetBaseConfig(exec, input);

    auto result_opt = coj::Run(config);
    ASSERT_TRUE(result_opt.has_value());

    auto& result = result_opt.value();
    EXPECT_EQ(result.status, RunStatus::Success);
    
    std::ifstream ifs(config.output_path);
    std::string output_str;
    std::getline(ifs, output_str);
    EXPECT_EQ(output_str, "30");
}

TEST_F(RunnerTest, Run_RuntimeError_ReturnsRTE) {
    std::string code = R"(
        #include <stdlib.h>
        int main() {
            exit(1); 
        }
    )";
    auto exec = CreateAndCompile("rte", code);
    auto input = CreateInputFile("rte", "");
    auto config = GetBaseConfig(exec, input);

    auto result = coj::Run(config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, RunStatus::RuntimeError);
    EXPECT_EQ(result->exit_status.Code().value_or(-1), 1);
}

TEST_F(RunnerTest, Run_InfiniteLoop_ReturnsTLE) {
    std::string code = R"(
        int main() {
            volatile int i = 0;
            while(true) { i++; } 
            return 0;
        }
    )";
    auto exec = CreateAndCompile("tle_cpu", code);
    auto input = CreateInputFile("tle_cpu", "");
    
    auto config = GetBaseConfig(exec, input);
    config.soft_limits.cpu_time = 200ms;
    config.hard_limits.cpu_time_sec = 1;

    auto result = coj::Run(config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, RunStatus::TimeLimit);
}

TEST_F(RunnerTest, Run_Sleep_ReturnsTLE) {
    std::string code = R"(
        #include <unistd.h>
        int main() {
            sleep(3); 
            return 0;
        }
    )";
    auto exec = CreateAndCompile("tle_wall", code);
    auto input = CreateInputFile("tle_wall", "");
    
    auto config = GetBaseConfig(exec, input);
    config.soft_limits.cpu_time = 1000ms;
    config.hard_limits.cpu_time_sec = 1;

    auto result = coj::Run(config);
    ASSERT_TRUE(result.has_value());
    
    EXPECT_EQ(result->status, RunStatus::TimeLimit);
}

TEST_F(RunnerTest, Run_LargeAllocation_ReturnsMLE) {
    std::string code = R"(
        #include <vector>
        int main() {
            std::vector<char> v(100 * 1024 * 1024, 1); 
            return 0;
        }
    )";
    auto exec = CreateAndCompile("mle", code);
    auto input = CreateInputFile("mle", "");
    
    auto config = GetBaseConfig(exec, input);
    config.soft_limits.memory_kb = 32 * 1024;
    config.hard_limits.memory_bytes = 256 * 1024 * 1024;

    auto result = coj::Run(config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->status, RunStatus::MemoryLimit);
}

} // namespace

} // namespace coj