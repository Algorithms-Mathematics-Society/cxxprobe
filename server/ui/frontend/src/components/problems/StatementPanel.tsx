import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import type { ProblemDetail } from "../../api/types";

export function StatementPanel({ problem }: { problem: ProblemDetail }) {
  return (
    <div className="h-full overflow-y-auto p-5">
      <h1 className="mb-2 text-lg font-bold">{problem.name}</h1>
      <div className="mb-4 flex flex-wrap gap-2 text-xs text-text-muted">
        <span className="rounded-full border border-border-subtle px-2.5 py-0.5">
          {problem.limits.memory_mb} MiB
        </span>
        <span className="rounded-full border border-border-subtle px-2.5 py-0.5">
          {problem.limits.cpu_ms}ms CPU
        </span>
        <span className="rounded-full border border-border-subtle px-2.5 py-0.5">
          {problem.limits.wall_ms}ms wall
        </span>
        <span className="rounded-full border border-border-subtle px-2.5 py-0.5">
          lang: {problem.language}
        </span>
      </div>
      <article className="prose prose-invert prose-sm max-w-none prose-pre:bg-panel-alt prose-code:before:content-none prose-code:after:content-none">
        <ReactMarkdown remarkPlugins={[remarkGfm]}>{problem.statement_markdown}</ReactMarkdown>
      </article>
    </div>
  );
}
