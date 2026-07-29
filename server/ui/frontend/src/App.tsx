import { Navigate, Route, Routes } from "react-router-dom";
import { Layout } from "./components/layout/Layout";
import { ProblemList } from "./components/problems/ProblemList";
import { ProblemWorkspace } from "./components/problems/ProblemWorkspace";
import { SubmissionHistory } from "./components/history/SubmissionHistory";
import { ApiExplorer } from "./components/explorer/ApiExplorer";
import { HealthPanel } from "./components/health/HealthPanel";
import { MetricsPanel } from "./components/metrics/MetricsPanel";

export default function App() {
  return (
    <Layout>
      <Routes>
        <Route path="/" element={<Navigate to="/problems" replace />} />
        <Route path="/problems" element={<ProblemList />} />
        <Route path="/problems/:slug" element={<ProblemWorkspace />} />
        <Route path="/history" element={<SubmissionHistory />} />
        <Route path="/explorer" element={<ApiExplorer />} />
        <Route path="/health" element={<HealthPanel />} />
        <Route path="/metrics" element={<MetricsPanel />} />
      </Routes>
    </Layout>
  );
}
