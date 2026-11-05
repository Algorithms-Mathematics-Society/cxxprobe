#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "bench/detail/sha256.hpp"
#include "cxxprobe/sandbox.hpp"

namespace {
namespace fs = std::filesystem;

void write_file(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream{path} << text;
}

std::string quote(const std::string& text) {
    std::string result = "'";
    for (char c : text) {
        result += c == '\'' ? "'\\''" : std::string(1, c);
    }
    return result + "'";
}

struct Result {
    int code;
    std::string text;
};

bool sandbox_available() {
    static const bool available = [] {
        try {
            cxxprobe::sandbox::Limits limits;
            limits.wall = std::chrono::milliseconds{5000};
            return cxxprobe::sandbox::run({"/bin/true"}, "", limits).exit_code == 0;
        } catch (const std::exception&) {
            return false;
        }
    }();
    return available;
}

constexpr const char* kNoSandbox = "sandbox not available: needs user namespaces + writable cgroup";

// Two parts behind one header. Part 1 does nearly all the work, so a profile
// has an obvious hot part; part 2 is trivial.
constexpr const char* kHotIngest =
    "#include \"desk.hpp\"\n"
    "int ingest(int x) {\n"
    "    volatile int acc = 0;\n"
    "    for (int i = 0; i < 300; ++i) acc = acc + ((x * 31 + i) % 7);\n"
    "    return x * 2 + (acc - acc);\n"
    "}\n"
    "int main() { return 0; }\n";
constexpr const char* kBook =
    "#include \"desk.hpp\"\nint book(int x) { return x + 5; }\nint main() { return 0; }\n";

class BenchPipelineTest : public ::testing::Test {
protected:
    fs::path root;  // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)

    void SetUp() override {
        static unsigned counter = 0;
        root = fs::temp_directory_path() /
               std::format("cxxprobe-bench-pipeline-{}-{}", ::getpid(), counter++);
        write_file(root / "contest.yaml", "version: 1\nname: Round 2\n");
        const auto desk = root / "desk";
        write_file(desk / "project.yaml",
                   "version: 1\nname: Desk\nparts:\n  - {dir: parts/ingest, label: '1'}\n"
                   "  - {dir: parts/book, label: '2'}\n");
        write_file(desk / "shared/desk.hpp", "#pragma once\nint ingest(int);\nint book(int);\n");
        for (const auto* part : {"ingest", "book"}) {
            write_file(desk / "parts" / part / "problem.yaml",
                       std::format("version: 2\nname: {}\n", part));
        }
        write_file(desk / "parts/ingest/solutions/main.cpp", kHotIngest);
        write_file(desk / "parts/book/solutions/main.cpp", kBook);
        write_file(desk / "benchmark/bench_main.cpp",
                   "#include <cstdint>\n#include <iostream>\n#include \"desk.hpp\"\n"
                   "int main() {\n"
                   "    long n = 0;\n    std::cin >> n;\n    std::uint64_t total = 0;\n"
                   "    for (long i = 0; i < n; ++i) total += book(ingest(int(i % 1000)));\n"
                   "    std::cout << total << '\\n';\n    return 0;\n}\n");
        write_file(desk / "benchmark/input.txt", "400000\n");
    }
    void TearDown() override { fs::remove_all(root); }

    Result cli(const std::vector<std::string>& args) {
        std::string command = "cd " + quote(root.string()) + " && " + quote(CXXPROBE_CLI_PATH);
        for (const auto& arg : args) {
            command += " " + quote(arg);
        }
        command += " 2>&1";
        FILE* pipe = ::popen(command.c_str(), "r");
        if (pipe == nullptr) {
            throw std::runtime_error("cannot launch CLI");
        }
        std::ostringstream output;
        std::array<char, 4096> buffer{};
        while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output << buffer.data();
        }
        const int status = ::pclose(pipe);
        return {.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1, .text = output.str()};
    }

    static nlohmann::json parsed(const Result& result) {
        try {
            return nlohmann::json::parse(result.text);
        } catch (const std::exception&) {
            ADD_FAILURE() << result.text;
            return {};
        }
    }

    nlohmann::json bench(std::vector<std::string> extra, int expected_code) {
        std::vector<std::string> args{"bench", "project", "desk", "--json"};
        args.insert(args.end(), extra.begin(), extra.end());
        const auto result = cli(args);
        EXPECT_EQ(result.code, expected_code) << result.text;
        return parsed(result);
    }
};

