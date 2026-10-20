"""Hostile report and archive tests. Synthetic traces confer no GPU evidence."""
import copy
import json
import pathlib
import tempfile
import unittest

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_evidence import BrowserProgressJournal, inspect_finite_report, numerical, NativeProgressEvidence
from bindless_finite_lifecycle_archive_audit import archived_path, read_json, inspect_snapshot, CLOSED_GATES, ROOT
from bindless_post_suppression_evidence_test import PostSuppressionEvidenceTest


class FiniteNativeEvidenceTest(unittest.TestCase):
    @staticmethod
    def transport(report):
        messages = [item["message"] for item in report["progress"]]
        report["progressTransport"] = dict(contractVersion=1, complete=True, messages=len(messages),
            codeUnits=sum(BrowserProgressJournal.code_units_of(message) for message in messages))

    def claim(self, buffer_type="uniform", interval="static", terminal="cancel"):
        physical, logical = terminal == "physical-loss", terminal in ("retired", "shutdown")
        policy = ("actual-device-loss state-content-invalidated" if physical else
                  "logical-device-terminal observations-refused physical-loss-undelivered" if logical else
                  "no-recording-state-revision-unchanged original-upload-content-unchanged")
        report = dict(success=True, diagnostics=[], nativeErrors=[], scenario="original-general-finite-claim-terminal",
            originalGeneralFiniteLifecycleVersion=4, originalGeneralFiniteContentPlanBufferType=buffer_type,
            originalGeneralFiniteContentPlanRangeKind=interval, originalGeneralFiniteClaimTerminalKind=terminal,
            originalGeneralFiniteClaimContentPolicy=policy, originalGeneralFiniteNumericalValues=[],
            physicalDeviceLossValidated=physical)
        for key in ("publicBindlessExecutionAvailable", "nativeSyncAvailable", "finiteLifecycle4Validated",
                    "fullFiniteExecutionValidated", "nativeShaderExecutionValidated", "originalGeneralFiniteOutputNumericalPassed"):
            report[key] = False
        for key in ("ContentPlan", "LifecycleSeal", "ContentPlanRetirement", "NativeClosure", "NativePayload", "OutputSnapshot"):
            report["originalGeneralFinite" + key + "Passed"] = True
        report["originalGeneralCompletedTextureUploadPassed"] = True
        for key in ("Held", "Terminal", "ReceiptRetirement", "Retention", "Content", "ExecutionAbsent"):
            report["originalGeneralFiniteClaim" + key + "Passed"] = True
        selected = buffer_type + " " + interval
        messages = [
            "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_OK commands10 sequenceCells9 finalCells4 steps10 sampled-mips2 completed-control1 upload0 visits12 rootBytes1 exact-less-exhausted rollback-no-root-journal-count-quota hot0-query0 native-closed",
            "BINDLESS_RHI_GENERAL_FINITE_LIFECYCLE4_OK ContentPlan5 synthetic-unit-fixture exact-less-exhausted sticky-claim forged-caller-drop hot0-query0 original-counts-quotas-zero cpu-admission-only",
            "BINDLESS_RHI_GENERAL_SAMPLED_INITIALIZER_OK same-device upload20 mips2 original-scopes-drained recorded-submit-completed committed-root",
            "BINDLESS_RHI_GENERAL_CLOSURE9_OK sources8 resources6 pipeline1 descriptor1 custody2 synthetic-unit-fixture exact-less-exhausted no-root-journal-mutation hot0-query0 freeze-native-closed",
            "BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_FROZEN_OK general-finite-original9 commands10 synthetic-unit-fixture original-native-bindings exact-pass-draw-bound exact-less-exhausted forged-native-mutated-reject hot0-query0 execution-closed",
            "BINDLESS_RHI_GENERAL_OUTPUT_SNAPSHOT2_PREPARED_OK Issuer7 ContentPlan5 synthetic-unit-fixture exact-less-exhausted cross-profile-forged-refused",
            "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_RETIRE_OK original-source-input-state-stream-initializer-quotas-zero native-public-closed",
            "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_DEVICE_RETIRE_OK original-device-pipeline-texture-upload-quotas-zero public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_HELD_OK original-root-Prepared journal-empty sticky-claim original-loss-armed pending-record1 issuers-live no-recording no-submission no-output public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_TERMINAL_OK " + terminal + " "
                + ("root-Invalidated" if physical else "root-Abandoned")
                + " journal-empty sticky-CPU-closed encoder0 groups0 submit0 original-local-loss-drained unpublished no-retry "
                + ("actual-device-loss" if physical else "physical-loss-undelivered") + " "
                + ("issuer-callback-facades-expired" if terminal == "drop" else "issuer-callback-facades-retained") + " public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_RETIRE_OK initializer-expired original-prepared-native-receipts7-suppressed reservations-quotas-zero no-recording-no-completion public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_RETENTION_OK " + selected
                + " caller-payload-root-closure-initializer-dropped claim-terminal-drained recording-quota-root-only public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_CLAIM4_CONTENT_OK " + selected + " " + policy
                + (" journal-count-quota-zero no-recording-no-output public-closed" if physical or logical
                   else " journal-count-quota-zero no-output public-closed"),
        ]
        report["progress"] = [dict(message=message) for message in messages]
        self.transport(report)
        return report

    def finite(self, identity):
        group, buffer_type, interval, terminal = identity
        if group == "claim_terminal":
            return self.claim(buffer_type, interval, terminal)
        report = self.claim(buffer_type, interval)
        report = {key: value for key, value in report.items() if not key.startswith("originalGeneralFiniteClaim")}
        common = [item for item in report["progress"] if not item["message"].startswith("BINDLESS_RHI_GENERAL_FINITE_CLAIM4_")]
        selected = buffer_type + " " + interval
        messages = []
        if group == "post_suppression":
            fixture = PostSuppressionEvidenceTest()
            report = fixture.report() if terminal == "commit" else fixture.publication_report()
            report["originalGeneralFiniteContentPlanBufferType"] = buffer_type
            report["originalGeneralFiniteContentPlanRangeKind"] = interval
            report["progress"] = common + [dict(message=item["message"].replace("uniform static ", selected + " "))
                                         for item in report["progress"]]
            self.transport(report)
            return report
        scenario = dict(recording_terminal="recording-terminal", submission_terminal="submission-terminal",
                        physical_loss="physical-loss", map_terminal="map-terminal", execution="execution")[group]
        report["scenario"] = "original-general-finite-" + scenario
        committed = False
        if group == "recording_terminal":
            for key in ("originalGeneralFiniteOutputSnapshotPassed", "originalGeneralFiniteOutputNumericalPassed",
                        "originalGeneralFiniteNumericalValues", "physicalDeviceLossValidated"):
                report.pop(key)
            common = [item for item in common if "OUTPUT_SNAPSHOT2" not in item["message"]]
            report.update(originalGeneralFiniteRecordingTerminalKind=terminal,
                originalGeneralFiniteRecordingTerminalHeldPassed=True, originalGeneralFiniteRecordingTerminalPassed=True,
                originalGeneralFiniteRecordingTerminalRetentionPassed=True, originalGeneralFiniteContentPlanRollbackPassed=True)
            messages += [
                "BINDLESS_RHI_GENERAL_FINITE_RECORDING4_HELD_OK " + terminal
                    + " root-Pending journal-retained initializer-retained native-scopes3 no-submit no-output "
                    + ("issuer-custody-expired" if terminal == "drop" else "custody-live"),
                "BINDLESS_RHI_GENERAL_FINITE_RECORDING4_TERMINAL_OK " + terminal
                    + " root-Abandoned journal-empty scopes-drained loss-suppressed initializer-expired original-reservations-quotas-zero unpublished no-retry public-closed",
                "BINDLESS_RHI_GENERAL_FINITE_RECORDING4_RETENTION_OK " + selected
                    + " caller-payload-root-closure-initializer-dropped terminal-drained recording-quota-root-only public-closed",
                "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_ROLLBACK_OK " + selected
                    + " offset0 size16 extent16 original-upload-root-issuer pending-state-exclusion content-generation-revision-unchanged journal-count-quota-zero hot0-query0 native-closed"]
        elif group == "submission_terminal":
            committed = terminal == "reverse-scopes"
            report.update(originalGeneralFiniteSubmissionTerminalKind=terminal,
                originalGeneralFiniteSubmissionTerminalHeldPassed=True, originalGeneralFiniteSubmissionTerminalPassed=True,
                originalGeneralFiniteSubmissionTerminalRetentionPassed=True, originalGeneralFiniteSubmissionTerminalReceiptRetirementPassed=True,
                originalGeneralFiniteSubmissionVersion=2, originalGeneralFiniteOutputVersion=2,
                originalGeneralFiniteContentPlanCommitPassed=committed, originalGeneralFiniteContentPlanInvalidatedPassed=not committed)
            messages += [
                "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_HELD_OK " + terminal
                    + " submit-invocation1 pending-record1 loss-armed physical-loss-undelivered scopes-retained output-unpublished root-Pending journal-retained",
                "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_TERMINAL_OK " + terminal
                    + " scopes-queue-drained loss-suppressed physical-loss-undelivered no-retry "
                    + ("root-Committed numerical-output" if committed else "root-Invalidated output-unpublished") + " public-closed",
                "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_RETENTION_OK " + selected
                    + " caller-payload-root-closure-initializer-dropped terminal-drained recording-quota-root-only public-closed",
                "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_RETIRE_OK initializer-expired original-recording-submission-callback-receipts-reservations-quotas-zero public-closed"]
        elif group == "physical_loss":
            report.update(originalGeneralFiniteLossKind=terminal, physicalDeviceLossValidated=True,
                originalGeneralFiniteLossHeldPassed=True, originalGeneralFiniteLossTerminalPassed=True,
                originalGeneralFiniteLossRetirementPassed=True, originalGeneralFiniteLossRetentionPassed=True,
                originalGeneralFiniteContentPlanInvalidatedPassed=True)
            messages += [
                "BINDLESS_RHI_GENERAL_FINITE_LOSS4_HELD_OK " + terminal
                    + " pending-record1 original-loss-armed physical-loss-undelivered owners-retained no-completion no-output",
                "BINDLESS_RHI_GENERAL_FINITE_LOSS4_TERMINAL_OK " + terminal
                    + " actual-device-destroy original-loss-delivered genuine-local-drain root-Invalidated journal-empty no-completion unpublished no-retry public-closed",
                "BINDLESS_RHI_GENERAL_FINITE_LOSS4_RETIRE_OK initializer-expired original-receipts-reservations-quotas-zero actual-loss-drained public-closed",
                "BINDLESS_RHI_GENERAL_FINITE_LOSS4_RETENTION_OK " + selected
                    + " caller-payload-root-closure-initializer-dropped actual-loss-drained recording-quota-root-only public-closed"]
        elif group == "map_terminal":
            committed = terminal not in ("throw-before", "throw-after", "scope-registration", "unmap-throw", "pending-loss", "published-loss")
            output = terminal in ("reverse-scopes", "published-loss")
            report.update(originalGeneralFiniteMapTerminalKind=terminal, physicalDeviceLossValidated=not committed,
                originalGeneralFiniteSubmissionVersion=2, originalGeneralFiniteOutputVersion=2,
                **{"originalGeneralFiniteMap" + name + "Passed": True for name in
                   ("Output", "CopyHeld", "Held", "Drain", "UpstreamRetirement", "Retention")})
            messages += [
                "BINDLESS_RHI_GENERAL_FINITE_MAP4_COPY_HELD_OK " + terminal
                    + " original-upstream-completed downstream-copy-scopes3-held root-Pending unpublished queue-callback-retained public-closed",
                "BINDLESS_RHI_GENERAL_FINITE_MAP4_HELD_OK " + terminal
                    + " native-map-invoked pending-record1 original-scopes-held unpublished owners-retained root-Pending",
                "BINDLESS_RHI_GENERAL_FINITE_MAP4_OUTPUT_OK " + terminal
                    + " Snapshot2 Output2 original-upstream-completed original-copy-map-drained "
                    + ("numerical-published" if output else "invalidated-unpublished-no-retry") + " "
                    + ("no-device-loss" if committed else "actual-device-loss") + " snapshot-reuse-refused public-closed",
                "BINDLESS_RHI_GENERAL_OUTPUT_MAP1_OK " + terminal + " original-copy-map genuine-drain "
                    + ("committed-numerical-published" if output else "invalidated-unpublished-no-retry") + " "
                    + ("no-device-loss" if committed else "actual-device-loss") + " result-quota-zero public-closed",
                "BINDLESS_RHI_GENERAL_FINITE_MAP4_UPSTREAM_RETIRE_OK original-Submission2 completed-committed recording-scopes3 submission-scopes3 queue1 original-receipts-reservations-quotas-zero public-closed",
                "BINDLESS_RHI_GENERAL_FINITE_MAP4_RETENTION_OK " + selected
                    + " caller-payload-root-closure-initializer-dropped original-upstream-and-output-drained recording-quota-root-only public-closed"]
            if not committed:
                messages.append("BINDLESS_RHI_GENERAL_FINITE_MAP4_UPSTREAM_LOSS_OK " + selected + " " + terminal
                    + " original-upstream-committed physical-controls-invalidated journal-count-quota-zero published-bytes-policy-checked public-closed")
        else:
            committed = True
            report.update(originalGeneralFinitePendingNativeValidationPassed=True,
                originalGeneralFiniteContentPlanCommitPassed=True, originalGeneralFiniteContentPlanRollbackPassed=False,
                originalGeneralFiniteNativeRecordingPassed=True, originalGeneralFiniteNativeRecordingRetentionPassed=True,
                originalGeneralFiniteNativeRecordingVersion=7, originalGeneralFiniteNativeSubmissionAvailable=True,
                originalGeneralFiniteNativeClosureRetentionPassed=True, originalGeneralFiniteNativeClosureContractVersion=9,
                originalGeneralFiniteInitializedBufferCustodyVersion=2, originalGeneralFiniteNativeFreezePassed=True,
                originalGeneralFiniteNativePayloadRetentionPassed=True, originalGeneralFiniteNativeFreezeVersion=7,
                originalGeneralFiniteNativePayloadVersion=7, originalGeneralFiniteNativePayloadCommands=10,
                originalGeneralFiniteFrozenDependencies=4, originalGeneralFiniteColdRecordingExecutionAvailable=True,
                originalGeneralFiniteSubmissionPassed=True, originalGeneralFiniteSubmissionVersion=2,
                originalGeneralFiniteOutputVersion=2, originalGeneralFiniteExecutionRetentionPassed=True)
            for key in ("finiteLifecycle4Validated", "fullFiniteExecutionValidated", "physicalDeviceLossValidated"):
                report.pop(key)
            messages += [
                "BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_OK Cold7 Command7 Callback7 Issuer7 Lifecycle4 ContentPlan5 Submission2 Output2 commands10 recordingScopes3 submissionScopes3 queue1 synthetic-unit-fixture exact-less-exhausted original-seal-journal full-backing caller-drop forged-native-mutation strict-public hot0-query0 real-completed committed numerical-readback quotas-zero public-closed",
                "BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_RETENTION_OK " + selected
                    + " caller-payload-root-closure-initializer-dropped original-submitted-output-mapped recording-quota-root-only public-closed",
                "BINDLESS_RHI_GENERAL_FROZEN7_OK Closure9 Freeze7 Payload7 commands10 dependencies4 initial1 final3 complete-original-offsets synthetic-unit-fixture exact-less-exhausted original-root-journal hot0-query0 no-execution-authority"]
        if committed:
            messages.append("BINDLESS_RHI_GENERAL_CONTENT_PLAN5_COMMIT_OK " + selected
                + " offset0 size16 extent16 original-upload-root-issuer pending-state-exclusion content-generation-revision-unchanged journal-count-quota-zero hot0-query0 public-closed")
        elif group in ("submission_terminal", "physical_loss"):
            messages.append("BINDLESS_RHI_GENERAL_CONTENT_PLAN5_INVALIDATED_OK " + selected + " " + terminal
                + " original-upload-root-issuer state-content-invalidated journal-count-quota-zero no-rollback no-output public-closed")
        has_output = group == "execution" or group == "submission_terminal" and committed or group == "map_terminal" and output
        if has_output:
            values = [value + (4 if interval == "dynamic" else 0) for value in (1.02734375, 2.03125, 3.03515625, 4.0390625)]
            report.update(originalGeneralFiniteOutputNumericalPassed=True, originalGeneralFiniteNumericalValues=values,
                          nativeShaderExecutionValidated=True)
            rgba = " ".join(str(value) for value in values)
            messages += ["BINDLESS_RHI_GENERAL_FINITE_NUMERICAL2_OK original-copy1 map1 unmap-drain committed-published rgba "
                         + rgba + " Output2 selected-bytes-oracle public-closed",
                         "BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK original-copy1 map1 unmap-drain committed-published rgba "
                         + rgba + " owners4 hot0-query0 public-closed"]
        report["progress"] = common + [dict(message=message) for message in messages]
        self.transport(report)
        return report

    def test_full_204_terminal_and_six_numerical_schemas(self):
        for identity in expected_cases("compute-final").values():
            with self.subTest(identity=identity):
                inspect_finite_report(self.finite(identity), identity)

    def test_each_profile_rejects_missing_terminal_or_numerical_proof(self):
        for identity in expected_cases("compute-final").values():
            valid = self.finite(identity)
            for index, item in enumerate(valid["progress"]):
                # Successful map terminals may complete before a pending-map
                # checkpoint is emitted; every other protocol marker is required.
                if identity[0] == "map_terminal" and identity[3] in ("reverse-scopes", "published-loss") and "MAP4_HELD_OK" in item["message"]:
                    continue
                hostile = copy.deepcopy(valid); hostile["progress"].pop(index); self.transport(hostile)
                with self.subTest(identity=identity, marker=item["message"].split()[0]), self.assertRaises(ValueError):
                    inspect_finite_report(hostile, identity)

    def test_exact_thirty_claim_schemas(self):
        for identity in expected_cases("claim").values():
            with self.subTest(identity=identity):
                inspect_finite_report(self.claim(*identity[1:]), identity)

    def test_each_original_marker_missing_duplicated_or_semantically_changed_refuses(self):
        valid = self.claim()
        for index in range(len(valid["progress"])):
            for mutation in ("missing", "duplicate", "changed"):
                hostile = copy.deepcopy(valid)
                if mutation == "missing":
                    hostile["progress"].pop(index)
                elif mutation == "duplicate":
                    hostile["progress"].append(copy.deepcopy(hostile["progress"][index]))
                else:
                    hostile["progress"][index]["message"] += " unexpected-ownership"
                self.transport(hostile)
                with self.subTest(index=index, mutation=mutation), self.assertRaises(ValueError):
                    inspect_finite_report(hostile, ("claim_terminal", "uniform", "static", "cancel"))

    def test_each_boolean_is_explicit_and_cannot_be_an_integer(self):
        valid = self.claim()
        for key, value in valid.items():
            if type(value) is not bool:
                continue
            for mutation in ("missing", "flipped", "integer"):
                hostile = copy.deepcopy(valid)
                if mutation == "missing":
                    hostile.pop(key)
                else:
                    hostile[key] = not value if mutation == "flipped" else int(value)
                with self.subTest(key=key, mutation=mutation), self.assertRaises(ValueError):
                    inspect_finite_report(hostile, ("claim_terminal", "uniform", "static", "cancel"))

    def test_scenario_and_physical_loss_cannot_borrow_another_terminal(self):
        for terminal in ("drop", "retired", "shutdown", "physical-loss"):
            with self.subTest(terminal=terminal), self.assertRaises(ValueError):
                inspect_finite_report(self.claim(terminal=terminal), ("claim_terminal", "uniform", "static", "cancel"))

    def test_transport_and_unexpected_numerical_execution_refuse(self):
        for key, value in (("messages", 0), ("codeUnits", 0), ("complete", False), ("contractVersion", 2)):
            hostile = self.claim(); hostile["progressTransport"][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                inspect_finite_report(hostile, ("claim_terminal", "uniform", "static", "cancel"))
        for marker in ("BINDLESS_RHI_GENERAL_FINITE_NUMERICAL2_OK unexpected",
                       "BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK unexpected",
                       "BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_OK unexpected"):
            hostile = self.claim(); hostile["progress"].append(dict(message=marker)); self.transport(hostile)
            with self.subTest(marker=marker), self.assertRaises(ValueError):
                inspect_finite_report(hostile, ("claim_terminal", "uniform", "static", "cancel"))

    def test_reported_values_must_match_both_native_numerical_markers(self):
        report = self.claim()
        report.update(originalGeneralFiniteOutputNumericalPassed=True,
                      originalGeneralFiniteNumericalValues=[1.02734375, 2.03125, 3.03515625, 4.0390625])
        report["progress"] += [dict(message=marker) for marker in (
            "BINDLESS_RHI_GENERAL_FINITE_NUMERICAL2_OK original-copy1 map1 unmap-drain committed-published rgba 1.02734375 2.03125000 3.03515625 4.03906250 Output2 selected-bytes-oracle public-closed",
            "BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK original-copy1 map1 unmap-drain committed-published rgba 1.02734375 2.03125000 3.03515625 4.03906250 owners4 hot0-query0 public-closed")]
        self.transport(report)
        numerical(report, NativeProgressEvidence(report), True, "static")
        hostile = copy.deepcopy(report)
        hostile["progress"][-1]["message"] = hostile["progress"][-1]["message"].replace("1.02734375", "nan")
        self.transport(hostile)
        with self.assertRaises(ValueError):
            numerical(hostile, NativeProgressEvidence(hostile), True, "static")
        with self.assertRaises(ValueError):
            numerical(report, NativeProgressEvidence(report), True, "dynamic")

    def test_archive_path_escape_duplicate_json_keys_and_missing_file_refuse(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            path = root / "report.json"; path.write_text('{"success":true,"success":false}', encoding="utf-8")
            with self.assertRaises(ValueError):
                read_json(path)
            for relative in ("../report.json", "missing.json", str(path.resolve())):
                with self.subTest(relative=relative), self.assertRaises(ValueError):
                    archived_path(root, relative)


class FiniteArchiveEvidenceTest(unittest.TestCase):
    @staticmethod
    def write(path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, indent=2), encoding="utf-8")

    def snapshot(self, root):
        sources = [ROOT / name for name in (
            "CMakeLists.txt", "tests/contract/bindless_webgpu_rhi_smoke.py",
            "tests/contract/bindless_finite_lifecycle_acceptance.py", "tests/fixtures/bindless_webgpu_rhi_runner.html",
            "tests/fixtures/original_general_finite_claim_terminal_fixture.h", "scripts/browser_contract_runner.py",
            "build/unit-fixture/arp_rhi_webgpu_bindless_pipeline_browser_smoke.data",
            "build/unit-fixture/arp_rhi_webgpu_bindless_pipeline_browser_smoke.js",
            "build/unit-fixture/arp_rhi_webgpu_bindless_pipeline_browser_smoke.wasm",
            "build/unit-fixture/bindless_webgpu_rhi_runner.html", "build/unit-fixture/browser_progress_transport.js")]
        inputs = []
        for index, source in enumerate(sources):
            destination = root / "inputs" / str(index) / source.name
            destination.parent.mkdir(parents=True)
            destination.write_bytes(b"synthetic archive unit fixture; no native evidence")
            inputs.append(dict(source=str(source), path=str(destination.relative_to(root)), **fingerprint(destination)))
        records, tests = [], []
        fixture = FiniteNativeEvidenceTest()
        for name, identity in expected_cases("claim").items():
            command = ["unit-fixture-python", "unit-fixture-driver", "--general-resource-projection-scenario",
                "--general-finite-lifecycle", "--general-claim-terminal", identity[3],
                "--general-content-buffer-type", identity[1], "--output", str(root / "cases" / name)]
            if identity[2] == "dynamic": command.append("--general-content-plan-dynamic")
            if identity[2] == "whole": command.append("--general-source-whole")
            path = root / "cases" / name / "report.json"
            report = fixture.claim(*identity[1:])
            self.write(path, report); self.write(path.with_name("driver.log"), report)
            path.with_name("browser.log").write_text("synthetic unit fixture", encoding="utf-8")
            records.append(dict(name=name, identity=list(identity), command=command, exitCode=0, accepted=True,
                report=dict(path=str(path.relative_to(root)), **fingerprint(path))))
            tests.append(dict(name=name, command=command))
        manifest = dict(contract="FiniteLifecycleAcceptance/1", stage="claim", scope="private finite Compute only",
            complete=True, cases=30, results=records, inputs=inputs, transitiveSdkProvenanceComplete=False,
            finiteMatrixValidated=False, **CLOSED_GATES)
        self.write(root / "validation.json", manifest)
        self.write(root / "inputs.json", inputs)
        self.write(root / "tests.json", dict(tests=tests))
        self.write(root / "attempt.json", dict(stage="claim", expected=30, results=records,
            complete=False, publicBindlessExecutionAvailable=False))
        return manifest

    def test_complete_stage_keeps_aggregate_and_public_gates_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory); self.snapshot(root)
            result = inspect_snapshot(root)
            self.assertEqual(len(result["profiles"]), 30)
            self.assertFalse(result["finiteLifecycle4Validated"])
            self.assertFalse(result["publicBindlessExecutionAvailable"])
            # Live source verification must not accept synthetic archived bytes.
            with self.assertRaises(ValueError):
                inspect_snapshot(root, verify_live=True)

    def test_tampered_selected_input_and_report_hash_refuse(self):
        for which in ("input", "report"):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); manifest = self.snapshot(root)
                entry = manifest["inputs"][0] if which == "input" else manifest["results"][0]["report"]
                path = root / entry["path"]; path.write_bytes(path.read_bytes() + b" changed")
                with self.subTest(which=which), self.assertRaises(ValueError):
                    inspect_snapshot(root)

    def test_missing_duplicate_failed_or_mismatched_case_refuses(self):
        for mutation in ("missing", "duplicate", "failed", "identity", "selector", "driver", "payload", "gate"):
            with tempfile.TemporaryDirectory() as directory:
                root = pathlib.Path(directory); manifest = self.snapshot(root)
                if mutation == "missing": manifest["results"].pop()
                if mutation == "duplicate": manifest["results"][-1] = copy.deepcopy(manifest["results"][0])
                if mutation == "failed": manifest["results"][0]["exitCode"] = 1
                if mutation == "identity": manifest["results"][0]["identity"][3] = "drop"
                if mutation == "selector":
                    command = manifest["results"][0]["command"]
                    command[command.index("--general-claim-terminal") + 1] = "drop"
                    tests = read_json(root / "tests.json"); tests["tests"][0]["command"] = command
                    self.write(root / "tests.json", tests)
                if mutation == "driver":
                    path = root / manifest["results"][0]["report"]["path"]
                    self.write(path.with_name("driver.log"), dict(success=True))
                if mutation == "payload":
                    manifest["inputs"] = [entry for entry in manifest["inputs"] if not entry["source"].endswith(".wasm")]
                    self.write(root / "inputs.json", manifest["inputs"])
                if mutation == "gate": manifest["finiteLifecycle4Validated"] = True
                self.write(root / "validation.json", manifest)
                self.write(root / "attempt.json", dict(stage="claim", expected=30, results=manifest["results"],
                    complete=False, publicBindlessExecutionAvailable=False))
                with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                    inspect_snapshot(root)


if __name__ == "__main__":
    unittest.main()
