// NOLINTBEGIN(misc-include-cleaner) — yaml-cpp/CLI11 internal headers
#include <yaml-cpp/yaml.h>

#include <CLI/CLI.hpp>
// NOLINTEND(misc-include-cleaner)

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cxxprobe/sandbox.hpp"

namespace {

namespace fs = std::filesystem;
using ms = std::chrono::milliseconds;
using cxxprobe::sandbox::Limits;
using cxxprobe::sandbox::Result;

// ─── Constants ────────────────────────────────────────────────────────────────

constexpr std::size_t kMaxOutputBytes = 4ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaxStdinBytes = 4ULL * 1024ULL * 1024ULL;
constexpr double kMiBd = 1024.0 * 1024.0;

// ─── Color support ────────────────────────────────────────────────────────────

struct Col {
    const char* grn{""};
    const char* red{""};
    const char* yel{""};
    const char* cyn{""};
    const char* bold{""};
    const char* rst{""};
};

Col make_col(bool enable) {
    if (!enable) {
        return {};
    }
    // NOLINTBEGIN(readability-magic-numbers)
    return {"\033[32m", "\033[31m", "\033[33m", "\033[36m", "\033[1m", "\033[0m"};
    // NOLINTEND(readability-magic-numbers)
}

// ─── Duration parser ──────────────────────────────────────────────────────────
// Accepts: "2s", "500ms", "2000" (raw = ms).

ms parse_duration(const std::string& raw) {
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

    // Only pure digit strings accepted — reject leading minus, decimals, etc.
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

// ─── Stdin loader ─────────────────────────────────────────────────────────────
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

// ─── JSON string helper ───────────────────────────────────────────────────────

std::string json_str(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char ch : s) {
        switch (ch) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (ch < 0x20U) {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(ch));
                } else {
                    out += static_cast<char>(ch);
                }
        }
    }
    out += '"';
    return out;
}

// ─── Verdict ─────────────────────────────────────────────────────────────────

enum class Verdict { AC, WA, TLE, MLE, OLE, RE };

const char* verdict_str(Verdict v) {
    switch (v) {
        case Verdict::AC:
            return "AC";
        case Verdict::WA:
            return "WA";
        case Verdict::TLE:
            return "TLE";
        case Verdict::MLE:
            return "MLE";
        case Verdict::OLE:
            return "OLE";
        case Verdict::RE:
            return "RE";
    }
    std::unreachable();
}

const char* verdict_col(Verdict v, const Col& col) {
    switch (v) {
        case Verdict::AC:
            return col.grn;
        case Verdict::WA:
            return col.red;
        case Verdict::RE:
            return col.red;
        case Verdict::TLE:
            return col.yel;
        case Verdict::MLE:
            return col.yel;
        case Verdict::OLE:
            return col.yel;
    }
    std::unreachable();
}

// Priority: TLE > MLE > OLE > RE > WA/AC.
Verdict compute_verdict(const Result& r, const Limits& lim, bool checker_ac) {
    if (r.wall_timed_out || r.cpu_time >= lim.cpu) {
        return Verdict::TLE;
    }
    if (lim.memory_bytes > 0 && r.peak_memory_bytes >= lim.memory_bytes) {
        return Verdict::MLE;
    }
    if (r.stdout_data.size() >= kMaxOutputBytes) {
        return Verdict::OLE;
    }
    if (r.exit_code != 0) {
        return Verdict::RE;
    }
    return checker_ac ? Verdict::AC : Verdict::WA;
}

// ─── Token equality (default checker) ────────────────────────────────────────

