# cxxprobe CLI Usage Guide

`cxxprobe` runs a program inside a Linux sandbox (cgroup v2 + user namespace),
captures its output, enforces resource limits, and optionally judges the result
against expected output.

> **Requires Linux with cgroup v2.** Running as root (or with write access to
> `/sys/fs/cgroup`) is needed for the sandbox. CI (ubuntu-24.04) has root
> access; local machines may need `sudo` or `CXXPROBE_ALLOW_UNPRIVILEGED=1`.

---

## Quick reference

```
cxxprobe [OPTIONS] program [args...]
```

| Flag | Short | Default | Description |
|---|---|---|---|
| `--memory-mb N` | `-m` | `256` | Memory limit in MiB |
| `--cpu DURATION` | `-t` | `5000ms` | CPU time limit |
| `--wall DURATION` | `-w` | `10000ms` | Wall-clock limit |
| `--pids N` | `-p` | `64` | Max process count |
| `--input FILE` | `-i` | (empty) | Stdin file; `-` reads from pipe |
| `--expected FILE` | `-e` | (none) | Expected output — enables verdict |
| `--checker PATH` | | (none) | Custom checker binary (testlib ABI) |
| `--cases PATH` | | (none) | Batch mode: directory or manifest |
| `--json` | | off | Emit result as JSON |
| `--quiet` | `-q` | off | Suppress metadata line |
| `--no-color` | | off | Disable ANSI colors |
| `--version` | `-V` | | Print version and exit |

### Duration format

`--cpu` and `--wall` accept three forms:

```
--wall 2s       # 2 seconds
--wall 500ms    # 500 milliseconds
--wall 2000     # raw milliseconds (backward-compatible)
```

---

## 1. Basic sandbox run

Run a program, capture its output, see resource usage:

```bash
cxxprobe ./solution
```

Output:
```
<stdout of solution>
[exit:0 | cpu:42ms | wall:47ms | mem:3.2MiB]
```

Metadata line is on **stderr**; stdout passes through unchanged. Use `-q` to
suppress the metadata line (e.g. when you only care about output).

### Provide stdin from a file

```bash
cxxprobe -i tests/1.in ./solution
```

### Pipe stdin directly

```bash
echo "5" | cxxprobe -i - ./solution
```

The `-i -` form reads up to 4 MiB from real stdin into memory, then feeds it to
the sandboxed program.

### Custom limits

```bash
# 128 MiB RAM, 2 second wall clock, 1 second CPU
cxxprobe -m 128 -w 2s -t 1s ./solution

# Very tight: 32 MiB, 500 ms, 8 PIDs (single-threaded program)
cxxprobe -m 32 -w 500ms -t 500ms -p 8 ./solution
```

### Pass arguments to the program

```bash
cxxprobe ./checker input.txt output.txt answer.txt
```

Everything after the program path is passed through as argv.

---

## 2. JSON output

Add `--json` to get a machine-readable result:

```bash
cxxprobe --json -i 1.in ./solution
```

```json
{
  "exit_code": 0,
  "peak_memory_bytes": 3354624,
  "cpu_time_ms": 42,
  "wall_time_ms": 47,
  "stdout": "15\n",
  "stderr": ""
}
```

When judging is active (`-e` or `--cases`), a `"verdict"` field is added:

```json
{
  "exit_code": 0,
  "peak_memory_bytes": 3354624,
  "cpu_time_ms": 42,
  "wall_time_ms": 47,
  "verdict": "AC",
  "stdout": "15\n",
  "stderr": ""
}
```

---

## 3. Single-run judging with `-e` / `--expected`

Runs the program and compares its stdout against an expected output file:

```bash
cxxprobe -i 1.in -e 1.ans ./solution
```

Output:
```
15
[exit:0 | cpu:42ms | wall:47ms | mem:3.2MiB | AC]
```

### Verdict table

| Verdict | Meaning | Exit code |
|---|---|---|
| `AC` | Accepted — output matches expected | `0` |
| `WA` | Wrong Answer | `1` |
| `TLE` | Time Limit Exceeded (wall or CPU) | `1` |
| `MLE` | Memory Limit Exceeded | `1` |
| `OLE` | Output Limit Exceeded (>4 MiB) | `1` |
| `RE` | Runtime Error (non-zero exit) | `1` |

Verdict priority when multiple conditions trigger: **TLE > MLE > OLE > RE > WA/AC**.

### Exit codes (with judging active)

