---
name: cxxprobe-problems
description: Author, verify and package competitive-programming problems for cxxprobe — the v2 package layout (problem.yaml, statement/, tests/, solutions/, validator/, checker/, generators/), the three independent check types (manual I/O, symbolic source scan, GTest behaviour), and the CLI that runs them. Use when creating or editing a cxxprobe problem or contest, writing test data, checkers, validators or generators, debugging a FAIL/ERROR from `cxxprobe test problem`, or packing problems for import into AMS Access.
---

# Authoring cxxprobe problems

A cxxprobe problem is a directory with `problem.yaml` at its root. Each part
lives in a named subdirectory, and **every optional part is enabled by the
presence of its entry file** — there is no flag to remember. Drop in
`validator/validator.cpp` and validation turns on.

Your job when authoring is not to produce a problem that *looks* right. It is
to produce one where the reference solution passes, wrong solutions fail, and
the test data is strong enough to tell them apart. cxxprobe gives you a way to
prove all three. Use it before declaring anything done.

## Before you start

Check which CLI you have, because the scaffolding command differs:

```bash
cxxprobe --version
cxxprobe --help | grep -E '^\s+(package|new)'
```

- **`package` subcommand present** → `cxxprobe package init "Name"`
- **only `new`** (v0.14.0 and earlier) → `cxxprobe new problem "Name"`

Everything else in this skill is identical on both. When a command below
shows `package init`, substitute `new problem` on older builds.

## The shape of a problem

```
sum-two-numbers/
  problem.yaml          required
  statement/problem.md  the statement contestants read
  tests/1.in 1.ans      manual I/O tests
  solutions/main.cpp    the reference solution — required
  validator/validator.cpp     optional: are the *inputs* legal?
  checker/checker.cpp         optional: is the *output* acceptable?
  checker/behavior_gtest.cpp  optional: is the *code* right? (RAII, types, API)
  generators/gen_*.cpp + plan.yaml   optional: generate test inputs
  attachments/          optional: handed to contestants verbatim
```

Only `problem.yaml` and a solution are required. `package init` deliberately
does not create the optional directories — an empty one would be noise when
presence is what enables the feature.

## The workflow that actually works

```bash
cxxprobe new contest "Round 1"       # once per contest
cd round-1
cxxprobe package init "Sum Two Numbers"   # or: cxxprobe new problem "..."
# write statement, solution, tests
cxxprobe test problem sum-two-numbers
```

`cxxprobe test problem` runs all three check types and prints one verdict.
**Run it after every change.** Exit codes: `0` PASS, `1` FAIL (a real test
failure), `2` ERROR (compile failure, missing problem, bad config) — the
distinction matters when scripting.

Add `--json` for machine-readable output, `--submission PATH` to grade some
other file instead of the reference solution.

## problem.yaml

Write the minimum. Every omitted field falls back to a sane default, and a
file full of nulls is harder to read than a short one.

```yaml
version: 2                    # must be 2; v1 is not accepted and has no shim
name: "A: Sum Two Numbers"

statement:
  dir: statement
  entry: problem.md

solutions:
  dir: solutions

tests:
  dir: tests

checker:
  behavior:
    enabled: false            # say so explicitly — see below
```

Defaults when omitted: compiler `g++`, `c++23`, `-O2 -Wall`; limits 256 MiB,
5 s CPU, 10 s wall, 64 pids.

**Turn the behaviour checker off explicitly** rather than just deleting
`checker/behavior_gtest.cpp`. Anyone reading the file later then knows there
is deliberately no behaviour checker, not a forgotten one.

Full field reference: `references/problem-yaml.md`.

## The three check types

They are independent and answer different questions. A problem may use any
combination.

| Type | Question | Enabled by |
|---|---|---|
| **Manual** | Does it print the right output? | `tests/` contains a `.in` |
| **Symbolic** | Does the source use the required technique? | `symbolic:` in problem.yaml |
| **Behaviour** | Is the code itself right — RAII, move semantics, types? | `checker/behavior_gtest.cpp` |

Each reports `PASS | FAIL | SKIPPED | ERROR` separately. `SKIPPED` means not
configured; `ERROR` means infrastructure failed (usually a compile error), not
that a test failed.

### Manual tests

`tests/1.in` with `tests/1.ans`. Default comparison is exact token match.

**The most common authoring bug is invisible whitespace.** `printf '%s'` and
`echo` differ; a stray `\r` or missing trailing newline shows up as a WA you
cannot see. When a test fails and the output looks identical, check the bytes:

```bash
cat -A tests/2.in    # $ marks line ends, ^M marks CR
```

### Symbolic checks

For problems where *how* the answer was reached matters — "use `std::bit_cast`,
not `memcpy`".

```yaml
symbolic:
  must_include:
    - "std::bit_cast"                    # bare string = literal substring
  must_not_include:
    - pattern: "\\bmemcpy\\s*\\("        # map form = regex + custom message
      regex: true
      message: "memcpy for type punning is UB — use std::bit_cast"
```

