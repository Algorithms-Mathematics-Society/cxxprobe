#include "pipe_sync.hpp"

#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <stdexcept>

namespace cxxprobe::sandbox::detail {

// ── SyncEnd ────────────────────────────────────────────────────────────────

SyncEnd::~SyncEnd() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

SyncEnd::SyncEnd(SyncEnd&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }

namespace {

// Wire format constants — 5-byte datagram: [1-byte tag][4-byte LE payload].
// SOCK_SEQPACKET delivers exactly one datagram per send/recv pair.
constexpr std::size_t kWireSize = 5;
constexpr unsigned kByte = 8;
constexpr uint32_t kByteMask = 0xFFU;

std::array<uint8_t, kWireSize> encode(SyncMsg msg, int err_payload) {
    std::array<uint8_t, kWireSize> buf{};
    buf[0] = static_cast<uint8_t>(msg);
    uint32_t val{};
    std::memcpy(&val, &err_payload, sizeof(val));
    buf[1] = static_cast<uint8_t>(val & kByteMask);
    buf[2] = static_cast<uint8_t>((val >> kByte) & kByteMask);
    buf[3] = static_cast<uint8_t>((val >> (2 * kByte)) & kByteMask);
    buf[4] = static_cast<uint8_t>((val >> (3 * kByte)) & kByteMask);
    return buf;
}

int decode_payload(const std::array<uint8_t, kWireSize>& buf) {
    uint32_t val = static_cast<uint32_t>(buf[1]) | (static_cast<uint32_t>(buf[2]) << kByte) |
                   (static_cast<uint32_t>(buf[3]) << (2 * kByte)) |
                   (static_cast<uint32_t>(buf[4]) << (3 * kByte));
    int err{};
    std::memcpy(&err, &val, sizeof(err));
    return err;
}

}  // namespace

void SyncEnd::send(SyncMsg msg, int err_payload) const {
    auto buf = encode(msg, err_payload);
    ssize_t n = ::write(fd_, buf.data(), buf.size());
    if (n < 0) {
        throw std::runtime_error{std::format("SyncEnd::send write: {}", std::strerror(errno))};
    }
}

SyncMsg SyncEnd::recv() const {
    std::array<uint8_t, kWireSize> buf{};
    ssize_t n = ::read(fd_, buf.data(), buf.size());
    if (n < 0) {
        throw std::runtime_error{std::format("SyncEnd::recv read: {}", std::strerror(errno))};
    }
    if (n == 0) {
        throw std::runtime_error{"SyncEnd::recv: peer closed connection unexpectedly"};
    }
    if (static_cast<std::size_t>(n) != buf.size()) {
        throw std::runtime_error{std::format("SyncEnd::recv: short read ({}/{})", n, buf.size())};
    }

    auto msg = static_cast<SyncMsg>(buf[0]);
    if (msg == SyncMsg::Error) {
        throw std::runtime_error{std::format("SyncEnd::recv: peer reported error: {}",
                                             std::strerror(decode_payload(buf)))};
    }
    return msg;
}

// ── SyncChannel ────────────────────────────────────────────────────────────

SyncChannel::SyncChannel() : fds_{-1, -1} {
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, fds_.data()) < 0) {
        throw std::runtime_error{std::format("socketpair: {}", std::strerror(errno))};
    }
}

SyncChannel::~SyncChannel() {
    // fds that weren't transferred to a SyncEnd still need closing.
    for (int& fd : fds_) {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
    }
}

SyncEnd SyncChannel::parent_end() {
    // Parent takes fds_[0]; close fds_[1] so only the child inherits it.
    if (fds_[1] >= 0) {
        ::close(fds_[1]);
        fds_[1] = -1;
    }
    int fd = fds_[0];
    fds_[0] = -1;
    return SyncEnd{fd};
}

SyncEnd SyncChannel::child_end() {
    // Child takes fds_[1]; close fds_[0] so the parent doesn't leak it.
    if (fds_[0] >= 0) {
        ::close(fds_[0]);
        fds_[0] = -1;
    }
    int fd = fds_[1];
    fds_[1] = -1;
    return SyncEnd{fd};
}

std::pair<SyncEnd, SyncEnd> SyncChannel::split() {
    // Transfer both fds without closing either. Used with clone() where
    // both processes start with inherited copies of all fds; SOCK_CLOEXEC
    // ensures the fd each side doesn't own is closed on exec().
    int parent_fd = fds_[0];
    int child_fd = fds_[1];
    fds_[0] = -1;
    fds_[1] = -1;
    return {SyncEnd{parent_fd}, SyncEnd{child_fd}};
}

}  // namespace cxxprobe::sandbox::detail
