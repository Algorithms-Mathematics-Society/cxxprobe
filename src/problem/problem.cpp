#include "cxxprobe/problem.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <format>
#include <stdexcept>

namespace cxxprobe::problem {

namespace {

// Accepts "2s", "500ms", "2000" (raw = ms). Mirrors the CLI's duration
// parser (cli/common/text_utils.cpp) — kept as a small private duplicate
// here rather than a shared dependency, since the two live in different
// subsystems (library vs. CLI) and the parser is a handful of lines.
std::chrono::milliseconds parse_duration_ms(const std::string& raw) {
    std::string_view sv{raw};
    std::string_view num;
    bool as_seconds = false;

    if (sv.size() >= 3 && sv.ends_with("ms")) {
        num = sv.substr(0, sv.size() - 2);
    } else if (sv.size() >= 2 && sv.back() == 's') {
        num = sv.substr(0, sv.size() - 1);
        as_seconds = true;
    } else {
        num = sv;
    }

    auto all_digits = [](std::string_view s) {
        return !s.empty() &&
               std::ranges::all_of(s, [](unsigned char c) { return std::isdigit(c) != 0; });
    };
    if (!all_digits(num)) {
        throw std::runtime_error{
            std::format("invalid duration '{}' — use e.g. 2s, 500ms, 2000", raw)};
    }

    unsigned long val = std::stoul(std::string{num});
    if (as_seconds) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::seconds{val});
    }
    return std::chrono::milliseconds{val};
}

std::vector<SymbolicCheck> parse_symbolic_list(const YAML::Node& node) {
    std::vector<SymbolicCheck> out;
    if (!node || !node.IsSequence()) {
        return out;
    }
    for (const auto& entry : node) {
        SymbolicCheck check;
        if (entry.IsScalar()) {
            check.pattern = entry.as<std::string>();
        } else if (entry.IsMap()) {
            if (!entry["pattern"]) {
                throw std::runtime_error{"symbolic check entry missing 'pattern'"};
            }
            check.pattern = entry["pattern"].as<std::string>();
            check.regex = entry["regex"] ? entry["regex"].as<bool>() : false;
            check.message = entry["message"] ? entry["message"].as<std::string>() : "";
        } else {
            throw std::runtime_error{"symbolic check entry must be a string or a map"};
        }
        out.push_back(std::move(check));
    }
    return out;
}

bool dir_has_case_files(const std::filesystem::path& dir) {
    if (!std::filesystem::is_directory(dir)) {
        return false;
    }
    std::filesystem::directory_iterator entries{dir};
    return std::ranges::any_of(entries,
                               [](const auto& entry) { return entry.path().extension() == ".in"; });
}

CompilerConfig parse_compiler_section(const YAML::Node& doc) {
    CompilerConfig compiler;
    YAML::Node c = doc["compiler"];
    if (!c) {
        return compiler;
    }
    if (c["cxx"] && !c["cxx"].IsNull()) {
        compiler.cxx = c["cxx"].as<std::string>();
    }
    if (c["std"] && !c["std"].IsNull()) {
        compiler.std_flag = c["std"].as<std::string>();
    }
    if (c["flags"] && !c["flags"].IsNull()) {
        std::vector<std::string> flags;
        for (const auto& f : c["flags"]) {
            flags.push_back(f.as<std::string>());
        }
        compiler.flags = std::move(flags);
    }
    if (c["extra_sources"]) {
        for (const auto& s : c["extra_sources"]) {
            compiler.extra_sources.push_back(s.as<std::string>());
        }
    }
    return compiler;
}

LimitsOverride parse_limits_section(const YAML::Node& doc) {
    LimitsOverride limits;
    YAML::Node l = doc["limits"];
    if (!l) {
        return limits;
    }
    if (l["memory_mb"] && !l["memory_mb"].IsNull()) {
        limits.memory_mb = l["memory_mb"].as<std::size_t>();
    }
    if (l["cpu"] && !l["cpu"].IsNull()) {
        limits.cpu = l["cpu"].as<std::string>();
    }
    if (l["wall"] && !l["wall"].IsNull()) {
        limits.wall = l["wall"].as<std::string>();
    }
    if (l["pids"] && !l["pids"].IsNull()) {
        limits.pids = l["pids"].as<unsigned>();
    }
    return limits;
}

// Throws if tests.dir and tests.manifest are both explicitly set, or if
// tests.enabled is explicitly true but no case data is actually present.
ManualTestsConfig parse_tests_section(const YAML::Node& doc,
                                      const std::filesystem::path& problem_dir) {
    ManualTestsConfig tests;
    std::optional<bool> enabled_explicit;
    YAML::Node t = doc["tests"];
    if (t) {
        if (t["enabled"]) {
            enabled_explicit = t["enabled"].as<bool>();
        }
        if (t["dir"]) {
            tests.dir = t["dir"].as<std::string>();
        }
        if (t["manifest"] && !t["manifest"].IsNull()) {
            tests.manifest = t["manifest"].as<std::string>();
        }
        if (t["checker"] && !t["checker"].IsNull()) {
            tests.checker = t["checker"].as<std::string>();
        }
        if (t["dir"] && t["manifest"] && !t["manifest"].IsNull()) {
            throw std::runtime_error{
                "problem.yaml: tests.dir and tests.manifest are mutually exclusive"};
        }
    }

    bool resource_present = tests.manifest ? std::filesystem::exists(problem_dir / *tests.manifest)
                                           : dir_has_case_files(problem_dir / tests.dir);
    if (enabled_explicit) {
        if (*enabled_explicit && !resource_present) {
            throw std::runtime_error{std::format(
                "problem.yaml: tests.enabled is true but no test cases found under '{}'",
                (tests.manifest ? *tests.manifest : tests.dir))};
        }
        tests.enabled = *enabled_explicit;
    } else {
        tests.enabled = resource_present;
    }
    return tests;
}

