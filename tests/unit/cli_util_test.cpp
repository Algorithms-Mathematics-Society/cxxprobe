#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <string>

// Pull in the functions under test by compiling main.cpp into a helper
// translation unit — instead, we duplicate the small pure functions here so
// we don't link against the CLI entry point.  If the implementations in
// cli/main.cpp change, these tests must stay in sync.

using ms = std::chrono::milliseconds;

// ─── parse_duration (duplicated from cli/main.cpp) ───────────────────────────

static ms parse_duration(const std::string& raw) {
    std::string_view sv{raw};
    std::string_view num;
    bool as_seconds = false;

    if (sv.size() >= 3 && sv.ends_with("ms")) {
        num = sv.substr(0, sv.size() - 2);
    } else if (sv.size() >= 2 && sv.back() == 's') {
        num = sv.substr(0, sv.size() - 1);
        as_seconds = true;
    } else {
        num = sv;
    }

    // Reject empty, leading minus, decimal points — only pure digit strings.
    auto all_digits = [](std::string_view s) {
        return !s.empty() && std::all_of(s.begin(), s.end(),
                                         [](unsigned char c) { return std::isdigit(c) != 0; });
    };
    if (!all_digits(num)) {
        throw std::invalid_argument{
            std::format("invalid duration '{}' — use e.g. 2s, 500ms, 2000", raw)};
    }

    unsigned long val = std::stoul(std::string{num});
    if (as_seconds) {
        return std::chrono::duration_cast<ms>(std::chrono::seconds{val});
    }
    return ms{val};
}

// ─── token_equal (duplicated from cli/main.cpp) ──────────────────────────────

static bool token_equal(std::string_view a, std::string_view b) {
    auto tokenize = [](std::string_view s) {
        std::vector<std::string_view> toks;
        std::size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
                ++i;
            }
            if (i >= s.size()) {
                break;
            }
            std::size_t j = i;
            while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) {
                ++j;
            }
            toks.push_back(s.substr(i, j - i));
            i = j;
        }
        return toks;
    };
    return tokenize(a) == tokenize(b);
}

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
