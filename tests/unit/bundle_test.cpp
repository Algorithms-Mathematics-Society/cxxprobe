#include "cxxprobe/bundle.hpp"

#include <gtest/gtest.h>
#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace fs = std::filesystem;
using cxxprobe::bundle::ValidationLimits;

namespace {

class BundleTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() /
               ("cxxprobe-bundle-test-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        fs::remove_all(dir_);
        fs::create_directories(dir_ / "a-problem");
        write("contest.yaml", "version: 1\nname: \"Contest\"\n");
        write("a-problem/problem.yaml",
              "version: 1\nname: \"A Problem\"\nstatement: problem.md\n"
              "solution:\n  file: solution.cpp\nsymbolic:\n  must_include: [\"main\"]\n");
        write("a-problem/problem.md", "# A Problem\n");
        write("a-problem/solution.cpp", "int main() {}\n");
    }

    void TearDown() override { fs::remove_all(dir_); }

    void write(const fs::path& relative, std::string_view content) {
        const fs::path path = dir_ / relative;
        fs::create_directories(path.parent_path());
        std::ofstream output{path, std::ios::binary};
        output << content;
    }

    fs::path dir_;
};

}  // namespace

TEST_F(BundleTest, ProducesGoldenCanonicalManifest) {
    const auto json = cxxprobe::bundle::to_json(cxxprobe::bundle::validate(dir_));
    ASSERT_EQ(json["files"].size(), 4U);
    EXPECT_EQ(json["contract"], "cxxprobe.problem-bundle");
    EXPECT_EQ(json["schema_version"], 1);
    EXPECT_EQ(json["bundle_sha256"],
              "5c2115f6bdbcc10b460946c3009f32df0f0fd35ed840eaa4560ec8fb62cd3ce9");
    EXPECT_EQ(json["files"][0]["path"], "a-problem/problem.md");
    EXPECT_EQ(json["files"][0]["sha256"],
              "d3ec5d7506b2761adc61abb6a0f83e1e6b1dfdf5cfd509ae780541bbb71ee946");
    EXPECT_EQ(json["files"][1]["path"], "a-problem/problem.yaml");
    EXPECT_EQ(json["files"][1]["sha256"],
              "b5612d46e424d7139180d0723d0a9a271ae751740f3547ad4b94317815c50766");
    EXPECT_EQ(json["files"][2]["path"], "a-problem/solution.cpp");
    EXPECT_EQ(json["files"][2]["sha256"],
              "bc8bb8e433bf65214540115414c821c904b2a30d60a3ac0424bf9b77a00024b7");
    EXPECT_EQ(json["files"][3]["path"], "contest.yaml");
    EXPECT_EQ(json["files"][3]["sha256"],
              "76dd86fde798b89c559d1af33611e44b7f29e1c15d2003f3d0d4c6a9d35d91f6");
    EXPECT_EQ(json["problems"][0]["slug"], "a-problem");
    EXPECT_EQ(json["problems"][0]["execution"]["compiler"]["cxx"], "g++");
    EXPECT_EQ(json["problems"][0]["execution"]["compiler"]["std_flag"], "c++23");
    EXPECT_EQ(json["problems"][0]["execution"]["compiler"]["flags"],
              nlohmann::json::array({"-O2", "-Wall"}));
    EXPECT_EQ(json["problems"][0]["execution"]["compiler"]["extra_sources"],
              nlohmann::json::array());
    EXPECT_EQ(json["problems"][0]["execution"]["limits"]["memory_bytes"], 268435456);
    EXPECT_EQ(json["problems"][0]["execution"]["limits"]["cpu_time_ms"], 5000);
    EXPECT_EQ(json["problems"][0]["execution"]["limits"]["wall_time_ms"], 10000);
    EXPECT_EQ(json["problems"][0]["execution"]["limits"]["max_pids"], 64);
}

TEST_F(BundleTest, ResolvedExecutionIsDigestCovered) {
    const auto before = cxxprobe::bundle::validate(dir_).bundle_sha256;
    write("a-problem/problem.yaml",
          "version: 1\nname: \"A Problem\"\nstatement: problem.md\n"
          "solution:\n  file: solution.cpp\ncompiler:\n  cxx: clang++\n  std: c++20\n"
          "  flags: [-O0, -Werror]\nlimits:\n  memory_mb: 128\n  cpu: 750ms\n"
          "  wall: 2s\n  pids: 8\nsymbolic:\n  must_include: [\"main\"]\n");

    const auto manifest = cxxprobe::bundle::validate(dir_);
    const auto json = cxxprobe::bundle::to_json(manifest);
    EXPECT_NE(manifest.bundle_sha256, before);
    EXPECT_EQ(json["problems"][0]["execution"]["compiler"]["cxx"], "clang++");
    EXPECT_EQ(json["problems"][0]["execution"]["compiler"]["std_flag"], "c++20");
    EXPECT_EQ(json["problems"][0]["execution"]["compiler"]["flags"],
              nlohmann::json::array({"-O0", "-Werror"}));
    EXPECT_EQ(json["problems"][0]["execution"]["limits"]["memory_bytes"], 134217728);
    EXPECT_EQ(json["problems"][0]["execution"]["limits"]["cpu_time_ms"], 750);
    EXPECT_EQ(json["problems"][0]["execution"]["limits"]["wall_time_ms"], 2000);
    EXPECT_EQ(json["problems"][0]["execution"]["limits"]["max_pids"], 8);
}

