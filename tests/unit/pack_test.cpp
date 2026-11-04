#include "cxxprobe/pack.hpp"

#include <gtest/gtest.h>
#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "detail/zip_archive.hpp"

using cxxprobe::pack::pack_contest;
using cxxprobe::pack::PackOptions;
using cxxprobe::pack::unpack_contest;
using cxxprobe::pack::detail::ZipWriter;

namespace {

class PackTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("cxxprobe-pack-test-" +
                std::to_string(::testing::UnitTest::GetInstance()->random_seed()) + "-" +
                std::string{::testing::UnitTest::GetInstance()->current_test_info()->name()});
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_ / "contest" / "a-slug" / "tests");
        std::ofstream(dir_ / "contest" / "contest.yaml")
            << "version: 1\nname: \"Test\"\ndescription: \"\"\n";
        std::ofstream(dir_ / "contest" / "a-slug" / "problem.yaml")
            << "version: 2\nname: \"A: Test\"\n";
        std::ofstream(dir_ / "contest" / "a-slug" / "problem.md") << "# statement\n";
        std::ofstream(dir_ / "contest" / "a-slug" / "tests" / "1.in") << "1 2\n";
        std::ofstream(dir_ / "contest" / "a-slug" / "tests" / "1.ans") << "3\n";
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::filesystem::path dir_;
};

TEST_F(PackTest, RoundTripPreservesProblemFiles) {
    auto pack_result = pack_contest(dir_ / "contest", dir_ / "pack.zip");
    ASSERT_EQ(pack_result.problems.size(), 1U);
    EXPECT_EQ(pack_result.problems.front().slug, "a-slug");

    auto unpack_result = unpack_contest(dir_ / "pack.zip", dir_ / "unpacked");
    ASSERT_EQ(unpack_result.problem_slugs.size(), 1U);
    EXPECT_EQ(unpack_result.problem_slugs.front(), "a-slug");
    EXPECT_TRUE(std::filesystem::exists(dir_ / "unpacked" / "contest.yaml"));
    EXPECT_TRUE(std::filesystem::exists(dir_ / "unpacked" / "a-slug" / "problem.yaml"));
    EXPECT_TRUE(std::filesystem::exists(dir_ / "unpacked" / "a-slug" / "tests" / "1.in"));
}

TEST_F(PackTest, DenylistedJunkIsExcluded) {
    std::filesystem::create_directories(dir_ / "contest" / "a-slug" / ".git");
    std::ofstream(dir_ / "contest" / "a-slug" / ".git" / "HEAD") << "junk\n";
    std::ofstream(dir_ / "contest" / "a-slug" / "a.out") << "junk\n";

    pack_contest(dir_ / "contest", dir_ / "pack.zip");
    unpack_contest(dir_ / "pack.zip", dir_ / "unpacked");

    EXPECT_FALSE(std::filesystem::exists(dir_ / "unpacked" / "a-slug" / ".git"));
    EXPECT_FALSE(std::filesystem::exists(dir_ / "unpacked" / "a-slug" / "a.out"));
}

TEST_F(PackTest, ProblemsFilterIncludesOnlyRequestedSlugs) {
    std::filesystem::create_directories(dir_ / "contest" / "b-slug");
    std::ofstream(dir_ / "contest" / "b-slug" / "problem.yaml")
        << "version: 2\nname: \"B: Test\"\n";

    PackOptions opts;
    opts.problem_slugs = {"a-slug"};
    auto result = pack_contest(dir_ / "contest", dir_ / "pack.zip", opts);

    ASSERT_EQ(result.problems.size(), 1U);
    EXPECT_EQ(result.problems.front().slug, "a-slug");
}

TEST_F(PackTest, PackThrowsOnUnknownRequestedSlug) {
    PackOptions opts;
    opts.problem_slugs = {"does-not-exist"};
    EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip", opts), std::runtime_error);
}

TEST_F(PackTest, PackThrowsWithoutContestYaml) {
    std::filesystem::remove(dir_ / "contest" / "contest.yaml");
    EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip"), std::runtime_error);
}

TEST_F(PackTest, UnpackRejectsNewerFormatVersion) {
    ZipWriter writer(dir_ / "future.zip");
    writer.add_data("manifest.json", R"({"format_version": 999, "problems": []})");
    writer.close();

    EXPECT_THROW(unpack_contest(dir_ / "future.zip", dir_ / "unpacked"), std::runtime_error);
}

