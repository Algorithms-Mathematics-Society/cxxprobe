"""Test-only reference checks for the manifest value contract, not an engine loader.

No defaults, file reads, discovery, symlink checks, or package validation here.
JSON Schema cannot express uniqueness by a member or by a path basename.
"""

from pathlib import PurePosixPath

from jsonschema import Draft202012Validator


def contract_errors(document, schema):
    """Return (field, message) pairs; semantic checks require a valid structure."""
    errors = []
    for error in Draft202012Validator(schema).iter_errors(document):
        field = ".".join(map(str, error.absolute_path)) or "project.yaml"
        errors.append((field, error.message))
    if errors:
        return errors

    labels = set()
    basenames = set()
    for index, part in enumerate(document["parts"]):
        label = part["label"]
        basename = PurePosixPath(part["dir"]).name
        if label in labels:
            errors.append((f"parts.{index}.label", f"duplicate label {label!r}"))
        if basename in basenames:
            errors.append((f"parts.{index}.dir", f"duplicate part basename {basename!r}"))
        labels.add(label)
        basenames.add(basename)
    return errors
