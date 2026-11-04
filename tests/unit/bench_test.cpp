#include "cxxprobe/bench.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "bench/detail/profile.hpp"
#include "bench/detail/sha256.hpp"

namespace {

using cxxprobe::bench::quantile;

TEST(BenchQuantile, MatchesLinearInterpolationType7) {
    // numpy.quantile([1, 2, 3, 4], [0.25, 0.5, 0.75]) == [1.75, 2.5, 3.25]
    const std::vector<double> even{4, 1, 3, 2};
    EXPECT_DOUBLE_EQ(quantile(even, 0.25), 1.75);
    EXPECT_DOUBLE_EQ(quantile(even, 0.5), 2.5);
    EXPECT_DOUBLE_EQ(quantile(even, 0.75), 3.25);
    // Seven values, the default repetition count: the median is the 4th.
    const std::vector<double> seven{7, 1, 6, 2, 5, 3, 4};
    EXPECT_DOUBLE_EQ(quantile(seven, 0.5), 4);
    EXPECT_DOUBLE_EQ(quantile(seven, 0.75) - quantile(seven, 0.25), 3);
    EXPECT_DOUBLE_EQ(quantile({42}, 0.5), 42);
}

TEST(BenchSha256, KnownVectors) {
    using cxxprobe::bench::detail::sha256_hex;
    EXPECT_EQ(sha256_hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256_hex("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // Two blocks: 56 bytes forces the length into a second block.
    EXPECT_EQ(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(BenchFlamegraph, OneRectPerFrameWithEscapedTitles) {
    const auto svg = cxxprobe::bench::detail::flamegraph_svg(
        {{"main;solve<int>;hot", 90}, {"main;cold", 10}}, "100 samples");
    EXPECT_NE(svg.find("<svg"), std::string::npos);
    EXPECT_NE(svg.find("solve&lt;int&gt; (90 samples)"), std::string::npos);
    EXPECT_NE(svg.find("cold (10 samples)"), std::string::npos);
    EXPECT_NE(svg.find("all (100 samples)"), std::string::npos);
    std::size_t rects = 0;
    for (auto at = svg.find("<rect"); at != std::string::npos; at = svg.find("<rect", at + 1)) {
        ++rects;
    }
    EXPECT_EQ(rects, 5U);  // all, main, solve, hot, cold
}

TEST(BenchPlacement, ChoosesAnAllowedCpuAndRejectsOthers) {
    const auto chosen = cxxprobe::bench::place(-1);
    EXPECT_GE(chosen.cpu, 0);
    for (int sibling : chosen.siblings) {
        EXPECT_NE(sibling, chosen.cpu);
    }
    EXPECT_EQ(cxxprobe::bench::place(chosen.cpu).cpu, chosen.cpu);
    EXPECT_THROW(cxxprobe::bench::place(4095), std::runtime_error);
}

}  // namespace
