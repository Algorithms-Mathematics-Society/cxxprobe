import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { HashRouter } from "react-router-dom";
import "./index.css";
import "./lib/monacoSetup";
import App from "./App.tsx";

// Served from a byte-embedded binary via UiAssetHandler (path-based lookup,
// no server-side route matching) — hash routing avoids needing the server
// to rewrite arbitrary deep-link paths back to index.html.
const queryClient = new QueryClient({
  defaultOptions: {
    queries: {
      retry: 1,
      refetchOnWindowFocus: false,
    },
  },
});

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <QueryClientProvider client={queryClient}>
      <HashRouter>
        <App />
      </HashRouter>
    </QueryClientProvider>
  </StrictMode>,
);
