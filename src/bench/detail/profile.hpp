#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "../../judge/project_link.hpp"
#include "cxxprobe/bench.hpp"

namespace cxxprobe::bench::detail {

// One profiled run of an already verified benchmark binary, separate from
// the timed batches. Samples user-space callchains (frame pointers) at a
// fixed frequency with a software CPU clock, so it needs no PMU and no
// `perf` binary; names frames from the binary's own symbol table; and gives
// each frame's symbol to the part, shared source or entry whose object file
// defines it. Writes <out_dir>/stacks.folded and <out_dir>/flamegraph.svg.
//
// Profiling runs outside the sandbox on purpose: only code that has already
// passed verification on this input gets here (#11). Single-threaded
// drivers only; samples from other threads are not collected.
Profile record_profile(const std::filesystem::path& binary, const std::string& input, int cpu,
                       const std::vector<cxxprobe::judge::detail::OwnedObject>& objects,
                       const std::filesystem::path& out_dir);

// Exposed for tests: folded stacks ("a;b;c N" lines) to a flamegraph SVG.
std::string flamegraph_svg(const std::vector<std::pair<std::string, std::uint64_t>>& folded,
                           const std::string& title);

}  // namespace cxxprobe::bench::detail
