const COLORS: Record<string, string> = {
  pass: "bg-pass/15 text-pass",
  finished: "bg-pass/15 text-pass",
  ok: "bg-pass/15 text-pass",
  fail: "bg-fail/15 text-fail",
  error: "bg-fail/15 text-fail",
  degraded: "bg-fail/15 text-fail",
  skipped: "bg-warn/15 text-warn",
  queued: "bg-warn/15 text-warn",
  running: "bg-warn/15 text-warn",
};

export function StatusBadge({ status }: { status: string }) {
  const key = status.toLowerCase();
  const classes = COLORS[key] ?? "bg-text-muted/15 text-text-muted";
  return (
    <span
      className={`inline-flex items-center rounded-full px-2.5 py-0.5 text-xs font-semibold uppercase tracking-wide ${classes}`}
    >
      {status}
    </span>
  );
}
