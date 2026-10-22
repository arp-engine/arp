"""Synthetic receipt checks only; these tests confer no platform evidence."""
import copy
import struct
import unittest

from bindless_webgpu_rhi_smoke import validate_scene_lifecycle_report, expected_scene_color
from browser_contract_runner import BrowserProgressJournal


def report(family):
    names = ("terrain",) if family == "terrain" else ("mesh", "skin", "grass", "canopy")
    messages = ["BINDLESS_SCENE_DEVICE_PLAN_OK sealed-package queried-adapter actual-device before-rhi no-capability-grant"]
    messages += [f"BINDLESS_WEBGPU_SCENE_PUBLIC_LIFECYCLE entry={name}{stage}Vertex "
                 f"outcome={outcome} results={2 if stage == 'Color' else 1}"
                 for name in names for stage in ("Color", "Depth", "Shadow")
                 for outcome in ("cancelled-unpublished", "completed")]
    representative = names[0]
    messages += [f"BINDLESS_WEBGPU_SCENE_FAILURE_LIFECYCLE entry={representative}{stage}Vertex "
                 f"scenario={scenario} outcome={'completed' if scenario == 'reverse' else 'unpublished'} "
                 f"{'caller-conformance ' if scenario == 'material-range-rejected' else ''}"
                 f"results={2 if stage == 'Color' else 1}"
                 for stage in ("Color", "Depth", "Shadow")
                 for scenario in ("cold-cancel", "recording-error", "submission-error", "reverse",
                                  "pending-cancel", "caller-drop", "throw-after", "throw-before", "retired-lifetime", "material-range-rejected")]
    values = []
    for x, y in ((-.5, -.5), (.5, -.5), (-.5, .5), (.5, .5)):
        pixel = [0.] * 23
        pixel[:3] = (x, y, .5)
        for clip in ((11, 15, 19) if family == "terrain" else (6, 10, 14)):
            pixel[clip:clip + 4] = (x, y, .5, 1.)
        values += pixel
    words = struct.unpack("<92I", struct.pack("<92f", *values))
    messages += [f"BINDLESS_SCENE_GEOMETRY_WORDS entry={name}ColorVertex words=" + ",".join(map(str, words)) for name in names]
    for name in names:
        for stage in ("Color", "Depth", "Shadow"):
            if stage == "Color":
                pixels = [lane for y in (-.5, -.5, .5, .5) for lane in expected_scene_color(family == "terrain", y)]
                packed = struct.pack("<16e", *pixels)
            else:
                packed = struct.pack("<4f", *([.5] * 4))
            messages += [f"BINDLESS_SCENE_ATTACHMENT_BYTES entry={name}{stage}Vertex "
                         f"fault=0 format={'rgba16f' if stage == 'Color' else 'd32'} bytes="
                         + ",".join(map(str, packed)) + " observer=completed-native-diagnostic"]
    messages += [f"BINDLESS_SCENE_FAULT_WORD entry={name}{stage}Vertex scenario=success word=0"
                 for name in names for stage in ("Color", "Depth", "Shadow")]
    for stage in ("Color", "Depth", "Shadow"):
        for scenario, fault in (("reverse", 0),):
            messages += [f"BINDLESS_SCENE_FAULT_WORD entry={representative}{stage}Vertex scenario={scenario} word={fault}"]
    messages += ["BINDLESS_WEBGPU_SCENE_RETIRE_OK original-owners-expired budgets-zero scopes3-drained no-visual-or-numeric-equivalence-grant"]
    journal = BrowserProgressJournal()
    for offset in range(0, len(messages), journal.limits["batchMessages"]):
        journal.accept(dict(contractVersion=1, first=offset,
                            messages=messages[offset:offset + journal.limits["batchMessages"]]), 0)
    return dict(success=True, scenario="original-bindless-scene-lifecycle", sceneFamily=family,
                executionPassed=True, retirementPassed=True, publicBindlessExecutionAvailable=False,
                productionReady=False, numericEquivalencePassed=False, attachmentNumericPassed=False,
                diagnostics=[], nativeErrors=[], progress=journal.observations,
                progressTransport=dict(contractVersion=1, complete=True, messages=len(messages), codeUnits=journal.code_units),
                plannedDevice=dict(createdBeforeRhi=True, requiredLimits={"maxBindGroups": 3},
                                   actualLimits={"maxBindGroups": 4}))


