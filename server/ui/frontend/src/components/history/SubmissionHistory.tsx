import { useHistory } from "../../hooks/useHistory";
import { useEvents } from "../../hooks/useEvents";
import { useQueryClient } from "@tanstack/react-query";
import { useEffect } from "react";
import { StatusBadge } from "../ui/StatusBadge";

export function SubmissionHistory() {
  const { data: submissions, isLoading, isError } = useHistory(50);
  const { events } = useEvents();
  const queryClient = useQueryClient();

  // Any submission lifecycle event anywhere in the contest should refresh
  // this list — it's a global firehose subscription (no submission_id
  // filter), matching the "recent activity across the whole contest" intent
  // of a history page.
  useEffect(() => {
    if (events.length > 0) {
      queryClient.invalidateQueries({ queryKey: ["history"] });
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps -- only the count changing should retrigger
  }, [events.length, queryClient]);

  return (
    <div className="grid h-full grid-cols-[1fr_320px] overflow-hidden">
      <div className="overflow-y-auto p-6">
        <h1 className="mb-4 text-xl font-bold">Submission History</h1>
        {isLoading && <p className="text-text-muted">Loading…</p>}
        {isError && <p className="text-fail">Failed to load history.</p>}
        {!isLoading && submissions?.length === 0 && (
          <p className="text-text-muted">No submissions yet.</p>
        )}
        <table className="w-full text-sm">
          <thead>
            <tr className="border-b border-border-subtle text-left text-xs tracking-wide text-text-muted uppercase">
              <th className="py-2 pr-4 font-medium">ID</th>
              <th className="py-2 pr-4 font-medium">Problem</th>
              <th className="py-2 pr-4 font-medium">Status</th>
              <th className="py-2 font-medium">Created</th>
            </tr>
          </thead>
          <tbody>
            {submissions?.map((s) => (
              <tr key={s.id} className="border-b border-border-subtle/60">
                <td className="py-2 pr-4 font-mono text-xs text-text-muted">
                  {s.id.slice(0, 8)}
                </td>
                <td className="py-2 pr-4">{s.problem_slug}</td>
                <td className="py-2 pr-4">
                  <StatusBadge status={s.status} />
                </td>
                <td className="py-2 text-text-muted">{s.created_at}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>

      <aside className="overflow-y-auto border-l border-border-subtle bg-panel p-4">
        <h2 className="mb-3 text-sm font-semibold text-text-muted">Live Activity</h2>
        <div className="space-y-1 font-mono text-xs">
          {events.length === 0 && <p className="text-text-muted">Waiting for events…</p>}
          {events.map((e, i) => (
            <div key={i} className="border-b border-border-subtle/60 py-1 text-text-muted">
              <span className="text-text">{e.type}</span>{" "}
              {e.submission_id && <span>{e.submission_id.slice(0, 8)}</span>}
            </div>
          ))}
        </div>
      </aside>
    </div>
  );
}
