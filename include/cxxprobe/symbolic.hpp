#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "cxxprobe/problem.hpp"

namespace cxxprobe::symbolic {

// Strips // line comments, /* */ block comments, and the contents of
// "..."/'...' /raw string literals, replacing stripped bytes with spaces
// (not deleting them) so the output is the SAME LENGTH as the input,
// preserving byte/line offsets. Preprocessor directives and all other
// tokens are left untouched.
//
// Known limitation: does not perform phase-2 backslash-newline splicing —
// not worth the complexity for a keyword-usage check.
std::string strip_comments_and_literals(std::string_view source);

struct CheckOutcome {
    std::string pattern;
    bool regex{false};
    bool expect_present{true};  // true = must_include, false = must_not_include
    bool matched{false};        // did pattern/regex match the stripped source
    bool satisfied{false};      // matched == expect_present
    std::string message;
};

// expect_present: true for must_include, false for must_not_include.
// Throws std::runtime_error if check.regex is true and the pattern is invalid.
CheckOutcome evaluate(const cxxprobe::problem::SymbolicCheck& check, std::string_view stripped_source,
                       bool expect_present);

struct Report {
    bool passed{false};
    std::vector<CheckOutcome> outcomes;
};

// Reads source_file, strips it, evaluates every check in config.
// Throws std::runtime_error on unreadable file or invalid regex pattern.
Report run(const cxxprobe::problem::SymbolicConfig& config, const std::filesystem::path& source_file);

}  // namespace cxxprobe::symbolic
