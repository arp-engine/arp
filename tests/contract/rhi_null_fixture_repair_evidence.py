"""Build the full Null smoke with explicit fixture repairs in a copied tree.

Preserves the sealed live fixture, original main, compiler flags and libraries.
An overlay success is candidate evidence, not a repaired registered target,
native execution, transitive SDK provenance or public bindless authority.
"""
import argparse
import difflib
import json
import pathlib
import re
import shutil
import subprocess
import sys

from bindless_finite_lifecycle_acceptance import fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, read_json, verify_file
from bindless_webgpu_probe_build_evidence import command_tokens
from bindless_webgpu_probe_archive_audit import dependency_profile

ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET = "arp_rhi_null_hidden_frame_smoke_tests"
MAIN = "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp"
FIXTURE = "tests/fixtures/original_typed_dependency_decision_fixture.h"
PAYLOAD_FIXTURE = "tests/fixtures/original_typed_native_payload_fixture.h"
CLOSURE_FIXTURE = "tests/fixtures/original_geometry_readback_composition_fixture.h"
OBJECT = "CMakeFiles/" + TARGET + ".dir/" + MAIN + ".obj"
OLD = b'        bad.contractVersion=8u;is(C::validateFreezeIntent(bad),"RHI-ORIGINAL-DEPENDENCY-FREEZE-VERSION");'
NEW = (b'        bad.contractVersion=8u;is(C::validateFreezeIntent(bad),"RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE");'
       b'\n        bad.contractVersion=9u;is(C::validateFreezeIntent(bad),"RHI-ORIGINAL-DEPENDENCY-FREEZE-VERSION");')
PAYLOAD_OLD = b'            assert(!arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(rhi::Backend::WebGPU,8u).payloadAvailable);'
PAYLOAD_NEW = b'''            for(const auto backend:{rhi::Backend::Null,rhi::Backend::Vulkan,rhi::Backend::Metal,rhi::Backend::WebGPU}) {
                const auto graphicsDecision=arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(backend,8u);
                assert(graphicsDecision.payloadAvailable==(backend==rhi::Backend::WebGPU));
                assert(!graphicsDecision.recordingAvailable && !graphicsDecision.executionAvailable && graphicsDecision.diagnostic);
                assert((graphicsDecision.disposition==arp::rhi::semantics::OriginalCommandNativeClosureLowering::Disposition::ValidatorOnly)==(backend==rhi::Backend::Null));
            }
            const auto unknownPayload=arp::rhi::semantics::OriginalTypedNativePayloadLowering::lowering(rhi::Backend::WebGPU,9u);
            assert(!unknownPayload.payloadAvailable && !unknownPayload.recordingAvailable && !unknownPayload.executionAvailable);
            is(unknownPayload.diagnostic,"RHI-ORIGINAL-TYPED-PAYLOAD-VERSION");'''
CLOSURE_OLD = b'''        assert(!ClosureContract::lossJoinVersion(8u,true,false) && !ClosureContract::lossJoinVersion(10u,false,true));
        assert(ClosureContract::lossJoinVersion(9u,false,true)==1u);
        assert(!ClosureContract::lossJoinVersion(9u,true,false));
        assert(ClosureContract::pipelineSourceVersion(7u,false,true)==1u);
        assert(ClosureContract::pipelineSourceVersion(7u,true,false)==2u);
        assert(!ClosureContract::pipelineSourceVersion(7u,true,true));
        assert(!ClosureContract::pipelineSourceVersion(7u,false,false));
        assert(ClosureContract::pipelineSourceVersion(3u,true,false)==2u);
        assert(ClosureContract::pipelineSourceVersion(4u,true,false)==2u);
        assert(ClosureContract::pipelineSourceVersion(6u,false,true)==1u);
        assert(!ClosureContract::pipelineSourceVersion(4u,false,true));
        assert(!ClosureContract::pipelineSourceVersion(6u,true,false));
        assert(ClosureContract::pipelineSourceVersion(8u,false,true)==1u);
        assert(!ClosureContract::pipelineSourceVersion(8u,true,false) && !ClosureContract::pipelineSourceVersion(10u,false,true));
        assert(ClosureContract::pipelineSourceVersion(9u,false,true)==1u);
        assert(!ClosureContract::pipelineSourceVersion(9u,true,false));'''
