# project.yaml v1 contract

This is the schema-first deliverable for [#3](https://github.com/Algorithms-Mathematics-Society/cxxprobe/issues/3)
and the Wave 1 handoff in [#13](https://github.com/Algorithms-Mathematics-Society/cxxprobe/issues/13).
[project.schema.json](project.schema.json) is JSON Schema draft 2020-12 over the
YAML document's JSON-compatible value model. MUST requirements below are normative.
This PR adds no runtime project support. Existing `problem.yaml` v2, plain problem
slugs, discovery and CLI behavior are unchanged. Do not close #3 with this PR.

## Shape and defaults

```yaml
version: 1
name: The Order Desk
shared:
  dir: shared
  sources: [driver.cpp]
parts:
  - {dir: parts/1-ingest, label: "1"}
  - {dir: parts/2-book, label: "2"}
integration:
  dir: integration
  entry: integration_gtest.cpp
  fallback_dir: fallback
```

`version`, `name`, and a nonempty `parts` list are required. `version` MUST be the
integer 1; `name` MUST be a string containing a non-whitespace character.
Each part MUST have `dir` and `label`. Unknown fields and explicit nulls are
invalid throughout. YAML mappings MUST have unique string keys. Numeric labels
MUST be quoted: `label: "1"`, never `label: 1`. No string/number coercion is part
of this contract.

`shared` and `integration` are optional mappings. Omitted sections or members mean:

| Member | Default | Resolution base |
| --- | --- | --- |
| `shared.dir` | `shared` | project directory |
| `shared.sources` | `[]` | each item beneath resolved `shared.dir` |
| `parts[].dir` | required | project directory |
| `integration.dir` | `integration` | project directory |
| `integration.entry` | `integration_gtest.cpp` | resolved `integration.dir` |
| `integration.fallback_dir` | absent; no fallback directory configured | resolved `integration.dir` |

JSON Schema defaults are annotations, not mutations: a future loader MUST apply
the defaults. An empty sources list permits header-only shared content and does
not require `shared.dir` to exist. An absent integration entry MUST remain valid;
the future integration judge reports SKIPPED, following the presence-based checker
convention in `src/problem/problem.cpp` and issue #5. Setting `fallback_dir` names
a directory; fallback file naming and substitution behavior belong to #5/#6,
not this manifest schema.

## Paths and identities

All path fields MUST be nonempty normalized relative POSIX paths. Absolute paths,
Windows drive/UNC paths, backslashes, colons, control characters, empty components,
trailing slashes, and `.` or `..` components are invalid. No shell expansion,
globbing, tilde expansion, or URI interpretation occurs. Nested paths are allowed.
`shared.sources` MUST contain no duplicate paths. Paths and identifiers are
case-sensitive, with no case folding or slugification.

The project directory is the directory containing `project.yaml`. Every resolved
path MUST stay inside that project after symlink resolution. Additionally,
`shared.sources[]` MUST stay beneath resolved `shared.dir`, and
`integration.entry`/`integration.fallback_dir` MUST stay beneath resolved
`integration.dir`. For a missing optional path, existing ancestors MUST still
resolve within its base. Lexical schema checks cannot prove this containment.

For the example above, `driver.cpp` means `order-desk/shared/driver.cpp`;
`fallback` means `order-desk/integration/fallback`, **not** `order-desk/fallback`.
This follows the existing checker `dir`/`entry` idiom. The nested example in
[tests/schema/examples](../tests/schema/examples) exercises each resolution base.

A label MUST match `^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$` over the **entire** string:
1–64 ASCII characters, alphanumeric first, then alphanumeric/underscore/hyphen.
This is worker PR #17's actual `jobs.py` `PART_LABEL` expression. No trailing
newline is permitted (Python `$` with `match` alone may accept one). Labels MUST
be unique within a project. Labels `one` and `ONE` are distinct.

Discovery MUST use the following identity rules, preserving existing problem
basename semantics:

- Project slug: project directory basename, e.g. `order-desk`, independent of `name`.
- Part local slug: basename of the declared `parts[].dir`, e.g. `1-ingest`.
- Qualified part slug: `<project-slug>/<part-local-slug>`, e.g. `order-desk/1-ingest`.
- Worker submission key: the exact label, e.g. `"1"`; it is neither slug nor path.

Part basenames MUST be unique within a project, even for different paths and
labels: `a/foo` and `b/foo` conflict. Duplicate directories therefore also conflict.
Different projects may reuse both labels and local slugs. Project discovery MUST
visit only declared parts (one project-to-part level), not recursively discover
arbitrary descendants or nested projects. A part is an ordinary problem directory
with its existing `problem.yaml`; its schema is unchanged. A directory MUST NOT
be both a problem and a project. Plain contest problems keep their current
unqualified slug. Future discovery MUST reject any identity collision rather than
silently overwriting an entry.

## Worker handoff

[Worker #7](https://github.com/Algorithms-Mathematics-Society/cxxprobe-worker/issues/7)
and [draft PR #17](https://github.com/Algorithms-Mathematics-Society/cxxprobe-worker/pull/17)
use `project_slug: "order-desk"` and `parts: {"1": "<submission URI>", ...}`.
The manifest's label set is authoritative. Missing labels MUST be diagnosed by
label; an explicit fallback can cover a missing submission under the future
#6 CLI contract. Reject unknown submission/fallback labels. Do not derive labels
from directory names or display names, or implicitly substitute a reference for
a missing worker submission. Existing problem jobs remain unchanged.

The package metadata seam PR #17 already reads is `manifest.json`:

```json
{"projects": [{"slug": "order-desk", "parts": [{"label": "1"}, {"label": "2"}]}]}
```

This is the minimum identity projection expected of future project packaging,
not a complete package-manifest schema. `projects[].slug` MUST equal the project
slug above and `parts[].label` MUST preserve each manifest label as a string.
Additional packaging metadata, CLI argv, result formats, and substitution policy
are outside this PR. The parent coordinates the worker contract handoff; this
artifact does not claim that packaging or worker execution already supports it.

## Executable proof and remaining #3 work

Run from the repository root:

```sh
python -m pip install -r tests/schema/requirements.txt
python -m unittest discover -s tests/schema -v
```

The validator library and PyYAML are test-only tools; there are no new engine
libraries or runtime dependencies. CI runs this suite independently of the C++
build. Examples are manifest-value fixtures, not runnable project packages.

JSON Schema checks types, required/unknown members, label/path syntax, nonempty
parts and duplicate source strings. The test-only `project_contract.py` adds
label and basename uniqueness, which standard JSON Schema cannot express by
property projection. Tests deliberately show that schema validation alone accepts
those duplicate-label/basename documents and that semantic checks reject them.
The test helper is a reference oracle, not a public loader or package validator.
Filesystem/discovery behavior is specified here and is **not** tested as working.

Remaining work before #3 can close:

- Implement `ProjectConfig`, `PartEntry`, `SharedConfig`, `IntegrationConfig` and
  the YAML loader using the existing yaml-cpp idioms; reject duplicate keys and
  apply defaults. Every malformed field MUST name its field/index in diagnostics.
- Enforce label/basename uniqueness, directory existence, and a `problem.yaml`
  in every part. Require `shared.dir` when sources are nonempty. Check path
  containment, including symlinks, and extend `warn_if_absolute_reference` to
  project paths (absolute project references are invalid, not merely warnings).
- Implement declared-part discovery and qualified identity/collision handling.
  Preserve byte-identical plain-problem behavior and test mixed contests plus
  two projects reusing a label/local slug.
- Wire project structural validation into `cxxprobe package validate`; test all
  malformed fields and missing directories against real filesystem fixtures.

Shared source injection (#4), integration judging (#5/#6), project packaging
(#7), and benchmarking are separate followups. No closing keyword for #3 belongs
in this schema PR.
