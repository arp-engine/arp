"""Join independently replayed finite cohorts without aggregate authorization."""
import argparse
import json
import pathlib
import sys

from bindless_finite_lifecycle_acceptance import expected_cases, fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, read_json, verify_file
from bindless_finite_lifecycle_cohort_archive import expected_cohort, inspected_files, replay
from bindless_finite_lifecycle_evidence import require_fields
from bindless_finite_lifecycle_predecessor_evidence import COHORTS, PACKAGES

CONTRACT = "FiniteLifecyclePredecessorSetInventory/1"


class FiniteLifecyclePredecessorSet:
    @staticmethod
    def inspect(cohorts, final_stage_audit=None):
        if not isinstance(cohorts, dict) or any(name not in COHORTS for name in cohorts):
            raise ValueError("FINITE-PREDECESSOR-SET-COHORT-NAME")
        roots = {name: pathlib.Path(path).resolve() for name, path in cohorts.items()}
        owners = list(roots.values())
        if any(a == b or a.is_relative_to(b) or b.is_relative_to(a)
               for index, a in enumerate(owners) for b in owners[index + 1:]):
            raise ValueError("FINITE-PREDECESSOR-SET-OWNER")
        missing = sorted(set(COHORTS) - set(roots))
        if missing and final_stage_audit is not None:
            raise ValueError("FINITE-PREDECESSOR-SET-FINAL-REQUIRES-COMPLETE-SET")
        final = pathlib.Path(final_stage_audit).resolve() if final_stage_audit is not None else None
        observations, files, identities = {}, {}, {}
        if final is not None:
            files.update(inspected_files(final))
        packages = None
        for name in COHORTS:
            if name not in roots:
                continue
            root = roots[name]
            manifest_path = root / "validation.json"
            stamp = fingerprint(manifest_path)
            manifest = read_json(manifest_path)
            require_fields(manifest, dict(cohort=name))
            result = replay(root, final)
            require_fields(result, dict(cohort=name, complete=True, shaderPackagesBound=True,
                finalCohortProfileCompared=final is not None,
                aggregatePrerequisitesValidated=False, affectedRegressionsValidated=False,
                finalPayloadRerunValidated=False, actualProbeBuildEvidenceValidated=False,
                **CLOSED_GATES))
            expected = {case: list(identity) for case, identity in expected_cohort(name).items()}
            require_fields(manifest, dict(identities=expected, cases=len(expected)))
            if set(identities) & set(expected):
                raise ValueError("FINITE-PREDECESSOR-SET-OVERLAPPING-CASES")
            identities.update(expected)
            current = manifest.get("packageIdentities")
            if not isinstance(current, dict) or set(current) != set(PACKAGES):
                raise ValueError("FINITE-PREDECESSOR-SET-PACKAGE-IDENTITY")
            if packages is not None and current != packages:
                raise ValueError("FINITE-PREDECESSOR-SET-PACKAGE-MISMATCH")
            packages = current
            files[manifest_path] = stamp
            for entry in manifest["artifacts"]:
                path = verify_file(root, entry)
                files[path] = {key: entry[key] for key in ("bytes", "sha256")}
            observations[name] = dict(archive=str(root), manifest=stamp,
                cases=len(expected), selectedArtifactsVerified=result["selectedArtifactsVerified"],
                sourceAcceptance=manifest["sourceAcceptance"],
                finalCohortProfileCompared=final is not None)
        if not missing and set(identities) != set(expected_cases("compute-final")):
            raise ValueError("FINITE-PREDECESSOR-SET-COVERAGE")
        if any(fingerprint(path) != expected for path, expected in files.items()):
            raise ValueError("FINITE-PREDECESSOR-SET-CHANGED-DURING-INSPECTION")
        numerical = sum(identity[0] == "execution" for identity in identities.values())
        return dict(contract=CONTRACT, complete=False,
            scope="private finite Compute predecessor set CPU inventory only",
            requiredCohorts=list(COHORTS), missingCohorts=missing, cohorts=observations,
            allRequiredCohortsPresent=not missing, exactCaseCoverageVerified=not missing,
            coveredCases=len(identities), coveredNumericalCases=numerical,
            coveredTerminalCases=len(identities) - numerical,
            packageIdentities=packages or {}, suppliedCohortPackagesBound=bool(roots),
            allCohortsComparedToIndependentFinal=not missing and final is not None,
            comparedFinalStageAudit=(dict(path=str(final / "validation.json"),
                **files[final / "validation.json"]) if final is not None else None),
            diagnostic="FINITE-PREDECESSOR-SET-MISSING-COHORTS" if missing else "",
            aggregatePrerequisitesValidated=False, affectedRegressionsValidated=False,
            finalPayloadRerunValidated=False, actualProbeBuildEvidenceValidated=False,
            transitiveSdkProvenanceComplete=False, **CLOSED_GATES)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cohort", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--final-stage-audit", type=pathlib.Path)
    parser.add_argument("--report", type=pathlib.Path)
    args = parser.parse_args()
    try:
        cohorts = {}
        for declaration in args.cohort:
            name, separator, path = declaration.partition("=")
            if not separator or not path or name in cohorts:
                raise ValueError("FINITE-PREDECESSOR-SET-DECLARATION")
            cohorts[name] = pathlib.Path(path)
        if args.report:
            report = args.report.resolve()
            owners = [path.resolve() for path in cohorts.values()]
            if args.final_stage_audit:
                owners.append(args.final_stage_audit.resolve())
            if any(report.is_relative_to(owner) for owner in owners):
                raise ValueError("FINITE-PREDECESSOR-SET-REPORT-OWNER")
        result = FiniteLifecyclePredecessorSet.inspect(cohorts, args.final_stage_audit)
        if args.report:
            with args.report.open("x", encoding="utf-8") as output:
                json.dump(result, output, indent=2)
        print(json.dumps(result, indent=2))
    except (ValueError, OSError, KeyError, TypeError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
