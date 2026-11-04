#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../../src/pack/detail/zip_archive.hpp"
#include "cxxprobe/integration.hpp"
#include "cxxprobe/problem.hpp"
#include "cxxprobe/project.hpp"
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

std::vector<std::string> archive_names(const fs::path& zip) {
    std::vector<std::string> names;
    for (const auto& entry : cxxprobe::pack::detail::ZipReader{zip}.list_entries()) {
        names.push_back(entry.name);
    }
    std::ranges::sort(names);
    return names;
}

nlohmann::json archive_manifest(const fs::path& zip) {
    const cxxprobe::pack::detail::ZipReader reader{zip};
    for (const auto& entry : reader.list_entries()) {
        if (entry.name == "manifest.json") {
            return nlohmann::json::parse(reader.read_data(entry));
        }
    }
    throw std::runtime_error("no manifest.json");
}

bool contains(const std::vector<std::string>& names, const std::string& name) {
    return std::ranges::find(names, name) != names.end();
}

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

TEST_F(ProjectPipelineTest, SelectingOnePartOfAProjectIsRejected) {
    project("desk");
    const auto result = cli({"package", "pack", "--problems", "desk/one", "-o", "part.zip"});
    EXPECT_NE(result.code, 0);
    EXPECT_NE(result.text.find("pack the project whole"), std::string::npos) << result.text;
    EXPECT_FALSE(fs::exists(root / "part.zip"));
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

// A project whose one part has no extra_sources of its own: everything it
// links against comes from the project's shared section.
void shared_project(const fs::path& desk, const std::string& sources) {
    write_file(desk / "project.yaml", "version: 1\nname: Desk\nshared:\n  sources: " + sources +
                                          "\nparts:\n  - {dir: parts/one, label: '1'}\n");
    write_file(desk / "shared/order.hpp", "#pragma once\nint order_total(int lines);\n");
    write_file(desk / "shared/order.cpp",
               "#include \"order.hpp\"\nint order_total(int lines) { return lines * 3; }\n");
    const auto part = desk / "parts/one";
    write_file(part / "problem.yaml",
               "version: 2\nname: Ingest\nsymbolic:\n  must_include: [order_total]\n");
    write_file(part / "solutions/main.cpp",
               "#include <iostream>\n#include \"order.hpp\"\n"
               "int main() { int n = 0; std::cin >> n; std::cout << order_total(n) << '\\n';\n"
               "    return 0;\n}\n");
    write_file(part / "tests/1.in", "4\n");
    write_file(part / "tests/1.out", "12\n");
    write_file(part / "checker/behavior_gtest.cpp",
               "#define main solution_main\n#include CXXPROBE_SOLUTION_FILE\n#undef main\n"
               "#include <gtest/gtest.h>\n"
               "TEST(Behavior, SharedTotal) { EXPECT_EQ(order_total(2), 6); }\n");
}

TEST_F(ProjectPipelineTest, SharedSourcesReachEveryPartBuild) {
    shared_project(root / "desk", "[order.cpp]");
    const auto config = cxxprobe::problem::load_from_dir(root / "desk/parts/one");
    EXPECT_EQ(config.compiler.extra_sources, (std::vector<std::string>{"../../shared/order.cpp"}));
    EXPECT_EQ(config.compiler.shared_sources, config.compiler.extra_sources);
    EXPECT_EQ(config.compiler.include_dirs, (std::vector<std::string>{"../../shared"}));

    const auto result = cli({"test", "problem", "desk/one", "--json"});
    ASSERT_EQ(result.code, 0) << result.text;
    const auto report = nlohmann::json::parse(result.text);
    for (const auto* section : {"manual", "symbolic", "behavior"}) {
        EXPECT_EQ(report["tests"][section]["status"], "PASS") << section << "\n" << result.text;
    }
}

TEST_F(ProjectPipelineTest, MissingSharedSourceFailsWithTheCompilerDiagnostic) {
    shared_project(root / "desk", "[]");
    const auto result = cli({"test", "problem", "desk/one", "--json"});
    EXPECT_EQ(result.code, 2) << result.text;
    EXPECT_NE(result.text.find("order_total"), std::string::npos) << result.text;
    EXPECT_NE(result.text.find("undefined reference"), std::string::npos) << result.text;
}

TEST_F(ProjectPipelineTest, PartRedeclaringASharedSourceIsRejected) {
    shared_project(root / "desk", "[order.cpp]");
    write_file(root / "desk/parts/one/problem.yaml",
               "version: 2\nname: Ingest\ncompiler:\n  extra_sources: [../../shared/order.cpp]\n");
    EXPECT_THROW(cxxprobe::problem::load_from_dir(root / "desk/parts/one"), std::runtime_error);
    const auto result = cli({"package", "validate", "desk", "--json"});
    EXPECT_EQ(result.code, 1) << result.text;
    EXPECT_NE(result.text.find("compiler.extra_sources"), std::string::npos) << result.text;
}

TEST_F(ProjectPipelineTest, InspectShowsInjectedSharedSources) {
    shared_project(root / "desk", "[order.cpp]");
    const auto json = cli({"package", "inspect", "desk/one", "--json"});
    EXPECT_EQ(json.code, 0) << json.text;
    EXPECT_NE(json.text.find("\"shared_sources\""), std::string::npos) << json.text;
    EXPECT_NE(json.text.find("../../shared/order.cpp"), std::string::npos) << json.text;
    const auto text = cli({"package", "inspect", "desk/one"});
    EXPECT_EQ(text.code, 0) << text.text;
    EXPECT_NE(text.text.find("shared sources  1"), std::string::npos) << text.text;
    EXPECT_NE(text.text.find("-I ../../shared"), std::string::npos) << text.text;
}

TEST_F(ProjectPipelineTest, MixedContestPacksUnpacksAndStillPasses) {
    problem(root / "warmup");
    shared_project(root / "desk", "[order.cpp]");
    write_file(root / "desk/notes/scratch.txt", "not declared, not packed\n");
    const auto packed = cli({"package", "pack", "-o", "mixed.zip"});
    ASSERT_EQ(packed.code, 0) << packed.text;
    EXPECT_NE(packed.text.find("desk  (Desk, 1 parts)"), std::string::npos) << packed.text;

    const auto names = archive_names(root / "mixed.zip");
    for (const auto* name : {"desk/project.yaml", "desk/shared/order.hpp", "desk/shared/order.cpp",
                             "desk/parts/one/problem.yaml", "desk/parts/one/solutions/main.cpp",
                             "warmup/problem.yaml"}) {
        EXPECT_TRUE(contains(names, name)) << name;
    }
    EXPECT_FALSE(contains(names, "desk/notes/scratch.txt"));

    const auto manifest = archive_manifest(root / "mixed.zip");
    ASSERT_EQ(manifest["problems"].size(), 1U);
    EXPECT_EQ(manifest["problems"][0]["slug"], "warmup");
    ASSERT_EQ(manifest["projects"].size(), 1U);
    const auto& desk = manifest["projects"][0];
    EXPECT_EQ(desk["slug"], "desk");
    EXPECT_EQ(desk["name"], "Desk");
    ASSERT_EQ(desk["parts"].size(), 1U);
    EXPECT_EQ(desk["parts"][0]["label"], "1");
    EXPECT_EQ(desk["parts"][0]["slug"], "desk/one");
    EXPECT_EQ(desk["parts"][0]["dir"], "parts/one");
    EXPECT_EQ(desk["parts"][0]["preview"]["slug"], "desk/one");

    const auto unpacked = cli({"package", "unpack", "mixed.zip", "-C", "out"});
    ASSERT_EQ(unpacked.code, 0) << unpacked.text;
    EXPECT_NE(unpacked.text.find("Unpacked 1 project(s)"), std::string::npos) << unpacked.text;
    const auto tested =
        cli({"test", "problem", "desk/one", "-C", (root / "out").string(), "--json"});
    EXPECT_EQ(tested.code, 0) << tested.text;
    const auto project = cli({"test", "project", "desk", "-C", (root / "out").string(), "--json"});
    EXPECT_EQ(project.code, 0) << project.text;
}

TEST_F(ProjectPipelineTest, PlainContestManifestHasNoProjectsKey) {
    problem(root / "warmup");
    const auto packed = cli({"package", "pack", "-o", "plain.zip"});
    ASSERT_EQ(packed.code, 0) << packed.text;
    const auto manifest = archive_manifest(root / "plain.zip");
    std::vector<std::string> keys;
    for (const auto& [key, value] : manifest.items()) {
        keys.push_back(key);
    }
    EXPECT_EQ(keys, (std::vector<std::string>{"contest", "cxxprobe_version", "format_version",
                                              "packed_at", "problems"}));
}

TEST_F(ProjectPipelineTest, SymlinkedSharedSourceIsNeverPacked) {
    shared_project(root / "desk", "[order.cpp]");
    fs::rename(root / "desk/shared/order.cpp", root / "desk/shared/real.cpp");
    fs::create_symlink("real.cpp", root / "desk/shared/order.cpp");
    const auto result = cli({"package", "pack", "--problems", "desk", "-o", "desk.zip"});
    EXPECT_NE(result.code, 0);
    EXPECT_NE(result.text.find("shared source 'shared/order.cpp'"), std::string::npos)
        << result.text;
    EXPECT_FALSE(fs::exists(root / "desk.zip"));
}

// Two parts linked through one shared header: part 1 defines ingest(), part 2
// defines book(), and the integration suite exercises both together.
void write_part(const fs::path& dir, const std::string& name, const std::string& body) {
    write_file(dir / "problem.yaml", "version: 2\nname: " + name + "\n");
    write_file(dir / "solutions/main.cpp",
               "#include \"desk.hpp\"\n" + body + "\nint main() { return 0; }\n");
}

void linked_project(const fs::path& desk, const std::string& integration_extra = "") {
    write_file(desk / "project.yaml",
               "version: 1\nname: Desk\nparts:\n  - {dir: parts/ingest, label: '1'}\n"
               "  - {dir: parts/book, label: '2'}\n" +
                   integration_extra);
    write_file(desk / "shared/desk.hpp", "#pragma once\nint ingest(int);\nint book(int);\n");
    write_part(desk / "parts/ingest", "Ingest", "int ingest(int x) { return x * 2; }");
    write_part(desk / "parts/book", "Book", "int book(int x) { return x + 5; }");
    write_file(desk / "integration/integration_gtest.cpp",
               "#include <gtest/gtest.h>\n#include \"desk.hpp\"\n"
               "TEST(Integration, Part1_Ingest) { EXPECT_EQ(ingest(2), 4); }\n"
               "TEST(Integration, Part_2_Book) { EXPECT_EQ(book(1), 6); }\n"
               "TEST(Integration, RoundTrip) { EXPECT_EQ(book(ingest(1)), 7); }\n");
}

cxxprobe::judge::IntegrationReport integrate(const fs::path& desk) {
    const auto project = cxxprobe::project::load_from_dir(desk);
    return cxxprobe::judge::run_integration(project, {},
                                            cxxprobe::judge::reference_sources(project));
}

TEST_F(ProjectPipelineTest, CorrectPartsLinkAndTheSuitePasses) {
    linked_project(root / "desk");
    const auto report = integrate(root / "desk");
    ASSERT_EQ(report.status, cxxprobe::judge::Status::Pass)
        << report.error << report.link.diagnostics;
    EXPECT_EQ(report.total, 3);
    EXPECT_EQ(report.passed, 3);
    ASSERT_EQ(report.parts.size(), 2U);
    EXPECT_EQ(report.parts[0].origin, "reference");
    EXPECT_EQ(report.parts[1].slug, "desk/book");
    ASSERT_EQ(report.cases.size(), 3U);
    EXPECT_EQ(report.cases[0].parts, (std::vector<std::string>{"1"}));
    EXPECT_EQ(report.cases[1].parts, (std::vector<std::string>{"2"}));
    EXPECT_TRUE(report.cases[2].parts.empty());
}

TEST_F(ProjectPipelineTest, MissingDefinitionIsALinkErrorNamingTheLinkedParts) {
    linked_project(root / "desk");
    // Compiles alone, but never defines the book() the header promises.
    write_part(root / "desk/parts/book", "Book", "");
    const auto report = integrate(root / "desk");
    EXPECT_EQ(report.status, cxxprobe::judge::Status::Error);
    EXPECT_FALSE(report.link.ok);
    EXPECT_NE(report.link.diagnostics.find("book"), std::string::npos) << report.link.diagnostics;
    EXPECT_NE(report.error.find("part '2' (reference)"), std::string::npos) << report.error;
    EXPECT_NE(report.error.find("parts/book/solutions/main.cpp"), std::string::npos)
        << report.error;
}

TEST_F(ProjectPipelineTest, FailingCaseIsAttributedToItsPart) {
    linked_project(root / "desk");
    write_part(root / "desk/parts/book", "Book", "int book(int x) { return x; }");
    const auto report = integrate(root / "desk");
    EXPECT_EQ(report.status, cxxprobe::judge::Status::Fail);
    EXPECT_EQ(report.passed, 1);
    const auto failed = std::ranges::find_if(report.cases, [](const auto& c) { return c.failed; });
    ASSERT_NE(failed, report.cases.end());
    EXPECT_EQ(failed->parts, (std::vector<std::string>{"2"}));
    const auto json = cxxprobe::judge::to_json(report);
    EXPECT_EQ(json["status"], "FAIL");
    EXPECT_EQ(json["cases"][1]["parts"][0], "2");
}

TEST_F(ProjectPipelineTest, NoIntegrationEntryIsSkippedAndPartsStillJudge) {
    linked_project(root / "desk");
    fs::remove(root / "desk/integration/integration_gtest.cpp");
    const auto report = integrate(root / "desk");
    EXPECT_EQ(report.status, cxxprobe::judge::Status::Skipped);
    EXPECT_FALSE(report.link.ran);
    EXPECT_EQ(report.parts.size(), 2U);
    const auto part = cli({"test", "problem", "desk/ingest", "--json"});
    EXPECT_EQ(part.code, 0) << part.text;
}

TEST_F(ProjectPipelineTest, UncompilablePartIsReplacedByItsVisibleFallback) {
    linked_project(root / "desk", "integration:\n  fallback_dir: fallback\n");
    write_file(root / "desk/integration/fallback/2.cpp",
               "#include \"desk.hpp\"\nint book(int x) { return x + 5; }\n");
    const auto submission = root / "broken.cpp";
    write_file(submission, "this is not C++\n");
    const auto project = cxxprobe::project::load_from_dir(root / "desk");
    auto sources = cxxprobe::judge::reference_sources(project);
    sources[1] = {.label = "2", .source = submission, .origin = "submission"};
    const auto report = cxxprobe::judge::run_integration(project, {}, sources);
    EXPECT_EQ(report.status, cxxprobe::judge::Status::Pass) << report.error;
    EXPECT_EQ(report.parts[1].origin, "fallback");
    EXPECT_TRUE(report.parts[1].compiled);
    EXPECT_FALSE(report.parts[1].diagnostics.empty());

    fs::remove(root / "desk/integration/fallback/2.cpp");
    const auto without = cxxprobe::judge::run_integration(project, {}, sources);
    EXPECT_EQ(without.status, cxxprobe::judge::Status::Error);
    EXPECT_NE(without.error.find("part(s) '2' failed to compile"), std::string::npos)
        << without.error;
}

TEST(IntegrationAttribution, LabelsMustBeDelimited) {
    const std::vector<std::string> labels{"1", "12", "a_b"};
    using cxxprobe::judge::attributed_parts;
    EXPECT_EQ(attributed_parts("Integration", "Part1_Ingest", labels),
              (std::vector<std::string>{"1"}));
    EXPECT_EQ(attributed_parts("part_12", "Books", labels), (std::vector<std::string>{"12"}));
    EXPECT_EQ(attributed_parts("Integration", "Shared_a_b", labels),
              (std::vector<std::string>{"a_b"}));
    EXPECT_TRUE(attributed_parts("Integration", "Round123Trip", labels).empty());
    EXPECT_TRUE(attributed_parts("Integration", "RoundTrip", labels).empty());
}

// Each part's own behaviour check, so a wrong book() fails its per-part
// verdict too and a correct ingest() has a verdict of its own to keep.
void book_behavior(const fs::path& desk) {
    write_file(desk / "parts/ingest/checker/behavior_gtest.cpp",
               "#define main solution_main\n#include CXXPROBE_SOLUTION_FILE\n#undef main\n"
               "#include <gtest/gtest.h>\nTEST(Behavior, Ingest) { EXPECT_EQ(ingest(2), 4); }\n");
    write_file(desk / "parts/book/checker/behavior_gtest.cpp",
               "#define main solution_main\n#include CXXPROBE_SOLUTION_FILE\n#undef main\n"
               "#include <gtest/gtest.h>\nTEST(Behavior, Book) { EXPECT_EQ(book(1), 6); }\n");
}

nlohmann::json parsed(const Result& result) {
    try {
        return nlohmann::json::parse(result.text);
    } catch (const std::exception&) {
        ADD_FAILURE() << result.text;
        return {};
    }
}

TEST_F(ProjectPipelineTest, TestProjectPassesOnACorrectProject) {
    linked_project(root / "desk");
    book_behavior(root / "desk");
    const auto result = cli({"test", "project", "desk", "--json"});
    ASSERT_EQ(result.code, 0) << result.text;
    const auto report = parsed(result);
    EXPECT_EQ(report["overall"], "PASS");
    EXPECT_EQ(report["integration"]["status"], "PASS");
    EXPECT_EQ(report["parts"][1]["status"], "PASS");
    EXPECT_EQ(report["parts"][1]["origin"], "reference");
    EXPECT_TRUE(report["substitutions"].empty());

    const auto human = cli({"test", "project", "desk", "--no-color"});
    EXPECT_EQ(human.code, 0) << human.text;
    EXPECT_NE(human.text.find("Integration     PASS (3/3)"), std::string::npos) << human.text;
}

TEST_F(ProjectPipelineTest, BrokenPartFailsByNameWhileOthersKeepTheirVerdicts) {
    linked_project(root / "desk");
    book_behavior(root / "desk");
    write_file(root / "wrong.cpp",
               "#include \"desk.hpp\"\nint book(int x) { return x; }\nint main() { return 0; }\n");
    const auto result = cli({"test", "project", "desk", "--submission", "2=wrong.cpp", "--json"});
    EXPECT_EQ(result.code, 1) << result.text;
    const auto report = parsed(result);
    EXPECT_EQ(report["overall"], "FAIL");
    EXPECT_EQ(report["parts"][0]["status"], "PASS");
    EXPECT_EQ(report["parts"][1]["status"], "FAIL");
    EXPECT_EQ(report["parts"][1]["origin"], "submission");
    EXPECT_EQ(report["integration"]["status"], "FAIL");
    for (const auto& c : report["integration"]["cases"]) {
        if (c["failed"] == true && !c["parts"].empty()) {
            EXPECT_EQ(c["parts"][0], "2") << c.dump();
        }
    }
    ASSERT_EQ(report["substitutions"].size(), 1U);
    EXPECT_EQ(report["substitutions"][0]["label"], "1");
    EXPECT_EQ(report["substitutions"][0]["origin"], "reference");
}

TEST_F(ProjectPipelineTest, NoSuppliedPartsEqualsTheBareForm) {
    linked_project(root / "desk");
    const auto bare = parsed(cli({"test", "project", "desk", "--json"}));
    const auto named = parsed(cli({"test", "project", "desk", "--json", "--fallback", "1=reference",
                                   "--fallback", "2=reference"}));
    EXPECT_EQ(bare["overall"], named["overall"]);
    EXPECT_EQ(bare["integration"]["status"], named["integration"]["status"]);
    EXPECT_EQ(bare["substitutions"], named["substitutions"]);
    for (std::size_t i = 0; i < 2; ++i) {
        EXPECT_EQ(bare["parts"][i]["origin"], named["parts"][i]["origin"]);
        EXPECT_EQ(bare["parts"][i]["source"], named["parts"][i]["source"]);
    }
}

TEST_F(ProjectPipelineTest, EmptyFallbackProvesTheIntegrationNeedsThePart) {
    linked_project(root / "desk");
    const auto result = cli({"test", "project", "desk", "--fallback", "2=empty", "--json"});
    EXPECT_EQ(result.code, 2) << result.text;
    const auto report = parsed(result);
    EXPECT_EQ(report["parts"][1]["status"], "SKIPPED");
    EXPECT_EQ(report["parts"][1]["origin"], "empty");
    EXPECT_EQ(report["integration"]["status"], "ERROR");
    EXPECT_NE(report["integration"]["error"].get<std::string>().find("nothing linked"),
              std::string::npos)
        << result.text;
}

TEST_F(ProjectPipelineTest, UnknownOrRepeatedLabelsAreRejected) {
    linked_project(root / "desk");
    write_file(root / "x.cpp", "int main() { return 0; }\n");
    const auto unknown = cli({"test", "project", "desk", "--submission", "9=x.cpp"});
    EXPECT_EQ(unknown.code, 2);
    EXPECT_NE(unknown.text.find("unknown part label '9' (declared: 1, 2)"), std::string::npos)
        << unknown.text;
    const auto twice =
        cli({"test", "project", "desk", "--submission", "1=x.cpp", "--submission", "1=x.cpp"});
    EXPECT_EQ(twice.code, 2);
    EXPECT_NE(twice.text.find("named twice"), std::string::npos) << twice.text;
    const auto bad = cli({"test", "project", "desk", "--fallback", "1=maybe"});
    EXPECT_EQ(bad.code, 2);
    EXPECT_NE(bad.text.find("expected reference or empty"), std::string::npos) << bad.text;
    const auto part = cli({"test", "project", "desk/ingest"});
    EXPECT_EQ(part.code, 2);
}

TEST_F(ProjectPipelineTest, FallbackSubstitutionIsListed) {
    linked_project(root / "desk", "integration:\n  fallback_dir: fallback\n");
    write_file(root / "desk/integration/fallback/2.cpp",
               "#include \"desk.hpp\"\nint book(int x) { return x + 5; }\n");
    book_behavior(root / "desk");
    write_file(root / "broken.cpp", "this is not C++\n");
    const auto result = cli({"test", "project", "desk", "--submission", "2=broken.cpp", "--json"});
    EXPECT_EQ(result.code, 2) << result.text;
    const auto report = parsed(result);
    EXPECT_EQ(report["parts"][1]["status"], "ERROR");
    EXPECT_EQ(report["integration"]["status"], "PASS");
    EXPECT_EQ(report["integration"]["parts"][1]["origin"], "fallback");
    bool listed = false;
    for (const auto& sub : report["substitutions"]) {
        listed = listed || (sub["label"] == "2" && sub["origin"] == "fallback" &&
                            sub["reason"] == "submission did not compile");
    }
    EXPECT_TRUE(listed) << report["substitutions"].dump();
}

TEST_F(ProjectPipelineTest, ScaffoldedProjectPasses) {
    const auto init = cli({"package", "init", "Order Desk", "--project", "--parts", "3"});
    ASSERT_EQ(init.code, 0) << init.text;
    for (const auto* file :
         {"order-desk/project.yaml", "order-desk/shared/contract.hpp",
          "order-desk/integration/integration_gtest.cpp", "order-desk/parts/part-3/problem.yaml"}) {
        EXPECT_TRUE(fs::exists(root / file)) << file;
    }
    const auto result = cli({"test", "project", "order-desk", "--json"});
    EXPECT_EQ(result.code, 0) << result.text;
    const auto report = parsed(result);
    EXPECT_EQ(report["parts"].size(), 3U);
    EXPECT_EQ(report["integration"]["status"], "PASS");
    const auto again = cli({"package", "init", "Order Desk", "--project"});
    EXPECT_EQ(again.code, 2);
}

}  // namespace
