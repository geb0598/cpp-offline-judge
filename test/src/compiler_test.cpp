#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "coj/compiler.h"

namespace coj {

namespace {

namespace fs = std::filesystem;

class CompilerTest : public ::testing::Test {
protected:
    fs::path sandbox_dir_;

    void SetUp() override {
        sandbox_dir_ = fs::temp_directory_path() / "coj_compiler_test_sandbox";
        fs::create_directories(sandbox_dir_);
    }

    void TearDown() override {
        fs::remove_all(sandbox_dir_);
    }

    fs::path CreateSourceFile(const std::string& filename, const std::string& code) {
        fs::path file_path = sandbox_dir_ / filename;
        std::ofstream ofs(file_path);
        ofs << code;
        return file_path;
    }
};

TEST_F(CompilerTest, Compile_ValidCppCode_ReturnsSuccessAndExecutable) {
    std::string valid_code = R"(
        #include <iostream>
        int main() {
            std::cout << "Hello, COJ!" << std::endl;
            return 0;
        }
    )";
    fs::path source_path = CreateSourceFile("valid.cpp", valid_code);

    CppCompiler compiler;
    compiler.Arg("-O2").Arg("-Wall").Arg("-std=c++23");

    auto result_expected = compiler.Compile(source_path, sandbox_dir_);

    ASSERT_TRUE(result_expected.has_value()) << "System error during spawn/wait: " << result_expected.error().message();
    
    auto& result = result_expected.value();
    
    EXPECT_TRUE(result.is_successful) << "Compile failed with output:\n" << result.output;
    
    ASSERT_TRUE(result.exec_path.has_value());
    EXPECT_TRUE(fs::exists(result.exec_path.value()));
}

TEST_F(CompilerTest, Compile_InvalidCppCode_ReturnsFalseAndErrorMessage) {
    std::string invalid_code = R"(
        int main() {
            int a = 10 // Missing semicolon!
            return 0;
        }
    )";
    fs::path source_path = CreateSourceFile("invalid.cpp", invalid_code);

    CppCompiler compiler;

    auto result_expected = compiler.Compile(source_path, sandbox_dir_);

    ASSERT_TRUE(result_expected.has_value()); 
    
    auto& result = result_expected.value();
    
    EXPECT_FALSE(result.is_successful);
    
    EXPECT_FALSE(result.exec_path.has_value());
    
    EXPECT_FALSE(result.output.empty());
    EXPECT_TRUE(result.output.find("error:") != std::string::npos || result.output.find("expected") != std::string::npos) 
        << "Output does not look like a compiler error:\n" << result.output;

    std::cerr 
        << "\n=== Compiler Raw Output ===\n"
        << result.output
        << "\n===========================\n";
}

} // namespace

} // namespace coj