TEST_F(PackTest, UnpackRefusesToOverwriteWithoutForce) {
    pack_contest(dir_ / "contest", dir_ / "pack.zip");
    unpack_contest(dir_ / "pack.zip", dir_ / "unpacked");

    EXPECT_THROW(unpack_contest(dir_ / "pack.zip", dir_ / "unpacked"), std::runtime_error);
    EXPECT_NO_THROW(unpack_contest(dir_ / "pack.zip", dir_ / "unpacked", /*force=*/true));
}

TEST_F(PackTest, UnpackRejectsZipSlipEntry) {
    ZipWriter writer(dir_ / "hostile.zip");
    writer.add_data("manifest.json", R"({"format_version": 1, "problems": []})");
    writer.add_data("../escaped.txt", "should never be written outside dest_dir");
    writer.close();

    EXPECT_THROW(unpack_contest(dir_ / "hostile.zip", dir_ / "unpacked"), std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(dir_ / "escaped.txt"));
}

TEST_F(PackTest, AttachmentManifestMatchesPackedFilesAndPreservesPlainPreview) {
    namespace fs = std::filesystem;
    for (const auto& dir : {"attachments", "public", "candidate/start"}) {
        const auto root = dir_ / "contest" / "a-slug";
        std::ofstream(root / "problem.yaml")
            << "version: 2\nname: Test\nattachments:\n  dir: " << dir << "\n";
        fs::create_directories(root / dir / "include");
        fs::create_directories(root / dir / "build");
        std::ofstream(root / dir / "z.cpp") << "starter";
        std::ofstream(root / dir / "include" / "a.hpp") << "header";
        std::ofstream(root / dir / "junk.o") << "excluded";
        std::ofstream(root / dir / "build" / "private.cpp") << "excluded";
        auto cfg = cxxprobe::problem::load_from_dir(root);
        auto plain = cxxprobe::problem::preview_to_json(cfg, {});
        EXPECT_FALSE(plain.contains("attachments"));
        pack_contest(dir_ / "contest", dir_ / "pack.zip");
        cxxprobe::pack::detail::ZipReader reader(dir_ / "pack.zip");
        auto entries = reader.list_entries();
        ASSERT_FALSE(entries.empty());
        auto manifest = nlohmann::ordered_json::parse(reader.read_data(entries.front()));
        auto packed = manifest["problems"][0];
        EXPECT_EQ(packed["attachments"]["dir"], dir);
        EXPECT_EQ(packed["attachments"]["files"],
                  (std::vector<std::string>{"include/a.hpp", "z.cpp"}));
        for (const auto& file : packed["attachments"]["files"]) {
            std::string wanted = "a-slug/" + std::string(dir) + "/" + file.get<std::string>();
            EXPECT_TRUE(
                std::ranges::any_of(entries, [&](const auto& e) { return e.name == wanted; }));
        }
        packed.erase("attachments");
        EXPECT_EQ(packed, plain);
        auto inspected = cxxprobe::problem::inspect_attachments(cfg);
        EXPECT_EQ(inspected.files, (std::vector<std::string>{"include/a.hpp", "z.cpp"}));
    }
}

TEST_F(PackTest, MissingAndEmptyDeclaredAttachmentsWarnWithoutFailingPack) {
    auto root = dir_ / "contest" / "a-slug";
    std::ofstream(root / "problem.yaml") << "version: 2\nname: Test\nattachments: {dir: public}\n";
    for (bool create : {false, true}) {
        if (create) {
            std::filesystem::create_directory(root / "public");
        }
        testing::internal::CaptureStderr();
        pack_contest(dir_ / "contest", dir_ / "pack.zip");
        auto warning = testing::internal::GetCapturedStderr();
        EXPECT_NE(warning.find("no packable candidate files"), std::string::npos);
        auto report = cxxprobe::problem::validate_package(root);
        EXPECT_TRUE(std::ranges::any_of(report.warnings, [](const auto& w) {
            return w.find("no packable candidate files") != std::string::npos;
        }));
    }
}

