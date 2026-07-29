import type { ReactNode } from "react";
import type { SubmissionRecord } from "../../api/types";
import { StatusBadge } from "../ui/StatusBadge";

interface ResultTabProps {
  submissionId: string | null;
  submission: SubmissionRecord | undefined;
  isLoading: boolean;
  error: string | null;
}

export function ResultTab({ submissionId, submission, isLoading, error }: ResultTabProps) {
  if (!submissionId) {
    return (
      <p className="p-4 text-sm text-text-muted">
        Submit your solution to see the judging result here.
      </p>
    );
  }
  if (error) {
    return <p className="p-4 text-sm text-fail">{error}</p>;
  }
  if (isLoading && !submission) {
    return <p className="p-4 text-sm text-text-muted">Loading…</p>;
  }
  if (!submission) return null;

  const report = submission.report;

  return (
    <div className="space-y-4 overflow-y-auto p-4">
      <div className="flex items-center gap-2">
        <StatusBadge status={submission.status} />
        {report && <StatusBadge status={report.overall} />}
        <span className="font-mono text-xs text-text-muted">{submissionId.slice(0, 8)}</span>
      </div>

      {!report && (submission.status === "queued" || submission.status === "running") && (
        <p className="text-sm text-text-muted">Judging in progress…</p>
      )}

      {report && (
        <>
          <ReportSection title="Manual Tests" status={report.tests.manual.status}>
            {report.tests.manual.total > 0 && (
              <p className="mb-2 text-xs text-text-muted">
                {report.tests.manual.passed} / {report.tests.manual.total} passed
              </p>
            )}
            <div className="space-y-1">
              {report.tests.manual.cases.map((c) => (
                <div
                  key={c.label}
                  className="flex items-center gap-2 rounded bg-panel-alt px-2 py-1 font-mono text-xs"
                >
                  <span className="w-10 text-text-muted">#{c.label}</span>
                  <span className={c.verdict === "AC" ? "text-pass" : "text-fail"}>
                    {c.verdict || "—"}
                  </span>
                  <span className="ml-auto text-text-muted">
                    {c.cpu_time_ms}ms cpu / {c.wall_time_ms}ms wall
                  </span>
                </div>
              ))}
            </div>
          </ReportSection>

          <ReportSection title="Symbolic Checks" status={report.tests.symbolic.status}>
            <div className="space-y-1">
              {report.tests.symbolic.checks.map((c, i) => (
                <div key={i} className="rounded bg-panel-alt px-2 py-1.5 text-xs">
                  <div className="flex items-center gap-2 font-mono">
                    <span className={c.satisfied ? "text-pass" : "text-fail"}>
                      {c.satisfied ? "OK" : "FAIL"}
                    </span>
                    <span className="text-text-muted">[{c.kind}]</span>
                    <span>{c.pattern}</span>
                  </div>
                  {c.message && !c.satisfied && (
                    <p className="mt-1 text-text-muted">{c.message}</p>
                  )}
                </div>
              ))}
            </div>
          </ReportSection>

          <ReportSection title="Behavior Tests" status={report.tests.behavior.status}>
            {report.tests.behavior.total > 0 && (
              <p className="mb-2 text-xs text-text-muted">
                {report.tests.behavior.passed} / {report.tests.behavior.total} passed
              </p>
            )}
            <div className="space-y-1">
              {report.tests.behavior.cases.map((c) => (
                <div key={c.name} className="rounded bg-panel-alt px-2 py-1.5 text-xs">
                  <div className="flex items-center gap-2 font-mono">
                    <span className={c.failed ? "text-fail" : "text-pass"}>
                      {c.failed ? "FAIL" : "PASS"}
                    </span>
                    <span>{c.name}</span>
                    <span className="ml-auto text-text-muted">{c.time_ms}ms</span>
                  </div>
                  {c.failure_messages?.map((m, i) => (
                    <p key={i} className="mt-1 font-mono text-fail whitespace-pre-wrap">
                      {m}
                    </p>
                  ))}
                </div>
              ))}
            </div>
          </ReportSection>

          {(report.compile.solution?.diagnostics || report.compile.behavior_binary?.diagnostics) && (
            <ReportSection title="Compiler Output" status="ERROR">
              {report.compile.solution?.diagnostics && (
                <pre className="overflow-x-auto rounded bg-panel-alt p-2 font-mono text-xs whitespace-pre-wrap text-fail">
                  {report.compile.solution.diagnostics}
                </pre>
              )}
              {report.compile.behavior_binary?.diagnostics && (
                <pre className="mt-2 overflow-x-auto rounded bg-panel-alt p-2 font-mono text-xs whitespace-pre-wrap text-fail">
                  {report.compile.behavior_binary.diagnostics}
                </pre>
              )}
            </ReportSection>
          )}
        </>
      )}
    </div>
  );
}

function ReportSection({
  title,
  status,
  children,
}: {
  title: string;
  status: string;
  children: ReactNode;
}) {
  if (status === "SKIPPED") {
    return (
      <div className="flex items-center gap-2 text-sm text-text-muted">
        <span className="font-medium">{title}</span>
        <StatusBadge status={status} />
      </div>
    );
  }
  return (
    <div>
      <div className="mb-2 flex items-center gap-2">
        <span className="text-sm font-medium">{title}</span>
        <StatusBadge status={status} />
      </div>
      {children}
    </div>
  );
}
