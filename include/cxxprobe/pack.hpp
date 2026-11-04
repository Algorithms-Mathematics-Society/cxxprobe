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

struct PackedProject {
    std::string slug;
    std::string name;
    std::vector<PackedProblem> parts;  // qualified slugs, in manifest order
};

struct PackOptions {
    // Empty = every plain problem and project found under contest_dir. A
    // project slug takes the project whole; naming one of its parts is an error.
    std::vector<std::string> problem_slugs;
};

struct PackResult {
    std::filesystem::path output_path;
    std::vector<PackedProblem> problems;
    std::vector<PackedProject> projects;
};

// Packs contest_dir (must contain a contest.yaml) into a zip at
// output_path: manifest.json + a verbatim copy of contest.yaml + a
// sorted, denylist-filtered regular files of each selected problem directory.
// Symlinks and special files are excluded. Per-problem attachments metadata
// lists packed candidate files relative to the configured attachments directory.
// A project is packed beneath its slug as project.yaml plus its shared,
// integration and declared part directories, and described in manifest
// projects[] (slug, name, and each part's label, qualified slug, dir and
// preview); a contest without projects has no projects key at all.
// Fail-fast: throws std::runtime_error if contest_dir has no contest.yaml,
// a requested slug in opts.problem_slugs doesn't exist, or any selected
// problem's problem.yaml fails to load.
PackResult pack_contest(const std::filesystem::path& contest_dir,
                        const std::filesystem::path& output_path, const PackOptions& opts = {});

struct UnpackResult {
    std::filesystem::path dest_dir;
    std::vector<std::string> problem_slugs;
    std::vector<std::string> project_slugs;
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
