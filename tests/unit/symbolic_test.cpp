#include "cxxprobe/symbolic.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

using cxxprobe::problem::SymbolicCheck;
using cxxprobe::problem::SymbolicConfig;
using cxxprobe::symbolic::evaluate;
using cxxprobe::symbolic::strip_comments_and_literals;

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
