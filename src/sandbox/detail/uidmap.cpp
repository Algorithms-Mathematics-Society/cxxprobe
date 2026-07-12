#include "uidmap.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cxxprobe::sandbox::detail {

namespace {

void write_proc_file(const std::string& path, std::string_view content) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::runtime_error{std::format("open {}: {}", path, std::strerror(errno))};
    }

    // /proc uid_map, gid_map, and setgroups files are written atomically by
    // the kernel; a single write() is both correct and required.
    ssize_t n = ::write(fd, content.data(), content.size());
    int saved_errno = errno;
    ::close(fd);

    if (n < 0) {
        throw std::runtime_error{std::format("write {}: {}", path, std::strerror(saved_errno))};
    }
    if (static_cast<std::size_t>(n) != content.size()) {
        throw std::runtime_error{
            std::format("write {}: short write ({}/{})", path, n, content.size())};
    }
}

}  // namespace

void write_uid_map(pid_t child_pid) {
    uid_t real_uid = ::getuid();
    write_proc_file(std::format("/proc/{}/uid_map", child_pid), std::format("0 {} 1\n", real_uid));
}

void write_gid_map(pid_t child_pid) {
    gid_t real_gid = ::getgid();

    // setgroups must be set to "deny" before writing gid_map when the caller
    // is unprivileged (kernel 3.19+). Required even if we never call setgroups.
    write_proc_file(std::format("/proc/{}/setgroups", child_pid), "deny");

    write_proc_file(std::format("/proc/{}/gid_map", child_pid), std::format("0 {} 1\n", real_gid));
}

}  // namespace cxxprobe::sandbox::detail
