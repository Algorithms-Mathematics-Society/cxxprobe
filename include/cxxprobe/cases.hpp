#pragma once

#include <cstdint>
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

enum class Verdict : std::uint8_t { AC, WA, TLE, MLE, OLE, RE };

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

// Generous defaults for running a checker binary — setter-authored tooling,
// not a submission, so this is deliberately decoupled from the problem's
// contestant-facing limits (a checker shouldn't false-positive TLE against
// a tight submission time limit): 512 MiB, 10s CPU, 15s wall, 16 PIDs.
cxxprobe::sandbox::Limits default_checker_limits();

struct CheckerOutcome {
    bool ac{false};
    std::string diagnostics;  // the checker's stderr, if any
};

// Runs a testlib-ABI checker binary (checker <input> <output> <answer>,
// exit 0 = AC) through cxxprobe::sandbox::run() — the checker is sandboxed
// and resource-limited exactly like compile/solution execution already
// are, and its stderr is captured rather than inherited straight to the
// terminal. If checker_bin is empty, falls back to token_equal against
// result.stdout_data (diagnostics stays empty in that case).
CheckerOutcome check_output(
    const std::string& checker_bin, const std::string& input_data,
    const cxxprobe::sandbox::Result& result, const std::string& answer_data,
    const cxxprobe::sandbox::Limits& checker_limits = default_checker_limits());

// Verdict priority when multiple conditions trigger: TLE > MLE > OLE > RE > WA/AC.
Verdict compute_verdict(const cxxprobe::sandbox::Result& result,
                        const cxxprobe::sandbox::Limits& limits, bool checker_ac);

}  // namespace cxxprobe::cases
