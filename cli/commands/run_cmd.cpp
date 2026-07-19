#include "run_cmd.hpp"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "../common/color.hpp"
#include "../common/json_io.hpp"
#include "../common/text_utils.hpp"
#include "cxxprobe/cases.hpp"
#include "cxxprobe/sandbox.hpp"

namespace cxxprobe::cli {

namespace {

namespace fs = std::filesystem;
using ms = std::chrono::milliseconds;
using cxxprobe::cases::TestCase;
using cxxprobe::cases::Verdict;
using cxxprobe::sandbox::Limits;
using cxxprobe::sandbox::Result;

constexpr std::size_t kMaxStdinBytes = 4ULL * 1024ULL * 1024ULL;
constexpr double kMiBd = 1024.0 * 1024.0;

const char* verdict_col(Verdict v, const Col& col) {
    switch (v) {
        case Verdict::AC:
            return col.grn;
        case Verdict::WA:
        case Verdict::RE:
            return col.red;
        case Verdict::TLE:
        case Verdict::MLE:
        case Verdict::OLE:
            return col.yel;
    }
    return col.rst;
}

// path="-" reads from real stdin (pipe), bounded at kMaxStdinBytes.
std::string load_stdin(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::istream* src = nullptr;
    std::ifstream fstr;
    if (path == "-") {
        src = &std::cin;
    } else {
        fstr.open(path, std::ios::binary);
        if (!fstr) {
            throw std::runtime_error{std::format("cannot open input file: {}", path)};
        }
        src = &fstr;
    }
    std::string buf;
    std::array<char, 4096> tmp{};
    while (buf.size() < kMaxStdinBytes) {
        src->read(tmp.data(), static_cast<std::streamsize>(tmp.size()));
        auto got = static_cast<std::size_t>(src->gcount());
        if (got == 0) {
            break;
        }
        buf.append(tmp.data(), std::min(got, kMaxStdinBytes - buf.size()));
    }
    return buf;
}

void print_human(const Result& res, const std::optional<Verdict>& verdict, bool quiet,
                 const Col& col) {
    std::cout << res.stdout_data;
    std::cerr << res.stderr_data;
    if (!quiet) {
        std::string mem_str =
            res.peak_memory_bytes > 0
                ? std::format("{:.1f}MiB", static_cast<double>(res.peak_memory_bytes) / kMiBd)
                : "--";
        std::string vpart;
        if (verdict) {
            vpart = std::format(" | {}{}{}", verdict_col(*verdict, col),
                                cxxprobe::cases::verdict_str(*verdict), col.rst);
        }
        std::cerr << std::format("{}[exit:{} | cpu:{}ms | wall:{}ms | mem:{}{}]{}\n", col.bold,
                                 res.exit_code, res.cpu_time.count(), res.wall_time.count(),
                                 mem_str, vpart, col.rst);
    }
}

int single_exit_code(const Result& res, const std::optional<Verdict>& verdict) {
    if (verdict) {
        return *verdict == Verdict::AC ? 0 : 1;
    }
    if (res.exit_code < 0) {
        return 128 + (-res.exit_code);
    }
    return std::min(res.exit_code, 125);
}

void print_case_line(int index, const std::string& label, const std::optional<Verdict>& verdict,
                     const std::optional<Result>& result, const std::optional<std::string>& error,
                     const Col& col) {
    (void)index;
    if (error) {
        std::cout << std::format("{:>6}: {}ERR{} ({})\n", label, col.red, col.rst, *error);
        return;
    }
    const Result& r = *result;
    const char* vcol = verdict ? verdict_col(*verdict, col) : col.cyn;
    const char* vstr = verdict ? cxxprobe::cases::verdict_str(*verdict) : "---";
    std::string mem =
        r.peak_memory_bytes > 0
            ? std::format("{:.1f}MiB", static_cast<double>(r.peak_memory_bytes) / kMiBd)
            : "--";
    std::cout << std::format("{:>6}: {}{:<3}{}   cpu:{:>7}ms   wall:{:>7}ms   mem:{:>9}\n", label,
                             vcol, vstr, col.rst, r.cpu_time.count(), r.wall_time.count(), mem);
}

int run_batch(const std::vector<std::string>& argv, const std::string& cases_path,
              const std::string& checker_bin, const Limits& limits, bool json_output,
              const Col& col) {
    std::vector<TestCase> test_cases;
    try {
        test_cases = cxxprobe::cases::load_cases(cases_path);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    Json json_results = Json::array();
    int passed = 0;
    bool any_verdict = false;
    bool any_sandbox_error = false;
    int index = 0;

    for (auto& tc : test_cases) {
        ++index;
        std::optional<Result> result;
        std::optional<std::string> error;
        std::optional<Verdict> verdict;

        try {
            result = cxxprobe::sandbox::run(argv, tc.input_data, limits);
            if (tc.answer_data) {
                bool ok = cxxprobe::cases::check_output(checker_bin, tc.input_data, *result,
                                                        *tc.answer_data);
                verdict = cxxprobe::cases::compute_verdict(*result, limits, ok);
                any_verdict = true;
                if (*verdict == Verdict::AC) {
                    ++passed;
                }
            }
        } catch (const std::exception& ex) {
            error = ex.what();
            any_sandbox_error = true;
        }

        if (!json_output) {
            print_case_line(index, tc.label, verdict, result, error, col);
        } else {
            Json j;
            j["index"] = index;
            j["label"] = tc.label;
            if (error) {
                j["error"] = *error;
            } else {
                if (verdict) {
                    j["verdict"] = cxxprobe::cases::verdict_str(*verdict);
                }
                j["exit_code"] = result->exit_code;
                j["cpu_time_ms"] = result->cpu_time.count();
                j["wall_time_ms"] = result->wall_time.count();
                j["peak_memory_bytes"] = result->peak_memory_bytes;
            }
            json_results.push_back(std::move(j));
        }
    }

    if (json_output) {
        Json out;
        out["results"] = std::move(json_results);
        out["summary"] = {{"passed", passed}, {"total", static_cast<int>(test_cases.size())}};
        std::cout << out.dump(2) << "\n";
    } else {
        std::cout << "---\n";
        if (any_verdict) {
            std::cout << std::format(
                "{}{}/{} passed{}\n",
                passed == static_cast<int>(test_cases.size()) ? col.grn : col.red, passed,
                test_cases.size(), col.rst);
        } else {
            std::cout << std::format("{} case(s) run (no expected output to judge)\n",
                                     test_cases.size());
        }
    }

    if (any_sandbox_error) {
        return 2;
    }
    return (any_verdict && passed != static_cast<int>(test_cases.size())) ? 1 : 0;
}

int run_single(const std::vector<std::string>& argv, const std::string& input_file,
               const std::string& expected_file, const std::string& checker_bin,
               const Limits& limits, bool json_output, bool quiet, const Col& col) {
    std::string stdin_data;
    try {
        stdin_data = load_stdin(input_file);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    Result res;
    try {
        res = cxxprobe::sandbox::run(argv, std::move(stdin_data), limits);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: sandbox error: " << ex.what() << "\n";
        return 2;
    }

    std::optional<Verdict> verdict;
    if (!expected_file.empty()) {
        std::string answer;
        try {
            std::ifstream ifs{expected_file, std::ios::binary};
            if (!ifs) {
                throw std::runtime_error{std::format("cannot open: {}", expected_file)};
            }
            answer = {std::istreambuf_iterator<char>{ifs}, {}};
        } catch (const std::exception& ex) {
            std::cerr << "cxxprobe: " << ex.what() << "\n";
            return 2;
        }
        std::string in_for_checker;
        if (input_file != "-") {
            try {
                in_for_checker = load_stdin(input_file);
            } catch (...) {
            }
        }
        bool ok = cxxprobe::cases::check_output(checker_bin, in_for_checker, res, answer);
        verdict = cxxprobe::cases::compute_verdict(res, limits, ok);
    }

    if (json_output) {
        std::cout << result_to_json(res, verdict).dump(2) << "\n";
    } else {
        print_human(res, verdict, quiet, col);
    }
    return single_exit_code(res, verdict);
}

}  // namespace

RunCommand::RunCommand(CLI::App& parent) {
    app_ = parent.add_subcommand("run", "Run a program in the sandbox (single run or batch)");

    app_->add_option("-m,--memory-mb", memory_mib_, "Memory limit in MiB [default: 256]");
    app_->add_option("-t,--cpu,--cpu-ms", cpu_str_,
                     "CPU time limit: 2s / 500ms / 2000 [default: 5000ms]");
    app_->add_option("-w,--wall,--wall-ms", wall_str_,
                     "Wall-clock limit: 2s / 500ms / 10000 [default: 10000ms]");
    app_->add_option("-p,--pids", max_pids_, "Maximum PIDs in sandbox [default: 64]");

    app_->add_option("-i,--input", input_file_,
                     "File to use as stdin; use - to read from pipe [default: empty]");

    app_->add_option("-e,--expected", expected_file_,
                     "Expected output file — enables verdict judgment");
    app_->add_option("--checker", checker_bin_,
                     "Custom checker binary (testlib ABI: checker <in> <out> <ans>)")
        ->check(CLI::ExistingFile);

    app_->add_option("--cases", cases_path_,
                     "Batch mode: directory of *.in/*.ans pairs or YAML/JSON manifest");

    app_->add_flag("--json", json_output_, "Emit result(s) as JSON");
    app_->add_flag("-q,--quiet", quiet_, "Suppress metadata summary (human mode)");
    app_->add_flag("--no-color", no_color_, "Disable ANSI color output");

    app_->add_option("program", program_args_, "Program to run, followed by its arguments")
        ->required()
        ->expected(1, -1)
        ->allow_extra_args();
}

int RunCommand::execute() {
    ms cpu_limit{};
    ms wall_limit{};
    try {
        cpu_limit = parse_duration(cpu_str_);
        wall_limit = parse_duration(wall_str_);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    if (!checker_bin_.empty() && expected_file_.empty() && cases_path_.empty()) {
        std::cerr << "cxxprobe: --checker requires --expected (single-run) or --cases (batch)\n";
        return 2;
    }

    const Col col = make_col(!no_color_ && (isatty(STDERR_FILENO) != 0));

    Limits limits;
    limits.memory_bytes = static_cast<std::size_t>(memory_mib_) * 1024UL * 1024UL;
    limits.cpu = cpu_limit;
    limits.wall = wall_limit;
    limits.max_pids = max_pids_;

    if (!cases_path_.empty()) {
        return run_batch(program_args_, cases_path_, checker_bin_, limits, json_output_, col);
    }
    return run_single(program_args_, input_file_, expected_file_, checker_bin_, limits,
                      json_output_, quiet_, col);
}

}  // namespace cxxprobe::cli
