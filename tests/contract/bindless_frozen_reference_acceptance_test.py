"""Synthetic namespace/registration negatives only; no native execution evidence."""
import copy
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from bindless_frozen_reference_acceptance import FrozenReferenceFiniteAttempt
from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint


class FrozenReferenceAttemptTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="arp-finite-reference-contract-")
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name).resolve()
        self.source = self.root / "original"
        self.materialized = self.root / "materialized"
        self.reference = self.root / "reference"
        self.output = self.root / "attempt"
        self.stage = "claim"
        self.prefix = "arp_rhi_webgpu_bindless_pipeline_browser_smoke"
        self.names = ["tests/contract/bindless_webgpu_rhi_smoke.py",
            "build/web-webgpu-release/bindless_webgpu_rhi_runner.html",
            "build/web-webgpu-release/browser_progress_transport.js"]
        self.names += ["build/web-webgpu-release/" + self.prefix + suffix for suffix in (".js", ".wasm", ".data")]
        self.available = {self.materialized / name for name in self.names}

    def command(self, buffer="uniform", interval="static", terminal="cancel", group="claim_terminal"):
        result = ["registered-python", str(self.source / self.names[0]), "--browser", "registered-chrome",
            "--runner", str(self.source / "build/web-webgpu-release"), "--timeout-seconds", "180",
            "--general-resource-projection-scenario", "--general-finite-lifecycle",
            "--general-claim-terminal", terminal, "--general-content-buffer-type", buffer,
            "--output", str(self.source / "build/web-webgpu-release/artifacts/original")]
        if interval == "dynamic":
            result.append("--general-content-plan-dynamic")
        if interval == "whole":
            result.append("--general-source-whole")
        selectors = dict(recording_terminal="--general-recording-terminal",
            submission_terminal="--general-submission-terminal", physical_loss="--general-physical-loss",
            map_terminal="--general-map-terminal", claim_terminal="--general-claim-terminal",
            post_suppression="--general-post-suppression-failure", execution="--general-finite-execution")
        position = result.index("--general-claim-terminal")
        result[position] = selectors[group]
        if group == "execution":
            del result[position + 1]
            result.remove("--general-finite-lifecycle")
        return result

    def relocate(self, command, identity=("claim_terminal", "uniform", "static", "cancel")):
        return FrozenReferenceFiniteAttempt.relocate(command, identity, self.source,
            self.materialized, self.available, self.output)

    def fixture(self):
        self.reference.mkdir()
        self.materialized.mkdir()
        originals, copied = [], []
        for index, name in enumerate(self.names):
            path = self.materialized / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(("synthetic-input-" + name).encode())
            archive = self.reference / "inputs" / str(index) / path.name
            archive.parent.mkdir(parents=True)
            archive.write_bytes(path.read_bytes())
            originals.append(dict(source=str(self.source / name), path=str(archive.relative_to(self.reference)), **fingerprint(path)))
            copied.append(dict(originalSource=str(self.source / name), path=name, **fingerprint(path)))
        metadata = dict(contract="FrozenInputReferenceMaterialization/1", sourceRoot=str(self.source),
            reference=str(self.reference), copiedInputs=len(copied), inputs=copied,
            publicBindlessExecutionAvailable=False, ordinaryComputePredecessorEligible=False,
            platformValidated=False, productionReady=False)
        registrations = []
        for name, (group, buffer, interval, terminal) in expected_cases(self.stage).items():
            registrations.append(dict(name=name, command=self.command(buffer, interval, terminal, group)))
        self.write(self.reference / "inputs.json", originals)
        self.write(self.reference / "tests.json", dict(tests=registrations))
        self.write(self.materialized / "reference-manifest.json", metadata)
        return metadata, registrations

    @staticmethod
    def write(path, value):
        path.write_text(json.dumps(value), encoding="utf-8")

    def attempt(self):
        return FrozenReferenceFiniteAttempt(self.reference, self.materialized, self.output, self.stage)

    def test_only_three_declared_positions_change_deadline_selectors_stay_exact(self):
        for _, buffer, interval, terminal in expected_cases("claim").values():
            command = self.command(buffer, interval, terminal)
            relocated = self.relocate(command, ("claim_terminal", buffer, interval, terminal))
            changed = {index for index, pair in enumerate(zip(command, relocated)) if pair[0] != pair[1]}
            self.assertEqual(changed, {1, command.index("--runner") + 1, command.index("--output") + 1})
            self.assertEqual(relocated[relocated.index("--timeout-seconds") + 1], "180")
            self.assertEqual(command[1], str(self.source / self.names[0]))

    def test_missing_payload_and_unregistered_driver_or_runner_refuse(self):
        self.available.remove(self.materialized / ("build/web-webgpu-release/" + self.prefix + ".wasm"))
        with self.assertRaisesRegex(ValueError, "MISSING-INPUT"):
            self.relocate(self.command())
        for position in (1, self.command().index("--runner") + 1):
            command = self.command()
            command[position] = str(self.root / "unregistered")
            with self.assertRaisesRegex(ValueError, "REGISTERED-INPUT"):
                self.relocate(command)

    def test_ambiguous_arguments_invalid_deadline_and_hidden_original_path_refuse(self):
        for option in ("--runner", "--output", "--timeout-seconds"):
            with self.assertRaisesRegex(ValueError, "COMMAND"):
                self.relocate(self.command() + [option, "duplicate"])
        for value in ("0", "-1", "nan", "Infinity"):
            command = self.command()
            command[command.index("--timeout-seconds") + 1] = value
            with self.assertRaisesRegex(ValueError, "DEADLINE"):
                self.relocate(command)
        with self.assertRaisesRegex(ValueError, "UNDECLARED-SOURCE-PATH"):
            self.relocate(self.command() + ["--extra", str(self.source / "secret-input")])

    def test_prepare_freezes_registration_and_keeps_every_execution_gate_closed(self):
        self.fixture()
        with mock.patch("bindless_frozen_reference_acceptance.subprocess.call", side_effect=AssertionError("forbidden native launch")):
            result = self.attempt().prepare()
        self.assertEqual(result["expected"], 30)
        self.assertIs(result["complete"], False)
        for field in ("publicBindlessExecutionAvailable", "ordinaryComputePredecessorEligible",
                "nativeSyncAvailable", "graphicsValidated", "woccValidated", "platformValidated", "productionReady"):
            self.assertIs(result[field], False)
        self.assertFalse((self.output / "validation.json").exists())

    def test_missing_case_and_duplicate_registration_refuse_before_attempt_creation(self):
        _, registered = self.fixture()
        for changed in (registered[:-1], registered + [registered[0]]):
            self.write(self.reference / "tests.json", dict(tests=changed))
            with self.assertRaises(ValueError):
                self.attempt().prepare()
            self.assertFalse(self.output.exists())

    def test_changed_copy_forged_source_and_path_escape_refuse(self):
        metadata, _ = self.fixture()
        for edit in (lambda m: m["inputs"][0].update(bytes=0),
                     lambda m: m["inputs"][0].update(originalSource=str(self.root / "forged")),
                     lambda m: m["inputs"][0].update(path="../outside")):
            wrong = copy.deepcopy(metadata)
            edit(wrong)
            self.write(self.materialized / "reference-manifest.json", wrong)
            with self.assertRaises(ValueError):
                self.attempt().prepare()
            self.assertFalse(self.output.exists())

    def test_reference_outputs_and_overwriting_prior_attempt_refuse(self):
        self.fixture()
        for parent in (self.source, self.reference, self.materialized):
            attempt = FrozenReferenceFiniteAttempt(self.reference, self.materialized, parent / "new-output", "claim")
            with self.assertRaisesRegex(ValueError, "OUTPUT-OWNERSHIP"):
                attempt.prepare()
            self.assertFalse((parent / "new-output").exists())
        self.output.mkdir()
        marker = self.output / "preserved-failure"
        marker.write_text("failed attempt must remain", encoding="utf-8")
        with self.assertRaises(FileExistsError):
            self.attempt().prepare()
        self.assertEqual(marker.read_text(encoding="utf-8"), "failed attempt must remain")

    def test_post_preflight_materialized_mutation_is_detected(self):
        self.fixture()
        attempt = self.attempt()
        attempt.prepare()
        (self.materialized / self.names[0]).write_text("changed", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "INPUT-CHANGED"):
            attempt.verify_unchanged()


if __name__ == "__main__":
    unittest.main()
