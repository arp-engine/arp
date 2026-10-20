"""Strict selected finite Compute acceptance; never authorize a device extension."""
import argparse
import json
import pathlib
import subprocess
import sys

from bindless_finite_lifecycle_acceptance import fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, read_json
from bindless_finite_lifecycle_cohort_archive import inspected_files
from bindless_finite_lifecycle_evidence import require_fields
from bindless_finite_lifecycle_predecessor_evidence import COHORTS, inspect_audited_stage
from bindless_finite_predecessor_set import FiniteLifecyclePredecessorSet
from bindless_webgpu_probe_archive_audit import artifact_inventory, configuration, replay as replay_probe
from bindless_webgpu_probe_build_evidence import TARGET, ORDINARY, MAIN, PROBE

CONTRACT = "FiniteComputeLifecycleAggregate/1"
SCOPE = "selected private finite Compute Lifecycle4 only"


def validate_report_owner(report, cohorts, final, probe):
    report = report.resolve()
    owners = [pathlib.Path(path).resolve() for path in cohorts.values()]
    owners += [pathlib.Path(path).resolve() for path in (final, probe) if path is not None]
    if any(report.is_relative_to(owner) for owner in owners):
        raise ValueError("FINITE-AGGREGATE-REPORT-OWNER")


def inspect_final_build(final, probe, log_source, ninja, nm):
    stage = inspect_audited_stage(final)
    require_fields(stage, dict(stage="compute-final", cases=210, shaderPackagesBound=True))
    built = replay_probe(probe, log_source, ninja, nm)
    require_fields(built, dict(complete=True, actualVerboseBuildEvidenceReplayed=True,
        originalBackendProbeDefinitionsAbsent=True, **CLOSED_GATES))
    sources = artifact_inventory(probe, read_json(probe / "validation.json"))
    inputs = stage["frozenInputIdentities"]
    caches = [source for source in sources if source.name == "CMakeCache.txt"]
    if len(caches) != 1:
        raise ValueError("FINITE-AGGREGATE-BUILD-CACHE")
    build = configuration(sources[caches[0]].read_text(encoding="utf-8"))
    source_root = pathlib.Path(built["logicalSourceRoot"]).resolve()
    payloads = {build / (TARGET + suffix) for suffix in (".js", ".wasm", ".data")}
    required = {source_root / name for name in ("CMakeLists.txt", ORDINARY, MAIN, PROBE,
        "tests/fixtures/webgpu_post_suppression_failure_probe_fixture.h")}
    required |= payloads | {caches[0]}
    if not required <= set(sources) or not {str(path) for path in required} <= set(inputs):
        raise ValueError("FINITE-AGGREGATE-BUILD-CORRESPONDENCE")
    shared = set(sources) & {pathlib.Path(source).resolve() for source in inputs}
    for source in shared:
        if inputs[str(source)] != fingerprint(sources[source]):
            raise ValueError("FINITE-AGGREGATE-BUILD-CORRESPONDENCE")
    for source in payloads:
        require_fields(built["payload"][source.name], inputs[str(source)])
    return dict(finalStage=stage["audit"], probeBuildReplay=built,
        sharedFrozenBuildInputsVerified=len(shared), packageIdentities=stage["packageIdentities"])


