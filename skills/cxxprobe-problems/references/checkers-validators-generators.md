# Checkers, validators and generators

Three separate tools that are easy to conflate. They answer different
questions and fail in different ways:

| | Judges | Lives in | Run by |
|---|---|---|---|
| **Output checker** | the submission's *output* | `checker/checker.cpp` | every judge run |
| **Behaviour checker** | the submission's *code* | `checker/behavior_gtest.cpp` | every judge run |
| **Validator** | the test's *input* | `validator/validator.cpp` | `cxxprobe validate` |
| **Generator** | — (produces inputs) | `generators/*.cpp` + `plan.yaml` | `cxxprobe generate` |

---

## Output checkers

Needed whenever exact token comparison is wrong: floating point with
tolerance, several valid answers, or "print any one correct output".

### The ABI

```text
checker <input_file> <output_file> <answer_file>
exit 0        → accepted
exit non-zero → wrong answer
stderr        → passed through to you as the diagnostic
stdout        → discarded
```

[testlib.h](https://github.com/MikeMirzayanov/testlib) checkers — the
Codeforces/Polygon format — work unmodified. cxxprobe does not vendor testlib
and does not need to.

### A checker without any library

```cpp
// checker/checker.cpp — floating point within 1e-6
#include <cmath>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    std::ifstream out(argv[2]);
    std::ifstream ans(argv[3]);
    double got, want;
    if (!(out >> got) || !(ans >> want)) {
        std::cerr << "missing numeric output\n";
        return 1;
    }
    if (std::abs(got - want) > 1e-6) {
        std::cerr << "expected " << want << ", got " << got << "\n";
        return 1;
    }
    return 0;
}
```

Write a real diagnostic to stderr. "wrong answer" tells a contestant nothing;
"expected 3.14159, got 3.14" tells them whether they have a precision bug or
an algorithmic one.

### Ad-hoc use outside a package

```bash
cxxprobe run -i 1.in -e 1.ans --checker ./checker ./solution
cxxprobe run --cases tests/ --checker ./checker ./solution
```

`--checker` must be combined with `--expected` or `--cases`; alone it is a
usage error (exit 2). The path is resolved relative to the working directory,
**not** `$PATH` — write `./checker`.

---

## Behaviour checkers

Compiled together with the submission, so they can assert on its internal API:
that a type is move-only, that a destructor releases, that a container exposes
the right interface. This is how you set a problem about *writing C++*
rather than about computing an answer.

```cpp
// checker/behavior_gtest.cpp
#include <gtest/gtest.h>

#define main solution_main          // rename the submission's main() out of
#include CXXPROBE_SOLUTION_FILE     // the way; GTest supplies its own
#undef main

TEST(FileHandle, OpensSuccessfully) {
    FileHandle f{std::tmpfile()};
    EXPECT_TRUE(f.is_open());
}

TEST(FileHandle, MoveTransfersOwnership) {
    FileHandle a{std::tmpfile()};
    FileHandle b{std::move(a)};
    EXPECT_TRUE(b.is_open());
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_FALSE(a.is_open());
}

TEST(FileHandle, NotCopyable) {
    EXPECT_FALSE(std::is_copy_constructible_v<FileHandle>);
    EXPECT_FALSE(std::is_copy_assignable_v<FileHandle>);
}
```

Two things are load-bearing:

- **`CXXPROBE_SOLUTION_FILE`**, never a hardcoded path. The macro points at
  whichever file is being graded, which is what makes `--submission` work.
- **`#define main solution_main`**, or the submission's `main()` collides with
  GTest's and the link fails with a duplicate symbol.

A behaviour-checked problem usually still wants one trivial manual test, just
to confirm the program runs at all.

Static assertions (`std::is_copy_constructible_v`, `std::is_nothrow_*`) are
the cheapest checks to write and among the most valuable — they catch a
missing `= delete` that no runtime test would.

---

## Validators

A validator decides whether a *test input* is legal. Without one, a malformed
test silently punishes contestants for your mistake: a solution that correctly
assumes `n ≥ 1` gets a WA on the input where you typed `0`.

### The protocol

```text
validator <label>
  stdin         = the test's .in content
  exit 0        → valid
  exit non-zero → invalid, stderr is the diagnostic
```

This is testlib's `registerValidation()` / `ensure()` convention, so a testlib
validator works unmodified.

```cpp
// validator/validator.cpp
#include "testlib.h"

int main(int argc, char* argv[]) {
    registerValidation(argc, argv);
    int n = inf.readInt(1, 100000, "n");
    inf.readEoln();
    for (int i = 0; i < n; ++i) {
        inf.readInt(-1000000000, 1000000000, "a_i");
        inf.readEoln();
    }
    inf.readEof();          // nothing after the last line
    return 0;
}
```

Without any library, the contract is four lines:

```cpp
#include <cstdio>
int main() {
    long long a, b;
    if (std::scanf("%lld %lld", &a, &b) != 2) { std::fprintf(stderr, "expected two integers\n"); return 1; }
    if (a < -1000000000 || a > 1000000000)    { std::fprintf(stderr, "a out of range\n");        return 1; }
    if (std::getchar() != '\n')               { std::fprintf(stderr, "trailing junk\n");         return 1; }
    return 0;
}
```

`readEof()` / the trailing-junk check matters more than it looks: a stray
blank line at the end of a generated file is the single most common malformed
test.

Run with `cxxprobe validate <slug>`. Exit `0` valid (or no validator
configured — a skip, not a failure), `1` a case was rejected, `2` usage or
compile error.

Validators run with fixed generous limits (512 MiB, 10 s CPU, 15 s wall),
independent of the problem's own `limits:`.

---

## Generators

Generators write `.in` files **only** — never `.ans`. Answers come from
running the reference solution, which is the point: a generator that also
produced answers would just be a second implementation to get wrong.

```cpp
// generators/gen_random.cpp
#include <cstdio>
#include <cstdlib>
#include <random>

int main(int argc, char** argv) {
    int n = argc > 1 ? std::atoi(argv[1]) : 10;
    unsigned seed = argc > 2 ? std::atoi(argv[2]) : 42;

    std::mt19937 rng{seed};
    std::uniform_int_distribution<int> dist(1, 1000000000);

    std::printf("%d\n", n);
    for (int i = 0; i < n; ++i) std::printf("%d\n", dist(rng));
    return 0;
}
```

**Seed from `argv`, never from the clock.** A generator that is not
reproducible turns "this test fails" into an unreproducible report.

```yaml
# generators/plan.yaml — one entry per test
- { generator: gen_random.cpp, args: ["10", "1"] }
- { generator: gen_random.cpp, args: ["1000", "2"] }
- { generator: gen_random.cpp, args: ["100000", "3"], label: max }
- { generator: gen_edge.cpp,   args: ["1"],           label: minimal }
```

`label` is optional; unlabelled entries take the next unused numeric stem
under `tests/`, continuing your existing numbering rather than colliding with
it. With `1.in` and `2.in` already present, the first two entries become
`3.in` and `4.in`.

```bash
cxxprobe generate <slug>              # write the inputs
cxxprobe generate <slug> --dry-run    # run everything, write nothing
cxxprobe generate <slug> --force      # overwrite existing files
cxxprobe generate <slug> --strict     # non-zero exit if a case fails validation
```

Generated cases are validated automatically when a validator exists, unless
you pass `--no-validate`. Use `--strict` in CI so a generator that drifts out
of the stated constraints fails the build rather than quietly shipping.

After generating, produce the answers by running the reference solution over
each new `.in`, then re-run `cxxprobe test problem` to confirm it still passes.
