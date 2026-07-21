# cxxprobe

**Run untrusted C++ safely, judge it automatically, and grade a whole contest
without writing a single line of grading infrastructure.**

[![CI](https://github.com/Algorithms-Mathematics-Society/cxxprobe/actions/workflows/ci.yml/badge.svg)](https://github.com/Algorithms-Mathematics-Society/cxxprobe/actions/workflows/ci.yml)
[![Docs](https://img.shields.io/badge/docs-live-3ba1a1)](https://algorithms-mathematics-society.github.io/cxxprobe/)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)

cxxprobe is built for coding contests, technical interviews, and classroom
grading — anywhere you need to run someone else's C++ and trust the result.
It sandboxes the program (real memory/CPU/wall-clock/process limits enforced
by the Linux kernel, not best-effort polling), then judges it three
different ways at once: does the output match, did they actually use the
technique the problem is testing for, and does the implementation actually
*behave* correctly — not just produce the right stdout.

```bash
cxxprobe new contest "AMS Ascent"
cxxprobe new problem "A: FileReader RAII"
# fill in solution.cpp, then:
cxxprobe test problem a-filereader-raii
```

```text
Problem: A: FileReader RAII (a-filereader-raii)

Manual tests    PASS (3/3)
Symbolic checks PASS
Behavior tests  PASS (4/4)

---
Overall: PASS
```

## 📖 Full documentation

**→ [algorithms-mathematics-society.github.io/cxxprobe](https://algorithms-mathematics-society.github.io/cxxprobe/)**

Start with [Getting Started](https://algorithms-mathematics-society.github.io/cxxprobe/getting-started)
to build it, or jump straight into [Build a Contest](https://algorithms-mathematics-society.github.io/cxxprobe/guides/build-a-contest)
for a full worked walkthrough.

## What it does

- **Sandboxes real submissions safely.** Memory, CPU time, wall-clock time,
  and process count are enforced by the kernel (cgroup v2 + Linux user
  namespaces) — a fork bomb or an infinite loop can't take down your grading
  host.
- **Scaffolds a whole contest for you.** `cxxprobe new contest` /
  `cxxprobe new problem` generate the problem statement, solution template,
  test directory, and checker template — no hand-rolled folder conventions.
- **Judges three different ways, together.** Manual `.in`/`.out` test cases,
  source-level checks ("did they use `std::bit_cast`, not `memcpy`?"), and a
  GTest-linked behavior checker that tests the actual implementation — mix
  and match per problem.
- **Grades contestants, not just your reference solution.**
  `cxxprobe test problem --submission path/to/contestant.cpp` runs the exact
  same three-way judging against anyone's code.
- **Speaks JSON.** Every command has a `--json` mode built for grading
  pipelines and CI, not just terminal output.

## Requirements

Linux with cgroup v2 (including WSL2). See
[Getting Started](https://algorithms-mathematics-society.github.io/cxxprobe/getting-started)
for the full build (CMake + Conan) and one-time setup.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the development setup, code style,
and PR process.

## License

[GPLv3](LICENSE)
