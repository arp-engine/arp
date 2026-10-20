"""Synthetic hostile replay tests; fake tools and markers grant no GPU proof."""
import copy
import json
import pathlib
import shutil
import tempfile
import unittest
import xml.etree.ElementTree as xml
from unittest import mock

from bindless_finite_lifecycle_acceptance import fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, ROOT, read_json, verify_file
import bindless_finite_lifecycle_evidence_test as native_fixtures
import bindless_finite_lifecycle_predecessor_evidence_test as package_fixtures
import bindless_webgpu_post_suppression_attempt_test as attempt_fixtures
import bindless_webgpu_probe_archive_audit_test as build_fixtures
from bindless_webgpu_probe_archive_audit import replay as replay_probe, artifact_inventory
from bindless_webgpu_probe_build_evidence import ORDINARY, MAIN, PROBE, TARGET
from bindless_webgpu_post_suppression_archive_audit import cases, inspect_attempt, freeze, replay
import bindless_webgpu_post_suppression_archive_audit as post_audit
from bindless_webgpu_post_suppression_attempt import selection_expression

NINJA = pathlib.Path("unit-ninja")
NM = pathlib.Path("unit-nm")


class PostSuppressionArchiveTest(unittest.TestCase):
    def add_frozen_probe_readers(self, snapshot):
        inputs = read_json(snapshot / "inputs.json")
        for filename in ("bindless_webgpu_probe_archive_audit.py", "bindless_webgpu_probe_build_evidence.py"):
            source = pathlib.Path(__file__).with_name(filename)
            text = source.read_text(encoding="utf-8")
            if filename == "bindless_webgpu_probe_archive_audit.py":
                for line in ("        logicalSourceRoot=str(source_root),\n",
                             "        latestExecutionRecords=execution_records,\n",
                             "        dependencyMtimes=dependency_mtimes,\n"):
                    self.assertIn(line, text)
                    text = text.replace(line, "")
            destination = snapshot / "frozen-readers" / filename
            destination.parent.mkdir(exist_ok=True)
            destination.write_text(text, encoding="utf-8")
            inputs.append(dict(source=str(ROOT / "tests/contract" / filename),
                path=str(destination.relative_to(snapshot)), **fingerprint(destination)))
        self.write(snapshot / "inputs.json", inputs)
        for filename in ("preflight.json", "validation.json"):
            path = snapshot / filename; manifest = read_json(path)
            manifest["inputs"] = inputs
            for key in post_audit.PROBE_DERIVED_FIELDS:
                manifest["probeBuildReplay"].pop(key)
            self.write(path, manifest)

    def test_frozen_legacy_reader_matches_without_editing_captured_result(self):
        with tempfile.TemporaryDirectory() as directory:
            snapshot, log_source, fake_tools = self.fixture(pathlib.Path(directory))
            self.add_frozen_probe_readers(snapshot)
            before = {p: fingerprint(p) for p in snapshot.rglob("*") if p.is_file()}
            with mock.patch("subprocess.check_output", side_effect=fake_tools):
                result = inspect_attempt(snapshot, log_source, NINJA, NM)
            self.assertEqual(result["cases"], 12)
            self.assertIn("logicalSourceRoot", result["probeBuildReplay"])
            self.assertEqual(before, {p: fingerprint(p) for p in snapshot.rglob("*") if p.is_file()})

    def test_archived_cache_source_root_controls_relocated_reader_without_live_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            snapshot, log_source, fake_tools = self.fixture(pathlib.Path(directory))
            self.add_frozen_probe_readers(snapshot)
            with mock.patch.object(post_audit, "ROOT", snapshot / "unrelated-relocated-reader"), mock.patch("subprocess.check_output", side_effect=fake_tools):
                result = inspect_attempt(snapshot, log_source, NINJA, NM)
            self.assertEqual(result["cases"], 12)
            self.assertEqual(result["probeBuildReplay"]["logicalSourceRoot"], str(ROOT.resolve()))

    def test_legacy_reader_missing_changed_partial_or_forged_result_refuses(self):
        for mutation in ("missing", "changed", "partial", "payload", "root", "unrelated"):
            with tempfile.TemporaryDirectory() as directory:
                snapshot, log_source, fake_tools = self.fixture(pathlib.Path(directory))
                self.add_frozen_probe_readers(snapshot)
                if mutation in ("missing", "changed"):
                    path = snapshot / "frozen-readers/bindless_webgpu_probe_archive_audit.py"
                    if mutation == "missing": path.unlink()
                    else: path.write_bytes(path.read_bytes() + b"\nchanged\n")
                else:
                    for filename in ("preflight.json", "validation.json"):
                        path = snapshot / filename; manifest = read_json(path)
                        captured = manifest["probeBuildReplay"]
                        if mutation == "partial": captured["dependencyMtimes"] = {}
                        if mutation == "payload": captured["payload"] = {}
                        if mutation == "root": captured["logicalSourceRoot"] = str(snapshot / "forged")
                        if mutation == "unrelated": captured.pop("archivedRecipesReplayed")
                        self.write(path, manifest)
                with self.subTest(mutation=mutation), mock.patch("subprocess.check_output", side_effect=fake_tools), self.assertRaises((ValueError, OSError)):
                    inspect_attempt(snapshot, log_source, NINJA, NM)

    def test_rehashed_legacy_reader_cannot_override_current_payload_observation(self):
        with tempfile.TemporaryDirectory() as directory:
            snapshot, log_source, fake_tools = self.fixture(pathlib.Path(directory))
            self.add_frozen_probe_readers(snapshot)
            path = snapshot / "frozen-readers/bindless_webgpu_probe_archive_audit.py"
            text = path.read_text(encoding="utf-8")
            self.assertIn("payload=payload, postSuppressionRuntimeValidated=False", text)
            path.write_text(text.replace("payload=payload, postSuppressionRuntimeValidated=False",
                "payload={}, postSuppressionRuntimeValidated=False"), encoding="utf-8")
            inputs = read_json(snapshot / "inputs.json")
            next(row for row in inputs if row["path"] == str(path.relative_to(snapshot))).update(fingerprint(path))
            self.write(snapshot / "inputs.json", inputs)
            for filename in ("preflight.json", "validation.json"):
                target = snapshot / filename; manifest = read_json(target)
                manifest["inputs"] = inputs
                manifest["probeBuildReplay"]["payload"] = {}
                self.write(target, manifest)
            with mock.patch("subprocess.check_output", side_effect=fake_tools), self.assertRaisesRegex(ValueError, "FINITE-NATIVE-FIELD: payload"):
                inspect_attempt(snapshot, log_source, NINJA, NM)

    @staticmethod
    def write(path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value), encoding="utf-8")

    def fixture(self, root):
        root = root.resolve()
        archive, build, log_source, unused, fake_tools = build_fixtures.ProbeArchiveAuditTest().fixture(root)
        snapshot = root / "snapshot"; snapshot.mkdir()
        probe_root = snapshot / "probe-build"; archive.rename(probe_root)
        probe_sources = artifact_inventory(probe_root, read_json(probe_root / "validation.json"))
        frozen = {source: path.read_bytes() for source, path in probe_sources.items()
                  if source.name == "CMakeCache.txt" or source.name in {TARGET + suffix for suffix in (".js", ".wasm", ".data")}
                  or source in {ROOT / name for name in ("CMakeLists.txt", ORDINARY, MAIN, PROBE,
                      "tests/fixtures/webgpu_post_suppression_failure_probe_fixture.h")}}
        frozen.update({ROOT / name: b"synthetic source, no native authority" for name in (
            "tests/contract/bindless_webgpu_rhi_smoke.py",
            "tests/contract/bindless_webgpu_post_suppression_attempt.py",
            "tests/contract/bindless_webgpu_post_suppression_attempt_test.py",
            "scripts/browser_contract_runner.py")})
        frozen.update({build / name: b"synthetic browser input" for name in (
            "bindless_webgpu_rhi_runner.html", "browser_progress_transport.js")})
        inputs = []
        for index, (source, data) in enumerate(frozen.items()):
            path = snapshot / "inputs" / str(index) / source.name
            path.parent.mkdir(parents=True); path.write_bytes(data)
            inputs.append(dict(source=str(source), path=str(path.relative_to(snapshot)), **fingerprint(path)))
        packages, unused = package_fixtures.PredecessorEvidenceTest.package_fixture(snapshot / "package-fixture")
        package_fields = read_json(snapshot / "package-fixture/report.json")
        for entry in packages["inputs"]:
            copied = copy.deepcopy(entry); copied["path"] = str(pathlib.Path("package-fixture") / copied["path"])
            inputs.append(copied)
        inventory = attempt_fixtures.PostSuppressionAttemptTest.inventory(build)
        for index, test in enumerate(inventory["tests"]):
            test["backtrace"] = 227 + index
            test["properties"] = [dict(name="TIMEOUT", value=360.0)]
            command = test["command"]
            command[command.index("--output") + 1] = str(build / "artifacts" / "post-suppression" / str(index))
        selected = {test["name"]: test for test in inventory["tests"]}
        filtered = copy.deepcopy(inventory)
        for index, test in enumerate(filtered["tests"]): test["backtrace"] = 1 + index
        records = []; suite = xml.Element("testsuite", tests="12", failures="0", disabled="0", skipped="0")
        lines = ["Test project " + str(build)]
        for index, (name, identity) in enumerate(cases().items()):
            report = native_fixtures.FiniteNativeEvidenceTest().finite(identity); report.update(package_fields)
            path = snapshot / "cases" / name / "report.json"; self.write(path, report)
            stdout = json.dumps(report); path.with_name("driver.log").write_text(stdout, encoding="utf-8")
            path.with_name("browser.log").write_text("synthetic, no GPU execution", encoding="utf-8")
            testcase = xml.SubElement(suite, "testcase", name=name, status="run")
            xml.SubElement(testcase, "system-out").text = stdout
            records.append(dict(name=name, identity=list(identity), command=selected[name]["command"], exitCode=0,
                accepted=True, report=dict(path=str(path.relative_to(snapshot)), **fingerprint(path))))
            lines.append(f" {index + 1}/12 Test #{index + 114}: {name} ..........   Passed   75.08 sec")
        lines.append("100% tests passed, 0 tests failed out of 12")
        (snapshot / "ctest.log").write_text("\n".join(lines), encoding="utf-8")
        xml.ElementTree(suite).write(snapshot / "ctest.xml", encoding="utf-8")
        with mock.patch("subprocess.check_output", side_effect=fake_tools):
            probe = replay_probe(probe_root, log_source, NINJA, NM)
        budget = 33554432
        preflight = dict(contract="FinitePostSuppressionNativeAttempt/1", complete=False,
            scope="private finite Compute probe12 only", cases=12, probeBuildReplay=probe, inputs=inputs,
            selectedTests=selected, perTestOutputBytes=budget,
            ordinaryFrozenInputsReference=dict(path=str(root / "absent-ordinary-inputs.json"), bytes=1, sha256="f" * 64),
            affectedRegressionsValidated=False, aggregatePrerequisitesValidated=False, **CLOSED_GATES)
        self.write(snapshot / "preflight.json", preflight)
        self.write(snapshot / "validation.json", dict(preflight, complete=True, results=records, exitCode=0,
                                                      selectedNativeReportsChecked=True))
        self.write(snapshot / "attempt.json", dict(complete=False, exitCode=0, selectedNativeReportsChecked=False, **CLOSED_GATES))
        self.write(snapshot / "inputs.json", inputs); self.write(snapshot / "tests.json", inventory)
        self.write(snapshot / "filtered-tests.json", filtered)
        self.write(snapshot / "invocation.json", dict(command=["ctest", "--test-dir", str(build), "--parallel", "1",
            "-R", selection_expression(cases()), "--no-tests=error", "--test-output-size-passed", str(budget),
            "--test-output-size-failed", str(budget), "--output-on-failure", "--output-junit", str(snapshot / "ctest.xml")]))
        captured = []
        for case in suite.findall("testcase"):
            name = case.get("name"); command = selected[name]["command"]
            source = pathlib.Path(command[command.index("--output") + 1]); files = []
            path = snapshot / "cases" / name / "junit-output.log"
            path.write_text(case.findtext("system-out"), encoding="utf-8")
            for filename in ("report.json", "browser.log", "junit-output.log"):
                path = snapshot / "cases" / name / filename
                origin = "ctest.xml:system-out:" + name if filename == "junit-output.log" else str(source / filename)
                files.append(dict(source=origin, path=str(path.relative_to(snapshot)), **fingerprint(path)))
            captured.append(dict(name=name, command=command, files=files, accepted=False))
        self.write(snapshot / "case-files.json", dict(contract="FinitePostSuppressionCaseCapture/1", complete=False,
            scope="captured registered CTest outputs only; no native acceptance", cases=12, captured=captured, **CLOSED_GATES))
        return snapshot, log_source, fake_tools

    def test_exact_copied_attempt_replays_packages_build_junit_and_native_markers(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); snapshot, log_source, fake_tools = self.fixture(root)
            with mock.patch("subprocess.check_output", side_effect=fake_tools):
                result = inspect_attempt(snapshot, log_source, NINJA, NM)
                self.assertEqual(result["cases"], 12)
                self.assertEqual(len(result["packageIdentities"]), 4)
                archive = root / "audit"; accepted = freeze(snapshot, archive, log_source, NINJA, NM)
                shutil.rmtree(snapshot)
                self.assertEqual(replay(archive, log_source, NINJA, NM), accepted)
                for gate in CLOSED_GATES: self.assertIs(accepted[gate], False)
                self.assertTrue(accepted["postSuppressionRuntimeValidated"])
                self.assertFalse(accepted["aggregatePrerequisitesValidated"])

    def test_incomplete_gate_failed_duplicate_missing_or_changed_reports_refuse(self):
        for mutation in ("complete", "gate", "exit", "duplicate", "missing", "report", "identity", "command"):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); snapshot, log_source, fake_tools = self.fixture(root)
                path = snapshot / "validation.json"; manifest = read_json(path)
                if mutation == "complete": manifest["complete"] = False
                if mutation == "gate": manifest["publicBindlessExecutionAvailable"] = True
                if mutation == "exit": manifest["exitCode"] = 1
                if mutation == "duplicate": manifest["results"][1] = copy.deepcopy(manifest["results"][0])
                if mutation == "missing": manifest["results"].pop()
                if mutation == "report": verify_file(snapshot, manifest["results"][0]["report"]).write_bytes(b"{}")
                if mutation == "identity": manifest["results"][0]["identity"][1] = "foreign-type"
                if mutation == "command": manifest["results"][0]["command"][0] = "foreign-python"
                self.write(path, manifest)
                with self.subTest(mutation=mutation), mock.patch("subprocess.check_output", side_effect=fake_tools), self.assertRaises(ValueError):
                    inspect_attempt(snapshot, log_source, NINJA, NM)

    def test_rehashed_native_marker_or_driver_package_forgery_refuses(self):
        for mutation in ("native", "package", "stdout"):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); snapshot, log_source, fake_tools = self.fixture(root)
                path = snapshot / "validation.json"; manifest = read_json(path); record = manifest["results"][0]
                report_path = snapshot / record["report"]["path"]; report = read_json(report_path)
                if mutation == "native": report["progress"] = []
                if mutation == "package": report["publicComputePackageIdentity"] = "f" * 64
                if mutation != "stdout":
                    self.write(report_path, report); record["report"].update(fingerprint(report_path)); self.write(path, manifest)
                    report_path.with_name("driver.log").write_text(json.dumps(report), encoding="utf-8")
                    junit = xml.parse(snapshot / "ctest.xml"); junit.getroot().find("testcase/system-out").text = json.dumps(report)
                    junit.write(snapshot / "ctest.xml", encoding="utf-8")
                else: report_path.with_name("driver.log").write_text("{}", encoding="utf-8")
                with self.subTest(mutation=mutation), mock.patch("subprocess.check_output", side_effect=fake_tools), self.assertRaises(ValueError):
                    inspect_attempt(snapshot, log_source, NINJA, NM)

    def test_filtered_execution_invocation_and_ctest_terminal_transcript_refuse_changes(self):
        for mutation in ("timeout", "extra-case", "parallel", "regex", "no-tests", "log", "junit"):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); snapshot, log_source, fake_tools = self.fixture(root)
                if mutation in ("timeout", "extra-case"):
                    path = snapshot / "filtered-tests.json"; filtered = read_json(path)
                    if mutation == "timeout": filtered["tests"][0]["properties"][0]["value"] = 1
                    else: filtered["tests"].append(dict(name="foreign", command=[]))
                    self.write(path, filtered)
                if mutation in ("parallel", "regex", "no-tests"):
                    path = snapshot / "invocation.json"; invocation = read_json(path)
                    position = invocation["command"].index({"parallel": "--parallel", "regex": "-R", "no-tests": "--no-tests=error"}[mutation])
                    if mutation == "no-tests": invocation["command"].pop(position)
                    else: invocation["command"][position + 1] = "foreign"
                    self.write(path, invocation)
                if mutation == "log": (snapshot / "ctest.log").write_text("100% tests passed", encoding="utf-8")
                if mutation == "junit":
                    path = snapshot / "ctest.xml"; junit = xml.parse(path); junit.getroot().find("testcase").set("status", "notrun")
                    junit.write(path, encoding="utf-8")
                with self.subTest(mutation=mutation), mock.patch("subprocess.check_output", side_effect=fake_tools), self.assertRaises(ValueError):
                    inspect_attempt(snapshot, log_source, NINJA, NM)

    def test_missing_copied_payload_never_reads_live_original_and_missing_packages_refuse(self):
        for mutation in ("payload", "publication"):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); snapshot, log_source, fake_tools = self.fixture(root)
                manifest_path = snapshot / "validation.json"; manifest = read_json(manifest_path)
                entry = next(entry for entry in manifest["inputs"] if pathlib.Path(entry["source"]).name ==
                             (TARGET + ".wasm" if mutation == "payload" else "WebGpuPublicCompute.emulated.bindless.yaml"))
                if mutation == "payload":
                    original = pathlib.Path(entry["source"]); original.parent.mkdir(parents=True, exist_ok=True)
                    original.write_bytes(verify_file(snapshot, entry).read_bytes()); verify_file(snapshot, entry).unlink()
                else:
                    manifest["inputs"].remove(entry); self.write(manifest_path, manifest)
                    self.write(snapshot / "inputs.json", manifest["inputs"])
                    preflight = read_json(snapshot / "preflight.json"); preflight["inputs"] = manifest["inputs"]
                    self.write(snapshot / "preflight.json", preflight)
                with self.subTest(mutation=mutation), mock.patch("subprocess.check_output", side_effect=fake_tools), self.assertRaises((ValueError, OSError)):
                    inspect_attempt(snapshot, log_source, NINJA, NM)

    def test_copied_summary_profile_missing_duplicate_or_gate_forgery_refuses(self):
        for mutation in ("profile", "duplicate", "missing", "gate", "count", "packages", "source"):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); snapshot, log_source, fake_tools = self.fixture(root); archive = root / "audit"
                with mock.patch("subprocess.check_output", side_effect=fake_tools): freeze(snapshot, archive, log_source, NINJA, NM)
                path = archive / "validation.json"; manifest = read_json(path)
                if mutation == "profile": manifest["semanticProfiles"] = {}
                if mutation == "duplicate": manifest["artifacts"].append(manifest["artifacts"][0]); manifest["artifactCount"] += 1
                if mutation == "missing": manifest["artifacts"].pop(); manifest["artifactCount"] -= 1
                if mutation == "gate": manifest["finiteLifecycle4Validated"] = True
                if mutation == "count": manifest["cases"] -= 1
                if mutation == "packages": manifest["packageIdentities"] = {}
                if mutation == "source": manifest["sourceAcceptance"]["sha256"] = "f" * 64
                self.write(path, manifest)
                with self.subTest(mutation=mutation), mock.patch("subprocess.check_output", side_effect=fake_tools), self.assertRaises(ValueError):
                    replay(archive, log_source, NINJA, NM)

    def test_source_build_correspondence_cannot_borrow_another_rehashed_payload(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); snapshot, log_source, fake_tools = self.fixture(root)
            manifest = read_json(snapshot / "validation.json")
            entry = next(entry for entry in manifest["inputs"] if pathlib.Path(entry["source"]).name == TARGET + ".wasm")
            path = verify_file(snapshot, entry); path.write_bytes(b"another rehashed payload"); entry.update(fingerprint(path))
            self.write(snapshot / "validation.json", manifest); self.write(snapshot / "inputs.json", manifest["inputs"])
            preflight = read_json(snapshot / "preflight.json"); preflight["inputs"] = manifest["inputs"]
            self.write(snapshot / "preflight.json", preflight)
            with mock.patch("subprocess.check_output", side_effect=fake_tools), self.assertRaisesRegex(ValueError, "BUILD-INPUT-CORRESPONDENCE"):
                inspect_attempt(snapshot, log_source, NINJA, NM)

    def test_case_capture_cannot_promote_authority_or_substitute_outputs(self):
        for mutation in ("accepted", "source", "missing", "duplicate", "stdout"):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); snapshot, log_source, fake_tools = self.fixture(root)
                path = snapshot / "case-files.json"; capture = read_json(path)
                record = capture["captured"][0]
                if mutation == "accepted": record["accepted"] = True
                if mutation == "source": record["files"][0]["source"] = "foreign-source"
                if mutation == "missing": record["files"].pop()
                if mutation == "duplicate": record["files"][1] = copy.deepcopy(record["files"][0])
                if mutation == "stdout":
                    entry = record["files"][2]; copied = verify_file(snapshot, entry)
                    copied.write_text("{}", encoding="utf-8"); entry.update(fingerprint(copied))
                self.write(path, capture)
                with self.subTest(mutation=mutation), mock.patch("subprocess.check_output", side_effect=fake_tools), self.assertRaises(ValueError):
                    inspect_attempt(snapshot, log_source, NINJA, NM)


if __name__ == "__main__":
    unittest.main()
