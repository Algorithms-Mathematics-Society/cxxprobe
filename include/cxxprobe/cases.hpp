#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cxxprobe/sandbox.hpp"

namespace cxxprobe::cases {

struct TestCase {
    std::string label;
    std::string input_data;
    std::optional<std::string> answer_data;
};

enum class Verdict { AC, WA, TLE, MLE, OLE, RE };

const char* verdict_str(Verdict v);

// Loads a directory of <label>.in / <label>.ans(.out) pairs, sorted
// numerically by label (so "10" sorts after "2"). A case without a matching
// answer file is still returned, just with answer_data unset.
std::vector<TestCase> load_cases_dir(const std::filesystem::path& dir);

// Loads a YAML/JSON manifest: an array of
// {input|input_data, answer|answer_data, label?}. Paths inside are resolved
// relative to manifest_path's parent directory.
std::vector<TestCase> load_cases_manifest(const std::filesystem::path& manifest_path);

// Dispatches to load_cases_manifest for .yaml/.yml/.json, else load_cases_dir.
std::vector<TestCase> load_cases(const std::filesystem::path& path);

// Default checker: whitespace-tokenized string equality.
bool token_equal(std::string_view a, std::string_view b);

// Runs a testlib-ABI checker binary (checker <input> <output> <answer>,
// exit 0 = AC). If checker_bin is empty, falls back to token_equal against
// result.stdout_data.
bool check_output(const std::string& checker_bin, const std::string& input_data,
                   const cxxprobe::sandbox::Result& result, const std::string& answer_data);

// Verdict priority when multiple conditions trigger: TLE > MLE > OLE > RE > WA/AC.
Verdict compute_verdict(const cxxprobe::sandbox::Result& result, const cxxprobe::sandbox::Limits& limits,
                        bool checker_ac);

}  // namespace cxxprobe::cases
