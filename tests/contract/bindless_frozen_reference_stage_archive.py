"""Freeze/replay proven reference inputs or a complete private Compute stage.

An inputs-only archive has no native reports. Complete-stage creation requires
the canonical complete acceptance inspection. Replay reads archived files alone
and neither path publishes aggregate, SDK or public execution readiness.
"""
import argparse
import json
import pathlib
import shutil
import sys
import types

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import (
    CLOSED_GATES, inspect_snapshot, read_json, verify_file)
from bindless_finite_lifecycle_evidence import require_fields
from bindless_frozen_reference_archive_audit import (
    FrozenReferenceSourceAudit, REQUIRED_SOURCES, VALIDATORS)

NAMESPACE_CONTRACT = "FrozenReferenceNamespaceArchive/1"
STAGE_CONTRACT = "FrozenReferenceFiniteStageArchive/1"
TOOLS = (*VALIDATORS, "bindless_frozen_reference_archive_audit", "bindless_frozen_reference_stage_archive")


class FrozenReferenceStageArchive:
    @staticmethod
    def logical_path(value):
        if not isinstance(value, str) or not pathlib.Path(value).is_absolute():
            raise ValueError("FINITE-REFERENCE-ARCHIVE-LOGICAL-PATH")
        return pathlib.Path(value).resolve()

    @staticmethod
    def replay(root):
        root = root.resolve()
        manifest_path = root / "validation.json"
        stamp = fingerprint(manifest_path)
        manifest = read_json(manifest_path)
        contract = manifest.get("contract")
        if contract not in (NAMESPACE_CONTRACT, STAGE_CONTRACT):
            raise ValueError("FINITE-REFERENCE-ARCHIVE-CONTRACT")
        native = contract == STAGE_CONTRACT
        require_fields(manifest, dict(complete=True,
            scope="private frozen-reference Compute selected evidence only",
            nativeAcceptanceInspected=native, aggregatePrerequisitesValidated=False,
            ordinaryComputePredecessorEligible=False, eligibleForAggregatePredecessor=False,
            transitiveSdkProvenanceComplete=False, **CLOSED_GATES))
        artifacts = manifest.get("artifacts")
        if not isinstance(artifacts, list) or not artifacts:
            raise ValueError("FINITE-REFERENCE-ARCHIVE-ARTIFACTS")
        require_fields(manifest, dict(artifactCount=len(artifacts)))
        paths, sources = {}, set()
        for entry in artifacts:
            path = verify_file(root, entry)
            source = FrozenReferenceStageArchive.logical_path(entry.get("source"))
            if path in paths or source in sources:
                raise ValueError("FINITE-REFERENCE-ARCHIVE-DUPLICATE-ARTIFACT")
            paths[path] = entry
            sources.add(source)
        mapping = manifest.get("mapping")
        if not isinstance(mapping, dict):
            raise ValueError("FINITE-REFERENCE-ARCHIVE-MAPPING")
        require_fields(mapping, dict(contract="FrozenReferenceSourceRootMapping/1", complete=False,
            sourceRootMappingVerified=True, nativeAcceptanceInspected=False,
            exactRegisteredSelectorsAndDeadlinesPreserved=True, aggregatePrerequisitesValidated=False,
            ordinaryComputePredecessorEligible=False, transitiveSdkProvenanceComplete=False, **CLOSED_GATES))
        original_root = FrozenReferenceStageArchive.logical_path(mapping.get("sourceRoot"))
        materialized = FrozenReferenceStageArchive.logical_path(mapping.get("mappedRoot"))
        reference = FrozenReferenceStageArchive.logical_path(manifest.get("referenceRoot"))
        owner = FrozenReferenceStageArchive.logical_path(manifest.get("snapshotSource"))
        roots = (owner, reference, materialized, original_root)
        if any(a == b or a.is_relative_to(b) or b.is_relative_to(a)
               for index, a in enumerate(roots[:3]) for b in roots[index + 1:]
               if not (a == reference and b == original_root)):
            raise ValueError("FINITE-REFERENCE-ARCHIVE-OWNER")
        snapshot = root / "snapshot"
        inputs = read_json(snapshot / "inputs.json")
        attempt_sources = FrozenReferenceSourceAudit.index_inputs(snapshot, inputs)
        expected_paths = set(attempt_sources.values())
        expected_paths.update(snapshot / name for name in ("inputs.json", "preflight.json", "tests.json"))

        def archived(source):
            path = attempt_sources.get(source)
            if path is None:
                raise ValueError("FINITE-REFERENCE-ARCHIVE-MISSING-SOURCE")
            return path

        materialization = read_json(archived(materialized / "reference-manifest.json"))
        require_fields(materialization, dict(contract="FrozenInputReferenceMaterialization/1",
            reference=str(reference), sourceRoot=str(original_root),
            publicBindlessExecutionAvailable=False, ordinaryComputePredecessorEligible=False,
            platformValidated=False, productionReady=False))
        original = read_json(archived(reference / "inputs.json"))
        original_sources = FrozenReferenceSourceAudit.index_inputs(root / "reference", original)
        expected_paths.update(original_sources.values())
        available, proof_files = FrozenReferenceSourceAudit.inspect_copy_proof(original_root,
            materialized, original_sources, attempt_sources, materialization.get("inputs"), attempt_sources)
        if not {materialized / name for name in REQUIRED_SOURCES} <= available:
            raise ValueError("FINITE-REFERENCE-ARCHIVE-REQUIRED-SOURCES")
        require_fields(materialization, dict(copiedInputs=len(original)))
        preflight = read_json(snapshot / "preflight.json")
        stage = mapping.get("stage")
        cases = expected_cases(stage)
        require_fields(preflight, dict(contract="FrozenReferenceFinitePreflight/1", complete=False,
            stage=stage, expected=len(cases), materializedInputsVerified=len(original),
            exactRegisteredSelectorsAndDeadlinesPreserved=True,
            aggregatePrerequisitesValidated=False, ordinaryComputePredecessorEligible=False,
            transitiveSdkProvenanceComplete=False, **CLOSED_GATES))
        require_fields(mapping, dict(materializedInputsVerified=len(original),
            attemptInputsVerified=len(inputs), registeredCasesVerified=len(cases)))
        registered = archived(reference / "tests.json")
        require_fields(preflight.get("registrationSource", {}), dict(
            source=str(reference / "tests.json"), **fingerprint(registered)))
        FrozenReferenceSourceAudit.inspect_registration(read_json(registered).get("tests"),
            read_json(snapshot / "tests.json").get("tests"), cases,
            original_root, materialized, available, owner)
        validator_sources = manifest.get("validatorSources")
        if not isinstance(validator_sources, dict) or set(validator_sources) != set(VALIDATORS):
            raise ValueError("FINITE-REFERENCE-ARCHIVE-VALIDATOR-SOURCES")
        tool_sources = manifest.get("toolSources")
        if not isinstance(tool_sources, dict) or set(tool_sources) != set(TOOLS):
            raise ValueError("FINITE-REFERENCE-ARCHIVE-TOOL-SOURCES")
        origins = {}
        for name in TOOLS:
            path = root / "tools" / (name + ".py")
            expected_paths.add(path)
            origin = FrozenReferenceStageArchive.logical_path(tool_sources[name])
            if origin.name != name + ".py":
                raise ValueError("FINITE-REFERENCE-ARCHIVE-TOOL-SOURCES")
            origins[path] = origin
            loaded = pathlib.Path(sys.modules[name].__file__).resolve()
            if path not in paths or fingerprint(loaded) != fingerprint(path):
                raise ValueError("FINITE-REFERENCE-ARCHIVE-LOADED-TOOL")
            if name in VALIDATORS:
                source = FrozenReferenceStageArchive.logical_path(validator_sources[name])
                if source != origin or source.name != name + ".py" or fingerprint(archived(source)) != fingerprint(path):
                    raise ValueError("FINITE-REFERENCE-ARCHIVE-VALIDATOR-IDENTITY")
        profiles = {}
        if native:
            acceptance = read_json(snapshot / "validation.json")
            require_fields(acceptance, dict(frozenReferenceAttempt=True,
                ordinaryComputePredecessorEligible=False, stage=stage))
            globals_ = dict(inspect_snapshot.__globals__, ROOT=materialized)
            inspector = types.FunctionType(inspect_snapshot.__code__, globals_,
                inspect_snapshot.__name__, inspect_snapshot.__defaults__, inspect_snapshot.__closure__)
            inspected = inspector(snapshot)
            expected_paths.update(inspected["files"])
            profiles = inspected["profiles"]
            if profiles != manifest.get("semanticProfiles"):
                raise ValueError("FINITE-REFERENCE-ARCHIVE-SEMANTIC-PROFILES")
            require_fields(manifest, dict(shaderMetadataFrozen=inspected["shaderMetadataFrozen"]))
        else:
            require_fields(manifest, dict(semanticProfiles={}, shaderMetadataFrozen=False))
            if (snapshot / "validation.json").exists() or (snapshot / "attempt.json").exists():
                raise ValueError("FINITE-REFERENCE-ARCHIVE-UNDECLARED-NATIVE-STAGE")
        if set(paths) != expected_paths:
            raise ValueError("FINITE-REFERENCE-ARCHIVE-CLOSURE")
        origins.update({path: owner / path.relative_to(snapshot)
                        for path in expected_paths if path.is_relative_to(snapshot)})
        origins.update({path: reference / path.relative_to(root / "reference")
                        for path in original_sources.values()})
        if any(FrozenReferenceStageArchive.logical_path(entry["source"]) != origins[path]
               for path, entry in paths.items()):
            raise ValueError("FINITE-REFERENCE-ARCHIVE-SOURCE-IDENTITY")
        if fingerprint(manifest_path) != stamp or any(
                fingerprint(path) != dict(bytes=entry["bytes"], sha256=entry["sha256"])
                for path, entry in paths.items()) or any(
                fingerprint(path) != expected for path, expected in proof_files.items()):
            raise ValueError("FINITE-REFERENCE-ARCHIVE-CHANGED-DURING-REPLAY")
        return dict(contract=contract, complete=True, stage=stage,
            selectedArtifactsVerified=len(paths), materializedInputsVerified=len(original),
            attemptInputsVerified=len(inputs), registeredCasesVerified=len(cases),
            nativeAcceptanceInspected=native, nativeCasesInspected=len(profiles),
            sourceRootMappingVerified=True, independentArchivedReplay=True,
            aggregatePrerequisitesValidated=False, ordinaryComputePredecessorEligible=False,
            eligibleForAggregatePredecessor=False, transitiveSdkProvenanceComplete=False, **CLOSED_GATES)

    @staticmethod
    def freeze(snapshot, reference, materialized, output, include_stage=False):
        audit = FrozenReferenceSourceAudit(snapshot, reference, materialized)
        inspection = audit.inspect() if include_stage else audit.prepare()
        mapping = audit.mapping
        output = output.resolve()
        owners = (audit.snapshot, audit.reference, audit.materialized,
                  pathlib.Path(mapping["sourceRoot"]))
        if any(output == owner or output.is_relative_to(owner) or owner.is_relative_to(output)
               for owner in owners):
            raise ValueError("FINITE-REFERENCE-ARCHIVE-OUTPUT-OWNER")
        original = read_json(audit.archived(audit.reference / "inputs.json"))
        files = {path: fingerprint(path) for path in audit.sources.values()}
        files.update({audit.snapshot / name: fingerprint(audit.snapshot / name)
                      for name in ("inputs.json", "preflight.json", "tests.json")})
        if include_stage:
            files.update(audit.inspected["files"])
        destinations = {source: "snapshot/" + source.relative_to(audit.snapshot).as_posix()
                        for source in files}
        for entry in original:
            source = verify_file(audit.reference, entry)
            files[source] = fingerprint(source)
            destinations[source] = "reference/" + source.relative_to(audit.reference).as_posix()
        for name in TOOLS:
            source = pathlib.Path(sys.modules[name].__file__).resolve()
            files[source] = fingerprint(source)
            destinations[source] = "tools/" + source.name
        output.mkdir(parents=True, exist_ok=False)
        (output / "attempt.json").write_text(json.dumps(dict(complete=False, **CLOSED_GATES), indent=2),
                                            encoding="utf-8")
        artifacts = []
        for source, expected in sorted(files.items()):
            destination = output / destinations[source]
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            row = dict(source=str(source), path=destinations[source], **expected)
            verify_file(output, row)
            artifacts.append(row)
        audit.verify_unchanged()
        if any(fingerprint(source) != expected for source, expected in files.items()):
            raise ValueError("FINITE-REFERENCE-ARCHIVE-CHANGED-DURING-FREEZE")
        manifest = dict(contract=STAGE_CONTRACT if include_stage else NAMESPACE_CONTRACT,
            complete=True, scope="private frozen-reference Compute selected evidence only",
            mapping=mapping, referenceRoot=str(audit.reference), snapshotSource=str(audit.snapshot),
            artifacts=artifacts, artifactCount=len(artifacts), nativeAcceptanceInspected=include_stage,
            semanticProfiles=inspection.get("semanticProfiles", {}),
            shaderMetadataFrozen=inspection.get("shaderMetadataFrozen", False),
            validatorSources={name: str(pathlib.Path(sys.modules[name].__file__).resolve()) for name in VALIDATORS},
            toolSources={name: str(pathlib.Path(sys.modules[name].__file__).resolve()) for name in TOOLS},
            aggregatePrerequisitesValidated=False, ordinaryComputePredecessorEligible=False,
            eligibleForAggregatePredecessor=False, transitiveSdkProvenanceComplete=False, **CLOSED_GATES)
        path = output / "validation.json"
        path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        try:
            return FrozenReferenceStageArchive.replay(output)
        except (ValueError, OSError, KeyError, TypeError):
            path.rename(output / "rejected.json")
            raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    create = actions.add_parser("freeze")
    create.add_argument("--snapshot", type=pathlib.Path, required=True)
    create.add_argument("--reference", type=pathlib.Path, required=True)
    create.add_argument("--materialized", type=pathlib.Path, required=True)
    create.add_argument("--output", type=pathlib.Path, required=True)
    create.add_argument("--include-stage", action="store_true")
    replay = actions.add_parser("replay")
    replay.add_argument("--archive", type=pathlib.Path, required=True)
    replay.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    try:
        # Script entrypoints use __main__, while copied-tool identity needs the
        # canonical module name. Preserve that same loaded object, not a reload.
        sys.modules.setdefault("bindless_frozen_reference_stage_archive", sys.modules[__name__])
        result = (FrozenReferenceStageArchive.freeze(args.snapshot, args.reference, args.materialized,
                    args.output, args.include_stage) if args.action == "freeze"
                  else FrozenReferenceStageArchive.replay(args.archive))
        if getattr(args, "report", None):
            with args.report.open("x", encoding="utf-8") as output:
                json.dump(result, output, indent=2)
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