bool token_equal(std::string_view a, std::string_view b) {
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

// ─── RAII temp file ───────────────────────────────────────────────────────────

struct TempFile {
    std::string path;

    explicit TempFile(std::string_view content = {}) {
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        char tmpl[] = "/tmp/cxxprobe-XXXXXX";
        int fd = ::mkstemp(tmpl);
        if (fd < 0) {
            throw std::runtime_error{std::format("mkstemp: {}", std::strerror(errno))};
        }
        path = tmpl;
        if (!content.empty()) {
            ssize_t written = ::write(fd, content.data(), content.size());
            if (written < 0 || static_cast<std::size_t>(written) != content.size()) {
                ::close(fd);
                ::unlink(path.c_str());
                throw std::runtime_error{"TempFile: write failed"};
            }
        }
        ::close(fd);
    }

    ~TempFile() {
        if (!path.empty()) {
            ::unlink(path.c_str());
        }
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&& other) noexcept : path{std::exchange(other.path, {})} {}
    TempFile& operator=(TempFile&&) = delete;
};

// ─── Custom checker runner (testlib ABI) ──────────────────────────────────────
// Calls: checker <input_file> <output_file> <answer_file>
// Exit 0 = AC, non-zero = WA. Checker stderr passes through to our stderr.

bool run_checker(const std::string& checker_bin, const std::string& input_path,
                 const std::string& output_path, const std::string& answer_path) {
    pid_t pid = ::fork();
    if (pid < 0) {
        throw std::runtime_error{std::format("fork: {}", std::strerror(errno))};
    }
    if (pid == 0) {
        // Redirect checker stdout to /dev/null; stderr passes through.
        int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDOUT_FILENO);
            ::close(devnull);
        }
        std::array<const char*, 5> exec_argv{
            checker_bin.c_str(),
            input_path.c_str(),
            output_path.c_str(),
            answer_path.c_str(),
            nullptr,
        };
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
        ::execv(checker_bin.c_str(), const_cast<char* const*>(exec_argv.data()));
        ::_exit(127);
    }
    int wstatus = 0;
    ::waitpid(pid, &wstatus, 0);
    return WIFEXITED(wstatus) && WEXITSTATUS(wstatus) == 0;
}

// Runs the configured checker (or built-in token equality) against a result.
// Returns true = AC.
bool check_output(const std::string& checker_bin, const std::string& input_data, const Result& res,
                  const std::string& answer_data) {
    if (checker_bin.empty()) {
        return token_equal(res.stdout_data, answer_data);
    }
    TempFile input_tmp{input_data};
    TempFile output_tmp{res.stdout_data};
    TempFile answer_tmp{answer_data};
    return run_checker(checker_bin, input_tmp.path, output_tmp.path, answer_tmp.path);
}

// ─── Single-run output ────────────────────────────────────────────────────────

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
            vpart = std::format(" | {}{}{}", verdict_col(*verdict, col), verdict_str(*verdict),
                                col.rst);
        }
        std::cerr << std::format("{}[exit:{} | cpu:{}ms | wall:{}ms | mem:{}{}]{}\n", col.bold,
                                 res.exit_code, res.cpu_time.count(), res.wall_time.count(),
                                 mem_str, vpart, col.rst);
    }
}

void print_json(const Result& res, const std::optional<Verdict>& verdict) {
    std::cout << "{\n"
              << "  \"exit_code\": " << res.exit_code << ",\n"
              << "  \"peak_memory_bytes\": " << res.peak_memory_bytes << ",\n"
              << "  \"cpu_time_ms\": " << res.cpu_time.count() << ",\n"
              << "  \"wall_time_ms\": " << res.wall_time.count() << ",\n";
    if (verdict) {
        std::cout << "  \"verdict\": \"" << verdict_str(*verdict) << "\",\n";
    }
    std::cout << "  \"stdout\": " << json_str(res.stdout_data) << ",\n"
              << "  \"stderr\": " << json_str(res.stderr_data) << "\n"
              << "}\n";
}

// ─── Exit code helpers ────────────────────────────────────────────────────────

int single_exit_code(const Result& res, const std::optional<Verdict>& verdict) {
    if (verdict) {
        return *verdict == Verdict::AC ? 0 : 1;
    }
    if (res.exit_code < 0) {
        // killed by signal — return 128+signal
        return 128 + (-res.exit_code);
    }
    return std::min(res.exit_code, 125);
}

// ─── Batch case loading ───────────────────────────────────────────────────────

struct TestCase {
    std::string label;
    std::string input_data;
    std::optional<std::string> answer_data;
};

