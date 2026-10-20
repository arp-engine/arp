"""Build provenance rejection tests; synthetic commands grant no GPU evidence."""
import unittest

from bindless_webgpu_probe_build_evidence import (MACRO, ROOT, ORDINARY, MAIN, PROBE, SYMBOL,
    inspect_compile_command, inspect_query, inspect_probe_symbols, inspect_build_log)


class ProbeBuildEvidenceTest(unittest.TestCase):
    def command(self, source, output, enabled=False, assertions=True):
        return ('"C:/unit fixture/no-compiler.exe" -O3 -DNDEBUG -std=c++17 '
                + ("-D" + MACRO + "=1 " if enabled else "") + ("-UNDEBUG " if assertions else "")
                + '-o "' + output + '" -c "' + (ROOT / source).as_posix() + '"')

    def test_ordinary_and_both_opted_in_fixture_compiles(self):
        for source, output, enabled, assertions in ((ORDINARY, "ordinary.cpp.o", False, False),
                (MAIN, "main.cpp.o", False, True), (MAIN, "main.cpp.o", True, True),
                (PROBE, "probe.cpp.o", True, True)):
            with self.subTest(source=source, enabled=enabled):
                inspect_compile_command(self.command(source, output, enabled, assertions), source, output, enabled, assertions)

    def test_macro_cannot_enter_the_ordinary_backend_or_be_disabled_in_probe(self):
        for command, enabled in ((self.command(ORDINARY, "ordinary.cpp.o", True), False),
                (self.command(PROBE, "probe.cpp.o", False), True),
                (self.command(PROBE, "probe.cpp.o", True) + " -D" + MACRO + "=0", True)):
            source = PROBE if enabled else ORDINARY
            output = "probe.cpp.o" if enabled else "ordinary.cpp.o"
            with self.subTest(enabled=enabled), self.assertRaises(ValueError):
                inspect_compile_command(command, source, output, enabled, True)

    def test_wrong_source_object_language_or_late_ndebug_refuses(self):
        valid = self.command(MAIN, "main.cpp.o")
        for command in (valid.replace((ROOT / MAIN).as_posix(), (ROOT / PROBE).as_posix()),
                valid.replace("main.cpp.o", "other.cpp.o"), valid.replace("-std=c++17", "-std=c++20"),
                valid + " -DNDEBUG", valid + " -std=c++20", valid + " -c other.cpp"):
            with self.subTest(command=command), self.assertRaises(ValueError):
                inspect_compile_command(command, MAIN, "main.cpp.o", False, True)

    def test_exactly_one_probe_link_and_no_probe_in_the_library(self):
        query = "target.js:\n  input: CXX_LINKER\n    main.cpp.o\n    probe.cpp.o\n    | ordinary.a\n  outputs:\n    all\n"
        self.assertEqual(inspect_query(query, ["main.cpp.o", "probe.cpp.o"], []),
                         ["main.cpp.o", "probe.cpp.o", "ordinary.a"])
        with self.assertRaises(ValueError):
            inspect_query(query, ["main.cpp.o"], ["probe.cpp.o"])
        for changed in (query.replace("    probe.cpp.o\n", ""),
                        query.replace("    probe.cpp.o\n", "    probe.cpp.o\n    probe.cpp.o\n")):
            with self.assertRaises(ValueError):
                inspect_query(changed, ["main.cpp.o", "probe.cpp.o"], [])

    def test_actual_probe_definitions_require_both_original_custody_overloads(self):
        symbols = "\n".join("00000000 T " + SYMBOL + method + "(std::shared_ptr<arp::rhi_webgpu::" + owner + "> const&)"
            for method in ("arm", "inspect") for owner in
            ("WebGpuOriginalTypedCallbackCustody", "WebGpuCanonicalCallbackCustody"))
        self.assertEqual(len(inspect_probe_symbols(symbols, True)), 4)
        inspect_probe_symbols("ordinary-symbol-only", False)
        with self.assertRaises(ValueError):
            inspect_probe_symbols(symbols, False)
        for changed in ("\n".join(symbols.splitlines()[1:]), symbols + "\n" + symbols.splitlines()[0],
                        symbols.replace("WebGpuCanonicalCallbackCustody", "ForeignCustody")):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                inspect_probe_symbols(changed, True)

    def test_configured_recipe_or_partial_log_cannot_prove_an_executed_link(self):
        commands = ["unit-ordinary", "unit-main", "unit-probe", "unit-link"]
        valid = "\n".join("[" + str(index + 1) + "/4] " + command for index, command in enumerate(commands))
        inspect_build_log(valid, commands)
        for log in ("", valid.replace("[3/4] unit-probe", ""), valid + "\n[4/4] unit-link",
                    valid + "\nFAILED: target.js", valid + "\nninja: build stopped: subcommand failed."):
            with self.subTest(log=log), self.assertRaises(ValueError):
                inspect_build_log(log, commands)


if __name__ == "__main__":
    unittest.main()