// Throws if symbolic.enabled is explicitly true but neither must_include nor
// must_not_include has any entries.
SymbolicConfig parse_symbolic_section(const YAML::Node& doc) {
    SymbolicConfig symbolic;
    std::optional<bool> enabled_explicit;
    YAML::Node s = doc["symbolic"];
    if (s) {
        if (s["enabled"]) {
            enabled_explicit = s["enabled"].as<bool>();
        }
        symbolic.must_include = parse_symbolic_list(s["must_include"]);
        symbolic.must_not_include = parse_symbolic_list(s["must_not_include"]);
    }

    bool has_checks = !symbolic.must_include.empty() || !symbolic.must_not_include.empty();
    if (enabled_explicit) {
        if (*enabled_explicit && !has_checks) {
            throw std::runtime_error{
                "problem.yaml: symbolic.enabled is true but must_include/must_not_include are both "
                "empty"};
        }
        symbolic.enabled = *enabled_explicit;
    } else {
        symbolic.enabled = has_checks;
    }
    return symbolic;
}

// Throws if behavior.enabled is explicitly true but checker_file doesn't
// exist on disk.
BehaviorConfig parse_behavior_section(const YAML::Node& doc,
                                      const std::filesystem::path& problem_dir) {
    BehaviorConfig behavior;
    std::optional<bool> enabled_explicit;
    YAML::Node b = doc["behavior"];
    if (b) {
        if (b["enabled"]) {
            enabled_explicit = b["enabled"].as<bool>();
        }
        if (b["checker_file"]) {
            behavior.checker_file = b["checker_file"].as<std::string>();
        }
        if (b["extra_flags"]) {
            for (const auto& f : b["extra_flags"]) {
                behavior.extra_flags.push_back(f.as<std::string>());
            }
        }
    }

    bool checker_present = std::filesystem::exists(problem_dir / behavior.checker_file);
    if (enabled_explicit) {
        if (*enabled_explicit && !checker_present) {
            throw std::runtime_error{std::format(
                "problem.yaml: behavior.enabled is true but checker_file '{}' does not exist",
                behavior.checker_file)};
        }
        behavior.enabled = *enabled_explicit;
    } else {
        behavior.enabled = checker_present;
    }
    return behavior;
}

}  // namespace

std::string slugify(std::string_view title) {
    std::string out;
    out.reserve(title.size());
    bool last_was_dash = false;
    for (unsigned char ch : title) {
        if (std::isalnum(ch) != 0) {
            out += static_cast<char>(std::tolower(ch));
            last_was_dash = false;
        } else if (!last_was_dash && !out.empty()) {
            out += '-';
            last_was_dash = true;
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out;
}

ProblemConfig load(const std::filesystem::path& problem_yaml_path) {
    if (!std::filesystem::exists(problem_yaml_path)) {
        throw std::runtime_error{
            std::format("problem config not found: {}", problem_yaml_path.string())};
    }

    YAML::Node doc = YAML::LoadFile(problem_yaml_path.string());
    if (!doc.IsMap()) {
        throw std::runtime_error{"problem.yaml must be a mapping"};
    }

    int version = doc["version"] ? doc["version"].as<int>() : 0;
    if (version != 1) {
        throw std::runtime_error{
            std::format("unsupported problem.yaml version {} (expected 1)", version)};
    }
    if (!doc["name"]) {
        throw std::runtime_error{"problem.yaml missing required 'name'"};
    }

    ProblemConfig cfg;
    cfg.problem_dir = std::filesystem::absolute(problem_yaml_path.parent_path());
    cfg.slug = cfg.problem_dir.filename().string();
    cfg.name = doc["name"].as<std::string>();

    if (doc["statement"]) {
        cfg.statement = doc["statement"].as<std::string>();
    }
    if (doc["solution"] && doc["solution"]["file"]) {
        cfg.solution_file = doc["solution"]["file"].as<std::string>();
    }

    cfg.compiler = parse_compiler_section(doc);
    cfg.limits = parse_limits_section(doc);
    cfg.tests = parse_tests_section(doc, cfg.problem_dir);
    cfg.symbolic = parse_symbolic_section(doc);
    cfg.behavior = parse_behavior_section(doc, cfg.problem_dir);

    return cfg;
}

ProblemConfig load_from_dir(const std::filesystem::path& problem_dir) {
    return load(problem_dir / "problem.yaml");
}

ResolvedCompiler resolve_compiler(const CompilerConfig& override_cfg,
                                  const ProjectDefaults& defaults) {
    return ResolvedCompiler{
        .cxx = override_cfg.cxx.value_or(defaults.cxx),
        .std_flag = override_cfg.std_flag.value_or(defaults.std_flag),
        .flags = override_cfg.flags.value_or(defaults.flags),
        .extra_sources = override_cfg.extra_sources,
    };
}

cxxprobe::sandbox::Limits resolve_limits(const LimitsOverride& override_cfg,
                                         const ProjectDefaults& defaults) {
    cxxprobe::sandbox::Limits limits = defaults.limits;
    if (override_cfg.memory_mb) {
        limits.memory_bytes = *override_cfg.memory_mb * 1024UL * 1024UL;
    }
    if (override_cfg.cpu) {
        limits.cpu = parse_duration_ms(*override_cfg.cpu);
    }
    if (override_cfg.wall) {
        limits.wall = parse_duration_ms(*override_cfg.wall);
    }
    if (override_cfg.pids) {
        limits.max_pids = *override_cfg.pids;
    }
    return limits;
}

}  // namespace cxxprobe::problem
