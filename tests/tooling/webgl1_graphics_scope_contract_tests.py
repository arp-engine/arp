"""Public diagnostic identity and scope contracts; no backend execution claims."""

from pathlib import Path
import re
import unittest

import yaml

from cpp_source_evidence import tokens


class GraphicsScopeContractTests(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def setUp(self):
        self.catalog = yaml.safe_load(
            (self.ROOT / "docs/rhi-webgl1-diagnostics.yaml").read_text(encoding="utf-8"))
        self.groups = self.catalog["groups"]

    def test_catalog_has_explicit_failure_and_recovery_boundaries(self):
        self.assertEqual(self.catalog["type"], "WebGl1DiagnosticCatalog")
        self.assertEqual(self.catalog["contractVersion"], 1)
        self.assertEqual(self.catalog["scope"], "WebGl1Graphics/1")
        self.assertEqual(self.catalog["authority"], "None")
        ids = [identifier for group in self.groups for identifier in group["ids"]]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertTrue(all(re.fullmatch(r"RHI-[A-Z0-9-]+", identifier) for identifier in ids))
        for group in self.groups:
            self.assertTrue(group["phase"])
            self.assertTrue(group["recovery"])
            self.assertIn(group["sideEffects"], ("NoneFromAdmission", "PossiblePartialIssue"))
            self.assertIn(group["status"], (
                "ImplementedAdmission", "ExistingSharedContract", "ReservedP1", "ReservedP2"))

    def test_all_current_profile_diagnostics_have_exact_catalog_entries(self):
        actual = set()
        for name in self.catalog["sourceFiles"]:
            self.assertTrue((self.ROOT / name).is_file(), name)
            for token, *_ in tokens((self.ROOT / name).read_text(encoding="utf-8")):
                if re.fullmatch(r'"RHI-WEBGL1-[A-Z0-9-]+"', token):
                    actual.add(token[1:-1])
        documented = {identifier for group in self.groups
                      if group["status"] == "ImplementedAdmission" for identifier in group["ids"]}
        self.assertEqual(actual, documented)

    def test_shared_stale_generation_uses_original_contract(self):
        groups = [group for group in self.groups if group["status"] == "ExistingSharedContract"]
        self.assertTrue(groups)
        for group in groups:
            literals = {token[1:-1] for token, *_ in tokens(
                (self.ROOT / group["sourceFile"]).read_text(encoding="utf-8"))
                if token.startswith('"') and token.endswith('"')}
            self.assertTrue(set(group["ids"]).issubset(literals))

    def test_required_categories_and_future_execution_are_not_conflated(self):
        categories = {group["category"] for group in self.groups}
        self.assertTrue({"UnsupportedFeature", "Extension", "UniformType", "UniformLimit",
                         "TextureAndSampler", "StaleGeneration", "ContextLost", "MemoryBudget",
                         "PayloadUnavailable", "NativeIssueFailure"}.issubset(categories))
        reserved = {identifier for group in self.groups if group["status"].startswith("Reserved")
                    for identifier in group["ids"]}
        implemented = {identifier for group in self.groups if group["status"] == "ImplementedAdmission"
                       for identifier in group["ids"]}
        self.assertFalse(reserved & implemented)
        partial = [group for group in self.groups if group["sideEffects"] == "PossiblePartialIssue"]
        self.assertEqual([group["category"] for group in partial], ["NativeIssueFailure"])

    def test_frozen_backend_identity_domains_are_explicit_and_distinct(self):
        def enum_values(path, name):
            source = (self.ROOT / path).read_text(encoding="utf-8")
            body = re.search(r"enum class " + name + r"\s*\{([^}]+)\}", source).group(1)
            return {key: int(value) for key, value in re.findall(r"(\w+)\s*=\s*(\d+)", body)}

        self.assertEqual(enum_values("src/rhi/gpu_common.h", "Backend"),
                         dict(Null=0, Vulkan=1, Metal=2, WebGPU=3, WebGL=4))
        self.assertEqual(enum_values("src/platform/render_backend_preference.h", "BackendPreference"),
                         dict(Default=0, Vulkan=1, Metal=2, WebGPU=3, Null=4, WebGL=5))

    def test_lowering_definition_covers_every_required_domain_and_all_backends(self):
        source = (self.ROOT / "docs/rhi-webgl1-graphics-lowering.md").read_text(encoding="utf-8")
        expected = {f"{domain}{index:02d}" for domain, count in (
            ("R", 7), ("D", 4), ("U", 2), ("G", 5), ("P", 6),
            ("A", 4), ("T", 3), ("S", 3), ("L", 3), ("Y", 2))
                    for index in range(1, count + 1)}
        rows = {}
        for line in source.splitlines():
            if not re.match(r"\| [RDUGPATSLY]\d\d ", line):
                continue
            cells = [cell.strip() for cell in line.split("|")[1:-1]]
            self.assertEqual(len(cells), 6)
            identifier = cells[0].split()[0]
            self.assertNotIn(identifier, rows)
            self.assertEqual(cells[1], "V")  # Null never supplies native execution.
            for cell in cells[2:]:
                self.assertRegex(cell, r"^[NEF](?:[ /；]|$)")
            rows[identifier] = cells
        self.assertEqual(set(rows), expected)
        self.assertIn("其当前执行结果全部为 F", source)
        self.assertIn("Unknown", source)  # Driver physical bytes are never estimates-as-facts.


if __name__ == "__main__":
    unittest.main()
