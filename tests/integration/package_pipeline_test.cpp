#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../src/pack/detail/zip_archive.hpp"
#include "cxxprobe/problem.hpp"

#ifndef CXXPROBE_CLI_PATH
#error "CXXPROBE_CLI_PATH not defined — check CMakeLists"
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string shell_quote(const std::string& s) { return "'" + s + "'"; }

struct CliResult {
    int exit_code{-1};
    std::string stdout_text;
};

CliResult run_cli(const std::vector<std::string>& args, const fs::path& cwd) {
    std::string cmd = "cd " + shell_quote(cwd.string()) + " && " + shell_quote(CXXPROBE_CLI_PATH);
    for (const auto& a : args) {
        cmd += " " + shell_quote(a);
    }
    cmd += " 2>&1";

    FILE* pipe = ::popen(cmd.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error{"popen failed"};
    }
    std::ostringstream out;
    std::array<char, 4096> buf{};
    while (std::fgets(buf.data(), buf.size(), pipe) != nullptr) {
        out << buf.data();
    }
    int status = ::pclose(pipe);
    CliResult result;
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    result.stdout_text = out.str();
    return result;
}

void write_file(const fs::path& p, std::string_view content) {
    fs::create_directories(p.parent_path());
    std::ofstream ofs{p, std::ios::binary};
    ofs << content;
}

void expect_declared_file(const fs::path& dir, const YAML::Node& section, const char* key) {
    if (!section || !section[key]) {
        return;
    }
    const auto file = dir / section[key].as<std::string>();
    EXPECT_TRUE(fs::is_regular_file(file)) << file;
}

// The package verbs are pure filesystem/YAML work — no compiling, no
// sandbox — so unlike the judging pipelines this suite runs everywhere.
class PackagePipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_dir_ =
            fs::temp_directory_path() / std::format("cxxprobe-package-pipeline-{}-{}",
                                                    static_cast<long>(::getpid()), counter_++);
        fs::create_directories(base_dir_);
        auto r = run_cli({"new", "contest", "Package Contest"}, base_dir_);
        if (r.exit_code != 0) {
            throw std::runtime_error{"new contest failed: " + r.stdout_text};
        }
        contest_dir_ = base_dir_ / "package-contest";
    }

    void TearDown() override {
        if (!base_dir_.empty()) {
            fs::remove_all(base_dir_);
        }
    }

    static int counter_;
    fs::path base_dir_;
    fs::path contest_dir_;
};
int PackagePipelineTest::counter_ = 0;

}  // namespace

TEST_F(PackagePipelineTest, InitScaffoldsTheV2Layout) {
    auto r = run_cli({"package", "init", "Sum Two Numbers"}, contest_dir_);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;

    fs::path dir = contest_dir_ / "sum-two-numbers";
    EXPECT_TRUE(fs::exists(dir / "problem.yaml"));
    EXPECT_TRUE(fs::exists(dir / "statement" / "problem.md"));
    EXPECT_TRUE(fs::exists(dir / "solutions" / "main.cpp"));
    EXPECT_TRUE(fs::is_directory(dir / "tests"));
    // Optional sections are absent until the setter supplies their files.
    EXPECT_FALSE(fs::exists(dir / "validator"));
    EXPECT_FALSE(fs::exists(dir / "generators"));
    EXPECT_FALSE(fs::exists(dir / "attachments"));
}

TEST_F(PackagePipelineTest, InitWritesDeclaredFilesAndEnablesDeclaredChecks) {
    ASSERT_EQ(run_cli({"package", "init", "Fresh"}, contest_dir_).exit_code, 0);
    const fs::path dir = contest_dir_ / "fresh";
    const auto yaml = YAML::LoadFile((dir / "problem.yaml").string());
    const auto config = cxxprobe::problem::load_from_dir(dir);

    // Every declared entry must exist; optional tools should only be examples
    // until the setter supplies their implementation.
    expect_declared_file(dir / config.statement.dir, yaml["statement"], "entry");
    expect_declared_file(dir / config.validator.dir, yaml["validator"], "entry");
    expect_declared_file(dir / config.generators.dir, yaml["generators"], "plan");
    expect_declared_file(dir / config.checker.dir, yaml["checker"]["io"], "entry");
    expect_declared_file(dir / config.checker.dir, yaml["checker"]["behavior"], "entry");
    EXPECT_TRUE(config.checker.behavior.enabled);
    EXPECT_FALSE(config.checker.io.enabled);
    EXPECT_FALSE(config.tests.enabled);
    EXPECT_FALSE(config.symbolic.enabled);
}

