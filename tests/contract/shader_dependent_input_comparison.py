"""Original/current compiler comparison with exact, independently checked provenance projection."""
import argparse
import hashlib
from io import BytesIO, TextIOWrapper
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / "tests/fixtures/shader_dependent_input/run_compiler.py"
SNAPSHOT = ROOT / "tests/fixtures/shader_dependent_input/scripts"
parser = argparse.ArgumentParser()
parser.add_argument("--executable", type=Path, required=True)
parser.add_argument("--slangc", type=Path, required=True)
parser.add_argument("--spirv-reflect", type=Path, required=True)
parser.add_argument("--library", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--smoke", action="store_true")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
evidence_root = args.output.resolve()
evidence = Path(tempfile.mkdtemp(prefix="run-", dir=evidence_root))
evidence.resolve().relative_to(evidence_root)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inputs(directory):
    return {path.relative_to(directory).as_posix(): path.read_bytes()
            for path in sorted(directory.rglob("*")) if path.is_file()}


def tool_hash(name, original):
    return digest(((SNAPSHOT if original else ROOT / "scripts") / name).read_bytes())


def encode_yaml(value, text_output):
    text = yaml.safe_dump(value, sort_keys=False)
    if not text_output:
        return text.encode("utf-8")
    # Match these producers' declared Path.write_text(newline=None), including
    # the Windows CRLF bytes. Payload YAML uses explicit binary UTF8 instead.
    buffer = BytesIO()
    with TextIOWrapper(buffer, encoding="utf-8", newline=None) as stream:
        stream.write(text)
        stream.flush()
        return buffer.getvalue()


def roundtrip(data, text_output=False):
    value = yaml.safe_load(data)
    if encode_yaml(value, text_output) != data:
        raise AssertionError("fixture YAML is not a lossless safe_dump roundtrip")
    return value


def project(data, fields, text_output=False):
    value = roundtrip(data, text_output)
    for field, old, current in fields:
        if value[field] != old:
            raise AssertionError("original provenance does not identify sealed input: " + field)
        value[field] = current
    return encode_yaml(value, text_output)


def compare(old, current):
    if old.keys() != current.keys():
        raise AssertionError("artifact file set changed: " + repr(old.keys() ^ current.keys()))
    expected = dict(old)
    projected = []
    # Only these declared source-identity fields may change. Each must identify
    # the exact old/new producer source; no path, option, layout or operation diff
    # is ignored. Native shader/GLSL bytes are always compared byte-for-byte.
    for name, data in old.items():
        if name.endswith(".bindless.payload.yaml"):
            expected[name] = project(data, [(field, tool_hash(source, True), tool_hash(source, False))
                for field, source in (("compilerHelperSha256", "compile_shaders.py"),
                                      ("generatorSha256", "bindless_codegen.py"))])
            projected.append(name)
        elif name.endswith(".glsl-es-100.yaml") and yaml.safe_load(data)["status"] == "Available":
            expected[name] = project(data, [(field, tool_hash(source, True), tool_hash(source, False))
                for field, source in (("generatorSha256", "webgl_shader_artifact.py"),
                                      ("adapterSha256", "spirv_cross_api.py"))], text_output=True)
            projected.append(name)
    for name, data in old.items():
        if name.endswith(".bindless.yaml"):
            publication = roundtrip(data, text_output=True)
            payload = publication["payload"]
            if publication["shaderPackageIdentity"] != digest(old[payload]):
                raise AssertionError("original package identity mismatch")
            publication["shaderPackageIdentity"] = digest(expected[payload])
            expected[name] = encode_yaml(publication, text_output=True)
            projected.append(name)
        elif name.endswith(".webgl-program.yaml"):
            publication = roundtrip(data, text_output=True)
            for target in publication["targets"]:
                if target["sha256"] != digest(old[target["path"]]):
                    raise AssertionError("original stage identity mismatch")
                target["sha256"] = digest(expected[target["path"]])
            expected[name] = encode_yaml(publication, text_output=True)
            projected.append(name)
    changed = [name for name in old if expected[name] != current[name]]
    if changed:
        raise AssertionError("uncontracted artifact differences: " + repr(changed))
    for name, data in current.items():
        if name.endswith(".bindless.payload.yaml"):
            payload = yaml.safe_load(data)
            for filename, sha in payload["artifacts"].items():
                if digest(current[filename]) != sha:
                    raise AssertionError("current artifact seal mismatch")
    return projected


def bindless(name, entries, libraries=(), directory="tests/fixtures"):
    result = ["--bindless-variant", str(ROOT / directory / (name + ".yaml")),
              "--bindless-source", str(ROOT / directory / (name + ".slang"))]
    for library in libraries:
        result += ["--bindless-source-library", str(ROOT / library)]
    for entry in entries:
        result += ["--bindless-entry", entry]
    return result


cases = []
for mode in ("Native", "Emulated"):
    specs = [("compute", "bindless_metal_compute", ["cleanMain", "faultMain"], ())]
    if not args.smoke:
        specs += [("matrix", "bindless_matrix_ordinary", ["computeMain"], ()),
                  ("matrix-push", "bindless_graphics_matrix_push", ["vertexMain", "fragmentMain"], ()),
                  ("pbr-libraries", "bindless_pbr_material_record", ["vertexMain", "colorMain", "depthMain", "shadowMain"],
                   ("shaders/core/leaf_surface.slang", "shaders/core/pbr_material.slang", "shaders/renderer/bindless_pbr_material.slang"))]
    for label, fixture, entries, libraries in specs:
        cases.append((label + "-" + mode.lower(), bindless(fixture, entries, libraries) + ["--bindless-mode", mode]))
    if not args.smoke:
        visibility = ["--bindless-variant", str(ROOT / "shaders/renderer/gpu_visibility.bindless.yaml"),
                      "--bindless-source", str(ROOT / "shaders/renderer/gpu_visibility_bindless.slang")]
        for source in ("shaders/core/instance_transform.slang", "shaders/core/gpu_visibility.slang", "shaders/core/gpu_visibility_kernel.slang"):
            visibility += ["--bindless-source-library", str(ROOT / source)]
        for phase in ("Reset", "Cull", "Emit", "Finalize"):
            visibility += ["--bindless-entry", "visibility" + phase + "ComputeMain"]
        cases.append(("visibility-" + mode.lower(), visibility + ["--bindless-mode", mode]))
        consumer = list(visibility)
        consumer[consumer.index("--bindless-variant") + 1] = str(ROOT / "shaders/renderer/gpu_visibility_consumer.bindless.yaml")
        consumer[consumer.index("--bindless-source") + 1] = str(ROOT / "shaders/renderer/gpu_visibility_consumer.slang")
        consumer += ["--bindless-source-library", str(ROOT / "shaders/renderer/gpu_visibility_bindless.slang"),
                     "--bindless-entry", "visibilityVertexMain", "--bindless-entry", "visibilityFragmentMain"]
        cases.append(("visibility-consumer-" + mode.lower(), consumer + ["--bindless-mode", mode]))
webgl = ["--graphics-module", "--glsl-es-100", "--spirv-cross-library", str(args.library)]
cases.append(("es100-graphics", [*webgl, "--fixture", "webgl1_graphics", "--preserve-implicit-sampling",
    "--webgl-optional-uniform", "Vertex:arpUbo_0_0.m3", "--webgl-optional-uniform", "Fragment:arpUbo_0_0.m3"]))
if not args.smoke:
    cases += [("es100-extensions", [*webgl, "--fixture", "webgl1_extensions", "--preserve-implicit-sampling"]),
              ("es100-sprites", [*webgl, "--module", "scene_2d_vertices"]),
              ("es100-environment", [*webgl, "--module", "editor_viewport_environment"]),
              ("es100-refused-uniform", ["--fixture", "webgl1_graphics", "--single-entry", "vertexMain",
                 "--glsl-es-100", "--spirv-cross-library", str(args.library), "--preserve-implicit-sampling",
                 "--webgl-optional-uniform", "Vertex:doesNotExist"])]

reports = []
for label, selection in cases:
    case = evidence / label
    case.mkdir()
    with tempfile.TemporaryDirectory(prefix="compiled-", dir=case) as temporary:
        output = Path(temporary)
        original_files = None
        for original in (True, False):
            command = [sys.executable, "-X", "utf8", str(DRIVER), *(["--original"] if original else []),
                       "--slangc", str(args.slangc), "--spirv-reflect", str(args.spirv_reflect),
                       "--out-dir", str(output), *selection,
                       *([] if original else ["--io-tool", str(args.executable)])]
            if not original:
                # Both invocations use the identical output pathname for stable
                # reflection bindings, but selected compilation starts without
                # stale files. Every removed path is owned by this fresh fixture.
                for path in output.rglob("*"):
                    if path.is_file():
                        path.resolve().relative_to(output.resolve())
                        path.unlink()
            result = subprocess.run(command, cwd=ROOT, capture_output=True, timeout=900)
            (case / ("original.log" if original else "file-service.log")).write_bytes(result.stdout + b"\n--- stderr ---\n" + result.stderr)
            if result.returncode:
                raise RuntimeError(label + ": compiler failed\n" + result.stderr.decode("utf-8", errors="replace")[-4000:])
            if original:
                original_files = inputs(output)
                shutil.copytree(output, case / "original-output")
            else:
                log = result.stdout.decode("utf-8")
                expected_invocations = 3 if "--graphics-module" in selection else 1
                if log.count("FileService/1") != expected_invocations or "LegacyHostFile/1" in log:
                    raise AssertionError("selected source was not inherited")
                current_files = inputs(output)
                shutil.copytree(output, case / "current-output")
                projected = compare(original_files, current_files)
                if label == "es100-refused-uniform":
                    refusals = [yaml.safe_load(data) for name, data in current_files.items()
                                if name.endswith(".glsl-es-100.yaml")]
                    if len(refusals) != 1 or refusals[0]["status"] != "DiagnosticFailure" or (
                            refusals[0]["diagnosticId"] != "SHADER-WEBGL1-OPTIONAL-UNIFORM-UNKNOWN"):
                        raise AssertionError("explicit ES100 refusal changed")
                reports.append(dict(case=label, files=len(current_files), projectedProvenanceFiles=projected,
                    filesSha256={name: digest(data) for name, data in current_files.items()}))
                print(label + ": files=" + str(len(current_files)) + " provenance_only=" + str(len(projected)), flush=True)
report = dict(type="ShaderDependentInputComparison", contractVersion=1, cases=reports,
              byteIdenticalExceptDeclaredSourceProvenance=True, smoke=args.smoke)
(evidence / "comparison.yaml").write_text(yaml.safe_dump(report, sort_keys=False), encoding="utf-8")
(evidence_root / "latest.yaml").write_text(yaml.safe_dump(dict(run=evidence.name, report="comparison.yaml")), encoding="utf-8")
print("evidence=" + str(evidence))
