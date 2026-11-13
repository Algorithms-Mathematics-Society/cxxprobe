#include "cxxprobe/symbolic.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "../../src/judge/candidate_files.hpp"

using cxxprobe::problem::SymbolicCheck;
using cxxprobe::problem::SymbolicConfig;
using cxxprobe::symbolic::evaluate;
using cxxprobe::symbolic::strip_comments_and_literals;

TEST(SymbolicBundle, UnterminatedCommentInHeaderCannotHideForbiddenUnit) {
    const auto root =
        std::filesystem::temp_directory_path() / "cxxprobe-symbolic-bundle-regression";
    std::filesystem::create_directories(root);
    const auto header = root / "a.h";
    const auto unit = root / "z.cpp";
    {
        std::ofstream out(header);
        out << "/*";
    }
    {
        std::ofstream out(unit);
        out << "std::vector<int> forbidden;\n";
    }
    SymbolicConfig config;
    config.enabled = true;
    config.must_not_include.push_back(SymbolicCheck{.pattern = "vector"});
    const auto result =
        cxxprobe::symbolic::run(config, std::vector<std::filesystem::path>{header, unit});
    EXPECT_FALSE(result.passed);
    std::filesystem::remove_all(root);
}

TEST(SymbolicBundle, PatternCannotSpanTwoSeparateFiles) {
    const auto root = std::filesystem::temp_directory_path() / "cxxprobe-symbolic-file-boundary";
    std::filesystem::create_directories(root);
    const auto first = root / "a.h";
    const auto second = root / "b.cpp";
    { std::ofstream(first) << "foo"; }
    { std::ofstream(second) << "bar"; }
    SymbolicConfig config;
    config.must_not_include.push_back(SymbolicCheck{.pattern = "foo\\s*bar", .regex = true});
    const auto result =
        cxxprobe::symbolic::run(config, std::vector<std::filesystem::path>{first, second});
    EXPECT_TRUE(result.passed);
    std::filesystem::remove_all(root);
}

TEST(CandidateFiles, ExplicitManifestKeepsUnitsAndHeadersSeparate) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "cxxprobe-candidate-manifest-regression";
    fs::create_directories(root);
    { std::ofstream(root / "main.cpp") << "int main(){return answer();}"; }
    { std::ofstream(root / "lib.cpp") << "int answer(){return 42;}"; }
    { std::ofstream(root / "lib.hpp") << "int answer();"; }
    {
        std::ofstream(root / "main.cpp.cxxprobe.json") << nlohmann::json{
            {"format", "cxxprobe-candidate-v1"},
            {"root", root.string()},
            {"units", {"main.cpp", "lib.cpp"}},
            {"files", {"main.cpp", "lib.cpp", "lib.hpp"}}}.dump();
    }
    const auto staged = cxxprobe::judge::detail::candidate_files(root / "main.cpp");
    EXPECT_TRUE(staged.bundled);
    EXPECT_EQ(staged.units.size(), 2);
    EXPECT_EQ(staged.files.size(), 3);
    fs::remove_all(root);
}

TEST(CandidateFiles, RefusesPathEscapeFromManifest) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "cxxprobe-candidate-escape-regression";
    fs::create_directories(root);
    { std::ofstream(root / "main.cpp") << "int main(){}"; }
    {
        std::ofstream(root / "main.cpp.cxxprobe.json") << nlohmann::json{
            {"format", "cxxprobe-candidate-v1"},
            {"root", root.string()},
            {"units", {"../escape.cpp"}},
            {"files", {"main.cpp", "../escape.cpp"}}}.dump();
    }
    EXPECT_THROW(cxxprobe::judge::detail::candidate_files(root / "main.cpp"), std::runtime_error);
    fs::remove_all(root);
}

TEST(CandidateFiles, CandidateHeadersPrecedePackagedReferenceHeaders) {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / "cxxprobe-candidate-include-precedence";
    const auto public_dir = root / "pack/attachments";
    fs::create_directories(public_dir);
    { std::ofstream(public_dir / ".cxxprobe-sanitized") << "readonly references only\n"; }
    const cxxprobe::judge::detail::CandidateFiles candidate{
        .root = root / "submission", .units = {}, .files = {}, .bundled = true};
    std::vector<std::string> flags{"-I" + (root / "pack/shared").string()};
    cxxprobe::judge::detail::prepend_candidate_includes(
        flags, candidate, root / "submission/nested/main.cpp", public_dir);
    EXPECT_EQ(flags[0], "-I" + (root / "submission/nested").string());
    EXPECT_EQ(flags[1], "-I" + (root / "submission").string());
    EXPECT_EQ(flags[2], "-I" + public_dir.string());
    EXPECT_EQ(flags[3], "-I" + (root / "pack/shared").string());
    fs::remove_all(root);
}