TEST_F(PackagePipelineTest, InitRefusesToOverwriteAnExistingDirectory) {
    ASSERT_EQ(run_cli({"package", "init", "Dup"}, contest_dir_).exit_code, 0);
    auto again = run_cli({"package", "init", "Dup"}, contest_dir_);
    EXPECT_NE(again.exit_code, 0);
}

TEST_F(PackagePipelineTest, ValidateAcceptsAFreshlyScaffoldedPackage) {
    ASSERT_EQ(run_cli({"package", "init", "Fresh"}, contest_dir_).exit_code, 0);

    auto r = run_cli({"package", "validate", "fresh", "--json"}, contest_dir_);
    EXPECT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    EXPECT_TRUE(j["ok"]);
    EXPECT_TRUE(j["errors"].empty());
    // A fresh package has no test data yet — that's a warning, not an error.
    EXPECT_FALSE(j["warnings"].empty());
}

TEST_F(PackagePipelineTest, ValidateReportsAMissingSolutionAsAnError) {
    ASSERT_EQ(run_cli({"package", "init", "Broken"}, contest_dir_).exit_code, 0);
    fs::remove(contest_dir_ / "broken" / "solutions" / "main.cpp");

    auto r = run_cli({"package", "validate", "broken", "--json"}, contest_dir_);
    EXPECT_EQ(r.exit_code, 1);
    json j = json::parse(r.stdout_text);
    EXPECT_FALSE(j["ok"]);
    ASSERT_FALSE(j["errors"].empty());
    EXPECT_NE(j["errors"][0].get<std::string>().find("no solution found"), std::string::npos);
}

TEST_F(PackagePipelineTest, ValidateWarnsAboutAnInputWithNoAnswer) {
    ASSERT_EQ(run_cli({"package", "init", "Partial"}, contest_dir_).exit_code, 0);
    write_file(contest_dir_ / "partial" / "tests" / "1.in", "3 4\n");

    auto r = run_cli({"package", "validate", "partial", "--json"}, contest_dir_);
    EXPECT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    EXPECT_TRUE(j["ok"]);
    ASSERT_FALSE(j["warnings"].empty());
    EXPECT_NE(j["warnings"][0].get<std::string>().find("no .ans/.out"), std::string::npos);
}

TEST_F(PackagePipelineTest, ValidateReportsAMalformedGeneratorPlanAsAnError) {
    ASSERT_EQ(run_cli({"package", "init", "GenBad"}, contest_dir_).exit_code, 0);
    write_file(contest_dir_ / "genbad" / "generators" / "plan.yaml", "- { args: [\"1\"] }\n");

    auto r = run_cli({"package", "validate", "genbad", "--json"}, contest_dir_);
    EXPECT_EQ(r.exit_code, 1);
    json j = json::parse(r.stdout_text);
    EXPECT_FALSE(j["ok"]);
}

TEST_F(PackagePipelineTest, InspectReportsTheEnabledSections) {
    ASSERT_EQ(run_cli({"package", "init", "Inspected"}, contest_dir_).exit_code, 0);
    fs::path dir = contest_dir_ / "inspected";
    write_file(dir / "validator" / "validator.cpp", "int main(){}\n");
    write_file(dir / "tests" / "1.in", "1\n");
    write_file(dir / "tests" / "1.ans", "1\n");

    auto r = run_cli({"package", "inspect", "inspected", "--json"}, contest_dir_);
    ASSERT_EQ(r.exit_code, 0) << r.stdout_text;
    json j = json::parse(r.stdout_text);
    EXPECT_EQ(j["slug"], "inspected");
    EXPECT_TRUE(j["has_validator"]);
    EXPECT_FALSE(j["has_checker_io"]);
    ASSERT_EQ(j["solutions"].size(), 1U);
    EXPECT_EQ(j["solutions"][0]["file"], "main.cpp");
    EXPECT_TRUE(j["solutions"][0]["primary"]);
    EXPECT_EQ(j["sample_tests"].size(), 1U);
}

TEST_F(PackagePipelineTest, UnknownProblemNameExitsTwo) {
    auto r = run_cli({"package", "validate", "does-not-exist"}, contest_dir_);
    EXPECT_EQ(r.exit_code, 2);
}

