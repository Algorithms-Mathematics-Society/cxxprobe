#include "cxxprobe/compile.hpp"

#include <filesystem>

namespace cxxprobe::compile {

cxxprobe::sandbox::Limits default_compile_limits() {
    cxxprobe::sandbox::Limits limits;
    limits.memory_bytes = 2ULL * 1024 * 1024 * 1024;  // 2 GiB
    limits.cpu = std::chrono::milliseconds{60000};
    limits.wall = std::chrono::milliseconds{90000};
    limits.max_pids = 32;
    return limits;
}

namespace {

// Resolves a possibly-relative path against req.working_dir. The compiler
// is sandboxed via cxxprobe::sandbox::run(), which has no cwd concept — the
// child inherits the caller's cwd — so every path handed to it must be
// absolute for `working_dir` to have any effect.
std::filesystem::path resolve(const Request& req, const std::filesystem::path& p) {
    if (p.is_absolute()) {
        return p;
    }
    std::filesystem::path base =
        req.working_dir.empty() ? std::filesystem::current_path() : req.working_dir;
    return std::filesystem::absolute(base / p);
}

}  // namespace

Result compile(const Request& req, const cxxprobe::sandbox::Limits& compile_limits) {
    std::vector<std::string> argv;
    argv.push_back(req.cxx);
    argv.push_back("-std=" + req.std_flag);
    for (const auto& f : req.flags) {
        argv.push_back(f);
    }
    for (const auto& f : req.extra_flags) {
        argv.push_back(f);
    }
    for (const auto& s : req.sources) {
        argv.push_back(resolve(req, s).string());
    }
    argv.emplace_back("-o");
    argv.push_back(resolve(req, req.output_binary).string());

    Result result;
    try {
        cxxprobe::sandbox::Result sres = cxxprobe::sandbox::run(std::move(argv), "", compile_limits);
        result.exit_code = sres.exit_code;
        result.diagnostics = sres.stdout_data + sres.stderr_data;
        result.cpu_time = sres.cpu_time;
        result.wall_time = sres.wall_time;
        result.sandbox_timed_out = sres.wall_timed_out;
        result.ok = (sres.exit_code == 0);
    } catch (const std::exception& ex) {
        result.ok = false;
        result.diagnostics = ex.what();
    }
    return result;
}

}  // namespace cxxprobe::compile
