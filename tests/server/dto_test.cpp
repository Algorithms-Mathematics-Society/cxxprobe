#include "server/api/dto.hpp"

#include <gtest/gtest.h>

using cxxprobe::server::api::BadRequestError;
using cxxprobe::server::repository::SubmissionRecord;
using cxxprobe::server::repository::SubmissionStatus;
using cxxprobe::server::services::ProblemSummary;

TEST(DtoTest, HealthToJsonHasExpectedFields) {
    auto j = cxxprobe::server::api::health_to_json(true, 3, 4, 2, 120);
    EXPECT_EQ(j["status"], "ok");
    EXPECT_EQ(j["workers"]["active"], 3);
    EXPECT_EQ(j["workers"]["total"], 4);
    EXPECT_EQ(j["queue_depth"], 2);
    EXPECT_EQ(j["uptime_seconds"], 120);
}

TEST(DtoTest, ProblemsListToJsonWrapsSummaries) {
    std::vector<ProblemSummary> problems{{.slug = "a-warmup", .name = "A: Warmup"},
                                         {.slug = "b-graph", .name = "B: Graph"}};
    auto j = cxxprobe::server::api::problems_list_to_json(problems);
    ASSERT_EQ(j["problems"].size(), 2U);
    EXPECT_EQ(j["problems"][0]["slug"], "a-warmup");
    EXPECT_EQ(j["problems"][1]["name"], "B: Graph");
}

TEST(DtoTest, SubmissionRecordToJsonEmbedsReportAsNestedObject) {
    SubmissionRecord rec;
    rec.id = "sub-1";
    rec.problem_slug = "a-warmup";
    rec.status = SubmissionStatus::Finished;
    rec.created_at = "2026-01-01T00:00:00Z";
    rec.finished_at = "2026-01-01T00:00:05Z";
    rec.report_json = R"({"overall":"PASS"})";

    auto j = cxxprobe::server::api::submission_record_to_json(rec);
    EXPECT_EQ(j["status"], "finished");
    EXPECT_EQ(j["report"]["overall"], "PASS");
    EXPECT_TRUE(j["report"].is_object());
}

TEST(DtoTest, SubmissionRecordToJsonReportIsNullWhenNotYetJudged) {
    SubmissionRecord rec;
    rec.id = "sub-1";
    rec.status = SubmissionStatus::Queued;
    auto j = cxxprobe::server::api::submission_record_to_json(rec);
    EXPECT_TRUE(j["report"].is_null());
    EXPECT_FALSE(j.contains("finished_at"));
}

TEST(DtoTest, ParseSubmitRequestExtractsFields) {
    auto req = cxxprobe::server::api::parse_submit_request(
        R"({"problem_slug":"a-warmup","language":"cpp","source":"int main(){}"})");
    EXPECT_EQ(req.problem_slug, "a-warmup");
    EXPECT_EQ(req.language, "cpp");
    EXPECT_EQ(req.source, "int main(){}");
}

TEST(DtoTest, ParseSubmitRequestLanguageOptional) {
    auto req =
        cxxprobe::server::api::parse_submit_request(R"({"problem_slug":"a-warmup","source":"x"})");
    EXPECT_EQ(req.language, "");
}

TEST(DtoTest, ParseSubmitRequestThrowsOnMalformedJson) {
    EXPECT_THROW(cxxprobe::server::api::parse_submit_request("not json"), BadRequestError);
}

TEST(DtoTest, ParseSubmitRequestThrowsOnMissingProblemSlug) {
    EXPECT_THROW(cxxprobe::server::api::parse_submit_request(R"({"source":"x"})"),
                BadRequestError);
}

TEST(DtoTest, ParseSubmitRequestThrowsOnMissingSource) {
    EXPECT_THROW(
        cxxprobe::server::api::parse_submit_request(R"({"problem_slug":"a-warmup"})"),
        BadRequestError);
}
