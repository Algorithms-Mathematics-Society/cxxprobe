#include <CLI/CLI.hpp>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "cxxprobe/sandbox.hpp"

namespace {

// ── JSON helpers ──────────────────────────────────────────────────────────────

constexpr unsigned kJsonControlCutoff = 0x20;

std::string json_string(const std::string& str) {
    std::string out;
    out.reserve(str.size() + 2);
    out += '"';
    for (unsigned char ch : str) {
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
                if (ch < kJsonControlCutoff) {
                    out += std::format("\\u{:04x}", static_cast<unsigned>(ch));
                } else {
                    out += static_cast<char>(ch);
                }
        }
    }
    out += '"';
    return out;
}

// ── output formatters ─────────────────────────────────────────────────────────

void print_json(const cxxprobe::sandbox::Result& res) {
    std::cout << "{\n"
              << "  \"exit_code\": " << res.exit_code << ",\n"
              << "  \"peak_memory_bytes\": " << res.peak_memory_bytes << ",\n"
              << "  \"cpu_time_ms\": " << res.cpu_time.count() << ",\n"
              << "  \"stdout\": " << json_string(res.stdout_data) << ",\n"
              << "  \"stderr\": " << json_string(res.stderr_data) << "\n"
              << "}\n";
}

void print_human(const cxxprobe::sandbox::Result& res, bool quiet) {
    std::cout << res.stdout_data;
    std::cerr << res.stderr_data;
    if (!quiet) {
        double mem_mib = static_cast<double>(res.peak_memory_bytes) / (1024.0 * 1024.0);
        std::cerr << std::format("[exit: {} | mem: {:.1f} MiB | cpu: {} ms]\n", res.exit_code,
                                 mem_mib, res.cpu_time.count());
    }
}

// ── stdin loader ──────────────────────────────────────────────────────────────

std::string load_input(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::ifstream ifs{path, std::ios::binary};
    if (!ifs) {
        throw std::runtime_error{std::format("cannot open input file: {}", path)};
    }
    return {std::istreambuf_iterator<char>{ifs}, {}};
}

}  // namespace

int main(int argc, char* argv[]) {
    CLI::App app{"cxxprobe — sandboxed program evaluator for coding contests"};
    app.set_version_flag("-V,--version", "cxxprobe 0.1.0");
    app.failure_message(CLI::FailureMessage::help);

    // Resource limits.
    unsigned memory_mib = 256;
    unsigned cpu_ms = 5000;
    unsigned wall_ms = 10000;
    unsigned max_pids = 64;

    app.add_option("-m,--memory-mb", memory_mib, "Memory limit in MiB (default: 256)");
    app.add_option("-t,--cpu-ms", cpu_ms, "CPU time limit in milliseconds (default: 5000)");
    app.add_option("-w,--wall-ms", wall_ms, "Wall-clock limit in milliseconds (default: 10000)");
    app.add_option("-p,--pids", max_pids, "Maximum number of PIDs (default: 64)");

    // I/O options.
    std::string input_file;
    bool json_output = false;
    bool quiet = false;

    app.add_option("-i,--input", input_file,
                   "File to use as stdin for the program (default: empty)");
    app.add_flag("--json", json_output, "Output result as JSON instead of plain text");
    app.add_flag("-q,--quiet", quiet, "Suppress the metadata summary line");

    // Program + its arguments — everything after --.
    std::vector<std::string> program_args;
    app.add_option("program", program_args, "Program to run, followed by its arguments")
        ->required()
        ->expected(1, -1)
        ->allow_extra_args();

    CLI11_PARSE(app, argc, argv);

    // Load stdin file.
    std::string stdin_data;
    try {
        stdin_data = load_input(input_file);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: " << ex.what() << "\n";
        return 1;
    }

    // Build limits.
    cxxprobe::sandbox::Limits limits;
    limits.memory_bytes = static_cast<std::size_t>(memory_mib) * 1024UL * 1024UL;
    limits.cpu = std::chrono::milliseconds{cpu_ms};
    limits.wall = std::chrono::milliseconds{wall_ms};
    limits.max_pids = max_pids;

    // Execute.
    cxxprobe::sandbox::Result result;
    try {
        result = cxxprobe::sandbox::run(std::move(program_args), std::move(stdin_data), limits);
    } catch (const std::exception& ex) {
        std::cerr << "cxxprobe: sandbox error: " << ex.what() << "\n";
        return 1;
    }

    // Output.
    if (json_output) {
        print_json(result);
    } else {
        print_human(result, quiet);
    }

    // Pass through exit code; negative = signal kill, clamp to avoid shell specials.
    if (result.exit_code < 0 || result.exit_code > 125) {
        return 1;
    }
    return result.exit_code;
}
