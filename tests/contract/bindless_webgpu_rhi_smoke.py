"""Browser validation of bounded WebGPU bindless RHI execution."""
import argparse
import functools
import http.server
import json
import pathlib
import re
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests/contract"))
from bindless_codegen import read_bindless_publication
from browser_contract_runner import BrowserProgressJournal, create_browser_http_server, retire_browser_process, wait_for_browser_result
from bindless_finite_lifecycle_evidence import NativeProgressEvidence, inspect_finite_report
from implicit_sampling_reference import load_reference_contract, validate_reference_result
from pbr_implicit_sampling_reference import load_pbr_reference_contract, validate_pbr_reference_result
from terrain27_implicit_sampling_reference import load_terrain27_reference_contract, validate_terrain27_reference_result


def validate_wocc_ibl_report(result, input_directory):
    """Independent host check; a browser success flag alone grants nothing."""
    sys.path.insert(0, str(ROOT / "tests/tools"))
    from bindless_graphics_readback_oracles import OrdinaryWoccIblOracle
    if (result.get("success") is not True or result.get("scenario") != "original-bindless-wocc-ibl"
            or result.get("publicBindlessExecutionAvailable") is not False
            or result.get("directTraditionalParity") is not False
            or result.get("productionReady") is not False
            or result.get("diagnostics") or result.get("nativeErrors")):
        raise ValueError("WOCC IBL browser scope/errors")
    for key in ("executionPassed", "referencePassed", "numericPassed", "bitwiseParityPassed", "retirementPassed"):
        if result.get(key) is not True:
            raise ValueError("WOCC IBL missing gate: " + key)
    progress = NativeProgressEvidence(result).messages
    details = " execution13 Content9 read18 control1 lifecycle12 same-root-journal genuine-draw-queue-map hot0 publication-gated budgets-returned"
    for scenario in range(11):
        for fault in (0, 1):
            marker = f"BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_WOCC_IBL_EXECUTION_OK scenario{scenario} fault{fault}" + details
            if progress.count(marker) != 1:
                raise ValueError("WOCC IBL missing/duplicate lifecycle: " + marker)
    if progress.count("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_WOCC_IBL_REFERENCE_EXECUTION_OK scenario0 fault0" + details) != 1:
        raise ValueError("WOCC IBL missing independent reference")
    for role, fault, count in (("WOCC_IBL_REFERENCE", 0, 1), ("WOCC_IBL_REFERENCE", 1, 0),
                               ("WOCC_IBL", 0, 2), ("WOCC_IBL", 1, 2)):
        marker = (f"BINDLESS_WEBGPU_{role}_NUMERIC_OK fault{fault} "
                  "lanes424 unfiltered80-exact reference-bitwise color2x2")
        if progress.count(marker) != count:
            raise ValueError("WOCC IBL missing/duplicate numeric receipt: " + marker)
    if progress.count("BINDLESS_WEBGPU_WOCC_IBL_RETIRE_OK frozen655328 original-owners-expired budgets-zero scopes3-drained") != 1:
        raise ValueError("WOCC IBL missing retirement")
    domains = [message for message in progress if message.startswith("BINDLESS_WEBGPU_ROOT_CPU_DOMAINS_OK ")]
    root_cpu = 65536 + (input_directory / "horizon.rgba16f").stat().st_size
    expected_domains = (f"BINDLESS_WEBGPU_ROOT_CPU_DOMAINS_OK graph{root_cpu} recording{root_cpu} "
                        f"submission{root_cpu} callback65536 mismatch-refused no-publication")
    if len(domains) != 23 or any(message != expected_domains for message in domains):
        raise ValueError("WOCC IBL independent CPU domains")
    reads = result.get("readbacks", [])
    if len(reads) != 5 or [(sum(r.get("reference") is ref and r.get("fault") is fault for r in reads))
            for ref, fault in ((True, False), (True, True), (False, False), (False, True))] != [1, 0, 2, 2]:
        raise ValueError("WOCC IBL readback roles")
    numeric = []
    for read in reads:
        words = read.get("words", [])
        if len(words) != 424 or any(type(word) is not int or not 0 <= word <= 0xffffffff for word in words):
            raise ValueError("WOCC IBL numeric footprint")
        numeric.append(struct.pack("<424I", *words))
    reference = numeric[next(i for i, read in enumerate(reads) if read["reference"])]
    oracle = OrdinaryWoccIblOracle(input_directory)
    anchors = oracle.verify_direct_reference(reference)
    for data in numeric:
        oracle.verify_direct_parity(data, reference)
    return dict(anchors=anchors, inputHashes=oracle.input_hashes, words=424,
                readbacks=5, lifecycleFlows=22, ownReferenceBitwise=True,
                directTraditionalParity=False)


