# cxxprobe Roadmap

cxxprobe is becoming the evaluation engine behind AMS Access's judging
pipeline: it compiles, sandboxes, executes, and judges submissions —
nothing else. Contests, users, auth, scoreboards, and AWS orchestration
are explicitly not cxxprobe's job; they live in AMS Access's own systems.

This document tracks that scope against what's actually built today, so
future sessions don't have to re-derive it from scratch. Status legend:

- ✅ done
- 🟡 partial
- ⬜ not started
- 🚫 not cxxprobe's responsibility (owned elsewhere, noted per item)

## Core Evaluation

| Item | Status | Notes |
|---|---|---|
| Compile submissions | ✅ | `compile::compile()`, `include/cxxprobe/compile.hpp` |
| Sandbox execution | ✅ | `sandbox::run()`, cgroup v2 + user/mount namespaces, `include/cxxprobe/sandbox.hpp` |
| Execute testcases | ✅ | `cases::load_cases_dir`/`load_cases_manifest`, `judge::run_manual_tests` |
| Run custom checkers | ✅ | `cases::check_output()` — testlib-ABI binary if `tests.checker` set, else built-in token-equal |
| Run validators | ⬜ | See Problem Setting — no input-format-validation concept exists yet, distinct from checkers |
| Enforce resource limits | ✅ | `sandbox::Limits` (memory/cpu/wall/pids), cgroup-enforced |
| Produce verdicts | ✅ | `cases::Verdict` (AC/WA/TLE/MLE/OLE/RE), `judge::Status` (Pass/Fail/Skipped/Error) |
| Produce testcase results | ✅ | `judge::CaseDetail` per case, with resource usage |
| Generate compile/runtime logs | ✅ | `compile::Result::diagnostics`, `sandbox::Result::stdout_data/stderr_data` |

## Problem Setting

The biggest gap — none of this exists yet. This is genuine Polygon/testlib-style
authoring tooling, not a small addition.

| Item | Status | Notes |
|---|---|---|
| Validate problem packages | 🟡 | `problem::load()` validates YAML schema + referenced-file existence; no deep dataset validation |
| Run validators against testcases | ⬜ | No validator concept at all — a distinct thing from checkers (validates input format, not output correctness) |
| Generate testcases using generators | ⬜ | No generator invocation exists; `docs/content/guides/stress-testing.mdx` shows this as an external bash loop today |
| Stress testing | ⬜ | Same — a documented workflow composed from `run`, not a built-in feature |
| Differential testing | ⬜ | Same |
| Verify checker correctness | ⬜ | No mechanism to test a checker against known-good/bad pairs |
| Detect invalid datasets | ⬜ | Malformed `.in` with no `.ans`, duplicate labels, etc. are silently tolerated |
| Test multiple solutions (AC/WA/TLE/RE) | 🟡 | `--submission` grades one arbitrary solution per invocation; no built-in "run N solutions" loop |
| Estimate runtime and memory usage | 🟡 | Every run reports exact resource usage; no statistical benchmarking across repeated runs |
| Detect weak testcases | ⬜ | No coverage/mutation analysis |
| Preview problem package before publishing | 🟡 | `cxxprobe test problem` (no `--submission`) validates the reference solution; `pack`'s `manifest.json` gives a JSON preview (Phase 1, this session) |

## Package Management

| Item | Status | Notes |
|---|---|---|
| Import problem packages | ✅ | `cxxprobe unpack` (Phase 1, this session) |
| Export problem packages | ✅ | `cxxprobe pack` (Phase 1, this session) |
| Version problem packages | 🟡 | `manifest.json`'s `format_version` versions the *pack schema*; no content-revision history for a problem over time |
| Package integrity verification | ⬜ | No checksums/signing |
| Dependency validation | ⬜ | No inter-problem/inter-package dependency concept |

## Execution Environment

| Item | Status | Notes |
|---|---|---|
| Language discovery | 🚫 | C++-only by design for now — deferred, see "Deferred" below |
| Compiler discovery | ⬜ | Compiler is a configurable string (`compiler.cxx`), invoked directly — no PATH/version probing |
| Toolchain management | ⬜ | No installation/version-pinning/multi-toolchain support |
| Sandbox configuration | ✅ | `sandbox::Limits`, configurable via CLI flags or `problem.yaml` |
| Runtime configuration | 🟡 | Compiler flags/std/extra sources configurable; no env var/mount/network policy knobs beyond the fixed namespace design |

