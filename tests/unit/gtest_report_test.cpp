#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "cxxprobe/gtest_report.hpp"

using cxxprobe::gtest_report::all_passed;
using cxxprobe::gtest_report::parse_file;
using cxxprobe::gtest_report::parse_string;

namespace {

constexpr const char* kPassingSample = R"({
  "tests": 1, "failures": 0, "disabled": 0, "errors": 0, "time": "0.005s", "name": "AllTests",
  "testsuites": [
    { "name": "Suite", "tests": 1, "failures": 0, "disabled": 0, "errors": 0, "time": "0.005s",
      "testsuite": [
        { "name": "Case1", "status": "RUN", "result": "COMPLETED", "time": "0.005s", "classname": "Suite" }
      ]
    }
  ]
})";

constexpr const char* kFailingSample = R"({
  "tests": 2, "failures": 1, "disabled": 0, "errors": 0, "time": "0.01s", "name": "AllTests",
  "testsuites": [
    { "name": "Suite", "tests": 2, "failures": 1, "disabled": 0, "errors": 0, "time": "0.01s",
      "testsuite": [
        { "name": "WillFail", "status": "RUN", "result": "COMPLETED", "time": "0.002s", "classname": "Suite",
          "failures": [ { "failure": "expected 1 got 2", "type": "" } ] },
        { "name": "WillPass", "status": "RUN", "result": "COMPLETED", "time": "0.008s", "classname": "Suite" }
      ]
    }
  ]
})";

}  // namespace

TEST(GtestReportParse, PassingReportParsesCorrectly) {
    auto report = parse_string(kPassingSample);
    EXPECT_EQ(report.tests, 1);
    EXPECT_EQ(report.failures, 0);
    ASSERT_EQ(report.cases.size(), 1U);
    EXPECT_EQ(report.cases[0].suite, "Suite");
    EXPECT_EQ(report.cases[0].name, "Case1");
    EXPECT_FALSE(report.cases[0].failed);
    EXPECT_DOUBLE_EQ(report.cases[0].time_seconds, 0.005);
    EXPECT_TRUE(all_passed(report));
}

TEST(GtestReportParse, FailingReportFlagsFailedCase) {
    auto report = parse_string(kFailingSample);
    EXPECT_EQ(report.tests, 2);
    EXPECT_EQ(report.failures, 1);
    ASSERT_EQ(report.cases.size(), 2U);
    EXPECT_TRUE(report.cases[0].failed);
    ASSERT_EQ(report.cases[0].failure_messages.size(), 1U);
    EXPECT_EQ(report.cases[0].failure_messages[0], "expected 1 got 2");
    EXPECT_FALSE(report.cases[1].failed);
    EXPECT_FALSE(all_passed(report));
}

TEST(GtestReportParse, MultipleSuitesFlattenedCorrectly) {
    constexpr const char* kTwoSuites = R"({
      "tests": 2, "failures": 0, "disabled": 0, "errors": 0, "time": "0s", "name": "AllTests",
      "testsuites": [
        { "name": "A", "tests": 1, "failures": 0, "disabled": 0, "errors": 0, "time": "0s",
          "testsuite": [ { "name": "One", "status": "RUN", "result": "COMPLETED", "time": "0s", "classname": "A" } ] },
        { "name": "B", "tests": 1, "failures": 0, "disabled": 0, "errors": 0, "time": "0s",
          "testsuite": [ { "name": "Two", "status": "RUN", "result": "COMPLETED", "time": "0s", "classname": "B" } ] }
      ]
    })";
    auto report = parse_string(kTwoSuites);
    ASSERT_EQ(report.cases.size(), 2U);
    EXPECT_EQ(report.cases[0].suite, "A");
    EXPECT_EQ(report.cases[1].suite, "B");
}

TEST(GtestReportParse, MalformedJsonThrows) { EXPECT_THROW(parse_string("{not valid json"), std::runtime_error); }

TEST(GtestReportParse, TopLevelNonObjectThrows) { EXPECT_THROW(parse_string("[1,2,3]"), std::runtime_error); }

TEST(GtestReportParse, MissingFileThrows) {
    EXPECT_THROW(parse_file("/nonexistent/path/results.json"), std::runtime_error);
}

TEST(GtestReportParse, ParseFileRoundTrip) {
    std::filesystem::path tmp = std::filesystem::temp_directory_path() / "cxxprobe-gtest-report-test.json";
    {
        std::ofstream ofs{tmp};
        ofs << kPassingSample;
    }
    auto report = parse_file(tmp);
    EXPECT_EQ(report.tests, 1);
    std::filesystem::remove(tmp);
}
