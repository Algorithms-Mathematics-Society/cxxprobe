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

---

## Deferred / Backlog

No committed timeline. Revisit if/when a concrete need shows up.

- **libclang-based static analysis** / declarative AST-query rule engine —
  superseded for now by the comment/string-aware regex approach in Phase 2;
  revisit only if regex-based symbolic checks prove insufficient in practice
  (libclang isn't cleanly Conan-installable and is a large added surface)
- **Doxygen API documentation** generation
- **Parallel worker-pool** batch/contest evaluation (today: `run --cases` and a
  grading loop over `test problem` are both sequential; parallelize externally
  via `xargs -P` / GNU `parallel` in the meantime)
- **Windows support** (Job Objects) — cxxprobe is Linux-only by design (cgroup v2
  + user namespaces); WSL2 is the supported Windows path
- **Distributed evaluation server** (gRPC API)
- **Web dashboard** for contest administration
- ConanCenter publication, formal security audit, LTS policy