## Worker Runtime

Per the locked decision this session: cxxprobe never talks to SQS/S3 directly.
A future external adapter (not part of this repo) owns queue polling, package
fetch/upload, and orchestration-level health/retry — it shells out to cxxprobe
per job. Items below marked 🚫 are that adapter's job, not cxxprobe's.

| Item | Status | Notes |
|---|---|---|
| Job execution | ✅ | `cxxprobe judge` (Phase 1, this session) — package-in/result-out, no contest-dir resolution |
| Worker registration | 🚫 | Owned by the future SQS/S3 adapter |
| Health reporting | 🚫 (to an external system) / ✅ (in-process) | `cxxprobe serve`'s `GET /health`/`GET /metrics` already exist for the self-contained HTTP-service mode; reporting to an external orchestrator is the adapter's job |
| Retry support | 🚫 | Owned by the future adapter — `cxxprobe judge`'s contract (a produced report vs. no output file) is exactly what a retry policy needs to key off of |
| Graceful cancellation | 🟡 | `cxxprobe serve`'s worker pool shuts down gracefully (finishes in-flight judges); no per-submission cancellation API. Per-job cancellation for the future adapter model is presumably just "don't wait for / kill the subprocess," not a cxxprobe concern |

## Developer Features

| Item | Status | Notes |
|---|---|---|
| Local judge CLI | ✅ | `run`, `new`, `test`, `pack`/`unpack`, `judge`, `serve` |
| Batch evaluation | ✅ | `cxxprobe run --cases <dir\|manifest>` |
| Benchmarking | ⬜ | No repeated-run statistical analysis |
| Dry-run mode | ⬜ | No flag validates config without compiling/executing |
| Rejudge support | ⬜ | `cxxprobe serve`'s API has no re-run-an-existing-submission endpoint |
| Plugin architecture | ⬜ | No extension-point mechanism of any kind |

## Outputs

| Item | Status | Notes |
|---|---|---|
| Verdict | ✅ | |
| Testcase results | ✅ | |
| Resource usage | ✅ | cpu/wall/memory, cgroup-measured |
| Compile logs | ✅ | |
| Runtime logs | ✅ | |
| Checker output | 🟡 | Drives AC/WA and is passed through to the terminal in CLI mode; not captured into the structured `JudgeReport`/JSON |
| Execution metadata | ✅ | Exit codes, problem/slug/submission identifiers, timestamps |

## Explicitly not cxxprobe's responsibility

Users, authentication, contests, registrations, teams, scoreboards,
clarifications, payments, notifications, analytics, database business
logic, AWS infrastructure. All of this lives in AMS Access.

## Phases

- **Phase 1 (this session)** — Package management (`pack`/`unpack`) and a
  worker-facing single-shot judge entry point (`cxxprobe judge`), plus the
  core-library hoist (`find_problem_dirs`, `preview_to_json`) that both
  needed. Zero new server API surface — `cxxprobe serve`'s existing
  REST/SSE contract is untouched.
- **Phase 2 (next)** — Problem-setting tooling: validators (input-format
  validation, distinct from checkers), generators (test-case generation
  via a user-provided program), and capturing checker stderr into the
  structured `JudgeReport`. This is the most product-critical gap — it's
  genuine Polygon/testlib-style authoring tooling that doesn't exist in
  any form yet.
- **Phase 3 (later)** — Stress/differential testing (built-in, not just a
  documented bash-loop pattern), checker-correctness verification,
  weak-testcase detection, benchmarking, dry-run mode, rejudge support,
  plugin architecture.
- **Deferred, separate project** — Multi-language support. cxxprobe stays
  C++-only for now; this would touch the compile step, sandbox invocation
  model (no compile step for interpreted languages), and per-language
  resource defaults — architecturally invasive enough to scope on its own.
- **Owned elsewhere, not part of this repo** — AWS infrastructure
  (Terraform/CDK, ECS/RDS/S3/SQS/ElastiCache/Firecracker), the SQS/S3
  worker adapter that wraps `cxxprobe judge` per job, and AMS Access itself
  (the DASH web dashboard, the Go backend, the proctor/desktop client).
