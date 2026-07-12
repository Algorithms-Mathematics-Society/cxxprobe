#include "detail/pipe_sync.hpp"

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>

using cxxprobe::sandbox::detail::SyncChannel;
using cxxprobe::sandbox::detail::SyncEnd;
using cxxprobe::sandbox::detail::SyncMsg;

// ── helpers ──────────────────────────────────────────────────────────────────

// Fork, run fn in child (fn receives child_end), wait for child.
// Returns child exit status.
template <typename F>
static int with_child(SyncChannel& ch, F fn) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        SyncEnd child = ch.child_end();
        fn(child);
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}

// ── SyncChannel construction / RAII ─────────────────────────────────────────

TEST(SyncChannel, ConstructAndDestroyNoThrow) { EXPECT_NO_THROW(SyncChannel ch); }

TEST(SyncChannel, ParentEndAndChildEndNoThrow) {
    SyncChannel ch;
    EXPECT_NO_THROW({
        SyncEnd parent = ch.parent_end();
        (void)parent;
    });
}

// ── round-trip ───────────────────────────────────────────────────────────────

TEST(SyncChannel, RoundTripAllMessageTypes) {
    SyncChannel ch;

    int ws = with_child(ch, [](SyncEnd& child) {
        child.send(SyncMsg::MapsWritten);
        child.send(SyncMsg::MapsAck);
        child.send(SyncMsg::ExecNow);
        child.send(SyncMsg::ExecAck);
    });

    SyncEnd parent = ch.parent_end();
    EXPECT_EQ(parent.recv(), SyncMsg::MapsWritten);
    EXPECT_EQ(parent.recv(), SyncMsg::MapsAck);
    EXPECT_EQ(parent.recv(), SyncMsg::ExecNow);
    EXPECT_EQ(parent.recv(), SyncMsg::ExecAck);

    EXPECT_TRUE(WIFEXITED(ws));
    EXPECT_EQ(WEXITSTATUS(ws), 0);
}

// ── bidirectional ─────────────────────────────────────────────────────────────

TEST(SyncChannel, BidirectionalExchange) {
    SyncChannel ch;

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        SyncEnd child = ch.child_end();
        SyncMsg msg = child.recv();  // wait for MapsWritten
        if (msg == SyncMsg::MapsWritten) {
            child.send(SyncMsg::MapsAck);
        }
        _exit(0);
    }

    SyncEnd parent = ch.parent_end();
    parent.send(SyncMsg::MapsWritten);
    SyncMsg reply = parent.recv();
    EXPECT_EQ(reply, SyncMsg::MapsAck);

    int ws = 0;
    waitpid(pid, &ws, 0);
    EXPECT_EQ(WEXITSTATUS(ws), 0);
}

// ── Error message ─────────────────────────────────────────────────────────────

TEST(SyncChannel, ErrorPayloadThrowsOnRecv) {
    SyncChannel ch;

    int ws = with_child(ch, [](SyncEnd& child) { child.send(SyncMsg::Error, ENOMEM); });

    SyncEnd parent = ch.parent_end();
    EXPECT_THROW((void)parent.recv(), std::runtime_error);

    // Verify child exited cleanly (error was communicated via message, not crash).
    EXPECT_TRUE(WIFEXITED(ws));
}

TEST(SyncChannel, ErrorPayloadMessageContainsErrno) {
    SyncChannel ch;

    with_child(ch, [](SyncEnd& child) { child.send(SyncMsg::Error, EACCES); });

    SyncEnd parent = ch.parent_end();
    try {
        (void)parent.recv();
        FAIL() << "expected runtime_error";
    } catch (const std::runtime_error& ex) {
        // The error message should contain the strerror for EACCES.
        EXPECT_NE(std::string{ex.what()}.find("Permission denied"), std::string::npos);
    }
}

// ── peer closed ───────────────────────────────────────────────────────────────

TEST(SyncChannel, PeerClosedThrowsOnRecv) {
    SyncChannel ch;

    pid_t pid = fork();
    ASSERT_GE(pid, 0);

    if (pid == 0) {
        // Get child end and immediately destroy — closes the socket.
        { SyncEnd child = ch.child_end(); }
        _exit(0);
    }

    // Wait for child so its fd is fully closed.
    int ws = 0;
    waitpid(pid, &ws, 0);

    // parent_end() also closes parent's copy of child's fd.
    SyncEnd parent = ch.parent_end();

    // Both copies of child's fd are closed → recv sees EOF → throws.
    EXPECT_THROW((void)parent.recv(), std::runtime_error);
}

// ── SyncEnd move ──────────────────────────────────────────────────────────────

TEST(SyncEnd, MoveTransfersOwnershipNoDoubleFree) {
    SyncChannel ch;
    SyncEnd a = ch.parent_end();
    SyncEnd b = std::move(a);
    // Destructor of moved-from `a` (fd_==-1) must not close anything.
    // Destructor of `b` closes the fd. No double-close = no crash.
}

TEST(SyncEnd, MovedFromCanBeDestroyed) {
    SyncChannel ch;
    {
        SyncEnd a = ch.parent_end();
        SyncEnd b = std::move(a);
        // `a` goes out of scope here — must not crash.
    }
    // `b` goes out of scope here — must not crash.
}
