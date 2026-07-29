import { useEffect, useRef, useState } from "react";
import { eventsUrl } from "../api/client";
import type { ServerEventFrame, ServerEventType } from "../api/types";

const EVENT_TYPES: ServerEventType[] = [
  "submission_queued",
  "submission_started",
  "submission_progress",
  "submission_finished",
  "worker_online",
  "worker_offline",
];

// Subscribes to GET /events (SSE), optionally scoped to one submission id.
// Returns the live feed (newest first, capped) plus the single most recent
// frame, so callers that only care about "did anything just happen for my
// submission" don't have to scan the whole list themselves.
export function useEvents(submissionId?: string) {
  const [events, setEvents] = useState<ServerEventFrame[]>([]);
  const [latest, setLatest] = useState<ServerEventFrame | null>(null);
  const sourceRef = useRef<EventSource | null>(null);

  useEffect(() => {
    const source = new EventSource(eventsUrl(submissionId));
    sourceRef.current = source;

    const listeners = EVENT_TYPES.map((type) => {
      const handler = (evt: MessageEvent<string>) => {
        let submission_id = "";
        try {
          submission_id = (JSON.parse(evt.data) as { submission_id?: string }).submission_id ?? "";
        } catch {
          // malformed frame — ignore, keep the connection alive
        }
        const frame: ServerEventFrame = { type, submission_id };
        setLatest(frame);
        setEvents((prev) => [frame, ...prev].slice(0, 200));
      };
      source.addEventListener(type, handler as EventListener);
      return { type, handler };
    });

    return () => {
      for (const { type, handler } of listeners) {
        source.removeEventListener(type, handler as EventListener);
      }
      source.close();
      sourceRef.current = null;
    };
  }, [submissionId]);

  return { events, latest };
}
