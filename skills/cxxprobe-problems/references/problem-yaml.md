# `problem.yaml` — full field reference

Every field except `version` and `name` is optional. Omitted fields fall back
to the defaults shown. Write the minimum your problem actually needs: a file
full of explicit nulls is harder to read than a short one, and the defaults
are the values you would have chosen anyway.

```yaml
version: 2                    # REQUIRED. Must be 2.
name: "A: Sum Two Numbers"    # REQUIRED. Shown to contestants; also a lookup key.

statement:
  dir: statement
  entry: problem.md

compiler:
  cxx: null                   # → g++
  std: null                   # → c++23
  flags: null                 # → -O2 -Wall
  extra_sources: []           # extra .cpp compiled alongside the submission

limits:
  memory_mb: null             # → 256
  cpu: null                   # → 5s
  wall: null                  # → 10s
  pids: null                  # → 64

tests:
  dir: tests
  manifest: null              # mutually exclusive with dir

checker:
  dir: checker
  io:
    entry: checker.cpp        # testlib-ABI output checker
    extra_flags: []
  behavior:
    entry: behavior_gtest.cpp # GTest-linked internal-API check
    extra_flags: []
    enabled: true             # set false to turn the section off explicitly

validator:
  dir: validator
  entry: validator.cpp

generators:
  dir: generators
  plan: plan.yaml

solutions:
  dir: solutions
  entries: []                 # inferred when solutions/ holds exactly one .cpp

symbolic:
  must_include: []
  must_not_include: []

attachments:
  dir: attachments            # copied to contestants verbatim, never parsed
```

## `name` and slugs

The directory name is the **slug** — kebab-cased, and what most commands take
as an argument. `cxxprobe test problem` accepts either the slug or the exact
`name:` string.

Prefixing the name with a contest letter (`"A: Sum Two Numbers"`) is the
convention; it is what contestants see in the problem list.

## `limits`

Durations accept a unit suffix: `5s`, `1500ms`, `2m`.

Set `cpu` from the reference solution's measured time with real headroom —
`test problem` prints `cpu:` per case. A limit at 2× the reference is tight
enough to exclude a quadratic solution and loose enough to survive a slower
judging host. If you want to *prove* it excludes the slow one, declare a
`tle_*.cpp` with `expected_verdict: TLE` and check for `MISMATCH`.

These limits apply to submissions only. Validators and generators run with
fixed generous limits (512 MiB, 10 s CPU, 15 s wall) so setter tooling never
false-positives against a contestant-facing time limit.

## `solutions.entries`

```yaml
solutions:
  dir: solutions
  entries:
    - { file: main.cpp, primary: true }
    - { file: wa_off_by_one.cpp,  expected_verdict: WA }
    - { file: tle_quadratic.cpp,  expected_verdict: TLE }
    - { file: re_out_of_bounds.cpp, expected_verdict: RE }
```

Verdicts: `AC`, `WA`, `TLE`, `MLE`, `OLE`, `RE`. There is deliberately no
`CE` — a solution that does not compile is a broken package, not a test
case, and `test problem` reports it as `ERROR` (exit 2) rather than a
verdict.

Exactly one entry must be `primary: true`, unless there is only one entry.
With no `entries:` block, a single `.cpp` in `solutions/` is inferred as
primary — two or more is ambiguous and fails to load.

Non-primary entries are judged against the same manual tests and reported in
an "Additional solutions" section. A `MISMATCH` there means your test data is
not doing the job you assumed it was.

## `symbolic`

Two list forms, freely mixed:

```yaml
symbolic:
  must_include:
    - "std::bit_cast"                       # literal substring
    - pattern: "constexpr\\s+\\w+\\s+\\w+"  # regex
      regex: true
      message: "the answer must be computed at compile time"
  must_not_include:
    - pattern: "\\bmemcpy\\s*\\("
      regex: true
      message: "memcpy for type punning is UB — use std::bit_cast"
    - "#include <bits/stdc++.h>"
```

The scan strips comments and string literals before matching, so a mention in
a comment does not trip a `must_not_include`. YAML needs backslashes doubled:
write `\\b`, not `\b`.

Symbolic checks are a blunt instrument by design — a regex scan, not a parse.
Use them for "did they reach for the intended technique", not as a security
boundary. Anything you truly must prevent belongs in a behaviour checker.

## `tests.manifest`

An alternative to a `tests/` directory when you want explicit control over
case labels, ordering, or inline data. Mutually exclusive with `dir:` — set
one or the other, never both.

## `compiler.extra_sources`

Additional `.cpp` files compiled alongside the submission — a provided
implementation the contestant is meant to use rather than write. Pair with
`attachments/` for the matching header.