TEST_F(BundleTest, IsIndependentOfCreationOrderMtimeAndPermissions) {
    const auto before = cxxprobe::bundle::to_json(cxxprobe::bundle::validate(dir_)).dump();
    const fs::path problem = dir_ / "a-problem/problem.yaml";
    fs::last_write_time(problem, fs::file_time_type::clock::now());
    fs::permissions(problem, fs::perms::owner_read | fs::perms::group_read,
                    fs::perm_options::replace);
    const auto after = cxxprobe::bundle::to_json(cxxprobe::bundle::validate(dir_)).dump();
    EXPECT_EQ(before, after);
}

TEST_F(BundleTest, ContentChangeChangesBundleDigest) {
    const auto before = cxxprobe::bundle::validate(dir_).bundle_sha256;
    write("a-problem/note.md", "content\n");
    const auto after = cxxprobe::bundle::validate(dir_).bundle_sha256;
    EXPECT_NE(before, after);
}

TEST_F(BundleTest, FileHashMatchesIndependentSha256KnownVector) {
    write("a-problem/vector.txt", "abc");
    const auto manifest = cxxprobe::bundle::validate(dir_);
    const auto vector = std::ranges::find(manifest.files, "a-problem/vector.txt",
                                          &cxxprobe::bundle::FileRecord::path);
    ASSERT_NE(vector, manifest.files.end());
    EXPECT_EQ(vector->sha256, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_F(BundleTest, RejectsSymlink) {
    fs::create_symlink(dir_ / "contest.yaml", dir_ / "a-problem/link.yaml");
    EXPECT_THROW(cxxprobe::bundle::validate(dir_), std::runtime_error);
}

TEST_F(BundleTest, RejectsSpecialFile) {
    const fs::path fifo = dir_ / "a-problem/input.pipe";
    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
    EXPECT_THROW(cxxprobe::bundle::validate(dir_), std::runtime_error);
}

TEST_F(BundleTest, RejectsMultiplyLinkedRegularFileEvenWhenOtherLinkIsOutsideBundle) {
    fs::path outside_link = dir_;
    outside_link += "-outside-link";
    fs::remove(outside_link);
    fs::create_hard_link(dir_ / "a-problem/solution.cpp", outside_link);
    try {
        (void)cxxprobe::bundle::validate(dir_);
        FAIL() << "expected multiply-linked file rejection";
    } catch (const std::runtime_error& error) {
        EXPECT_NE(std::string{error.what()}.find("multiply-linked"), std::string::npos);
    }
    fs::remove(outside_link);
}

TEST_F(BundleTest, RejectsInvalidUtf8Path) {
    std::string invalid_path{"a-problem/bad-"};
    invalid_path.push_back(static_cast<char>(0xff));
    write(fs::path{invalid_path}, "bad\n");
    EXPECT_THROW(cxxprobe::bundle::validate(dir_), std::runtime_error);
}

TEST_F(BundleTest, RejectsCaseCollidingPaths) {
    write("a-problem/Data.in", "1\n");
    write("a-problem/data.in", "2\n");
    EXPECT_THROW(cxxprobe::bundle::validate(dir_), std::runtime_error);
}

TEST_F(BundleTest, RejectsTraversalInProblemReference) {
    write("a-problem/problem.yaml", "version: 1\nname: \"A Problem\"\nstatement: ../outside.md\n");
    EXPECT_THROW(cxxprobe::bundle::validate(dir_), std::runtime_error);
}

TEST_F(BundleTest, RejectsMalformedProblemThroughExistingLoader) {
    write("a-problem/problem.yaml", "version: 99\nname: \"A Problem\"\n");
    EXPECT_THROW(cxxprobe::bundle::validate(dir_), std::runtime_error);
}

TEST_F(BundleTest, RejectsNestedProblemYamlInsteadOfSilentlyHashingIt) {
    write("a-problem/nested/problem.yaml", "version: 1\nname: \"Hidden\"\n");
    EXPECT_THROW(cxxprobe::bundle::validate(dir_), std::runtime_error);
}

TEST_F(BundleTest, RejectsMultipleProblemsBecauseDigestIsQuestionScoped) {
    write("b-problem/problem.yaml", "version: 1\nname: \"B Problem\"\n");
    EXPECT_THROW(cxxprobe::bundle::validate(dir_), std::runtime_error);
}

TEST_F(BundleTest, RejectsMissingReferencedFile) {
    fs::remove(dir_ / "a-problem/solution.cpp");
    EXPECT_THROW(cxxprobe::bundle::validate(dir_), std::runtime_error);
}

TEST_F(BundleTest, RejectsQuestionWithNoEnabledCheck) {
    write("a-problem/problem.yaml",
          "version: 1\nname: \"A Problem\"\nstatement: problem.md\n"
          "solution:\n  file: solution.cpp\n");
    EXPECT_THROW(cxxprobe::bundle::validate(dir_), std::runtime_error);
}

TEST_F(BundleTest, EnforcesConfiguredFileAndTotalBounds) {
    ValidationLimits limits;
    limits.max_files = 1;
    EXPECT_THROW(cxxprobe::bundle::validate(dir_, limits), std::runtime_error);

    limits.max_files = 10;
    limits.max_file_bytes = 8;
    EXPECT_THROW(cxxprobe::bundle::validate(dir_, limits), std::runtime_error);

    limits.max_file_bytes = 1024;
    limits.max_total_bytes = 16;
    EXPECT_THROW(cxxprobe::bundle::validate(dir_, limits), std::runtime_error);
}
