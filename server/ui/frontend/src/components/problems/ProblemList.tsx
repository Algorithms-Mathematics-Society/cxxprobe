import { Link } from "react-router-dom";
import { useProblems } from "../../hooks/useProblems";

export function ProblemList() {
  const { data: problems, isLoading, isError, error } = useProblems();

  return (
    <div className="mx-auto h-full max-w-3xl overflow-y-auto p-6">
      <h1 className="mb-1 text-xl font-bold">Problems</h1>
      <p className="mb-5 text-sm text-text-muted">
        Served from the contest directory passed to <code className="font-mono">cxxprobe serve</code>.
      </p>

      {isLoading && <p className="text-text-muted">Loading…</p>}
      {isError && (
        <p className="text-fail">Failed to load problems: {(error as Error).message}</p>
      )}
      {!isLoading && !isError && problems?.length === 0 && (
        <p className="text-text-muted">No problems found in the served contest directory.</p>
      )}

      <ul className="divide-y divide-border-subtle overflow-hidden rounded-lg border border-border-subtle">
        {problems?.map((p, i) => (
          <li key={p.slug}>
            <Link
              to={`/problems/${p.slug}`}
              className="flex items-center gap-3 bg-panel px-4 py-3 transition-colors hover:bg-panel-alt"
            >
              <span className="w-8 shrink-0 text-right font-mono text-sm text-text-muted">
                {i + 1}
              </span>
              <span className="font-medium">{p.name}</span>
              <span className="ml-auto font-mono text-xs text-text-muted">{p.slug}</span>
            </Link>
          </li>
        ))}
      </ul>
    </div>
  );
}
