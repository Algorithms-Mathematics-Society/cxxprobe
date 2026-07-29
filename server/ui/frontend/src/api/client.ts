import type {
  ApiErrorBody,
  HealthResponse,
  ProblemDetail,
  ProblemSummary,
  SubmissionAccepted,
  SubmissionHistoryItem,
  SubmissionRecord,
} from "./types";

declare global {
  interface Window {
    __CXXPROBE_API_BASE__?: string;
  }
}

// window.__CXXPROBE_API_BASE__ is substituted server-side by UiAssetHandler
// with the real "http://host:port" the API listens on (the UI and API are
// two separate ports) — the fallback below only matters for `vite dev`,
// where nothing substitutes the placeholder.
export function apiBase(): string {
  const injected = window.__CXXPROBE_API_BASE__;
  if (injected && !injected.includes("%%")) {
    return injected;
  }
  return `${window.location.protocol}//${window.location.hostname}:8191`;
}

export class ApiError extends Error {
  status: number;
  body: ApiErrorBody | null;

  constructor(status: number, body: ApiErrorBody | null) {
    super(body?.message ?? `request failed with status ${status}`);
    this.name = "ApiError";
    this.status = status;
    this.body = body;
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${apiBase()}${path}`, {
    ...init,
    headers: { "Content-Type": "application/json", ...init?.headers },
  });
  if (!res.ok) {
    let body: ApiErrorBody | null = null;
    try {
      body = (await res.json()) as ApiErrorBody;
    } catch {
      // non-JSON error body — leave body null, status still conveys the failure
    }
    throw new ApiError(res.status, body);
  }
  return (await res.json()) as T;
}

export function fetchProblems(): Promise<{ problems: ProblemSummary[] }> {
  return request("/problems");
}

export function fetchProblem(slug: string): Promise<ProblemDetail> {
  return request(`/problems/${encodeURIComponent(slug)}`);
}

export interface SubmitPayload {
  problem_slug: string;
  language: "cpp";
  source: string;
}

export function submitSolution(payload: SubmitPayload): Promise<SubmissionAccepted> {
  return request("/submissions", { method: "POST", body: JSON.stringify(payload) });
}

export function fetchSubmission(id: string): Promise<SubmissionRecord> {
  return request(`/submissions/${encodeURIComponent(id)}`);
}

export function fetchHistory(limit = 25): Promise<{ submissions: SubmissionHistoryItem[] }> {
  return request(`/submissions?limit=${limit}`);
}

export function fetchHealth(): Promise<HealthResponse> {
  return request("/health");
}

export async function fetchMetricsText(): Promise<string> {
  const res = await fetch(`${apiBase()}/metrics`, { headers: { Accept: "text/plain" } });
  return res.text();
}

export function eventsUrl(submissionId?: string): string {
  const url = `${apiBase()}/events`;
  return submissionId ? `${url}?submission_id=${encodeURIComponent(submissionId)}` : url;
}
