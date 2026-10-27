#include <gtest/gtest.h>

#include <string>

#include "cxxprobe/judge.hpp"

// What the report promises the judging worker.
//
// The worker reads `stdout` off each I/O case and forwards it so a candidate
// can see what their program printed. It has done so since `df4ae20` — but
// `case_detail_to_json` never wrote the field, so every sample run came back
// with a verdict and an empty Output box. Of 1,216 I/O cases in the 24
// October contest, not one carried any output.
//
// Nothing caught it because the two sides were only coupled by a string key.
// These tests are that coupling, written down.

namespace {

using cxxprobe::judge::CaseDetail;
using cxxprobe::judge::JudgeReport;
using cxxprobe::judge::to_json;

JudgeReport report_with_one_case(const CaseDetail& detail) {
    JudgeReport report;
    report.manual.cases.push_back(detail);
    return report;
}

TEST(JudgeJsonCaseDetail, CarriesWhatTheProgramPrinted) {
    CaseDetail detail;
    detail.label = "1";
    detail.verdict = "WA";
    detail.stdout_text = "7 11\n";
    detail.stderr_text = "warning: something\n";

    const auto json = to_json(report_with_one_case(detail));
    const auto& c = json["tests"]["manual"]["cases"][0];

    ASSERT_TRUE(c.contains("stdout")) << "the worker reads this key; without it the "
                                         "candidate's Output box is always empty";
    EXPECT_EQ(c["stdout"].get<std::string>(), "7 11\n");
    EXPECT_EQ(c["stderr"].get<std::string>(), "warning: something\n");
}

TEST(JudgeJsonCaseDetail, EmitsAnEmptyOutputRatherThanOmittingIt) {
    // A program that printed nothing and a field that went missing look
    // identical downstream if the key is dropped when empty, and they mean
    // very different things to someone debugging.
    CaseDetail detail;
    detail.label = "1";
    detail.verdict = "WA";

    const auto json = to_json(report_with_one_case(detail));
    const auto& c = json["tests"]["manual"]["cases"][0];

    ASSERT_TRUE(c.contains("stdout"));
    EXPECT_EQ(c["stdout"].get<std::string>(), "");
    ASSERT_TRUE(c.contains("stderr"));
    EXPECT_EQ(c["stderr"].get<std::string>(), "");
}

}  // namespace
