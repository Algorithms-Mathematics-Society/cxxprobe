import { useEffect, useState } from "react";
import { useParams } from "react-router-dom";
import { useQueryClient } from "@tanstack/react-query";
import { useProblem } from "../../hooks/useProblem";
import { useSubmit } from "../../hooks/useSubmit";
import { useSubmission } from "../../hooks/useSubmission";
import { useEvents } from "../../hooks/useEvents";
import { ApiError } from "../../api/client";
import { StatementPanel } from "./StatementPanel";
import { EditorPanel, DEFAULT_SOURCE } from "./EditorPanel";
import { ConsolePanel } from "./ConsolePanel";

export function ProblemWorkspace() {
  const { slug } = useParams<{ slug: string }>();
  const { data: problem, isLoading, isError, error } = useProblem(slug);
  const [source, setSource] = useState(DEFAULT_SOURCE);
  const [submissionId, setSubmissionId] = useState<string | null>(null);
  const [submitError, setSubmitError] = useState<string | null>(null);

  const submit = useSubmit();
  const submission = useSubmission(submissionId ?? undefined);
  const queryClient = useQueryClient();

  // SSE is the live-update source; the poll in useSubmission is the
  // correctness fallback if the connection drops. On a relevant frame,
  // just invalidate — the query refetches immediately rather than waiting
  // for the next poll tick.
  const { latest } = useEvents(submissionId ?? undefined);
  useEffect(() => {
    if (latest && submissionId && latest.submission_id === submissionId) {
      queryClient.invalidateQueries({ queryKey: ["submission", submissionId] });
    }
  }, [latest, submissionId, queryClient]);

  // Reset per-problem state when navigating to a different problem.
  useEffect(() => {
    setSource(DEFAULT_SOURCE);
    setSubmissionId(null);
    setSubmitError(null);
  }, [slug]);

  async function handleSubmit() {
    if (!problem) return;
    setSubmitError(null);
    try {
      const accepted = await submit.mutateAsync({
        problem_slug: problem.slug,
        language: "cpp",
        source,
      });
      setSubmissionId(accepted.id);
    } catch (err) {
      setSubmitError(err instanceof ApiError ? err.message : "submission failed");
    }
  }

  if (isLoading) {
    return <div className="p-6 text-text-muted">Loading problem…</div>;
  }
  if (isError || !problem) {
    return (
      <div className="p-6 text-fail">
        Failed to load problem: {error instanceof ApiError ? error.message : "unknown error"}
      </div>
    );
  }

  return (
    <div className="grid h-full grid-cols-2 gap-px overflow-hidden bg-border-subtle">
      <div className="overflow-hidden bg-canvas">
        <StatementPanel problem={problem} />
      </div>

      <div className="grid grid-rows-[1fr_auto_40%] overflow-hidden bg-canvas">
        <div className="min-h-0 overflow-hidden">
          <EditorPanel value={source} onChange={setSource} />
        </div>

        <div className="flex items-center gap-3 border-t border-border-subtle bg-panel px-4 py-2">
          <button
            type="button"
            onClick={handleSubmit}
            disabled={submit.isPending}
            className="rounded-md bg-accent px-4 py-1.5 text-sm font-semibold text-black transition-colors hover:bg-accent-hover disabled:cursor-not-allowed disabled:opacity-50"
          >
            {submit.isPending ? "Judging…" : "Submit"}
          </button>
          {submitError && <span className="text-sm text-fail">{submitError}</span>}
        </div>

        <div className="min-h-0 overflow-hidden border-t border-border-subtle bg-panel">
          <ConsolePanel
            problem={problem}
            submissionId={submissionId}
            submission={submission.data}
            isLoading={submission.isLoading}
            error={
              submission.isError
                ? submission.error instanceof ApiError
                  ? submission.error.message
                  : "failed to load submission"
                : null
            }
          />
        </div>
      </div>
    </div>
  );
}
