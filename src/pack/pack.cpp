#include "cxxprobe/pack.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "../problem/detail/package_files.hpp"
#include "cxxprobe/project.hpp"
#include "detail/zip_archive.hpp"

#ifndef CXXPROBE_VERSION
#define CXXPROBE_VERSION "0.0.0-dev"
#endif

namespace cxxprobe::pack {

namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;
using cxxprobe::pack::detail::ZipEntry;
using cxxprobe::pack::detail::ZipReader;
using cxxprobe::pack::detail::ZipWriter;

namespace {

constexpr int kManifestFormatVersion = 1;

std::string packed_at_now() {
    auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

// Warns (does not fail — pack does no validation, per design) when a
// problem's tests.manifest is an absolute path: operator/ silently
// discards problem_dir for an absolute right-hand side, so the referenced
// file lives outside the problem directory and won't be included in the
// pack.
void warn_if_absolute_reference(const cxxprobe::problem::ProblemConfig& config) {
    if (config.tests.manifest && fs::path(*config.tests.manifest).is_absolute()) {
        std::cerr << "cxxprobe: warning: problem '" << config.slug
                  << "': tests.manifest is an absolute path and will not be included in the pack\n";
    }
}

// Rejects absolute paths and any ".." component — a corrupt/hostile
// manifest must never be able to write outside dest_dir on extraction.
bool is_safe_relative_entry(const std::string& name) {
    if (name.empty() || name.front() == '/') {
        return false;
    }
    fs::path p(name);
    return std::ranges::none_of(p, [](const fs::path& part) { return part == ".."; });
}

}  // namespace

PackResult pack_contest(const fs::path& contest_dir, const fs::path& output_path,
                        const PackOptions& opts) {
    if (!fs::exists(contest_dir / "contest.yaml")) {
        throw std::runtime_error("pack: no contest.yaml found in " + contest_dir.string());
    }

    // Project archives need the #7 manifest contract. Validate project references
    // (absolute paths are errors) before refusing an incomplete flattened pack.
    if (opts.problem_slugs.empty()) {
        for (const auto& entry : fs::directory_iterator(contest_dir)) {
            if (entry.is_directory() && fs::exists(entry.path() / "project.yaml")) {
                (void)cxxprobe::project::load_from_dir(entry.path());
                throw std::runtime_error(
                    "pack: project packaging is not yet supported; "
                    "select ordinary problems explicitly");
            }
        }
    }

    std::vector<fs::path> problem_dirs;
    if (opts.problem_slugs.empty()) {
        problem_dirs = cxxprobe::problem::find_problem_dirs(contest_dir);
        std::ranges::sort(problem_dirs);
    } else {
        for (const auto& slug : opts.problem_slugs) {
            if (!cxxprobe::problem::detail::safe_path(slug) || slug == "." ||
                fs::path(slug).has_parent_path()) {
                throw std::runtime_error("pack: unsafe requested problem slug");
            }
            fs::path dir = contest_dir / slug;
            if (!fs::exists(dir / "problem.yaml")) {
                throw std::runtime_error("pack: no problem '" + slug + "' found in " +
                                         contest_dir.string());
            }
            problem_dirs.push_back(dir);
        }
    }

    cxxprobe::problem::ProjectDefaults defaults;
    std::vector<cxxprobe::problem::ProblemConfig> configs;
    configs.reserve(problem_dirs.size());
    for (const auto& dir : problem_dirs) {
        configs.push_back(cxxprobe::problem::load_from_dir(dir));
        if (configs.back().slug.contains('/')) {
            throw std::runtime_error(
                "pack: project parts require project packaging (not yet supported)");
        }
    }

    Json manifest;
    manifest["format_version"] = kManifestFormatVersion;
    manifest["packed_at"] = packed_at_now();
    manifest["cxxprobe_version"] = CXXPROBE_VERSION;
    Json contest_json;
    contest_json["name"] = fs::absolute(contest_dir).filename().string();
    manifest["contest"] = std::move(contest_json);

    Json problems_json = Json::array();
    std::vector<std::vector<std::string>> packed_files;
    PackResult result;
    result.output_path = output_path;
    for (const auto& config : configs) {
        warn_if_absolute_reference(config);
        namespace detail = cxxprobe::problem::detail;
        if (!detail::safe_path(config.slug) || config.slug == "." ||
            !fs::is_directory(fs::symlink_status(config.problem_dir))) {
            throw std::runtime_error("pack: unsafe problem directory");
        }
        auto dir = detail::attachment_dir(config);
        auto files = detail::package_files(config.problem_dir);
        auto candidates = detail::attachment_subset(files, dir);
        if (candidates.empty() &&
            (config.attachments.declared || fs::exists(config.problem_dir / dir))) {
            std::cerr << "cxxprobe: warning: problem '" << config.slug
                      << "': attachments directory '" << dir
                      << "' is missing or contains no packable candidate files\n";
        }
        auto preview = cxxprobe::problem::preview_to_json(config, defaults);
        preview["attachments"] = Json{{"dir", dir}, {"files", candidates}};
        problems_json.push_back(std::move(preview));
        packed_files.push_back(std::move(files));
        result.problems.push_back(PackedProblem{.slug = config.slug, .name = config.name});
    }
    manifest["problems"] = std::move(problems_json);

    ZipWriter writer(output_path);
    writer.add_data("manifest.json", manifest.dump(2));
    writer.add_file("contest.yaml", contest_dir / "contest.yaml");
    for (std::size_t i = 0; i < configs.size(); ++i) {
        const auto& config = configs[i];
        for (const auto& file : packed_files[i]) {
            writer.add_file(config.slug + "/" + file, config.problem_dir / file);
        }
    }
    writer.close();

    return result;
}

UnpackResult unpack_contest(const fs::path& zip_path, const fs::path& dest_dir, bool force) {
    ZipReader reader(zip_path);
    std::vector<ZipEntry> entries = reader.list_entries();

    const ZipEntry* manifest_entry = nullptr;
    for (const auto& entry : entries) {
        if (entry.name == "manifest.json") {
            manifest_entry = &entry;
            break;
        }
    }
    if (manifest_entry == nullptr) {
        throw std::runtime_error("unpack: " + zip_path.string() +
                                 " has no manifest.json — not a cxxprobe pack");
    }

    Json manifest;
    try {
        manifest = Json::parse(reader.read_data(*manifest_entry));
    } catch (const std::exception& ex) {
        throw std::runtime_error("unpack: manifest.json is not valid JSON: " +
                                 std::string(ex.what()));
    }

    int format_version = manifest.value("format_version", 0);
    if (format_version > kManifestFormatVersion) {
        throw std::runtime_error(
            std::format("unpack: pack format_version {} is newer than this cxxprobe build supports "
                        "(max {}) — upgrade cxxprobe",
                        format_version, kManifestFormatVersion));
    }

    if (!force && fs::exists(dest_dir / "contest.yaml")) {
        throw std::runtime_error("unpack: " + dest_dir.string() +
                                 " already contains a contest.yaml — pass force=true to overwrite");
    }

    UnpackResult result;
    result.dest_dir = dest_dir;
    result.manifest_format_version = format_version;
    if (manifest.contains("problems")) {
        for (const auto& p : manifest["problems"]) {
            result.problem_slugs.push_back(p.value("slug", std::string{}));
        }
    }

    for (const auto& entry : entries) {
        if (entry.name == "manifest.json") {
            continue;
        }
        if (!is_safe_relative_entry(entry.name)) {
            throw std::runtime_error("unpack: refusing unsafe entry path '" + entry.name + "'");
        }
        reader.extract_to(entry, dest_dir / entry.name);
    }

    return result;
}

}  // namespace cxxprobe::pack
