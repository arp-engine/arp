"""Synthetic hostile archive tests; mocked tools supply no native/GPU proof."""
import copy
import json
import pathlib
import tempfile
import unittest
from unittest import mock

from bindless_finite_lifecycle_acceptance import fingerprint
from bindless_finite_lifecycle_archive_audit import CLOSED_GATES, read_json
from bindless_webgpu_probe_build_evidence import ROOT, MACRO, TARGET, ORDINARY, MAIN, PROBE, SYMBOL
from bindless_webgpu_probe_archive_audit import OBJECTS, replay, configuration, inspect_ninja_log


class ProbeArchiveAuditTest(unittest.TestCase):
    def fixture(self, root):
        archive = root / "archive"; archive.mkdir()
        build = root / "original-build-absent"
        log_source = root / "original-log-absent.log"
        commands = {}
        for label, output in OBJECTS.items():
            source = dict(ordinary=ORDINARY, main=MAIN, probe=PROBE)[label]
            commands[label] = ("unit-compiler -std=c++17 "
                + ("-D" + MACRO + "=1 -UNDEBUG " if label != "ordinary" else "")
                + "-o " + output + " -c " + (ROOT / source).as_posix())
        commands["link"] = "unit-linker -o " + TARGET + ".js"
        library_query = "libarp_rhi_webgpu.a:\n  input: STATIC_LINKER\n    " + OBJECTS["ordinary"] + "\n  outputs:\n    all\n"
        browser_query = (TARGET + ".js:\n  input: CXX_LINKER\n    " + OBJECTS["main"]
            + "\n    " + OBJECTS["probe"] + "\n    | libarp_rhi_webgpu.a\n  outputs:\n    all\n")
        queries = dict(library=library_query, browser=browser_query)
        symbols = {label: "00000000 T ordinary_definition()\n" for label in ("ordinary", "main", "library")}
        symbols["probe"] = "\n".join("00000000 T " + SYMBOL + method + "(arp::rhi_webgpu::" + owner + " const&)"
            for method in ("arm", "inspect") for owner in
            ("WebGpuOriginalTypedCallbackCustody", "WebGpuCanonicalCallbackCustody")) + "\n"
        dependencies = {label: dict(count=2, ninjaOutput=output + ": #deps 2, deps mtime 42 (VALID)\n"
                       + "    source-one.h\n    source-two.h\n\n") for label, output in OBJECTS.items()}
        contents = {
            build / "CMakeCache.txt": (MACRO + ":BOOL=ON\nBUILD_SHARED_LIBS:BOOL=OFF\nCMAKE_BUILD_TYPE:STRING=Release\n"
                + "CMAKE_CACHEFILE_DIR:INTERNAL=" + str(build) + "\n"
                + "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(ROOT) + "\n").encode(),
            build / "build.ninja": b"include CMakeFiles/rules.ninja\n# synthetic, no compile authority\n",
            build / "CMakeFiles/rules.ninja": b"# synthetic, no compile authority\n",
            build / "CMakeFiles/3.31.0/CMakeSystem.cmake": b'set(CMAKE_SYSTEM_NAME "Emscripten")\n',
            build / ".ninja_deps": b"synthetic-deps",
            build / ".ninja_log": ("# ninja log v7\n" + "\n".join("0\t10\t42\t" + output + "\t0123456789abcdef"
                for output in list(OBJECTS.values()) + [TARGET + ".js"]) + "\n").encode(),
            log_source: "\n".join("[" + str(index + 1) + "/4] " + command
                for index, command in enumerate(commands.values())).encode(),
            build / "libarp_rhi_webgpu.a": symbols["library"].encode(),
        }
        contents.update({build / output: symbols[label].encode() for label, output in OBJECTS.items()})
        contents.update({build / (TARGET + suffix): ("synthetic payload " + suffix).encode()
                         for suffix in (".js", ".wasm", ".data")})
        for name in ("CMakeLists.txt", ORDINARY, MAIN, PROBE,
                     "tests/fixtures/webgpu_post_suppression_failure_probe_fixture.h"):
            contents[ROOT / name] = b"synthetic copied source, never used as native proof"
        artifacts, paths = [], {}
        for index, (source, data) in enumerate(contents.items()):
            path = archive / "artifacts" / str(index) / source.name
            path.parent.mkdir(parents=True); path.write_bytes(data)
            artifacts.append(dict(source=str(source), path=str(path.relative_to(archive)), **fingerprint(path)))
            paths[source] = path
        manifest = dict(contract="WebGpuProbeBuildEvidence/1", complete=True, mode="probe", probeEnabled=True,
            actualLinkExecutionValidated=True, configuredObjectGraphValidated=True,
            originalBackendProbeDefinitionsAbsent=True, assertionsEnabledOnFixture=True,
            objectDependenciesFresh=True, transitiveSdkProvenanceComplete=False,
            commands=commands, queries=queries, symbolOutputs=symbols, dependencies=dependencies,
            payload={TARGET + suffix: fingerprint(paths[build / (TARGET + suffix)]) for suffix in (".js", ".wasm", ".data")},
            artifacts=artifacts, **CLOSED_GATES)
        (archive / "validation.json").write_text(json.dumps(manifest), encoding="utf-8")
        # Replies are copied independently so edited manifest summaries cannot
        # change what the simulated archived recipe inspection observes.
        replies = copy.deepcopy(dict(commands=commands, queries=queries, dependencies=dependencies))
        def fake_tools(args, **kwargs):
            if args[0] == "unit-nm":
                return pathlib.Path(args[-1]).read_text(encoding="utf-8")
            if args[0] != "unit-ninja" or args[3] != "-t":
                raise AssertionError("Only Ninja read-only inspection and nm are permitted")
            recovered = pathlib.Path(args[2]); tool, target = args[4:]
            if tool == "commands":
                if target == TARGET + ".js": return "\n".join(replies["commands"].values())
                label = next(label for label, output in OBJECTS.items() if output == target)
                return replies["commands"][label]
            if tool == "query":
                return replies["queries"]["library" if target == "libarp_rhi_webgpu.a" else "browser"]
            if tool == "deps":
                if (recovered / ".ninja_deps").read_bytes() != b"synthetic-deps": return "invalid-deps"
                label = next(label for label, output in OBJECTS.items() if output == target)
                return replies["dependencies"][label]["ninjaOutput"].replace("(VALID)", "(STALE)")
            raise AssertionError("Unknown tool")
        return archive, build, log_source, paths, fake_tools

    @staticmethod
    def rehash(archive, path):
        manifest_path = archive / "validation.json"; manifest = read_json(manifest_path)
        entry = next(entry for entry in manifest["artifacts"] if (archive / entry["path"]).resolve() == path.resolve())
        entry.update(fingerprint(path))
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

    def test_replay_uses_only_archived_files_and_keeps_runtime_gates_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, build, log_source, unused, runner = self.fixture(pathlib.Path(directory))
            self.assertFalse(build.exists()); self.assertFalse(log_source.exists())
            with mock.patch("subprocess.check_output", side_effect=runner):
                result = replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))
            self.assertTrue(result["archivedRecipesReplayed"])
            self.assertTrue(result["archivedObjectSymbolsReplayed"])
            self.assertFalse(result["liveDependencyFreshnessRechecked"])
            self.assertFalse(result["postSuppressionRuntimeValidated"])
            for gate in CLOSED_GATES: self.assertIs(result[gate], False)

    def test_forged_summaries_or_missing_failed_or_duplicate_artifacts_refuse(self):
        for mutation in ("complete", "gate", "command", "query", "dependency", "symbols", "duplicate", "missing", "payload"):
            with tempfile.TemporaryDirectory() as directory:
                archive, build, log_source, unused, runner = self.fixture(pathlib.Path(directory))
                path = archive / "validation.json"; manifest = read_json(path)
                if mutation == "complete": manifest["complete"] = False
                if mutation == "gate": manifest["finiteLifecycle4Validated"] = True
                if mutation == "command": manifest["commands"]["probe"] += " -D" + MACRO + "=0"
                if mutation == "query": manifest["queries"]["browser"] += "forged-query"
                if mutation == "dependency": manifest["dependencies"]["main"]["count"] = 1
                if mutation == "symbols": manifest["symbolOutputs"]["probe"] = "forged"
                if mutation == "duplicate": manifest["artifacts"].append(manifest["artifacts"][0])
                if mutation == "missing": manifest["artifacts"] = [entry for entry in manifest["artifacts"]
                    if pathlib.Path(entry["source"]).name != "libarp_rhi_webgpu.a"]
                if mutation == "payload": manifest["payload"][TARGET + ".wasm"]["sha256"] = "f" * 64
                path.write_text(json.dumps(manifest), encoding="utf-8")
                with self.subTest(mutation=mutation), mock.patch("subprocess.check_output", side_effect=runner), self.assertRaises(ValueError):
                    replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_rehashed_ordinary_probe_definition_cannot_hide_behind_cached_symbols(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, build, log_source, paths, runner = self.fixture(pathlib.Path(directory))
            ordinary = paths[build / OBJECTS["ordinary"]]
            ordinary.write_bytes(paths[build / OBJECTS["probe"]].read_bytes())
            self.rehash(archive, ordinary)
            with mock.patch("subprocess.check_output", side_effect=runner), self.assertRaisesRegex(ValueError, "ORDINARY-SYMBOL"):
                replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_rehashed_partial_log_bad_ninja_trace_dependency_or_external_include_refuses(self):
        for mutation in ("verbose", "trace", "deps", "include", "rules"):
            with tempfile.TemporaryDirectory() as directory:
                archive, build, log_source, paths, runner = self.fixture(pathlib.Path(directory))
                source = log_source if mutation == "verbose" else build / dict(trace=".ninja_log", deps=".ninja_deps",
                    include="build.ninja", rules="CMakeFiles/rules.ninja").get(mutation, "unused")
                path = paths[source]
                if mutation == "verbose": path.write_text("\n".join(path.read_text().splitlines()[:-1]), encoding="utf-8")
                if mutation == "trace": path.write_text(path.read_text() + path.read_text().splitlines()[-1] + "\n", encoding="utf-8")
                if mutation == "deps": path.write_bytes(b"altered-deps")
                if mutation in ("include", "rules"): path.write_text(path.read_text() + "subninja D:/external.ninja\n", encoding="utf-8")
                self.rehash(archive, path)
                with self.subTest(mutation=mutation), mock.patch("subprocess.check_output", side_effect=runner), self.assertRaises(ValueError):
                    replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_build_log_must_be_the_exact_archived_source(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, unused, log_source, paths, runner = self.fixture(pathlib.Path(directory))
            live = pathlib.Path(directory) / "live-unarchived.log"; live.write_bytes(paths[log_source].read_bytes())
            with mock.patch("subprocess.check_output", side_effect=runner), self.assertRaisesRegex(ValueError, "MISSING-ARTIFACT"):
                replay(archive, live, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_incremental_history_replays_latest_rows_without_discarding_history(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, build, log_source, paths, runner = self.fixture(pathlib.Path(directory))
            trace = paths[build / ".ninja_log"]
            current = trace.read_text(encoding="utf-8")
            older = current.splitlines()[1:]
            older = [row.replace("\t42\t", "\t21\t") for row in older]
            trace.write_text("# ninja log v7\n" + "\n".join(older)
                             + "\n" + "\n".join(current.splitlines()[1:]) + "\n", encoding="utf-8")
            self.rehash(archive, trace)
            stamp = fingerprint(trace)
            with mock.patch("subprocess.check_output", side_effect=runner):
                result = replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))
            self.assertEqual(fingerprint(trace), stamp)
            self.assertEqual(len(result["latestExecutionRecords"]), 4)
            self.assertTrue(all(row["mtime"] == 42 for row in result["latestExecutionRecords"].values()))
            for gate in CLOSED_GATES:
                self.assertIs(result[gate], False)

    def test_relocated_verifier_uses_archived_source_label_without_live_source_lookup(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, build, log_source, unused, runner = self.fixture(pathlib.Path(directory))
            relocated = pathlib.Path(directory) / "copied-verifier-root"
            with mock.patch("subprocess.check_output", side_effect=runner), \
                    mock.patch("bindless_webgpu_probe_archive_audit.ROOT", relocated), \
                    mock.patch("bindless_webgpu_probe_build_evidence.ROOT", relocated):
                result = replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))
            self.assertEqual(pathlib.Path(result["logicalSourceRoot"]), ROOT.resolve())
            self.assertFalse(build.exists())

    def test_archived_source_label_missing_duplicate_relative_or_substituted_refuses(self):
        for mutation in ("missing", "duplicate", "relative", "foreign"):
            with tempfile.TemporaryDirectory() as directory:
                archive, build, log_source, paths, runner = self.fixture(pathlib.Path(directory))
                cache = paths[build / "CMakeCache.txt"]
                home = "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(ROOT) + "\n"
                replacement = dict(missing="", duplicate=home+home,
                    relative="CMAKE_HOME_DIRECTORY:INTERNAL=relative-source\n",
                    foreign="CMAKE_HOME_DIRECTORY:INTERNAL=" + str(pathlib.Path(directory) / "foreign") + "\n")
                cache.write_text(cache.read_text(encoding="utf-8").replace(home, replacement[mutation]), encoding="utf-8")
                self.rehash(archive, cache)
                with self.subTest(mutation=mutation), mock.patch("subprocess.check_output", side_effect=runner), \
                        self.assertRaises(ValueError):
                    replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_newer_object_record_cannot_reuse_an_older_matching_dependency(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, build, log_source, paths, runner = self.fixture(pathlib.Path(directory))
            trace = paths[build / ".ninja_log"]
            trace.write_text(trace.read_text(encoding="utf-8") + "11\t20\t43\t"
                             + OBJECTS["main"] + "\t0123456789abcdef\n", encoding="utf-8")
            self.rehash(archive, trace)
            with mock.patch("subprocess.check_output", side_effect=runner), \
                    self.assertRaisesRegex(ValueError, "PROBE-ARCHIVE-OBJECT-MTIME"):
                replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_edge_log_and_dependency_observations_can_have_distinct_timestamps(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, build, log_source, paths, runner = self.fixture(pathlib.Path(directory))
            trace = paths[build / ".ninja_log"]
            text = trace.read_text(encoding="utf-8")
            for output in OBJECTS.values():
                text = text.replace("\t42\t" + output + "\t", "\t40\t" + output + "\t")
            trace.write_text(text, encoding="utf-8")
            self.rehash(archive, trace)
            with mock.patch("subprocess.check_output", side_effect=runner):
                result = replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))
            self.assertEqual(set(result["dependencyMtimes"].values()), {42})
            self.assertTrue(all(result["latestExecutionRecords"][output]["mtime"] == 40
                                for output in OBJECTS.values()))

    def test_link_must_cover_dependency_mtime_even_when_it_covers_edge_log(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, build, log_source, paths, runner = self.fixture(pathlib.Path(directory))
            trace = paths[build / ".ninja_log"]
            text = trace.read_text(encoding="utf-8")
            for output in OBJECTS.values():
                text = text.replace("\t42\t" + output + "\t", "\t40\t" + output + "\t")
            text = text.replace("\t42\t" + TARGET + ".js\t", "\t41\t" + TARGET + ".js\t")
            trace.write_text(text, encoding="utf-8")
            self.rehash(archive, trace)
            with mock.patch("subprocess.check_output", side_effect=runner), \
                    self.assertRaisesRegex(ValueError, "PROBE-ARCHIVE-LINK-MTIME"):
                replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_link_record_cannot_predate_selected_object_records(self):
        with tempfile.TemporaryDirectory() as directory:
            archive, build, log_source, paths, runner = self.fixture(pathlib.Path(directory))
            trace = paths[build / ".ninja_log"]
            trace.write_text(trace.read_text(encoding="utf-8").replace(
                "\t42\t" + TARGET + ".js\t", "\t41\t" + TARGET + ".js\t"), encoding="utf-8")
            self.rehash(archive, trace)
            with mock.patch("subprocess.check_output", side_effect=runner), \
                    self.assertRaisesRegex(ValueError, "PROBE-ARCHIVE-LINK-MTIME"):
                replay(archive, log_source, pathlib.Path("unit-ninja"), pathlib.Path("unit-nm"))

    def test_configuration_and_ninja_trace_have_explicit_supported_schemas(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            text = (MACRO + ":BOOL=ON\nBUILD_SHARED_LIBS:BOOL=OFF\nCMAKE_BUILD_TYPE:STRING=Release\n"
                    + "CMAKE_CACHEFILE_DIR:INTERNAL=" + str(root) + "\n")
            self.assertEqual(configuration(text), root.resolve())
            for changed in (text.replace("BOOL=ON", "BOOL=OFF"), text + MACRO + ":BOOL=ON\n",
                            text.replace(str(root), "relative-build")):
                with self.assertRaises(ValueError): configuration(changed)
        valid = "# ninja log v7\n0\t10\t42\tmain.o\t0123456789abcdef\n"
        inspect_ninja_log(valid, ["main.o"])
        for changed in (valid.replace("v7", "v5"), valid.replace("42", "0"), valid.replace("0\t10", "10\t0"),
                        valid.replace("main.o", "foreign.o"), valid + valid.splitlines()[-1] + "\n",
                        valid + "0\t10\t41\tmain.o\t0123456789abcdef\n"):
            with self.assertRaises(ValueError): inspect_ninja_log(changed, ["main.o"])


if __name__ == "__main__":
    unittest.main()
