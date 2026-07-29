// Self-hosts Monaco entirely from the bundled npm package — no CDN fetch at
// runtime, matching the "one binary, works offline" requirement. Worker
// bundling is handled by vite-plugin-monaco-editor-esm (see vite.config.ts).
//
// Deliberately does NOT `import * as monaco from "monaco-editor"` — that
// pulls in all ~90 bundled languages (a multi-MB bundle) for an editor that
// only ever edits C++. Importing editor.all + just the cpp language
// contribution keeps the embedded binary from ballooning.
import "monaco-editor/esm/vs/editor/editor.all.js";
import "monaco-editor/esm/vs/basic-languages/cpp/cpp.contribution.js";
import * as monaco from "monaco-editor/esm/vs/editor/editor.api";
import { loader } from "@monaco-editor/react";

loader.config({ monaco });