TEST_F(BenchPipelineTest, OkRecordIsTheContractAndRepeatsWithinTolerance) {
    if (!sandbox_available()) {
        GTEST_SKIP() << kNoSandbox;
    }
    // A shared CI or dev host is noisier than a benchmark pool machine, so
    // this checks reproducibility at a looser gate, not the host's quiet.
    const std::vector<std::string> lenient{"--noise-threshold", "0.25", "--attempts", "5"};
    const auto first = bench(lenient, 0);
    const auto& record = first["result"];
    EXPECT_EQ(record.size(), 6U) << record.dump();
    EXPECT_EQ(record["status"], "OK");
    EXPECT_EQ(record["repetitions"], 7);
    EXPECT_TRUE(record["pinned_core"].is_number_integer());
    EXPECT_GT(record["median_ns"].get<double>(), 0);
    EXPECT_TRUE(record["instructions"].is_null() || record["instructions"].is_number_integer());
    EXPECT_EQ(first["input_sha256"], cxxprobe::bench::detail::sha256_hex("400000\n"));
    EXPECT_EQ(first["binary_sha256"].get<std::string>().size(), 64U);

    // The same binary benchmarked again agrees with itself. The tolerance is
    // the accepted noise of both batches plus a margin for a shared test host.
    const auto second = bench(lenient, 0);
    const double a = record["median_ns"];
    const double b = second["result"]["median_ns"];
    const double spread =
        (record["iqr_ns"].get<double>() / a) + (second["result"]["iqr_ns"].get<double>() / b);
    EXPECT_LE(std::abs(a - b) / a, spread + 0.15) << a << " vs " << b;
    EXPECT_EQ(first["binary_sha256"], second["binary_sha256"]);
}

TEST_F(BenchPipelineTest, ANoiseGateNoBatchCanPassReportsNoisyNotANumber) {
    if (!sandbox_available()) {
        GTEST_SKIP() << kNoSandbox;
    }
    const auto report =
        bench({"--noise-threshold", "0.0000000001", "--attempts", "2", "--repetitions", "3"}, 1);
    EXPECT_EQ(report["result"]["status"], "NOISY");
    EXPECT_EQ(report["attempts"].size(), 2U);
    // Diagnostic values are kept, but the status says they must not rank.
    EXPECT_GT(report["result"]["median_ns"].get<double>(), 0);
}

TEST_F(BenchPipelineTest, InstructionCountsAreStableAcrossRepetitionsWhenCounted) {
    if (!sandbox_available()) {
        GTEST_SKIP() << kNoSandbox;
    }
    const auto report = bench({"--noise-threshold", "0.25", "--attempts", "5"}, 0);
    const auto& counts = report["attempts"].back()["instructions"];
    if (counts.is_null()) {
        GTEST_SKIP() << "this host exposes no instruction counter";
    }
    std::vector<double> values(counts.begin(), counts.end());
    const auto [low, high] = std::ranges::minmax(values);
    ASSERT_GT(low, 0) << "an instruction event that never ran must be unavailable";
    EXPECT_LE((high - low) / low, 0.01);
}

TEST_F(BenchPipelineTest, WrongOutputOnTheBenchmarkInputIsNeverTimed) {
    if (!sandbox_available()) {
        GTEST_SKIP() << kNoSandbox;
    }
    write_file(root / "wrong.cpp",
               "#include \"desk.hpp\"\nint book(int x) { return x; }\nint main() { return 0; }\n");
    const auto malformed =
        cli({"bench", "project", "desk", "--submission", (root / "wrong.cpp").string()});
    EXPECT_EQ(malformed.code, 2);
    EXPECT_NE(malformed.text.find("expects <label>=<value>"), std::string::npos);
    const auto verified = bench({"--submission", "2=" + (root / "wrong.cpp").string()}, 1);
    EXPECT_EQ(verified["result"]["status"], "FAILED_VERIFY");
    EXPECT_TRUE(verified["result"]["median_ns"].is_null());
    EXPECT_EQ(verified["result"]["repetitions"], 0);
    EXPECT_TRUE(verified["attempts"].empty());
    EXPECT_NE(verified["verify_reason"].get<std::string>().find("differs from the reference"),
              std::string::npos);
}

TEST_F(BenchPipelineTest, APartThatDoesNotBuildFailsVerification) {
    if (!sandbox_available()) {
        GTEST_SKIP() << kNoSandbox;
    }
    write_file(root / "broken.cpp", "not C++\n");
    const auto report = bench({"--submission", "1=" + (root / "broken.cpp").string()}, 1);
    EXPECT_EQ(report["result"]["status"], "FAILED_VERIFY");
    EXPECT_EQ(report["verify_reason"], "a part does not compile");
    EXPECT_FALSE(report["parts"][0]["compiled"].get<bool>());
}

