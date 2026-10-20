"""Independent finite Compute report evidence; never publishes GPU capabilities.

Boolean browser summaries are checked against the original native progress
protocol. Unit fixtures are deliberately synthetic and confer no GPU evidence.
"""
import math
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "scripts"))
from browser_contract_runner import BrowserProgressJournal
from bindless_post_suppression_evidence import inspect_post_suppression_report


def require_fields(report, fields):
    if not isinstance(report, dict):
        raise ValueError("FINITE-NATIVE-FIELDS")
    for key, value in fields.items():
        if key not in report or type(report[key]) is not type(value) or report[key] != value:
            raise ValueError("FINITE-NATIVE-FIELD: " + key)


class NativeProgressEvidence:
    def __init__(self, report):
        progress = report.get("progress")
        if not isinstance(progress, list) or not progress or any(not isinstance(item, dict) for item in progress):
            raise ValueError("FINITE-NATIVE-PROGRESS")
        self.messages = [item.get("message") for item in progress]
        if any(not isinstance(message, str) for message in self.messages):
            raise ValueError("FINITE-NATIVE-PROGRESS")
        # Replay the existing bounded transport contract, including UTF16 units.
        journal = BrowserProgressJournal()
        batch_size = journal.limits["batchMessages"]
        for offset in range(0, len(self.messages), batch_size):
            journal.accept(dict(contractVersion=1, first=offset,
                                messages=self.messages[offset:offset + batch_size]), 0)
        require_fields(report.get("progressTransport", {}), dict(
            contractVersion=1, complete=True, messages=len(self.messages), codeUnits=journal.code_units))

    def one(self, prefix, suffix=None):
        family = prefix.split(" ", 1)[0] + " "
        matches = [message for message in self.messages if message.startswith(family)]
        if len(matches) != 1 or not matches[0].startswith(prefix) or (suffix is None and matches[0] != prefix
                                or suffix is not None and not matches[0].endswith(suffix)):
            raise ValueError("FINITE-NATIVE-MARKER: " + prefix)
        return matches[0]

    def absent(self, *prefixes):
        if any(message.startswith(prefixes) for message in self.messages):
            raise ValueError("FINITE-NATIVE-UNEXPECTED-MARKER")


def preparation(evidence, snapshot):
    evidence.one("BINDLESS_RHI_GENERAL_CONTENT_PLAN5_OK commands10 sequenceCells9 finalCells4 steps10 sampled-mips2 completed-control1 upload0 visits12 ",
                 " exact-less-exhausted rollback-no-root-journal-count-quota hot0-query0 native-closed")
    evidence.one("BINDLESS_RHI_GENERAL_FINITE_LIFECYCLE4_OK ContentPlan5 ",
                 " exact-less-exhausted sticky-claim forged-caller-drop hot0-query0 original-counts-quotas-zero cpu-admission-only")
    evidence.one("BINDLESS_RHI_GENERAL_SAMPLED_INITIALIZER_OK same-device upload20 mips2 original-scopes-drained recorded-submit-completed committed-root")
    evidence.one("BINDLESS_RHI_GENERAL_CLOSURE9_OK sources8 resources6 pipeline1 descriptor1 custody2 ",
                 " exact-less-exhausted no-root-journal-mutation hot0-query0 freeze-native-closed")
    evidence.one("BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_FROZEN_OK general-finite-original9 commands10 ",
                 " original-native-bindings exact-pass-draw-bound exact-less-exhausted forged-native-mutated-reject hot0-query0 execution-closed")
    evidence.one("BINDLESS_RHI_GENERAL_CONTENT_PLAN5_RETIRE_OK original-source-input-state-stream-initializer-quotas-zero native-public-closed")
    evidence.one("BINDLESS_RHI_GENERAL_CONTENT_PLAN5_DEVICE_RETIRE_OK original-device-pipeline-texture-upload-quotas-zero public-closed")
    if snapshot:
        evidence.one("BINDLESS_RHI_GENERAL_OUTPUT_SNAPSHOT2_PREPARED_OK Issuer7 ContentPlan5 ",
                     " exact-less-exhausted cross-profile-forged-refused")


