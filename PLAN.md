# cxxprobe — Development Roadmap

## Phase 0 — Repository Skeleton ✅

- ✅ CMake project with library, CLI, and tests
- ✅ Conan integration (GTest, yaml-cpp, CLI11, nlohmann_json)
- ✅ Pre-commit hooks (clang-format, cmake-format, conventional commits)
- ✅ CI pipeline (GitHub Actions): build matrix (GCC/Clang × Debug/Release), sanitizers, cppcheck, clang-tidy
- ✅ Semantic versioning & release automation

## Phase 1 — Core Sandbox Library ✅ (v0.3.x–v0.4.0)

- ✅ `cxxprobe::sandbox::run()` via `clone(CLONE_NEWUSER|CLONE_NEWNS)` + a cgroup v2 leaf per run
- ✅ `Limits` enforcement: `memory.max` (cgroup), `pids.max` (cgroup), `RLIMIT_AS` fallback
  when cgroup migration is blocked by `nsdelegate`
- ✅ `RLIMIT_CPU` enforcement — a fast kernel-level backstop that kills a CPU-bound
  runaway well before the (typically larger) wall-clock limit fires; `cpu.stat`
  remains the source of truth for exact `TLE` classification (`src/sandbox/detail/child.cpp`)
- ✅ Wall-clock `timerfd` + `epoll` + `cgroup.kill`/`SIGKILL`
- ✅ CPU/memory accounting via `cpu.stat` / `memory.peak`
- ✅ Unit and integration tests: memory limits, wall-clock limits, RLIMIT_CPU limits,
  stdin/stdout capture, output caps, cgroup RAII lifecycle

## Phase 2 — Config, Symbolic & Behavior Checks ✅ (v0.4.0)

- ✅ `cxxprobe::problem::ProblemConfig` — `problem.yaml` parser (compiler/limits
  overrides, manual/symbolic/behavior sections, `enabled` inference)
- ✅ `cxxprobe::symbolic` — comment/string-aware `must_include`/`must_not_include`
  source scanner (regex or literal substring)
- ✅ `cxxprobe::gtest_report` — parses GTest's `--gtest_output=json`
- ✅ `cxxprobe::compile` — sandboxed single-invocation compile wrapper (protects
  against compile-time bombs)
- ✅ `cxxprobe::cases` — shared test-case loading/judging (extracted from the CLI
  so both `run --cases` and `judge::run_problem` use one implementation)
- ✅ `cxxprobe::judge::run_problem` — aggregates all 3 consolidated test types into
  one report; embeddable directly by a judge platform, not just via the CLI
- ✅ `nlohmann::json` adopted project-wide, replacing hand-rolled JSON string
  building in the CLI

## Phase 3 — CLI & Contest Orchestration ✅ (v0.4.0)

- ✅ `cxxprobe run <flags> program` — rename of the original flat-flag single/batch
  mode, zero behavior change
- ✅ `cxxprobe new contest "Name"` / `cxxprobe new problem "Name"` — scaffolding
  (`contest.yaml` marker, `problem.yaml`, `problem.md`, `solution_template.cpp`,
  `checker_gtest.cpp`, `tests/`)
- ✅ `cxxprobe test problem "Name" [--submission path.cpp]` — runs all 3
  consolidated test types, human + `--json` output; `--submission` grades an
  arbitrary source file through the same pipeline (contestant grading, not just
  reference-solution validation)
- Deliberately **not** included: tool-enforced problem ordering/lettering —
  contest organizers manage presentation order themselves; problem folders are
  freeform slugified titles

## Phase 4 — HTTP Judging Service ✅ (v0.8.0)

- ✅ `cxxprobe serve` — exposes `problem::load_from_dir` → `judge::run_problem`
  (the same pipeline `test problem` uses) as a queued, worker-pooled REST + SSE
  service, for a contest platform to submit against instead of shelling out per
  submission. Not a contest platform itself — no users, no auth, no
  scoreboards; deliberately out of scope, someone else's job.
- ✅ `ISubmissionQueue` (moodycamel-backed) / `IJudgeService` / `ISubmissionRepository`
  (SQLite, hand-written RAII wrapper, WAL mode) / `IEventBus` (in-process
  pub/sub) — each an interface with one concrete in-process implementation
  today, so a future distributed deployment (Redis/SQS queue, Postgres,
  Redis Pub/Sub) only changes what's constructed in the composition root,
  never `Worker`/`WorkerManager`/handler code
- ✅ Fixed-size worker pool (`--workers`, `std::jthread`-based) caps concurrent
  sandboxed compiles/runs regardless of request burst size; per-submission
  exception isolation (a bad submission can't take a worker down permanently)
- ✅ Hand-rolled router/middleware chain on synchronous Boost.Beast (one
  request per connection, no keep-alive — a deliberate simplification);
  `LoggingMiddleware`/`CorsMiddleware`/`ErrorMappingMiddleware`
- ✅ `GET /health`, `/metrics` (Prometheus text + JSON), `/problems`,
  `/problems/{slug}`, `POST /submissions`, `GET /submissions/{id}`,
  `GET /submissions` (history), `GET /events` (SSE, global or
  `?submission_id=`-scoped)
- ✅ `--ui` — an embedded developer UI (vanilla JS + Alpine.js + htmx +
  CodeMirror 5, vendored static files, zero Node.js at build or runtime;
  embedded into the binary at C++ build time via a small Python generator
  script). Talks exclusively through the public REST/SSE API — no
  privileged path into server internals. For local testing/debugging only,
  not a contest-platform replacement.
- ✅ Graceful shutdown on `SIGINT`/`SIGTERM` — stop accepting, let in-flight
  judging finish, then exit
- Deliberately **not** included (see [HTTP Judging Service
  architecture](docs/content/architecture/http-service.mdx)): auth, gRPC,
  multi-language judging, live contest-directory reload — each is a
  documented extension point, not built

---

## Deferred / Backlog

No committed timeline. Revisit if/when a concrete need shows up.

- **libclang-based static analysis** / declarative AST-query rule engine —
  superseded for now by the comment/string-aware regex approach in Phase 2;
  revisit only if regex-based symbolic checks prove insufficient in practice
  (libclang isn't cleanly Conan-installable and is a large added surface)
- **Doxygen API documentation** generation
- **Parallel worker-pool** batch/contest evaluation for the CLI (today:
  `run --cases` and a grading loop over `test problem` are both sequential;
  parallelize externally via `xargs -P` / GNU `parallel` in the meantime —
  `cxxprobe serve`'s worker pool, added in Phase 4, is a separate thing)
- **Windows support** (Job Objects) — cxxprobe is Linux-only by design (cgroup v2
  + user namespaces); WSL2 is the supported Windows path
- **gRPC facade** alongside `cxxprobe serve`'s REST + SSE API — the service
  layer is already shaped for this as a pure-addition adapter
- **Distributed deployment** of `cxxprobe serve` (Redis/SQS queue, Postgres
  repository, Redis Pub/Sub event bus) — the interfaces exist, the concrete
  distributed implementations don't yet
- **Web dashboard** for contest administration (not the same as `serve --ui`,
  which is a local dev tool, not an admin console)
- ConanCenter publication, formal security audit, LTS policy