// Natural sort: numeric runs compared by value, not lexicographically.
bool natural_less(const std::string& a, const std::string& b) {
    std::size_t ia = 0;
    std::size_t ib = 0;
    while (ia < a.size() && ib < b.size()) {
        bool da = std::isdigit(static_cast<unsigned char>(a[ia])) != 0;
        bool db = std::isdigit(static_cast<unsigned char>(b[ib])) != 0;
        if (da && db) {
            std::size_t ea = ia;
            std::size_t eb = ib;
            while (ea < a.size() && std::isdigit(static_cast<unsigned char>(a[ea]))) {
                ++ea;
            }
            while (eb < b.size() && std::isdigit(static_cast<unsigned char>(b[eb]))) {
                ++eb;
            }
            std::string_view na{a.data() + ia, ea - ia};
            std::string_view nb{b.data() + ib, eb - ib};
            if (na.size() != nb.size()) {
                return na.size() < nb.size();
            }
            if (na != nb) {
                return na < nb;
            }
            ia = ea;
            ib = eb;
        } else {
            if (a[ia] != b[ib]) {
                return a[ia] < b[ib];
            }
            ++ia;
            ++ib;
        }
    }
    return a.size() < b.size();
}

static std::string read_file(const fs::path& p) {
    std::ifstream ifs{p, std::ios::binary};
    if (!ifs) {
        throw std::runtime_error{std::format("cannot open: {}", p.string())};
    }
    return {std::istreambuf_iterator<char>{ifs}, {}};
}

std::vector<TestCase> load_cases_dir(const fs::path& dir) {
    if (!fs::is_directory(dir)) {
        throw std::runtime_error{std::format("not a directory: {}", dir.string())};
    }
    std::vector<std::pair<std::string, fs::path>> in_files;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() == ".in") {
            in_files.emplace_back(entry.path().stem().string(), entry.path());
        }
    }
    std::sort(in_files.begin(), in_files.end(),
              [](const auto& x, const auto& y) { return natural_less(x.first, y.first); });

    std::vector<TestCase> cases;
    cases.reserve(in_files.size());
    for (auto& [stem, in_path] : in_files) {
        TestCase tc;
        tc.label = stem;
        tc.input_data = read_file(in_path);
        for (const char* ext : {".ans", ".out"}) {
            fs::path ans_path = in_path;
            ans_path.replace_extension(ext);
            if (fs::exists(ans_path)) {
                tc.answer_data = read_file(ans_path);
                break;
            }
        }
        cases.push_back(std::move(tc));
    }
    return cases;
}

std::vector<TestCase> load_cases_manifest(const fs::path& manifest_path) {
    YAML::Node doc = YAML::LoadFile(manifest_path.string());
    if (!doc.IsSequence()) {
        throw std::runtime_error{"manifest must be a YAML/JSON array"};
    }
    const fs::path base = manifest_path.parent_path();
    std::vector<TestCase> cases;
    int idx = 1;
    for (const auto& node : doc) {
        TestCase tc;
        tc.label = node["label"] ? node["label"].as<std::string>() : std::to_string(idx);
        ++idx;

        if (node["input"]) {
            fs::path p{node["input"].as<std::string>()};
            if (p.is_relative()) {
                p = base / p;
            }
            tc.input_data = read_file(p);
        } else if (node["input_data"]) {
            tc.input_data = node["input_data"].as<std::string>();
        }

        if (node["answer"]) {
            fs::path p{node["answer"].as<std::string>()};
            if (p.is_relative()) {
                p = base / p;
            }
            tc.answer_data = read_file(p);
        } else if (node["answer_data"]) {
            tc.answer_data = node["answer_data"].as<std::string>();
        }

        cases.push_back(std::move(tc));
    }
    return cases;
}

std::vector<TestCase> load_cases(const std::string& path_str) {
    const fs::path p{path_str};
    const std::string ext = p.extension().string();
    if (ext == ".json" || ext == ".yaml" || ext == ".yml") {
        return load_cases_manifest(p);
    }
    return load_cases_dir(p);
}

// ─── Batch runner ─────────────────────────────────────────────────────────────

struct CaseResult {
    std::string label;
    std::optional<Verdict> verdict;
    Result run_result;
    bool sandbox_error{false};
    std::string error_msg;
};