// ─── strip_comments_and_literals ───────────────────────────────────────────────

TEST(StripCommentsAndLiterals, PreservesLength) {
    std::string src = "int x; // comment\n/* block */ \"str\" 'c'\n";
    EXPECT_EQ(strip_comments_and_literals(src).size(), src.size());
}

TEST(StripCommentsAndLiterals, LineCommentBlanked) {
    std::string out = strip_comments_and_literals("int x; // memcpy here\nint y;");
    EXPECT_EQ(out.find("memcpy"), std::string::npos);
    EXPECT_NE(out.find("int x;"), std::string::npos);
    EXPECT_NE(out.find("int y;"), std::string::npos);
    // Newline is preserved so line offsets stay aligned.
    EXPECT_NE(out.find('\n'), std::string::npos);
}

TEST(StripCommentsAndLiterals, BlockCommentBlanked) {
    std::string out = strip_comments_and_literals("/* memcpy\nspans lines */ int x;");
    EXPECT_EQ(out.find("memcpy"), std::string::npos);
    EXPECT_EQ(out.find("spans"), std::string::npos);
    EXPECT_NE(out.find("int x;"), std::string::npos);
    // Embedded newline preserved.
    EXPECT_NE(out.find('\n'), std::string::npos);
}

TEST(StripCommentsAndLiterals, BlockCommentDoesNotNest) {
    // C++ block comments don't nest — first */ ends it, trailing " */" is code.
    std::string out = strip_comments_and_literals("/* outer /* inner */ code */");
    EXPECT_EQ(out.find("outer"), std::string::npos);
    EXPECT_EQ(out.find("inner"), std::string::npos);
    // The trailing " */" (after the first */) is left as ordinary code text.
    EXPECT_NE(out.find("code"), std::string::npos);
}

TEST(StripCommentsAndLiterals, StringLiteralContentBlanked) {
    std::string out = strip_comments_and_literals(R"(auto s = "memcpy inside string";)");
    EXPECT_EQ(out.find("memcpy"), std::string::npos);
    EXPECT_NE(out.find("auto s ="), std::string::npos);
}

TEST(StripCommentsAndLiterals, StringLiteralEscapedQuote) {
    std::string out = strip_comments_and_literals(R"(auto s = "a\"memcpy\"b"; int x;)");
    EXPECT_EQ(out.find("memcpy"), std::string::npos);
    EXPECT_NE(out.find("int x;"), std::string::npos);
}

TEST(StripCommentsAndLiterals, CharLiteralBlanked) {
    std::string out = strip_comments_and_literals("char c = 'x'; int y;");
    EXPECT_NE(out.find("int y;"), std::string::npos);
}

TEST(StripCommentsAndLiterals, DigitSeparatorNotTreatedAsCharLiteral) {
    std::string out = strip_comments_and_literals("long x = 1'000'000; int memcpy_count;");
    EXPECT_NE(out.find("1'000'000"), std::string::npos);
    EXPECT_NE(out.find("memcpy_count"), std::string::npos);
}

TEST(StripCommentsAndLiterals, RawStringLiteralBlanked) {
    std::string out =
        strip_comments_and_literals(R"CODE(auto s = R"(memcpy inside raw)"; int x;)CODE");
    EXPECT_EQ(out.find("memcpy"), std::string::npos);
    EXPECT_NE(out.find("int x;"), std::string::npos);
}

TEST(StripCommentsAndLiterals, RawStringLiteralCustomDelimiter) {
    std::string out = strip_comments_and_literals(
        R"CODE(auto s = R"DELIM(memcpy )" still raw)DELIM"; int x;)CODE");
    EXPECT_EQ(out.find("memcpy"), std::string::npos);
    EXPECT_EQ(out.find("still raw"), std::string::npos);
    EXPECT_NE(out.find("int x;"), std::string::npos);
}

