#pragma once

#include <filesystem>

namespace cxxprobe::embedded_gtest {

struct ResolvedPaths {
    std::filesystem::path include_dir;
    std::filesystem::path lib_dir;
};

// Extracts the embedded GTest include/lib files to a stable, process- and
// machine-independent cache directory under the system temp dir (keyed by
// bundle_hash(), so a different cxxprobe build never reuses another
// build's extraction), and returns the two directories a compiler
// invocation should use as -I/-L. Idempotent and safe to call
// concurrently from multiple threads/processes: extraction happens into a
// unique staging directory first, published via an atomic rename, so a
// racing caller either finds the fully-extracted directory already
// present or wins the race to create it — never observes a partial
// extraction. Throws std::runtime_error on filesystem failure.
ResolvedPaths resolve();

}  // namespace cxxprobe::embedded_gtest