def content(evidence, kind, buffer_type, interval, terminal=None):
    prefix = "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_" + kind + "_OK " + buffer_type + " " + interval + " "
    if kind == "INVALIDATED":
        evidence.one(prefix + terminal + " original-upload-root-issuer state-content-invalidated journal-count-quota-zero no-rollback no-output public-closed")
    else:
        evidence.one(prefix, " original-upload-root-issuer pending-state-exclusion content-generation-revision-unchanged journal-count-quota-zero hot0-query0 "
                     + ("native-closed" if kind == "ROLLBACK" else "public-closed"))
    for other in ("COMMIT", "ROLLBACK", "INVALIDATED"):
        if other != kind:
            evidence.absent("BINDLESS_RHI_GENERAL_CONTENT_PLAN5_" + other + "_OK ")


def numerical(report, evidence, enabled, interval):
    expected = [value + (4 if interval == "dynamic" else 0)
                for value in (1.02734375, 2.03125, 3.03515625, 4.0390625)]
    if enabled:
        require_fields(report, dict(originalGeneralFiniteOutputNumericalPassed=True,
                                    originalGeneralFiniteNumericalValues=expected))
        for prefix, suffix in (
            ("BINDLESS_RHI_GENERAL_FINITE_NUMERICAL2_OK ", " Output2 selected-bytes-oracle public-closed"),
            ("BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK ", " hot0-query0 public-closed")):
            marker = evidence.one(prefix + "original-copy1 map1 unmap-drain committed-published rgba ", suffix)
            try:
                values = [float(word) for word in marker.split(" rgba ", 1)[1].split()[:4]]
            except (ValueError, IndexError) as error:
                raise ValueError("FINITE-NATIVE-NUMERICAL") from error
            if len(values) != 4 or values != expected or not all(math.isfinite(value) for value in values):
                raise ValueError("FINITE-NATIVE-NUMERICAL")
    else:
        # Recording and physical-loss reports do not publish numerical fields.
        if "originalGeneralFiniteOutputNumericalPassed" in report:
            require_fields(report, dict(originalGeneralFiniteOutputNumericalPassed=False))
        if report.get("originalGeneralFiniteNumericalValues", []) != []:
            raise ValueError("FINITE-NATIVE-UNEXPECTED-OUTPUT")
        evidence.absent("BINDLESS_RHI_GENERAL_FINITE_NUMERICAL2_OK ", "BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK ")


