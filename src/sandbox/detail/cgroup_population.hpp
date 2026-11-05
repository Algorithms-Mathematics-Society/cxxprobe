#pragma once

#include <sys/types.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace cxxprobe::sandbox::detail {

// At the exec gate the trusted child is the only possible member of this new
// leaf. Verify the resulting membership, not merely a successful write attempt.
inline void require_cgroup_member(const std::filesystem::path& path, pid_t child) {
    std::ifstream procs{path / "cgroup.procs"};
    pid_t member = 0;
    if (!(procs >> member) || member != child) {
        throw std::runtime_error{"sandbox: child did not join its cgroup"};
    }
}

// cgroup.events can remain populated after waitpid reaps the last process.
// cgroup.procs excludes zombies, and is the evidence for live work. A nested
// cgroup may hide processes from this file; treat it conservatively as live
// rather than walking a workload-controlled directory tree without a bound.
inline bool has_live_processes(const std::filesystem::path& path) {
    std::ifstream procs{path / "cgroup.procs"};
    if (!procs) {
        throw std::runtime_error{"sandbox: cannot read cgroup processes"};
    }
    pid_t pid = 0;
    if (procs >> pid) {
        return true;
    }
    if (!procs.eof()) {
        throw std::runtime_error{"sandbox: invalid cgroup process list"};
    }
    std::ifstream stats{path / "cgroup.stat"};
    std::string key;
    std::size_t count = 0;
    while (stats >> key >> count) {
        if (key == "nr_descendants") {
            return count != 0;
        }
    }
    throw std::runtime_error{"sandbox: cannot read cgroup descendants"};
}

}  // namespace cxxprobe::sandbox::detail