TEST_F(BenchPipelineTest, ProfileWritesAFlamegraphAndBlamesTheHotPart) {
    if (!sandbox_available()) {
        GTEST_SKIP() << kNoSandbox;
    }
    const auto report = bench(
        {"--profile", (root / "profile").string(), "--noise-threshold", "0.25", "--attempts", "5"},
        0);
    const auto& profile = report["profile"];
    ASSERT_FALSE(profile.contains("error")) << profile.dump();
    EXPECT_TRUE(fs::is_regular_file(root / "profile/flamegraph.svg"));
    EXPECT_TRUE(fs::is_regular_file(root / "profile/stacks.folded"));
    std::uint64_t total = 0;
    double hot = 0;
    for (const auto& owner : profile["owners"]) {
        total += owner["samples"].get<std::uint64_t>();
        if (owner["owner"] == "1") {
            hot = owner["share"];
            EXPECT_EQ(owner["top_symbols"][0]["symbol"], "ingest(int)");
        }
    }
    EXPECT_EQ(total, profile["samples"].get<std::uint64_t>());
    EXPECT_GT(hot, 0.8) << profile.dump();
}

TEST_F(BenchPipelineTest, CalibrationPassesAgainstItselfAndFailsOnDrift) {
    if (!sandbox_available()) {
        GTEST_SKIP() << kNoSandbox;
    }
    const auto free =
        cli({"bench", "calibrate", "--repetitions", "3", "--json", "--noise-threshold", "0.5"});
    ASSERT_EQ(free.code, 0) << free.text;
    const auto measured = parsed(free);
    EXPECT_TRUE(measured["ok"].get<bool>());
    const double median = measured["median_ns"];

    const auto same =
        cli({"bench", "calibrate", "--repetitions", "3", "--json", "--noise-threshold", "0.5",
             "--tolerance", "0.5", "--baseline-ns", std::format("{:.0f}", median)});
    EXPECT_EQ(same.code, 0) << same.text;
    const auto drifted =
        cli({"bench", "calibrate", "--repetitions", "3", "--json", "--noise-threshold", "0.5",
             "--baseline-ns", std::format("{:.0f}", median / 4)});
    EXPECT_EQ(drifted.code, 1) << drifted.text;
    const auto report = parsed(drifted);
    EXPECT_FALSE(report["ok"].get<bool>());
    EXPECT_GT(report["drift"].get<double>(), 1.0);
}

TEST_F(BenchPipelineTest, AHostAwayFromItsCalibrationBaselineIsNoisyNotANumber) {
    if (!sandbox_available()) {
        GTEST_SKIP() << kNoSandbox;
    }
    // An evenly slowed host (a busy hyperthread sibling) passes the IQR gate
    // with a tight spread around the wrong number; only the host check, the
    // calibration workload timed around the measurement, catches it.
    const auto calibrated = parsed(
        cli({"bench", "calibrate", "--repetitions", "3", "--json", "--noise-threshold", "0.5"}));
    const double baseline = calibrated["median_ns"];
    const std::vector<std::string> lenient{"--noise-threshold", "0.25", "--attempts", "5"};

    auto honest = lenient;
    honest.insert(honest.end(), {"--host-baseline-ns", std::format("{:.0f}", baseline),
                                 "--host-tolerance", "0.5"});
    const auto ok = bench(honest, 0);
    EXPECT_EQ(ok["result"]["status"], "OK") << ok.dump();
    EXPECT_LE(ok["host"]["drift"].get<double>(), 0.5);

    auto drifted = lenient;
    drifted.insert(drifted.end(), {"--host-baseline-ns", std::format("{:.0f}", baseline / 3)});
    const auto noisy = bench(drifted, 1);
    EXPECT_EQ(noisy["result"]["status"], "NOISY");
    EXPECT_GT(noisy["host"]["drift"].get<double>(), 1.0);
    EXPECT_NE(noisy["noise_reason"].get<std::string>().find("drifted"), std::string::npos);
    EXPECT_FALSE(noisy.contains("profile"));
}

TEST_F(BenchPipelineTest, AProjectWithoutABenchmarkEntrySaysSo) {
    fs::remove(root / "desk/benchmark/bench_main.cpp");
    const auto result = cli({"bench", "project", "desk", "--json"});
    EXPECT_EQ(result.code, 2);
    EXPECT_NE(result.text.find("has no benchmark entry"), std::string::npos) << result.text;
}

TEST_F(BenchPipelineTest, BenchmarkPathsAreValidatedLikeEveryOtherSection) {
    write_file(root / "desk/project.yaml",
               "version: 1\nname: Desk\nparts:\n  - {dir: parts/ingest, label: '1'}\n"
               "  - {dir: parts/book, label: '2'}\nbenchmark:\n  input: ../../outside.txt\n");
    const auto result = cli({"package", "validate", "desk", "--json"});
    EXPECT_EQ(result.code, 1) << result.text;
    EXPECT_NE(result.text.find("benchmark.input"), std::string::npos) << result.text;
}

}  // namespace