CLOSURE_NEW = b'''        assert(!ClosureContract::lossJoinVersion(8u,true,false));
        assert(ClosureContract::lossJoinVersion(9u,false,true)==1u);
        assert(!ClosureContract::lossJoinVersion(9u,true,false));
        assert(ClosureContract::lossJoinVersion(10u,false,true)==1u);
        assert(!ClosureContract::lossJoinVersion(10u,true,false) && !ClosureContract::lossJoinVersion(10u,true,true)
            && !ClosureContract::lossJoinVersion(10u,false,false) && !ClosureContract::lossJoinVersion(11u,false,true));
        assert(ClosureContract::pipelineSourceVersion(7u,false,true)==1u);
        assert(ClosureContract::pipelineSourceVersion(7u,true,false)==2u);
        assert(!ClosureContract::pipelineSourceVersion(7u,true,true));
        assert(!ClosureContract::pipelineSourceVersion(7u,false,false));
        assert(ClosureContract::pipelineSourceVersion(3u,true,false)==2u);
        assert(ClosureContract::pipelineSourceVersion(4u,true,false)==2u);
        assert(ClosureContract::pipelineSourceVersion(6u,false,true)==1u);
        assert(!ClosureContract::pipelineSourceVersion(4u,false,true));
        assert(!ClosureContract::pipelineSourceVersion(6u,true,false));
        assert(ClosureContract::pipelineSourceVersion(8u,false,true)==1u);
        assert(!ClosureContract::pipelineSourceVersion(8u,true,false));
        assert(ClosureContract::pipelineSourceVersion(9u,false,true)==1u);
        assert(!ClosureContract::pipelineSourceVersion(9u,true,false));
        assert(ClosureContract::pipelineSourceVersion(10u,false,true)==1u);
        assert(!ClosureContract::pipelineSourceVersion(10u,true,false) && !ClosureContract::pipelineSourceVersion(10u,true,true)
            && !ClosureContract::pipelineSourceVersion(10u,false,false) && !ClosureContract::pipelineSourceVersion(11u,false,true));'''
REPAIRS = {FIXTURE: (OLD, NEW), PAYLOAD_FIXTURE: (PAYLOAD_OLD, PAYLOAD_NEW),
           CLOSURE_FIXTURE: (CLOSURE_OLD, CLOSURE_NEW)}


