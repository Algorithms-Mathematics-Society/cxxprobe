import { useHealth } from "../../hooks/useHealth";

function Tile({ label, value }: { label: string; value: string | number }) {
  return (
    <div className="min-w-[140px] rounded-lg border border-border-subtle px-4 py-3">
      <div className="text-2xl font-bold">{value}</div>
      <div className="text-xs tracking-wide text-text-muted uppercase">{label}</div>
    </div>
  );
}

export function HealthPanel() {
  const { data, isLoading, isError, refetch } = useHealth();

  return (
    <div className="mx-auto h-full max-w-3xl overflow-y-auto p-6">
      <div className="mb-4 flex items-center gap-3">
        <h1 className="text-xl font-bold">Health</h1>
        <button
          type="button"
          onClick={() => refetch()}
          className="rounded-md border border-border-subtle px-3 py-1 text-xs text-text-muted hover:text-text"
        >
          Refresh
        </button>
      </div>

      {isLoading && <p className="text-text-muted">Loading…</p>}
      {isError && <p className="text-fail">Failed to reach the API.</p>}

      {data && (
        <div className="flex flex-wrap gap-3">
          <Tile label="status" value={data.status} />
          <Tile label="workers active/total" value={`${data.workers.active} / ${data.workers.total}`} />
          <Tile label="queue depth" value={data.queue_depth} />
          <Tile label="uptime" value={`${data.uptime_seconds}s`} />
        </div>
      )}
    </div>
  );
}