TEST_F(PackTest, UnsafeAttachmentConfigurationIsRejected) {
    auto root = dir_ / "contest" / "a-slug";
    for (const auto& dir :
         {"../outside", "/tmp", ".", "", "public/../../outside", "C:/secret", "public\\secret"}) {
        std::ofstream(root / "problem.yaml")
            << "version: 2\nname: Test\nattachments: {dir: '" << dir << "'}\n";
        EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip"), std::runtime_error) << dir;
        EXPECT_FALSE(cxxprobe::problem::validate_package(root).ok) << dir;
        auto cfg = cxxprobe::problem::load_from_dir(root);
        EXPECT_THROW(cxxprobe::problem::inspect_attachments(cfg), std::runtime_error) << dir;
    }
}

TEST_F(PackTest, SymlinksAndSpecialFilesNeverReachTheArchiveOrAttachmentList) {
    namespace fs = std::filesystem;
    auto root = dir_ / "contest" / "a-slug";
    fs::create_directories(root / "attachments" / "nested");
    fs::create_directories(dir_ / "outside");
    std::ofstream(dir_ / "outside" / "secret.cpp") << "private";
    fs::create_symlink(dir_ / "outside" / "secret.cpp", root / "attachments" / "leak.cpp");
    fs::create_directory_symlink(dir_ / "outside", root / "attachments" / "leak-dir");
    fs::create_directory_symlink(root / "attachments", root / "attachments" / "loop");
    fs::create_symlink("missing", root / "attachments" / "dangling");
    fs::create_symlink(dir_ / "outside" / "secret.cpp", root / "other-leak.cpp");
    std::ofstream(root / "attachments" / "nested" / "ok.cpp") << "starter";
    ASSERT_EQ(::mkfifo((root / "attachments" / "pipe").c_str(), 0600), 0);
    auto cfg = cxxprobe::problem::load_from_dir(root);
    EXPECT_EQ(cxxprobe::problem::inspect_attachments(cfg).files,
              (std::vector<std::string>{"nested/ok.cpp"}));
    pack_contest(dir_ / "contest", dir_ / "pack.zip");
    unpack_contest(dir_ / "pack.zip", dir_ / "unpacked");
    EXPECT_TRUE(fs::exists(dir_ / "unpacked" / "a-slug" / "attachments" / "nested" / "ok.cpp"));
    EXPECT_FALSE(fs::exists(dir_ / "unpacked" / "a-slug" / "other-leak.cpp"));
    EXPECT_FALSE(fs::exists(dir_ / "unpacked" / "a-slug" / "attachments" / "leak.cpp"));
    std::ofstream(root / "problem.yaml")
        << "version: 2\nname: Test\nattachments: {dir: attachments/leak-dir}\n";
    EXPECT_TRUE(cxxprobe::problem::inspect_attachments(cxxprobe::problem::load_from_dir(root))
                    .files.empty());
}

TEST_F(PackTest, WalkDepthIsBoundedAndRequestedSlugsCannotEscape) {
    namespace fs = std::filesystem;
    auto root = dir_ / "contest" / "a-slug" / "attachments";
    auto deep = root;
    for (int i = 0; i < 65; ++i) {
        deep /= "d";
    }
    fs::create_directories(deep);
    std::ofstream(deep / "starter.cpp") << "starter";
    EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip"), std::runtime_error);
    EXPECT_THROW(cxxprobe::problem::inspect_attachments(
                     cxxprobe::problem::load_from_dir(root.parent_path())),
                 std::runtime_error);
    PackOptions opts;
    opts.problem_slugs = {"../contest/a-slug"};
    EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip", opts), std::runtime_error);
}

TEST_F(PackTest, DefaultAbsentStillEmitsExplicitEmptyMetadataWithoutPackWarning) {
    testing::internal::CaptureStderr();
    pack_contest(dir_ / "contest", dir_ / "pack.zip");
    auto warning = testing::internal::GetCapturedStderr();
    EXPECT_EQ(warning.find("attachments directory"), std::string::npos);
    cxxprobe::pack::detail::ZipReader reader(dir_ / "pack.zip");
    auto entries = reader.list_entries();
    auto manifest = nlohmann::ordered_json::parse(reader.read_data(entries.front()));
    EXPECT_EQ(manifest["problems"][0]["attachments"],
              (nlohmann::ordered_json{{"dir", "attachments"},
                                      {"files", nlohmann::ordered_json::array()}}));
}