The scan strips comments and string literals first, so a solution that only
*mentions* the forbidden call in a comment does not trip. Both forms mix
freely in one list. Escape backslashes for YAML (`\\b`, not `\b`).

### Behaviour checks

Compiled *together with* the submission, so you can assert on its internal
API. Two rules, both load-bearing:

```cpp
#include <gtest/gtest.h>

#define main solution_main          // the submission's main() must not
#include CXXPROBE_SOLUTION_FILE     // collide with GTest's own
#undef main

TEST(FileHandle, MoveTransfersOwnership) {
    FileHandle a{std::tmpfile()};
    FileHandle b{std::move(a)};
    EXPECT_TRUE(b.is_open());
    EXPECT_FALSE(a.is_open());
}

TEST(FileHandle, NotCopyable) {
    EXPECT_FALSE(std::is_copy_constructible_v<FileHandle>);
}
```

Use `CXXPROBE_SOLUTION_FILE`, never a hardcoded `#include "solutions/main.cpp"`
— the macro points at whichever file is being graded, which is what makes
`--submission` work.

## Proving the test data is strong enough

This is the part most authors skip, and it is the reason `test problem` has an
"Additional solutions" section. Write solutions you *expect* to fail, and
declare what they should earn:

```yaml
solutions:
  dir: solutions
  entries:
    - { file: main.cpp, primary: true }
    - { file: wa_off_by_one.cpp, expected_verdict: WA }
    - { file: tle_quadratic.cpp, expected_verdict: TLE }
```

```text
Additional solutions
  wa_off_by_one.cpp    expected WA   got WA   OK
  tle_quadratic.cpp    expected TLE  got AC   MISMATCH
```

A `MISMATCH` is the signal worth having: the quadratic solution was supposed
to time out and did not, so the tests are too small to separate an efficient
solution from a slow one. Mismatches do **not** change the primary solution's
Pass/Fail — "is the reference correct?" and "are the tests strong?" are
separate questions with separate answers.

Exactly one entry needs `primary: true`. With no `entries:` at all, a single
`.cpp` in `solutions/` is inferred as primary; two or more is ambiguous and
fails to load.

## Validators and generators

A **checker** judges output; a **validator** judges *input*. Without a
validator, a malformed test silently punishes contestants for your mistake.

`validator/validator.cpp` gets the test on stdin and its label as `argv[1]`;
exit 0 means legal, non-zero means illegal with stderr as the diagnostic.
This is testlib's `registerValidation()` convention, so testlib validators
work unmodified. Run with `cxxprobe validate <slug>`.

**Generators** write `.in` files only — never `.ans`. Declare them in
`generators/plan.yaml`, then `cxxprobe generate <slug>`:

```yaml
- { generator: gen_random.cpp, args: ["10", "1"] }
- { generator: gen_random.cpp, args: ["100000", "3"], label: max }
```

Unlabelled entries take the next unused numeric stem, continuing your
numbering rather than colliding with it. Generated cases are validated
automatically unless you pass `--no-validate`.

Details and full examples: `references/checkers-validators-generators.md`.

## Custom output checkers

The default exact-token comparison is wrong for floating point, multiple
valid answers, or "print any correct output". Write `checker/checker.cpp` to
the testlib ABI:

```text
checker <input_file> <output_file> <answer_file>
exit 0 → accepted,  non-zero → wrong answer
stderr → shown to you as the diagnostic,  stdout → discarded
```

Codeforces/Polygon testlib checkers work unmodified.

## Packing for AMS Access

```bash
cxxprobe package pack -C round-1 -o round-1.zip     # or: cxxprobe pack
```

Produces a flat zip (`manifest.json`, `contest.yaml`, one directory per
problem) that any `unzip` opens and that AMS Access imports directly. Upload
it in the org portal under Problems.

## Before you call a problem done

Work through this. Each line is a failure that has actually happened.

1. `cxxprobe test problem <slug>` → **Overall: PASS**
2. The statement's constraints match what the tests actually contain
3. A validator exists for anything non-trivial, and `cxxprobe validate <slug>`
   passes
4. At least one deliberately-wrong solution is declared, and it earns the
   verdict you expected — no `MISMATCH`
5. Edge cases are present: minimum, maximum, and whatever is degenerate for
   this problem (empty, single element, all-equal, negative)
6. The largest test actually approaches the stated limit — a `10^5` constraint
   tested only up to `10` proves nothing
7. `cat -A` on any test whose failure you could not explain

## Gotchas

- `version: 2` is mandatory. The v1 layout (loose `problem.md`,
  `solution.cpp`, `checker_gtest.cpp` at the problem root) is rejected with no
  compatibility shim.
- The problem's own `limits:` do **not** apply to validators or generators —
  they run with generous fixed limits so setter tooling never false-positives
  a TLE against a contestant-facing time limit.
- A checker path passed to `--checker` is resolved relative to the working
  directory, not `$PATH`. Use `./checker`.
- `ERROR` is not `FAIL`. If `test problem` exits 2, read the compile output —
  you have a broken build, not a failing test.
- Slugs are directory names, kebab-cased from the name. `cxxprobe test problem`
  accepts either the slug or the exact `name:` field.
