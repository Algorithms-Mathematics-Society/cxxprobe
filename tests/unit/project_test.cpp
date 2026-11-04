#include "cxxprobe/project.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {
namespace fs = std::filesystem;

constexpr std::string_view kBase =
    "version: 1\nname: Desk\nparts:\n  - {dir: parts/one, label: \"1\"}\n";

class ProjectTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto pattern = (fs::temp_directory_path() / "cxxprobe-project-XXXXXX").string();
        const auto* created = ::mkdtemp(pattern.data());
        ASSERT_NE(created, nullptr);
        workspace_ = created;
        dir_ = workspace_ / "desk";
        write("project.yaml", kBase);
        write("parts/one/problem.yaml", "version: 2\nname: Part\n");
    }
    void TearDown() override { fs::remove_all(workspace_); }
    void write(const fs::path& name, std::string_view content) {
        fs::create_directories((dir_ / name).parent_path());
        std::ofstream{dir_ / name} << content;
    }
    void invalid(std::string_view document, std::string_view field) {
        write("project.yaml", document);
        try {
            (void)cxxprobe::project::load_from_dir(dir_);
            FAIL() << "accepted invalid " << field << ": " << document;
        } catch (const std::runtime_error& error) {
            EXPECT_NE(std::string{error.what()}.find(field), std::string::npos) << error.what();
        }
        const auto report = cxxprobe::project::validate_package(dir_);
        EXPECT_FALSE(report.ok);
        ASSERT_FALSE(report.errors.empty());
        EXPECT_NE(report.errors.front().find(field), std::string::npos);
    }
    // GTest generates subclasses that share this fixture state.
    fs::path workspace_;  // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
    fs::path dir_;        // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(ProjectTest, DefaultsAndIdentityWithoutLoadingPartConfig) {
    write("parts/one/problem.yaml", "not a valid problem config");
    const auto cfg = cxxprobe::project::load_from_dir(dir_ / ".");
    EXPECT_EQ(cfg.project_dir, dir_);
    EXPECT_EQ(cfg.slug, "desk");
    EXPECT_EQ(cfg.name, "Desk");
    EXPECT_EQ(cfg.shared.dir, "shared");
    EXPECT_TRUE(cfg.shared.sources.empty());
    EXPECT_EQ(cfg.integration.dir, "integration");
    EXPECT_EQ(cfg.integration.entry, "integration_gtest.cpp");
    EXPECT_FALSE(cfg.integration.fallback_dir);
    ASSERT_EQ(cfg.parts.size(), 1);
    EXPECT_EQ(cfg.parts[0].dir, "parts/one");
    EXPECT_EQ(cfg.parts[0].label, "1");
}

TEST_F(ProjectTest, ExplicitSectionsResolveAgainstTheirOwnBase) {
    write("project.yaml", std::string{kBase} +
                              "shared: {dir: support/common, sources: [drivers/main.cpp]}\n"
                              "integration: {dir: checks, entry: suites/test.cpp, "
                              "fallback_dir: stubs/empty}\n");
    write("support/common/drivers/main.cpp", "// driver");
    write("checks/suites/test.cpp", "// integration");
    fs::create_directories(dir_ / "checks/stubs/empty");
    const auto cfg = cxxprobe::project::load_from_dir(dir_);
    EXPECT_EQ(cfg.shared.sources, std::vector<std::string>{"drivers/main.cpp"});
    EXPECT_EQ(cfg.integration.fallback_dir, "stubs/empty");
    // Configured optional entry/fallback may be absent.
    fs::remove_all(dir_ / "checks");
    EXPECT_NO_THROW(cxxprobe::project::load_from_dir(dir_));
}

TEST_F(ProjectTest, StrictScalarTypesAndRequiredFields) {
    for (const auto* version : {"\"1\"", "true", "null", "2", "1.0", "[]"}) {
        invalid(std::string{"version: "} + version +
                    "\nname: Desk\nparts: [{dir: parts/one, label: \"1\"}]\n",
                "version");
    }
    for (const auto* name : {"true", "42", "1.5", "null", "[]", R"("  \t")"}) {
        invalid(std::string{"version: 1\nname: "} + name +
                    "\nparts: [{dir: parts/one, label: \"1\"}]\n",
                "name");
    }
    for (const auto* label : {"1", "true", "null", "[]", R"("a\n")", "\"a/b\""}) {
        invalid(std::string{"version: 1\nname: Desk\nparts: [{dir: parts/one, label: "} + label +
                    "}]\n",
                "parts[0].label");
    }
    invalid("name: Desk\nparts: []\n", "version");
    invalid("version: 1\nparts: []\n", "name");
    invalid("version: 1\nname: Desk\n", "parts");
    invalid("version: 1\nname: Desk\nparts: []\n", "parts");
    invalid("version: 1\nname: Desk\nparts: [{}]\n", "parts[0].dir");
}

