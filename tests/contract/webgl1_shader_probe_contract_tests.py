#!/usr/bin/env python3
"""Verify real generated probe artifacts, without granting package authority."""
import argparse
import hashlib
from pathlib import Path
import sys
import unittest

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))
from spirv_layout import SpirvLayoutFacts


class WebGl1ShaderProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = ARGS.directory.resolve()
        cls.report = yaml.safe_load((cls.directory / "reflection.yaml").read_text(encoding="utf-8"))

    def test_report_is_not_an_executable_package(self):
        self.assertEqual(self.report["type"], "WebGl1ShaderFeasibility")
        self.assertEqual(self.report["contractVersion"], 1)
        self.assertEqual(self.report["status"], "ProbeOnly")
        self.assertIs(self.report["executablePackage"], False)
        self.assertEqual(self.report["matrixLayout"], "row-major")
        self.assertEqual(self.report["glslVersion"], 100)

    def test_generated_bytes_and_tool_identity_match(self):
        self.assertEqual(self.report["sourceSha256"], hashlib.sha256(ARGS.source.read_bytes()).hexdigest())
        generator = Path(__file__).resolve().parents[2] / "scripts/webgl_shader_codegen.py"
        self.assertEqual(self.report["generatorSha256"], hashlib.sha256(generator.read_bytes()).hexdigest())
        for tool, identity in self.report["tools"].items():
            with self.subTest(tool=tool):
                self.assertEqual(identity["sha256"], hashlib.sha256(Path(identity["path"]).read_bytes()).hexdigest())
                self.assertTrue(identity.get("version") or identity.get("versionStatus"))
        for stage, report in self.report["stages"].items():
            self.assertEqual(len(report["files"]), 4)
            for name, digest in report["files"].items():
                with self.subTest(stage=stage, file=name):
                    data = (self.directory / name).read_bytes()
                    self.assertTrue(data)
                    self.assertEqual(hashlib.sha256(data).hexdigest(), digest)

    def test_uniform_layout_and_push_size_from_binary(self):
        stage = self.report["stages"]["vert"]
        facts = SpirvLayoutFacts(self.directory / "probe.vert.spv")
        buffers = facts.buffer_layouts()
        self.assertEqual(set(buffers), {(0, 0)})
        block = buffers[(0, 0)]
        self.assertEqual(block["staticOccupiedExtentBytes"], 112)
        self.assertFalse(block["hasRuntimeArray"])
        self.assertEqual(facts.matrix_stride(block["blockTypeId"], 0), 16)
        reflection = stage["spirvReflection"]
        uniform = reflection["ubos"][0]
        self.assertEqual((uniform["set"], uniform["binding"], uniform["block_size"]), (0, 0, 112))
        members = reflection["types"][uniform["type"]]["members"]
        self.assertEqual([(m["name"], m["offset"]) for m in members],
                         [("viewProjection", 0), ("tint", 64), ("values", 80)])
        array = reflection["types"][members[2]["type"]]["members"][0]
        self.assertEqual((array["name"], array["array"], array["array_stride"]), ("data", [2], 16))
        self.assertEqual(stage["pushConstant"]["size"], 16)
        self.assertEqual(stage["pushConstant"]["members"][0]["offset"], 0)

    def test_stage_interface_uses_location_contract(self):
        stages = self.report["stages"]
        outputs = stages["vert"]["spirvReflection"]["outputs"]
        inputs = stages["frag"]["spirvReflection"]["inputs"]
        self.assertEqual({(x["location"], x["type"]) for x in outputs},
                         {(x["location"], x["type"]) for x in inputs})
        for stage in ("vert", "frag"):
            generated = (self.directory / ("probe." + stage)).read_text(encoding="utf-8")
            self.assertTrue(generated.startswith("#version 100"))
            self.assertNotIn("layout(", generated)
            for item in outputs:
                self.assertRegex(generated,
                    rf'\bvarying\s+(?:(?:lowp|mediump|highp)\s+)?{item["type"]}\s+arpVarying{item["location"]}\s*;')


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    ARGS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
