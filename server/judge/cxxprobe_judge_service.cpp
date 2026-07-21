#include "server/judge/cxxprobe_judge_service.hpp"

namespace cxxprobe::server::judge {

cxxprobe::judge::JudgeReport CxxProbeJudgeService::judge(
    const cxxprobe::problem::ProblemConfig& config,
    const cxxprobe::problem::ProjectDefaults& defaults,
    const std::filesystem::path& submission_path) {
    return cxxprobe::judge::run_problem(config, defaults, submission_path);
}

}  // namespace cxxprobe::server::judge
