#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cxxprobe/judge.hpp"
#include "server/repository/submission_record.hpp"

namespace cxxprobe::server::repository {

// The only persistence seam in `cxxprobe serve` — SubmissionService reads
// through it, Worker writes through it. Swapping SQLite for PostgreSQL
// later is a new implementation of this interface, nothing else.
class ISubmissionRepository {
public:
    ISubmissionRepository() = default;
    virtual ~ISubmissionRepository() = default;
    ISubmissionRepository(const ISubmissionRepository&) = delete;
    ISubmissionRepository& operator=(const ISubmissionRepository&) = delete;
    ISubmissionRepository(ISubmissionRepository&&) = delete;
    ISubmissionRepository& operator=(ISubmissionRepository&&) = delete;

    // Generates and returns the new submission's id.
    [[nodiscard]] virtual std::string create_submission(const NewSubmission& s) = 0;
    virtual void update_status(const std::string& id, SubmissionStatus status) = 0;
    virtual void store_report(const std::string& id,
                              const cxxprobe::judge::JudgeReport& report) = 0;
    [[nodiscard]] virtual std::optional<SubmissionRecord> fetch_submission(
        const std::string& id) = 0;

    // Most recent submissions first, for the "submission history" UI
    // feature — a simple LIMIT, not the cursor-style pagination a
    // high-volume deployment would eventually want.
    [[nodiscard]] virtual std::vector<SubmissionRecord> list_recent(int limit) = 0;
};

}  // namespace cxxprobe::server::repository
