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
               std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    };
    if (!all_digits(num)) {
        throw std::runtime_error{std::format("invalid duration '{}' — use e.g. 2s, 500ms, 2000", raw)};
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
    for (const auto& entry : std::filesystem::directory_iterator{dir}) {
        if (entry.path().extension() == ".in") {
            return true;
        }
    }
    return false;
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
        throw std::runtime_error{std::format("problem config not found: {}", problem_yaml_path.string())};
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

    ProblemConfig cfg;
    cfg.problem_dir = std::filesystem::absolute(problem_yaml_path.parent_path());
    cfg.slug = cfg.problem_dir.filename().string();

    if (!doc["name"]) {
        throw std::runtime_error{"problem.yaml missing required 'name'"};
    }
    cfg.name = doc["name"].as<std::string>();

    if (doc["statement"]) {
        cfg.statement = doc["statement"].as<std::string>();
    }
    if (doc["solution"] && doc["solution"]["file"]) {
        cfg.solution_file = doc["solution"]["file"].as<std::string>();
    }

    // ── compiler ─────────────────────────────────────────────────────────
    if (YAML::Node c = doc["compiler"]) {
        if (c["cxx"] && !c["cxx"].IsNull()) {
            cfg.compiler.cxx = c["cxx"].as<std::string>();
        }
        if (c["std"] && !c["std"].IsNull()) {
            cfg.compiler.std_flag = c["std"].as<std::string>();
        }
        if (c["flags"] && !c["flags"].IsNull()) {
            std::vector<std::string> flags;
            for (const auto& f : c["flags"]) {
                flags.push_back(f.as<std::string>());
            }
            cfg.compiler.flags = std::move(flags);
        }
        if (c["extra_sources"]) {
            for (const auto& s : c["extra_sources"]) {
                cfg.compiler.extra_sources.push_back(s.as<std::string>());
            }
        }
    }

    // ── limits ───────────────────────────────────────────────────────────
    if (YAML::Node l = doc["limits"]) {
        if (l["memory_mb"] && !l["memory_mb"].IsNull()) {
            cfg.limits.memory_mb = l["memory_mb"].as<std::size_t>();
        }
        if (l["cpu"] && !l["cpu"].IsNull()) {
            cfg.limits.cpu = l["cpu"].as<std::string>();
        }
        if (l["wall"] && !l["wall"].IsNull()) {
            cfg.limits.wall = l["wall"].as<std::string>();
        }
        if (l["pids"] && !l["pids"].IsNull()) {
            cfg.limits.pids = l["pids"].as<unsigned>();
        }
    }

    // ── tests (consolidated type 1) ─────────────────────────────────────
    std::optional<bool> tests_enabled_explicit;
    if (YAML::Node t = doc["tests"]) {
        if (t["enabled"]) {
            tests_enabled_explicit = t["enabled"].as<bool>();
        }
        if (t["dir"]) {
            cfg.tests.dir = t["dir"].as<std::string>();
        }
        if (t["manifest"] && !t["manifest"].IsNull()) {
            cfg.tests.manifest = t["manifest"].as<std::string>();
        }
        if (t["checker"] && !t["checker"].IsNull()) {
            cfg.tests.checker = t["checker"].as<std::string>();
        }
    }
    if (doc["tests"] && doc["tests"]["dir"] && doc["tests"]["manifest"] &&
        !doc["tests"]["manifest"].IsNull()) {
        throw std::runtime_error{"problem.yaml: tests.dir and tests.manifest are mutually exclusive"};
    }
    bool manual_resource_present =
        cfg.tests.manifest ? std::filesystem::exists(cfg.problem_dir / *cfg.tests.manifest)
                            : dir_has_case_files(cfg.problem_dir / cfg.tests.dir);
    if (tests_enabled_explicit) {
        if (*tests_enabled_explicit && !manual_resource_present) {
            throw std::runtime_error{std::format(
                "problem.yaml: tests.enabled is true but no test cases found under '{}'",
                (cfg.tests.manifest ? *cfg.tests.manifest : cfg.tests.dir))};
        }
        cfg.tests.enabled = *tests_enabled_explicit;
    } else {
        cfg.tests.enabled = manual_resource_present;
    }

    // ── symbolic (consolidated type 2) ──────────────────────────────────
    std::optional<bool> symbolic_enabled_explicit;
    if (YAML::Node s = doc["symbolic"]) {
        if (s["enabled"]) {
            symbolic_enabled_explicit = s["enabled"].as<bool>();
        }
        cfg.symbolic.must_include = parse_symbolic_list(s["must_include"]);
        cfg.symbolic.must_not_include = parse_symbolic_list(s["must_not_include"]);
    }
    bool has_symbolic_checks = !cfg.symbolic.must_include.empty() || !cfg.symbolic.must_not_include.empty();
    if (symbolic_enabled_explicit) {
        if (*symbolic_enabled_explicit && !has_symbolic_checks) {
            throw std::runtime_error{
                "problem.yaml: symbolic.enabled is true but must_include/must_not_include are both empty"};
        }
        cfg.symbolic.enabled = *symbolic_enabled_explicit;
    } else {
        cfg.symbolic.enabled = has_symbolic_checks;
    }

    // ── behavior (consolidated type 3) ──────────────────────────────────
    std::optional<bool> behavior_enabled_explicit;
    if (YAML::Node b = doc["behavior"]) {
        if (b["enabled"]) {
            behavior_enabled_explicit = b["enabled"].as<bool>();
        }
        if (b["checker_file"]) {
            cfg.behavior.checker_file = b["checker_file"].as<std::string>();
        }
        if (b["extra_flags"]) {
            for (const auto& f : b["extra_flags"]) {
                cfg.behavior.extra_flags.push_back(f.as<std::string>());
            }
        }
    }
    bool checker_file_present = std::filesystem::exists(cfg.problem_dir / cfg.behavior.checker_file);
    if (behavior_enabled_explicit) {
        if (*behavior_enabled_explicit && !checker_file_present) {
            throw std::runtime_error{std::format(
                "problem.yaml: behavior.enabled is true but checker_file '{}' does not exist",
                cfg.behavior.checker_file)};
        }
        cfg.behavior.enabled = *behavior_enabled_explicit;
    } else {
        cfg.behavior.enabled = checker_file_present;
    }

    return cfg;
}

ProblemConfig load_from_dir(const std::filesystem::path& problem_dir) {
    return load(problem_dir / "problem.yaml");
}

ResolvedCompiler resolve_compiler(const CompilerConfig& override_cfg, const ProjectDefaults& defaults) {
    return ResolvedCompiler{
        .cxx = override_cfg.cxx.value_or(defaults.cxx),
        .std_flag = override_cfg.std_flag.value_or(defaults.std_flag),
        .flags = override_cfg.flags.value_or(defaults.flags),
        .extra_sources = override_cfg.extra_sources,
    };
}

cxxprobe::sandbox::Limits resolve_limits(const LimitsOverride& override_cfg, const ProjectDefaults& defaults) {
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
