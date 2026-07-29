import { useState } from "react";
import { apiBase } from "../../api/client";

export function ApiExplorer() {
  const [method, setMethod] = useState<"GET" | "POST">("GET");
  const [path, setPath] = useState("/problems");
  const [body, setBody] = useState('{"problem_slug":"a-warmup","source":"int main(){}"}');
  const [status, setStatus] = useState<string>("");
  const [response, setResponse] = useState<string>("");
  const [sending, setSending] = useState(false);

  async function send() {
    setSending(true);
    setStatus("");
    setResponse("");
    try {
      const init: RequestInit = { method };
      if (method === "POST") {
        init.headers = { "Content-Type": "application/json" };
        init.body = body;
      }
      const res = await fetch(`${apiBase()}${path}`, init);
      setStatus(String(res.status));
      const text = await res.text();
      try {
        setResponse(JSON.stringify(JSON.parse(text), null, 2));
      } catch {
        setResponse(text);
      }
    } catch (err) {
      setStatus("error");
      setResponse(String(err));
    } finally {
      setSending(false);
    }
  }

  return (
    <div className="mx-auto h-full max-w-3xl overflow-y-auto p-6">
      <h1 className="mb-1 text-xl font-bold">API Explorer</h1>
      <p className="mb-5 text-sm text-text-muted">
        Every request here goes through the exact same public REST API this page uses for
        everything else — nothing here is a special or privileged path.
      </p>

      <div className="mb-3 flex gap-2">
        <select
          value={method}
          onChange={(e) => setMethod(e.target.value as "GET" | "POST")}
          className="rounded-md border border-border-subtle bg-panel-alt px-2 py-1.5 text-sm"
        >
          <option>GET</option>
          <option>POST</option>
        </select>
        <input
          type="text"
          value={path}
          onChange={(e) => setPath(e.target.value)}
          placeholder="/problems"
          className="flex-1 rounded-md border border-border-subtle bg-panel-alt px-3 py-1.5 font-mono text-sm"
        />
        <button
          type="button"
          onClick={send}
          disabled={sending}
          className="rounded-md bg-accent px-4 py-1.5 text-sm font-semibold text-black hover:bg-accent-hover disabled:opacity-50"
        >
          Send
        </button>
      </div>

      {method === "POST" && (
        <textarea
          value={body}
          onChange={(e) => setBody(e.target.value)}
          rows={4}
          className="mb-3 w-full rounded-md border border-border-subtle bg-panel-alt p-2 font-mono text-sm"
        />
      )}

      {status && (
        <p className="mb-2 text-sm text-text-muted">
          status: <span className="font-mono text-text">{status}</span>
        </p>
      )}
      <pre className="overflow-x-auto rounded-md bg-panel-alt p-3 font-mono text-xs whitespace-pre-wrap">
        {response}
      </pre>
    </div>
  );
}
