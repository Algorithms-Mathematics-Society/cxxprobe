import { useMetricsText } from "../../hooks/useMetrics";

export function MetricsPanel() {
  const { data, isLoading, isError } = useMetricsText();

  return (
    <div className="mx-auto h-full max-w-3xl overflow-y-auto p-6">
      <h1 className="mb-1 text-xl font-bold">Metrics</h1>
      <p className="mb-4 text-sm text-text-muted">Prometheus text exposition, refreshed every 5s.</p>
      {isLoading && <p className="text-text-muted">Loading…</p>}
      {isError && <p className="text-fail">Failed to reach the API.</p>}
      {data && (
        <pre className="overflow-x-auto rounded-md bg-panel-alt p-3 font-mono text-xs whitespace-pre-wrap">
          {data}
        </pre>
      )}
    </div>
  );
}
