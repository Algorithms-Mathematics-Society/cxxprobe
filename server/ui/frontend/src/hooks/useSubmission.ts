import { useQuery, useQueryClient } from "@tanstack/react-query";
import { useEffect } from "react";
import { fetchSubmission } from "../api/client";
import type { SubmissionRecord } from "../api/types";

const TERMINAL: SubmissionRecord["status"][] = ["finished", "error"];

// Polls GET /submissions/{id} every second while the submission is
// queued/running — a correctness fallback so the UI still converges to the
// final result even if the SSE connection drops, without depending on SSE
// as the only source of truth.
export function useSubmission(id: string | undefined) {
  const queryClient = useQueryClient();

  const query = useQuery({
    queryKey: ["submission", id],
    queryFn: () => fetchSubmission(id as string),
    enabled: Boolean(id),
    refetchInterval: (q) => {
      const data = q.state.data as SubmissionRecord | undefined;
      if (!data || TERMINAL.includes(data.status)) return false;
      return 1000;
    },
  });

  // A live SSE event for this id (see useEvents) can trigger an immediate
  // refetch instead of waiting for the next poll tick — wired by callers
  // via queryClient directly, see ProblemWorkspace.
  useEffect(() => {
    if (!id) return;
    return () => {
      queryClient.removeQueries({ queryKey: ["submission", id], exact: true });
    };
  }, [id, queryClient]);

  return query;
}
