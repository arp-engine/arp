"""Negative tests for evidence acceptance; synthetic reports grant no GPU evidence."""
import copy
import unittest

from bindless_finite_lifecycle_acceptance import expected_cases, inspect_report


class FiniteLifecycleEvidenceTest(unittest.TestCase):
    def claim(self):
        from bindless_finite_lifecycle_evidence_test import FiniteNativeEvidenceTest
        return FiniteNativeEvidenceTest().claim()

    def test_exact_matrix_and_separate_numerical_cases(self):
        self.assertEqual(len(expected_cases("claim")), 30)
        self.assertEqual(len(expected_cases("compute-default")), 192 + 6)
        self.assertEqual(len(expected_cases("compute-final")), 204 + 6)
        self.assertTrue(set(expected_cases("claim")) < set(expected_cases("compute-default")))
        self.assertEqual(set(expected_cases("compute-final")) - set(expected_cases("compute-default")),
                         {name for name in expected_cases("compute-final") if "post_suppression" in name})
        with self.assertRaises(ValueError):
            expected_cases("unknown")

    def test_native_errors_identity_execution_and_output_refuse(self):
        valid = self.claim()
        identity = ("claim_terminal", "uniform", "static", "cancel")
        inspect_report(valid, identity)
        for field, value in (("success", False), ("diagnostics", ["bad"]), ("nativeErrors", ["bad"]),
                             ("publicBindlessExecutionAvailable", True), ("nativeSyncAvailable", True),
                             ("finiteLifecycle4Validated", True), ("fullFiniteExecutionValidated", True),
                             ("originalGeneralFiniteLifecycleVersion", 3),
                             ("originalGeneralFiniteContentPlanBufferType", "readonly-storage"),
                             ("originalGeneralFiniteContentPlanRangeKind", "dynamic"),
                             ("originalGeneralFiniteClaimTerminalKind", "drop"),
                             ("scenario", "original-general-finite-execution"),
                             ("originalGeneralFiniteNumericalValues", [1]),
                             ("originalGeneralFiniteClaimExecutionAbsentPassed", False)):
            hostile = copy.deepcopy(valid); hostile[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                inspect_report(hostile, identity)

    def test_success_flags_without_native_progress_refuse(self):
        from bindless_finite_lifecycle_evidence_test import FiniteNativeEvidenceTest
        for index in range(len(self.claim()["progress"])):
            hostile = self.claim(); hostile["progress"].pop(index)
            FiniteNativeEvidenceTest.transport(hostile)
            with self.subTest(marker=index), self.assertRaises(ValueError):
                inspect_report(hostile, ("claim_terminal", "uniform", "static", "cancel"))


if __name__ == "__main__":
    unittest.main()
