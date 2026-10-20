"""Copy and independently replay an exact finite Compute predecessor cohort.

The cohort is a projection of genuine isolated cases in a fully audited stage,
not a new GPU run or an aggregate acceptance. Shader publications/payloads must
be archived. Final reruns cannot be reused as their own predecessors.
"""
import argparse
import importlib
import json
import pathlib
import shutil
import sys

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, read_json, verify_file
from bindless_finite_lifecycle_evidence import require_fields
from bindless_finite_lifecycle_predecessor_evidence import COHORTS, inspect_audited_stage

CONTRACT = "FiniteLifecycleCohortArchive/1"
LEGACY_REPLAY_TOOLS = ("bindless_finite_lifecycle_cohort_archive", "bindless_finite_lifecycle_cohort_archive_test")


def replay_tools(stage_root):
    contract = read_json(stage_root / "validation.json").get("contract")
    if contract == "FrozenReferenceFiniteStageArchive/1":
        from bindless_frozen_reference_stage_archive import TOOLS
        return (*TOOLS, "bindless_finite_lifecycle_predecessor_evidence", *LEGACY_REPLAY_TOOLS,
                "bindless_codegen", "slang_resource_switch")
    return LEGACY_REPLAY_TOOLS


def expected_cohort(cohort):
    if cohort not in COHORTS:
        raise ValueError("FINITE-COHORT-IDENTITY")
    return {name: identity for name, identity in expected_cases("compute-final").items()
            if identity[0] in COHORTS[cohort]}


def inspect_source(stage_root, cohort):
    expected = expected_cohort(cohort)
    audit = read_json(stage_root / "validation.json")
    if audit.get("contract") not in ("FiniteLifecycleStageAudit/1", "FrozenReferenceFiniteStageArchive/1"):
        # Keep the independent post-suppression probe/build path lazy. A frozen
        # reference cohort needs no unrelated live probe/build modules to replay.
        from bindless_webgpu_post_suppression_archive_audit import (
            CONTRACT as POST_CONTRACT, replay as inspect_post_suppression_audit)
        if audit.get("contract") != POST_CONTRACT:
            raise ValueError("FINITE-COHORT-SOURCE-CONTRACT")
        if cohort != "PostSuppression4":
            raise ValueError("FINITE-COHORT-MISSING-CASES")
        inventory = inspect_post_suppression_audit(stage_root, pathlib.Path(audit["buildLogSource"]),
            pathlib.Path("C:/mingw64/bin/ninja.exe"), pathlib.Path("C:/emsdk/upstream/bin/llvm-nm.exe"))
        if set(inventory["semanticProfiles"]) != set(expected):
            raise ValueError("FINITE-COHORT-MISSING-CASES")
        return dict(cohort=cohort, cases=len(expected),
            identities={name: list(identity) for name, identity in sorted(expected.items())},
            semanticProfiles=inventory["semanticProfiles"], packageIdentities=inventory["packageIdentities"],
            stage="post-suppression", stageAudit=dict(path=str(stage_root / "validation.json"),
                **fingerprint(stage_root / "validation.json")),
            sourceAcceptance=dict(source=audit["sourceAcceptance"]["path"],
                **{key: audit["sourceAcceptance"][key] for key in ("bytes", "sha256")}),
            sourceArtifacts=inventory["selectedArtifactsVerified"])
    inventory = inspect_audited_stage(stage_root)
    if not inventory["shaderPackagesBound"] or not inventory["eligibleForAggregatePredecessor"]:
        raise ValueError("FINITE-COHORT-PACKAGE-PROVENANCE")
    require_fields(audit, dict(shaderMetadataFrozen=True))
    # This tool preserves exact projections; it never manufactures absent cases.
    if inventory["cohorts"][cohort] != sorted(expected):
        raise ValueError("FINITE-COHORT-MISSING-CASES")
    profiles = {name: audit["semanticProfiles"][name] for name in sorted(expected)}
    acceptance = []
    for entry in audit["artifacts"]:
        if pathlib.Path(entry["source"]).name == "validation.json":
            candidate = read_json(verify_file(stage_root, entry))
            if (candidate.get("contract") == "FiniteLifecycleAcceptance/1"
                    and candidate.get("stage") == inventory["stage"]):
                acceptance.append(dict(source=entry["source"], bytes=entry["bytes"], sha256=entry["sha256"]))
    if len(acceptance) != 1:
        raise ValueError("FINITE-COHORT-ACCEPTANCE-OWNER")
    return dict(cohort=cohort, cases=len(expected),
                identities={name: list(identity) for name, identity in sorted(expected.items())},
                semanticProfiles=profiles, packageIdentities=inventory["packageIdentities"],
                stage=inventory["stage"], stageAudit=inventory["audit"],
                sourceAcceptance=acceptance[0], sourceArtifacts=inventory["selectedArtifactsVerified"])


