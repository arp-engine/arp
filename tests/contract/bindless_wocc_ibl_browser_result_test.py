"""Hostile checks for the independent browser IBL evidence validator."""
import copy
import struct
import sys
import unittest

from bindless_webgpu_rhi_smoke import ROOT, validate_wocc_ibl_report
import bindless_finite_lifecycle_evidence_test as finite_evidence_tests

sys.path.insert(0, str(ROOT / "tests/tools"))
from bindless_graphics_readback_oracles import OrdinaryWoccIblOracle


class WoccIblBrowserResultTests(unittest.TestCase):
    def setUp(self):
        self.inputs = ROOT / "tests/fixtures/wocc_ibl_inputs"
        oracle = OrdinaryWoccIblOracle(self.inputs)
        words = list(struct.unpack("<424I", struct.pack("<424f", *oracle.expected)))
        details = " execution13 Content9 read18 control1 lifecycle12 same-root-journal genuine-draw-queue-map hot0 publication-gated budgets-returned"
        self.report = dict(success=True, scenario="original-bindless-wocc-ibl",
            publicBindlessExecutionAvailable=False, productionReady=False, directTraditionalParity=False,
            diagnostics=[], nativeErrors=[], executionPassed=True, referencePassed=True,
            numericPassed=True, bitwiseParityPassed=True, retirementPassed=True,
            progress=[f"BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_WOCC_IBL_EXECUTION_OK scenario{s} fault{f}" + details
                      for s in range(11) for f in (0, 1)] + [
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_WOCC_IBL_REFERENCE_EXECUTION_OK scenario0 fault0" + details,
                "BINDLESS_WEBGPU_WOCC_IBL_RETIRE_OK frozen655328 original-owners-expired budgets-zero scopes3-drained"],
            readbacks=[dict(reference=ref, fault=fault, words=words.copy())
                       for ref, fault in ((True, False), (False, False), (False, True),
                                          (False, False), (False, True))])
        self.report["progress"] += [
            f"BINDLESS_WEBGPU_{role}_NUMERIC_OK fault{fault} lanes424 unfiltered80-exact reference-bitwise color2x2"
            for role, fault in (("WOCC_IBL_REFERENCE", 0), ("WOCC_IBL", 0), ("WOCC_IBL", 1),
                                ("WOCC_IBL", 0), ("WOCC_IBL", 1))]
        root_cpu = 65536 + (self.inputs / "horizon.rgba16f").stat().st_size
        self.report["progress"] += [(f"BINDLESS_WEBGPU_ROOT_CPU_DOMAINS_OK graph{root_cpu} recording{root_cpu} "
            f"submission{root_cpu} callback65536 mismatch-refused no-publication")] * 23
        self.report["progress"] = [dict(message=message) for message in self.report["progress"]]
        finite_evidence_tests.FiniteNativeEvidenceTest.transport(self.report)

    def refuses(self, report):
        finite_evidence_tests.FiniteNativeEvidenceTest.transport(report)
        with self.assertRaises((ValueError, RuntimeError)):
            validate_wocc_ibl_report(report, self.inputs)

    def test_exact_evidence_and_cpu_anchors(self):
        result = validate_wocc_ibl_report(self.report, self.inputs)
        self.assertEqual(result["anchors"]["cpuExactLanes"], 80)
        self.assertEqual(result["lifecycleFlows"], 22)
        self.assertTrue(result["ownReferenceBitwise"])
        self.assertFalse(result["directTraditionalParity"])

    def test_every_missing_or_duplicate_lifecycle(self):
        for i in range(len(self.report["progress"])):
            for duplicate in (False, True):
                report = copy.deepcopy(self.report)
                marker = report["progress"][i]
                if duplicate:
                    report["progress"].append(marker)
                else:
                    report["progress"].pop(i)
                self.refuses(report)

    def test_each_numeric_lane_must_match_independent_reference(self):
        for lane in range(424):
            report = copy.deepcopy(self.report)
            report["readbacks"][1]["words"][lane] ^= 1
            self.refuses(report)

    def test_consistent_corruption_still_fails_cpu_anchors(self):
        report = copy.deepcopy(self.report)
        for read in report["readbacks"]:
            read["words"][6] ^= 1
        self.refuses(report)

    def test_missing_role_footprint_nonfinite_and_untyped_word(self):
        for mutation in (lambda r: r["readbacks"].pop(),
                         lambda r: r["readbacks"][1].update(fault=True),
                         lambda r: r["readbacks"][1]["words"].pop(),
                         lambda r: r["readbacks"][1]["words"].__setitem__(0, True),
                         lambda r: [read["words"].__setitem__(0, 0x7fc00000) for read in r["readbacks"]]):
            report = copy.deepcopy(self.report)
            mutation(report)
            self.refuses(report)

    def test_error_or_overclaim_refuses(self):
        for key in ("diagnostics", "nativeErrors", "productionReady", "directTraditionalParity",
                    "publicBindlessExecutionAvailable", "executionPassed", "referencePassed",
                    "numericPassed", "bitwiseParityPassed", "retirementPassed", "success"):
            report = copy.deepcopy(self.report)
            report[key] = ["native failure"] if key in ("diagnostics", "nativeErrors") else not report[key]
            self.refuses(report)


if __name__ == "__main__":
    unittest.main()
