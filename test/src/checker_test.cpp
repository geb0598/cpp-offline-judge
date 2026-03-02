#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "coj/checker.h"

namespace coj {

namespace {

namespace fs = std::filesystem;

class CheckerTest : public ::testing::Test {
protected:
    fs::path sandbox_dir_;

    void SetUp() override {
        sandbox_dir_ = fs::temp_directory_path() / ("coj_checker_test_" + std::to_string(std::time(nullptr)));
        fs::create_directories(sandbox_dir_);
    }

    void TearDown() override {
        fs::remove_all(sandbox_dir_);
    }

    fs::path CreateFile(const std::string& filename, const std::string& content) {
        fs::path file_path = sandbox_dir_ / filename;
        std::ofstream ofs(file_path);
        ofs << content;
        return file_path;
    }
};

TEST_F(CheckerTest, Check_WhitespaceDifferences_ReturnsAccepted) {
    auto answer = CreateFile("1.out", "1 2 3\n4 5\n");
    auto user = CreateFile("1.user_out", "1 \t 2 \n\n 3 4  5 \n\n");

    CheckConfig config{.output_path = user, .answer_path = answer, .epsilon = 0.0};
    
    auto result = Check(config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), CheckResult::Accepted);
}

TEST_F(CheckerTest, Check_DifferentTokens_ReturnsWrongAnswer) {
    auto answer = CreateFile("2.out", "apple banana");
    auto user = CreateFile("2.user_out", "apple orange"); 

    CheckConfig config{.output_path = user, .answer_path = answer};
    EXPECT_EQ(Check(config).value(), CheckResult::WrongAnswer);
}

TEST_F(CheckerTest, Check_UserOutputTooShort_ReturnsWrongAnswer) {
    auto answer = CreateFile("3.out", "1 2 3");
    auto user = CreateFile("3.user_out", "1 2"); 

    CheckConfig config{.output_path = user, .answer_path = answer};
    EXPECT_EQ(Check(config).value(), CheckResult::WrongAnswer);
}

TEST_F(CheckerTest, Check_UserOutputTooLong_ReturnsWrongAnswer) {
    auto answer = CreateFile("4.out", "1 2");
    auto user = CreateFile("4.user_out", "1 2 3"); 

    CheckConfig config{.output_path = user, .answer_path = answer};
    EXPECT_EQ(Check(config).value(), CheckResult::WrongAnswer);
}

TEST_F(CheckerTest, Check_FloatWithinEpsilon_ReturnsAccepted) {
    auto answer = CreateFile("5.out", "3.14159265");
    auto user = CreateFile("5.user_out", "3.14159268"); 

    CheckConfig config{.output_path = user, .answer_path = answer, .epsilon = 1e-7};
    
    EXPECT_EQ(Check(config).value(), CheckResult::Accepted);
}

TEST_F(CheckerTest, Check_FloatOutsideEpsilon_ReturnsWrongAnswer) {
    auto answer = CreateFile("6.out", "3.14159265");
    auto user = CreateFile("6.user_out", "3.14159300"); 

    CheckConfig config{.output_path = user, .answer_path = answer, .epsilon = 1e-7};
    
    EXPECT_EQ(Check(config).value(), CheckResult::WrongAnswer);
}

TEST_F(CheckerTest, Check_HybridTextAndFloat_ReturnsAccepted) {
    auto answer = CreateFile("7.out", "The answer is 1.000 !");
    auto user = CreateFile("7.user_out", "The answer \n is 1.0000001 ! \n"); 

    CheckConfig config{.output_path = user, .answer_path = answer, .epsilon = 1e-6};
    
    EXPECT_EQ(Check(config).value(), CheckResult::Accepted);
}

TEST_F(CheckerTest, Check_MissingUserFile_ReturnsWrongAnswer) {
    auto answer = CreateFile("8.out", "1 2 3");
    fs::path missing_user = sandbox_dir_ / "missing.user_out";

    CheckConfig config{.output_path = missing_user, .answer_path = answer};
    
    auto result = Check(config);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), CheckResult::WrongAnswer);
}

TEST_F(CheckerTest, Check_MissingAnswerFile_ReturnsSystemError) {
    fs::path missing_answer = sandbox_dir_ / "missing.out";
    auto user = CreateFile("9.user_out", "1 2 3");

    CheckConfig config{.output_path = user, .answer_path = missing_answer};
    
    auto result = Check(config);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::errc::no_such_file_or_directory);
}

} // namespace

} // namespace coj