void print_case_line(const CaseResult& cr, const Col& col) {
    if (cr.sandbox_error) {
        std::cout << std::format("{:>6}: {}ERR{} ({})\n", cr.label, col.red, col.rst, cr.error_msg);
        return;
    }
    const auto& r = cr.run_result;
    const char* vcol = cr.verdict ? verdict_col(*cr.verdict, col) : col.cyn;
    const char* vstr = cr.verdict ? verdict_str(*cr.verdict) : "---";
    std::string mem =
        r.peak_memory_bytes > 0
            ? std::format("{:.1f}MiB", static_cast<double>(r.peak_memory_bytes) / kMiBd)
            : "--";
    std::cout << std::format("{:>6}: {}{:<3}{}   cpu:{:>7}ms   wall:{:>7}ms   mem:{:>9}\n",
                             cr.label, vcol, vstr, col.rst, r.cpu_time.count(), r.wall_time.count(),
                             mem);
}

int run_batch(const std::vector<std::string>& argv, const std::string& cases_path,
              const std::string& checker_bin, const Limits& limits, bool json_output,
              const Col& col) {
    std::vector<TestCase> cases;
    try {
        cases = load_cases(cases_path);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    std::vector<CaseResult> results;
    results.reserve(cases.size());

    for (auto& tc : cases) {
        CaseResult cr;
        cr.label = tc.label;
        try {
            cr.run_result = cxxprobe::sandbox::run(argv, std::move(tc.input_data), limits);
            if (tc.answer_data) {
                bool ok = check_output(checker_bin, cr.run_result.stdout_data, cr.run_result,
                                       *tc.answer_data);
                cr.verdict = compute_verdict(cr.run_result, limits, ok);
            }
        } catch (const std::exception& ex) {
            cr.sandbox_error = true;
            cr.error_msg = ex.what();
        }
        if (!json_output) {
            print_case_line(cr, col);
        }
        results.push_back(std::move(cr));
    }

    if (json_output) {
        int passed = 0;
        std::cout << "{\n  \"results\": [\n";
        for (std::size_t i = 0; i < results.size(); ++i) {
            const auto& cr = results[i];
            const auto& r = cr.run_result;
            std::cout << "    {\"index\": " << (i + 1) << ", \"label\": " << json_str(cr.label);
            if (cr.sandbox_error) {
                std::cout << ", \"error\": " << json_str(cr.error_msg);
            } else {
                if (cr.verdict) {
                    std::cout << ", \"verdict\": \"" << verdict_str(*cr.verdict) << "\"";
                    if (*cr.verdict == Verdict::AC) {
                        ++passed;
                    }
                }
                std::cout << ", \"exit_code\": " << r.exit_code
                          << ", \"cpu_time_ms\": " << r.cpu_time.count()
                          << ", \"wall_time_ms\": " << r.wall_time.count()
                          << ", \"peak_memory_bytes\": " << r.peak_memory_bytes;
            }
            std::cout << "}";
            if (i + 1 < results.size()) {
                std::cout << ",";
            }
            std::cout << "\n";
        }
        std::cout << "  ],\n";
        std::cout << "  \"summary\": {\"passed\": " << passed << ", \"total\": " << results.size()
                  << "}\n}\n";
    } else {
        // Print summary line.
        int passed = 0;
        bool any_verdict = false;
        for (const auto& cr : results) {
            if (cr.verdict) {
                any_verdict = true;
                if (*cr.verdict == Verdict::AC) {
                    ++passed;
                }
            }
        }
        std::cout << "---\n";
        if (any_verdict) {
            std::cout << std::format("{}{}/{} passed{}\n",
                                     passed == static_cast<int>(results.size()) ? col.grn : col.red,
                                     passed, results.size(), col.rst);
        } else {
            std::cout << std::format("{} case(s) run (no expected output to judge)\n",
                                     results.size());
        }
    }

    // Exit 0 if all judged cases passed (or no judging), 1 if any failed.
    for (const auto& cr : results) {
        if (cr.sandbox_error) {
            return 2;
        }
        if (cr.verdict && *cr.verdict != Verdict::AC) {
            return 1;
        }
    }
    return 0;
}

// ─── Single-run ───────────────────────────────────────────────────────────────

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
            answer = read_file(expected_file);
        } catch (const std::exception& ex) {
            std::cerr << "cxxprobe: " << ex.what() << "\n";
            return 2;
        }
        // For the checker, re-load stdin from file if path is available.
        // If `-i -` was used, stdin_data was consumed; pass the captured
        // stdout instead of re-reading stdin (checker gets empty input).
        std::string in_for_checker;
        if (input_file != "-") {
            try {
                in_for_checker = load_stdin(input_file);
            } catch (...) {
            }
        }
        bool ok = check_output(checker_bin, in_for_checker, res, answer);
        verdict = compute_verdict(res, limits, ok);
    }

    if (json_output) {
        print_json(res, verdict);
    } else {
        print_human(res, verdict, quiet, col);
    }
    return single_exit_code(res, verdict);
}

}  // namespace