class FiniteComputeLifecycleAggregate:
    @staticmethod
    def inspect(cohorts, final, probe, log_source, ninja, nm, available_only=False):
        if not isinstance(cohorts, dict) or set(cohorts) - set(COHORTS):
            raise ValueError("FINITE-AGGREGATE-COHORT-NAME")
        cohorts = {name: pathlib.Path(path).resolve() for name, path in cohorts.items()}
        final = pathlib.Path(final).resolve() if final is not None else None
        probe = pathlib.Path(probe).resolve() if probe is not None else None
        owners = [*cohorts.values(), *(path for path in (final, probe) if path is not None)]
        if any(a == b or a.is_relative_to(b) or b.is_relative_to(a)
               for index, a in enumerate(owners) for b in owners[index + 1:]):
            raise ValueError("FINITE-AGGREGATE-INPUT-OWNER")
        missing = ["cohort:" + name for name in COHORTS if name not in cohorts]
        missing += [name for name, path in (("final", final), ("probe", probe))
                    if path is None or not (path / "validation.json").is_file()]
        if log_source is None:
            missing.append("build-log-source")
        if missing and not available_only:
            raise ValueError("FINITE-AGGREGATE-MISSING-INPUTS: " + ",".join(missing))
        held = {}
        for owner in owners:
            if (owner / "validation.json").is_file():
                held.update(inspected_files(owner))
        if missing:
            inventory = FiniteLifecyclePredecessorSet.inspect(cohorts)
            result = dict(contract=CONTRACT, scope=SCOPE, complete=False,
                missingInputs=missing, predecessorInventory=inventory,
                aggregatePrerequisitesValidated=False, affectedRegressionsValidated=False,
                transitiveSdkProvenanceComplete=False, **CLOSED_GATES)
        else:
            inventory = FiniteLifecyclePredecessorSet.inspect(cohorts, final)
            require_fields(inventory, dict(allRequiredCohortsPresent=True,
                exactCaseCoverageVerified=True, coveredCases=210, coveredTerminalCases=204,
                coveredNumericalCases=6, allCohortsComparedToIndependentFinal=True,
                aggregatePrerequisitesValidated=False, **CLOSED_GATES))
            build = inspect_final_build(final, probe, pathlib.Path(log_source), ninja, nm)
            if build["packageIdentities"] != inventory["packageIdentities"]:
                raise ValueError("FINITE-AGGREGATE-PACKAGE-MISMATCH")
            gates = dict(CLOSED_GATES, finiteLifecycle4Validated=not available_only)
            result = dict(contract=CONTRACT, scope=SCOPE, complete=not available_only,
                missingInputs=[], terminalCases=204, numericalCases=6,
                predecessorInventory=inventory, finalBuildCorrespondence=build,
                aggregatePrerequisitesValidated=not available_only,
                finalPayloadRerunValidated=not available_only,
                actualProbeBuildEvidenceValidated=True, affectedRegressionsValidated=False,
                transitiveSdkProvenanceComplete=False, **gates)
        if any(fingerprint(path) != stamp for path, stamp in held.items()):
            raise ValueError("FINITE-AGGREGATE-CHANGED-DURING-INSPECTION")
        # These identities bind existing immutable archive owners. This receipt
        # does not pretend to copy a new self-contained native/SDK archive.
        result["inputArtifacts"] = [dict(path=str(path), **stamp) for path, stamp in sorted(held.items())]
        result["selfContainedArchive"] = False
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cohort", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--final-stage-audit", type=pathlib.Path)
    parser.add_argument("--probe-archive", type=pathlib.Path)
    parser.add_argument("--build-log-source", type=pathlib.Path)
    parser.add_argument("--ninja", type=pathlib.Path, default=pathlib.Path("C:/mingw64/bin/ninja.exe"))
    parser.add_argument("--nm", type=pathlib.Path, default=pathlib.Path("C:/emsdk/upstream/bin/llvm-nm.exe"))
    parser.add_argument("--available-only", action="store_true")
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    try:
        cohorts = {}
        for declaration in args.cohort:
            name, separator, path = declaration.partition("=")
            if not separator or not path or name in cohorts:
                raise ValueError("FINITE-AGGREGATE-DECLARATION")
            cohorts[name] = pathlib.Path(path)
        if args.report:
            validate_report_owner(args.report, cohorts, args.final_stage_audit, args.probe_archive)
        result = FiniteComputeLifecycleAggregate.inspect(cohorts, args.final_stage_audit,
            args.probe_archive, args.build_log_source, args.ninja, args.nm, args.available_only)
        if args.report:
            with args.report.open("x", encoding="utf-8") as output:
                json.dump(result, output, indent=2)
        print(json.dumps({key: value for key, value in result.items()
            if key not in ("inputArtifacts", "predecessorInventory", "finalBuildCorrespondence")}, indent=2))
    except (ValueError, OSError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