TEST_F(PackagePipelineTest, TopLevelPackAndUnpackAndNewProblemAreGone) {
    // These moved under `package` in the v2 break; the old spellings must
    // not silently resolve to something else.
    EXPECT_NE(run_cli({"pack"}, contest_dir_).exit_code, 0);
    EXPECT_NE(run_cli({"unpack", "x.zip"}, contest_dir_).exit_code, 0);
    EXPECT_NE(run_cli({"new", "problem", "Nope"}, contest_dir_).exit_code, 0);
}

TEST_F(PackagePipelineTest, AttachmentInspectValidateAndPackUseTheConfiguredDirectory) {
    ASSERT_EQ(run_cli({"package", "init", "Candidate"}, contest_dir_).exit_code, 0);
    auto root = contest_dir_ / "candidate";
    write_file(root / "problem.yaml",
               "version: 2\nname: Candidate\nattachments: {dir: candidate/start}\n");
    for (bool empty : {false, true}) {
        if (empty) {
            fs::create_directories(root / "candidate" / "start");
        }
        auto validation = run_cli({"package", "validate", "candidate", "--json"}, contest_dir_);
        ASSERT_EQ(validation.exit_code, 0) << validation.stdout_text;
        auto warnings = json::parse(validation.stdout_text)["warnings"].dump();
        EXPECT_NE(warnings.find("no packable candidate files"), std::string::npos);
        auto packed =
            run_cli({"package", "pack", "-o", (base_dir_ / "pack.zip").string()}, contest_dir_);
        EXPECT_EQ(packed.exit_code, 0) << packed.stdout_text;
        EXPECT_NE(packed.stdout_text.find("no packable candidate files"), std::string::npos);
        auto inspected = run_cli({"package", "inspect", "candidate", "--json"}, contest_dir_);
        ASSERT_EQ(inspected.exit_code, 0) << inspected.stdout_text;
        EXPECT_EQ(json::parse(inspected.stdout_text)["attachments"]["files"], json::array());
    }
    write_file(root / "candidate" / "start" / "nested" / "helper.hpp", "header");
    write_file(root / "candidate" / "start" / "main.cpp", "starter");
    write_file(root / "candidate" / "start" / "skip.o", "excluded");
    auto inspected = run_cli({"package", "inspect", "candidate", "--json"}, contest_dir_);
    ASSERT_EQ(inspected.exit_code, 0) << inspected.stdout_text;
    auto attachments = json::parse(inspected.stdout_text)["attachments"];
    EXPECT_EQ(attachments["dir"], "candidate/start");
    EXPECT_EQ(attachments["files"], (std::vector<std::string>{"main.cpp", "nested/helper.hpp"}));
    auto text = run_cli({"package", "inspect", "candidate"}, contest_dir_);
    EXPECT_EQ(text.exit_code, 0) << text.stdout_text;
    EXPECT_NE(text.stdout_text.find("candidate/start (2 candidate files)"), std::string::npos);
    EXPECT_NE(text.stdout_text.find("nested/helper.hpp"), std::string::npos);
    auto packed =
        run_cli({"package", "pack", "-o", (base_dir_ / "pack.zip").string()}, contest_dir_);
    EXPECT_EQ(packed.exit_code, 0) << packed.stdout_text;
    EXPECT_EQ(packed.stdout_text.find("no packable candidate files"), std::string::npos);
    write_file(root / "problem.yaml",
               "version: 2\nname: Candidate\nattachments: {dir: ../private}\n");
    EXPECT_EQ(run_cli({"package", "validate", "candidate", "--json"}, contest_dir_).exit_code, 1);
    EXPECT_EQ(run_cli({"package", "inspect", "candidate", "--json"}, contest_dir_).exit_code, 2);
    EXPECT_EQ(run_cli({"package", "pack", "-o", (base_dir_ / "unsafe.zip").string()}, contest_dir_)
                  .exit_code,
              2);
}