def inspected_files(stage_root):
    audit_path = stage_root / "validation.json"
    files = {audit_path: fingerprint(audit_path)}
    for entry in read_json(audit_path)["artifacts"]:
        path = verify_file(stage_root, entry)
        files[path] = dict(bytes=entry["bytes"], sha256=entry["sha256"])
    return files


def replay(root, final_stage_audit=None):
    root = root.resolve()
    path = root / "validation.json"
    stamp = fingerprint(path)
    archive = read_json(path)
    require_fields(archive, dict(contract=CONTRACT, complete=True,
        scope="private finite Compute predecessor evidence projection only",
        aggregatePrerequisitesValidated=False, affectedRegressionsValidated=False,
        finalPayloadRerunValidated=False, actualProbeBuildEvidenceValidated=False, **CLOSED_GATES))
    artifacts = archive.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        raise ValueError("FINITE-COHORT-ARTIFACTS")
    require_fields(archive, dict(artifactCount=len(artifacts)))
    sources, paths = set(), set()
    for entry in artifacts:
        source = entry.get("source")
        if not isinstance(source, str):
            raise ValueError("FINITE-COHORT-ARTIFACT-SOURCE")
        source = pathlib.Path(source).resolve()
        archived = verify_file(root, entry)
        if source in sources or archived in paths:
            raise ValueError("FINITE-COHORT-DUPLICATE-ARTIFACT")
        sources.add(source); paths.add(archived)
    # No live original path is consulted. Re-run the existing stage archive,
    # original native marker checker and authoritative shader-publication reader.
    stage_root = root / "stage"
    expected_files = inspected_files(stage_root)
    names = replay_tools(stage_root)
    if "replayTools" in archive and archive["replayTools"] != list(names):
        raise ValueError("FINITE-COHORT-REPLAY-TOOLS")
    if len(names) != len(LEGACY_REPLAY_TOOLS) and "replayTools" not in archive:
        raise ValueError("FINITE-COHORT-REPLAY-TOOLS")
    tool_paths = {root / "tools" / (name + ".py") for name in names}
    if set(expected_files) | tool_paths != paths:
        raise ValueError("FINITE-COHORT-MISSING-STAGE-ARTIFACT")
    projection = inspect_source(stage_root, archive.get("cohort"))
    for key, value in projection.items():
        if key == "stageAudit":
            # The parent audit is relocated; exact bytes and SHA remain binding.
            require_fields(archive.get(key, {}), {name: value[name] for name in ("bytes", "sha256")})
        else:
            require_fields(archive, {key: value})
    if final_stage_audit is not None:
        final_inventory = inspect_audited_stage(final_stage_audit.resolve())
        if final_inventory["stage"] != "compute-final":
            raise ValueError("FINITE-COHORT-FINAL-STAGE")
        final = inspect_source(final_stage_audit.resolve(), archive["cohort"])
        if (final["stageAudit"]["sha256"] == projection["stageAudit"]["sha256"]
                or final["sourceAcceptance"]["sha256"] == projection["sourceAcceptance"]["sha256"]
                or pathlib.Path(final["sourceAcceptance"]["source"]).resolve()
                    == pathlib.Path(projection["sourceAcceptance"]["source"]).resolve()):
            raise ValueError("FINITE-COHORT-SELF-PREDECESSOR")
        if (final["semanticProfiles"] != projection["semanticProfiles"]
                or final["packageIdentities"] != projection["packageIdentities"]):
            raise ValueError("FINITE-COHORT-FINAL-PROFILE")
    if fingerprint(path) != stamp or any(
            fingerprint(verify_file(root, entry)) != dict(bytes=entry["bytes"], sha256=entry["sha256"])
            for entry in artifacts):
        raise ValueError("FINITE-COHORT-CHANGED-DURING-REPLAY")
    return dict(contract=CONTRACT, complete=True, cohort=projection["cohort"], cases=projection["cases"],
                selectedArtifactsVerified=len(artifacts), shaderPackagesBound=True,
                finalCohortProfileCompared=final_stage_audit is not None,
                aggregatePrerequisitesValidated=False, affectedRegressionsValidated=False,
                finalPayloadRerunValidated=False, actualProbeBuildEvidenceValidated=False, **CLOSED_GATES)


