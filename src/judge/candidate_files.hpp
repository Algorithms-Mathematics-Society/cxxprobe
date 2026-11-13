#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace cxxprobe::judge::detail {

namespace fs = std::filesystem;

struct CandidateFiles {
    fs::path root;
    std::vector<fs::path> units;
    std::vector<fs::path> files;
    bool bundled{false};
};

inline void prepend_candidate_includes(std::vector<std::string>& flags,
                                       const CandidateFiles& candidate, const fs::path& entry,
                                       const fs::path& public_dir) {
    if (!candidate.bundled) return;
    if (!fs::is_regular_file(public_dir / ".cxxprobe-sanitized")) {
        throw std::runtime_error("candidate package attachments were not sanitized");
    }
    flags.insert(flags.begin(), {
        "-I" + entry.parent_path().string(),
        "-I" + candidate.root.string(),
        "-I" + public_dir.string(),
    });
}

inline bool within(const fs::path& path, const fs::path& root) {
    auto mismatch = std::mismatch(root.begin(), root.end(), path.begin(), path.end());
    return mismatch.first == root.end();
}

inline CandidateFiles candidate_files(const fs::path& entry) {
    const fs::path sidecar{entry.string() + ".cxxprobe.json"};
    if (!fs::exists(sidecar)) {
        const auto canonical_entry = fs::canonical(entry);
        return {.root = canonical_entry.parent_path(), .units = {canonical_entry},
                .files = {canonical_entry}, .bundled = false};
    }
    std::ifstream stream{sidecar};
    if (!stream) throw std::runtime_error("cannot open candidate manifest");
    const auto doc = nlohmann::json::parse(stream);
    if (!doc.is_object() || doc.value("format", "") != "cxxprobe-candidate-v1" ||
        !doc.contains("root") || !doc.contains("units") || !doc.contains("files")) {
        throw std::runtime_error("invalid candidate manifest");
    }
    const auto root = fs::canonical(doc.at("root").get<std::string>());
    if (!fs::is_directory(root)) throw std::runtime_error("candidate root is not a directory");
    CandidateFiles result{.root = root, .bundled = true};
    auto read_paths = [&](const nlohmann::json& paths, std::vector<fs::path>& output) {
        if (!paths.is_array() || paths.empty() || paths.size() > 16)
            throw std::runtime_error("invalid candidate file list");
        for (const auto& item : paths) {
            const auto rel = item.get<std::string>();
            const fs::path raw{rel};
            if (rel.empty() || raw.is_absolute() || rel.find('\\') != std::string::npos ||
                std::ranges::any_of(raw, [](const fs::path& part) {
                    return part == "." || part == ".." || part.empty();
                })) throw std::runtime_error("unsafe candidate path");
            const auto absolute = fs::canonical(root / raw);
            if (!within(absolute, root) || !fs::is_regular_file(absolute) ||
                std::ranges::find(output, absolute) != output.end())
                throw std::runtime_error("invalid candidate file");
            output.push_back(absolute);
        }
    };
    read_paths(doc.at("files"), result.files);
    read_paths(doc.at("units"), result.units);
    const auto canonical_entry = fs::canonical(entry);
    if (std::ranges::find(result.units, canonical_entry) == result.units.end())
        throw std::runtime_error("candidate entrypoint is not a compilation unit");
    for (const auto& unit : result.units) {
        if (std::ranges::find(result.files, unit) == result.files.end())
            throw std::runtime_error("candidate unit is not in file list");
    }
    return result;
}

}  // namespace cxxprobe::judge::detail
