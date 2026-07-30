#include <fstream>
#include <sstream>

#include "cxxprobe/cases.hpp"
#include "cxxprobe/problem.hpp"

namespace cxxprobe::problem {

nlohmann::ordered_json preview_to_json(const ProblemConfig& config, const ProjectDefaults& defaults,
                                       const PreviewOptions& opts) {
    using Json = nlohmann::ordered_json;

    std::string statement;
    std::ifstream ifs(config.problem_dir / config.statement.dir / config.statement.entry,
                      std::ios::binary);
    if (ifs) {
        std::ostringstream ss;
        ss << ifs.rdbuf();
        statement = ss.str();
    }

    // v1 has no public/hidden test distinction — every configured manual
    // test is shown as a "sample", capped so a problem with a large hidden
    // suite doesn't dump all of it into a preview.
    std::vector<cxxprobe::cases::TestCase> sample_tests;
    if (config.tests.enabled) {
        try {
            sample_tests =
                config.tests.manifest
                    ? cxxprobe::cases::load_cases_manifest(config.problem_dir /
                                                           *config.tests.manifest)
                    : cxxprobe::cases::load_cases_dir(config.problem_dir / config.tests.dir);
        } catch (const std::exception&) {
            // Malformed test data shouldn't break a preview — just show none.
        }
        if (sample_tests.size() > opts.max_sample_tests) {
            sample_tests.resize(opts.max_sample_tests);
        }
    }

    Json j;
    j["slug"] = config.slug;
    j["name"] = config.name;
    j["statement_markdown"] = statement;

    cxxprobe::sandbox::Limits limits = resolve_limits(config.limits, defaults);
    Json limits_json;
    limits_json["memory_mb"] = limits.memory_bytes / (1024 * 1024);
    limits_json["cpu_ms"] = limits.cpu.count();
    limits_json["wall_ms"] = limits.wall.count();
    j["limits"] = std::move(limits_json);

    // v1 is C++-only judging — always "cpp".
    j["language"] = "cpp";

    Json samples = Json::array();
    for (const auto& tc : sample_tests) {
        Json sample;
        sample["label"] = tc.label;
        sample["input"] = tc.input_data;
        if (tc.answer_data) {
            sample["expected_output"] = *tc.answer_data;
        }
        samples.push_back(std::move(sample));
    }
    j["sample_tests"] = std::move(samples);

    return j;
}

}  // namespace cxxprobe::problem
