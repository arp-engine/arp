"""Hostile evidence tests only; these synthetic traces prove no native execution."""
import copy
import unittest

from bindless_post_suppression_evidence import BrowserProgressJournal, inspect_post_suppression_report


class PostSuppressionEvidenceTest(unittest.TestCase):
    def report(self):
        messages = [
            "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_HELD_OK commit original-submitted-root-Pending original-loss-Armed no-injection native-receipts-pending output-unpublished public-closed",
            "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_TERMINAL_OK commit original-root-Invalidated journal-empty suppression1 detached-revisit1 real-queue-receipt foreign-callbacks-drained loss-userdata-cleared physical-loss-undelivered committed-before-failure0 unpublished no-retry public-closed",
            "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_DIAGNOSTIC commit RHI-GENERAL-SUBMISSION-COMPLETION-PROFILE native-post-suppression-rejection",
            "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_RETIRE_OK uniform static commit caller-payload-root-closure-initializer-dropped original-records-reservations-counts-quotas-zero no-publication public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_RETIRE_OK initializer-expired original-recording-submission-callback-receipts-reservations-quotas-zero public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_RETENTION_OK uniform static caller-payload-root-closure-initializer-dropped terminal-drained recording-quota-root-only public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_HELD_OK post-suppression-commit submit-invocation1 pending-record1 loss-armed physical-loss-undelivered scopes-retained output-unpublished root-Pending journal-retained",
            "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_INVALIDATED_OK uniform static synthetic-test-only original-upload-root-issuer state-content-invalidated journal-count-quota-zero no-rollback no-output public-closed",
        ]
        report = dict(scenario="original-general-finite-post-suppression-failure", success=True,
                      diagnostics=[], nativeErrors=[], originalGeneralPostSuppressionFailureKind="commit",
                      originalGeneralPostSuppressionFailureDiagnostic="RHI-GENERAL-SUBMISSION-COMPLETION-PROFILE",
                      originalGeneralFiniteContentPlanBufferType="uniform", originalGeneralFiniteContentPlanRangeKind="static",
                      originalGeneralFiniteLifecycleVersion=4, originalGeneralPostSuppressionMapDrainPassed=False,
                      originalGeneralCompletedTextureUploadPassed=True, originalGeneralFiniteNumericalValues=[],
                      progress=[dict(message=message) for message in messages])
        for name in ("Held", "Terminal", "Diagnostic", "Retirement", "ReceiptRetirement", "Retention",
                     "UnexpectedOutputAbsent", "OriginalHeld", "Content"):
            report["originalGeneralPostSuppression" + name + "Passed"] = True
        for name in ("ContentPlan", "LifecycleSeal", "ContentPlanRetirement", "NativeClosure", "NativePayload", "OutputSnapshot"):
            report["originalGeneralFinite" + name + "Passed"] = True
        for name in ("physicalDeviceLossValidated", "finiteLifecycle4Validated", "fullFiniteExecutionValidated",
                     "nativeShaderExecutionValidated", "nativeSyncAvailable", "publicBindlessExecutionAvailable",
                     "originalGeneralFiniteOutputNumericalPassed"):
            report[name] = False
        self.transport(report)
        return report

    def transport(self, report):
        report["progressTransport"] = dict(contractVersion=1, complete=True, messages=len(report["progress"]),
            codeUnits=sum(BrowserProgressJournal.code_units_of(entry["message"]) for entry in report["progress"]))

    def publication_report(self):
        report = self.report()
        messages = [entry["message"] for entry in report["progress"][:4]]
        messages = [message.replace(" commit ", " publication ")
                    .replace("committed-before-failure0", "committed-before-failure1")
                    .replace("RHI-GENERAL-SUBMISSION-COMPLETION-PROFILE", "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-OPT-IN")
                    for message in messages]
        messages += [
            "BINDLESS_RHI_GENERAL_FINITE_MAP4_UPSTREAM_RETIRE_OK original-Submission2 completed-committed recording-scopes3 submission-scopes3 queue1 original-receipts-reservations-quotas-zero public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_MAP4_RETENTION_OK uniform static caller-payload-root-closure-initializer-dropped original-upstream-and-output-drained recording-quota-root-only public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_MAP4_COPY_HELD_OK post-suppression-publication original-upstream-completed downstream-copy-scopes3-held root-Pending unpublished queue-callback-retained public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_MAP4_HELD_OK post-suppression-publication native-map-invoked pending-record1 original-scopes-held unpublished owners-retained root-Pending",
            "BINDLESS_RHI_GENERAL_OUTPUT_MAP1_OK post-suppression-publication original-copy-map genuine-drain invalidated-unpublished-no-retry no-device-loss result-quota-zero public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_MAP4_OUTPUT_OK post-suppression-publication Snapshot2 Output2 original-upstream-completed original-copy-map-drained invalidated-unpublished-no-retry no-device-loss snapshot-reuse-refused public-closed",
            "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_COMMIT_OK uniform static synthetic-test-only original-upload-root-issuer pending-state-exclusion content-generation-revision-unchanged journal-count-quota-zero hot0-query0 public-closed",
        ]
        report["progress"] = [dict(message=message) for message in messages]
        report["originalGeneralPostSuppressionFailureKind"] = "publication"
        report["originalGeneralPostSuppressionFailureDiagnostic"] = "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-OPT-IN"
        report["originalGeneralPostSuppressionMapDrainPassed"] = True
        self.transport(report)
        return report

    def test_both_failure_profiles_and_all_selected_buffer_ranges(self):
        for failure in ("commit", "publication"):
            for buffer_type in ("uniform", "readonly-storage"):
                for interval in ("static", "dynamic", "whole"):
                    report = self.report() if failure == "commit" else self.publication_report()
                    report["originalGeneralFiniteContentPlanBufferType"] = buffer_type
                    report["originalGeneralFiniteContentPlanRangeKind"] = interval
                    for entry in report["progress"]:
                        entry["message"] = entry["message"].replace("uniform static ", buffer_type + " " + interval + " ")
                    self.transport(report)
                    with self.subTest(failure=failure, buffer=buffer_type, interval=interval):
                        inspect_post_suppression_report(report, buffer_type, interval, failure)

    def test_publication_requires_each_native_map_and_content_proof(self):
        valid = self.publication_report()
        for index in range(len(valid["progress"])):
            hostile = copy.deepcopy(valid); hostile["progress"].pop(index); self.transport(hostile)
            with self.subTest(marker=index), self.assertRaises(ValueError):
                inspect_post_suppression_report(hostile, "uniform", "static", "publication")

    def test_missing_or_duplicate_native_markers_refuse_even_with_success_flags(self):
        valid = self.report()
        inspect_post_suppression_report(valid, "uniform", "static", "commit")
        for index in range(len(valid["progress"])):
            for duplicate in (False, True):
                report = copy.deepcopy(valid)
                if duplicate:
                    report["progress"].append(copy.deepcopy(report["progress"][index]))
                else:
                    report["progress"].pop(index)
                self.transport(report)
                with self.subTest(marker=index, duplicate=duplicate), self.assertRaises(ValueError):
                    inspect_post_suppression_report(report, "uniform", "static", "commit")

    def test_closed_gate_claims_native_errors_and_unexpected_output_refuse(self):
        for name, value in (("nativeErrors", ["native failure"]), ("publicBindlessExecutionAvailable", True),
                            ("nativeSyncAvailable", True), ("physicalDeviceLossValidated", True),
                            ("finiteLifecycle4Validated", True), ("originalGeneralFiniteNumericalValues", [1]),
                            ("originalGeneralPostSuppressionRetirementPassed", False),
                            ("originalGeneralPostSuppressionFailureDiagnostic", "other"),
                            ("originalGeneralFiniteContentPlanRangeKind", "dynamic")):
            report = self.report(); report[name] = value
            with self.subTest(field=name), self.assertRaises(ValueError):
                inspect_post_suppression_report(report, "uniform", "static", "commit")
        for unexpected in ("BINDLESS_RHI_GENERAL_FINITE_NUMERICAL2_OK unexpected",
                           "BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK unexpected",
                           "BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_OK unexpected"):
            report = self.report(); report["progress"].append(dict(message=unexpected)); self.transport(report)
            with self.assertRaises(ValueError):
                inspect_post_suppression_report(report, "uniform", "static", "commit")

    def test_other_scenario_and_publication_cannot_borrow_commit_evidence(self):
        report = self.report(); report["scenario"] = "original-general-finite-claim-terminal"
        with self.assertRaises(ValueError):
            inspect_post_suppression_report(report, "uniform", "static", "commit")
        report = self.report(); report["originalGeneralPostSuppressionFailureKind"] = "publication"
        report["originalGeneralPostSuppressionFailureDiagnostic"] = "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-OPT-IN"
        report["originalGeneralPostSuppressionMapDrainPassed"] = True
        with self.assertRaises(ValueError):
            inspect_post_suppression_report(report, "uniform", "static", "publication")

    def test_progress_receipt_cannot_hide_removed_proof(self):
        report = self.report(); report["progressTransport"]["codeUnits"] -= 1
        with self.assertRaises(ValueError):
            inspect_post_suppression_report(report, "uniform", "static", "commit")


if __name__ == "__main__":
    unittest.main()
