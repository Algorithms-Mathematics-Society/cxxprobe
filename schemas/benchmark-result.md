# Benchmark result contract (issue #10)

The inner record `cxxprobe bench project --json` emits under `result`, and the
provenance projection the worker wraps it with. The engine is
`src/bench/bench.cpp`; the decisions it makes are recorded under
[Decisions](#decisions) below, replacing the open questions of the first draft.
[Ownership comments on #10](https://github.com/Algorithms-Mathematics-Society/cxxprobe/issues/10#issuecomment-6612509872)
and [#12](https://github.com/Algorithms-Mathematics-Society/cxxprobe/issues/12)
establish the following seam; the proposal choices below are not agreed policy.

## Established seam

Shubh owns `status` (`OK`, `NOISY`, `FAILED_VERIFY`), `median_ns`, `iqr_ns`,
`instructions`, `repetitions`, and `pinned_core`. Rasesh owns `machine_id`,
`calibration_ok`, `input_hash`, and `revision_hash`, plus worker envelope and
sandbox affinity APIs. Preserve these names and statuses.

The root of [benchmark-result.schema.json](benchmark-result.schema.json) validates
only the engine inner result. `$defs.provenance` validates a standalone projection
of the four provenance fields for review. It defines neither nesting nor a flat
worker envelope. Do not submit a combined worker record to the root validator.
The worker owner defines composition, transport, extra envelope metadata and
failure reporting. No public CLI flags or sandbox calls are defined here.

The issue specifies repeated wall-time measurements, median, IQR, instruction
count and a default of seven repetitions. Seven is not a fixed allowed count.
Timing units are nanoseconds; instructions are counts, not rates. **Proposed
interpretation:** repetitions count completed measured runs represented by this
result, excluding warmups and rejected batches; `pinned_core` identifies the
measured host's logical CPU ID;
physical-core selection and sibling isolation remain the affinity owner's work.
An ID alone cannot prove affinity or sibling isolation.

## Proposed value shape (joint review required)

All six inner fields are required; unknown members and type coercions are
rejected. JSON numbers must be finite; duplicate object keys must be rejected
before schema validation. JSON Schema cannot detect duplicates after parsing.
`default: 7` is an annotation for producer configuration, never permission to
omit or fill in an observed result's repetition count.

| Status | Proposed timing / repetitions | Proposed instructions / core |
| --- | --- | --- |
| `OK` | nonnegative numeric median and IQR, at least 2 completed runs | nonnegative integer or null for each |
| `NOISY` | same shape, diagnostic values only | same shape |
| `FAILED_VERIFY` | both timings null, repetitions 0 | instructions null; core integer or null |

Fractional nanoseconds permit aggregate statistics without rounding policy.
`null` means unavailable, never zero. Zero is a measured value in this proposed structural domain. **Consumer proposal
for joint review:** a zero median is diagnostic only and cannot rank; ranking
requires `median_ns > 0`, since the API #57 ratio divides best time by median.
This is an eligibility proposal, not newly agreed policy. Failed verification
has no benchmark measurement; a core may still identify a previously selected CPU.
A `NOISY` record retains its diagnostic aggregate but must never feed ranking.
Allowing `OK` with unavailable counters or an unpinned local run is a **draft
shape choice**, not permission to rank such a run. Minimum 2, numeric domains,
nullability and failed-record layout also require joint approval.

Provenance projection proposal: all four fields are required, unknown members
and nulls rejected, `calibration_ok` a boolean, other fields nonempty strings
without whitespace or control characters. Hashes remain opaque and case-sensitive;
no algorithm, digest length, Git hash format or machine naming scheme is agreed.
`calibration_ok: false` is structurally valid. It does not imply `FAILED_VERIFY`.

## Consumer semantics, separate from schema validity

Consumers must validate the appropriate inner/provenance shapes and refuse
ranking unless status is `OK` and the record is valid and calibrated. Comparisons
must use the same exact `input_hash`; missing provenance cannot pass eligibility.
Do not treat schema validity, `OK` alone, or a populated core as proof of trust.
Consumers must also enforce the agreed worker verification and execution policy:
verify the exact artifact on the benchmark input, then time that artifact without
a rebuild. `revision_hash` alone does not prove artifact identity. Verification,
calibration freshness, affinity, sample completeness and hash binding require
producer/worker evidence and cannot be proven by this schema.

Unavailable-counter or unpinned results must not be silently promoted to trusted
fleet measurements. The worker owner must define their eligibility policy during
joint review. Calibration failure is a provenance/eligibility failure; do not
invent a new status or conflate it with failed verification. Scoring implementation
and cross-record comparison are outside this draft.

## Decisions

These were the open questions of the draft. `cxxprobe bench project` implements
them; changing one is a contract change.

- **Timed region.** Wall time from `execve` to child exit, measured by the
  parent on `CLOCK_MONOTONIC` (`steady_clock`) at nanosecond resolution.
  Sandbox setup, uid maps and output draining are outside it.
- **Warmups and failed runs.** Discarded warmup runs until at least
  `--warmups` (1) runs and `--warmup-ms` (1000 ms) have passed: on c7i.xlarge
  the first timed second after building the parts was consistently 8-11%
  noisier than the rest, which one run does not absorb. Any timed run that exits non-zero, times out, or prints
  output that does not token-match the reference is `FAILED_VERIFY`; it is
  never dropped and replaced.
- **Verification.** The candidate is linked once. Its output on the benchmark
  input must token-match the reference parts' output on the same input before
  anything is timed, and that same binary is then timed (`binary_sha256` is
  its hash). A part that does not compile, or parts that do not link, are
  `FAILED_VERIFY` too.
- **Quantiles.** Linear interpolation between order statistics (Hyndman-Fan
  type 7, numpy's default): median = q(0.5), IQR = q(0.75) - q(0.25). For the
  default 7 runs the median is the 4th order statistic.
- **Noise gate and retries.** A batch is accepted when `iqr_ns <= threshold x
  median_ns` (equality accepts), threshold 0.05 by default. A rejected batch
  is re-run, up to 3 batches (`--attempts`). The record reports the accepted
  batch, or the last batch with status `NOISY` when none was accepted;
  `attempts[]` carries every batch's samples as diagnostics.
- **Host check.** With `--host-baseline-ns` (the worker passes its
  machine's calibration baseline), the calibration workload is timed on the
  same CPU just before and just after the batches. If either is more than
  `--host-tolerance` (5%) from the baseline, an otherwise `OK` result is
  `NOISY` with `noise_reason`. This is the case the IQR gate cannot see: a
  busy hyperthread sibling slowed a benchmark by 28% on c7i.xlarge while its
  IQR stayed under 5%. Instruction counts did not move, which is how the two
  are told apart.
- **Instructions.** User-space retired instructions (`exclude_kernel`) of the
  measured process and everything it starts (`inherit`), from exec to exit;
  `instructions` is the median of the accepted batch's per-run counts. `null`
  when the host exposes no hardware counter (most VMs without a vPMU);
  `null` never means zero.
- **Pinning.** `pinned_core` is the logical CPU the measured process was
  bound to with `sched_setaffinity`. Chosen automatically as the first logical
  CPU of the highest-numbered physical core that does not hold CPU 0 (`--cpu`
  overrides). Its hyperthread siblings are reported in `cpu.siblings`; keeping
  them idle is the host's job (the benchmark pool isolates them), and pinning
  alone does not prove it.
- **Unpinned or uncounted local runs.** An `OK` record with
  `instructions: null` is a valid measurement of its host but carries no
  provenance; only the worker, with a calibrated machine and the hashes below,
  makes a record eligible to rank.
- **Worker envelope.** The worker publishes the inner record unchanged with
  `machine_id`, `calibration_ok`, `input_hash` (the SHA-256 of the input it
  staged; the engine reports the same value as `input_sha256`) and
  `revision_hash` (the job's revision) beside it. See the cxxprobe-worker
  README for the full envelope.

## Executable proof

From the repository root:

```sh
python -m pip install -r tests/schema/requirements.txt
python -m unittest discover -s tests/schema -v
```

Existing test-only jsonschema/PyYAML requirements and CI discovery are reused;
there are no new runtime dependencies. The benchmark examples include diagnostic
and unavailable-value records, not just rankable results. Tests exercise negative
mutations, strict provenance metadata and consumer-gating distinctions. They are
contract examples, not an implementation of worker trust or scoring.