TEST(StripCommentsAndLiterals, CommentMarkerInsideStringNotTreatedAsComment) {
    // The "//" inside the string literal must not start a line comment.
    std::string out = strip_comments_and_literals(R"(auto s = "http://example.com"; int keep;)");
    EXPECT_NE(out.find("int keep;"), std::string::npos);
}

TEST(StripCommentsAndLiterals, PreprocessorDirectivePreserved) {
    std::string out = strip_comments_and_literals("#include <cstring>\nint x;");
    EXPECT_NE(out.find("#include"), std::string::npos);
    EXPECT_NE(out.find("cstring"), std::string::npos);
}

// ─── evaluate ───────────────────────────────────────────────────────────────

TEST(SymbolicEvaluate, MustIncludeLiteralHit) {
    SymbolicCheck check{.pattern = "std::bit_cast", .regex = false, .message = ""};
    auto outcome = evaluate(check, "auto x = std::bit_cast<int>(y);", true);
    EXPECT_TRUE(outcome.matched);
    EXPECT_TRUE(outcome.satisfied);
}

TEST(SymbolicEvaluate, MustIncludeLiteralMiss) {
    SymbolicCheck check{.pattern = "std::bit_cast", .regex = false, .message = ""};
    auto outcome = evaluate(check, "auto x = memcpy(a, b, 4);", true);
    EXPECT_FALSE(outcome.matched);
    EXPECT_FALSE(outcome.satisfied);
}

TEST(SymbolicEvaluate, MustNotIncludeHitFails) {
    SymbolicCheck check{.pattern = "memcpy", .regex = false, .message = ""};
    auto outcome = evaluate(check, "memcpy(a, b, 4);", false);
    EXPECT_TRUE(outcome.matched);
    EXPECT_FALSE(outcome.satisfied);  // matched but expect_present=false -> not satisfied
}

TEST(SymbolicEvaluate, MustNotIncludeMissSucceeds) {
    SymbolicCheck check{.pattern = "memcpy", .regex = false, .message = ""};
    auto outcome = evaluate(check, "std::bit_cast<int>(y);", false);
    EXPECT_FALSE(outcome.matched);
    EXPECT_TRUE(outcome.satisfied);
}

TEST(SymbolicEvaluate, RegexMatch) {
    SymbolicCheck check{.pattern = R"(\bmemcpy\s*\()", .regex = true, .message = ""};
    EXPECT_TRUE(evaluate(check, "memcpy(a, b, 4);", false).matched);
    EXPECT_FALSE(evaluate(check, "not_memcpy(a);", false).matched);
}

TEST(SymbolicEvaluate, InvalidRegexThrows) {
    SymbolicCheck check{.pattern = "(unclosed", .regex = true, .message = ""};
    EXPECT_THROW(evaluate(check, "anything", true), std::runtime_error);
}

// ─── run (full file-based flow) ────────────────────────────────────────────

TEST(SymbolicRun, AggregatesAllChecks) {
    std::filesystem::path tmp =
        std::filesystem::temp_directory_path() / "cxxprobe-symbolic-test.cpp";
    {
        std::ofstream ofs{tmp};
        ofs << "int main() { int x = 1; return x; }\n";
    }

    SymbolicConfig config;
    config.must_include.push_back({.pattern = "return x", .regex = false, .message = ""});
    config.must_include.push_back(
        {.pattern = "std::bit_cast", .regex = false, .message = "missing"});
    config.must_not_include.push_back({.pattern = "memcpy", .regex = false, .message = ""});

    auto report = cxxprobe::symbolic::run(config, tmp);
    EXPECT_FALSE(report.passed);  // second must_include is unmet
    ASSERT_EQ(report.outcomes.size(), 3U);
    EXPECT_TRUE(report.outcomes[0].satisfied);
    EXPECT_FALSE(report.outcomes[1].satisfied);
    EXPECT_TRUE(report.outcomes[2].satisfied);

    std::filesystem::remove(tmp);
}

TEST(SymbolicRun, MissingFileThrows) {
    SymbolicConfig config;
    config.must_include.push_back({.pattern = "x", .regex = false, .message = ""});
    EXPECT_THROW(cxxprobe::symbolic::run(config, "/nonexistent/path/solution.cpp"),
                 std::runtime_error);
}
