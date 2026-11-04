#include "cxxprobe/project.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <format>
#include <initializer_list>
#include <regex>
#include <set>
#include <stdexcept>
#include <string_view>

namespace cxxprobe::project {
namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(std::string_view field, std::string_view message) {
    throw std::runtime_error{std::format("project.yaml: {}: {}", field, message)};
}

bool implicit_non_string(const std::string& value) {
    // yaml-cpp keeps scalar spelling/tag, not a resolved JSON type. Match YAML
    // bool/numeric spellings before as<string>() can silently coerce them.
    static const std::regex boolean{
        R"(^(?:yes|Yes|YES|no|No|NO|true|True|TRUE|false|False|FALSE|on|On|ON|off|Off|OFF)$)"};
    static const std::regex numeric{
        R"(^(?:[-+]?0b[0-1_]+|[-+]?0[0-7_]+|[-+]?(?:0|[1-9][0-9_]*)|[-+]?0x[0-9a-fA-F_]+|[-+]?[1-9][0-9_]*(?::[0-5]?[0-9])+|[-+]?[0-9][0-9_]*\.[0-9_]*(?:[eE][-+][0-9]+)?|\.[0-9][0-9_]*(?:[eE][-+][0-9]+)?|[-+]?[0-9][0-9_]*(?::[0-5]?[0-9])+\.[0-9_]*|[-+]?\.(?:inf|Inf|INF)|\.(?:nan|NaN|NAN))$)"};
    static const std::regex timestamp{
        R"(^(?:[0-9]{4}-[0-9]{2}-[0-9]{2}|[0-9]{4}-[0-9]{1,2}-[0-9]{1,2}(?:[Tt]|[ \t]+)[0-9]{1,2}:[0-9]{2}:[0-9]{2}(?:\.[0-9]*)?(?:[ \t]*(?:Z|[-+][0-9]{1,2}(?::[0-9]{2})?))?)$)"};
    return std::regex_match(value, boolean) || std::regex_match(value, numeric) ||
           std::regex_match(value, timestamp);
}

std::string string_value(const YAML::Node& node, const std::string& field) {
    if (!node || !node.IsScalar()) {
        fail(field, "must be a string (quote numeric labels)");
    }
    const auto& tag = node.Tag();
    if (tag != "!" && tag != "tag:yaml.org,2002:str" &&
        (tag != "?" || implicit_non_string(node.Scalar()))) {
        fail(field, "must be a string (quote numeric labels)");
    }
    return node.Scalar();
}

void mapping(const YAML::Node& node, const std::string& field,
             std::initializer_list<std::string_view> allowed) {
    if (!node || !node.IsMap()) {
        fail(field, "must be a mapping");
    }
    std::set<std::string> seen;
    for (const auto& item : node) {
        const auto key = string_value(item.first, field + ".<key>");
        const auto qualified = field == "project.yaml" ? key : std::format("{}.{}", field, key);
        if (!seen.insert(key).second) {
            fail(qualified, "duplicate key");
        }
        if (std::ranges::find(allowed, key) == allowed.end()) {
            fail(qualified, "unknown field");
        }
    }
}

std::string relative_path(const YAML::Node& node, const std::string& field) {
    auto value = string_value(node, field);
    if (value.empty() || value.find_first_of("\\:") != std::string::npos ||
        std::ranges::any_of(value, [](unsigned char c) { return c < 32 || c == 127; })) {
        fail(field, "must be a normalized relative POSIX path");
    }
    std::string_view remaining{value};
    for (;;) {
        const auto slash = remaining.find('/');
        const auto component = remaining.substr(0, slash);
        if (component.empty() || component == "." || component == "..") {
            fail(field, "must be a normalized relative POSIX path");
        }
        if (slash == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(slash + 1);
    }
    return value;
}

// weakly_canonical alone treats dangling symlinks as nonexistent components.
// Expand symlinks ourselves, including targets with missing descendants, so an
// optional missing entry cannot hide an outside-project ancestor. Bounded like
// kernel pathname resolution; filesystem errors become field diagnostics.
fs::path resolved_path(const fs::path& path) {
    const auto absolute = fs::absolute(path);
    auto resolved = absolute.root_path();
    std::deque<fs::path> pending;
    for (const auto& component : absolute.relative_path()) {
        pending.push_back(component);
    }
    unsigned links = 0;
    while (!pending.empty()) {
        auto component = pending.front();
        pending.pop_front();
        if (component == "." || component.empty()) {
            continue;
        }
        if (component == "..") {
            resolved = resolved.parent_path();
            continue;
        }
        const auto candidate = resolved / component;
        std::error_code error;
        const auto status = fs::symlink_status(candidate, error);
        if (error && error != std::errc::no_such_file_or_directory) {
            throw fs::filesystem_error{"cannot inspect path", candidate, error};
        }
        if (!fs::is_symlink(status)) {
            resolved = candidate;
            continue;
        }
        if (++links > 40) {
            throw std::runtime_error{"symlink resolution limit exceeded"};
        }
        const auto target = fs::read_symlink(candidate);
        if (target.is_absolute()) {
            resolved = target.root_path();
        }
        std::vector<fs::path> components;
        for (const auto& part : target.relative_path()) {
            components.push_back(part);
        }
        pending.insert(pending.begin(), components.begin(), components.end());
    }
    return resolved;
}

bool within(const fs::path& path, const fs::path& base) {
    auto entry = path.begin();
    for (const auto& component : base) {
        if (entry == path.end() || *entry++ != component) {
            return false;
        }
    }
    return true;
}

fs::path contained(const fs::path& base, const fs::path& path, const std::string& field) {
    try {
        const auto resolved = resolved_path(path);
        if (!within(resolved, resolved_path(base))) {
            fail(field, "resolved path escapes its containing directory");
        }
        return resolved;
    } catch (const fs::filesystem_error& ex) {
        fail(field, ex.what());
    } catch (const std::runtime_error& ex) {
        if (std::string_view{ex.what()}.starts_with("project.yaml:")) {
            throw;
        }
        fail(field, ex.what());
    }
}

void file_kind(const fs::path& path, const std::string& field, bool directory, bool required) {
    try {
        if (!fs::exists(path)) {
            // A dangling link is not an absent optional file.
            if (required || fs::is_symlink(fs::symlink_status(path))) {
                fail(field, directory ? "directory does not exist" : "file does not exist");
            }
            return;
        }
        if (directory ? !fs::is_directory(path) : !fs::is_regular_file(path)) {
            fail(field, directory ? "must be a directory" : "must be a regular file");
        }
    } catch (const fs::filesystem_error& ex) {
        fail(field, ex.what());
    }
}

void no_nested_ancestor(const fs::path& root, const fs::path& part, const std::string& field) {
    // Check both the spelling declared in YAML and its canonical route.
    for (auto ancestor = part.parent_path(); ancestor != root;) {
        if (ancestor.empty() || ancestor == ancestor.parent_path()) {
            fail(field, "part ancestry escapes project");
        }
        if (fs::exists(ancestor / "project.yaml")) {
            fail(field, "nested projects are not permitted in part ancestors");
        }
        ancestor = ancestor.parent_path();
    }
}

bool unicode_space_only(std::string_view value) {
    // Unicode White_Space plus U+001C..U+001F, matching the schema's Python \S.
    // Exact UTF-8 spellings avoid locale-dependent isspace on individual bytes.
    constexpr std::array<std::string_view, 19> spaces = {
        "\xc2\x85",     "\xc2\xa0",     "\xe1\x9a\x80", "\xe2\x80\x80", "\xe2\x80\x81",
        "\xe2\x80\x82", "\xe2\x80\x83", "\xe2\x80\x84", "\xe2\x80\x85", "\xe2\x80\x86",
        "\xe2\x80\x87", "\xe2\x80\x88", "\xe2\x80\x89", "\xe2\x80\x8a", "\xe2\x80\xa8",
        "\xe2\x80\xa9", "\xe2\x80\xaf", "\xe2\x81\x9f", "\xe3\x80\x80",
    };
    while (!value.empty()) {
        const auto c = static_cast<unsigned char>(value.front());
        if (std::isspace(c) != 0 || (c >= 0x1c && c <= 0x1f)) {
            value.remove_prefix(1);
            continue;
        }
        const auto* const found =
            std::ranges::find_if(spaces, [value](auto space) { return value.starts_with(space); });
        if (found == std::end(spaces)) {
            return false;
        }
        value.remove_prefix(found->size());
    }
    return true;
}

void parse_shared(const YAML::Node& doc, ProjectConfig& cfg) {
    if (const auto shared = doc["shared"]) {
        mapping(shared, "shared", {"dir", "sources"});
        if (shared["dir"]) {
            cfg.shared.dir = relative_path(shared["dir"], "shared.dir");
        }
        if (const auto sources = shared["sources"]) {
            if (!sources.IsSequence()) {
                fail("shared.sources", "must be a list");
            }
            std::set<std::string> seen;
            for (std::size_t i = 0; i < sources.size(); ++i) {
                const auto field = std::format("shared.sources[{}]", i);
                auto source = relative_path(sources[i], field);
                if (!seen.insert(source).second) {
                    fail(field, "duplicate source path");
                }
                cfg.shared.sources.push_back(std::move(source));
            }
        }
    }
}

void parse_integration(const YAML::Node& doc, ProjectConfig& cfg) {
    if (const auto integration = doc["integration"]) {
        mapping(integration, "integration", {"dir", "entry", "fallback_dir"});
        if (integration["dir"]) {
            cfg.integration.dir = relative_path(integration["dir"], "integration.dir");
        }
        if (integration["entry"]) {
            cfg.integration.entry = relative_path(integration["entry"], "integration.entry");
        }
        if (integration["fallback_dir"]) {
            cfg.integration.fallback_dir =
                relative_path(integration["fallback_dir"], "integration.fallback_dir");
        }
    }
}

void parse_benchmark(const YAML::Node& doc, ProjectConfig& cfg) {
    if (const auto benchmark = doc["benchmark"]) {
        mapping(benchmark, "benchmark", {"dir", "entry", "input"});
        if (benchmark["dir"]) {
            cfg.benchmark.dir = relative_path(benchmark["dir"], "benchmark.dir");
        }
        if (benchmark["entry"]) {
            cfg.benchmark.entry = relative_path(benchmark["entry"], "benchmark.entry");
        }
        if (benchmark["input"]) {
            cfg.benchmark.input = relative_path(benchmark["input"], "benchmark.input");
        }
    }
}

void parse_parts(const YAML::Node& doc, ProjectConfig& cfg) {
    const auto parts = doc["parts"];
    if (!parts || !parts.IsSequence() || parts.size() == 0) {
        fail("parts", "must be a nonempty list");
    }
    std::set<std::string> labels;
    std::set<std::string> basenames;
    static const std::regex label_pattern{R"(^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$)"};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto field = std::format("parts[{}]", i);
        mapping(parts[i], field, {"dir", "label"});
        auto dir = relative_path(parts[i]["dir"], field + ".dir");
        auto label = string_value(parts[i]["label"], field + ".label");
        if (!std::regex_match(label, label_pattern)) {
            fail(field + ".label", "must match [A-Za-z0-9][A-Za-z0-9_-]{0,63}");
        }
        if (!labels.insert(label).second) {
            fail(field + ".label", "duplicate label");
        }
        if (!basenames.insert(fs::path{dir}.filename().string()).second) {
            fail(field + ".dir", "duplicate part basename");
        }
        cfg.parts.push_back(PartEntry{.dir = std::move(dir), .label = std::move(label)});
    }
}

SharedInputs check_shared(const ProjectConfig& cfg) {
    SharedInputs out;
    const auto shared_dir = cfg.project_dir / cfg.shared.dir;
    out.dir = contained(cfg.project_dir, shared_dir, "shared.dir");
    file_kind(shared_dir, "shared.dir", true, !cfg.shared.sources.empty());
    out.dir_exists = fs::is_directory(shared_dir);
    for (std::size_t i = 0; i < cfg.shared.sources.size(); ++i) {
        const auto field = std::format("shared.sources[{}]", i);
        const auto source = shared_dir / cfg.shared.sources[i];
        out.sources.push_back(contained(shared_dir, source, field));
        file_kind(source, field, false, true);
    }
    return out;
}

ProjectConfig parse(const fs::path& project_dir, bool check_filesystem) {
    ProjectConfig cfg;
    cfg.project_dir = fs::absolute(project_dir).lexically_normal();
    if (cfg.project_dir.filename().empty()) {
        cfg.project_dir = cfg.project_dir.parent_path();
    }
    cfg.slug = cfg.project_dir.filename().string();
    file_kind(cfg.project_dir, "project_dir", true, true);
    contained(cfg.project_dir, cfg.project_dir / "project.yaml", "project.yaml");
    file_kind(cfg.project_dir / "project.yaml", "project.yaml", false, true);
    if (fs::exists(cfg.project_dir / "problem.yaml")) {
        fail("project_dir", "a directory cannot contain both problem.yaml and project.yaml");
    }
    YAML::Node doc;
    try {
        doc = YAML::LoadFile((cfg.project_dir / "project.yaml").string());
    } catch (const YAML::Exception& ex) {
        fail("project.yaml", ex.what());
    }
    mapping(doc, "project.yaml",
            {"version", "name", "shared", "parts", "integration", "benchmark"});
    const auto version = doc["version"];
    if (!version || !version.IsScalar() ||
        (version.Tag() != "?" && version.Tag() != "tag:yaml.org,2002:int") ||
        !std::regex_match(version.Scalar(), std::regex{R"(^[+]?[0-9]+$)"})) {
        fail("version", "must be integer 1");
    }
    try {
        if (version.as<int>() != 1) {
            fail("version", "must be integer 1");
        }
    } catch (const YAML::Exception&) {
        fail("version", "must be integer 1");
    }
    cfg.name = string_value(doc["name"], "name");
    if (unicode_space_only(cfg.name)) {
        fail("name", "must contain a non-whitespace character");
    }
    parse_shared(doc, cfg);
    parse_integration(doc, cfg);
    parse_benchmark(doc, cfg);
    parse_parts(doc, cfg);
    if (!check_filesystem) {
        return cfg;
    }
    check_shared(cfg);
    const auto integration_dir = cfg.project_dir / cfg.integration.dir;
    contained(cfg.project_dir, integration_dir, "integration.dir");
    file_kind(integration_dir, "integration.dir", true, false);
    const auto entry = integration_dir / cfg.integration.entry;
    contained(integration_dir, entry, "integration.entry");
    file_kind(entry, "integration.entry", false, false);
    if (cfg.integration.fallback_dir) {
        const auto fallback = integration_dir / *cfg.integration.fallback_dir;
        contained(integration_dir, fallback, "integration.fallback_dir");
        file_kind(fallback, "integration.fallback_dir", true, false);
    }
    const auto benchmark_dir = cfg.project_dir / cfg.benchmark.dir;
    contained(cfg.project_dir, benchmark_dir, "benchmark.dir");
    file_kind(benchmark_dir, "benchmark.dir", true, false);
    contained(benchmark_dir, benchmark_dir / cfg.benchmark.entry, "benchmark.entry");
    file_kind(benchmark_dir / cfg.benchmark.entry, "benchmark.entry", false, false);
    contained(benchmark_dir, benchmark_dir / cfg.benchmark.input, "benchmark.input");
    file_kind(benchmark_dir / cfg.benchmark.input, "benchmark.input", false, false);
    for (std::size_t i = 0; i < cfg.parts.size(); ++i) {
        const auto field = std::format("parts[{}].dir", i);
        const auto part_dir = cfg.project_dir / cfg.parts[i].dir;
        const auto resolved_part = contained(cfg.project_dir, part_dir, field);
        no_nested_ancestor(cfg.project_dir, part_dir, field);
        no_nested_ancestor(resolved_path(cfg.project_dir), resolved_part, field);
        file_kind(part_dir, field, true, true);
        contained(part_dir, part_dir / "problem.yaml", field + ".problem.yaml");
        file_kind(part_dir / "problem.yaml", field + ".problem.yaml", false, true);
        if (fs::exists(part_dir / "project.yaml")) {
            fail(field, "nested projects are not permitted");
        }
    }
    return cfg;
}

}  // namespace

