# DRAFT benchmark result contract — issue #10

**Review handoff only; pending joint review by Shubh and Rasesh.**
This schema-first draft adds no engine, counter, affinity, worker or scoring
implementation. It does not complete or close [#10](https://github.com/Algorithms-Mathematics-Society/cxxprobe/issues/10).
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

## Decisions still open

- Exact timed region and wall-clock source; warmup and failed-run policy.
- Median/IQR quantile algorithm, including even counts and small samples.
- Instruction aggregate and user/kernel/child scope; unavailable-counter policy.
- Unpinned local status/null semantics and trusted-fleet requirements.
- Noise threshold, equality at the boundary, and bounded retry budget; which
  attempt supplies diagnostics, and how exhaustion yields `NOISY`.
- Worker envelope layout, provenance formats and evidence for calibration and
  exact-artifact verification, owned by Rasesh.

No quantile algorithm, instruction aggregation, threshold or retry count is chosen
here. They are not needed to review the shape and must be agreed before a producer
or consumer relies on the numeric values. In particular, retries must eventually
be bounded, but this draft does not prescribe a budget.

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
