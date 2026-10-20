"""Registered-case and JUnit rejection tests; no browser/GPU evidence is mocked."""
import copy
import json
import pathlib
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as xml

from bindless_finite_lifecycle_acceptance import expected_cases
from bindless_webgpu_post_suppression_attempt import (
    select_tests, inspect_junit, verify_filtered_selection, capture_case_outputs)
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES


class PostSuppressionAttemptTest(unittest.TestCase):
    def test_failed_and_partial_case_outputs_are_copied_without_acceptance(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).resolve(); build = root / "build"; build.mkdir()
            unused, selected = select_tests(self.inventory(build), build)
            name = next(iter(selected)); command = selected[name]["command"]
            source = pathlib.Path(command[command.index("--output") + 1]); source.mkdir(parents=True)
            report = json.dumps(dict(success=False, noGpuAuthority=True))
            (source / "report.json").write_text(report, encoding="utf-8")
            (source / "browser.log").write_text("preserved failure", encoding="utf-8")
            output = root / "capture"; output.mkdir()
            captured = capture_case_outputs(output, selected, {name: report + "\nnative rejection"})
            self.assertFalse(captured["complete"]); self.assertEqual(captured["cases"], 12)
            self.assertTrue(all(record["accepted"] is False for record in captured["captured"]))
            self.assertEqual((output / "cases" / name / "report.json").read_text(), report)
            self.assertEqual((output / "cases" / name / "junit-output.log").read_text(), report + "\nnative rejection")
            for gate in CLOSED_GATES: self.assertIs(captured[gate], False)
            with self.assertRaises(ValueError): capture_case_outputs(output, selected, {})

    def test_case_output_change_during_copy_refuses(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory).resolve(); build = root / "build"; build.mkdir()
            unused, selected = select_tests(self.inventory(build), build)
            name = next(iter(selected)); command = selected[name]["command"]
            source = pathlib.Path(command[command.index("--output") + 1]); source.mkdir(parents=True)
            (source / "report.json").write_text("{}", encoding="utf-8")
            output = root / "capture"; output.mkdir()
            def changed_copy(source, destination):
                pathlib.Path(destination).write_bytes(pathlib.Path(source).read_bytes())
                pathlib.Path(source).write_bytes(b"changed")
            with mock.patch("shutil.copyfile", side_effect=changed_copy), self.assertRaisesRegex(ValueError, "CAPTURE-CHANGED"):
                capture_case_outputs(output, selected, {})

    @staticmethod
    def inventory(build):
        tests = []
        for name, identity in expected_cases("compute-final").items():
            if identity[0] != "post_suppression": continue
            unused, buffer_type, interval, terminal = identity
            command = ["unit-python", "unit-driver", "--general-resource-projection-scenario",
                "--general-finite-lifecycle", "--general-post-suppression-failure", terminal,
                "--general-content-buffer-type", buffer_type, "--runner", str(build),
                "--output", str(build / "cases" / name)]
            if interval == "dynamic": command.append("--general-content-plan-dynamic")
            if interval == "whole": command.append("--general-source-whole")
            tests.append(dict(name=name, command=command))
        return dict(tests=tests)

    def test_exact_twelve_registered_cases_and_selectors(self):
        with tempfile.TemporaryDirectory() as directory:
            build = pathlib.Path(directory)
            expected, selected = select_tests(self.inventory(build), build)
            self.assertEqual(len(expected), 12)
            self.assertEqual(set(expected), set(selected))

    def test_ctest_filtered_backtrace_ids_are_diagnostic_only(self):
        with tempfile.TemporaryDirectory() as directory:
            build = pathlib.Path(directory); inventory = self.inventory(build)
            for index, test in enumerate(inventory["tests"]):
                test["backtrace"] = 227 + index
                test["properties"] = [dict(name="TIMEOUT", value=360.0), dict(name="PROCESSORS", value=1)]
            expected, selected = select_tests(inventory, build)
            filtered = copy.deepcopy(inventory)
            for index, test in enumerate(filtered["tests"]): test["backtrace"] = 1 + index
            verify_filtered_selection(expected, selected, filtered, build)

    def test_filtered_execution_or_extra_case_changes_refuse(self):
        for mutation in ("command", "timeout", "property", "extra_field", "missing_field", "extra_case"):
            with tempfile.TemporaryDirectory() as directory:
                build = pathlib.Path(directory); inventory = self.inventory(build)
                for test in inventory["tests"]:
                    test["backtrace"] = 227
                    test["properties"] = [dict(name="TIMEOUT", value=360.0)]
                expected, selected = select_tests(inventory, build)
                filtered = copy.deepcopy(inventory); first = filtered["tests"][0]
                first["backtrace"] = 1
                if mutation == "command": first["command"][0] = "foreign-python"
                if mutation == "timeout": first["properties"][0]["value"] = 1.0
                if mutation == "property": first["properties"].append(dict(name="DISABLED", value=True))
                if mutation == "extra_field": first["unrecognized_execution_field"] = True
                if mutation == "missing_field": first.pop("properties")
                if mutation == "extra_case": filtered["tests"].append(dict(name="foreign-case", command=[]))
                with self.subTest(mutation=mutation), self.assertRaisesRegex(ValueError, "POST-ATTEMPT-CTEST-SELECTION"):
                    verify_filtered_selection(expected, selected, filtered, build)

    def test_missing_duplicate_conflicting_foreign_or_preexisting_outputs_refuse(self):
        for mutation in ("missing", "duplicate", "selector", "runner", "output", "stale"):
            with tempfile.TemporaryDirectory() as directory:
                build = pathlib.Path(directory); inventory = self.inventory(build)
                if mutation == "missing": inventory["tests"].pop()
                if mutation == "duplicate": inventory["tests"].append(copy.deepcopy(inventory["tests"][0]))
                command = inventory["tests"][0]["command"]
                if mutation == "selector": command += ["--general-recording-terminal", "cancel"]
                if mutation == "runner": command[command.index("--runner") + 1] = str(build.parent / "foreign")
                if mutation == "output": command[command.index("--output") + 1] = str(build.parent / "foreign")
                if mutation == "stale":
                    path = pathlib.Path(command[command.index("--output") + 1]) / "report.json"
                    path.parent.mkdir(parents=True); path.write_text("old result", encoding="utf-8")
                with self.subTest(mutation=mutation), self.assertRaises(ValueError): select_tests(inventory, build)

    def test_complete_junit_retains_exact_driver_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            expected, unused = select_tests(self.inventory(root), root)
            suite = xml.Element("testsuite", tests="12", failures="0", disabled="0", skipped="0")
            stdout = json.dumps(dict(synthetic=True, gpuAuthority=False))
            for name in expected:
                case = xml.SubElement(suite, "testcase", name=name, status="run")
                xml.SubElement(case, "system-out").text = stdout
            path = root / "junit.xml"; xml.ElementTree(suite).write(path, encoding="utf-8")
            self.assertEqual(inspect_junit(path, expected), {name: stdout for name in expected})

    def test_junit_cannot_hide_missing_cases_errors_skips_or_failed_exit(self):
        for mutation in ("case", "missing", "status", "failure", "error", "skipped", "count", "failures", "disabled"):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); expected, unused = select_tests(self.inventory(root), root)
                suite = xml.Element("testsuite", tests="12", failures="0", disabled="0", skipped="0")
                for name in expected: xml.SubElement(suite, "testcase", name=name, status="run")
                first = suite.find("testcase")
                if mutation == "case": first.set("name", "foreign")
                if mutation == "missing": suite.remove(first)
                if mutation == "status": first.set("status", "notrun")
                if mutation in ("failure", "error", "skipped"): xml.SubElement(first, mutation)
                if mutation == "count": suite.set("tests", "11")
                if mutation in ("failures", "disabled"): suite.set(mutation, "1")
                path = root / "junit.xml"; xml.ElementTree(suite).write(path, encoding="utf-8")
                with self.subTest(mutation=mutation), self.assertRaises(ValueError): inspect_junit(path, expected)


if __name__ == "__main__":
    unittest.main()
