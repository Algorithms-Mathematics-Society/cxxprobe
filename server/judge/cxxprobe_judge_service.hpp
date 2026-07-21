#pragma once

#include "server/judge/ijudge_service.hpp"

namespace cxxprobe::server::judge {

class CxxProbeJudgeService final : public IJudgeService {
public:
    [[nodiscard]] cxxprobe::judge::JudgeReport judge(
        const cxxprobe::problem::ProblemConfig& config,
        const cxxprobe::problem::ProjectDefaults& defaults,
        const std::filesystem::path& submission_path) override;
};

}  // namespace cxxprobe::server::judge