TEST_F(PackagePipelineTest, ControlCharactersCannotEnterAttachmentMetadataOrArchivePaths) {
    ASSERT_EQ(run_cli({"package", "init", "Controls"}, contest_dir_).exit_code, 0);
    auto root = contest_dir_ / "controls";
    write_file(root / "attachments" / "main.cpp", "starter");
    for (int code = 1; code <= 127; ++code) {
        if (code >= 32 && code != 127) {
            continue;
        }
        auto name = std::string("bad") + static_cast<char>(code) + ".cpp";
        write_file(root / "attachments" / name, "excluded");
        write_file(
            root / "attachments" / (std::string("dir") + static_cast<char>(code)) / "leak.cpp",
            "excluded");
    }
    auto inspected = run_cli({"package", "inspect", "controls", "--json"}, contest_dir_);
    ASSERT_EQ(inspected.exit_code, 0) << inspected.stdout_text;
    EXPECT_EQ(json::parse(inspected.stdout_text)["attachments"]["files"],
              (std::vector<std::string>{"main.cpp"}));
    auto packed =
        run_cli({"package", "pack", "-o", (base_dir_ / "controls.zip").string()}, contest_dir_);
    ASSERT_EQ(packed.exit_code, 0) << packed.stdout_text;
    cxxprobe::pack::detail::ZipReader reader(base_dir_ / "controls.zip");
    auto entries = reader.list_entries();
    for (const auto& entry : entries) {
        EXPECT_FALSE(
            std::ranges::any_of(entry.name, [](unsigned char ch) { return ch < 32 || ch == 127; }));
    }
    auto manifest = json::parse(reader.read_data(entries.front()));
    EXPECT_EQ(manifest["problems"][0]["attachments"]["files"],
              (std::vector<std::string>{"main.cpp"}));
    for (int code = 0; code <= 127; ++code) {
        if (code >= 32 && code != 127) {
            continue;
        }
        std::ostringstream yaml;
        yaml << "version: 2\nname: Controls\nattachments: {dir: \"bad\\x" << std::hex
             << std::setw(2) << std::setfill('0') << code << "\"}\n";
        write_file(root / "problem.yaml", yaml.str());
        EXPECT_EQ(run_cli({"package", "inspect", "controls", "--json"}, contest_dir_).exit_code, 2)
            << code;
        EXPECT_EQ(run_cli({"package", "validate", "controls", "--json"}, contest_dir_).exit_code, 1)
            << code;
        EXPECT_EQ(
            run_cli({"package", "pack", "-o", (base_dir_ / "unsafe.zip").string()}, contest_dir_)
                .exit_code,
            2)
            << code;
    }
}

TEST_F(PackagePipelineTest, RenamedPrivateDirectoriesFreeTheirConventionalNamesForAttachments) {
    for (const auto& section : {"solutions", "checker", "tests", "validator", "generators"}) {
        std::string slug = std::string("reuse-") + section;
        auto initialized = run_cli({"package", "init", slug}, contest_dir_);
        ASSERT_EQ(initialized.exit_code, 0) << initialized.stdout_text;
        auto root = contest_dir_ / slug;
        if (fs::exists(root / section)) {
            fs::rename(root / section, root / "private");
        }
        write_file(root / section / "starter.cpp", "// candidate starter\n");
        write_file(root / "problem.yaml", "version: 2\nname: Reused\n" + std::string(section) +
                                              ": {dir: ./private/}\nattachments: {dir: " + section +
                                              "}\n");
        auto validation = run_cli({"package", "validate", slug, "--json"}, contest_dir_);
        EXPECT_EQ(validation.exit_code, 0) << validation.stdout_text;
        auto inspected = run_cli({"package", "inspect", slug, "--json"}, contest_dir_);
        ASSERT_EQ(inspected.exit_code, 0) << inspected.stdout_text;
        auto metadata = json::parse(inspected.stdout_text)["attachments"];
        EXPECT_EQ(metadata["dir"], section);
        EXPECT_EQ(metadata["files"], (std::vector<std::string>{"starter.cpp"}));
        auto packed = run_cli(
            {"package", "pack", "--problems", slug, "-o", (base_dir_ / "reuse.zip").string()},
            contest_dir_);
        ASSERT_EQ(packed.exit_code, 0) << packed.stdout_text;
        cxxprobe::pack::detail::ZipReader reader(base_dir_ / "reuse.zip");
        auto entries = reader.list_entries();
        auto manifest = json::parse(reader.read_data(entries.front()));
        EXPECT_EQ(manifest["problems"][0]["attachments"], metadata);
        // Moving the actual private directory under the candidate directory still fails.
        write_file(root / "problem.yaml", "version: 2\nname: Reused\n" + std::string(section) +
                                              ": {dir: " + section +
                                              "/private}\nattachments: {dir: " + section + "}\n");
        EXPECT_EQ(run_cli({"package", "validate", slug, "--json"}, contest_dir_).exit_code, 1);
        EXPECT_EQ(run_cli({"package", "inspect", slug, "--json"}, contest_dir_).exit_code, 2);
        EXPECT_EQ(run_cli({"package", "pack", "--problems", slug, "-o",
                           (base_dir_ / "unsafe.zip").string()},
                          contest_dir_)
                      .exit_code,
                  2);
    }
}