- `0` — AC
- `1` — any non-AC verdict (WA, TLE, MLE, OLE, RE)
- `2` — sandbox error (permission denied, binary not found, etc.)

### Exit codes (without judging)

- Child's exit code, pass-through
- If killed by signal: `128 + signal_number` (e.g. SIGKILL → `137`)
- `2` — sandbox error

---

## 4. Custom checker (`--checker`)

For problems where output isn't a fixed string (floating point, multiple valid
answers, interactive), provide a checker binary in **testlib ABI**:

```
checker <input_file> <output_file> <answer_file>
exit 0 → AC
exit non-zero → WA
```

Testlib checkers (Codeforces/polygon format) work out of the box:

```bash
cxxprobe -i 1.in -e 1.ans --checker ./checker ./solution
```

The checker's stderr is passed through to the terminal (useful for "wrong answer
on 3rd integer" style messages). The checker's stdout is discarded.

**Note:** `--checker` must be combined with `--expected` (single-run) or
`--cases` (batch mode). It is an error to supply `--checker` without one of
those.

**Note:** The checker path must be an absolute path or relative to the current
working directory (not resolved via `$PATH`). Use `./checker` not `checker`.

### Default checker (no `--checker`)

Without `--checker`, output is compared **token by token**, ignoring all
whitespace differences:

- `"1 2 3\n"` matches `"1  2   3"` ✓
- `"42\n"` matches `"42"` ✓
- `"1.0"` does **not** match `"1"` ✗ (tokens are strings, not numbers)

---

## 5. Batch mode (`--cases`)

Run the same binary against multiple test cases with one invocation.

### Directory format

Given a directory of paired files:
```
tests/
  1.in   1.ans
  2.in   2.ans
  10.in  10.ans
```

```bash
cxxprobe --cases tests/ ./solution
```

Files are sorted **numerically** (so `10.in` comes after `2.in`). Both `.ans`
and `.out` extensions are recognized for expected output. Cases without a
matching `.ans`/`.out` are run but not judged.

Output:
```
     1: AC    cpu:    42ms    wall:    47ms    mem:    3.2MiB
     2: WA    cpu:    38ms    wall:    43ms    mem:    2.9MiB
    10: TLE   cpu:  5000ms    wall:  5001ms    mem:   14.0MiB
---
1/3 passed
```

### YAML/JSON manifest

Create a `.yaml`, `.yml`, or `.json` manifest for more control. Paths are
relative to the manifest file.

```yaml
# cases.yaml
- input: "tests/1.in"
  answer: "tests/1.ans"

- input: "tests/2.in"
  answer: "tests/2.ans"
  label: "edge-empty"          # optional custom label

- input_data: "3\n1 2 3\n"    # inline input
  answer_data: "6\n"           # inline expected output
```

```bash
cxxprobe --cases cases.yaml ./solution
```

JSON manifests work identically (YAML is a superset of JSON):

```json
[
  {"input": "tests/1.in", "answer": "tests/1.ans"},
  {"input_data": "3\n1 2 3\n", "answer_data": "6\n", "label": "inline-3"}
]
```

### Batch with custom checker

```bash
cxxprobe --cases tests/ --checker ./checker ./solution
```

The checker is called for every case that has expected output.

### Batch JSON output

```bash
cxxprobe --json --cases tests/ ./solution
```

```json
{
  "results": [
    {"index": 1, "label": "1", "verdict": "AC", "exit_code": 0,
     "cpu_time_ms": 42, "wall_time_ms": 47, "peak_memory_bytes": 3354624},
    {"index": 2, "label": "2", "verdict": "WA", "exit_code": 0,
     "cpu_time_ms": 38, "wall_time_ms": 43, "peak_memory_bytes": 3041280},
    {"index": 3, "label": "10", "verdict": "TLE", "exit_code": -9,
     "cpu_time_ms": 5000, "wall_time_ms": 5001, "peak_memory_bytes": 14680064}
  ],
  "summary": {"passed": 1, "total": 3}
}
```

### Batch exit codes

- `0` — all judged cases passed (or no expected output provided)
- `1` — at least one non-AC verdict
- `2` — at least one sandbox error

---

## 6. Problemsetting workflows

### Validate your solution against a test set

```bash
# Structure: tests/{1..n}.in + tests/{1..n}.ans
cxxprobe -m 256 -w 2s --cases tests/ ./solution
```

### Validate with a checker (geometry / float problems)

```bash
cxxprobe -m 256 -w 2s --cases tests/ --checker ./checker ./solution
```