def inspect_finite_report(report, identity):
    # Deferred to avoid a cycle when the matrix runner consumes this validator.
    from bindless_finite_lifecycle_acceptance import TYPES, RANGES, TERMINALS
    group, buffer_type, interval, terminal = identity
    if (buffer_type not in TYPES or interval not in RANGES
            or group != "execution" and (group not in TERMINALS or terminal not in TERMINALS[group])
            or group == "execution" and terminal is not None):
        raise ValueError("FINITE-NATIVE-IDENTITY")
    scenario = dict(recording_terminal="recording-terminal", submission_terminal="submission-terminal",
                    physical_loss="physical-loss", map_terminal="map-terminal", claim_terminal="claim-terminal",
                    post_suppression="post-suppression-failure", execution="execution")[group]
    require_fields(report, dict(success=True, diagnostics=[], nativeErrors=[],
        scenario="original-general-finite-" + scenario, originalGeneralFiniteLifecycleVersion=4,
        originalGeneralFiniteContentPlanBufferType=buffer_type, originalGeneralFiniteContentPlanRangeKind=interval,
        publicBindlessExecutionAvailable=False, nativeSyncAvailable=False))
    # Execution7's historical report schema has no aggregate gate fields.
    # Their absence is not promotion; every lifecycle report must explicitly close them.
    for gate in ("finiteLifecycle4Validated", "fullFiniteExecutionValidated"):
        if group != "execution" or gate in report:
            require_fields(report, {gate: False})
    preparation_fields = dict(originalGeneralFiniteContentPlanPassed=True,
        originalGeneralFiniteLifecycleSealPassed=True, originalGeneralFiniteContentPlanRetirementPassed=True,
        originalGeneralCompletedTextureUploadPassed=True, originalGeneralFiniteNativeClosurePassed=True,
        originalGeneralFiniteNativePayloadPassed=True)
    if group not in ("recording_terminal", "execution"):
        preparation_fields["originalGeneralFiniteOutputSnapshotPassed"] = True
    require_fields(report, preparation_fields)
    evidence = NativeProgressEvidence(report)
    preparation(evidence, group != "recording_terminal")
    selected = buffer_type + " " + interval
    emits_numerical = (group == "execution"
        or group == "submission_terminal" and terminal == "reverse-scopes"
        or group == "map_terminal" and terminal in ("reverse-scopes", "published-loss"))
    require_fields(report, dict(nativeShaderExecutionValidated=emits_numerical))
    if not emits_numerical and group not in ("recording_terminal", "physical_loss"):
        require_fields(report, dict(originalGeneralFiniteOutputNumericalPassed=False,
                                    originalGeneralFiniteNumericalValues=[]))
    if group in ("recording_terminal", "submission_terminal", "execution") and "physicalDeviceLossValidated" in report:
        require_fields(report, dict(physicalDeviceLossValidated=False))
    numerical(report, evidence, emits_numerical, interval)
    if group == "post_suppression":
        inspect_post_suppression_report(report, buffer_type, interval, terminal)
    elif group == "claim_terminal":
        physical, logical = terminal == "physical-loss", terminal in ("retired", "shutdown")
        policy = ("actual-device-loss state-content-invalidated" if physical else
                  "logical-device-terminal observations-refused physical-loss-undelivered" if logical else
                  "no-recording-state-revision-unchanged original-upload-content-unchanged")
        require_fields(report, dict(originalGeneralFiniteClaimTerminalKind=terminal,
            originalGeneralFiniteClaimContentPolicy=policy, physicalDeviceLossValidated=physical,
            **{"originalGeneralFiniteClaim" + name + "Passed": True for name in
               ("Held", "Terminal", "ReceiptRetirement", "Retention", "Content", "ExecutionAbsent")}))
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_CLAIM4_HELD_OK original-root-Prepared journal-empty sticky-claim original-loss-armed pending-record1 issuers-live no-recording no-submission no-output public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_CLAIM4_TERMINAL_OK " + terminal + " "
            + ("root-Invalidated" if physical else "root-Abandoned")
            + " journal-empty sticky-CPU-closed encoder0 groups0 submit0 original-local-loss-drained unpublished no-retry "
            + ("actual-device-loss" if physical else "physical-loss-undelivered") + " "
            + ("issuer-callback-facades-expired" if terminal == "drop" else "issuer-callback-facades-retained") + " public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_CLAIM4_RETIRE_OK initializer-expired original-prepared-native-receipts7-suppressed reservations-quotas-zero no-recording-no-completion public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_CLAIM4_RETENTION_OK " + selected
            + " caller-payload-root-closure-initializer-dropped claim-terminal-drained recording-quota-root-only public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_CLAIM4_CONTENT_OK " + selected + " " + policy
            + (" journal-count-quota-zero no-recording-no-output public-closed" if physical or logical
               else " journal-count-quota-zero no-output public-closed"))
        evidence.absent("BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_OK ", "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_COMMIT_OK ",
                        "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_ROLLBACK_OK ", "BINDLESS_RHI_GENERAL_CONTENT_PLAN5_INVALIDATED_OK ")
    elif group == "recording_terminal":
        require_fields(report, dict(originalGeneralFiniteRecordingTerminalKind=terminal,
            originalGeneralFiniteRecordingTerminalHeldPassed=True, originalGeneralFiniteRecordingTerminalPassed=True,
            originalGeneralFiniteRecordingTerminalRetentionPassed=True, originalGeneralFiniteContentPlanRollbackPassed=True))
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_RECORDING4_HELD_OK " + terminal
            + " root-Pending journal-retained initializer-retained native-scopes3 no-submit no-output "
            + ("issuer-custody-expired" if terminal == "drop" else "custody-live"))
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_RECORDING4_TERMINAL_OK " + terminal
            + " root-Abandoned journal-empty scopes-drained loss-suppressed initializer-expired original-reservations-quotas-zero unpublished no-retry public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_RECORDING4_RETENTION_OK " + selected
            + " caller-payload-root-closure-initializer-dropped terminal-drained recording-quota-root-only public-closed")
        content(evidence, "ROLLBACK", buffer_type, interval)
        evidence.absent("BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_OK ")
    elif group == "submission_terminal":
        completes = terminal == "reverse-scopes"
        require_fields(report, dict(originalGeneralFiniteSubmissionTerminalKind=terminal,
            originalGeneralFiniteSubmissionTerminalHeldPassed=True, originalGeneralFiniteSubmissionTerminalPassed=True,
            originalGeneralFiniteSubmissionTerminalRetentionPassed=True, originalGeneralFiniteSubmissionTerminalReceiptRetirementPassed=True,
            originalGeneralFiniteSubmissionVersion=2, originalGeneralFiniteOutputVersion=2,
            originalGeneralFiniteContentPlanCommitPassed=completes, originalGeneralFiniteContentPlanInvalidatedPassed=not completes))
        held = evidence.one("BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_HELD_OK " + terminal
            + " submit-invocation1 pending-record1 loss-armed physical-loss-undelivered scopes-retained output-unpublished ", "")
        if not held.endswith((" root-Pending journal-retained", " root-Invalidated journal-empty")):
            raise ValueError("FINITE-NATIVE-SUBMISSION-HELD")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_TERMINAL_OK " + terminal
            + " scopes-queue-drained loss-suppressed physical-loss-undelivered no-retry "
            + ("root-Committed numerical-output" if completes else "root-Invalidated output-unpublished") + " public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_RETENTION_OK " + selected
            + " caller-payload-root-closure-initializer-dropped terminal-drained recording-quota-root-only public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_SUBMISSION4_RETIRE_OK initializer-expired original-recording-submission-callback-receipts-reservations-quotas-zero public-closed")
        content(evidence, "COMMIT" if completes else "INVALIDATED", buffer_type, interval, terminal)
    elif group == "physical_loss":
        require_fields(report, dict(originalGeneralFiniteLossKind=terminal, physicalDeviceLossValidated=True,
            originalGeneralFiniteLossHeldPassed=True, originalGeneralFiniteLossTerminalPassed=True,
            originalGeneralFiniteLossRetirementPassed=True, originalGeneralFiniteLossRetentionPassed=True,
            originalGeneralFiniteContentPlanInvalidatedPassed=True))
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_LOSS4_HELD_OK " + terminal
            + " pending-record1 original-loss-armed physical-loss-undelivered owners-retained no-completion no-output")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_LOSS4_TERMINAL_OK " + terminal
            + " actual-device-destroy original-loss-delivered genuine-local-drain root-Invalidated journal-empty no-completion unpublished no-retry public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_LOSS4_RETIRE_OK initializer-expired original-receipts-reservations-quotas-zero actual-loss-drained public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_LOSS4_RETENTION_OK " + selected
            + " caller-payload-root-closure-initializer-dropped actual-loss-drained recording-quota-root-only public-closed")
        content(evidence, "INVALIDATED", buffer_type, interval, terminal)
    elif group == "map_terminal":
        physical = terminal in ("throw-before", "throw-after", "scope-registration", "unmap-throw", "pending-loss", "published-loss")
        require_fields(report, dict(originalGeneralFiniteMapTerminalKind=terminal, physicalDeviceLossValidated=physical,
            originalGeneralFiniteSubmissionVersion=2, originalGeneralFiniteOutputVersion=2,
            **{"originalGeneralFiniteMap" + name + "Passed": True for name in
               ("Output", "CopyHeld", "Drain", "UpstreamRetirement", "Retention")}))
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_MAP4_COPY_HELD_OK " + terminal
            + " original-upstream-completed downstream-copy-scopes3-held root-Pending unpublished queue-callback-retained public-closed")
        if not emits_numerical:
            require_fields(report, dict(originalGeneralFiniteMapHeldPassed=True))
            held = evidence.one("BINDLESS_RHI_GENERAL_FINITE_MAP4_HELD_OK " + terminal
                + " native-map-invoked pending-record1 original-scopes-held unpublished owners-retained ", "")
            if not held.endswith((" root-Pending", " root-Invalidated")):
                raise ValueError("FINITE-NATIVE-MAP-HELD")
        loss = "actual-device-loss" if physical else "no-device-loss"
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_MAP4_OUTPUT_OK " + terminal
            + " Snapshot2 Output2 original-upstream-completed original-copy-map-drained "
            + ("numerical-published" if emits_numerical else "invalidated-unpublished-no-retry")
            + " " + loss + " snapshot-reuse-refused public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_OUTPUT_MAP1_OK " + terminal + " original-copy-map genuine-drain "
            + ("committed-numerical-published" if emits_numerical else "invalidated-unpublished-no-retry")
            + " " + loss + " result-quota-zero public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_MAP4_UPSTREAM_RETIRE_OK original-Submission2 completed-committed recording-scopes3 submission-scopes3 queue1 original-receipts-reservations-quotas-zero public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_MAP4_RETENTION_OK " + selected
            + " caller-payload-root-closure-initializer-dropped original-upstream-and-output-drained recording-quota-root-only public-closed")
        if physical:
            evidence.one("BINDLESS_RHI_GENERAL_FINITE_MAP4_UPSTREAM_LOSS_OK " + selected + " " + terminal
                + " original-upstream-committed physical-controls-invalidated journal-count-quota-zero published-bytes-policy-checked public-closed")
            evidence.absent("BINDLESS_RHI_GENERAL_CONTENT_PLAN5_COMMIT_OK ")
        else:
            content(evidence, "COMMIT", buffer_type, interval)
            evidence.absent("BINDLESS_RHI_GENERAL_FINITE_MAP4_UPSTREAM_LOSS_OK ")
    else:
        require_fields(report, dict(originalGeneralFinitePendingNativeValidationPassed=True,
            originalGeneralFiniteContentPlanCommitPassed=True, originalGeneralFiniteContentPlanRollbackPassed=False,
            originalGeneralFiniteNativeRecordingPassed=True, originalGeneralFiniteNativeRecordingRetentionPassed=True,
            originalGeneralFiniteNativeRecordingVersion=7, originalGeneralFiniteNativeSubmissionAvailable=True,
            originalGeneralFiniteNativeClosureRetentionPassed=True, originalGeneralFiniteNativeClosureContractVersion=9,
            originalGeneralFiniteInitializedBufferCustodyVersion=2, originalGeneralFiniteNativeFreezePassed=True,
            originalGeneralFiniteNativePayloadRetentionPassed=True, originalGeneralFiniteNativeFreezeVersion=7,
            originalGeneralFiniteNativePayloadVersion=7, originalGeneralFiniteNativePayloadCommands=10,
            originalGeneralFiniteFrozenDependencies=4, originalGeneralFiniteColdRecordingExecutionAvailable=True,
            originalGeneralFiniteSubmissionPassed=True, originalGeneralFiniteSubmissionVersion=2,
            originalGeneralFiniteOutputVersion=2, originalGeneralFiniteExecutionRetentionPassed=True))
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_OK Cold7 Command7 Callback7 Issuer7 Lifecycle4 ContentPlan5 Submission2 Output2 commands10 recordingScopes3 submissionScopes3 queue1 ",
            " exact-less-exhausted original-seal-journal full-backing caller-drop forged-native-mutation strict-public hot0-query0 real-completed committed numerical-readback quotas-zero public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FINITE_EXECUTION7_RETENTION_OK " + selected
            + " caller-payload-root-closure-initializer-dropped original-submitted-output-mapped recording-quota-root-only public-closed")
        evidence.one("BINDLESS_RHI_GENERAL_FROZEN7_OK Closure9 Freeze7 Payload7 commands10 dependencies4 initial1 final3 complete-original-offsets ",
            " exact-less-exhausted original-root-journal hot0-query0 no-execution-authority")
        content(evidence, "COMMIT", buffer_type, interval)
    # Comparison intentionally excludes timing, platform diagnostics and authored
    # capacity byte counts; it includes the complete reported semantic profile.
    return {key: value for key, value in report.items() if key.startswith("originalGeneral")
            or key in ("scenario", "nativeShaderExecutionValidated", "physicalDeviceLossValidated",
                       "nativeSyncAvailable", "publicBindlessExecutionAvailable")}
