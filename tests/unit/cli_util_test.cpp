#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <string>

#include "../../cli/common/text_utils.hpp"
#include "cxxprobe/cases.hpp"

using ms = std::chrono::milliseconds;
using cxxprobe::cases::token_equal;
using cxxprobe::cli::parse_duration;

// ─── parse_duration tests ─────────────────────────────────────────────────────

TEST(ParseDuration, RawMs) {
    EXPECT_EQ(parse_duration("2000"), ms{2000});
    EXPECT_EQ(parse_duration("0"), ms{0});
    EXPECT_EQ(parse_duration("1"), ms{1});
}

TEST(ParseDuration, SuffixMs) {
    EXPECT_EQ(parse_duration("500ms"), ms{500});
    EXPECT_EQ(parse_duration("2000ms"), ms{2000});
    EXPECT_EQ(parse_duration("0ms"), ms{0});
}

TEST(ParseDuration, SuffixS) {
    EXPECT_EQ(parse_duration("1s"), ms{1000});
    EXPECT_EQ(parse_duration("2s"), ms{2000});
    EXPECT_EQ(parse_duration("10s"), ms{10000});
}

TEST(ParseDuration, InvalidThrows) {
    EXPECT_THROW(parse_duration(""), std::invalid_argument);
    EXPECT_THROW(parse_duration("abc"), std::invalid_argument);
    EXPECT_THROW(parse_duration("1.5s"), std::invalid_argument);
    EXPECT_THROW(parse_duration("-1"), std::invalid_argument);
}

TEST(ParseDuration, MsSuffixPriority) {
    // "1000ms" must not be treated as "1000m" + "s"
    EXPECT_EQ(parse_duration("1000ms"), ms{1000});
}

// ─── token_equal tests ────────────────────────────────────────────────────────

TEST(TokenEqual, ExactMatch) { EXPECT_TRUE(token_equal("hello world", "hello world")); }

TEST(TokenEqual, TrailingNewline) {
    EXPECT_TRUE(token_equal("42\n", "42"));
    EXPECT_TRUE(token_equal("42", "42\n"));
}

TEST(TokenEqual, ExtraSpaces) {
    EXPECT_TRUE(token_equal("1 2 3", "1  2   3"));
    EXPECT_TRUE(token_equal("  1 2 3  ", "1 2 3"));
}

TEST(TokenEqual, MultilineVsSpaced) { EXPECT_TRUE(token_equal("1\n2\n3", "1 2 3")); }

TEST(TokenEqual, DifferentTokens) {
    EXPECT_FALSE(token_equal("1 2 3", "1 2 4"));
    EXPECT_FALSE(token_equal("abc", "abcd"));
}

TEST(TokenEqual, DifferentCount) {
    EXPECT_FALSE(token_equal("1 2 3", "1 2"));
    EXPECT_FALSE(token_equal("1 2", "1 2 3"));
}

TEST(TokenEqual, BothEmpty) {
    EXPECT_TRUE(token_equal("", ""));
    EXPECT_TRUE(token_equal("   ", "\n\t"));
}

TEST(TokenEqual, EmptyVsNonempty) {
    EXPECT_FALSE(token_equal("", "x"));
    EXPECT_FALSE(token_equal("x", ""));
}

TEST(TokenEqual, FloatTokens) {
    // tokens are strings — "1.0" != "1" even though numerically equal
    EXPECT_FALSE(token_equal("1.0", "1"));
    EXPECT_TRUE(token_equal("3.14 2.71", "3.14  2.71"));
}