class SceneLifecycleReportTests(unittest.TestCase):
    def test_finite_families(self):
        for family in ("pbr", "terrain"):
            validate_scene_lifecycle_report(report(family), family)

    def test_summary_cannot_replace_receipts(self):
        original = report("pbr")
        for field in ("progress", "progressTransport", "plannedDevice"):
            value = copy.deepcopy(original)
            del value[field]
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_scene_lifecycle_report(value, "pbr")

    def test_missing_or_duplicate_entry(self):
        for duplicate in (False, True):
            value = report("terrain")
            if duplicate:
                value["progress"][1]["message"] = value["progress"][2]["message"]
            else:
                value["progress"][1]["message"] = "unrelated successful test"
            messages = [item["message"] for item in value["progress"]]
            value["progressTransport"]["codeUnits"] = sum(BrowserProgressJournal.code_units_of(message) for message in messages)
            with self.assertRaisesRegex(ValueError, "RECEIPTS"):
                validate_scene_lifecycle_report(value, "terrain")

    def test_scope_escalation_refused(self):
        for field in ("publicBindlessExecutionAvailable", "productionReady", "numericEquivalencePassed", "attachmentNumericPassed"):
            value = report("pbr")
            value[field] = True
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "SCOPE"):
                validate_scene_lifecycle_report(value, "pbr")

    def test_device_limits_refused(self):
        for limit in (2, True, 2.0):
            value = report("terrain")
            value["plannedDevice"]["actualLimits"]["maxBindGroups"] = limit
            with self.assertRaisesRegex(ValueError, "DEVICE-LIMIT"):
                validate_scene_lifecycle_report(value, "terrain")

    def test_scope_and_retirement_are_required(self):
        for field, replacement in (("sceneFamily", "pbr"), ("nativeErrors", ["map failed"]),
                                   ("retirementPassed", False), ("executionPassed", False)):
            value = report("terrain")
            value[field] = replacement
            with self.subTest(field=field), self.assertRaises(ValueError):
                validate_scene_lifecycle_report(value, "terrain")

    def test_numeric_readback_cannot_be_missing_or_zero(self):
        for missing in (True, False):
            value = report("terrain")
            receipt = next(item for item in value["progress"] if item["message"].startswith("BINDLESS_SCENE_GEOMETRY_WORDS "))
            receipt["message"] = ("unrelated" if missing else
                "BINDLESS_SCENE_GEOMETRY_WORDS entry=terrainColorVertex words=" + ",".join(["0"] * 92))
            value["progressTransport"]["codeUnits"] = sum(BrowserProgressJournal.code_units_of(item["message"]) for item in value["progress"])
            with self.assertRaisesRegex(ValueError, "GEOMETRY"):
                validate_scene_lifecycle_report(value, "terrain")

    def test_failures_must_drain_without_publication(self):
        for mutation in ("missing", "duplicate", "published", "reverse-failed"):
            value = report("pbr")
            failures = [item for item in value["progress"] if item["message"].startswith("BINDLESS_WEBGPU_SCENE_FAILURE_LIFECYCLE ")]
            if mutation == "missing":
                failures[0]["message"] = "unrelated"
            elif mutation == "duplicate":
                failures[0]["message"] = failures[1]["message"]
            elif mutation == "published":
                failures[0]["message"] = failures[0]["message"].replace("outcome=unpublished", "outcome=completed")
            else:
                receipt = next(item for item in failures if "scenario=reverse " in item["message"])
                receipt["message"] = receipt["message"].replace("outcome=completed", "outcome=unpublished")
            value["progressTransport"]["codeUnits"] = sum(BrowserProgressJournal.code_units_of(item["message"]) for item in value["progress"])
            with self.subTest(mutation=mutation), self.assertRaisesRegex(ValueError, "FAILURE-RECEIPTS"):
                validate_scene_lifecycle_report(value, "pbr")

    def test_attachment_values_cannot_be_missing_or_fabricated(self):
        for mutation in ("missing", "duplicate", "zero-color", "clear-depth", "wrong-format"):
            value = report("terrain")
            receipts = [item for item in value["progress"] if item["message"].startswith("BINDLESS_SCENE_ATTACHMENT_BYTES ")]
            if mutation == "missing":
                receipts[0]["message"] = "unrelated"
            elif mutation == "duplicate":
                receipts[0]["message"] = receipts[1]["message"]
            elif mutation == "wrong-format":
                receipts[0]["message"] = receipts[0]["message"].replace("rgba16f", "d32")
            else:
                target = receipts[0] if mutation == "zero-color" else receipts[1]
                packed = bytes(32) if mutation == "zero-color" else struct.pack("<4f", *([1.] * 4))
                prefix = target["message"].split("bytes=")[0]
                target["message"] = prefix + "bytes=" + ",".join(map(str, packed)) + " observer=completed-native-diagnostic"
            value["progressTransport"]["codeUnits"] = sum(BrowserProgressJournal.code_units_of(item["message"]) for item in value["progress"])
            with self.subTest(mutation=mutation), self.assertRaisesRegex(ValueError, "ATTACHMENT"):
                validate_scene_lifecycle_report(value, "terrain")


if __name__ == "__main__":
    unittest.main()