class NullFixtureRepairEvidence:
    @staticmethod
    def candidate_patch_bytes(sources):
        chunks = []
        for relative in REPAIRS:
            source = ROOT / relative
            if source not in sources:
                continue
            original = source.read_bytes()
            repaired = NullFixtureRepairEvidence.repaired_bytes(original, relative)
            chunks.append("diff --git a/" + relative + " b/" + relative + "\n")
            chunks.extend(difflib.unified_diff(original.decode("utf-8").splitlines(keepends=True),
                repaired.decode("utf-8").splitlines(keepends=True), fromfile="a/" + relative, tofile="b/" + relative))
        return "".join(chunks).replace("\r\n", "\n").encode("utf-8")

    @staticmethod
    def repaired_bytes(data, relative=FIXTURE):
        if relative not in REPAIRS:
            raise ValueError("NULL-REPAIR-FIXTURE-DOMAIN")
        old, new = REPAIRS[relative]
        # Whole blocks are LF-authored while the checked-out fixtures may be CRLF.
        old = old.replace(b"\n", b"\r\n") if b"\r\n" in data else old
        if data.count(old) != 1 or relative == FIXTURE and b'bad.contractVersion=9u;' in data:
            raise ValueError("NULL-REPAIR-EXACT-NEGATIVE-GUARD")
        newline = b"\r\n" if b"\r\n" in data else b"\n"
        return data.replace(old, new.replace(b"\n", newline))

    @staticmethod
    def compile_recipe(command, root, overlay, output):
        args = command_tokens(command)
        if (not args or pathlib.Path(args[0]).name.lower() != "g++.exe"
                or any(arg in ("&&", "||", ";", "|", ">", "<") for arg in args)):
            raise ValueError("NULL-REPAIR-COMPILER")
        for flag in ("-c", "-o", "-MT", "-MF"):
            if args.count(flag) != 1 or args.index(flag) + 1 >= len(args):
                raise ValueError("NULL-REPAIR-COMPILE-RECIPE")
        if (pathlib.Path(args[args.index("-c") + 1]).resolve() != root / MAIN
                or args[args.index("-o") + 1].replace("\\", "/") != OBJECT
                or [arg for arg in args if arg in ("-DNDEBUG", "-UNDEBUG")][-1:] != ["-UNDEBUG"]
                or [arg for arg in args if arg.startswith("-std=")] != ["-std=c++17"]):
            raise ValueError("NULL-REPAIR-SOURCE-ASSERTIONS")
        args[args.index("-c") + 1] = str(overlay / MAIN)
        for flag, destination in (("-o", output / "smoke.obj"),
                                  ("-MT", output / "smoke.obj"), ("-MF", output / "smoke.obj.d")):
            args[args.index(flag) + 1] = str(destination)
        return args

    @staticmethod
    def link_recipe(command, compiler, output):
        match = re.fullmatch(r'[^\r\n]*cmd\.exe /C "cd \. && (.+) && cd \."', command)
        if not match:
            raise ValueError("NULL-REPAIR-LINK-WRAPPER")
        args = command_tokens(match[1])
        if (not args or args[0] != compiler or any(token in ("&&", "||", ";", "|", ">", "<") for token in args)
                or args.count(OBJECT) != 1 or args.count("-o") != 1
                or args[args.index("-o") + 1] != TARGET + ".exe"):
            raise ValueError("NULL-REPAIR-LINK-OWNER")
        implib = "-Wl,--out-implib,lib" + TARGET + ".dll.a"
        if args.count(implib) != 1:
            raise ValueError("NULL-REPAIR-LINK-IMPLIB")
        args[args.index(OBJECT)] = str(output / "smoke.obj")
        args[args.index("-o") + 1] = str(output / "smoke.exe")
        args[args.index(implib)] = "-Wl,--out-implib," + str(output / "smoke.dll.a")
        return args

    @staticmethod
    def verify_unchanged(inputs):
        if any(fingerprint(path) != stamp for path, stamp in inputs.items()):
            raise ValueError("NULL-REPAIR-INPUT-DRIFT")

    def __init__(self, build, candidate, sealed, output, ninja):
        self.build, self.output = build.resolve(), output.resolve()
        if (self.output.exists() or not self.output.is_relative_to(ROOT / "build")
                or self.output == self.build or self.output.is_relative_to(self.build)):
            raise ValueError("NULL-REPAIR-OUTPUT-OWNER")
        self.candidate, self.sealed, self.ninja = candidate.resolve(), sealed.resolve(), ninja.resolve()

    def run(self):
        recipe = subprocess.check_output([str(self.ninja), "-C", str(self.build), "-t", "commands", TARGET],
                                         encoding="utf-8").splitlines()
        compile_commands = [line for line in recipe if " -c " in line and MAIN in line.replace("\\", "/")]
        link_commands = [line for line in recipe if " -o " + TARGET + ".exe " in line]
        if len(compile_commands) != 1 or len(link_commands) != 1:
            raise ValueError("NULL-REPAIR-NINJA-RECIPE")
        overlay = self.output / "overlay"
        compile_args = self.compile_recipe(compile_commands[0], ROOT, overlay, self.output)
        link_args = self.link_recipe(link_commands[0], compile_args[0], self.output)
        candidate = read_json(self.candidate)
        if (candidate.get("contract") not in ("NullFreeze8NegativeGuardRepairCandidate/1",
                "NullFreeze8Payload8NegativeGuardRepairCandidate/1", "NullGraphicsNegativeGuardRepairCandidate/1")
                or candidate.get("applied") is not False):
            raise ValueError("NULL-REPAIR-CANDIDATE-SOURCE")
        sources = ([candidate["source"]] if candidate["contract"] == "NullFreeze8NegativeGuardRepairCandidate/1"
                   else candidate.get("sources"))
        expected = ({FIXTURE} if candidate["contract"] == "NullFreeze8NegativeGuardRepairCandidate/1"
                    else {FIXTURE, PAYLOAD_FIXTURE} if candidate["contract"] == "NullFreeze8Payload8NegativeGuardRepairCandidate/1"
                    else set(REPAIRS))
        repair_sources = {}
        if not isinstance(sources, list) or len(sources) != len(expected):
            raise ValueError("NULL-REPAIR-CANDIDATE-SOURCE")
        for row in sources:
            source = pathlib.Path(row["path"]).resolve()
            if (not source.is_relative_to(ROOT) or source.relative_to(ROOT).as_posix() not in expected
                    or source in repair_sources or fingerprint(source) != {key: row[key] for key in ("bytes", "sha256")}):
                raise ValueError("NULL-REPAIR-CANDIDATE-SOURCE")
            repair_sources[source] = source.relative_to(ROOT).as_posix()
        patch = pathlib.Path(candidate["patch"]["path"])
        if fingerprint(patch) != {key: candidate["patch"][key] for key in ("bytes", "sha256")}:
            raise ValueError("NULL-REPAIR-CANDIDATE-PATCH")
        patch_bytes = patch.read_bytes().replace(b"\r\n", b"\n")
        if candidate["contract"] == "NullGraphicsNegativeGuardRepairCandidate/1":
            if patch_bytes != self.candidate_patch_bytes(repair_sources):
                raise ValueError("NULL-REPAIR-CANDIDATE-GUARD")
        else:
            for relative in repair_sources.values():
                old, new = REPAIRS[relative]
                if b"-" + old + b"\n" not in patch_bytes or b"+" + new.replace(b"\n", b"\n+") + b"\n" not in patch_bytes:
                    raise ValueError("NULL-REPAIR-CANDIDATE-GUARD")

        dependency_text = subprocess.check_output([str(self.ninja), "-C", str(self.build), "-t", "deps", OBJECT],
                                                 encoding="utf-8")
        dependencies, status = dependency_profile(dependency_text, OBJECT)
        if status != "VALID":
            raise ValueError("NULL-REPAIR-ORIGINAL-DEPENDENCIES")
        paths = {pathlib.Path(name).resolve() if pathlib.Path(name).is_absolute()
                 else (self.build / name).resolve() for name in dependencies["paths"]}
        fixtures = sorted((ROOT / "tests/fixtures").rglob("*"))
        copied_sources = sorted({ROOT / MAIN} | {path for path in fixtures if path.is_file()}
                                | {path for path in paths if path.is_relative_to(ROOT / "tests")})
        paths.update(path for path in fixtures if path.is_file())
        paths.update(repair_sources)
        paths.update((ROOT / MAIN, self.candidate, patch, self.sealed, pathlib.Path(compile_args[0]),
                      self.ninja, pathlib.Path(__file__).resolve(), self.build / "CMakeCache.txt",
                      self.build / "build.ninja", self.build / "CMakeFiles/rules.ninja"))
        paths.update(self.build / token for token in link_args if token.endswith(".a") and not token.startswith("-"))
        sealed = read_json(self.sealed)
        sealed_rows = [row for row in sealed["files"] if row["name"] == "CMakeLists.txt"
                       or row["name"].startswith(("src/", "tests/", "payload/"))]
        if len(sealed_rows) != 24:
            raise ValueError("NULL-REPAIR-SEALED-INVENTORY")
        for row in sealed_rows:
            verify_file(self.sealed.parent, row)
            source = pathlib.Path(row["source"]).resolve()
            if fingerprint(source) != {key: row[key] for key in ("bytes", "sha256")}:
                raise ValueError("NULL-REPAIR-SEALED-DRIFT")
            paths.add(source)
        inputs = {path: fingerprint(path) for path in paths}
        self.output.mkdir(parents=True)
        self.report = dict(contract="NullFixtureRepairOverlayEvidence/1", complete=False,
            scope="full general Null smoke with explicit copied negative-guard repairs",
            candidateAppliedToLiveSource=False, registeredTargetRepaired=False,
            repairSmokeValidated=False, assertionsEnabled=True, gpuExecuted=False,
            transitiveSdkProvenanceComplete=False, sealedGraphicsInputsVerified=len(sealed_rows),
            originalDependencyCount=dependencies["count"], inputFiles=len(inputs), **CLOSED_GATES)
        def save():
            (self.output / "validation.json").write_text(json.dumps(self.report, indent=2), encoding="utf-8")
        (self.output / "original-commands.json").write_text(json.dumps(recipe, indent=2), encoding="utf-8")
        (self.output / "inputs.json").write_text(json.dumps(
            [dict(source=str(path), **stamp) for path, stamp in sorted(inputs.items())], indent=2), encoding="utf-8")
        (self.output / "original-dependencies.txt").write_text(dependency_text, encoding="utf-8")
        save()
        try:
            copies = {}
            for source in copied_sources:
                destination = overlay / source.relative_to(ROOT)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
                if fingerprint(source) != fingerprint(destination):
                    raise ValueError("NULL-REPAIR-COPY-IDENTITY")
                copies[destination] = fingerprint(destination)
            repaired_fixtures = []
            for relative in repair_sources.values():
                repaired = overlay / relative
                repaired.write_bytes(self.repaired_bytes(repaired.read_bytes(), relative))
                copies[repaired] = fingerprint(repaired)
                repaired_fixtures.append(dict(path=str(repaired), **fingerprint(repaired)))
            (self.output / "copied-inputs.json").write_text(json.dumps(
                [dict(source=str(ROOT / path.relative_to(overlay)), path=str(path.relative_to(self.output)), **stamp)
                 for path, stamp in sorted(copies.items())], indent=2), encoding="utf-8")
            self.report["copiedFiles"] = len(copies)
            self.report["repairedFixtures"] = repaired_fixtures
            self.report["copiedMain"] = dict(path=str(overlay / MAIN), **fingerprint(overlay / MAIN))
            for stage, args in (("compile", compile_args), ("link", link_args), ("smoke", [str(self.output / "smoke.exe")])):
                self.verify_unchanged(inputs)
                self.verify_unchanged(copies)
                self.report[stage + "Command"] = args
                save()
                with (self.output / (stage + ".log")).open("wb") as log:
                    completed = subprocess.run(args, cwd=self.build, stdout=log, stderr=subprocess.STDOUT, shell=False)
                self.report[stage + "ExitCode"] = completed.returncode
                save()
                if completed.returncode:
                    raise ValueError("NULL-REPAIR-" + stage.upper() + "-FAILED")
                if stage == "compile":
                    depfile = (self.output / "smoke.obj.d").read_text(encoding="utf-8").replace("\\", "/")
                    for relative in repair_sources.values():
                        if (overlay / relative).as_posix() not in depfile or (ROOT / relative).as_posix() in depfile:
                            raise ValueError("NULL-REPAIR-FIXTURE-INCLUDE-OWNER")
            self.verify_unchanged(inputs)
            self.verify_unchanged(copies)
            self.report.update(complete=True, repairSmokeValidated=True,
                               liveInputsUnchanged=True, originalMainPreserved=True,
                               fixtureIncludeOwnerValidated=True,
                               executable=dict(path=str(self.output / "smoke.exe"), **fingerprint(self.output / "smoke.exe")))
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            self.report["diagnostic"] = str(error)
            raise
        finally:
            try:
                self.verify_unchanged(inputs)
                self.report["liveInputsUnchanged"] = True
            except (OSError, ValueError) as error:
                self.report.update(liveInputsUnchanged=False, complete=False, repairSmokeValidated=False,
                                   diagnostic=str(error))
                raise
            finally:
                save()
        return self.report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("build", "candidate", "sealed", "output", "ninja"):
        parser.add_argument("--" + name, type=pathlib.Path, required=True)
    args = parser.parse_args()
    try:
        result = NullFixtureRepairEvidence(args.build, args.candidate, args.sealed, args.output, args.ninja).run()
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
