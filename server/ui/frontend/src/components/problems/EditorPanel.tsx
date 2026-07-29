import Editor, { type OnMount } from "@monaco-editor/react";

export const DEFAULT_SOURCE = `#include <iostream>

int main() {
    // your solution here
    return 0;
}
`;

interface EditorPanelProps {
  value: string;
  onChange: (value: string) => void;
}

export function EditorPanel({ value, onChange }: EditorPanelProps) {
  const handleMount: OnMount = (editor) => {
    editor.focus();
  };

  return (
    <Editor
      height="100%"
      defaultLanguage="cpp"
      theme="vs-dark"
      value={value}
      onChange={(next) => onChange(next ?? "")}
      onMount={handleMount}
      options={{
        fontSize: 13,
        minimap: { enabled: false },
        automaticLayout: true,
        tabSize: 4,
        insertSpaces: true,
        scrollBeyondLastLine: false,
        padding: { top: 12 },
      }}
    />
  );
}
