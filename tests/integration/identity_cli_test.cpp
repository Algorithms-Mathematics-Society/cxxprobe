#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef CXXPROBE_CLI_PATH
#error "CXXPROBE_CLI_PATH not defined — check CMakeLists"
#endif

#ifndef CXXPROBE_EXPECTED_VERSION
#error "CXXPROBE_EXPECTED_VERSION not defined — check CMakeLists"
#endif

#ifndef CXXPROBE_EXPECTED_COMMIT
#error "CXXPROBE_EXPECTED_COMMIT not defined — check CMakeLists"
#endif

#ifndef CXXPROBE_EXPECTED_DIRTY
#error "CXXPROBE_EXPECTED_DIRTY not defined — check CMakeLists"
#endif

namespace {

struct CliResult {
    int exit_code{-1};
    std::string stdout_text;
};

CliResult run_cli(const std::vector<std::string>& args) {
    std::array<int, 2> output_pipe{};
    if (::pipe(output_pipe.data()) != 0) {
        throw std::runtime_error{std::string{"pipe failed: "} + std::strerror(errno)};
    }

    const pid_t child = ::fork();
    if (child < 0) {
        ::close(output_pipe[0]);
        ::close(output_pipe[1]);
        throw std::runtime_error{std::string{"fork failed: "} + std::strerror(errno)};
    }
    if (child == 0) {
        ::close(output_pipe[0]);
        if (::dup2(output_pipe[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        ::close(output_pipe[1]);

        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(CXXPROBE_CLI_PATH));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        ::execv(CXXPROBE_CLI_PATH, argv.data());
        _exit(127);
    }

    ::close(output_pipe[1]);
    std::string output;
    std::array<char, 4096> buffer{};
    for (;;) {
        const ssize_t count = ::read(output_pipe[0], buffer.data(), buffer.size());
        if (count > 0) {
            output.append(buffer.data(), static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    ::close(output_pipe[0]);

    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            throw std::runtime_error{std::string{"waitpid failed: "} + std::strerror(errno)};
        }
    }
    return {.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1,
            .stdout_text = std::move(output)};
}

}  // namespace

TEST(IdentityCliTest, EmitsStableOneLineJson) {
    const CliResult result = run_cli({"identity", "--json"});
    ASSERT_EQ(result.exit_code, 0);

    const nlohmann::ordered_json expected{{"contract", "cxxprobe.engine-identity"},
                                          {"schema_version", 1},
                                          {"name", "cxxprobe"},
                                          {"version", CXXPROBE_EXPECTED_VERSION},
                                          {"commit", CXXPROBE_EXPECTED_COMMIT},
                                          {"dirty", CXXPROBE_EXPECTED_DIRTY}};
    EXPECT_EQ(result.stdout_text, expected.dump() + "\n");
}

TEST(IdentityCliTest, PreservesVersionFlag) {
    const CliResult result = run_cli({"--version"});
    ASSERT_EQ(result.exit_code, 0);
    EXPECT_EQ(result.stdout_text, std::string{"cxxprobe "} + CXXPROBE_EXPECTED_VERSION + "\n");
}