TEST_F(ProjectTest, DuplicateUnknownNonStringKeysAndNullSections) {
    for (const auto* tail : {
             "name: Again\n",
             "unknown: true\n",
             "true: value\n",
             "shared: null\n",
             "integration: null\n",
             "shared: {dir: shared, dir: other}\n",
             "shared: {sources: null}\n",
             "shared: {unknown: true}\n",
             "integration: {entry: null}\n",
             "integration: {dir: checks, dir: other}\n",
             "integration: {unknown: true}\n",
         }) {
        invalid(std::string{kBase} + tail, "project.yaml");
    }
    invalid("version: 1\nname: Desk\nparts: [{dir: parts/one, label: a, label: b}]\n",
            "parts[0].label");
    invalid("version: 1\nname: Desk\nparts: [{dir: parts/one, label: a, unknown: b}]\n",
            "parts[0].unknown");
    invalid("[]", "project.yaml");
    invalid("{", "project.yaml");
}

TEST_F(ProjectTest, PathSyntaxRejectsTraversalAndNonPortableSpellings) {
    for (const auto* path :
         {"", ".", "..", "../out", "a/../b", "/tmp/out", "a//b", "a/", "./a", "C:/a", "a\\b"}) {
        for (const auto* section : {"shared", "integration"}) {
            // YAML single quotes preserve backslash for the lexical check.
            invalid(std::string{kBase} + section + ": {dir: '" + path + "'}\n",
                    std::string{section} + ".dir");
        }
    }
    invalid(std::string{kBase} + "shared: {sources: ['../driver.cpp']}\n", "shared.sources[0]");
    invalid(std::string{kBase} + "integration: {entry: '../test.cpp'}\n", "integration.entry");
    invalid(std::string{kBase} + "integration: {fallback_dir: '../fallback'}\n",
            "integration.fallback_dir");
}

TEST_F(ProjectTest, UniqueLabelsBasenamesAndSources) {
    invalid(std::string{kBase} + "  - {dir: other/two, label: \"1\"}\n", "parts[1].label");
    invalid(std::string{kBase} + "  - {dir: other/one, label: \"2\"}\n", "parts[1].dir");
    invalid(std::string{kBase} + "shared: {sources: [a.cpp, a.cpp]}\n", "shared.sources[1]");
    write("parts/two/problem.yaml", "version: 2\nname: Two\n");
    write("project.yaml",
          "version: 1\nname: Desk\nparts: [{dir: parts/one, label: one}, "
          "{dir: parts/two, label: ONE}]\n");
    EXPECT_NO_THROW(cxxprobe::project::load_from_dir(dir_));
}

TEST_F(ProjectTest, MissingPartProblemAndSourceFilesNameTheirField) {
    fs::remove(dir_ / "parts/one/problem.yaml");
    invalid(kBase, "parts[0].dir.problem.yaml");
    fs::remove_all(dir_ / "parts/one");
    invalid(kBase, "parts[0].dir");
    write("parts/one/problem.yaml", "version: 2\nname: Part\n");
    invalid(std::string{kBase} + "shared: {sources: [driver.cpp]}\n", "shared.dir");
    fs::create_directories(dir_ / "shared");
    invalid(std::string{kBase} + "shared: {sources: [driver.cpp]}\n", "shared.sources[0]");
    fs::create_directories(dir_ / "shared/driver.cpp");
    invalid(std::string{kBase} + "shared: {sources: [driver.cpp]}\n", "shared.sources[0]");
}

TEST_F(ProjectTest, BothProblemAndProjectAndNestedProjectAreRejected) {
    write("problem.yaml", "version: 2\nname: Plain\n");
    invalid(kBase, "project_dir");
    fs::remove(dir_ / "problem.yaml");
    write("parts/one/project.yaml", kBase);
    invalid(kBase, "parts[0].dir");
    fs::remove(dir_ / "parts/one/project.yaml");
    write("parts/project.yaml", kBase);
    invalid(kBase, "parts[0].dir");
}

TEST_F(ProjectTest, OutsideAndDanglingOptionalAncestorsAreRejected) {
    fs::create_directories(workspace_ / "outside");
    fs::create_directory_symlink(workspace_ / "outside", dir_ / "shared");
    invalid(kBase, "shared.dir");
    fs::remove(dir_ / "shared");
    fs::create_directory_symlink(workspace_ / "missing", dir_ / "integration");
    invalid(kBase, "integration.dir");
    fs::remove(dir_ / "integration");
    fs::create_directories(dir_ / "integration");
    fs::create_directory_symlink(workspace_ / "missing", dir_ / "integration/escape");
    invalid(std::string{kBase} + "integration: {entry: escape/missing.cpp}\n", "integration.entry");
    invalid(std::string{kBase} + "integration: {fallback_dir: escape/missing}\n",
            "integration.fallback_dir");
}

