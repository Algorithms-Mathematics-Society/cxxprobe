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
#include "cxxprobe/project_discovery.hpp"
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

// What a pack carries: plain problems keep their unqualified directories, and
// projects travel whole, since a part does not compile without its project's
// shared sources.
struct Selection {
    std::vector<fs::path> problems;
    std::vector<fs::path> projects;
};

void select_requested(const fs::path& contest_dir, const std::string& slug, Selection& out) {
    if (!cxxprobe::problem::detail::safe_path(slug) || slug == ".") {
        throw std::runtime_error("pack: unsafe requested problem slug");
    }
    const fs::path requested{slug};
    const auto unit = contest_dir / *requested.begin();
    if (requested.has_parent_path()) {
        if (fs::exists(unit / "project.yaml")) {
            throw std::runtime_error(std::format(
                "pack: '{}' is one part of project '{}'; pack the project whole, since a part "
                "does not compile without its shared sources",
                slug, unit.filename().string()));
        }
        throw std::runtime_error("pack: unsafe requested problem slug");
    }
    if (fs::exists(unit / "project.yaml")) {
        out.projects.push_back(unit);
    } else if (fs::exists(unit / "problem.yaml")) {
        out.problems.push_back(unit);
    } else {
        throw std::runtime_error("pack: no problem '" + slug + "' found in " +
                                 contest_dir.string());
    }
}

Selection select_units(const fs::path& contest_dir, const PackOptions& opts) {
    Selection out;
    if (!opts.problem_slugs.empty()) {
        for (const auto& slug : opts.problem_slugs) {
            select_requested(contest_dir, slug, out);
        }
        return out;
    }
    // Discovery validates every project and rejects colliding identities.
    for (const auto& entry : cxxprobe::project::discover_problems(contest_dir)) {
        if (!entry.slug.contains('/')) {
            out.problems.push_back(entry.dir);
        }
    }
    for (const auto& entry : fs::directory_iterator(contest_dir)) {
        if (entry.is_directory() && fs::exists(entry.path() / "project.yaml")) {
            out.projects.push_back(entry.path());
        }
    }
    std::ranges::sort(out.problems);
    std::ranges::sort(out.projects);
    return out;
}

// One archive subtree: every file under root, written beneath prefix.
struct PackedTree {
    std::string prefix;
    fs::path root;
    std::vector<std::string> files;
};

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

// The preview an importer reads for one problem, with its attachment files
// taken from the files actually packed for it (relative to its directory).
Json problem_entry(const cxxprobe::problem::ProblemConfig& config,
                   const std::vector<std::string>& files) {
    namespace detail = cxxprobe::problem::detail;
    warn_if_absolute_reference(config);
    auto dir = detail::attachment_dir(config);
    auto candidates = detail::attachment_subset(files, dir);
    if (candidates.empty() &&
        (config.attachments.declared || fs::exists(config.problem_dir / dir))) {
        std::cerr << "cxxprobe: warning: problem '" << config.slug << "': attachments directory '"
                  << dir << "' is missing or contains no packable candidate files\n";
    }
    auto preview = cxxprobe::problem::preview_to_json(config, cxxprobe::problem::ProjectDefaults{});
    preview["attachments"] = Json{{"dir", dir}, {"files", candidates}};
    return preview;
}

bool under(const std::string& file, const std::string& dir) { return file.starts_with(dir + "/"); }

// project.yaml plus the shared, integration and declared part subtrees, and
// nothing else under the project. Symlinks never enter a pack, so anything a
// part needs to compile must be a regular file here or the pack is refused.
std::vector<std::string> project_files(const cxxprobe::project::ProjectConfig& project) {
    std::vector<std::string> kept;
    for (auto& file : cxxprobe::problem::detail::package_files(project.project_dir)) {
        const bool declared =
            file == "project.yaml" || under(file, project.shared.dir) ||
            under(file, project.integration.dir) ||
            std::ranges::any_of(project.parts, [&](const auto& p) { return under(file, p.dir); });
        if (declared) {
            kept.push_back(std::move(file));
        }
    }
    auto require = [&](const std::string& file, const std::string& field) {
        if (!std::ranges::binary_search(kept, file)) {
            throw std::runtime_error(std::format(
                "pack: project '{}': {} '{}' is not a packable regular file (symlinks are never "
                "packed)",
                project.slug, field, file));
        }
    };
    for (const auto& source : project.shared.sources) {
        require((fs::path{project.shared.dir} / source).generic_string(), "shared source");
    }
    for (const auto& part : project.parts) {
        require((fs::path{part.dir} / "problem.yaml").generic_string(), "part manifest");
    }
    return kept;
}

