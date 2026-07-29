#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "cxxprobe/problem.hpp"

namespace cxxprobe::pack {

struct PackedProblem {
    std::string slug;
    std::string name;
};

struct PackOptions {
    std::vector<std::string> problem_slugs;  // empty = every problem found under contest_dir
};

struct PackResult {
    std::filesystem::path output_path;
    std::vector<PackedProblem> problems;
};

// Packs contest_dir (must contain a contest.yaml) into a zip at
// output_path: manifest.json + a verbatim copy of contest.yaml + a
// denylist-filtered verbatim copy of each selected problem directory.
// Fail-fast: throws std::runtime_error if contest_dir has no contest.yaml,
// a requested slug in opts.problem_slugs doesn't exist, or any selected
// problem's problem.yaml fails to load.
PackResult pack_contest(const std::filesystem::path& contest_dir,
                        const std::filesystem::path& output_path, const PackOptions& opts = {});

struct UnpackResult {
    std::filesystem::path dest_dir;
    std::vector<std::string> problem_slugs;
    int manifest_format_version{0};
};

// Unpacks a zip produced by pack_contest into dest_dir. Throws
// std::runtime_error if: zip_path isn't a valid zip or has no
// manifest.json at its root; manifest.json's format_version is newer than
// this build supports; dest_dir already contains a contest.yaml and force
// is false.
UnpackResult unpack_contest(const std::filesystem::path& zip_path,
                            const std::filesystem::path& dest_dir, bool force = false);

}  // namespace cxxprobe::pack
