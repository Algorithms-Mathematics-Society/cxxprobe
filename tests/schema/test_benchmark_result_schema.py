"""Draft value-contract proof; no runtime or scoring implementation."""

import copy
import json
from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

HERE = Path(__file__).resolve().parent
SCHEMA = json.loads((HERE.parents[1] / 'schemas/benchmark-result.schema.json').read_text())
VALIDATOR = Draft202012Validator(SCHEMA)
PROVENANCE = Draft202012Validator({
    '$schema': SCHEMA['$schema'], '$defs': SCHEMA['$defs'],
    '$ref': '#/$defs/provenance',
})
OK = json.loads((HERE / 'examples/ok.benchmark-result.json').read_text())
METADATA = dict(machine_id='bench-host-1', calibration_ok=True,
                input_hash='opaque-input-digest', revision_hash='opaque-revision')


class BenchmarkResultSchemaTest(unittest.TestCase):
    def test_schema_and_examples(self):
        Draft202012Validator.check_schema(SCHEMA)
        examples = list((HERE / 'examples').glob('*.benchmark-result.json'))
        self.assertTrue(examples)
        statuses = set()
        for path in examples:
            with self.subTest(example=path.name):
                value = json.loads(path.read_text())
                VALIDATOR.validate(value)
                statuses.add(value['status'])
        self.assertEqual(statuses, {'OK', 'NOISY', 'FAILED_VERIFY'})

    def test_required_and_unknown_fields(self):
        for key in OK:
            value = copy.deepcopy(OK)
            del value[key]
            with self.subTest(missing=key):
                self.assertFalse(VALIDATOR.is_valid(value))
        self.assertFalse(VALIDATOR.is_valid(dict(OK, typo=1)))
        self.assertFalse(VALIDATOR.is_valid(dict(OK, **METADATA)))
        self.assertFalse(VALIDATOR.is_valid({'result': OK}))

    def test_bad_values_and_no_coercion(self):
        cases = {
            'status': ['ok', 'FAILED', None, 0],
            'median_ns': [None, -1, True, '1200'],
            'iqr_ns': [None, -1, False, '40'],
            'instructions': [-1, 1.5, True, '800'],
            'repetitions': [0, 1, -1, 2.5, True, None, '7'],
            'pinned_core': [-1, 0.5, False, '2'],
        }
        for key, values in cases.items():
            for candidate in values:
                with self.subTest(field=key, value=candidate):
                    self.assertFalse(VALIDATOR.is_valid(dict(OK, **{key: candidate})))
        for value in [None, [], 7, 'OK']:
            self.assertFalse(VALIDATOR.is_valid(value))

    def test_default_is_annotation_not_observation(self):
        self.assertEqual(SCHEMA['$defs']['engineResult']['properties']
                         ['repetitions']['default'], 7)
        for count in [2, 3, 7, 8, 20]:
            self.assertTrue(VALIDATOR.is_valid(dict(OK, repetitions=count)))
        value = dict(OK)
        del value['repetitions']
        self.assertFalse(VALIDATOR.is_valid(value))
        self.assertNotIn('repetitions', value)

    def test_failed_verification_has_no_measurement(self):
        failed = json.loads((HERE / 'examples/failed-verify.benchmark-result.json').read_text())
        for key, candidate in [('median_ns', 0), ('iqr_ns', 0),
                               ('instructions', 0), ('repetitions', 7)]:
            with self.subTest(field=key):
                self.assertFalse(VALIDATOR.is_valid(dict(failed, **{key: candidate})))
        self.assertTrue(VALIDATOR.is_valid(dict(failed, pinned_core=0)))

    def test_proposed_unavailable_values_and_zero_are_distinct(self):
        self.assertTrue(VALIDATOR.is_valid(dict(OK, median_ns=0, iqr_ns=0,
                                               instructions=0, pinned_core=0)))
        self.assertTrue(VALIDATOR.is_valid(dict(OK, instructions=None, pinned_core=None)))
        self.assertTrue(VALIDATOR.is_valid(dict(OK, median_ns=0.5, iqr_ns=0.25)))

    def test_strict_provenance_projection(self):
        PROVENANCE.validate(METADATA)
        for key in METADATA:
            value = dict(METADATA)
            del value[key]
            with self.subTest(missing=key):
                self.assertFalse(PROVENANCE.is_valid(value))
            for candidate in ([None, 0, 'true'] if key == 'calibration_ok'
                              else [None, 1, '', ' ', 'x\n', 'a b', 'x\x00', 'x\x7f']):
                with self.subTest(field=key, value=candidate):
                    self.assertFalse(PROVENANCE.is_valid(dict(METADATA, **{key: candidate})))
        self.assertFalse(PROVENANCE.is_valid(dict(METADATA, typo=True)))
        self.assertFalse(PROVENANCE.is_valid(dict(METADATA, result=OK)))

    def test_validity_does_not_establish_eligibility_or_comparability(self):
        # These schema-valid values still fail the documented necessary gates.
        noisy = dict(OK, status='NOISY')
        VALIDATOR.validate(noisy)
        self.assertNotEqual(noisy['status'], 'OK')
        uncalibrated = dict(METADATA, calibration_ok=False)
        PROVENANCE.validate(uncalibrated)
        self.assertFalse(uncalibrated['calibration_ok'])
        other_input = dict(METADATA, input_hash='another-input')
        PROVENANCE.validate(other_input)
        self.assertNotEqual(METADATA['input_hash'], other_input['input_hash'])
        # Inner validation alone supplies neither calibration nor input binding.
        VALIDATOR.validate(OK)
        self.assertNotIn('calibration_ok', OK)
        self.assertNotIn('input_hash', OK)


if __name__ == '__main__':
    unittest.main()
