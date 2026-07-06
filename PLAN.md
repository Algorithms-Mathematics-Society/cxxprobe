# cxxprobe — Development Roadmap

## Phase 0 — Repository Skeleton ✅

- ✅ CMake project with library, CLI, and tests
- ✅ Conan integration (GTest, yaml-cpp)
- ✅ Pre-commit hooks (clang-format, cmake-format, conventional commits)
- ✅ CI pipeline (GitHub Actions): build matrix (GCC/Clang × Debug/Release), sanitizers, cppcheck, clang-tidy
- ✅ Semantic versioning & release automation

## Phase 1 — Core Sandbox Library (v0.5.0)

- [ ] Implement `cxxprobe::sandbox::run()` with `fork/exec` and pipe capture
- [ ] `Limits` enforcement (`RLIMIT_CPU`, `RLIMIT_AS`)
- [ ] cgroups v2 integration for accurate physical memory limits
- [ ] CPU time monitoring via `/proc/stat`
- [ ] Unit and integration tests: time limits, memory limits, file limits, concurrency

## Phase 2 — Config & Test Harness (v0.6.0)

- [ ] `cxxprobe::config::ProblemConfig` YAML parser
- [ ] GTest helpers (`EXPECT_SANDBOX_SUCCESS`, `EXPECT_MEMORY_LE`)
- [ ] Compile-and-run abstraction for contestant code
- [ ] Example problem (RAII) with full pipeline

## Phase 3 — CLI & Contest Orchestration (v0.7.0)

- [ ] `cxxprobe init`, `new-problem`, `validate`
- [ ] `cxxprobe test` (single submission)
- [ ] Contest discovery and batch evaluation (`test-contest`)
- [ ] Parallel evaluation with configurable worker pool

## Phase 4 — Static Analysis (v0.8.0)

- [ ] Integrate libclang
- [ ] `cxxprobe::static_analysis::Checker` base class
- [ ] Declarative rule engine (YAML → AST queries)
- [ ] Clang-Tidy plugin support
- [ ] Result aggregation with runtime/static weights

## Phase 5 — Documentation & Beta (v0.9.0)

- [ ] Complete API documentation (Doxygen)
- [ ] Tutorial: "Your first contest"
- [ ] Beta testing with partner organizations

## Phase 6 — Production Release (v1.0.0)

- [ ] Security audit (sandbox escape, resource exhaustion)
- [ ] Performance benchmarking
- [ ] ConanCenter publication
- [ ] Long-term support policy

## Phase 7 — Post-Release

- [ ] Windows support (Job Objects)
- [ ] Property-based testing (RapidCheck)
- [ ] Distributed evaluation server (gRPC API)
- [ ] Web dashboard for contest administration