TEST_F(ProjectTest, SourceAndIntegrationEntryStayWithinTheirSection) {
    write("secret.cpp", "// elsewhere in project");
    fs::create_directories(dir_ / "shared");
    fs::create_symlink(dir_ / "secret.cpp", dir_ / "shared/driver.cpp");
    invalid(std::string{kBase} + "shared: {sources: [driver.cpp]}\n", "shared.sources[0]");
    fs::create_directories(dir_ / "integration");
    fs::create_symlink(dir_ / "secret.cpp", dir_ / "integration/integration_gtest.cpp");
    invalid(kBase, "integration.entry");
}

TEST_F(ProjectTest, EscapingPartAndProblemFileAreRejected) {
    fs::create_directories(workspace_ / "outside");
    fs::remove_all(dir_ / "parts/one");
    fs::create_directory_symlink(workspace_ / "outside", dir_ / "parts/one");
    invalid(kBase, "parts[0].dir");
    fs::remove(dir_ / "parts/one");
    fs::create_directories(dir_ / "parts/one");
    std::ofstream{workspace_ / "outside/problem.yaml"} << "version: 2\nname: Outside\n";
    fs::create_symlink(workspace_ / "outside/problem.yaml", dir_ / "parts/one/problem.yaml");
    invalid(kBase, "parts[0].dir.problem.yaml");
    EXPECT_THROW(cxxprobe::project::declared_part_slug(dir_, "parts/one"), std::runtime_error);
}

TEST_F(ProjectTest, PackageReportCombinesPartErrorsAndWarnings) {
    const auto bad = cxxprobe::project::validate_package(dir_);
    EXPECT_FALSE(bad.ok);
    ASSERT_FALSE(bad.errors.empty());
    EXPECT_NE(bad.errors.front().find("parts[0] (label '1')"), std::string::npos);
    write("parts/one/solutions/main.cpp", "int main() {}\n");
    const auto good = cxxprobe::project::validate_package(dir_);
    EXPECT_TRUE(good.ok);
    EXPECT_TRUE(good.errors.empty());
    ASSERT_FALSE(good.warnings.empty());
    EXPECT_NE(good.warnings.front().find("parts[0] (label '1')"), std::string::npos);
}

TEST_F(ProjectTest, NameTypesMatchSchemaIncludingUnicodeWhitespaceAndTimestamps) {
    for (const auto* name : {
             "2026-11-04",
             "2026-11-04T12:30:00Z",
             "2026-11-04 12:30:00+05:30",
             "\"\xc2\xa0\"",
             "\"\xe2\x80\x83\"",
         }) {
        invalid(std::string{"version: 1\nname: "} + name +
                    "\nparts: [{dir: parts/one, label: \"1\"}]\n",
                "name");
    }
    write("project.yaml",
          "version: 1\nname: \"2026-11-04\"\n"
          "parts: [{dir: parts/one, label: \"1\"}]\n");
    EXPECT_EQ(cxxprobe::project::load_from_dir(dir_).name, "2026-11-04");
}

TEST_F(ProjectTest, DeclaredIdentityDoesNotValidateOtherPartFiles) {
    write("project.yaml", std::string{kBase} + "  - {dir: parts/two, label: \"2\"}\n");
    EXPECT_EQ(cxxprobe::project::declared_part_slug(dir_, dir_ / "parts/one"), "desk/one");
    EXPECT_EQ(cxxprobe::project::declared_part_slug(dir_, "parts/one"), "desk/one");
    EXPECT_FALSE(cxxprobe::project::declared_part_slug(dir_, "parts/unknown"));
    EXPECT_THROW(cxxprobe::project::load_from_dir(dir_), std::runtime_error);
    write("parts/project.yaml", kBase);
    EXPECT_THROW(cxxprobe::project::declared_part_slug(dir_, "parts/one"), std::runtime_error);
}

TEST_F(ProjectTest, ResolvedIntermediateProjectIsRejected) {
    write("hidden/project.yaml", kBase);
    write("hidden/container/one/problem.yaml", "version: 2\nname: Part\n");
    fs::remove_all(dir_ / "parts");
    fs::create_directory_symlink(dir_ / "hidden/container", dir_ / "parts");
    invalid(kBase, "parts[0].dir");
    EXPECT_THROW(cxxprobe::project::declared_part_slug(dir_, "parts/one"), std::runtime_error);
}

}  // namespace
