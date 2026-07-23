#include "cxxprobe/judge.hpp"

#include <gtest/gtest.h>

#include <regex>
#include <string>

namespace {

using Json = nlohmann::ordered_json;
using cxxprobe::judge::EngineProvenance;
using cxxprobe::judge::JudgeReport;
using cxxprobe::judge::Status;
using cxxprobe::judge::engine_provenance;
using cxxprobe::judge::kJudgeReportSchemaVersion;
using cxxprobe::judge::to_json;

TEST(JudgeReportJson, EngineProvenanceIsMachineReadable) {
    const EngineProvenance provenance = engine_provenance();

    EXPECT_EQ(provenance.name, "cxxprobe");
    EXPECT_FALSE(provenance.version.empty());
    EXPECT_TRUE(provenance.commit == "unknown" ||
                std::regex_match(provenance.commit, std::regex{"[0-9a-fA-F]{40}"}));

    const Json json = to_json(JudgeReport{});
    EXPECT_TRUE(json.at("report_schema_version").is_number_unsigned());
    EXPECT_TRUE(json.at("engine").at("name").is_string());
    EXPECT_TRUE(json.at("engine").at("version").is_string());
    EXPECT_TRUE(json.at("engine").at("commit").is_string());
    EXPECT_TRUE(json.at("engine").at("dirty").is_boolean());
    EXPECT_TRUE(json.at("execution").at("compiler").at("cxx").is_string());
    EXPECT_TRUE(json.at("execution").at("compiler").at("std_flag").is_string());
    EXPECT_TRUE(json.at("execution").at("compiler").at("flags").is_array());
    EXPECT_TRUE(json.at("execution").at("compiler").at("extra_sources").is_array());
    EXPECT_TRUE(json.at("execution").at("limits").at("memory_bytes").is_number_unsigned());
    EXPECT_TRUE(json.at("execution").at("limits").at("cpu_time_ms").is_number_integer());
    EXPECT_TRUE(json.at("execution").at("limits").at("wall_time_ms").is_number_integer());
    EXPECT_TRUE(json.at("execution").at("limits").at("max_pids").is_number_unsigned());
}

TEST(JudgeReportJson, CanonicalShapeMatchesGolden) {
    const EngineProvenance provenance = engine_provenance();

    JudgeReport report;
    report.problem_name = "Sum Two Numbers";
    report.slug = "sum-two-numbers";
    report.submission_path = "/work/submission.cpp";
    report.overall = Status::Fail;

    report.manual.status = Status::Fail;
    report.manual.passed = 1;
    report.manual.total = 2;
    report.manual.cases.push_back({.label = "case-2",
                                   .verdict = "WA",
                                   .exit_code = 0,
                                   .cpu_time_ms = 8,
                                   .wall_time_ms = 11,
                                   .peak_memory_bytes = 4096});

    report.symbolic.status = Status::Pass;
    report.symbolic.checks.push_back({.pattern = "std::vector",
                                      .regex = false,
                                      .expect_present = true,
                                      .matched = true,
                                      .satisfied = true,
                                      .message = "required container"});

    report.behavior.status = Status::Skipped;
    report.solution_compile = {
        .ran = true, .ok = true, .exit_code = 0, .diagnostics = "warning: example"};
    report.execution.compiler = {.cxx = "/usr/bin/g++",
                                 .std_flag = "c++23",
                                 .flags = {"-O2", "-Wall"},
                                 .extra_sources = {"helper.cpp"}};
    report.execution.limits = {
        .memory_bytes = 268435456, .cpu_time_ms = 5000, .wall_time_ms = 10000, .max_pids = 64};

    Json expected;
    expected["report_schema_version"] = kJudgeReportSchemaVersion;
    expected["engine"] = {{"name", provenance.name},
                          {"version", provenance.version},
                          {"commit", provenance.commit},
                          {"dirty", provenance.dirty}};
    expected["execution"] = {
        {"compiler",
         {{"cxx", "/usr/bin/g++"},
          {"std_flag", "c++23"},
          {"flags", {"-O2", "-Wall"}},
          {"extra_sources", {"helper.cpp"}}}},
        {"limits",
         {{"memory_bytes", 268435456},
          {"cpu_time_ms", 5000},
          {"wall_time_ms", 10000},
          {"max_pids", 64}}}};
    expected["problem"] = "Sum Two Numbers";
    expected["slug"] = "sum-two-numbers";
    expected["submission"] = "/work/submission.cpp";
    expected["overall"] = "FAIL";
    expected["tests"] = {
        {"manual",
         {{"status", "FAIL"},
          {"passed", 1},
          {"total", 2},
          {"cases",
           {{{"label", "case-2"},
             {"verdict", "WA"},
             {"exit_code", 0},
             {"cpu_time_ms", 8},
             {"wall_time_ms", 11},
             {"peak_memory_bytes", 4096}}}}}},
        {"symbolic",
         {{"status", "PASS"},
          {"checks",
           {{{"kind", "must_include"},
             {"pattern", "std::vector"},
             {"regex", false},
             {"matched", true},
             {"satisfied", true},
             {"message", "required container"}}}}}},
        {"behavior",
         {{"status", "SKIPPED"}, {"passed", 0}, {"total", 0}, {"cases", Json::array()}}}};
    expected["compile"] = {
        {"solution", {{"ok", true}, {"exit_code", 0}, {"diagnostics", "warning: example"}}}};

    EXPECT_EQ(to_json(report), expected);
}

}  // namespace