// ─── Entry point ──────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    CLI::App app{"cxxprobe — sandboxed program evaluator for coding contests"};
    app.set_version_flag("-V,--version", "cxxprobe 0.2.0");
    app.failure_message(CLI::FailureMessage::help);
    app.allow_extras(false);

    // Resource limits.
    unsigned memory_mib = 256;
    std::string cpu_str = "5000";
    std::string wall_str = "10000";
    unsigned max_pids = 64;

    app.add_option("-m,--memory-mb", memory_mib, "Memory limit in MiB [default: 256]");
    app.add_option("-t,--cpu,--cpu-ms", cpu_str,
                   "CPU time limit: 2s / 500ms / 2000 [default: 5000ms]");
    app.add_option("-w,--wall,--wall-ms", wall_str,
                   "Wall-clock limit: 2s / 500ms / 10000 [default: 10000ms]");
    app.add_option("-p,--pids", max_pids, "Maximum PIDs in sandbox [default: 64]");

    // I/O.
    std::string input_file;
    app.add_option("-i,--input", input_file,
                   "File to use as stdin; use - to read from pipe [default: empty]");

    // Judging (single-run).
    std::string expected_file;
    std::string checker_bin;
    app.add_option("-e,--expected", expected_file,
                   "Expected output file — enables verdict judgment");
    app.add_option("--checker", checker_bin,
                   "Custom checker binary (testlib ABI: checker <in> <out> <ans>)")
        ->check(CLI::ExistingFile);

    // Batch mode.
    std::string cases_path;
    app.add_option("--cases", cases_path,
                   "Batch mode: directory of *.in/*.ans pairs or YAML/JSON manifest");

    // Output style.
    bool json_output = false;
    bool quiet = false;
    bool no_color = false;
    app.add_flag("--json", json_output, "Emit result(s) as JSON");
    app.add_flag("-q,--quiet", quiet, "Suppress metadata summary (human mode)");
    app.add_flag("--no-color", no_color, "Disable ANSI color output");

    // Program + its arguments.
    std::vector<std::string> program_args;
    app.add_option("program", program_args, "Program to run, followed by its arguments")
        ->required()
        ->expected(1, -1)
        ->allow_extra_args();

    CLI11_PARSE(app, argc, argv);

    // Validate durations.
    ms cpu_limit{};
    ms wall_limit{};
    try {
        cpu_limit = parse_duration(cpu_str);
        wall_limit = parse_duration(wall_str);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 2;
    }

    // Validate --checker requires --expected or --cases.
    if (!checker_bin.empty() && expected_file.empty() && cases_path.empty()) {
        std::cerr << "cxxprobe: --checker requires --expected (single-run) or --cases (batch)\n";
        return 2;
    }

    const Col col = make_col(!no_color && (isatty(STDERR_FILENO) != 0));

    Limits limits;
    limits.memory_bytes = static_cast<std::size_t>(memory_mib) * 1024UL * 1024UL;
    limits.cpu = cpu_limit;
    limits.wall = wall_limit;
    limits.max_pids = max_pids;

    if (!cases_path.empty()) {
        return run_batch(program_args, cases_path, checker_bin, limits, json_output, col);
    }
    return run_single(program_args, input_file, expected_file, checker_bin, limits, json_output,
                      quiet, col);
}