def validate_finite_graphics_root_report(result, buffer_type, whole, dynamic=False, terminal="abandon", shader_content=False, native_custody=False, dependency_freeze=False, native_payload=False, native_cold=False, native_command=False, native_callback=False):
    if whole and dynamic or terminal not in ("abandon", "drop"):
        return False
    expected = 1
    try:
        progress = NativeProgressEvidence(result).messages
    except (ValueError, TypeError, AttributeError):
        return False
    roots=[m for m in progress if re.fullmatch(
        r"BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROOT_OK completed1 attachment1 cells1 bind1 draw1 "
        r"attachmentSteps2 commands42 projections[1-9][0-9]* finalCells5 stateSteps17 rootBytes[1-9][0-9]* "
        r"exact-less-exhausted same-original-Stream-Usage-Dependency pending-rollback content-unchanged hot0-query0 native-closed",m)]
    retired=progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROOT_RETIRE_OK original-journal-rollback transaction-count0 quota0 no-native-draw-or-completion")
    pending=progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_PENDING_OK sampled-completed-stamp-current attachment-private-proposal original-journal no-native-completion")
    shader_marker = "BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_INITIALIZED_OK " + buffer_type + " " + ("whole" if whole else "dynamic" if dynamic else "static") + " original-Staging4 Source5 Fragment frozen-backing scopes3 queue1 callbacks-drained hot0-query0 no-draw"
    shader_retire = "BINDLESS_RHI_GENERAL_GRAPHICS_SHADER_BUFFER_RETIRE_OK original-initializer-callbacks-drained source-quotas-zero no-native-draw"
    shader_matches = (result.get("shaderBufferInitialized", False) is shader_content
        and result.get("shaderBufferRetired", False) is shader_content
        and progress.count(shader_marker) == (1 if shader_content else 0)
        and progress.count(shader_retire) == (1 if shader_content else 0))
    if shader_content:
        shader_matches = shader_matches and progress.count("BINDLESS_RHI_GENERAL_FINITE_CONTENT1_INITIALIZED_OK complete-backing Staging4 original-write scopes3 queue1 authentic-ready pending-refused execution-closed") == 1
    custody_markers = [m for m in progress if re.fullmatch(
        r"BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_OK sources10 resources8 pipeline1 descriptor1 custody2 "
        r"complete-backing[1-9][0-9]* closureBytes[1-9][0-9]* retainedBytes[1-9][0-9]* owners[1-9][0-9]* "
        r"exact-less-exhausted no-root-journal-mutation hot0-query0 freeze-native-closed",m)]
    cold_retained=progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_RETAIN_OK same-original-Prepared-root payload-freeze-closure-initializer-seal-reservation-caller-drop sealed-CPU-refused no-record-or-completion")
    cold_retired=progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_RETIRE_OK same-original-Abandoned-root original-seal-drop reservations0 callbacks0 quota-baseline no-record-or-completion")
    custody_retire = cold_retired if native_cold else progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_RETIRE_OK " + (
        "same-original-Pending-root Freeze8-child-drop closure-quota-baseline no-draw-or-completion" if dependency_freeze else
        "same-original-Prepared-root initializer-caller-drop native-owners-retained closure-quota-baseline no-draw-or-completion"))
    custody_matches = ((not native_custody or shader_content)
        and result.get("nativeCustodyValidated", False) is native_custody
        and len(custody_markers) == (1 if native_custody else 0)
        and custody_retire == (1 if native_custody else 0))
    freeze_markers = [m for m in progress if re.fullmatch(
        r"BINDLESS_RHI_GENERAL_GRAPHICS_FREEZE8_OK Closure10 ContentPlan6 commands14 dependencies6 initial2 final4 "
        r"endpoints14 cells7 passes24 visits168 pairs21 freezeBytes[1-9][0-9]* owners[1-9][0-9]* "
        r"exact-less-exhausted hostile-clauses-refused same-Prepared-root hot0-query0 payload-native-closed",m)]
    freeze_retire = cold_retired if native_cold else progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_FREEZE8_RETIRE_OK same-original-Pending-root stale-candidates-refused caller-drop quota-baseline no-draw-or-completion")
    freeze_retained = cold_retained if native_cold else progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_CLOSURE10_RETAIN_OK same-original-Prepared-root initializer-caller-drop Freeze8-retained no-draw-or-completion")
    freeze_matches = ((not dependency_freeze or native_custody)
        and result.get("nativeDependencyFreezeValidated", False) is dependency_freeze
        and len(freeze_markers)==(1 if dependency_freeze else 0)
        and freeze_retire==(1 if dependency_freeze else 0) and freeze_retained==(1 if dependency_freeze else 0))
    payload_markers=[m for m in progress if re.fullmatch(
        r"BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_OK Freeze8 Closure10 ContentPlan6 commands14 calls9 render1 compute0 "
        r"pipeline1 group1 draw1 dispatch0 dependencies6 implicit8 viewport1 scissor1 attachment1 dynamicOffsets"
        + ("0" if whole else "1") + r" owners[1-9][0-9]* exact-less-exhausted original-pass-group-attachment-mutated "
        r"same-Prepared-root hot0-query0 cold-record-execution-closed",m)]
    payload_retained=cold_retained if native_cold else progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_RETAIN_OK same-original-Prepared-root freeze-closure-initializer-caller-drop original-native-inputs-retained no-record-or-completion")
    payload_retired=cold_retired if native_cold else progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_PAYLOAD8_RETIRE_OK same-original-Pending-root stale-inputs-refused payload-child-drop no-record-or-completion")
    payload_stale=progress.count("BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_REJECT_OK " + ("graphics-Cold-abandoned-root" if native_cold else "graphics-Pending-root") + " genuine-frozen-native-input original-root-stale counts-cleared hot0 execution-closed")
    payload_frozen=[m for m in progress if re.fullmatch(
        r"BINDLESS_RHI_TYPED_NATIVE_PAYLOAD_FROZEN_OK general-finite-graphics10 commands14 calls9 render1 compute0 draws1 "
        r"dispatches0 dependencies6 colors1 bytes[1-9][0-9]* original-native-bindings exact-pass-draw-bound "
        r"exact-less-exhausted forged-native-mutated-reject hot0-query0 execution-closed",m)]
    payload_matches=((not native_payload or dependency_freeze)
        and result.get("nativePayloadInputsValidated",False) is native_payload
        and all(count==(1 if native_payload else 0) for count in
            (len(payload_markers),payload_retained,payload_retired,payload_stale,len(payload_frozen))))
    cold_markers=[m for m in progress if re.fullmatch(
        r"BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_OK Payload8 Lifecycle5 ContentPlan6 commands14 calls9 render1 compute0 "
        r"callbacks8 owners[1-9][0-9]* coldBytes[1-9][0-9]* exact-less-exhausted forged-reservation-native-mutated "
        r"legacy-refused same-Prepared-root sealed-CPU-refused hot0-query0 no-encoder-or-execution",m)]
    cold_candidate=progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_CANDIDATE_RETIRE_OK before-any-proposal original-factory transaction0 quota0 generation-unchanged CPU-baseline-separate")
    cold_matches=((not native_cold or native_payload)
        and result.get("nativeColdAdmissionValidated",False) is native_cold
        and result.get("coldCandidateIndependentOfCpuBaseline",False) is native_cold
        and all(count==(1 if native_cold else 0) for count in (len(cold_markers),cold_retained,cold_retired,cold_candidate)))
    command_markers=[m for m in progress if re.fullmatch(
        r"BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_OK Cold8 Payload8 Lifecycle5 ContentPlan6 commands14 calls9 command1 "
        r"owners[1-9][0-9]* owner-add3 legacy-refused short-exhausted-recover duplicate-peer-forged-native-mutated-refused "
        r"sealed-CPU-refused hot0-query0 no-encoder-callback-or-execution",m)]
    command_retained=progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_RETAIN_OK same-original-Prepared-root original-Cold8-caller-drop payload-freeze-closure-initializer-seal-reservation-retained sealed-CPU-refused no-encoder-or-completion")
    command_retired=progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_RETIRE_OK same-original-Abandoned-root original-shell-Cold8-seal-drop reservations0 callbacks0 quota-baseline no-record-or-completion")
    command_matches=((not native_command or native_cold)
        and result.get("nativeCommandConnectionValidated",False) is native_command
        and all(count==(1 if native_command else 0) for count in (len(command_markers),command_retained,command_retired)))
    callback_markers=[m for m in progress if re.fullmatch(
        r"BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_OK Command8 Cold8 Lifecycle5 ContentPlan6 callbacks8 native7 local1 shared1 "
        r"owners[1-9][0-9]* owner-add11 legacy-short-enlarged-exhausted-forged-native-mutated-refused "
        r"original-subscription-no-new-registration hot0-query0 no-issuer-record-execution",m)]
    callback_retained=progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_RETAIN_OK same-original-Prepared-root Command8-Cold8-caller-drop original-observer-seal-reservation-initializers-retained pending1 armed1 no-native-execution")
    callback_retired=progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_RETIRE_OK same-original-Abandoned-root original-observer-suppressed sticky-claim reservations0 callbacks0 quota-baseline no-completion-or-public-execution")
    callback_matches=((not native_callback or native_command)
        and result.get("nativeCallbackObserverValidated",False) is native_callback
        and all(count==(1 if native_callback else 0) for count in (len(callback_markers),callback_retained,callback_retired)))
    return (result.get("success") is True and shader_matches and custody_matches and freeze_matches and payload_matches and cold_matches and command_matches and callback_matches
        and result.get("scenario") == ("original-general-finite-graphics-native-callback" if native_callback else "original-general-finite-graphics-native-command" if native_command else "original-general-finite-graphics-native-cold" if native_cold else "original-general-finite-graphics-native-payload" if native_payload else "original-general-finite-graphics-dependency-freeze" if dependency_freeze else "original-general-finite-graphics-native-custody" if native_custody else "original-general-finite-graphics-shader-buffer" if shader_content else "original-general-finite-graphics-content-root")
        and result.get("bufferType") == buffer_type
        and result.get("rangeKind") == ("whole" if whole else "dynamic" if dynamic else "static")
        and result.get("rollbackKind") == terminal
        and type(result.get("rootCount")) is int and result["rootCount"] == expected
        and type(result.get("rootRetirementCount")) is int and result["rootRetirementCount"] == expected
        and type(result.get("pendingCount")) is int and result["pendingCount"] == expected
        and len(roots)==expected and retired==expected and pending==expected
        and progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT6_ROLLBACK_OK " + terminal
            + " stale-observations-refused original-Inputs-recaptured sampled-stamp-current attachment-uninitialized generation-one-exhausted") == expected
        and "BINDLESS_RHI_GENERAL_SAMPLED_INITIALIZER_OK same-device upload20 mips2 original-scopes-drained recorded-submit-completed committed-root" in progress
        and "BINDLESS_RHI_GENERAL_GRAPHICS_CONTENT_SOURCES_RETIRE_OK original-upload-scopes-callbacks-content-domains-quotas-zero attachment-never-completed no-root-draw-output" in progress
        and progress.count("BINDLESS_RHI_GENERAL_GRAPHICS_USAGE6_DEPENDENCY6_RETIRE_OK original-attachment-state-stream-quotas-zero native-closed")==expected
        and result.get("initializedSourcesRetired") is True
        and result.get("usageDependencyRetired") is True
        and result.get("nativeErrors") == [] and result.get("diagnostics") == []
        and result.get("progressTransport", {}).get("complete") is True
        and all(result.get(field) is False for field in (
            "nativeDrawReadbackValidated", "finiteGraphicsValidated", "fullFiniteExecutionValidated",
            "finiteLifecycle4Validated", "publicBindlessExecutionAvailable", "nativeSyncPromoted",
            "platformValidated", "productionReady")))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--browser", required=True)
    parser.add_argument("--runner", type=pathlib.Path, required=True)
    parser.add_argument("--original-bindless-root-preparation", action="store_true")
    parser.add_argument("--original-root-geometry-execution-only", action="store_true")
    parser.add_argument("--original-root-sampled-execution-only", action="store_true")
    parser.add_argument("--original-root-array-execution-only", action="store_true")
    parser.add_argument("--original-root-composed-execution-only", action="store_true")
    parser.add_argument("--original-root-matrix-execution-only", action="store_true")
    parser.add_argument("--original-root-skinning256", action="store_true")
    parser.add_argument("--original-root-instance64", action="store_true")
    parser.add_argument("--original-root-pbr-material", action="store_true")
    parser.add_argument("--original-root-terrain27", action="store_true")
    parser.add_argument("--original-root-wocc-ibl", action="store_true")
    parser.add_argument("--original-root-push-execution-only", action="store_true")
    parser.add_argument("--original-root-storage-execution-only", action="store_true")
    parser.add_argument("--original-root-implicit-parity", action="store_true")
    parser.add_argument("--wasm-baseline-only", action="store_true",
        help="Use V8's baseline Wasm compiler for the large original Root correctness fixture; not performance evidence")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--disable-indirect-first-instance", action="store_true")
    parser.add_argument("--typed-execution-scenario", action="store_true")
    parser.add_argument("--stream-geometry-scenario", action="store_true")
    parser.add_argument("--shader-usage-values-scenario", action="store_true")
    parser.add_argument("--general-resource-projection-scenario", action="store_true")
    parser.add_argument("--general-finite-graphics-content-root", action="store_true")
    parser.add_argument("--general-graphics-shader-content", action="store_true")
    parser.add_argument("--general-graphics-native-custody", action="store_true")
    parser.add_argument("--general-graphics-dependency-freeze", action="store_true")
    parser.add_argument("--general-graphics-native-payload", action="store_true")
    parser.add_argument("--general-graphics-native-cold", action="store_true")
    parser.add_argument("--general-graphics-native-command", action="store_true")
    parser.add_argument("--general-graphics-native-callback", action="store_true")
    parser.add_argument("--general-graphics-root-terminal", choices=("abandon", "drop"))
    parser.add_argument("--general-finite-binding", action="store_true")
    parser.add_argument("--general-finite-source", action="store_true")
    parser.add_argument("--general-finite-inputs", action="store_true")
    parser.add_argument("--general-finite-consumption", action="store_true")
    parser.add_argument("--general-finite-state-stream", action="store_true")
    parser.add_argument("--general-finite-usage", action="store_true")
    parser.add_argument("--general-finite-content", action="store_true")
    parser.add_argument("--general-finite-content-plan", action="store_true")
    parser.add_argument("--general-finite-native-closure", action="store_true")
    parser.add_argument("--general-finite-native-payload", action="store_true")
    parser.add_argument("--general-finite-native-recording", action="store_true")
    parser.add_argument("--general-finite-execution", action="store_true")
    parser.add_argument("--general-finite-lifecycle", action="store_true")
    parser.add_argument("--general-post-suppression-failure", choices=("commit", "publication"))
    parser.add_argument("--general-claim-terminal", choices=("cancel", "drop", "retired", "shutdown", "physical-loss"))
    parser.add_argument("--general-content-plan-dynamic", action="store_true")
    parser.add_argument("--general-content-buffer-type", choices=("uniform", "readonly-storage"))
    parser.add_argument("--general-content-initialization-terminal",
                        choices=("cancel-before", "cancel-pending", "native-error", "pending-loss",
                                 "cancel-drop-pending", "pending-drop-loss"))
    parser.add_argument("--general-source-whole", action="store_true")
    parser.add_argument("--general-source-terminal", choices=("reset", "retired", "device-loss"))
    parser.add_argument("--general-recording-terminal", choices=("cancel", "drop", "native-error", "reverse-scopes"))
    parser.add_argument("--general-submission-terminal", choices=("cancel", "retired", "shutdown", "throw-before", "throw-after", "native-error", "reverse-scopes", "partial-scope"))
    parser.add_argument("--general-physical-loss", choices=("queue-uncertain", "recording-shutdown", "partial-push", "recording-exception"))
    parser.add_argument("--general-map-terminal", choices=("cancel", "native-error", "read-error", "scope-native-error",
                        "throw-before", "throw-after", "scope-registration", "unmap-throw", "pending-loss", "reverse-scopes", "published-loss"))
    parser.add_argument("--completed-shader-texture-input-scenario", action="store_true")
    parser.add_argument("--stream-geometry-lifecycle-scenario",
                        choices=("recording-cancel", "submission-loss", "map-loss"))
    parser.add_argument("--typed-target-finite-loss-scenario", action="store_true")
    parser.add_argument("--canonical-upload-scope-uncertainty", choices=("buffer", "texture"))
    parser.add_argument("--timeout-seconds", type=int, default=60,
                        help="Explicit wall-clock test budget, including original cold quota searches")
    parser.add_argument("--general-graphics-recording-terminal", choices=("cancel","drop","native-error","reverse-scopes","retired","shutdown"))
    parser.add_argument("--general-graphics-native-submission",action="store_true")
    parser.add_argument("--general-graphics-output-readback",action="store_true")
    parser.add_argument("--general-graphics-output-terminal",choices=("cancel","native-error","read-error",
        "scope-native-error","throw-before","throw-after","scope-registration","unmap-throw","pending-loss",
        "reverse-scopes","published-loss","drop"))
    parser.add_argument("--general-graphics-submission-terminal",
        choices=("cancel","retired","shutdown","throw-before","throw-after","native-error","reverse-scopes","partial-scope"))
    parser.add_argument("--general-graphics-physical-loss",
        choices=("queue-uncertain","recording-shutdown","partial-push","recording-exception"))
    args = parser.parse_args()
    if args.original_root_wocc_ibl and (not args.original_bindless_root_preparation or any((
            args.original_root_geometry_execution_only, args.original_root_sampled_execution_only, args.original_root_array_execution_only,
            args.original_root_composed_execution_only, args.original_root_matrix_execution_only,
            args.original_root_push_execution_only, args.original_root_storage_execution_only))):
        parser.error("--original-root-wocc-ibl requires the exclusive original Root IBL cohort")
    if args.original_root_implicit_parity and not (args.original_bindless_root_preparation
            and (args.original_root_sampled_execution_only or args.original_root_array_execution_only)):
        parser.error("--original-root-implicit-parity requires one scoped original Root sampled/array execution cohort")
    if args.original_root_geometry_execution_only and not args.original_bindless_root_preparation:
        parser.error("--original-root-geometry-execution-only requires the original Root fixture")
    if args.original_root_sampled_execution_only and not args.original_bindless_root_preparation:
        parser.error("--original-root-sampled-execution-only requires the original Root fixture")
    if args.original_root_array_execution_only and not args.original_bindless_root_preparation:
        parser.error("--original-root-array-execution-only requires the original Root fixture")
    if args.original_root_storage_execution_only and not args.original_bindless_root_preparation:
        parser.error("--original-root-storage-execution-only requires the original Root fixture")
    if args.original_root_composed_execution_only and not args.original_bindless_root_preparation:
        parser.error("--original-root-composed-execution-only requires the original Root fixture")
    if args.original_root_matrix_execution_only and not args.original_bindless_root_preparation:
        parser.error("--original-root-matrix-execution-only requires the original Root fixture")
    if args.original_root_skinning256 and not args.original_root_matrix_execution_only:
        parser.error("--original-root-skinning256 requires the original Root matrix execution cohort")
    if args.original_root_instance64 and (not args.original_root_matrix_execution_only or args.original_root_skinning256):
        parser.error("--original-root-instance64 requires the original Root matrix cohort without skinning256")
    if args.original_root_pbr_material and (not args.original_root_matrix_execution_only or args.original_root_skinning256 or args.original_root_instance64):
        parser.error("--original-root-pbr-material requires the original Root matrix cohort without other data models")
    if args.original_root_terrain27 and (not args.original_root_matrix_execution_only or args.original_root_skinning256 or args.original_root_instance64 or args.original_root_pbr_material):
        parser.error("--original-root-terrain27 requires the original Root matrix cohort without other data models")
    if args.original_root_push_execution_only and not args.original_bindless_root_preparation:
        parser.error("--original-root-push-execution-only requires the original Root fixture")
    if sum((args.original_root_geometry_execution_only,args.original_root_push_execution_only,args.original_root_array_execution_only,args.original_root_sampled_execution_only,args.original_root_composed_execution_only,args.original_root_storage_execution_only,args.original_root_matrix_execution_only))>1:
        parser.error("choose one original Root execution domain")
    if args.wasm_baseline_only and not args.original_bindless_root_preparation:
        parser.error("--wasm-baseline-only requires the original Root correctness fixture")
    if args.general_graphics_recording_terminal and not (args.general_finite_graphics_content_root and args.general_graphics_native_callback):
        parser.error("Graphics recording requires the complete original Callback8 chain")
    if args.general_graphics_native_submission and args.general_graphics_recording_terminal!="cancel":
        parser.error("The first Graphics submission pilot requires the complete recording chain and success terminal")
    if args.general_graphics_submission_terminal and not args.general_graphics_native_submission:
        parser.error("Graphics submission terminal requires explicit Graphics8/Submission3")
    if args.general_graphics_physical_loss and (not args.general_graphics_native_submission
            or args.general_graphics_submission_terminal or args.general_graphics_output_readback):
        parser.error("Graphics physical loss requires the independent Graphics8/Submission3 chain without another terminal or readback")
    if args.general_graphics_output_readback and (not args.general_graphics_native_submission
            or args.general_graphics_submission_terminal or args.general_map_terminal):
        parser.error("Graphics Output3 readback requires the complete success submission chain without terminal injection")
    if args.general_graphics_output_terminal and not args.general_graphics_output_readback:
        parser.error("Graphics Output3 terminal requires explicit original attachment readback")
    if args.general_finite_graphics_content_root and any((args.general_resource_projection_scenario,
            args.general_finite_binding,args.general_finite_source,args.general_finite_inputs,
            args.general_finite_consumption,args.general_finite_state_stream,args.general_finite_usage,
            args.general_finite_content,args.general_finite_content_plan,args.general_finite_native_closure,
            args.general_finite_native_payload,args.general_finite_native_recording,args.general_finite_execution,
            args.general_finite_lifecycle,args.general_source_terminal,args.general_content_initialization_terminal,
            args.general_claim_terminal,args.general_recording_terminal,args.general_submission_terminal,
            args.general_physical_loss,args.general_map_terminal,args.general_post_suppression_failure,
            args.completed_shader_texture_input_scenario,
            args.shader_usage_values_scenario,args.stream_geometry_scenario,args.stream_geometry_lifecycle_scenario,
            args.typed_execution_scenario,args.typed_target_finite_loss_scenario,args.canonical_upload_scope_uncertainty)):
        parser.error("finite Graphics ContentPlan6 root requires its independent initialized Graphics workload")
    if sum((args.general_finite_content_plan, args.general_finite_native_closure, args.general_finite_native_payload, args.general_finite_native_recording, args.general_finite_execution)) > 1:
        parser.error("ContentPlan5, Closure9, Payload7 and Recording7 are independent scenarios")
    finite_plan_scenario = args.general_finite_content_plan or args.general_finite_native_closure or args.general_finite_native_payload or args.general_finite_native_recording or args.general_finite_execution or args.general_finite_lifecycle
    if args.general_finite_lifecycle and (sum(bool(value) for value in (args.general_claim_terminal, args.general_recording_terminal, args.general_submission_terminal, args.general_physical_loss, args.general_map_terminal, args.general_post_suppression_failure)) != 1
            or args.general_finite_execution or args.general_finite_native_recording
            or args.general_finite_native_payload or args.general_finite_native_closure or args.general_finite_content_plan):
        parser.error("finite Lifecycle4 requires exactly one independent claim, recording, submission, physical-loss, map or post-suppression terminal scenario")
    if finite_plan_scenario and (not args.general_resource_projection_scenario
            or args.general_finite_binding or args.general_finite_source or args.general_finite_content
            or args.general_finite_inputs or args.general_finite_consumption or args.general_finite_state_stream
            or args.general_finite_usage or args.general_source_terminal or args.general_content_initialization_terminal
            or ((args.general_claim_terminal or args.general_recording_terminal or args.general_submission_terminal or args.general_physical_loss or args.general_map_terminal or args.general_post_suppression_failure) and not args.general_finite_lifecycle)):
        parser.error("ContentPlan5 requires its independent completed-upload General scenario")
    if args.general_graphics_root_terminal and not args.general_finite_graphics_content_root:
        parser.error("Graphics root terminal requires its independent ContentPlan6 scenario")
    if args.general_graphics_shader_content and not args.general_finite_graphics_content_root:
        parser.error("Graphics shader content requires its independent ContentPlan6 scenario")
    if args.general_graphics_native_custody and not args.general_graphics_shader_content:
        parser.error("Graphics native custody requires its independent initialized shader buffer scenario")
    if args.general_graphics_dependency_freeze and not args.general_graphics_native_custody:
        parser.error("Graphics dependency freeze requires its independent Closure10 custody scenario")
    if args.general_graphics_native_payload and not args.general_graphics_dependency_freeze:
        parser.error("Graphics native payload requires its independent Freeze8 scenario")
    if args.general_graphics_native_cold and not args.general_graphics_native_payload:
        parser.error("Graphics native cold requires its independent Payload8 scenario")
    if args.general_graphics_native_command and not args.general_graphics_native_cold:
        parser.error("Graphics native command requires its independent Cold8 scenario")
    if args.general_graphics_native_callback and not args.general_graphics_native_command:
        parser.error("Graphics native callback requires its independent Command8 scenario")
    if args.general_content_plan_dynamic and (not (finite_plan_scenario or args.general_finite_graphics_content_root) or args.general_source_whole):
        parser.error("ContentPlan5 dynamic requires its finite layout")
    if args.general_post_suppression_failure and not args.general_finite_lifecycle:
        parser.error("post-suppression failure requires its independent finite Lifecycle4 scenario")
    if args.general_claim_terminal and not args.general_finite_lifecycle:
        parser.error("claim terminal requires its independent finite Lifecycle4 scenario")
    if args.general_finite_usage and not args.general_finite_state_stream:
        parser.error("--general-finite-usage requires --general-finite-state-stream")
    if args.general_finite_usage and args.general_content_initialization_terminal:
        parser.error("General finite usage requires a Ready content scenario")
    if args.general_finite_state_stream and not args.general_finite_consumption:
        parser.error("--general-finite-state-stream requires --general-finite-consumption")
    if args.general_finite_consumption and not args.general_finite_inputs:
        parser.error("--general-finite-consumption requires --general-finite-inputs")
    if args.general_finite_inputs and not args.general_finite_source:
        parser.error("general finite inputs requires the independent Source5 scenario")
    if args.general_content_buffer_type and not (args.general_finite_content or finite_plan_scenario or args.general_finite_graphics_content_root):
        parser.error("General content buffer type requires its independent finite content scenario")
    if args.general_content_initialization_terminal and (not args.general_finite_content or args.general_source_terminal):
        parser.error("General initialization terminal requires its independent finite content scenario")
    if args.general_content_initialization_terminal in ("pending-loss", "pending-drop-loss") and not args.general_source_whole:
        parser.error("Pending initialization loss requires its independent whole Source5 layout")
    composed_content_usage = (args.general_finite_usage and args.general_finite_source
                              and args.general_finite_content and not args.general_finite_binding)
    if (args.general_finite_binding or args.general_finite_source or args.general_finite_content) and (not args.general_resource_projection_scenario
            or args.general_recording_terminal or args.general_submission_terminal
            or args.general_physical_loss or args.general_map_terminal
            or (sum((args.general_finite_binding, args.general_finite_source, args.general_finite_content)) > 1
                and not composed_content_usage)):
        parser.error("general finite binding requires its independent General scenario")
    if args.general_source_terminal and not (args.general_finite_source or args.general_finite_content):
        parser.error("General Source terminal requires its independent finite Source scenario")
    if args.general_source_whole and not (args.general_finite_source or args.general_finite_content or finite_plan_scenario or args.general_finite_graphics_content_root):
        parser.error("General whole Source requires its independent Source scenario")
    if args.general_map_terminal and (not args.general_resource_projection_scenario or args.general_recording_terminal
            or args.general_submission_terminal or args.general_physical_loss):
        parser.error("general map terminal requires its own General scenario")
    if args.general_physical_loss and (not args.general_resource_projection_scenario or args.general_recording_terminal or args.general_submission_terminal):
        parser.error("general physical loss requires its own General scenario")
    if args.general_submission_terminal and (not args.general_resource_projection_scenario or args.general_recording_terminal):
        parser.error("general submission terminal requires its own General scenario")
    if args.general_recording_terminal and not args.general_resource_projection_scenario:
        parser.error("general recording terminal requires the General scenario")
    if args.completed_shader_texture_input_scenario and (args.general_resource_projection_scenario
            or args.shader_usage_values_scenario or args.stream_geometry_scenario
            or args.stream_geometry_lifecycle_scenario or args.typed_execution_scenario
            or args.typed_target_finite_loss_scenario or args.canonical_upload_scope_uncertainty):
        parser.error("completed shader texture input uses its own scenario")
    if args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive")
    if args.general_resource_projection_scenario and (args.shader_usage_values_scenario
            or args.stream_geometry_scenario or args.stream_geometry_lifecycle_scenario
            or args.typed_execution_scenario or args.typed_target_finite_loss_scenario
            or args.canonical_upload_scope_uncertainty):
        parser.error("general resource projection is an independent original CPU scenario")
    if args.shader_usage_values_scenario and (args.stream_geometry_scenario
            or args.stream_geometry_lifecycle_scenario or args.typed_execution_scenario
            or args.typed_target_finite_loss_scenario or args.canonical_upload_scope_uncertainty):
        parser.error("shader usage values is an independent contract scenario")
    args.output.mkdir(parents=True, exist_ok=True)
    public_manifest = read_bindless_publication(
        args.runner / "bindless-rhi-fixture/public-compute/WebGpuPublicCompute.emulated.bindless.yaml")
    public_graphics_manifest = read_bindless_publication(
        args.runner / "bindless-rhi-fixture/public-graphics/WebGpuPublicGraphics.emulated.bindless.yaml")
    producer_manifest = read_bindless_publication(args.runner /
        "bindless-rhi-fixture/zero-base-producer/ArpZeroBaseIndirectProducer.emulated.bindless.yaml")
    indirect_consumer_manifest = read_bindless_publication(args.runner /
        "bindless-rhi-fixture/indirect-consumer/BindlessIndirectConsumer.emulated.bindless.yaml")
    ready = threading.Event()
    reference_contract = load_reference_contract(args.runner, args.original_root_array_execution_only) if args.original_root_implicit_parity else None
    pbr_reference_contract = load_pbr_reference_contract(args.runner) if args.original_root_pbr_material else None
    terrain_reference_contract = load_terrain27_reference_contract(args.runner) if args.original_root_terrain27 else None
    result = {}
    journal = BrowserProgressJournal()
    progress = journal.observations
    started = time.monotonic()

    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, *unused):
            pass

        def do_GET(self):
            if self.path != "/progress-contract.js":
                return super().do_GET()
            body = ("window.BrowserProgressLimits=" + json.dumps(journal.limits) + ";").encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/javascript")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            if self.path == "/progress":
                try:
                    batch = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                    receipt = journal.accept(batch, time.monotonic() - started)
                    # Preserve accepted evidence even if an outer CTest timeout
                    # terminates the runner before its final result arrives.
                    if args.original_bindless_root_preparation:
                        args.output.mkdir(parents=True, exist_ok=True)
                        (args.output / "progress.json").write_text(
                            json.dumps({"finalResult": False, "progress": progress}, indent=2),
                            encoding="utf-8")
                except (ValueError, AttributeError) as error:
                    self.send_error(400, str(error))
                    return
                body = json.dumps(receipt).encode()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if self.path != "/result":
                self.send_error(404)
                return
            result.update(json.loads(self.rfile.read(int(self.headers["Content-Length"]))))
            complete = journal.matches(result.get("progress"))
            result["progressTransport"] = {"contractVersion": 1, "complete": complete,
                "messages": len(journal.messages), "codeUnits": journal.code_units}
            if not complete:
                result["success"] = False
                result.setdefault("diagnostics", []).append("BROWSER-PROGRESS-FINAL-MISMATCH")
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"ok")
            ready.set()

    server = create_browser_http_server(
        functools.partial(Handler, directory=str(args.runner.resolve())))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    query = []
    if args.original_bindless_root_preparation:
        query.append("original-bindless-root-preparation")
    if args.original_root_geometry_execution_only:
        query.append("original-root-geometry-execution-only")
    if args.original_root_sampled_execution_only:
        query.append("original-root-sampled-execution-only")
    if args.original_root_array_execution_only:
        query.append("original-root-array-execution-only")
    if args.original_root_composed_execution_only:
        query.append("original-root-composed-execution-only")
    if args.original_root_push_execution_only:
        query.append("original-root-push-execution-only")
    if args.original_root_matrix_execution_only:
        query.append("original-root-matrix-execution-only")
    if args.original_root_skinning256:
        query.append("original-root-skinning256")
    if args.original_root_instance64:
        query.append("original-root-instance64")
    if args.original_root_pbr_material:
        query.append("original-root-pbr-material")
    if args.original_root_terrain27:
        query.append("original-root-terrain27")
    if args.original_root_wocc_ibl:
        query.append("original-root-wocc-ibl")
    if args.original_root_storage_execution_only:
        query.append("original-root-storage-execution-only")
    if args.original_root_implicit_parity:
        query.append("original-root-implicit-parity")
    if args.general_finite_graphics_content_root:
        query.extend(("general-resource-projection", "general-finite-graphics-content-root"))
        query.append("general-graphics-root-terminal=" + (args.general_graphics_root_terminal or "abandon"))
    if args.general_graphics_shader_content:
        query.append("general-graphics-shader-content")
    if args.general_graphics_native_custody:
        query.append("general-graphics-native-custody")
    if args.general_graphics_dependency_freeze:
        query.append("general-graphics-dependency-freeze")
    if args.general_graphics_native_payload:
        query.append("general-graphics-native-payload")
    if args.general_graphics_native_cold:
        query.append("general-graphics-native-cold")
    if args.general_graphics_native_command:
        query.append("general-graphics-native-command")
    if args.general_graphics_native_callback:
        query.append("general-graphics-native-callback")
    if args.general_graphics_recording_terminal:
        query.extend(("general-graphics-native-issuer","general-graphics-native-recording",
            "general-graphics-recording-terminal="+args.general_graphics_recording_terminal))
    if args.general_graphics_native_submission:
        query.append("general-graphics-native-submission")
    if args.general_graphics_output_readback:
        query.append("general-graphics-output-readback")
    if args.general_graphics_output_terminal:
        query.append("general-graphics-output-terminal="+args.general_graphics_output_terminal)
    if args.general_graphics_submission_terminal:
        query.append("general-graphics-submission-terminal="+args.general_graphics_submission_terminal)
    if args.general_graphics_physical_loss:
        query.append("general-graphics-physical-loss="+args.general_graphics_physical_loss)
    if args.shader_usage_values_scenario:
        query.append("shader-usage-values")
    if args.general_resource_projection_scenario:
        query.append("general-resource-projection")
    if args.general_finite_binding:
        query.append("general-finite-binding")
    if args.general_finite_source:
        query.append("general-finite-source")
    if args.general_finite_inputs:
        query.append("general-finite-inputs")
    if args.general_finite_consumption:
        query.append("general-finite-consumption")
    if args.general_finite_state_stream:
        query.append("general-finite-state-stream")
    if args.general_finite_usage:
        query.append("general-finite-usage")
    if args.general_finite_content:
        query.append("general-finite-content")
    if args.general_finite_content_plan:
        query.append("general-finite-content-plan")
    if args.general_finite_native_closure:
        query.append("general-finite-native-closure")
    if args.general_finite_native_payload:
        query.append("general-finite-native-payload")
    if args.general_finite_native_recording:
        query.append("general-finite-native-recording")
    if args.general_finite_execution:
        query.append("general-finite-execution")
    if args.general_finite_lifecycle:
        query.append("general-finite-lifecycle")
    if args.general_content_plan_dynamic:
        query.append("general-content-plan-dynamic")
    if args.general_content_buffer_type:
        query.append("general-content-buffer-type=" + args.general_content_buffer_type)
    if args.general_content_initialization_terminal:
        query.append("general-content-initialization-terminal=" + args.general_content_initialization_terminal)
    if args.general_source_whole:
        query.append("general-source-whole")
    if args.general_source_terminal:
        query.append("general-source-terminal=" + args.general_source_terminal)
    if args.general_recording_terminal:
        query.append("general-recording-terminal=" + args.general_recording_terminal)
    if args.general_submission_terminal:
        query.append("general-submission-terminal=" + args.general_submission_terminal)
    if args.general_physical_loss:
        query.append("general-physical-loss=" + args.general_physical_loss)
    if args.general_post_suppression_failure:
        query.append("general-post-suppression-failure=" + args.general_post_suppression_failure)
    if args.general_claim_terminal:
        query.append("general-claim-terminal=" + args.general_claim_terminal)
    if args.general_map_terminal:
        query.append("general-map-terminal=" + args.general_map_terminal)
    if args.completed_shader_texture_input_scenario:
        query.append("shader-texture-input")
    if args.stream_geometry_scenario or args.stream_geometry_lifecycle_scenario:
        query.append("stream-geometry")
    if args.stream_geometry_lifecycle_scenario:
        query.append("geometry-lifecycle=" + args.stream_geometry_lifecycle_scenario)
    if args.typed_execution_scenario or args.typed_target_finite_loss_scenario:
        query.append("typed-execution")
    if args.typed_target_finite_loss_scenario:
        query.append("target-finite-loss")
    if args.disable_indirect_first_instance:
        query.append("disable-indirect-first-instance")
    if args.canonical_upload_scope_uncertainty:
        query.append("canonical-upload-scope-uncertainty=" + args.canonical_upload_scope_uncertainty)
    try:
        profile_directory=tempfile.TemporaryDirectory(prefix="arp-bindless-rhi-browser-")
        with profile_directory as profile:
            runner_name = ("bindless_webgpu_original_root_runner.html" if args.original_bindless_root_preparation else "original_general_finite_graphics_recording_runner.html" if args.general_graphics_recording_terminal else "original_general_finite_graphics_content_root_runner.html"
                if args.general_finite_graphics_content_root else "bindless_webgpu_rhi_runner.html")
            command = [args.browser, "--headless=new", "--no-first-run",
                "--enable-logging=stderr",
                "--remote-debugging-port=0",
                # Isolate this temporary test profile from externally installed
                # extensions and their document-start scripts.
                "--disable-extensions",
                "--no-default-browser-check", "--disable-background-networking",
                # Keep real queue/map/scope polling timers active in the
                # headless page. This changes scheduling only; no native
                # feature, callback result or test deadline is substituted.
                "--disable-background-timer-throttling",
                "--disable-renderer-backgrounding", "--disable-backgrounding-occluded-windows",
                "--user-data-dir=" + profile,
                f"http://127.0.0.1:{server.server_port}/{runner_name}"
                + ("?" + "&".join(query) if query else "")]
            if args.wasm_baseline_only:
                command.insert(1, "--js-flags=--liftoff-only")
            with (args.output / "browser.log").open("w", encoding="utf-8") as log:
                browser = subprocess.Popen(command, stdout=log, stderr=log,
                    creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
                try:
                    if not wait_for_browser_result(ready, browser, profile, args.timeout_seconds):
                        raise RuntimeError(
                            "RHI WebGPU pipeline smoke timed out; progress=" + json.dumps(progress))
                except RuntimeError as error:
                    result["success"] = False
                    result.setdefault("diagnostics", []).append(str(error))
                    raise
                finally:
                    retire_browser_process(browser,profile_directory,10)
    finally:
        server.shutdown()
        server.server_close()
        result["progress"] = progress
        result["hostWasmCompilerPolicy"] = "baseline-only" if args.wasm_baseline_only else "default"
        result["publicComputePackageIdentity"] = public_manifest["shaderPackageIdentity"]
        result["publicGraphicsPackageIdentity"] = public_graphics_manifest["shaderPackageIdentity"]
        result["preparedZeroBaseProducerPackageIdentity"] = producer_manifest["shaderPackageIdentity"]
        result["processorIntentConsumerPackageIdentity"] = indirect_consumer_manifest["shaderPackageIdentity"]
        result["publicIndirectInstanceConsumer"] = public_graphics_manifest.get("indirectInstanceConsumer")
        result["publicEmulatedAllowlist"] = [
            "Compute.SampledTexture.load(nonUniform=true)",
            "Compute.SampledTexture.query(nonUniform=true)",
            "Compute.SampledTexture.sampleLevel(nonUniform=true,independentSampler=true)",
            "Compute.StorageTexture.load(nonUniform=true)",
            "Compute.StorageTexture.query(nonUniform=true)",
            "Compute.StorageTexture.store(nonUniform=true)",
            "Compute.UniformBuffer.load(nonUniform=true)",
            "Compute.ReadOnlyStorageBuffer.load(nonUniform=true)",
            "Compute.ReadOnlyStorageBuffer.query(nonUniform=true)",
            "Compute.StorageBuffer.load(nonUniform=true)",
            "Compute.StorageBuffer.query(nonUniform=true)",
            "Compute.StorageBuffer.store(nonUniform=true)",
            "Compute.StorageBuffer.atomicAdd(nonUniform=true)"]
        result["publicGraphicsEmulatedAllowlist"] = [
            "Fragment.SampledTexture.load(nonUniform=true)",
            "Fragment.SampledTexture.query(nonUniform=true)",
            "Fragment.SampledTexture.sampleLevel(nonUniform=true,independentSampler=true)",
            "Fragment.SampledTexture.sampleGrad(nonUniform=true,independentSampler=true,explicitGradients=true)",
            "Fragment.UniformBuffer.load(nonUniform=true,abi=Slang.float4.uniform/1)",
            "Fragment.ReadOnlyStorageBuffer.load(nonUniform=true,abi=Slang.uint32.structured/1)",
            "Fragment.ReadOnlyStorageBuffer.query(nonUniform=true,abi=Slang.uint32.structured/1)",
            "Fragment.StorageBuffer.load(nonUniform=true,abi=Slang.uint32.structured/1)",
            "Fragment.StorageBuffer.query(nonUniform=true,abi=Slang.uint32.structured/1)",
            "Fragment.StorageBuffer.store(nonUniform=true,abi=Slang.uint32.structured/1)",
            "Fragment.StorageBuffer.atomicAdd(nonUniform=true,abi=Slang.uint32.structured/1)"]
        if args.original_bindless_root_preparation and not args.original_root_wocc_ibl:
            for key in ("publicComputePackageIdentity", "publicGraphicsPackageIdentity",
                        "preparedZeroBaseProducerPackageIdentity", "processorIntentConsumerPackageIdentity",
                        "publicIndirectInstanceConsumer", "publicEmulatedAllowlist", "publicGraphicsEmulatedAllowlist"):
                result.pop(key, None)
            root_manifest = read_bindless_publication(
                args.runner / "bindless-rhi-fixture/original-root/MetalComputeBuffers.emulated.bindless.yaml")
            result["originalRootPreparationPackageIdentity"] = root_manifest["shaderPackageIdentity"]
            result["originalRootPreparationScope"] = {
                "contractVersion": 1, "stages": ["Compute"],
                "resources": ["UniformBuffer", "ReadOnlyStorageBuffer", "StorageBuffer"],
                "shaderEntries": ["cleanMain", "faultMain"], "executionAvailable": False}
            result["originalRootCallbackColdScope"] = {
                "contractVersion": 1, "lifecycleVersion": 10, "capacityRequestVersion": 6,
                "callbackTickets": 12, "callbackRegistrations": 12, "faultMaps": 1,
                "nativeCallbackIssued": False, "executionAvailable": False}
            result["originalRootBufferStorageScope"] = {
                "contractVersion": 3, "snapshots": 1, "resultBytes": 4,
                "reusesOriginalResult": True, "nativeMapReceiptPrepared": True,
                "nativeMapIssued": False, "publicationAvailable": False}
            result["originalRootCommandShellScope"] = {
                "contractVersions": [1, 2], "originalSealClaimed": True,
                "nativeDeviceQueueConnected": True, "nativeEncoderOpened": False,
                "executionAvailable": False}
            result["originalRootNativeProgramScope"] = {
                "contractVersion": 1, "shellContractVersion": 2,
                "originalStreamReused": True, "nativeDispatchLimitQueried": True,
                "frozenOperations": ["clear", "bind", "dispatch", "copy"],
                "budgetAndDispatchLimitRefusalValidated": True,
                "nativeEncoderOpened": False, "executionAvailable": False}
            result["originalRootCallbackCustodyScope"] = {
                "contractVersion": 1, "canonicalPoolReused": True,
                "nativeLossRegistrationArmedAndSuppressed": True,
                "nativeMapReceiptsPrepared": True, "nativeMapIssued": False,
                "nativeQueueOrScopeIssued": False, "executionAvailable": False}
            result["originalRootIssuerScope"] = {
                "contractVersion": 1, "existingIssuersReused": True,
                "nativeListenersPrepared": 7, "nativeListenersIssued": False,
                "executionAvailable": False, "completionAvailable": False}
            result["originalRootExecutionProfileScope"] = {
                "contractVersion": 1, "capabilityPublished": True,
                "preparationPromotionAllowed": False, "unavailableRequiredRejected": True,
                "unavailableAutoTraditional": True, "executionAvailable": True, "lifecycleVersion": 11,
                "numericCounters": [14, 16], "faultBits": [0, 1], "fullScenePublicExecutionAvailable": False}
            result["originalRootGraphicsPreparationScope"] = {
                "contractVersion": 2, "scalarBufferFamilies": ["RootFragmentBuffers", "RootFragmentRecords"],
                "preparedEntries": ["cleanFragment", "faultFragment"], "sameCanonicalInventory": True,
                "genuineRenderPipelineAndPhysicalGroup": result.get("success") is True
                    and result.get("originalBindlessRootGraphicsPreparationPassed") is True,
                "executionAvailable": False,
                "completionAvailable": False, "productionReady": False}
            result["originalRootSampledGraphicsColdScope"] = {
                "contractVersion": 3, "stages": ["Fragment"],
                "resourceDimensions": ["Texture2D", "Texture2DArray"],
                "format": "Rgba8Unorm", "independentSamplers": True,
                "expectedPreparationCases": 3,
                "preparationValidated": result.get("success") is True
                    and result.get("originalBindlessRootSampledPreparationPassed") is True,
                "contentValidated": False, "executionAvailable": False,
                "completionAvailable": False, "productionReady": False}
            publication_validated = (result.get("success") is True
                and result.get("originalCompletedTextureDescriptorPublicationPassed") is True)
            result["canonicalColorPublication6Scope"] = {
                "contractVersion": 6, "contentControlVersion": 1,
                "actualPublicationValidated": result.get("success") is True
                    and result.get("canonicalColorPublication6Passed") is True,
                "actualTextureFormat": "Rgba8Unorm", "actualDimension": "Texture2D",
                "versionRetained": result.get("canonicalColorPublication6Passed") is True,
                "rawExportClosed": result.get("canonicalColorPublication6Passed") is True,
                "originalContent9Read18Validated": result.get("canonicalColorRead18Passed") is True,
                "sampledExecutionValidated": False, "hdrCubeExecutionValidated": False,
                "productionReady": False}
            result["ordinaryColorClosure17Scope"] = {
                "preparationContractVersion": 11, "closureContractVersion": 17,
                "actualUploadAndColdCorrespondenceValidated": result.get("ordinaryColorClosure17Passed") is True,
                "formats": ["Rgba8Unorm", "Rgba16Float"],
                "dimensions": ["Texture2D", "Texture2DArray", "TextureCube"],
                "sampledExecutionValidated": False, "productionReady": False}
            result["ordinaryColorExecution13Scope"] = {
                "executionContractVersion": 13, "closureContractVersion": 18,
                "contentPlanVersion": 9, "completedReadProfile": 3,
                "executionValidated": result.get("ordinaryColorExecution13Passed") is True,
                "numericValidated": result.get("ordinaryColorExecution13NumericPassed") is True,
                "formats": ["Rgba8Unorm", "Rgba16Float"],
                "dimensions": ["Texture2D", "Texture2DArray", "TextureCube"],
                "rgba8NumericLanes": 76, "hdrNumericLanes": 112,
                "productionReady": False}
            result["originalCompletedTextureDescriptorPublicationScope"] = {
                "contractVersion": 2, "contentControlVersion": 1,
                "actualCanonicalUploadCompleted": publication_validated,
                "texelBytes": 20, "mips": 2, "mintedContentGeneration": 2,
                "ordinaryAccessClosed": publication_validated, "publicationAtomicOnOom": publication_validated,
                "sampledExecutionValidated": False, "productionReady": False}
            result["originalCompletedTextureRootPreparationScope"] = {
                "preparationContractVersion": 3, "publicationContractVersion": 2,
                "contentControlVersion": 1, "sampledTextures": 2, "independentSamplers": 2,
                "preparationValidated": result.get("success") is True
                    and result.get("originalCompletedTextureRootPreparationPassed") is True,
                "stateTransitionAvailable": False, "executionAvailable": False,
                "completionAvailable": False, "productionReady": False}
            result["originalCompletedTextureRootContentScope"] = {
                "contentPlanVersion": 9, "completedReadContractVersion": 1,
                "readPlanVersion": 10, "pendingReadVersion": 6,
                "contentControlVersion": 1, "sampledTextures": 2, "attachmentControls": 1,
                "planningValidated": result.get("success") is True
                    and result.get("originalCompletedTextureRootContentPassed") is True,
                "sameRootInventoryAndJournal": True,
                "nativeStateTransitionValidated": False, "sampledExecutionValidated": False,
                "sampledCompletionValidated": False, "productionReady": False}
            graphics_validated = (result.get("success") is True
                and result.get("originalBindlessRootGraphicsExecutionPassed") is True
                and result.get("originalBindlessRootGraphicsNumericPassed") is True)
            result["originalRootFragmentGraphicsExecutionScope"] = {
                "contractVersion": 2, "lifecycleVersion": 12, "contentPlanVersion": 9,
                "attachmentControlVersion": 5, "nativeClosureVersion": 4,
                "nativeProgramVersion": 2, "callbackAdmissionVersion": 2,
                "sameCanonicalInventoryAndJournal": True,
                "driverAllocationQueryAvailable": False, "attachmentBudgetKind": "bounded-texel-footprint",
                "scalarBufferFamilies": ["RootFragmentBuffers", "RootFragmentRecords"],
                "stages": ["Fragment"], "vertexIndexedResourcesAvailable": False,
                "lifecycleScenariosPerFamily": 11, "cleanFaultWorkflows": 88,
                "numericalReceipts": 16, "faultBits": [0, 1],
                "executionValidated": graphics_validated, "completionValidated": graphics_validated,
                "fullScenePublicExecutionAvailable": False, "productionReady": False}
            result["originalRootFiniteBufferRangeScope"] = {
                "publicationContractVersion": 1, "bufferRangeSchemaVersion": 2,
                "queriedAlignmentAndSizeLimits": True, "genericRangeWritesAvailable": False,
                "sameSnapshotInventoryJournalAndResult": True,
                "cleanFaultWorkflows": 44, "numericalReceipts": 8,
                "outsideViewCanariesUnchanged": result.get("success") is True
                    and result.get("originalBindlessRootFiniteRangeNumericPassed") is True,
                "executionValidated": result.get("success") is True
                    and result.get("originalBindlessRootFiniteRangeExecutionPassed") is True,
                "productionReady": False}
        if (args.original_bindless_root_preparation and not args.original_root_storage_execution_only
                and not args.original_root_matrix_execution_only and not args.original_root_push_execution_only
                and not args.original_root_wocc_ibl):
            validated = (result.get("success") is True and result.get("originalBindlessRootSampledExecutionPassed") is True
                and result.get("originalBindlessRootSampledNumericPassed") is True and result.get("originalBindlessRootSampledOrdinaryPassed") is True)
            result["originalSampledTextureExecutionScope"] = {
                "executionContractVersion": 3, "nativeClosureVersion": 5,
                "contentPlanVersion": 9, "completedReadVersion": 10, "uploadControlVersion": 1,
                "rootSourceVersion": 2, "pipelineInterfaceVersion": 1, "logicalSets": [0, 1, 2], "indexedSet": 1,
                "ordinaryDescriptors": ["UniformBuffer", "ReadOnlyStorageBuffer", "StorageBuffer"],
                "ordinaryBufferDomainsValidated": validated, "ordinaryBufferReceipts": 4,
                "stages": ["Vertex", "Fragment"], "textureStages": ["Fragment"], "textureDimensions": ["Texture2D"], "textureFormat": "Rgba8Unorm",
                "operations": ["load", "query", "sampleLevel", "sampleGrad"],
                "implicitSamplingValidated": False, "arraySamplingValidated": False,
                "textures": 2, "independentSamplers": 2, "cleanFaultWorkflows": 22, "numericalReceipts": 4,
                "executionValidated": validated, "completionValidated": validated,
                "fullScenePublicExecutionAvailable": False, "productionReady": False}
        (args.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if args.original_root_wocc_ibl:
        for key in ("publicComputePackageIdentity", "publicGraphicsPackageIdentity",
                    "preparedZeroBaseProducerPackageIdentity", "processorIntentConsumerPackageIdentity",
                    "publicIndirectInstanceConsumer", "publicEmulatedAllowlist", "publicGraphicsEmulatedAllowlist"):
            result.pop(key, None)
        try:
            result["independentWoccIbl"] = validate_wocc_ibl_report(
                result, ROOT / "tests/fixtures/wocc_ibl_inputs")
            validated = True
        except (ValueError, RuntimeError, KeyError, TypeError) as error:
            result["success"] = False
            result.setdefault("diagnostics", []).append(str(error))
            validated = False
        (args.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0 if validated else 1
    if args.original_root_composed_execution_only:
        validated = (result.get("success") is True
            and result.get("originalBindlessRootComposedExecutionPassed") is True
            and result.get("originalBindlessRootComposedNumericPassed") is True
            and result.get("originalBindlessRootComposedGraphicsExecutionPassed") is True
            and result.get("originalBindlessRootComposedGraphicsNumericPassed") is True
            and result.get("originalBindlessRootRuntimeMember2Passed") is True
            and result.get("originalBindlessRootComposedAliasRefusalPassed") is True
            and result.get("originalBindlessRootComposedRetirementPassed") is True)
        result["originalComposedBufferExecutionScope"] = {
            "rootSourceVersion": 2, "pipelineInterfaceVersion": 1,
            "compute": {"executionVersion": 1, "closureVersion": 1, "lifecycleVersion": 11,
                        "cleanFaultWorkflows": 22, "numericalReceipts": 4},
            "graphics": {"executionVersion": 2, "closureVersion": 4, "lifecycleVersion": 12,
                         "contentPlanVersion": 9, "cleanFaultWorkflows": 22, "numericalReceipts": 4},
            "stages": ["Compute", "Vertex", "Fragment"], "logicalSets": [0, 1, 2], "indexedSet": 1,
            "ordinaryDescriptors": ["UniformBuffer", "ReadOnlyStorageBuffer", "StorageBuffer"],
            "sameCanonicalInventoryAndJournal": True, "cleanFaultWorkflows": 44, "numericalReceipts": 8,
            "writableAliasRefusalBeforeRecording": result.get("originalBindlessRootComposedAliasRefusalPassed") is True,
            "executionValidated": validated, "completionValidated": validated,
            "ordinaryGraphics": validated, "ordinaryImages": False, "dynamicOffsets": False, "ordinaryArrays": False,
            "fullScenePublicExecutionAvailable": False, "productionReady": False}
        (args.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if args.original_root_array_execution_only:
        validated = (result.get("success") is True
            and result.get("originalBindlessRootArrayExecutionPassed") is True
            and result.get("originalBindlessRootArrayNumericPassed") is True
            and result.get("originalBindlessRootArrayOrdinaryPassed") is True
            and result.get("originalBindlessRootArrayRetirementPassed") is True)
        result["originalArrayTextureExecutionScope"] = {
            "executionContractVersion": 4, "nativeClosureVersion": 6,
            "contentPlanVersion": 9, "completedReadVersion": 10, "uploadControlVersion": 1,
            "lifecycleVersion": 12, "sameCanonicalInventoryAndJournal": True,
            "rootSourceVersion": 2, "pipelineInterfaceVersion": 1, "logicalSets": [0, 1, 2], "indexedSet": 1,
            "ordinaryDescriptors": ["UniformBuffer", "ReadOnlyStorageBuffer", "StorageBuffer"],
            "ordinaryBufferDomainsValidated": validated, "ordinaryBufferReceipts": 4,
            "stages": ["Vertex", "Fragment"], "textureStages": ["Fragment"], "textureDimensions": ["Texture2DArray"],
            "textureFormat": "Rgba8Unorm", "operations": ["load", "query", "sampleLevel", "sampleGrad"],
            "textures": 2, "independentSamplers": 2, "arrayLayers": 2, "mips": 2,
            "cleanFaultWorkflows": 22, "numericalReceipts": 4,
            "executionValidated": validated, "completionValidated": validated,
            "implicitSamplingValidated": False, "fullScenePublicExecutionAvailable": False,
            "productionReady": False}
        (args.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if args.original_root_implicit_parity:
        result["implicitSamplingParityValidated"] = validate_reference_result(result, reference_contract)
        if not result["implicitSamplingParityValidated"]:
            result["success"] = False
            result.setdefault("diagnostics", []).append("IMPLICIT-REFERENCE-EVIDENCE-MISMATCH")
        scope = result.get("originalArrayTextureExecutionScope" if args.original_root_array_execution_only else "originalSampledTextureExecutionScope")
        if scope is not None:
            scope["implicitDerivativeReferenceParity"] = result["implicitSamplingParityValidated"]
        (args.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    if args.original_root_geometry_execution_only:
        messages = [p["message"] for p in result.get("progress", [])]
        execution_details = " Geometry1 Root5 Content9 lifecycle12 same-root-journal budgets-returned"
        numeric_details = " indexed3 vertexOffset16 indexOffset2 firstInstance1 same-root-journal"
        executed = all(messages.count(f"BINDLESS_WEBGPU_INITIALIZED_GEOMETRY_EXECUTION_OK rate{rate} scenario{scenario} fault{fault}" + execution_details) == 1
            for rate in ("Vertex", "Instance") for scenario in range(11) for fault in (0, 1))
        numeric = all(messages.count(f"BINDLESS_WEBGPU_INITIALIZED_GEOMETRY_NUMERIC_OK rate{rate} fault{fault}" + numeric_details) == 2
            for rate in ("Vertex", "Instance") for fault in (0, 1))
        validated = (result.get("success") is True and result.get("geometryExecutionOnly") is True
            and result.get("scenario") == "original-bindless-initialized-geometry"
            and all(result.get(k) is True for k in ("originalGeometryExecutionPassed", "originalGeometryNumericPassed", "originalGeometryRetirementPassed"))
            and executed and numeric and result.get("progressTransport", {}).get("complete") is True
            and result.get("publicBindlessExecutionAvailable") is False and result.get("productionReady") is False
            and not result.get("diagnostics") and not result.get("nativeErrors"))
        result["originalGeometryExecutionScope"] = {"geometryVersion": 1, "rootVersion": 5,
            "contentVersion": 9, "lifecycleVersion": 12, "initializedFetchVersion": 2,
            "cleanFaultWorkflows": 44, "numericalReceipts": 8,
            "rates": ["Vertex", "Instance"], "sameCanonicalInventoryAndJournal": True,
            "executionValidated": validated, "fullScenePublicExecutionAvailable": False, "productionReady": False}
        result["success"] = validated
        (args.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0 if validated else 1
    if args.original_root_push_execution_only:
        messages = [p["message"] for p in result.get("progress", [])]
        executed = [m for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PUSH_EXECUTION_OK ")]
        numeric = [m for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PUSH_NUMERIC_OK ")]
        multi_executed = [m for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PUSH_MULTI_EXECUTION_OK ")]
        multi_numeric = [m for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PUSH_MULTI_NUMERIC_OK ")]
        slices = [re.fullmatch(r"BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PUSH_SLICES_OK updates2 secondOffset(\d+) resident(\d+) alignment(\d+)", m)
            for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PUSH_SLICES_OK ")]
        aligned = len(slices) == 22 and all(m and int(m[1]) >= 224 and int(m[3]) > 0
            and int(m[1]) % int(m[3]) == 0 and int(m[2]) == int(m[1]) + 224 for m in slices)
        validated = (result.get("success") is True and result.get("pushExecutionOnly") is True
            and all(result.get(k) is True for k in ("originalPushExecutionPassed", "originalPushNumericPassed", "originalPushRetirementPassed"))
            and len(executed) == 22 and len(numeric) == 4
            and len(multi_executed) == 22 and len(multi_numeric) == 4 and aligned
            and all(result.get(k) is True for k in ("originalPushMultiExecutionPassed", "originalPushMultiNumericPassed", "originalPushAlignedSlicesPassed"))
            and result.get("progressTransport", {}).get("complete") is True
            and result.get("publicBindlessExecutionAvailable") is False
            and not result.get("diagnostics") and not result.get("nativeErrors"))
        result["originalPushExecutionScope"] = {
            "executionVersion": 2, "closureVersion": 4, "lifecycleVersion": 12,
            "stages": ["Vertex", "Fragment"], "pushBlockBytes": 224,
            "numericFloatLanes": 4, "exactNumericEquality": True,
            "cleanFaultWorkflows": 44, "numericalReceipts": 8,
            "singleUpdateWorkflows": 22, "twoUpdateWorkflows": 22,
            "nonzeroAlignedOffsetValidated": aligned, "distinctFrozenSlicesValidated": validated,
            "twoUpdatePayloadBytes": 448,
            "queriedUploadSlices": [{"secondOffset": int(m[1]), "residentBytes": int(m[2]), "alignment": int(m[3])} for m in slices if m],
            "executionValidated": validated, "sameCanonicalInventoryAndJournal": True,
            "pushExecutionValidated": validated, "productionReady": False}
        (args.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0 if validated else 1
    if args.original_root_matrix_execution_only:
        messages = [p["message"] for p in result.get("progress", [])]
        sampled_material = args.original_root_pbr_material or args.original_root_terrain27
        execution_prefix = ("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_TERRAIN27_EXECUTION_OK " if args.original_root_terrain27
            else "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PBR_MATERIAL_EXECUTION_OK " if args.original_root_pbr_material
            else "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_MATRIX_EXECUTION_OK ")
        executed = [m for m in messages if m.startswith(execution_prefix)]
        numeric_prefix = ("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_TERRAIN27_NUMERIC_OK " if args.original_root_terrain27
            else "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PBR_MATERIAL_NUMERIC_OK " if args.original_root_pbr_material
            else "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_INSTANCE64_NUMERIC_OK " if args.original_root_instance64
            else "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_MATRIX_NUMERIC_OK ")
        numeric = [m for m in messages if m.startswith(numeric_prefix)]
        workload = ("GraphicsTerrain27" if args.original_root_terrain27 else "GraphicsPbrMaterial" if args.original_root_pbr_material else "GraphicsInstance64" if args.original_root_instance64 else
            "GraphicsSkinning256" if args.original_root_skinning256 else "GraphicsMatrixOrdinary")
        numeric_description = ("25-2D two-array-pages mip1 six-weights snow-wet ordinary96+16" if args.original_root_terrain27 else "five-role material mip1 discard filters leaf normal ordinary288+16" if args.original_root_pbr_material else
            "instance64 base1 rotation-normal-tangent metadata-bits wind-current-previous-grass-canopy-rigid ordinary256+224" if args.original_root_instance64 else
            "skinning256 temporal-high-indices previous-count220 normalized quaternion normal-tangent ordinary32864+32"
            if args.original_root_skinning256 else "current-previous-palette projective-w ordinary320+16")
        numeric_mode = " lanes128 tolerance2e-5 " if sampled_material else " lanes176 tolerance2e-5 metadata-bits " if args.original_root_instance64 else " lanes64 position-exact normal-tangent2e-5 " if args.original_root_skinning256 else " lanes32 exact "
        matched_numeric = all(numeric.count(numeric_prefix + "fault"
            + str(fault) + numeric_mode + numeric_description + " color2x2") == 2 for fault in (0, 1))
        validated = (result.get("success") is True and result.get("matrixExecutionOnly") is True
            and result.get("workload") == workload
            and result.get("skinning256") is args.original_root_skinning256
            and result.get("instance64") is args.original_root_instance64
            and result.get("pbrMaterial") is args.original_root_pbr_material
            and result.get("terrain27") is args.original_root_terrain27
            and matched_numeric
            and all(result.get(k) is True for k in ("originalMatrixExecutionPassed", "originalMatrixNumericPassed", "originalMatrixRetirementPassed"))
            and len(executed) == 22 and len(numeric) == 4
            and result.get("progressTransport", {}).get("complete") is True
            and result.get("publicBindlessExecutionAvailable") is False
            and not result.get("diagnostics") and not result.get("nativeErrors"))
        pbr_parity = args.original_root_pbr_material and validate_pbr_reference_result(result, pbr_reference_contract)
        if args.original_root_pbr_material:
            validated = validated and pbr_parity
            result["success"] = validated
        result["originalMatrixExecutionScope"] = {
            "executionVersion": 4 if args.original_root_terrain27 else 3 if sampled_material else 2,
            "closureVersion": 6 if args.original_root_terrain27 else 5 if sampled_material else 4, "lifecycleVersion": 12,
            "workload": workload,
            "stages": ["Vertex", "Fragment"],
            "ordinaryBufferBytes": [96, 16] if args.original_root_terrain27 else [288, 16] if args.original_root_pbr_material else [256, 224] if args.original_root_instance64 else [32864, 32] if args.original_root_skinning256 else [320, 16],
            "numericFloatLanes": 128 if sampled_material else 176 if args.original_root_instance64 else 64 if args.original_root_skinning256 else 32,
            "exactNumericEquality": not (args.original_root_instance64 or args.original_root_skinning256 or sampled_material),
            "arithmeticTolerance": 2e-5 if args.original_root_instance64 or args.original_root_skinning256 or sampled_material else 0,
            "exactPositionLanes": args.original_root_skinning256,
            "exactMetadataBits": args.original_root_instance64,
            "cleanFaultWorkflows": 22, "numericalReceipts": 4,
            "executionValidated": validated, "sameCanonicalInventoryAndJournal": True,
            "pushExecutionValidated": False, "productionReady": False}
        if args.original_root_pbr_material:
            result["originalMatrixExecutionScope"]["sampledResources"] = {
                "textures": 10, "independentSamplers": 2, "mips": 2,
                "operations": ["sampleGrad"], "nonUniformMaterialSelection": True,
                "implicitSamplingEquivalent": pbr_parity, "actualDiscardValidated": pbr_parity,
                "distinctFiltersValidated": pbr_parity,
                "coveragePredicateValidated": validated}
        if args.original_root_terrain27:
            terrain_parity = validate_terrain27_reference_result(result, terrain_reference_contract)
            plan = result.get("plannedDevice", {})
            expected_limits = {"maxBindGroups": 2, "maxBindingsPerBindGroup": 31,
                "maxSampledTexturesPerShaderStage": 27, "maxSamplersPerShaderStage": 1,
                "maxUniformBuffersPerShaderStage": 1, "maxStorageBuffersPerShaderStage": 4}
            validated = (validated and terrain_parity and plan.get("createdBeforeRhi") is True
                and plan.get("requiredLimits") == expected_limits
                and all(plan.get("actualLimits", {}).get(k, 0) >= v for k, v in expected_limits.items()))
            result["success"] = validated
            result["originalMatrixExecutionScope"]["executionValidated"] = validated
            result["originalMatrixExecutionScope"]["sampledResources"] = {
                "texture2D": 25, "texture2DArray": 2, "arrayLayers": 2,
                "independentSamplers": 1, "mips": 2, "operations": ["sampleGrad"],
                "nonUniformMaterialSelection": True, "implicitSamplingEquivalent": terrain_parity,
                "fullProductionTerrainValidated": False}
        (args.output / "report.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
        print(json.dumps(result, indent=2))
        return 0 if validated else 1
    if args.original_root_storage_execution_only:
        validated=(result.get("success") is True and result.get("storageExecutionOnly") is True
            and all(result.get(k) is True for k in ("originalStorageExecutionPassed","originalStorageNumericPassed","originalStorageRetirementPassed"))
            and result.get("publicBindlessExecutionAvailable") is False
            and not result.get("diagnostics") and not result.get("nativeErrors"))
        result["originalStorageExecutionScope"]={"executionVersion":10,"closureVersion":12,"lifecycleVersion":16,
            "programVersion":4,"contentPlanVersion":14,"completionVersion":10,"sameCanonicalInventoryAndJournal":True,
            "cleanFaultWorkflows":22,"numericalReceipts":4,"executionValidated":validated,"completionValidated":validated,
            "fullScenePublicExecutionAvailable":False,"productionReady":False}
        (args.output / "report.json").write_text(json.dumps(result,indent=2),encoding="utf-8")
        print(json.dumps(result,indent=2))
        return 0 if validated else 1
    print(json.dumps(result, indent=2))
    if args.original_root_composed_execution_only:
        messages = [p["message"] for p in result.get("progress", [])]
        executed = [m for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPOSED_EXECUTION_OK ")]
        numeric = [m for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPOSED_NUMERIC_OK ")]
        return 0 if (result.get("success") is True and result.get("composedExecutionOnly") is True
            and result.get("originalBindlessRootComposedExecutionPassed") is True
            and result.get("originalBindlessRootComposedNumericPassed") is True
            and result.get("originalBindlessRootComposedRetirementPassed") is True
            and result.get("originalBindlessRootComposedGraphicsExecutionPassed") is True
            and result.get("originalBindlessRootComposedGraphicsNumericPassed") is True
            and result.get("originalBindlessRootRuntimeMember2Passed") is True
            and result.get("originalBindlessRootComposedAliasRefusalPassed") is True
            and result.get("publicBindlessExecutionAvailable") is False
            and not result.get("diagnostics") and not result.get("nativeErrors")
            and len(executed)==22 and len(numeric)==4) else 1
    if args.original_root_array_execution_only:
        messages = [p["message"] for p in result.get("progress", [])]
        executed = [m for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_ARRAY_EXECUTION_OK ")]
        numeric = [m for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_ARRAY_NUMERIC_OK ")]
        return 0 if (result.get("success") is True and result.get("arrayExecutionOnly") is True
            and result.get("originalBindlessRootArrayExecutionPassed") is True
            and result.get("originalBindlessRootArrayNumericPassed") is True
            and result.get("originalBindlessRootArrayOrdinaryPassed") is True
            and result.get("originalBindlessRootArrayRetirementPassed") is True
            and result.get("publicBindlessExecutionAvailable") is False
            and not result.get("diagnostics") and not result.get("nativeErrors")
            and len(executed)==22 and len(numeric)==4) else 1
    if args.original_root_sampled_execution_only:
        messages = [p["message"] for p in result.get("progress", [])]
        executed = [m for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_SAMPLED_EXECUTION_OK ")]
        numeric = [m for m in messages if m.startswith("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_SAMPLED_NUMERIC_OK ")]
        return 0 if (result.get("success") is True and result.get("sampledExecutionOnly") is True
            and result.get("originalBindlessRootSampledExecutionPassed") is True
            and result.get("originalBindlessRootSampledNumericPassed") is True
            and result.get("originalBindlessRootSampledOrdinaryPassed") is True
            and result.get("originalBindlessRootSampledRetirementPassed") is True
            and result.get("publicBindlessExecutionAvailable") is False
            and not result.get("diagnostics") and not result.get("nativeErrors")
            and len(executed)==22 and len(numeric)==4) else 1
    scope_mode_matches = result.get("canonicalUploadScopeUncertaintyKind") == args.canonical_upload_scope_uncertainty
    root_preparation_matches = not args.original_bindless_root_preparation or (
        result.get("scenario") == "original-bindless-root-preparation"
        and result.get("originalBindlessRootPreparationPassed") is True
        and result.get("originalBindlessRootGraphicsPreparationPassed") is True
        and result.get("originalBindlessRootSampledPreparationPassed") is True
        and result.get("originalCompletedTextureDescriptorPublicationPassed") is True
        and result.get("canonicalColorPublication6Passed") is True
        and result.get("canonicalColorRead18Passed") is True
        and result.get("ordinaryColorClosure17Passed") is True
        and result.get("ordinaryColorExecution13Passed") is True
        and result.get("ordinaryColorExecution13NumericPassed") is True
        and result.get("originalCompletedTextureRootPreparationPassed") is True
        and result.get("originalCompletedTextureRootContentPassed") is True
        and result.get("originalBindlessRootRetirementPassed") is True
        and result.get("originalBindlessRootNativeClosurePassed") is True
        and result.get("originalBindlessRootCleanFaultEntriesPassed") is True
        and result.get("originalBindlessRootCallbackColdAdmissionPassed") is True
        and result.get("originalBindlessRootBufferStoragePassed") is True
        and result.get("originalBindlessRootCommandShellPassed") is True
        and result.get("originalBindlessRootNativeProgramPassed") is True
        and result.get("originalBindlessRootCallbackCustodyPassed") is True
        and result.get("originalBindlessRootIssuersPassed") is True
        and result.get("originalBindlessRootExecutionProfileRefusalPassed") is True
        and result.get("originalBindlessRootExecutionPassed") is True
        and result.get("publicOriginalBindlessRootExecutionAvailable") is True
        and result.get("originalBindlessRootNumericPassed") is True
        and result.get("nativeExecutionValidated") is True
        and result.get("publicBindlessExecutionAvailable") is False)
    if not root_preparation_matches:
        return 1
    usage_mode_matches = not args.shader_usage_values_scenario or (
        result.get("scenario") == "typed-general-shader-usage-values"
        and result.get("generalShaderUsageValuesPassed") is True
        and result.get("publicBindlessExecutionAvailable") is False
        and result.get("nativeExecutionValidated") is False)
    terminal_mode_matches = not args.general_recording_terminal or args.general_finite_lifecycle or (
        result.get("scenario") == "original-general-recording-terminal"
        and result.get("originalGeneralRecordingTerminalKind") == args.general_recording_terminal
        and result.get("originalGeneralRecordingTerminalPassed") is True
        and result.get("nativeExecutionValidated") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    submission_probe_selected = args.general_submission_terminal in (
        "throw-before", "throw-after", "native-error", "reverse-scopes", "partial-scope")
    submission_terminal_matches = not args.general_submission_terminal or submission_probe_selected or args.general_finite_lifecycle or (
        result.get("scenario") == "original-general-submission-terminal"
        and result.get("originalGeneralSubmissionTerminalKind") == args.general_submission_terminal
        and result.get("originalGeneralSubmissionTerminalPassed") is True
        and result.get("nativeExecutionValidated") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    submission_probe_matches = not submission_probe_selected or args.general_finite_lifecycle or (
        result.get("scenario") == "original-general-submission-probe"
        and result.get("originalGeneralSubmissionProbeKind") == args.general_submission_terminal
        and result.get("originalGeneralSubmissionProbePassed") is True
        and (args.general_submission_terminal != "reverse-scopes" or result.get("originalGeneralOutputNumericalPassed") is True)
        and result.get("nativeExecutionValidated") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    physical_mode_matches = not args.general_physical_loss or args.general_finite_lifecycle or (
        result.get("scenario") == "original-general-physical-loss"
        and result.get("originalGeneralPhysicalLossKind") == args.general_physical_loss
        and result.get("originalGeneralPhysicalLossPassed") is True
        and result.get("nativeExecutionValidated") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    map_mode_matches = not args.general_map_terminal or args.general_finite_lifecycle or (
        result.get("scenario") == "original-general-output-map-terminal"
        and result.get("originalGeneralOutputMapKind") == args.general_map_terminal
        and result.get("originalGeneralOutputMapPassed") is True
        and result.get("originalGeneralOutputMapRetirementPassed") is True
        and result.get("originalGeneralOutputNumericalPassed") is (args.general_map_terminal in ("reverse-scopes", "published-loss"))
        and result.get("originalGeneralOutputMapPhysicalLoss") is (args.general_map_terminal in (
            "throw-before", "throw-after", "scope-registration", "unmap-throw", "pending-loss", "published-loss"))
        and result.get("nativeExecutionValidated") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    finite_mode_matches = not args.general_finite_binding or (
        result.get("scenario") == "original-general-finite-binding"
        and result.get("originalGeneralFiniteCompositionPassed") is True
        and result.get("originalGeneralFiniteRetirementPassed") is True
        and result.get("nativeShaderExecutionValidated") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    finite_source_matches = not args.general_finite_source or (
        result.get("scenario") == ("original-general-finite-content-usage" if composed_content_usage
                                   else "original-general-finite-source")
        and result.get("originalGeneralFiniteSourcePassed") is True
        and result.get("originalGeneralWholeSourceProfile") is args.general_source_whole
        and result.get("originalGeneralFiniteSourceRetirementPassed") is True
        and result.get("originalGeneralFiniteSourceTerminal") == (args.general_source_terminal or "reset")
        and result.get("originalGeneralFiniteSourceTerminalPassed") is True
        and (not args.general_finite_inputs or (
            result.get("originalGeneralFiniteInputsPassed") is True
            and result.get("originalGeneralFiniteInputsRetirementPassed") is True
            and result.get("originalGeneralFiniteInputsTerminalPassed") is True))
        and (not args.general_finite_consumption or (
            result.get("originalGeneralFiniteConsumptionPassed") is True
            and result.get("originalGeneralFiniteConsumptionRetirementPassed") is True
            and result.get("originalGeneralFiniteConsumptionTerminalPassed") is True
            and result.get("originalGeneralFiniteRenderBindingProjectionValidated") is False))
        and (not args.general_finite_state_stream or (
            result.get("originalGeneralFiniteStateStreamPassed") is True
            and result.get("originalGeneralFiniteStateStreamRetirementPassed") is True
            and result.get("originalGeneralFiniteStateStreamTerminalPassed") is True))
        and (not args.general_finite_usage or (
            result.get("originalGeneralFiniteUsageDependencyPassed") is True
            and result.get("originalGeneralFiniteUsageDependencyRetirementPassed") is True
            and result.get("originalGeneralFiniteUsageDependencyTerminalPassed") is True))
        and result.get("nativeShaderExecutionValidated") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    initialization_terminal_matches = not args.general_content_initialization_terminal or (
        result.get("scenario") == "original-general-finite-initialization-terminal"
        and result.get("originalGeneralFiniteInitializationTerminalKind") == args.general_content_initialization_terminal
        and result.get("originalGeneralFiniteInitializationRefusalPassed") is True
        and result.get("originalGeneralFiniteInitializationDrainPassed") is True
        and result.get("originalGeneralFiniteInitializedContentRetirementPassed") is True
        and result.get("originalGeneralFiniteInitializedReadyAccepted") is False
        and result.get("originalGeneralFiniteContentBufferType") == (args.general_content_buffer_type or "uniform")
        and result.get("originalGeneralFiniteContentBufferTypePassed") is True
        and result.get("originalGeneralFiniteInitializationCallbackOnlyPassed") is (
            args.general_content_initialization_terminal in ("cancel-drop-pending", "pending-drop-loss"))
        and result.get("originalGeneralWholeContentProfile") is args.general_source_whole
        and result.get("nativeShaderExecutionValidated") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    finite_content_matches = not args.general_finite_content or bool(args.general_content_initialization_terminal) or (
        result.get("scenario") == ("original-general-finite-content-usage" if composed_content_usage
                                   else "original-general-finite-initialized-content")
        and result.get("originalGeneralFiniteContentBufferType") == (args.general_content_buffer_type or "uniform")
        and result.get("originalGeneralFiniteContentBufferTypePassed") is True
        and result.get("originalGeneralFiniteInitializedContentPassed") is True
        and result.get("originalGeneralFiniteInitializedContentRetirementPassed") is True
        and result.get("originalGeneralFiniteContentTerminal") == (args.general_source_terminal or "reset")
        and result.get("originalGeneralFiniteContentTerminalPassed") is True
        and result.get("originalGeneralWholeContentProfile") is args.general_source_whole
        and result.get("nativeShaderExecutionValidated") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    finite_lifecycle_matches = not args.general_finite_lifecycle or bool(args.general_claim_terminal or args.general_submission_terminal or args.general_physical_loss or args.general_map_terminal or args.general_post_suppression_failure) or (
        result.get("scenario") == "original-general-finite-recording-terminal"
        and result.get("originalGeneralFiniteRecordingTerminalKind") == args.general_recording_terminal
        and result.get("originalGeneralFiniteRecordingTerminalHeldPassed") is True
        and result.get("originalGeneralFiniteRecordingTerminalPassed") is True
        and result.get("originalGeneralFiniteRecordingTerminalRetentionPassed") is True
        and result.get("originalGeneralFiniteContentPlanPassed") is True
        and result.get("originalGeneralFiniteLifecycleSealPassed") is True
        and result.get("originalGeneralFiniteContentPlanRollbackPassed") is True
        and result.get("originalGeneralFiniteContentPlanRetirementPassed") is True
        and result.get("originalGeneralCompletedTextureUploadPassed") is True
        and result.get("originalGeneralFiniteNativeClosurePassed") is True
        and result.get("originalGeneralFiniteNativePayloadPassed") is True
        and result.get("originalGeneralFiniteLifecycleVersion") == 4
        and result.get("originalGeneralFiniteContentPlanBufferType") == (args.general_content_buffer_type or "uniform")
        and result.get("originalGeneralFiniteContentPlanRangeKind") == ("whole" if args.general_source_whole else "dynamic" if args.general_content_plan_dynamic else "static")
        and result.get("finiteLifecycle4Validated") is False
        and result.get("fullFiniteExecutionValidated") is False
        and result.get("nativeShaderExecutionValidated") is False
        and result.get("nativeSyncAvailable") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    finite_claim_content_policy = (
        "actual-device-loss state-content-invalidated" if args.general_claim_terminal == "physical-loss"
        else "logical-device-terminal observations-refused physical-loss-undelivered" if args.general_claim_terminal in ("retired", "shutdown")
        else "no-recording-state-revision-unchanged original-upload-content-unchanged")
    finite_claim_lifecycle_matches = not (args.general_finite_lifecycle and args.general_claim_terminal) or (
        result.get("scenario") == "original-general-finite-claim-terminal"
        and result.get("originalGeneralFiniteClaimTerminalKind") == args.general_claim_terminal
        and result.get("originalGeneralFiniteClaimHeldPassed") is True
        and result.get("originalGeneralFiniteClaimTerminalPassed") is True
        and result.get("originalGeneralFiniteClaimReceiptRetirementPassed") is True
        and result.get("originalGeneralFiniteClaimRetentionPassed") is True
        and result.get("originalGeneralFiniteClaimContentPassed") is True
        and result.get("originalGeneralFiniteClaimContentPolicy") == finite_claim_content_policy
        and result.get("originalGeneralFiniteClaimExecutionAbsentPassed") is True
        and result.get("originalGeneralFiniteContentPlanPassed") is True
        and result.get("originalGeneralFiniteLifecycleSealPassed") is True
        and result.get("originalGeneralFiniteContentPlanRetirementPassed") is True
        and result.get("originalGeneralCompletedTextureUploadPassed") is True
        and result.get("originalGeneralFiniteNativeClosurePassed") is True
        and result.get("originalGeneralFiniteNativePayloadPassed") is True
        and result.get("originalGeneralFiniteOutputSnapshotPassed") is True
        and result.get("originalGeneralFiniteOutputNumericalPassed") is False
        and result.get("originalGeneralFiniteNumericalValues") == []
        and result.get("originalGeneralFiniteLifecycleVersion") == 4
        and result.get("originalGeneralFiniteContentPlanBufferType") == (args.general_content_buffer_type or "uniform")
        and result.get("originalGeneralFiniteContentPlanRangeKind") == ("whole" if args.general_source_whole else "dynamic" if args.general_content_plan_dynamic else "static")
        and result.get("physicalDeviceLossValidated") is (args.general_claim_terminal == "physical-loss")
        and result.get("finiteLifecycle4Validated") is False
        and result.get("fullFiniteExecutionValidated") is False
        and result.get("nativeShaderExecutionValidated") is False
        and result.get("nativeSyncAvailable") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    finite_submission_lifecycle_matches = not (args.general_finite_lifecycle and args.general_submission_terminal) or (
        result.get("scenario") == "original-general-finite-submission-terminal"
        and result.get("originalGeneralFiniteSubmissionTerminalKind") == args.general_submission_terminal
        and result.get("originalGeneralFiniteSubmissionTerminalHeldPassed") is True
        and result.get("originalGeneralFiniteSubmissionTerminalPassed") is True
        and result.get("originalGeneralFiniteSubmissionTerminalRetentionPassed") is True
        and result.get("originalGeneralFiniteSubmissionTerminalReceiptRetirementPassed") is True
        and result.get("originalGeneralFiniteOutputSnapshotPassed") is True
        and result.get("originalGeneralFiniteSubmissionVersion") == 2
        and result.get("originalGeneralFiniteOutputVersion") == 2
        and result.get("originalGeneralFiniteContentPlanPassed") is True
        and result.get("originalGeneralFiniteLifecycleSealPassed") is True
        and result.get("originalGeneralFiniteContentPlanRetirementPassed") is True
        and result.get("originalGeneralCompletedTextureUploadPassed") is True
        and result.get("originalGeneralFiniteNativeClosurePassed") is True
        and result.get("originalGeneralFiniteNativePayloadPassed") is True
        and result.get("originalGeneralFiniteLifecycleVersion") == 4
        and result.get("originalGeneralFiniteContentPlanBufferType") == (args.general_content_buffer_type or "uniform")
        and result.get("originalGeneralFiniteContentPlanRangeKind") == ("whole" if args.general_source_whole else "dynamic" if args.general_content_plan_dynamic else "static")
        and result.get("originalGeneralFiniteContentPlanInvalidatedPassed") is (args.general_submission_terminal != "reverse-scopes")
        and result.get("originalGeneralFiniteContentPlanCommitPassed") is (args.general_submission_terminal == "reverse-scopes")
        and result.get("originalGeneralFiniteOutputNumericalPassed") is (args.general_submission_terminal == "reverse-scopes")
        and result.get("nativeShaderExecutionValidated") is (args.general_submission_terminal == "reverse-scopes")
        and result.get("originalGeneralFiniteNumericalValues") == (
            [value + (4 if args.general_content_plan_dynamic else 0) for value in [1.02734375, 2.03125, 3.03515625, 4.0390625]]
            if args.general_submission_terminal == "reverse-scopes" else [])
        and result.get("finiteLifecycle4Validated") is False
        and result.get("fullFiniteExecutionValidated") is False
        and result.get("nativeSyncAvailable") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    finite_loss_lifecycle_matches = not (args.general_finite_lifecycle and args.general_physical_loss) or (
        result.get("scenario") == "original-general-finite-physical-loss"
        and result.get("originalGeneralFiniteLossKind") == args.general_physical_loss
        and result.get("originalGeneralFiniteLossHeldPassed") is True
        and result.get("originalGeneralFiniteLossTerminalPassed") is True
        and result.get("originalGeneralFiniteLossRetirementPassed") is True
        and result.get("originalGeneralFiniteLossRetentionPassed") is True
        and result.get("originalGeneralFiniteOutputSnapshotPassed") is True
        and result.get("originalGeneralFiniteContentPlanPassed") is True
        and result.get("originalGeneralFiniteLifecycleSealPassed") is True
        and result.get("originalGeneralFiniteContentPlanInvalidatedPassed") is True
        and result.get("originalGeneralFiniteContentPlanRetirementPassed") is True
        and result.get("originalGeneralCompletedTextureUploadPassed") is True
        and result.get("originalGeneralFiniteNativeClosurePassed") is True
        and result.get("originalGeneralFiniteNativePayloadPassed") is True
        and result.get("originalGeneralFiniteLifecycleVersion") == 4
        and result.get("originalGeneralFiniteContentPlanBufferType") == (args.general_content_buffer_type or "uniform")
        and result.get("originalGeneralFiniteContentPlanRangeKind") == ("whole" if args.general_source_whole else "dynamic" if args.general_content_plan_dynamic else "static")
        and result.get("physicalDeviceLossValidated") is True
        and result.get("finiteLifecycle4Validated") is False
        and result.get("fullFiniteExecutionValidated") is False
        and result.get("nativeShaderExecutionValidated") is False
        and result.get("nativeSyncAvailable") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    finite_map_completes = args.general_map_terminal in ("reverse-scopes", "published-loss")
    finite_map_physical = args.general_map_terminal in ("throw-before", "throw-after", "scope-registration", "unmap-throw", "pending-loss", "published-loss")
    finite_map_lifecycle_matches = not (args.general_finite_lifecycle and args.general_map_terminal) or (
        result.get("scenario") == "original-general-finite-map-terminal"
        and result.get("originalGeneralFiniteMapTerminalKind") == args.general_map_terminal
        and result.get("originalGeneralFiniteMapOutputPassed") is True
        and result.get("originalGeneralFiniteMapCopyHeldPassed") is True
        and (finite_map_completes or result.get("originalGeneralFiniteMapHeldPassed") is True)
        and result.get("originalGeneralFiniteMapDrainPassed") is True
        and result.get("originalGeneralFiniteMapUpstreamRetirementPassed") is True
        and result.get("originalGeneralFiniteMapRetentionPassed") is True
        and result.get("originalGeneralFiniteOutputSnapshotPassed") is True
        and result.get("originalGeneralFiniteSubmissionVersion") == 2
        and result.get("originalGeneralFiniteOutputVersion") == 2
        and result.get("originalGeneralFiniteContentPlanPassed") is True
        and result.get("originalGeneralFiniteLifecycleSealPassed") is True
        and result.get("originalGeneralFiniteContentPlanRetirementPassed") is True
        and result.get("originalGeneralCompletedTextureUploadPassed") is True
        and result.get("originalGeneralFiniteNativeClosurePassed") is True
        and result.get("originalGeneralFiniteNativePayloadPassed") is True
        and result.get("originalGeneralFiniteLifecycleVersion") == 4
        and result.get("originalGeneralFiniteContentPlanBufferType") == (args.general_content_buffer_type or "uniform")
        and result.get("originalGeneralFiniteContentPlanRangeKind") == ("whole" if args.general_source_whole else "dynamic" if args.general_content_plan_dynamic else "static")
        and result.get("originalGeneralFiniteOutputNumericalPassed") is finite_map_completes
        and result.get("nativeShaderExecutionValidated") is finite_map_completes
        and result.get("physicalDeviceLossValidated") is finite_map_physical
        and result.get("originalGeneralFiniteNumericalValues") == (
            [value + (4 if args.general_content_plan_dynamic else 0) for value in [1.02734375, 2.03125, 3.03515625, 4.0390625]]
            if finite_map_completes else [])
        and result.get("finiteLifecycle4Validated") is False
        and result.get("fullFiniteExecutionValidated") is False
        and result.get("nativeSyncAvailable") is False
        and result.get("publicBindlessExecutionAvailable") is False)
    content_plan_matches = not finite_plan_scenario or args.general_finite_lifecycle or (
        result.get("scenario") == ("original-general-finite-execution" if args.general_finite_execution else "original-general-finite-native-recording" if args.general_finite_native_recording else "original-general-finite-native-payload" if args.general_finite_native_payload else "original-general-finite-native-closure" if args.general_finite_native_closure else "original-general-finite-content-plan")
        and result.get("originalGeneralFiniteContentPlanPassed") is True
        and result.get("originalGeneralFiniteLifecycleSealPassed") is True
        and result.get("originalGeneralFiniteLifecycleVersion") == 4
        and result.get("originalGeneralFinitePendingNativeValidationPassed") is (args.general_finite_native_recording or args.general_finite_execution)
        and result.get("originalGeneralFiniteContentPlanRollbackPassed") is (not args.general_finite_execution)
        and (not args.general_finite_execution or result.get("originalGeneralFiniteContentPlanCommitPassed") is True)
        and result.get("originalGeneralFiniteContentPlanRetirementPassed") is True
        and result.get("originalGeneralCompletedTextureUploadPassed") is True
        and result.get("originalGeneralFiniteContentPlanBufferType") == (args.general_content_buffer_type or "uniform")
        and result.get("originalGeneralFiniteContentPlanRangeKind") == ("whole" if args.general_source_whole else "dynamic" if args.general_content_plan_dynamic else "static")
        and result.get("nativeShaderExecutionValidated") is args.general_finite_execution
        and result.get("publicBindlessExecutionAvailable") is False)
    finite_closure_matches = not (args.general_finite_native_closure or args.general_finite_native_payload or args.general_finite_native_recording or args.general_finite_execution) or (
        result.get("originalGeneralFiniteNativeClosurePassed") is True
        and result.get("originalGeneralFiniteNativeClosureRetentionPassed") is True
        and result.get("originalGeneralFiniteNativeClosureContractVersion") == 9
        and result.get("originalGeneralFiniteInitializedBufferCustodyVersion") == 2)
    finite_payload_matches = not (args.general_finite_native_payload or args.general_finite_native_recording or args.general_finite_execution) or (
        result.get("originalGeneralFiniteNativeFreezePassed") is True
        and result.get("originalGeneralFiniteNativePayloadPassed") is True
        and result.get("originalGeneralFiniteNativePayloadRetentionPassed") is True
        and result.get("originalGeneralFiniteNativeFreezeVersion") == 7
        and result.get("originalGeneralFiniteNativePayloadVersion") == 7
        and result.get("originalGeneralFiniteNativePayloadCommands") == 10
        and result.get("originalGeneralFiniteFrozenDependencies") == 4
        and result.get("originalGeneralFiniteColdRecordingExecutionAvailable") is args.general_finite_execution)
    finite_recording_matches = not (args.general_finite_native_recording or args.general_finite_execution) or (
        result.get("originalGeneralFiniteNativeRecordingPassed") is True
        and result.get("originalGeneralFiniteNativeRecordingRetentionPassed") is True
        and result.get("originalGeneralFiniteNativeRecordingVersion") == 7
        and result.get("originalGeneralFiniteNativeSubmissionAvailable") is args.general_finite_execution)
    finite_execution_matches = not args.general_finite_execution or (
        result.get("originalGeneralFiniteSubmissionPassed") is True
        and result.get("originalGeneralFiniteSubmissionVersion") == 2
        and result.get("originalGeneralFiniteOutputVersion") == 2
        and result.get("originalGeneralFiniteOutputNumericalPassed") is True
        and result.get("originalGeneralFiniteExecutionRetentionPassed") is True
        and result.get("nativeSyncAvailable") is False
        and isinstance(result.get("originalGeneralFiniteNumericalValues"), list)
        and len(result["originalGeneralFiniteNumericalValues"]) == 4)
    general_mode_matches = not args.general_resource_projection_scenario or finite_plan_scenario or args.general_finite_binding or args.general_finite_source or args.general_finite_content or bool(args.general_recording_terminal) or bool(args.general_submission_terminal) or bool(args.general_physical_loss) or bool(args.general_map_terminal) or (
        result.get("scenario") == "original-general-shader-resource-projection"
        and result.get("originalGeneralResourceProjectionPassed") is True
        and result.get("originalGeneralResourceOwnerInventoryPassed") is True
        and result.get("originalGeneralDependencyProjectionPassed") is True
        and result.get("originalSamplerCustodyPassed") is True
        and result.get("originalSamplerDescriptorReferencePassed") is True
        and result.get("originalSamplerNativeLossJoinPassed") is True
        and result.get("originalGeneralDescriptorBindingPassed") is True
        and result.get("originalGeneralDescriptorWritePassed") is True
        and result.get("originalGeneralNativeClosurePassed") is True
        and result.get("originalGeneralDependencyFreezePassed") is True
        and result.get("originalGeneralNativePayloadPassed") is True
        and result.get("originalGeneralShaderBufferContentPassed") is True
        and result.get("originalGeneralShaderTextureContentPassed") is True
        and result.get("completedTextureReadPlanPassed") is True
        and result.get("originalGeneralNativeColdPassed") is True
        and result.get("originalGeneralNativeCommandPassed") is True
        and result.get("originalGeneralNativeCallbackPassed") is True
        and result.get("originalGeneralNativeIssuerPassed") is True
        and result.get("originalGeneralPendingReadRecordingPassed") is True
        and result.get("originalGeneralSubmissionPassed") is True
        and result.get("originalGeneralOutputSnapshotPassed") is True
        and result.get("originalGeneralOutputReadbackInputsPassed") is True
        and result.get("originalGeneralOutputNativeJoinPassed") is True
        and result.get("originalGeneralOutputColdPassed") is True
        and result.get("originalGeneralOutputPairingPassed") is True
        and result.get("originalGeneralOutputNumericalPassed") is True
        and result.get("publicBindlessExecutionAvailable") is False
        and result.get("nativeExecutionValidated") is False)
    texture_mode_matches = not args.completed_shader_texture_input_scenario or (
        result.get("scenario") == "original-completed-shader-texture-input"
        and result.get("completedShaderTextureInputPassed") is True
        and result.get("completedShaderTextureViewInputPassed") is True
        and result.get("completedShaderTextureInputRetirementPassed") is True
        and result.get("numericalUploadedTextureReadback") is True
        and result.get("publicBindlessExecutionAvailable") is False
        and result.get("nativeShaderExecutionValidated") is False)
    if args.general_graphics_recording_terminal:
        script="""const fs=require('fs');const inspect=require(process.argv[1]).inspect;
const report=JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
const derived=inspect(report.progress.map(row=>row.message),process.argv[3],report.success,report.diagnostics||[],report.nativeErrors||[]);
const gates=['nativeDrawReadbackValidated','finiteGraphicsValidated','fullFiniteExecutionValidated','finiteLifecycle4Validated','publicBindlessExecutionAvailable','nativeSyncPromoted','woccValidated','platformValidated','productionReady'];
if(!derived.success || report.nativeRecordingValidated!==true || !report.progressTransport.complete || !gates.every(k=>report[k]===false))process.exit(1);
const physical=new URLSearchParams(process.argv[3]).get('general-graphics-physical-loss');
const submitted=physical===null || physical==='queue-uncertain';
if(report.privateGraphicsPhysicalLifetimeValidated!==(physical!==null)
  || derived.privateGraphicsPhysicalLifetimeValidated!==(physical!==null))process.exit(1);
if(process.argv[3].includes('general-graphics-native-submission') && (report.nativeSubmissionValidated!==submitted || derived.nativeSubmissionValidated!==submitted))process.exit(1);
if(physical!==null && (report.physicalTerminal!==physical || derived.physicalTerminal!==physical
  || report.privateGraphicsPhysicalLifetimeValidated!==true || derived.privateGraphicsPhysicalLifetimeValidated!==true
  || report.physicalDeviceLossValidated!==true || derived.physicalDeviceLossValidated!==true
  || report.nativeSubmissionCompletionValidated!==false || derived.nativeSubmissionCompletionValidated!==false))process.exit(1);
const readback=new URLSearchParams(process.argv[3]).has('general-graphics-output-readback');
const outputTerminal=new URLSearchParams(process.argv[3]).get('general-graphics-output-terminal');
const publishes=outputTerminal===null || ['reverse-scopes','published-loss'].includes(outputTerminal);
if(report.privateGraphicsOutput3ReadbackValidated!==(readback&&publishes) || derived.privateGraphicsOutput3ReadbackValidated!==(readback&&publishes))process.exit(1);
if(outputTerminal!==null && (report.outputTerminal!==outputTerminal || derived.outputTerminal!==outputTerminal
  || report.privateGraphicsOutput3LifecycleValidated!==true || derived.privateGraphicsOutput3LifecycleValidated!==true
  || report.physicalDeviceLossValidated!==['throw-before','throw-after','scope-registration','unmap-throw','pending-loss','published-loss'].includes(outputTerminal)))process.exit(1);
const terminal=new URLSearchParams(process.argv[3]).get('general-graphics-submission-terminal');
if(terminal!==null && (report.submissionTerminal!==terminal || derived.submissionTerminal!==terminal
  || report.submissionTerminalValidated!==true || derived.submissionTerminalValidated!==true
  || report.nativeSubmissionCompletionValidated!==(terminal==='reverse-scopes')
  || derived.nativeSubmissionCompletionValidated!==(terminal==='reverse-scopes')))process.exit(1);
const logicalLifetime=['retired','shutdown'].includes(terminal)
  || ['retired','shutdown'].includes(new URLSearchParams(process.argv[3]).get('general-graphics-recording-terminal'));
if(report.privateGraphicsLogicalLifetimeValidated!==logicalLifetime
  || derived.privateGraphicsLogicalLifetimeValidated!==logicalLifetime)process.exit(1);
const recordingLifetime=['retired','shutdown'].includes(new URLSearchParams(process.argv[3]).get('general-graphics-recording-terminal'));
if(report.privateGraphicsRecordingLifetimeValidated!==recordingLifetime
  || derived.privateGraphicsRecordingLifetimeValidated!==recordingLifetime
  || (recordingLifetime && (report.nativeSubmissionValidated!==false || report.nativeSubmissionCompletionValidated!==false
    || report.physicalDeviceLossValidated!==false)))process.exit(1);
"""
        checked=subprocess.run(["node","-e",script,str(args.runner/"original_general_finite_graphics_recording_report.js"),
            str(args.output/"report.json"),"?"+"&".join(query)])
        return checked.returncode
    if args.general_finite_graphics_content_root:
        return 0 if validate_finite_graphics_root_report(result, args.general_content_buffer_type or "uniform",
            args.general_source_whole, args.general_content_plan_dynamic,
            args.general_graphics_root_terminal or "abandon", args.general_graphics_shader_content, args.general_graphics_native_custody, args.general_graphics_dependency_freeze, args.general_graphics_native_payload, args.general_graphics_native_cold, args.general_graphics_native_command, args.general_graphics_native_callback) else 1
    finite_native_evidence_matches = True
    if args.general_finite_lifecycle or args.general_finite_execution:
        selectors = (("claim_terminal", args.general_claim_terminal),
                     ("recording_terminal", args.general_recording_terminal),
                     ("submission_terminal", args.general_submission_terminal),
                     ("physical_loss", args.general_physical_loss),
                     ("map_terminal", args.general_map_terminal),
                     ("post_suppression", args.general_post_suppression_failure))
        group, terminal = ("execution", None) if args.general_finite_execution else next(
            (group, terminal) for group, terminal in selectors if terminal)
        identity = (group, args.general_content_buffer_type or "uniform",
                    "whole" if args.general_source_whole else "dynamic" if args.general_content_plan_dynamic else "static", terminal)
        try:
            inspect_finite_report(result, identity)
        except (ValueError, TypeError, KeyError) as error:
            print(str(error), file=sys.stderr)
            finite_native_evidence_matches = False
    return 0 if result.get("success") and finite_native_evidence_matches and finite_claim_lifecycle_matches and finite_map_lifecycle_matches and finite_loss_lifecycle_matches and finite_submission_lifecycle_matches and finite_lifecycle_matches and finite_execution_matches and finite_recording_matches and finite_payload_matches and finite_closure_matches and content_plan_matches and scope_mode_matches and usage_mode_matches and general_mode_matches and finite_mode_matches and finite_source_matches and finite_content_matches and initialization_terminal_matches and terminal_mode_matches and submission_terminal_matches and submission_probe_matches and physical_mode_matches and map_mode_matches and texture_mode_matches else 1


if __name__ == "__main__":
    raise SystemExit(main())
