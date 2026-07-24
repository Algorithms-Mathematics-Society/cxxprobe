#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cxxprobe::bundle {

inline constexpr std::string_view kContract = "cxxprobe.problem-bundle";
inline constexpr std::uint32_t kSchemaVersionV1 = 1;
inline constexpr std::uint32_t kSchemaVersionV2 = 2;
inline constexpr std::uint32_t kSchemaVersion = kSchemaVersionV2;

struct ValidationLimits {
    std::size_t max_files{4096};
    std::uint64_t max_file_bytes{64ULL * 1024ULL * 1024ULL};
    std::uint64_t max_total_bytes{512ULL * 1024ULL * 1024ULL};
    std::size_t max_path_bytes{512};
    std::size_t max_component_bytes{255};
    std::size_t max_depth{32};
    std::size_t max_public_assets{64};
    std::uint64_t max_public_statement_bytes{2ULL * 1024ULL * 1024ULL};
    std::uint64_t max_public_starter_bytes{1ULL * 1024ULL * 1024ULL};
    std::uint64_t max_public_asset_bytes{16ULL * 1024ULL * 1024ULL};
    std::uint64_t max_public_total_bytes{64ULL * 1024ULL * 1024ULL};
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

struct PublicAssetRecord {
    std::string path;
    std::string media_type;
};

struct PublicStarterRecord {
    std::string path;
    std::string language;
};

struct PublicRecord {
    std::optional<std::string> statement;
    std::vector<PublicAssetRecord> assets;
    std::optional<PublicStarterRecord> starter;
};

struct ProblemRecord {
    std::string slug;
    std::string name;
    PublicRecord public_files;
    ExecutionRecord execution;
};

struct Manifest {
    std::uint32_t schema_version{kSchemaVersionV1};
    std::string bundle_sha256;
    std::uint64_t total_bytes{};
    std::vector<ProblemRecord> problems;
    std::vector<FileRecord> files;
};

// Validates a contest tree and returns its deterministic, content-addressed
// manifest. Throws std::runtime_error when the bundle is invalid or exceeds
// a limit. The digest is independent of mtimes, owners and permissions.
Manifest validate(const std::filesystem::path& contest_dir, const ValidationLimits& limits = {});

// Field order is part of each schema version. The bundle digest is SHA-256
// of the compact ordered JSON returned here with bundle_sha256 omitted.
nlohmann::ordered_json to_json(const Manifest& manifest);

}  // namespace cxxprobe::bundle
