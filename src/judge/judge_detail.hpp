#pragma once

#include <unistd.h>

#include <atomic>
#include <filesystem>
#include <format>
#include <string_view>

#include "cxxprobe/compile.hpp"
#include "cxxprobe/judge.hpp"

namespace cxxprobe::judge::detail {

// Unique per process and call; shared by every judge translation unit.
inline std::filesystem::path make_temp_path(std::string_view prefix) {
    static std::atomic<unsigned long> counter{0};
    return std::filesystem::temp_directory_path() /
           std::format("{}-{}-{}", prefix, static_cast<long>(::getpid()), counter.fetch_add(1));
}

inline CompileStepReport to_compile_report(const cxxprobe::compile::Result& r) {
    CompileStepReport rep;
    rep.ran = true;
    rep.ok = r.ok;
    rep.exit_code = r.exit_code;
    rep.diagnostics = r.diagnostics;
    return rep;
}

}  // namespace cxxprobe::judge::detail
