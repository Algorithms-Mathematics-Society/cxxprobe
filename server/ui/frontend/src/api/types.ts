// Mirrors the JSON shapes produced by server/api/dto.cpp and
// cxxprobe::judge::to_json (src/judge/judge_json.cpp) — kept in sync by
// hand since the backend has no OpenAPI/schema generation (yet).

export type Status = "PASS" | "FAIL" | "SKIPPED" | "ERROR";
export type SubmissionStatus = "queued" | "running" | "finished" | "error";

export interface ProblemSummary {
  slug: string;
  name: string;
}

export interface SampleTest {
  label: string;
  input: string;
  expected_output?: string;
}

export interface ProblemLimits {
  memory_mb: number;
  cpu_ms: number;
  wall_ms: number;
}

export interface ProblemDetail {
  slug: string;
  name: string;
  statement_markdown: string;
  limits: ProblemLimits;
  language: string;
  sample_tests: SampleTest[];
}

export interface CaseDetail {
  label: string;
  verdict?: string;
  exit_code: number;
  cpu_time_ms: number;
  wall_time_ms: number;
  peak_memory_bytes: number;
}

export interface ManualTestsReport {
  status: Status;
  passed: number;
  total: number;
  cases: CaseDetail[];
}

export interface SymbolicCheckOutcome {
  kind: "must_include" | "must_not_include";
  pattern: string;
  regex: boolean;
  matched: boolean;
  satisfied: boolean;
  message?: string;
}

export interface SymbolicReport {
  status: Status;
  checks: SymbolicCheckOutcome[];
}

export interface GTestCaseResult {
  name: string;
  failed: boolean;
  time_ms: number;
  failure_messages?: string[];
}

export interface BehaviorReport {
  status: Status;
  passed: number;
  total: number;
  cases: GTestCaseResult[];
}

export interface CompileStepReport {
  ok: boolean;
  exit_code: number;
  diagnostics?: string;
}

export interface JudgeReport {
  problem: string;
  slug: string;
  submission: string;
  overall: Status;
  tests: {
    manual: ManualTestsReport;
    symbolic: SymbolicReport;
    behavior: BehaviorReport;
  };
  compile: {
    solution?: CompileStepReport;
    behavior_binary?: CompileStepReport;
  };
}

export interface SubmissionAccepted {
  id: string;
  status: "queued";
  problem_slug: string;
}

export interface SubmissionRecord {
  id: string;
  problem_slug: string;
  status: SubmissionStatus;
  created_at: string;
  finished_at?: string;
  report: JudgeReport | null;
}

export interface SubmissionHistoryItem {
  id: string;
  problem_slug: string;
  status: SubmissionStatus;
  created_at: string;
  finished_at?: string;
}

export interface HealthResponse {
  status: "ok" | "degraded";
  workers: { active: number; total: number };
  queue_depth: number;
  uptime_seconds: number;
}

export interface ApiErrorBody {
  error: string;
  message: string;
}

export type ServerEventType =
  | "submission_queued"
  | "submission_started"
  | "submission_progress"
  | "submission_finished"
  | "worker_online"
  | "worker_offline";

export interface ServerEventFrame {
  type: ServerEventType;
  submission_id: string;
}
