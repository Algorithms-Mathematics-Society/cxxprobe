#pragma once

#include <sys/socket.h>

#include <array>
#include <cstdint>
#include <utility>

namespace cxxprobe::sandbox::detail {

// Typed messages exchanged over the sync channel.
// Sequence: parent clones → parent sends MapsWritten → child acks MapsAck
//           → parent sends ExecNow → child execs.
// Either side sends Error on failure (carries an errno value in payload).
enum class SyncMsg : uint8_t {
    MapsWritten = 1,
    MapsAck = 2,
    ExecNow = 3,
    ExecAck = 4,
    Error = 0xFF,
};

// Half of a SyncChannel — one file descriptor, owning (closes on destruction).
// Created by SyncChannel::parent_end() / ::child_end() after clone().
class SyncEnd {
public:
    explicit SyncEnd(int raw_fd) noexcept : fd_{raw_fd} {}
    ~SyncEnd();

    SyncEnd(const SyncEnd&) = delete;
    SyncEnd& operator=(const SyncEnd&) = delete;
    SyncEnd(SyncEnd&& other) noexcept;
    SyncEnd& operator=(SyncEnd&&) = delete;

    // Send a message, optionally with a 4-byte errno payload for Error.
    void send(SyncMsg msg, int err_payload = 0) const;

    // Block until a message arrives. Throws on unexpected message or I/O error.
    [[nodiscard]] SyncMsg recv() const;

private:
    int fd_;
};

// Created before clone(). Holds both ends of a SOCK_SEQPACKET socketpair.
// Call parent_end() in the parent and child_end() in the child (each call
// closes the opposite fd so only one end survives per process).
class SyncChannel {
public:
    SyncChannel();
    ~SyncChannel();

    SyncChannel(const SyncChannel&) = delete;
    SyncChannel& operator=(const SyncChannel&) = delete;
    SyncChannel(SyncChannel&&) = delete;
    SyncChannel& operator=(SyncChannel&&) = delete;

    // Use in fork()-based scenarios: each process calls its respective method,
    // which closes the opposite fd that process doesn't need.
    [[nodiscard]] SyncEnd parent_end();
    [[nodiscard]] SyncEnd child_end();

    // Use in clone()-based scenarios: returns both ends atomically without
    // closing anything. Both processes rely on SOCK_CLOEXEC to auto-close the
    // fd they don't own after exec(). The SyncChannel owns nothing after this.
    [[nodiscard]] std::pair<SyncEnd, SyncEnd> split();

private:
    std::array<int, 2> fds_;  // fds_[0] = parent, fds_[1] = child
};

}  // namespace cxxprobe::sandbox::detail
