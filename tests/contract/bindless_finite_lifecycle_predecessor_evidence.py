"""Replay copied stage evidence and bind driver metadata to archived publications.

This is an available-evidence inventory, not the final Lifecycle4 aggregate.
Missing predecessors or incomplete shader provenance keep every gate closed.
"""
import argparse
import json
import pathlib
import shutil
import sys
import tempfile

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import (CLOSED_GATES, read_json, verify_file,
                                                  inspect_snapshot, ROOT)
from bindless_finite_lifecycle_evidence import require_fields

sys.path.insert(0, str(ROOT / "scripts"))
from bindless_codegen import read_bindless_publication

PACKAGES = {
    "WebGpuPublicCompute.emulated.bindless.yaml": "publicComputePackageIdentity",
    "WebGpuPublicGraphics.emulated.bindless.yaml": "publicGraphicsPackageIdentity",
    "ArpZeroBaseIndirectProducer.emulated.bindless.yaml": "preparedZeroBaseProducerPackageIdentity",
    "BindlessIndirectConsumer.emulated.bindless.yaml": "processorIntentConsumerPackageIdentity",
}
COHORTS = {"Execution7": ("execution", "physical_loss", "map_terminal"),
           "Recording4": ("recording_terminal",), "Submission4": ("submission_terminal",),
           "Claim4": ("claim_terminal",), "PostSuppression4": ("post_suppression",)}


def bind_packages(snapshot_root, manifest):
    inputs = manifest["inputs"]
    by_source = {pathlib.Path(entry["source"]).resolve(): verify_file(snapshot_root, entry) for entry in inputs}
    publications = {}
    for filename, report_field in PACKAGES.items():
        matches = [source for source in by_source if source.name == filename]
        if not matches:
            return None
        if len(matches) != 1:
            raise ValueError("FINITE-PREDECESSOR-DUPLICATE-PACKAGE")
        source = matches[0]
        # Use the authoritative reader on the archived publication directory.
        # Original host files are never used to fill a missing archive member.
        with tempfile.TemporaryDirectory(prefix="arp-finite-package-replay-") as directory:
            parent = pathlib.Path(directory)
            for sibling, archived in by_source.items():
                if sibling.parent == source.parent:
                    shutil.copyfile(archived, parent / sibling.name)
            package = read_bindless_publication(parent / source.name)
        identity = package["shaderPackageIdentity"]
        for record in manifest["results"]:
            report = read_json(verify_file(snapshot_root, record["report"]))
            require_fields(report, {report_field: identity})
        publications[filename] = identity
    return publications


def verify_reference_publication_reader(snapshot_root, materialized_root):
    """The loaded reader cannot replace the attempt's frozen compiler helper."""
    from bindless_frozen_reference_archive_audit import FrozenReferenceSourceAudit
    sources = FrozenReferenceSourceAudit.index_inputs(snapshot_root, read_json(snapshot_root / "inputs.json"))
    stamps = {}
    for name in ("bindless_codegen", "slang_resource_switch"):
        loaded = pathlib.Path(sys.modules[name].__file__).resolve()
        archived = sources.get(materialized_root / "scripts" / (name + ".py"))
        if archived is None or fingerprint(loaded) != fingerprint(archived):
            raise ValueError("FINITE-PREDECESSOR-FROZEN-READER-IDENTITY")
        stamps[name] = fingerprint(archived)
    return stamps


def inspect_reference_stage(root):
    from bindless_frozen_reference_stage_archive import FrozenReferenceStageArchive, STAGE_CONTRACT
    path = root / "validation.json"
    stamp = fingerprint(path)
    inventory = FrozenReferenceStageArchive.replay(root)
    if inventory["contract"] != STAGE_CONTRACT or inventory["nativeAcceptanceInspected"] is not True:
        raise ValueError("FINITE-PREDECESSOR-REFERENCE-NATIVE-STAGE-MISSING")
    audit = read_json(path)
    snapshot = root / "snapshot"
    manifest = read_json(snapshot / "validation.json")
    reader_stamps = verify_reference_publication_reader(snapshot,
        pathlib.Path(audit["mapping"]["mappedRoot"]).resolve())
    packages = bind_packages(snapshot, manifest)
    cases = expected_cases(inventory["stage"])
    cohorts = {cohort: sorted(name for name, identity in cases.items() if identity[0] in groups)
               for cohort, groups in COHORTS.items()}
    if fingerprint(path) != stamp:
        raise ValueError("FINITE-PREDECESSOR-CHANGED-AUDIT")
    # Replay again after publication inspection, preserving all input/report
    # hashes and closed gates rather than trusting the authored eligibility bit.
    if FrozenReferenceStageArchive.replay(root) != inventory:
        raise ValueError("FINITE-PREDECESSOR-CHANGED-AUDIT")
    return dict(contract="FiniteLifecyclePredecessorEvidenceInventory/1", complete=False,
        stage=inventory["stage"], audit=dict(path=str(path), **stamp), cases=len(cases),
        cohorts=cohorts, packageIdentities=packages or {}, shaderPackagesBound=packages is not None,
        eligibleForAggregatePredecessor=packages is not None,
        selectedArtifactsVerified=inventory["selectedArtifactsVerified"],
        sourceRootMappingVerified=True, frozenPublicationReaderVerified=True,
        publicationReaderSources=reader_stamps, aggregatePrerequisitesValidated=False,
        finalPayloadRerunValidated=False, **CLOSED_GATES)


