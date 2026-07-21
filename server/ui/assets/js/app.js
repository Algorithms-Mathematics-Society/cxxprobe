// cxxprobe developer UI — talks exclusively to the public REST/SSE API
// (never a "backdoor" call into server internals). Every network call in
// this file is a plain fetch()/EventSource against the same base URL a
// script or AMS Judge would use.

function apiBase() {
    // Injected by UiAssetHandler at request time (see server/handlers/
    // ui_asset_handler.cpp) so the UI knows which port the real API is on
    // without hardcoding it — the UI and API are two separate listeners.
    return window.__CXXPROBE_API_BASE__ || (window.location.protocol + "//" + window.location.hostname + ":8191");
}

function statusClass(status) {
    if (!status) return "";
    return "status-" + String(status).toLowerCase();
}

function cxxprobeApp() {
    return {
        tab: "problems",
        apiBase: apiBase(),

        // --- Problems / submit ---
        problems: [],
        selectedProblem: null,
        editor: null,
        submitting: false,
        submission: null,
        submitError: "",

        // --- History ---
        history: [],

        // --- Health / metrics ---
        health: null,
        metricsText: "",

        // --- Events ---
        eventLog: [],
        eventSource: null,

        // --- API explorer ---
        explorer: {
            method: "GET",
            path: "/health",
            body: "",
            status: "",
            response: "",
        },

        async init() {
            await this.loadProblems();
            await this.refreshHealth();
            this.connectEvents();
            this.loadHistory();
        },

        selectTab(tab) {
            this.tab = tab;
            if (tab === "history") this.loadHistory();
            if (tab === "health") this.refreshHealth();
            if (tab === "metrics") this.refreshMetrics();
        },

        async loadProblems() {
            const res = await fetch(this.apiBase + "/problems");
            const data = await res.json();
            this.problems = data.problems || [];
        },

        async selectProblem(slug) {
            const res = await fetch(this.apiBase + "/problems/" + encodeURIComponent(slug));
            if (!res.ok) return;
            this.selectedProblem = await res.json();
            this.submission = null;
            this.submitError = "";
            this.$nextTick(() => this.mountEditor());
        },

        mountEditor() {
            const el = document.getElementById("source-editor");
            if (!el || typeof CodeMirror === "undefined") return;
            if (this.editor) {
                this.editor.toTextArea();
                this.editor = null;
            }
            this.editor = CodeMirror.fromTextArea(el, {
                mode: "text/x-c++src",
                lineNumbers: true,
                indentUnit: 4,
                tabSize: 4,
                viewportMargin: Infinity,
            });
            this.editor.setValue(
                "#include <iostream>\n\nint main() {\n    // your solution here\n    return 0;\n}\n"
            );
        },

        async submit() {
            if (!this.selectedProblem || !this.editor) return;
            this.submitting = true;
            this.submitError = "";
            this.submission = null;
            try {
                const res = await fetch(this.apiBase + "/submissions", {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify({
                        problem_slug: this.selectedProblem.slug,
                        language: "cpp",
                        source: this.editor.getValue(),
                    }),
                });
                const data = await res.json();
                if (!res.ok) {
                    this.submitError = (data && data.message) || "submission rejected";
                    this.submitting = false;
                    return;
                }
                this.submission = data;
                this.pollSubmission(data.id);
            } catch (err) {
                this.submitError = String(err);
                this.submitting = false;
            }
        },

        async pollSubmission(id) {
            const res = await fetch(this.apiBase + "/submissions/" + id);
            const data = await res.json();
            this.submission = data;
            if (data.status === "queued" || data.status === "running") {
                setTimeout(() => this.pollSubmission(id), 1000);
            } else {
                this.submitting = false;
                this.loadHistory();
            }
        },

        async loadHistory() {
            const res = await fetch(this.apiBase + "/submissions?limit=25");
            const data = await res.json();
            this.history = data.submissions || [];
        },

        async refreshHealth() {
            try {
                const res = await fetch(this.apiBase + "/health");
                this.health = await res.json();
            } catch (err) {
                this.health = null;
            }
        },

        async refreshMetrics() {
            const res = await fetch(this.apiBase + "/metrics", {
                headers: { Accept: "text/plain" },
            });
            this.metricsText = await res.text();
        },

        connectEvents() {
            if (typeof EventSource === "undefined") return;
            this.eventSource = new EventSource(this.apiBase + "/events");
            const record = (type) => (evt) => {
                let parsed = {};
                try {
                    parsed = JSON.parse(evt.data);
                } catch (e) {
                    /* ignore malformed frame */
                }
                const line = new Date().toLocaleTimeString() + "  " + type + "  " + (parsed.submission_id || "");
                this.eventLog.unshift(line);
                if (this.eventLog.length > 200) this.eventLog.pop();
                if (parsed.submission_id && this.submission && parsed.submission_id === this.submission.id) {
                    this.pollSubmission(parsed.submission_id);
                }
                if (type === "submission_finished" || type === "submission_queued") {
                    this.loadHistory();
                }
            };
            for (const type of [
                "submission_queued",
                "submission_started",
                "submission_progress",
                "submission_finished",
                "worker_online",
                "worker_offline",
            ]) {
                this.eventSource.addEventListener(type, record(type));
            }
        },

        async runExplorer() {
            this.explorer.status = "";
            this.explorer.response = "";
            try {
                const opts = { method: this.explorer.method };
                if (this.explorer.method === "POST" && this.explorer.body) {
                    opts.headers = { "Content-Type": "application/json" };
                    opts.body = this.explorer.body;
                }
                const res = await fetch(this.apiBase + this.explorer.path, opts);
                this.explorer.status = res.status;
                const text = await res.text();
                try {
                    this.explorer.response = JSON.stringify(JSON.parse(text), null, 2);
                } catch (e) {
                    this.explorer.response = text;
                }
            } catch (err) {
                this.explorer.status = "error";
                this.explorer.response = String(err);
            }
        },

        statusClass,
    };
}
