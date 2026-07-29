import { useEffect, useState, type ReactNode } from "react";
import type { ProblemDetail, SubmissionRecord } from "../../api/types";
import { TestcasesTab } from "./TestcasesTab";
import { ResultTab } from "./ResultTab";

type Tab = "testcases" | "result";

interface ConsolePanelProps {
  problem: ProblemDetail;
  submissionId: string | null;
  submission: SubmissionRecord | undefined;
  isLoading: boolean;
  error: string | null;
}

export function ConsolePanel({
  problem,
  submissionId,
  submission,
  isLoading,
  error,
}: ConsolePanelProps) {
  const [tab, setTab] = useState<Tab>("testcases");

  // Jump to the Result tab as soon as a new submission is in flight, so
  // the user isn't left staring at the Testcases tab after hitting Submit.
  useEffect(() => {
    if (submissionId) setTab("result");
  }, [submissionId]);

  return (
    <div className="flex h-full flex-col">
      <div className="flex shrink-0 border-b border-border-subtle">
        <TabButton active={tab === "testcases"} onClick={() => setTab("testcases")}>
          Testcases
        </TabButton>
        <TabButton active={tab === "result"} onClick={() => setTab("result")}>
          Result
        </TabButton>
      </div>
      <div className="min-h-0 flex-1">
        {tab === "testcases" ? (
          <TestcasesTab sampleTests={problem.sample_tests} />
        ) : (
          <ResultTab
            submissionId={submissionId}
            submission={submission}
            isLoading={isLoading}
            error={error}
          />
        )}
      </div>
    </div>
  );
}

function TabButton({
  active,
  onClick,
  children,
}: {
  active: boolean;
  onClick: () => void;
  children: ReactNode;
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      className={`px-4 py-2 text-sm font-medium ${
        active ? "border-b-2 border-accent text-accent" : "text-text-muted hover:text-text"
      }`}
    >
      {children}
    </button>
  );
}
