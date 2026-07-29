import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";
import monacoEditorEsmPlugin from "vite-plugin-monaco-editor-esm";

// Relative asset paths ("./assets/...") — the built output is served from
// UiAssetHandler by request path, not from a fixed origin/subpath, so
// absolute "/assets/..." paths would break if the dev UI is ever mounted
// under a prefix.
export default defineConfig({
  base: "./",
  plugins: [
    react(),
    tailwindcss(),
    // Only the base editor worker — this is a C++-only editor, so the
    // json/css/html/typescript language workers (the plugin's defaults)
    // would just be dead weight in the embedded binary.
    monacoEditorEsmPlugin({ languageWorkers: ["editorWorkerService"] }),
  ],
  build: {
    outDir: "dist",
    emptyOutDir: true,
  },
});
