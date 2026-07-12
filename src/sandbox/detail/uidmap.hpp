#pragma once

#include <sys/types.h>

namespace cxxprobe::sandbox::detail {

// Write uid_map and gid_map for a child that was cloned with CLONE_NEWUSER.
// Maps uid/gid 0 inside the namespace to the caller's real uid/gid outside.
//
// Must be called by the PARENT after clone() returns, before the child
// proceeds past its synchronisation point. On Linux 3.19+, setgroups must
// be set to "deny" before gid_map is written; this is handled internally.
void write_uid_map(pid_t child_pid);
void write_gid_map(pid_t child_pid);

}  // namespace cxxprobe::sandbox::detail