Json project_entry(const fs::path& dir, std::vector<PackedTree>& trees, PackResult& result) {
    const auto project = cxxprobe::project::load_from_dir(dir);
    if (!cxxprobe::problem::detail::safe_path(project.slug) || project.slug == ".") {
        throw std::runtime_error("pack: unsafe project directory");
    }
    auto files = project_files(project);
    Json parts = Json::array();
    PackedProject packed{.slug = project.slug, .name = project.name};
    for (const auto& part : project.parts) {
        const auto config = cxxprobe::problem::load_from_dir(project.project_dir / part.dir);
        parts.push_back(
            Json{{"label", part.label},
                 {"slug", config.slug},
                 {"dir", part.dir},
                 {"preview", problem_entry(config, cxxprobe::problem::detail::attachment_subset(
                                                       files, part.dir))}});
        packed.parts.push_back(PackedProblem{.slug = config.slug, .name = config.name});
    }
    result.projects.push_back(std::move(packed));
    trees.push_back(
        PackedTree{.prefix = project.slug, .root = project.project_dir, .files = std::move(files)});
    return Json{{"slug", project.slug}, {"name", project.name}, {"parts", std::move(parts)}};
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
    const auto selection = select_units(contest_dir, opts);

    PackResult result;
    result.output_path = output_path;
    std::vector<PackedTree> trees;
    Json problems_json = Json::array();
    for (const auto& dir : selection.problems) {
        const auto config = cxxprobe::problem::load_from_dir(dir);
        if (config.slug.contains('/')) {
            throw std::runtime_error("pack: '" + dir.filename().string() +
                                     "' is a project part; pack its project instead");
        }
        if (!cxxprobe::problem::detail::safe_path(config.slug) || config.slug == "." ||
            !fs::is_directory(fs::symlink_status(config.problem_dir))) {
            throw std::runtime_error("pack: unsafe problem directory");
        }
        auto files = cxxprobe::problem::detail::package_files(config.problem_dir);
        problems_json.push_back(problem_entry(config, files));
        result.problems.push_back(PackedProblem{.slug = config.slug, .name = config.name});
        trees.push_back(PackedTree{
            .prefix = config.slug, .root = config.problem_dir, .files = std::move(files)});
    }
    Json projects_json = Json::array();
    for (const auto& dir : selection.projects) {
        projects_json.push_back(project_entry(dir, trees, result));
    }

    Json manifest;
    manifest["format_version"] = kManifestFormatVersion;
    manifest["packed_at"] = packed_at_now();
    manifest["cxxprobe_version"] = CXXPROBE_VERSION;
    manifest["contest"] = Json{{"name", fs::absolute(contest_dir).filename().string()}};
    manifest["problems"] = std::move(problems_json);
    // Absent for a plain contest, whose manifest must not change.
    if (!projects_json.empty()) {
        manifest["projects"] = std::move(projects_json);
    }

    ZipWriter writer(output_path);
    writer.add_data("manifest.json", manifest.dump(2));
    writer.add_file("contest.yaml", contest_dir / "contest.yaml");
    for (const auto& tree : trees) {
        for (const auto& file : tree.files) {
            writer.add_file(tree.prefix + "/" + file, tree.root / file);
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
    if (manifest.contains("projects")) {
        for (const auto& p : manifest["projects"]) {
            result.project_slugs.push_back(p.value("slug", std::string{}));
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