### Generate + judge in a pipeline

```bash
for i in $(seq 1 100); do
    ./gen $i > tmp.in
    ./brute < tmp.in > tmp.ans
    result=$(cxxprobe -i tmp.in -e tmp.ans --checker ./checker --json ./solution)
    verdict=$(echo "$result" | grep '"verdict"' | grep -oP '(?<=": ")[^"]+')
    if [ "$verdict" != "AC" ]; then
        echo "FAIL on seed $i: $verdict"
        cat tmp.in
        break
    fi
done
```

### Stress test two solutions

```bash
for i in $(seq 1 1000); do
    ./gen $i > tmp.in
    out_a=$(cxxprobe -i tmp.in -q ./sol_a)
    out_b=$(cxxprobe -i tmp.in -q ./sol_b)
    if [ "$out_a" != "$out_b" ]; then
        echo "Mismatch on seed $i"
        cat tmp.in
        break
    fi
done
```

### Judge with strict time limit and capture JSON for a grading script

```bash
cxxprobe \
  -m 256 -w 2s -t 1500ms \
  -i contestant_input.txt \
  -e expected.ans \
  --json \
  ./contestant_solution \
  > result.json

# Exit 0 = AC, 1 = non-AC
jq '.verdict' result.json
```

### Check memory usage of a solution

```bash
cxxprobe --json -i big_input.txt ./solution | jq '{"mem_mib": (.peak_memory_bytes/1048576|.*10|round/10), "cpu_ms": .cpu_time_ms}'
```

### Run without judging, capture stdout to a file

```bash
cxxprobe -i input.txt -q ./solution > output.txt
```

(Standard shell redirect works — no `--output-file` flag needed.)

---

## 7. Resource limits reference

| Resource | Flag | Default | Sandbox mechanism |
|---|---|---|---|
| Memory | `-m / --memory-mb` | 256 MiB | `memory.max` in cgroup v2 |
| Swap | (always 0) | 0 | `memory.swap.max=0` |
| CPU time | `-t / --cpu` | 5 s | `cpu.stat usage_usec` measured post-run |
| Wall clock | `-w / --wall` | 10 s | `timerfd` + `epoll` + `cgroup.kill` |
| PIDs | `-p / --pids` | 64 | `pids.max` in cgroup v2 |
| Stdout | (fixed) | 4 MiB | Drain thread discards after cap |
| Stderr | (fixed) | 4 MiB | Drain thread discards after cap |
| Stdin | (fixed) | 4 MiB | Capped when loading with `-i -` |

**Wall clock** is the primary kill mechanism. When the timer fires, `cgroup.kill`
(Linux 5.14+) is written and SIGKILL is sent as a fallback. The CPU time limit
is checked post-run against `cpu.stat` to set the TLE verdict (it does not
interrupt execution by itself).

---

## 8. Output capture notes

- Stdout and stderr of the sandboxed program are each capped at **4 MiB**.
  Data beyond the cap is silently discarded to prevent deadlock (the sandbox
  keeps reading so the child's pipe buffer never fills).
- When the cap is hit, `stdout_data.size() == 4194304` and the verdict is
  `OLE` (if judging is active).
- In human mode (`print_human`), the raw captured stdout is written to the
  CLI's stdout and raw captured stderr is written to the CLI's stderr — the
  sandbox is transparent to the terminal.
- In `--json` mode, stdout and stderr are JSON-escaped strings. Control
  characters below `0x20` are encoded as `\uXXXX`.

---

## 9. Sandbox internals (for problemsetters)

The sandbox uses **Linux user namespaces** (`CLONE_NEWUSER | CLONE_NEWNS`) so
it works without systemd or root (subject to kernel user-namespace permissions).
Each run:

1. Creates a unique cgroup leaf under `/sys/fs/cgroup/cxxprobe/<uuid>/`
2. Clones a child process into a new user + mount namespace
3. Writes uid/gid maps so the child sees itself as root (uid 0) inside its namespace
4. Moves the child into the cgroup (enforces memory/cpu/pids limits)
5. Signals the child to exec the target program
6. Waits via `epoll(pidfd, timerfd)` — no busy-polling
7. On timeout: `cgroup.kill`, then `SIGKILL` belt-and-suspenders
8. Reads `memory.peak` and `cpu.stat` from the cgroup after exit
9. Removes the cgroup directory (RAII destructor)

The child remounts `/proc` inside its namespace so it cannot inspect the host
process tree.