TEST_F(PackTest, PrivateDirectoryOverlapsAreRejectedIncludingConfiguredLayouts) {
    auto root = dir_ / "contest" / "a-slug";
    for (const auto& dir : {"solutions", "checker/sub", "tests", "validator", "generators",
                            "assets", "assets/reference", "assets/reference/sub"}) {
        std::ofstream yaml(root / "problem.yaml");
        yaml << "version: 2\nname: Test\n";
        if (std::string_view(dir).starts_with("assets")) {
            yaml << "solutions: {dir: assets/reference}\n";
        }
        yaml << "attachments: {dir: '" << dir << "'}\n";
        yaml.close();
        EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip"), std::runtime_error) << dir;
        EXPECT_FALSE(cxxprobe::problem::validate_package(root).ok) << dir;
        EXPECT_THROW(cxxprobe::problem::inspect_attachments(cxxprobe::problem::load_from_dir(root)),
                     std::runtime_error)
            << dir;
    }
}

TEST_F(PackTest, NormalizedDirectoryAndOnlyFilteredFilesHaveConsistentMetadata) {
    auto root = dir_ / "contest" / "a-slug";
    std::ofstream(root / "problem.yaml")
        << "version: 2\nname: Test\nattachments: {dir: './public//start/'}\n";
    std::filesystem::create_directories(root / "public" / "start");
    std::ofstream(root / "public" / "start" / "junk.o") << "excluded";
    auto cfg = cxxprobe::problem::load_from_dir(root);
    auto files = cxxprobe::problem::inspect_attachments(cfg);
    EXPECT_EQ(files.dir, "public/start");
    EXPECT_TRUE(files.files.empty());
    std::ofstream(root / "public" / "start" / "main.cpp") << "starter";
    files = cxxprobe::problem::inspect_attachments(cfg);
    EXPECT_EQ(files.files, (std::vector<std::string>{"main.cpp"}));
    EXPECT_NO_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip"));
}

TEST_F(PackTest, InferredDefaultAttachmentsAndConfiguredPrivateDirectories) {
    auto root = dir_ / "contest" / "a-slug";
    std::filesystem::create_directory(root / "attachments");
    std::ofstream(root / "attachments" / "starter.cpp") << "starter";
    auto cfg = cxxprobe::problem::load_from_dir(root);
    EXPECT_FALSE(cfg.attachments.declared);
    EXPECT_EQ(cxxprobe::problem::inspect_attachments(cfg).files,
              (std::vector<std::string>{"starter.cpp"}));
    pack_contest(dir_ / "contest", dir_ / "pack.zip");
    cxxprobe::pack::detail::ZipReader reader(dir_ / "pack.zip");
    auto entries = reader.list_entries();
    auto manifest = nlohmann::ordered_json::parse(reader.read_data(entries.front()));
    EXPECT_EQ(manifest["problems"][0]["attachments"]["files"],
              (std::vector<std::string>{"starter.cpp"}));
    for (const auto& section : {"solutions", "checker", "tests", "validator", "generators"}) {
        std::ofstream(root / "problem.yaml")
            << "version: 2\nname: Test\n"
            << section << ": {dir: renamed/private}\nattachments: {dir: renamed}\n";
        cfg = cxxprobe::problem::load_from_dir(root);
        EXPECT_THROW(cxxprobe::problem::inspect_attachments(cfg), std::runtime_error) << section;
        EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip"), std::runtime_error)
            << section;
    }
}

TEST_F(PackTest, AbsolutePrivateDirectoryInsideProblemStillBlocksAttachmentOverlap) {
    auto root = dir_ / "contest" / "a-slug";
    std::ofstream(root / "problem.yaml")
        << "version: 2\nname: Test\nsolutions: {dir: '" << (root / "public" / "reference").string()
        << "'}\nattachments: {dir: public}\n";
    EXPECT_THROW(cxxprobe::problem::inspect_attachments(cxxprobe::problem::load_from_dir(root)),
                 std::runtime_error);
    EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip"), std::runtime_error);
}

TEST_F(PackTest, RelativePrivateDirectoryAliasStillBlocksAttachmentOverlap) {
    auto root = dir_ / "contest" / "a-slug";
    std::ofstream(root / "problem.yaml")
        << "version: 2\nname: Test\nsolutions: {dir: ../a-slug/public/reference}\nattachments: "
           "{dir: public}\n";
    EXPECT_THROW(cxxprobe::problem::inspect_attachments(cxxprobe::problem::load_from_dir(root)),
                 std::runtime_error);
    EXPECT_THROW(pack_contest(dir_ / "contest", dir_ / "pack.zip"), std::runtime_error);
}

}  // namespace
