#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace cxxprobe::bundle {

inline constexpr std::string_view kContract = "cxxprobe.problem-bundle";
inline constexpr std::uint32_t kSchemaVersion = 1;

struct ValidationLimits {
    std::size_t max_files{4096};
    std::uint64_t max_file_bytes{64ULL * 1024ULL * 1024ULL};
    std::uint64_t max_total_bytes{512ULL * 1024ULL * 1024ULL};
    std::size_t max_path_bytes{512};
    std::size_t max_component_bytes{255};
    std::size_t max_depth{32};
};

struct FileRecord {
    std::string path;
    std::uint64_t size_bytes{};
    std::string sha256;
};

struct CompilerExecution {
    std::string cxx;
    std::string std_flag;
    std::vector<std::string> flags;
    std::vector<std::string> extra_sources;
};

struct LimitsExecution {
    std::uint64_t memory_bytes{};
    std::int64_t cpu_time_ms{};
    std::int64_t wall_time_ms{};
    unsigned max_pids{};
};

struct ExecutionRecord {
    CompilerExecution compiler;
    LimitsExecution limits;
};

struct ProblemRecord {
    std::string slug;
    std::string name;
    ExecutionRecord execution;
};

struct Manifest {
    std::string bundle_sha256;
    std::uint64_t total_bytes{};
    std::vector<ProblemRecord> problems;
    std::vector<FileRecord> files;
};

// Validates a contest tree and returns its deterministic, content-addressed
// manifest. Throws std::runtime_error when the bundle is invalid or exceeds
// a limit. The digest is independent of mtimes, owners and permissions.
Manifest validate(const std::filesystem::path& contest_dir, const ValidationLimits& limits = {});

// Field order is part of schema version 1. The bundle digest is SHA-256 of
// the compact ordered JSON returned here with bundle_sha256 omitted.
nlohmann::ordered_json to_json(const Manifest& manifest);

}  // namespace cxxprobe::bundle