ProjectConfig load_from_dir(const fs::path& project_dir) {
    try {
        return parse(project_dir, true);
    } catch (const fs::filesystem_error& ex) {
        fail("project_dir", ex.what());
    } catch (const YAML::Exception& ex) {
        fail("project.yaml", ex.what());
    }
}

std::optional<std::string> declared_part_slug(const fs::path& project_dir,
                                              const fs::path& part_dir) {
    try {
        const auto cfg = parse(project_dir, false);
        const auto requested =
            (part_dir.is_absolute() ? part_dir : cfg.project_dir / part_dir).lexically_normal();
        for (std::size_t i = 0; i < cfg.parts.size(); ++i) {
            const auto declared = cfg.project_dir / cfg.parts[i].dir;
            if (declared == requested) {
                const auto field = std::format("parts[{}].dir", i);
                const auto resolved = contained(cfg.project_dir, requested, field);
                no_nested_ancestor(cfg.project_dir, requested, field);
                no_nested_ancestor(resolved_path(cfg.project_dir), resolved, field);
                file_kind(requested, field, true, true);
                contained(requested, requested / "problem.yaml", field + ".problem.yaml");
                file_kind(requested / "problem.yaml", field + ".problem.yaml", false, true);
                if (fs::exists(requested / "project.yaml")) {
                    fail(field, "nested projects are not permitted");
                }
                return cfg.slug + "/" + requested.filename().string();
            }
        }
        return std::nullopt;
    } catch (const fs::filesystem_error& ex) {
        fail("project_dir", ex.what());
    } catch (const YAML::Exception& ex) {
        fail("project.yaml", ex.what());
    }
}

SharedInputs shared_inputs(const fs::path& project_dir) {
    try {
        return check_shared(parse(project_dir, false));
    } catch (const fs::filesystem_error& ex) {
        fail("project_dir", ex.what());
    } catch (const YAML::Exception& ex) {
        fail("project.yaml", ex.what());
    }
}

problem::PackageReport validate_package(const fs::path& project_dir) {
    problem::PackageReport out;
    try {
        const auto cfg = load_from_dir(project_dir);
        for (std::size_t i = 0; i < cfg.parts.size(); ++i) {
            const auto& part = cfg.parts[i];
            const auto report = problem::validate_package(cfg.project_dir / part.dir);
            const auto prefix = std::format("parts[{}] (label '{}'): ", i, part.label);
            for (const auto& error : report.errors) {
                out.errors.push_back(prefix + error);
            }
            for (const auto& warning : report.warnings) {
                out.warnings.push_back(prefix + warning);
            }
        }
        out.ok = out.errors.empty();
    } catch (const std::exception& ex) {
        out.errors.emplace_back(ex.what());
    }
    return out;
}

}  // namespace cxxprobe::project
