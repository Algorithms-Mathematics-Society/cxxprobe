"""Run: python -m unittest discover -s tests/schema -v (see requirements.txt)."""

import copy
import json
from pathlib import Path, PurePosixPath
import re
import unittest

from jsonschema import Draft202012Validator
import yaml

from project_contract import contract_errors

HERE = Path(__file__).resolve().parent
SCHEMA = json.loads((HERE.parents[1] / "schemas/project.schema.json").read_text())
FULL = yaml.safe_load((HERE / "examples/order-desk.project.yaml").read_text())


class ProjectSchemaTest(unittest.TestCase):
    def test_schema_is_valid_and_all_examples_satisfy_value_contract(self):
        Draft202012Validator.check_schema(SCHEMA)
        examples = sorted((HERE / "examples").glob("*.project.yaml"))
        self.assertEqual(len(examples), 3)
        for example in examples:
            with self.subTest(example=example.name):
                self.assertEqual(contract_errors(yaml.safe_load(example.read_text()), SCHEMA), [])

    def test_required_fields_at_each_level(self):
        for path in [(key,) for key in ("version", "name", "parts")] + [
            ("parts", 0, "dir"), ("parts", 0, "label")
        ]:
            document = copy.deepcopy(FULL)
            parent = document
            for key in path[:-1]:
                parent = parent[key]
            del parent[path[-1]]
            with self.subTest(path=path):
                errors = contract_errors(document, SCHEMA)
                self.assertTrue(errors)
                self.assertIn(path[-1], str(errors))

    def test_malformed_fields_name_the_field(self):
        cases = [
            (("version",), value) for value in (0, 2, "1", True, None)
        ] + [
            (("name",), value) for value in ("", " \n\t", 42, None)
        ] + [
            (("parts",), value) for value in ([], {}, "parts", None)
        ] + [
            (("shared",), []), (("integration",), None),
            (("shared", "sources"), "driver.cpp"),
            (("shared", "sources"), ["driver.cpp", "driver.cpp"]),
            (("shared", "sources", 0), 1),
            (("parts", 0), "part"), (("parts", 0, "dir"), 1),
            (("parts", 0, "label"), 1),
        ]
        for path, value in cases:
            document = copy.deepcopy(FULL)
            parent = document
            for key in path[:-1]:
                parent = parent[key]
            parent[path[-1]] = value
            with self.subTest(path=path, value=value):
                errors = contract_errors(document, SCHEMA)
                self.assertTrue(errors)
                self.assertIn(".".join(map(str, path)), [field for field, _ in errors])

    def test_unknown_fields_are_rejected_at_every_mapping(self):
        for path in [(), ("shared",), ("integration",), ("parts", 0)]:
            document = copy.deepcopy(FULL)
            parent = document
            for key in path:
                parent = parent[key]
            parent["typo"] = True
            with self.subTest(path=path):
                self.assertIn("typo", str(contract_errors(document, SCHEMA)))

    def test_labels_match_worker_pr17_part_label(self):
        # Actual PR #17 jobs.py PART_LABEL; use fullmatch to reject final newlines.
        worker_label = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$")
        labels = ["1", "Book-2", "Core_1", "a" * 64, "", "a" * 65,
                  "_a", "-a", ".", "..", "a/b", "a\\b", "a=b", "a b",
                  "é", "a\n", "a\x00", "a:b"]
        for label in labels:
            document = copy.deepcopy(FULL)
            document["parts"][0]["label"] = label
            with self.subTest(label=label):
                self.assertEqual(not contract_errors(document, SCHEMA),
                                 worker_label.fullmatch(label) is not None)

    def test_all_path_fields_reject_non_normalized_or_escaping_paths(self):
        paths = [("shared", "dir"), ("shared", "sources", 0),
                 ("parts", 0, "dir"), ("integration", "dir"),
                 ("integration", "entry"), ("integration", "fallback_dir")]
        for path in paths:
            for value in ["", ".", "..", "../x", "x/..", "x/../y", "x/./y",
                          "/tmp/x", "//server/x", "C:/x", "C:x", "x\\y",
                          "x//y", "x/", "x\n", "x\x00", "x\x7f"]:
                document = copy.deepcopy(FULL)
                parent = document
                for key in path[:-1]:
                    parent = parent[key]
                parent[path[-1]] = value
                with self.subTest(path=path, value=value):
                    self.assertTrue(contract_errors(document, SCHEMA))

    def test_optional_sections_and_members_have_documented_defaults(self):
        document = {"version": 1, "name": "Minimal", "parts": FULL["parts"]}
        self.assertEqual(contract_errors(document, SCHEMA), [])
        document.update(shared={}, integration={})
        self.assertEqual(contract_errors(document, SCHEMA), [])
        # Schema validation does not insert defaults or require integration files.
        self.assertEqual(document["shared"], {})
        self.assertNotIn("fallback_dir", document["integration"])
        for section, key, expected in [
            ("shared", "dir", "shared"), ("shared", "sources", []),
            ("integration", "dir", "integration"),
            ("integration", "entry", "integration_gtest.cpp")
        ]:
            self.assertEqual(SCHEMA["properties"][section]["properties"][key]["default"], expected)

    def test_semantic_uniqueness_is_not_claimed_by_json_schema(self):
        for second, field in [
            ({"dir": "parts/other", "label": "1"}, "label"),
            ({"dir": "elsewhere/1-ingest", "label": "other"}, "dir"),
            (FULL["parts"][0], "label"),
        ]:
            document = copy.deepcopy(FULL)
            document["parts"].append(second)
            with self.subTest(second=second):
                self.assertTrue(Draft202012Validator(SCHEMA).is_valid(document))
                self.assertIn(f"parts.2.{field}", [p for p, _ in contract_errors(document, SCHEMA)])
        document = copy.deepcopy(FULL)
        document["parts"][1]["label"] = "ONE"
        document["parts"][0]["label"] = "one"
        self.assertEqual(contract_errors(document, SCHEMA), [])

    def test_worker_identity_and_path_base_examples(self):
        # Contract examples, not an implementation of discovery or packaging.
        for project_slug in ["order-desk", "other-desk"]:
            parts = {p["label"]: f'{project_slug}/{PurePosixPath(p["dir"]).name}'
                     for p in FULL["parts"]}
            self.assertEqual(parts["1"], f"{project_slug}/1-ingest")
            self.assertEqual(set(parts), {"1", "2"})
        root = PurePosixPath("round-2/order-desk")
        shared = root / FULL["shared"]["dir"]
        integration = root / FULL["integration"]["dir"]
        self.assertEqual(str(shared / FULL["shared"]["sources"][0]),
                         "round-2/order-desk/shared/driver.cpp")
        self.assertEqual(str(integration / FULL["integration"]["fallback_dir"]),
                         "round-2/order-desk/integration/fallback")
        self.assertEqual(str(integration / FULL["integration"]["entry"]),
                         "round-2/order-desk/integration/integration_gtest.cpp")


if __name__ == "__main__":
    unittest.main()
