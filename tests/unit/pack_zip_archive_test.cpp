#include <gtest/gtest.h>
#include <sys/stat.h>

#include <filesystem>
#include <fstream>

#include "detail/zip_archive.hpp"

using cxxprobe::pack::detail::ZipReader;
using cxxprobe::pack::detail::ZipWriter;

namespace {

class ZipArchiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("cxxprobe-zip-test-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::filesystem::path dir_;
};

unsigned permission_bits(const std::filesystem::path& path) {
    struct stat st {};
    if (::stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return st.st_mode & 07777U;
}

TEST_F(ZipArchiveTest, RoundTripsExecutableBit) {
    std::filesystem::path src = dir_ / "checker";
    std::ofstream(src) << "#!/bin/sh\nexit 0\n";
    std::filesystem::permissions(
        src, std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                 std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                 std::filesystem::perms::others_exec);
    unsigned original_mode = permission_bits(src);
    ASSERT_TRUE(original_mode & 0100) << "test fixture itself must be +x";

    std::filesystem::path zip_path = dir_ / "out.zip";
    {
        ZipWriter writer(zip_path);
        writer.add_file("checker", src);
        writer.close();
    }

    std::filesystem::path dest = dir_ / "extracted" / "checker";
    {
        ZipReader reader(zip_path);
        auto entries = reader.list_entries();
        ASSERT_EQ(entries.size(), 1U);
        reader.extract_to(entries.front(), dest);
    }

    EXPECT_EQ(permission_bits(dest), original_mode);
}

TEST_F(ZipArchiveTest, AddDataRoundTrips) {
    std::filesystem::path zip_path = dir_ / "out.zip";
    {
        ZipWriter writer(zip_path);
        writer.add_data("manifest.json", R"({"hello":"world"})");
        writer.close();
    }

    ZipReader reader(zip_path);
    auto entries = reader.list_entries();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries.front().name, "manifest.json");
    EXPECT_EQ(reader.read_data(entries.front()), R"({"hello":"world"})");
}

TEST_F(ZipArchiveTest, ExtractRestoresDirectoryStructure) {
    std::filesystem::path zip_path = dir_ / "out.zip";
    std::filesystem::path src_file = dir_ / "1.in";
    std::ofstream(src_file) << "1 2\n";
    {
        ZipWriter writer(zip_path);
        writer.add_file("a-slug/tests/1.in", src_file);
        writer.close();
    }

    ZipReader reader(zip_path);
    auto entries = reader.list_entries();
    ASSERT_EQ(entries.size(), 1U);
    std::filesystem::path dest_root = dir_ / "extracted";
    reader.extract_to(entries.front(), dest_root / entries.front().name);

    EXPECT_TRUE(std::filesystem::exists(dest_root / "a-slug" / "tests" / "1.in"));
}

}  // namespace
