#pragma once

#include <string>
#include <vector>

#include "pipe_sync.hpp"

namespace cxxprobe::sandbox::detail {

// File-descriptor bundle passed into the child process.
// All fds are inherited across clone(); the child closes the ones it doesn't own.
struct ChildFds {
    int stdin_read;    // read end of stdin pipe  (child reads from here)
    int stdout_write;  // write end of stdout pipe (child writes here)
    int stderr_write;  // write end of stderr pipe (child writes here)
};

// Arguments for the child-side entry point.
struct ChildArgs {
    std::vector<std::string> argv;
    ChildFds fds;
    SyncEnd sync;                       // child's half of the sync channel
    std::string cgroup_procs_path;      // absolute path to leaf's cgroup.procs
    std::size_t memory_limit_bytes{0};  // fallback RLIMIT_AS when cgroup migration is blocked
};

// Entry point executed inside the cloned process (user + mount namespace).
// Never returns normally — calls _exit() or exec().
// Called by the orchestrator's clone() callback.
[[noreturn]] void child_main(ChildArgs args);

}  // namespace cxxprobe::sandbox::detail
