#pragma once

#include <filesystem>

#include "cxxprobe/judge.hpp"
#include "cxxprobe/problem.hpp"

namespace cxxprobe::server::judge {

// Thin seam over cxxprobe::judge::run_problem so Worker depends on an
// interface, not a free function directly — leaves room for caching,
// metrics wrapping, or routing to a distributed judge pool later without
// touching Worker/WorkerManager.
class IJudgeService {
public:
    IJudgeService() = default;
    virtual ~IJudgeService() = default;
    IJudgeService(const IJudgeService&) = delete;
    IJudgeService& operator=(const IJudgeService&) = delete;
    IJudgeService(IJudgeService&&) = delete;
    IJudgeService& operator=(IJudgeService&&) = delete;

    [[nodiscard]] virtual cxxprobe::judge::JudgeReport judge(
        const cxxprobe::problem::ProblemConfig& config,
        const cxxprobe::problem::ProjectDefaults& defaults,
        const std::filesystem::path& submission_path) = 0;
};

}  // namespace cxxprobe::server::judge