def freeze(stage_root, cohort, output):
    stage_root = stage_root.resolve(); output = output.resolve()
    if output == stage_root or output.is_relative_to(stage_root) or stage_root.is_relative_to(output):
        raise ValueError("FINITE-COHORT-OUTPUT-OVERLAP")
    projection = inspect_source(stage_root, cohort)
    files = inspected_files(stage_root)
    output.mkdir(parents=True, exist_ok=False)
    (output / "attempt.json").write_text(json.dumps(dict(complete=False, **CLOSED_GATES), indent=2), encoding="utf-8")
    artifacts = []
    # Preserve the stage's internal relative layout, including its own copied
    # acceptance. The replay continues to work after original archives disappear.
    for source, expected in sorted(files.items()):
        destination = output / "stage" / source.relative_to(stage_root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        if fingerprint(destination) != expected:
            raise ValueError("FINITE-COHORT-CHANGED-COPY")
        artifacts.append(dict(source=str(source), path=str(destination.relative_to(output)), **expected))
    names = replay_tools(stage_root)
    for name in names:
        source = (pathlib.Path(__file__).with_name(name + ".py") if name.endswith("_test")
                  else pathlib.Path(importlib.import_module(name).__file__).resolve())
        expected = fingerprint(source)
        destination = output / "tools" / source.name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, destination)
        if fingerprint(destination) != expected or fingerprint(source) != expected:
            raise ValueError("FINITE-COHORT-CHANGED-TOOL")
        artifacts.append(dict(source=str(source), path=str(destination.relative_to(output)), **expected))
    if any(fingerprint(source) != expected for source, expected in files.items()):
        raise ValueError("FINITE-COHORT-CHANGED-SOURCE")
    manifest = dict(contract=CONTRACT, complete=True,
        scope="private finite Compute predecessor evidence projection only", **projection,
        artifacts=artifacts, artifactCount=len(artifacts), aggregatePrerequisitesValidated=False,
        affectedRegressionsValidated=False, finalPayloadRerunValidated=False,
        actualProbeBuildEvidenceValidated=False, **CLOSED_GATES)
    if len(names) != len(LEGACY_REPLAY_TOOLS):
        manifest["replayTools"] = list(names)
    path = output / "validation.json"
    path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    try:
        return replay(output)
    except (ValueError, OSError, KeyError, TypeError):
        # Keep the failed candidate for diagnosis without a success filename.
        path.rename(output / "rejected.json")
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    create = actions.add_parser("freeze")
    create.add_argument("--stage-audit", type=pathlib.Path, required=True)
    create.add_argument("--cohort", choices=tuple(COHORTS), required=True)
    create.add_argument("--output", type=pathlib.Path, required=True)
    inspect = actions.add_parser("replay")
    inspect.add_argument("--archive", type=pathlib.Path, required=True)
    inspect.add_argument("--final-stage-audit", type=pathlib.Path)
    args = parser.parse_args()
    try:
        sys.modules.setdefault("bindless_finite_lifecycle_cohort_archive", sys.modules[__name__])
        result = (freeze(args.stage_audit, args.cohort, args.output) if args.action == "freeze"
                  else replay(args.archive, args.final_stage_audit))
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
