"""Exact native post-suppression report validation; grants no execution authority."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
from browser_contract_runner import BrowserProgressJournal


def inspect_post_suppression_report(report, buffer_type, interval, failure):
    if buffer_type not in ("uniform", "readonly-storage") or interval not in ("static", "dynamic", "whole"):
        raise ValueError("POST-SUPPRESSION-EVIDENCE-IDENTITY")
    if failure not in ("commit", "publication"):
        raise ValueError("POST-SUPPRESSION-EVIDENCE-CHOICE")
    publication = failure == "publication"
    diagnostic = "RHI-ORIGINAL-SNAPSHOT-PUBLICATION-OPT-IN" if publication else "RHI-GENERAL-SUBMISSION-COMPLETION-PROFILE"
    if (report.get("scenario") != "original-general-finite-post-suppression-failure"
            or report.get("originalGeneralPostSuppressionFailureKind") != failure
            or report.get("originalGeneralPostSuppressionFailureDiagnostic") != diagnostic
            or report.get("originalGeneralFiniteContentPlanBufferType") != buffer_type
            or report.get("originalGeneralFiniteContentPlanRangeKind") != interval
            or report.get("originalGeneralFiniteLifecycleVersion") != 4):
        raise ValueError("POST-SUPPRESSION-EVIDENCE-IDENTITY")
    if report.get("success") is not True or report.get("diagnostics") != [] or report.get("nativeErrors") != []:
        raise ValueError("POST-SUPPRESSION-EVIDENCE-NATIVE-ERROR")
    required = ("Held", "Terminal", "Diagnostic", "Retirement", "ReceiptRetirement", "Retention",
                "UnexpectedOutputAbsent", "OriginalHeld", "Content")
    if any(report.get("originalGeneralPostSuppression" + name + "Passed") is not True for name in required):
        raise ValueError("POST-SUPPRESSION-EVIDENCE-MISSING-PROOF")
    for name in ("ContentPlan", "LifecycleSeal", "ContentPlanRetirement", "NativeClosure", "NativePayload",
                 "OutputSnapshot"):
        if report.get("originalGeneralFinite" + name + "Passed") is not True:
            raise ValueError("POST-SUPPRESSION-EVIDENCE-PREPARATION")
    if report.get("originalGeneralCompletedTextureUploadPassed") is not True:
        raise ValueError("POST-SUPPRESSION-EVIDENCE-PREPARATION")
    if report.get("originalGeneralPostSuppressionMapDrainPassed") is not publication:
        raise ValueError("POST-SUPPRESSION-EVIDENCE-MAP-PROFILE")
    for gate in ("physicalDeviceLossValidated", "finiteLifecycle4Validated", "fullFiniteExecutionValidated",
                 "nativeShaderExecutionValidated", "nativeSyncAvailable", "publicBindlessExecutionAvailable",
                 "originalGeneralFiniteOutputNumericalPassed"):
        if report.get(gate) is not False:
            raise ValueError("POST-SUPPRESSION-EVIDENCE-CLOSED-GATE")
    if report.get("originalGeneralFiniteNumericalValues") != []:
        raise ValueError("POST-SUPPRESSION-EVIDENCE-UNEXPECTED-OUTPUT")
    progress = report.get("progress")
    if not isinstance(progress, list) or not progress or any(not isinstance(entry, dict) for entry in progress):
        raise ValueError("POST-SUPPRESSION-EVIDENCE-PROGRESS")
    messages = [entry.get("message") for entry in progress]
    if any(not isinstance(message, str) for message in messages):
        raise ValueError("POST-SUPPRESSION-EVIDENCE-PROGRESS")
    transport = report.get("progressTransport", {})
    if (transport.get("contractVersion") != 1 or transport.get("complete") is not True
            or transport.get("messages") != len(messages)
            or transport.get("codeUnits") != sum(BrowserProgressJournal.code_units_of(message) for message in messages)):
        raise ValueError("POST-SUPPRESSION-EVIDENCE-PROGRESS")
    required_messages = [
        "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_HELD_OK " + failure
        + " original-submitted-root-Pending original-loss-Armed no-injection native-receipts-pending output-unpublished public-closed",
        "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_TERMINAL_OK " + failure
        + " original-root-Invalidated journal-empty suppression1 detached-revisit1 real-queue-receipt foreign-callbacks-drained loss-userdata-cleared physical-loss-undelivered committed-before-failure"
        + ("1" if publication else "0") + " unpublished no-retry public-closed",
        "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_DIAGNOSTIC " + failure + " " + diagnostic + " native-post-suppression-rejection",
        "BINDLESS_RHI_GENERAL_POST_SUPPRESSION4_RETIRE_OK " + buffer_type + " " + interval + " " + failure
        + " caller-payload-root-closure-initializer-dropped original-records-reservations-counts-quotas-zero no-publication public-closed",
    ]
    if publication:
        required_messages += [
            "BINDLESS_RHI_GENERAL_FINITE_MAP4_UPSTREAM_RETIRE_OK original-Submission2 completed-committed recording-scopes3 submission-scopes3 queue1 original-receipts-reservations-quotas-zero public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_MAP4_RETENTION_OK " + buffer_type + " " + interval
            + " caller-payload-root-closure-initializer-dropped original-upstream-and-output-drained recording-quota-root-only public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_MAP4_COPY_HELD_OK post-suppression-publication original-upstream-completed downstream-copy-scopes3-held root-Pending unpublished queue-callback-retained public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_MAP4_HELD_OK post-suppression-publication native-map-invoked pending-record1 original-scopes-held unpublished owners-retained root-Pending",
            "BINDLESS_RHI_GENERAL_OUTPUT_MAP1_OK post-suppression-publication original-copy-map genuine-drain invalidated-unpublished-no-retry no-device-loss result-quota-zero public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_MAP4_OUTPUT_OK post-suppression-publication Snapshot2 Output2 original-upstream-completed original-copy-map-drained invalidated-unpublished-no-retry no-device-loss snapshot-reuse-refused public-closed",
        ]
    else:
        required_messages += [
            "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_RETIRE_OK initializer-expired original-recording-submission-callback-receipts-reservations-quotas-zero public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_RETENTION_OK " + buffer_type + " " + interval
            + " caller-payload-root-closure-initializer-dropped terminal-drained recording-quota-root-only public-closed",
            "BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_HELD_OK post-suppression-commit submit-invocation1 pending-record1 loss-armed physical-loss-undelivered scopes-retained output-unpublished root-Pending journal-retained",
        ]
    if any(messages.count(message) != 1 for message in required_messages):
        raise ValueError("POST-SUPPRESSION-EVIDENCE-NATIVE-MARKER")
    content_prefix = "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_" + ("COMMIT" if publication else "INVALIDATED") + "_OK " + buffer_type + " " + interval + " "
    content_suffix = (" original-upload-root-issuer pending-state-exclusion content-generation-revision-unchanged journal-count-quota-zero hot0-query0 public-closed"
                      if publication else " original-upload-root-issuer state-content-invalidated journal-count-quota-zero no-rollback no-output public-closed")
    if sum(message.startswith(content_prefix) and message.endswith(content_suffix) for message in messages) != 1:
        raise ValueError("POST-SUPPRESSION-EVIDENCE-CONTENT")
    unexpected = ("BINDLESS_RHI_GENERAL_FINITE_NUMERICAL2_OK ", "BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK ")
    if not publication:
        unexpected += ("BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_OK ",)
    if any(message.startswith(unexpected) for message in messages):
        raise ValueError("POST-SUPPRESSION-EVIDENCE-UNEXPECTED-OUTPUT")
