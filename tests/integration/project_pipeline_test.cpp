#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cxxprobe/problem.hpp"
#include "cxxprobe/project_discovery.hpp"

namespace {
namespace fs = std::filesystem;

void write_file(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path} << text;
}

std::string quote(const std::string& text) {
    std::string result = "'";
    for (char c : text) {
        result += c == '\'' ? "'\\''" : std::string(1, c);
    }
    return result + "'";
}

struct Result {
    int code;
    std::string text;
};

class ProjectPipelineTest : public ::testing::Test {
protected:
    // Shared with the generated TEST_F subclasses.
    fs::path root;  // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
    void SetUp() override {
        static unsigned counter = 0;
        root = fs::temp_directory_path() /
               std::format("cxxprobe-project-pipeline-{}-{}", ::getpid(), counter++);
        write_file(root / "contest.yaml", "version: 1\nname: Mixed\n");
    }
    void TearDown() override { fs::remove_all(root); }
    static void problem(const fs::path& path) {
        write_file(path / "problem.yaml", "version: 2\nname: Ordinary\n");
        write_file(path / "solutions/main.cpp", "int main() { return 0; }\n");
    }
    void project(const std::string& slug) {
        write_file(root / slug / "project.yaml",
                   "version: 1\nname: Project\nparts:\n"
                   "  - {dir: parts/one, label: '1'}\n");
        problem(root / slug / "parts/one");
    }
    Result cli(const std::vector<std::string>& args) {
        std::string command = "cd " + quote(root.string()) + " && " + quote(CXXPROBE_CLI_PATH);
        for (const auto& arg : args) {
            command += " " + quote(arg);
        }
        command += " 2>&1";
        FILE* pipe = ::popen(command.c_str(), "r");
        if (pipe == nullptr) {
            throw std::runtime_error("cannot launch CLI");
        }
        std::ostringstream output;
        std::array<char, 4096> buffer{};
        while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output << buffer.data();
        }
        const int status = ::pclose(pipe);
        return {.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1, .text = output.str()};
    }
};

TEST_F(ProjectPipelineTest, MixedContestAndQualifiedPartsValidate) {
    problem(root / "warmup");
    project("desk");
    for (const auto& slug : {"warmup", "desk", "desk/one"}) {
        const auto result = cli({"package", "validate", slug, "--json"});
        EXPECT_EQ(result.code, 0) << result.text;
    }
    EXPECT_EQ(cxxprobe::problem::load_from_dir(root / "desk/parts/one").slug, "desk/one");
    EXPECT_EQ(cxxprobe::problem::load_from_dir(root / "warmup").slug, "warmup");
}

TEST_F(ProjectPipelineTest, SameLabelsAndBasenamesAcrossProjectsAreDistinct) {
    project("desk");
    project("shop");
    const auto discovered = cxxprobe::project::discover_problems(root);
    std::vector<std::string> slugs;
    slugs.reserve(discovered.size());
    for (const auto& entry : discovered) {
        slugs.push_back(entry.slug);
    }
    std::ranges::sort(slugs);
    EXPECT_EQ(slugs, (std::vector<std::string>{"desk/one", "shop/one"}));
    for (const auto& slug : slugs) {
        const auto result = cli({"package", "inspect", slug, "--json"});
        EXPECT_EQ(result.code, 0) << result.text;
    }
}

TEST_F(ProjectPipelineTest, OnlyDeclaredPartsAreDiscovered) {
    project("desk");
    problem(root / "desk/parts/undeclared");
    EXPECT_EQ(cxxprobe::problem::find_problem_dirs(root).size(), 1U);
    EXPECT_THROW(cxxprobe::problem::load_from_dir(root / "desk/parts/undeclared"),
                 std::runtime_error);
    const auto result = cli({"package", "validate", "desk/undeclared", "--json"});
    EXPECT_NE(result.code, 0);
}

TEST_F(ProjectPipelineTest, MalformedProjectNamesOffendingField) {
    project("desk");
    write_file(root / "desk/project.yaml", "version: 1\nname: Project\nparts: []\n");
    const auto result = cli({"package", "validate", "desk", "--json"});
    EXPECT_EQ(result.code, 1) << result.text;
    EXPECT_NE(result.text.find("parts"), std::string::npos);
}