def inspect_audited_stage(root):
    root = root.resolve()
    path = root / "validation.json"
    audit_stamp = fingerprint(path)
    audit = read_json(path)
    if audit.get("contract") in ("FrozenReferenceNamespaceArchive/1", "FrozenReferenceFiniteStageArchive/1"):
        return inspect_reference_stage(root)
    require_fields(audit, dict(contract="FiniteLifecycleStageAudit/1", complete=True,
        aggregatePrerequisitesValidated=False, transitiveSdkProvenanceComplete=False, **CLOSED_GATES))
    stage = audit.get("stage")
    cases = expected_cases(stage)
    require_fields(audit, dict(cases=len(cases)))
    artifacts = audit.get("artifacts")
    if not isinstance(artifacts, list):
        raise ValueError("FINITE-PREDECESSOR-ARTIFACTS")
    require_fields(audit, dict(artifactCount=len(artifacts)))
    sources = {}
    copied = set()
    candidates = []
    for entry in artifacts:
        archived = verify_file(root, entry)
        source = pathlib.Path(entry["source"]).resolve()
        if source in sources or archived in copied:
            raise ValueError("FINITE-PREDECESSOR-DUPLICATE-ARTIFACT")
        sources[source] = archived
        copied.add(archived)
        if source.name == "validation.json":
            candidate = read_json(archived)
            if candidate.get("contract") == "FiniteLifecycleAcceptance/1" and candidate.get("stage") == stage:
                candidates.append(source)
    if len(candidates) != 1:
        raise ValueError("FINITE-PREDECESSOR-PARENT")
    parent = candidates[0].parent
    with tempfile.TemporaryDirectory(prefix="arp-finite-stage-replay-") as directory:
        replay = pathlib.Path(directory)
        for source, archived in sources.items():
            if source.is_relative_to(parent):
                destination = replay / source.relative_to(parent)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(archived, destination)
        inspector = inspect_snapshot
        if stage == "compute-final":
            # The complete frozen final cache owns source/build labels. A
            # relocated reader's __file__ is not the historical source root.
            from bindless_webgpu_probe_archive_audit import cache_directory
            import types
            acceptance = read_json(replay / "validation.json")
            caches = [row for row in acceptance["inputs"] if pathlib.Path(row["source"]).name == "CMakeCache.txt"]
            if len(caches) != 1:
                raise ValueError("FINITE-PREDECESSOR-FINAL-CACHE")
            text = verify_file(replay, caches[0]).read_text(encoding="utf-8")
            source_root = cache_directory(text, "CMAKE_HOME_DIRECTORY", "FINITE-PREDECESSOR-FINAL-SOURCE-OWNER")
            build_root = cache_directory(text, "CMAKE_CACHEFILE_DIR", "FINITE-PREDECESSOR-FINAL-BUILD-OWNER")
            if pathlib.Path(caches[0]["source"]).resolve() != build_root / "CMakeCache.txt":
                raise ValueError("FINITE-PREDECESSOR-FINAL-CACHE-OWNER")
            globals_ = dict(inspect_snapshot.__globals__, ROOT=source_root)
            inspector = types.FunctionType(inspect_snapshot.__code__, globals_,
                inspect_snapshot.__name__, inspect_snapshot.__defaults__, inspect_snapshot.__closure__)
        inspected = inspector(replay)
        if inspected["profiles"] != audit.get("semanticProfiles"):
            raise ValueError("FINITE-PREDECESSOR-SEMANTIC-PROFILE")
        require_fields(audit, dict(shaderMetadataFrozen=inspected["shaderMetadataFrozen"]))
        manifest = read_json(replay / "validation.json")
        packages = bind_packages(replay, manifest)
        input_identities = {str(pathlib.Path(row["source"]).resolve()):
            {key: row[key] for key in ("bytes", "sha256")} for row in manifest["inputs"]}
    if fingerprint(path) != audit_stamp or any(
            fingerprint(verify_file(root, entry)) != dict(bytes=entry["bytes"], sha256=entry["sha256"])
            for entry in artifacts):
        raise ValueError("FINITE-PREDECESSOR-CHANGED-AUDIT")
    # The selected cohorts are projections of isolated genuine browser cases,
    # never extra GPU invocations or separate execution authorization.
    cohorts = {cohort: sorted(name for name, identity in cases.items() if identity[0] in groups)
               for cohort, groups in COHORTS.items()}
    return dict(contract="FiniteLifecyclePredecessorEvidenceInventory/1", complete=False,
        stage=stage, audit=dict(path=str(path), **audit_stamp),
        cases=len(cases), cohorts=cohorts, packageIdentities=packages or {},
        shaderPackagesBound=packages is not None,
        eligibleForAggregatePredecessor=packages is not None,
        frozenInputIdentities=input_identities,
        selectedArtifactsVerified=len(artifacts), aggregatePrerequisitesValidated=False,
        finalPayloadRerunValidated=False, **CLOSED_GATES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage-audit", type=pathlib.Path, required=True)
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    try:
        result = inspect_audited_stage(args.stage_audit)
        if args.report:
            # Inventory is diagnostic only; never overwrite prior observations.
            with args.report.open("x", encoding="utf-8") as output:
                json.dump(result, output, indent=2)
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
