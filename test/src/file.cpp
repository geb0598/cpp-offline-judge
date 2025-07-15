#include "gtest/gtest.h"
#include "file.h"
#include "utils.h" // For RandomGenerator
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <functional>
#include <cstring> // For strcpy
#include <unistd.h> // For close (on Unix-like systems)

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

// Test fixture for File classes
class FileTest : public ::testing::Test {
protected:
    std::filesystem::path temp_file_path;

    void SetUp() override {
        // Create a temporary file for tests
        temp_file_path = create_temp_file();
        GTEST_LOG_(INFO) << "Created temporary file: " << temp_file_path;
    }

    void TearDown() override {
        // Clean up the temporary file
        if (std::filesystem::exists(temp_file_path)) {
            std::filesystem::remove(temp_file_path);
            GTEST_LOG_(INFO) << "Removed temporary file: " << temp_file_path;
        }
    }
};

TEST_F(FileTest, FileImplConstructorPath) {
    GTEST_LOG_(INFO) << "Running FileImplConstructorPath test.";
    // Test constructor with a valid path for IFileImpl
    ASSERT_NO_THROW({
        IFileImpl file(temp_file_path);
        EXPECT_TRUE(file.is_opened());
    });

    // Test constructor with a valid path for OFileImpl
    ASSERT_NO_THROW({
        OFileImpl file(temp_file_path);
        EXPECT_TRUE(file.is_opened());
    });

    // Test constructor with a non-existent path (should throw invalid_argument)
    std::filesystem::path non_existent_path = std::filesystem::temp_directory_path() / "non_existent_file.txt";
    EXPECT_THROW({
        IFileImpl file(non_existent_path);
    }, std::invalid_argument);
    GTEST_LOG_(INFO) << "FileImplConstructorPath test finished.";
}

TEST_F(FileTest, IFileImplRead) {
    GTEST_LOG_(INFO) << "Running IFileImplRead test.";
    for (int i = 0; i < 100; ++i) { // Run 100 fuzzing iterations
        SCOPED_TRACE(testing::Message() << "Iteration " << i);
        size_t content_length = utils::RandomGenerator::get_instance().get_int(1, 1024); // Random length between 1 and 1024
        std::string test_content = utils::RandomGenerator::get_instance().get_string(content_length);
        
        GTEST_LOG_(INFO) << "  Iteration " << i << ": Generated content length: " << content_length;
        // GTEST_LOG_(INFO) << "  Iteration " << i << ": Generated content: '" << test_content << "'"; // Too verbose for long strings

        std::filesystem::path read_file_path = create_temp_file(test_content);
        GTEST_LOG_(INFO) << "  Iteration " << i << ": Created temp file for read: " << read_file_path;

        IFileImpl ifile(read_file_path);
        EXPECT_TRUE(ifile.is_readable());
        EXPECT_FALSE(ifile.is_writable());

        Bytes read_bytes = ifile.Read(test_content.length());
        std::string read_string(read_bytes.begin(), read_bytes.end());
        
        EXPECT_EQ(read_string, test_content)
            << "expected: '" << test_content << "'\nactual: '" << read_string << "'";

        // Test reading past EOF
        read_bytes = ifile.Read(10);
        EXPECT_TRUE(ifile.is_eof());
        EXPECT_TRUE(read_bytes.empty());

        std::filesystem::remove(read_file_path);
        GTEST_LOG_(INFO) << "  Iteration " << i << ": Removed temp file for read: " << read_file_path;
    }
    GTEST_LOG_(INFO) << "IFileImplRead test finished.";
}

TEST_F(FileTest, IFileImplWriteThrows) {
    GTEST_LOG_(INFO) << "Running IFileImplWriteThrows test.";
    IFileImpl ifile(temp_file_path);
    Bytes data = {'a', 'b', 'c'};
    EXPECT_THROW({
        ifile.Write(data, 0, data.size());
    }, std::runtime_error);
    GTEST_LOG_(INFO) << "IFileImplWriteThrows test finished.";
}

TEST_F(FileTest, OFileImplWrite) {
    GTEST_LOG_(INFO) << "Running OFileImplWrite test.";
    for (int i = 0; i < 100; ++i) { // Run 100 fuzzing iterations
        SCOPED_TRACE(testing::Message() << "Iteration " << i);
        size_t content_length = utils::RandomGenerator::get_instance().get_int(1, 1024); // Random length between 1 and 1024
        Bytes write_content_bytes = utils::RandomGenerator::get_instance().get_bytes(content_length);
        std::string write_content_str(write_content_bytes.begin(), write_content_bytes.end());

        GTEST_LOG_(INFO) << "  Iteration " << i << ": Generated content length: " << content_length;

        std::filesystem::path write_file_path = create_temp_file(); // Create an empty file
        GTEST_LOG_(INFO) << "  Iteration " << i << ": Created temp file for write: " << write_file_path;
        
        OFileImpl ofile(write_file_path);
        EXPECT_FALSE(ofile.is_readable());
        EXPECT_TRUE(ofile.is_writable());

        ofile.Write(write_content_bytes, 0, write_content_bytes.size());

        // Verify content by reading it back with an IFileImpl
        IFileImpl ifile(write_file_path);
        Bytes read_bytes = ifile.Read(write_content_bytes.size());
        std::string read_string(read_bytes.begin(), read_bytes.end());
        
        EXPECT_EQ(read_string, write_content_str)
            << "expected: '" << write_content_str << "'\nactual: '" << read_string << "'";

        std::filesystem::remove(write_file_path);
        GTEST_LOG_(INFO) << "  Iteration " << i << ": Removed temp file for write: " << write_file_path;
    }
    GTEST_LOG_(INFO) << "OFileImplWrite test finished.";
}

TEST_F(FileTest, OFileImplReadThrows) {
    GTEST_LOG_(INFO) << "Running OFileImplReadThrows test.";
    OFileImpl ofile(temp_file_path);
    EXPECT_THROW({
        ofile.Read(10);
    }, std::runtime_error);
    GTEST_LOG_(INFO) << "OFileImplReadThrows test finished.";
}

TEST_F(FileTest, FilenoAndFilepointer) {
    GTEST_LOG_(INFO) << "Running FilenoAndFilepointer test.";
    IFileImpl file(temp_file_path);
    EXPECT_GE(file.fileno(), 0); // File descriptor should be non-negative
    EXPECT_NE(file.filepointer(), nullptr); // FILE* should not be null
    GTEST_LOG_(INFO) << "FilenoAndFilepointer test finished.";
}



} // namespace coj
