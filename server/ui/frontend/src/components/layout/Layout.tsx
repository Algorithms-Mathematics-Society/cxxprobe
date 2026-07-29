import type { ReactNode } from "react";
import { NavLink } from "react-router-dom";

const NAV_ITEMS = [
  { to: "/problems", label: "Problems" },
  { to: "/history", label: "Submissions" },
  { to: "/explorer", label: "API Explorer" },
  { to: "/health", label: "Health" },
  { to: "/metrics", label: "Metrics" },
];

function navClass({ isActive }: { isActive: boolean }) {
  return [
    "rounded-md px-3 py-1.5 text-sm font-medium transition-colors",
    isActive
      ? "bg-panel-alt text-accent"
      : "text-text-muted hover:bg-panel-alt hover:text-text",
  ].join(" ");
}

export function Layout({ children }: { children: ReactNode }) {
  return (
    <div className="flex h-full flex-col">
      <header className="flex items-center gap-4 border-b border-border-subtle bg-panel px-4 py-2.5">
        <span className="text-lg font-bold tracking-tight text-accent">cxxprobe</span>
        <span className="rounded-full border border-border-subtle px-2.5 py-0.5 text-xs text-text-muted">
          developer UI — for local testing only, not a replacement for AMS Judge
        </span>
        <nav className="ml-auto flex items-center gap-1">
          {NAV_ITEMS.map((item) => (
            <NavLink key={item.to} to={item.to} className={navClass}>
              {item.label}
            </NavLink>
          ))}
        </nav>
      </header>
      <main className="min-h-0 flex-1 overflow-hidden">{children}</main>
    </div>
  );
}
