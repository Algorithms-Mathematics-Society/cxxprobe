import type { SampleTest } from "../../api/types";

export function TestcasesTab({ sampleTests }: { sampleTests: SampleTest[] }) {
  if (sampleTests.length === 0) {
    return (
      <p className="p-4 text-sm text-text-muted">
        This problem has no manual sample tests — it's graded by symbolic checks and/or a
        behavior checker instead. Submit to see the full result.
      </p>
    );
  }

  return (
    <div className="space-y-3 overflow-y-auto p-4">
      {sampleTests.map((t) => (
        <div key={t.label} className="rounded-lg border border-border-subtle p-3">
          <div className="mb-1.5 font-mono text-xs text-text-muted">Case {t.label}</div>
          <div className="grid grid-cols-2 gap-3">
            <div>
              <div className="mb-1 text-xs tracking-wide text-text-muted uppercase">Input</div>
              <pre className="overflow-x-auto rounded-md bg-panel-alt p-2 font-mono text-xs whitespace-pre-wrap">
                {t.input}
              </pre>
            </div>
            <div>
              <div className="mb-1 text-xs tracking-wide text-text-muted uppercase">
                Expected Output
              </div>
              <pre className="overflow-x-auto rounded-md bg-panel-alt p-2 font-mono text-xs whitespace-pre-wrap">
                {t.expected_output ?? "—"}
              </pre>
            </div>
          </div>
        </div>
      ))}
    </div>
  );
}