TEST_F(ProjectPipelineTest, InvalidChildReportNamesPart) {
    project("desk");
    fs::remove(root / "desk/parts/one/solutions/main.cpp");
    const auto result = cli({"package", "validate", "desk", "--json"});
    EXPECT_EQ(result.code, 1) << result.text;
    EXPECT_NE(result.text.find("parts[0]"), std::string::npos);
}

TEST_F(ProjectPipelineTest, AmbiguousProblemAndProjectIsRejected) {
    project("desk");
    problem(root / "desk");
    EXPECT_THROW(cxxprobe::project::discover_problems(root), std::runtime_error);
    const auto result = cli({"package", "validate", "desk", "--json"});
    EXPECT_EQ(result.code, 1) << result.text;
}

TEST_F(ProjectPipelineTest, PlainOutputUnchangedWhenProjectAdded) {
    problem(root / "warmup");
    const auto before = cli({"package", "validate", "warmup", "--json"});
    const auto preview_before = cli({"package", "inspect", "warmup", "--json"});
    project("desk");
    const auto after = cli({"package", "validate", "warmup", "--json"});
    const auto preview_after = cli({"package", "inspect", "warmup", "--json"});
    EXPECT_EQ(before.code, after.code);
    EXPECT_EQ(before.text, after.text);
    EXPECT_EQ(preview_before.text, preview_after.text);
}

TEST_F(ProjectPipelineTest, ProjectPackFailsWithoutWritingPartialArchive) {
    project("desk");
    const auto result = cli({"package", "pack", "-o", "mixed.zip"});
    EXPECT_NE(result.code, 0);
    EXPECT_NE(result.text.find("project packaging"), std::string::npos);
    EXPECT_FALSE(fs::exists(root / "mixed.zip"));
    problem(root / "warmup");
    const auto plain = cli({"package", "pack", "--problems", "warmup", "-o", "plain.zip"});
    EXPECT_EQ(plain.code, 0) << plain.text;
}

TEST_F(ProjectPipelineTest, AbsoluteProjectReferenceHasFieldDiagnosticBeforePack) {
    project("desk");
    write_file(root / "desk/project.yaml",
               "version: 1\nname: Project\nparts:\n  - {dir: /tmp, label: '1'}\n");
    const auto result = cli({"package", "pack", "-o", "mixed.zip"});
    EXPECT_NE(result.code, 0);
    EXPECT_NE(result.text.find("parts[0].dir"), std::string::npos);
    EXPECT_FALSE(fs::exists(root / "mixed.zip"));
}

TEST_F(ProjectPipelineTest, SymlinkAliasCannotFlattenProjectPart) {
    project("desk");
    fs::create_directory_symlink(root / "desk/parts/one", root / "alias");
    EXPECT_EQ(cxxprobe::problem::load_from_dir(root / "alias").slug, "desk/one");
    EXPECT_THROW(cxxprobe::project::discover_problems(root), std::runtime_error);
    const auto result = cli({"package", "pack", "--problems", "alias", "-o", "alias.zip"});
    EXPECT_NE(result.code, 0);
    EXPECT_FALSE(fs::exists(root / "alias.zip"));
}

TEST_F(ProjectPipelineTest, IntermediateProjectCannotChangePartIdentity) {
    project("desk");
    write_file(root / "desk/parts/project.yaml",
               "version: 1\nname: Nested\nparts:\n  - {dir: one, label: 'n'}\n");
    const auto result = cli({"package", "validate", "desk", "--json"});
    EXPECT_EQ(result.code, 1) << result.text;
    EXPECT_NE(result.text.find("parts[0].dir"), std::string::npos);
    const auto inspect = cli({"package", "inspect", "desk/one", "--json"});
    EXPECT_NE(inspect.code, 0);
    EXPECT_THROW(cxxprobe::problem::load_from_dir(root / "desk/parts/one"), std::runtime_error);
}

}  // namespace
