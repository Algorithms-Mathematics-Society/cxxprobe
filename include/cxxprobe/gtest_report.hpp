#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cxxprobe::gtest_report {

struct CaseResult {
    std::string suite;
    std::string name;    // "<suite>.<name>" is GTest's canonical full name
    std::string status;  // "RUN" | "NOTRUN"
    std::string result;  // "COMPLETED" | "SKIPPED" | "SUPPRESSED"
    double time_seconds{0};
    bool failed{false};
    std::vector<std::string> failure_messages;
};

struct Report {
    int tests{0};
    int failures{0};
    int disabled{0};
    int errors{0};
    double time_seconds{0};
    std::vector<CaseResult> cases;  // flattened across all testsuites
};

// Parses the file/text produced by --gtest_output=json[:<path>].
// Throws std::runtime_error on a missing file or malformed JSON.
Report parse_file(const std::filesystem::path& json_path);
Report parse_string(std::string_view json_text);

[[nodiscard]] bool all_passed(const Report& r);

}  // namespace cxxprobe::gtest_report
