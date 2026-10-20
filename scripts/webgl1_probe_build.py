"""Build a WebGL1-only feasibility fixture; never a production RHI backend."""
from datetime import datetime, timezone
import hashlib
from pathlib import Path
import shutil
import subprocess
import sys
from uuid import uuid4

import yaml


class WebGl1ProbeBuilder:
    SOURCE_INPUTS = (
        "scripts/build.py", "scripts/webgl1_probe_build.py", "scripts/webgl_shader_codegen.py",
        "scripts/compile_shaders.py", "scripts/shader_bindings_codegen.py",
        "scripts/spirv_layout.py", "tests/fixtures/webgl1_graphics_probe.slang",
        "src/rhi/semantics/webgl_shader_precision.h",
        "src/rhi/semantics/webgl_format_facts.h", "src/rhi/gpu_texture_format.h",
        "src/rhi/semantics/webgl_runtime_identity.h",
        "tests/fixtures/webgl1_runtime_probe.html", "tests/integration/rhi_webgl_runtime_probe.cpp",
    )

    def __init__(self, root, output_directory, tools):
        self.root = Path(root).resolve()
        self.output_directory = Path(output_directory).resolve()
        self.tools = {name: Path(path).resolve() for name, path in tools.items()}
        self.run_directory = None

    @staticmethod
    def identity(path):
        return dict(path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                    bytes=path.stat().st_size)

    def source_identities(self):
        return {name: self.identity(self.root / name) for name in self.SOURCE_INPUTS}

    def validate(self):
        if set(self.tools) != {"slangc", "spirvCross", "spirvReflect", "emxx"}:
            raise ValueError("RHI-WEBGL1-PROBE-BUILD-TOOLSET")
        for name, path in self.tools.items():
            if not path.is_file():
                raise ValueError("RHI-WEBGL1-PROBE-BUILD-TOOL-MISSING: " + name)
        # Outputs cannot enter production/source directories or overwrite the
        # checkout root. The explicitly named build root may be external.
        if self.output_directory == self.root:
            raise ValueError("RHI-WEBGL1-PROBE-BUILD-OUTPUT-ROOT")
        for directory in ("src", "scripts", "tests", "shaders", "docs"):
            protected = self.root / directory
            if self.output_directory == protected or protected in self.output_directory.parents:
                raise ValueError("RHI-WEBGL1-PROBE-BUILD-OUTPUT-SOURCE")
        self.source_identities()

    def commands(self, directory):
        shader = directory / "shaders"
        return [
            [sys.executable, "-X", "utf8", str(self.root / "scripts/webgl_shader_codegen.py"),
             "--source", str(self.root / "tests/fixtures/webgl1_graphics_probe.slang"),
             "--out-dir", str(shader), "--slangc", str(self.tools["slangc"]),
             "--spirv-cross", str(self.tools["spirvCross"]),
             "--spirv-reflect", str(self.tools["spirvReflect"])],
            [str(self.tools["emxx"]), str(self.root / "tests/integration/rhi_webgl_runtime_probe.cpp"),
             "-I", str(self.root / "src"),
             "-std=c++17", "-O1", "-sMIN_WEBGL_VERSION=1", "-sMAX_WEBGL_VERSION=1",
             "-sASSERTIONS=1", "-sEXIT_RUNTIME=0",
             "--preload-file", str(shader / "probe.vert") + "@/probe.vert",
             "--preload-file", str(shader / "probe.frag") + "@/probe.frag",
             "-o", str(directory / "probe.js")],
        ]

    def execute(self, command, log):
        with log.open("w", encoding="utf-8") as stream:
            subprocess.run(command, cwd=self.root, stdin=subprocess.DEVNULL,
                           stdout=stream, stderr=subprocess.STDOUT, check=True)

    def build(self):
        self.validate()
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        self.run_directory = self.output_directory / (stamp + "-" + uuid4().hex[:8])
        self.run_directory.mkdir(parents=True, exist_ok=False)
        directory = self.run_directory
        manifest_path = directory / "build-manifest.yaml"
        before = self.source_identities()
        tool_identities = {name: self.identity(path) for name, path in self.tools.items()}
        manifest = dict(type="WebGl1ProbeBuild", contractVersion=1, status="Building",
                        evidenceKind="ProbeOnly", executablePackage=False,
                        productionBackendValidated=False, gpuCompletionClaimed=False,
                        builtSuccessfully=False, collectedAt=datetime.now(timezone.utc).isoformat(),
                        sourceInputs=before, tools=tool_identities, commands=self.commands(directory),
                        stages=[])

        def save():
            manifest_path.write_text(yaml.safe_dump(manifest, sort_keys=False), encoding="utf-8")

        save()
        try:
            version_log = directory / "emscripten-version.log"
            self.execute([str(self.tools["emxx"]), "--version"], version_log)
            manifest["emscriptenVersion"] = version_log.read_text(encoding="utf-8").strip()
            save()
            for index, command in enumerate(manifest["commands"]):
                log = directory / ("shader-build.log" if index == 0 else "bridge-build.log")
                self.execute(command, log)
                manifest["stages"].append(dict(command=index, exitCode=0, log=str(log)))
                save()
            shutil.copyfile(self.root / "tests/fixtures/webgl1_runtime_probe.html", directory / "index.html")
            if before != self.source_identities() or tool_identities != {
                    name: self.identity(path) for name, path in self.tools.items()}:
                raise ValueError("RHI-WEBGL1-PROBE-BUILD-INPUT-CHANGED")
            artifacts = [directory / name for name in ("probe.js", "probe.wasm", "probe.data", "index.html")]
            artifacts += [directory / "shaders/reflection.yaml"]
            artifacts += [directory / ("shaders/probe." + stage + suffix)
                          for stage in ("vert", "frag") for suffix in ("", ".spv", ".metal", ".wgsl")]
            manifest["artifacts"] = [self.identity(path) for path in artifacts]
            manifest["status"] = "BuiltProbeOnly"
            manifest["builtSuccessfully"] = True
            save()
        except Exception as error:
            manifest["status"] = "Failed"
            manifest["diagnostic"] = str(error)
            manifest["builtSuccessfully"] = False
            if isinstance(error, subprocess.CalledProcessError):
                manifest["failedExitCode"] = error.returncode
            save()
            raise
        return manifest_path
