#include "project_link.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

#include "cxxprobe/compile.hpp"
#include "embedded_gtest/gtest_paths.hpp"
#include "candidate_files.hpp"

namespace cxxprobe::judge::detail {

namespace fs = std::filesystem;

TempFiles::~TempFiles() {
    for (const auto& path : paths_) {
        std::error_code ignored;
        fs::remove(path, ignored);
    }
}

fs::path TempFiles::add(std::string_view prefix, std::string_view suffix) {
    auto path = make_temp_path(prefix);
    path += suffix;
    paths_.push_back(path);
    return path;
}

namespace {

struct Toolchain {
    std::string cxx;
    std::string std_flag;
    std::vector<std::string> flags;
    std::vector<std::string> include_flags;  // -I..., already resolved
    fs::path working_dir;
};

Toolchain part_toolchain(const cxxprobe::problem::ResolvedCompiler& resolved,
                         const fs::path& problem_dir) {
    Toolchain tool{.cxx = resolved.cxx,
                   .std_flag = resolved.std_flag,
                   .flags = resolved.flags,
                   .include_flags = {},
                   .working_dir = problem_dir};
    for (const auto& dir : resolved.include_dirs) {
        tool.include_flags.push_back("-I" + (problem_dir / dir).string());
    }
    return tool;
}

cxxprobe::compile::Result compile_object(const Toolchain& tool, const LinkOptions& options,
                                         const fs::path& source, const std::string& main_name,
                                         const fs::path& output) {
    cxxprobe::compile::Request req;
    req.sources = {source};
    req.cxx = tool.cxx;
    req.std_flag = tool.std_flag;
    req.flags = tool.flags;
    req.extra_flags = {"-c"};
    if (!main_name.empty()) {
        req.extra_flags.push_back("-Dmain=" + main_name);
    }
    req.extra_flags.insert(req.extra_flags.end(), tool.include_flags.begin(),
                           tool.include_flags.end());
    req.extra_flags.insert(req.extra_flags.end(), options.compile_flags.begin(),
                           options.compile_flags.end());
    req.output_binary = output;
    req.working_dir = tool.working_dir;
    return cxxprobe::compile::compile(req);
}

const PartSource& source_for(const std::vector<PartSource>& sources, const std::string& label) {
    const auto it = std::ranges::find(sources, label, &PartSource::label);
    if (it == sources.end()) {
        throw std::runtime_error(
            std::format("project build: no source given for part '{}'", label));
    }
    if (std::ranges::count(sources, label, &PartSource::label) > 1) {
        throw std::runtime_error(
            std::format("project build: more than one source given for part '{}'", label));
    }
    return *it;
}

// The chosen source, or integration.fallback_dir/<label>.cpp in its place.
// Returns false only when the part ends up without a usable object.
bool build_part(const cxxprobe::project::ProjectConfig& project, const Toolchain& tool,
                const LinkOptions& options, const PartSource& chosen, std::size_t index,
                const fs::path& public_dir,
                IntegrationPart& part, ProjectLink& out, TempFiles& temps) {
    part.origin = chosen.origin;
    if (!chosen.source) {
        return true;
    }
    const auto main_name = std::format("cxxprobe_part_{}_main", index);
    part.source = chosen.source->string();
    const auto candidate = candidate_files(*chosen.source);
    Toolchain candidate_tool = tool;
    prepend_candidate_includes(candidate_tool.include_flags, candidate, *chosen.source, public_dir);
    std::vector<OwnedObject> built_objects;
    cxxprobe::compile::Result built;
    bool all_built = true;
    for (const auto& unit : candidate.units) {
        const auto object = temps.add("cxxprobe-part", ".o");
        built = compile_object(candidate_tool, options, unit,
                               unit == fs::canonical(*chosen.source) ? main_name : "", object);
        if (!built.ok) {
            all_built = false;
            break;
        }
        built_objects.push_back({.owner = part.label, .object = object});
    }
    if (all_built) {
        out.objects.insert(out.objects.end(), built_objects.begin(), built_objects.end());
        return true;
    }
    part.compiled = false;
    part.diagnostics = built.diagnostics;
    if (!project.integration.fallback_dir) {
        return false;
    }
    const auto fallback = project.project_dir / project.integration.dir /
                          *project.integration.fallback_dir / (part.label + ".cpp");
    if (!fs::is_regular_file(fallback)) {
        return false;
    }
    const auto fallback_object = temps.add("cxxprobe-part-fallback", ".o");
    if (!compile_object(tool, options, fallback, main_name, fallback_object).ok) {
        return false;
    }
    // The substitution must be visible, never silent: origin and the original
    // diagnostics both stay on the report.
    part.compiled = true;
    part.origin = "fallback";
    part.source = fallback.string();
    out.objects.push_back({.owner = part.label, .object = fallback_object});
    return true;
}

// A part's own extra sources travel with it; shared ones are linked once.
bool build_own_extras(const Toolchain& tool, const LinkOptions& options,
                      const cxxprobe::problem::ResolvedCompiler& resolved, const std::string& label,
                      ProjectLink& out, TempFiles& temps) {
    for (const auto& extra : resolved.extra_sources) {
        if (std::ranges::contains(resolved.shared_sources, extra)) {
            continue;
        }
        const auto object = temps.add("cxxprobe-part-extra", ".o");
        if (!compile_object(tool, options, tool.working_dir / extra, "", object).ok) {
            return false;
        }
        out.objects.push_back({.owner = label, .object = object});
    }
    return true;
}

// Shared sources and the entry, with the project defaults. A failure here is
// reported on the link step: no part is to blame for it.
bool build_common(const cxxprobe::project::ProjectConfig& project,
                  const cxxprobe::problem::ProjectDefaults& defaults, const fs::path& entry,
                  const LinkOptions& options, ProjectLink& out, TempFiles& temps) {
    const auto shared = cxxprobe::project::shared_inputs(project.project_dir);
    Toolchain tool{.cxx = defaults.cxx,
                   .std_flag = defaults.std_flag,
                   .flags = defaults.flags,
                   .include_flags = {},
                   .working_dir = project.project_dir};
    if (shared.dir_exists) {
        tool.include_flags.push_back("-I" + shared.dir.string());
    }
    if (options.gtest) {
        tool.include_flags.push_back("-I" + embedded_gtest::resolve().include_dir.string());
    }
    std::vector<OwnedObject> units;
    units.reserve(shared.sources.size() + 1);
    for (const auto& source : shared.sources) {
        units.push_back({.owner = "shared", .object = source});
    }
    units.push_back({.owner = "entry", .object = entry});
    for (const auto& unit : units) {
        const auto object = temps.add("cxxprobe-common", ".o");
        const auto built = compile_object(tool, options, unit.object, "", object);
        if (!built.ok) {
            out.link = to_compile_report(built);
            return false;
        }
        out.objects.push_back({.owner = unit.owner, .object = object});
    }
    return true;
}

}  // namespace

std::string linked_summary(const std::vector<IntegrationPart>& parts) {
    std::string out;
    for (const auto& part : parts) {
        out += std::format("{}  part '{}' ({}): {}", out.empty() ? "" : "\n", part.label,
                           part.origin, part.source.empty() ? "nothing linked" : part.source);
    }
    return out;
}

ProjectLink link_project(const cxxprobe::project::ProjectConfig& project,
                         const cxxprobe::problem::ProjectDefaults& defaults,
                         const std::vector<PartSource>& sources, const fs::path& entry,
                         const LinkOptions& options, TempFiles& temps) {
    ProjectLink out;
    for (std::size_t i = 0; i < project.parts.size(); ++i) {
        const auto& declared = project.parts[i];
        const auto& chosen = source_for(sources, declared.label);
        const auto config = cxxprobe::problem::load_from_dir(project.project_dir / declared.dir);
        IntegrationPart part{.label = declared.label, .slug = config.slug};
        const auto resolved = cxxprobe::problem::resolve_compiler(config.compiler, defaults);
        const auto tool = part_toolchain(resolved, config.problem_dir);
        if (!build_part(project, tool, options, chosen, i,
                        config.problem_dir / config.attachments.dir, part, out, temps) ||
            !build_own_extras(tool, options, resolved, declared.label, out, temps)) {
            out.failed.push_back(declared.label);
        }
        out.parts.push_back(std::move(part));
    }
    if (!out.failed.empty() || !build_common(project, defaults, entry, options, out, temps)) {
        return out;
    }

    const auto binary = temps.add("cxxprobe-project");
    cxxprobe::compile::Request link;
    for (const auto& unit : out.objects) {
        link.sources.push_back(unit.object);
    }
    link.cxx = defaults.cxx;
    link.std_flag = defaults.std_flag;
    link.flags = defaults.flags;
    if (options.gtest) {
        link.extra_flags = {"-L" + embedded_gtest::resolve().lib_dir.string(), "-lgtest_main",
                            "-lgtest", "-lpthread"};
    }
    link.extra_flags.insert(link.extra_flags.end(), options.link_flags.begin(),
                            options.link_flags.end());
    link.output_binary = binary;
    link.working_dir = project.project_dir;
    const auto linked = cxxprobe::compile::compile(link);
    out.link = to_compile_report(linked);
    if (linked.ok) {
        out.binary = binary;
    }
    return out;
}

}  // namespace cxxprobe::judge::detail
