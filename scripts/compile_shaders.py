#!/usr/bin/env python3
"""
Shader compilation pipeline for arp-engine.

Compiles all .slang shaders to SPIR-V, MSL (Metal), WGSL targets,
applies WGSL post-processing for WebGPU compatibility,
and generates per-shader reflection metadata (YAML).

Usage:
  python compile_shaders.py [--slangc PATH] [--vulkan-sdk PATH] [--out-dir PATH]
"""

import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import shader_bindings_codegen as _bindings_codegen  # noqa: E402


ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHADER_INCLUDE_DIR = os.path.join(ROOT, "shaders")
# After the engine/wocc repo split, the wocc shader sources that the engine
# still needs to compile (because tests/contract/shader_tests.cpp #includes
# their generated .shader_bindings.h headers) live under the engine's own
# shaders/wocc/ tree as test fixtures. The canonical wocc shader sources now
# live in the wocc repo (assets/shaders/); these engine-local copies are kept
# in sync for engine-side contract testing.
WOCC_SHADER_DIR = os.path.join(ROOT, "shaders", "wocc")


# ── utility ────────────────────────────────────────────────────────────────


def write_utf8_no_bom(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def read_text(path):
    with open(path, "r", encoding="utf-8") as f:
        return f.read()


def shader_artifact_file_roles(prefix, out_dir, wgsl_diagnostic=None, metal_diagnostic=None):
    """Exact completed compiler roles shared by single-entry/module publications."""
    paths = {"source": f"{prefix}.slang", "spirv": f"{prefix}.spv",
             "metal": f"{prefix}.metal", "wgsl": f"{prefix}.wgsl",
             "reflection": f"{prefix}.reflection.yaml"}
    files = {}
    if metal_diagnostic and (wgsl_diagnostic or metal_diagnostic != "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED"):
        raise RuntimeError("SHADER-ENTRY-TARGET-DIAGNOSTIC-CONTRACT")
    for role, name in paths.items():
        if role == "metal" and metal_diagnostic:
            if os.path.exists(os.path.join(out_dir, name)):
                raise RuntimeError("SHADER-METAL-RUNTIME-LENGTH-OUTPUT-PRESENT")
            files[role] = {"status": "DiagnosticFailure", "contract": "MetalRuntimeLengthDiagnostic/1", "diagnosticId": metal_diagnostic}
            continue
        if role == "wgsl" and wgsl_diagnostic:
            contracts = {
                "SHADER-WGSL-GRAPHICS-ATOMIC-UNSUPPORTED": "WgslGraphicsAtomicDiagnostic/1",
                "SHADER-WGSL-BUFFER-ARRAY-UNSUPPORTED": "WgslBufferArrayDiagnostic/1",
                "SHADER-WGSL-BUFFER-ARRAY-ABI-UNAVAILABLE": "WgslBufferArrayDiagnostic/2",
            }
            if wgsl_diagnostic not in contracts:
                raise RuntimeError("SHADER-ENTRY-TARGET-DIAGNOSTIC-CONTRACT")
            if os.path.exists(os.path.join(out_dir, name)):
                raise RuntimeError("SHADER-ENTRY-TARGET-DIAGNOSTIC-OUTPUT-PRESENT")
            contract = contracts[wgsl_diagnostic]
            files[role] = {"status": "DiagnosticFailure", "contract": contract, "diagnosticId": wgsl_diagnostic}
            continue
        with open(os.path.join(out_dir, name), "rb") as file:
            data = file.read()
        if not data:
            raise RuntimeError("SHADER-ENTRY-FILE-EMPTY")
        files[role] = {"path": name, "sha256": hashlib.sha256(data).hexdigest()}
    return files


def publish_graphics_module_artifact(prefix, out_dir, child_publications, metal_diagnostic=None):
    """Original combined bytes with explicit sealed per-stage provenance only."""
    import yaml
    with open(os.path.join(out_dir, f"{prefix}.reflection.yaml"), encoding="utf-8") as file:
        reflection = yaml.safe_load(file)
    entries = reflection.get("entryPoints", [])
    if len(entries) != 2 or {e.get("stage") for e in entries} != {"Vertex", "Fragment"}:
        raise RuntimeError("SHADER-MODULE-ENTRY-COVERAGE")
    selected = []
    for stage in ("Vertex", "Fragment"):
        item = next(e for e in entries if e["stage"] == stage)
        path = child_publications[stage]
        with open(path, "rb") as file:
            data = file.read()
        child = yaml.safe_load(data)
        if metal_diagnostic and (child["contractVersion"] != 5 or child["files"]["metal"] != {"status": "DiagnosticFailure", "contract": "MetalRuntimeLengthDiagnostic/1", "diagnosticId": metal_diagnostic}):
            raise RuntimeError("SHADER-MODULE-RUNTIME-LENGTH-CHILD-ROLE")
        if child["stage"] != stage or child["entry"] != item["name"]:
            raise RuntimeError("SHADER-MODULE-REFERENCE-ENTRY")
        child_source = os.path.join(os.path.dirname(path), child["files"]["source"]["path"])
        with open(child_source, "rb") as file:
            source = file.read()
        if hashlib.sha256(source).hexdigest() != reflection["sourceSha256"]:
            raise RuntimeError("SHADER-MODULE-REFERENCE-SOURCE")
        selected.append({"stage": stage, "entry": item["name"], "publication": {
            "path": os.path.relpath(path, out_dir).replace(os.sep, "/"), "sha256": hashlib.sha256(data).hexdigest()}})
    output = os.path.join(out_dir, f"{prefix}.graphics.module.yaml")
    write_utf8_no_bom(output, yaml.safe_dump({"type": "GraphicsShaderModuleArtifact", "contractVersion": 2 if metal_diagnostic else 1,
        "matrixLayout": "row-major", "files": shader_artifact_file_roles(prefix, out_dir, metal_diagnostic=metal_diagnostic), "entries": selected}, sort_keys=False))
    return output


def publish_single_entry_artifact(prefix, entry, out_dir, wgsl_diagnostic=None, spirv_reflect_exe=None, metal_diagnostic=None):
    """Publish exact bytes from one completed normal cross-target compilation."""
    import yaml
    paths = {"source": f"{prefix}.slang", "spirv": f"{prefix}.spv",
             "metal": f"{prefix}.metal", "wgsl": f"{prefix}.wgsl",
             "reflection": f"{prefix}.reflection.yaml"}
    with open(os.path.join(out_dir, paths["reflection"]), encoding="utf-8") as file:
        reflection = yaml.safe_load(file)
    entries = reflection.get("entryPoints", [])
    if len(entries) != 1 or entries[0].get("name") != entry or entries[0].get("stage") not in ("Vertex", "Fragment", "Compute"):
        raise RuntimeError("SHADER-ENTRY-REFLECTION-AMBIGUOUS")
    files = shader_artifact_file_roles(prefix, out_dir, wgsl_diagnostic, metal_diagnostic)
    if not spirv_reflect_exe:
        raise RuntimeError("SHADER-ENTRY-BUFFER-LAYOUT-REFLECT-REQUIRED")
    declaration_layouts = _bindings_codegen.reflect_buffer_declaration_layouts(
        spirv_reflect_exe, os.path.join(out_dir, paths["spirv"]))
    declarations = []
    scalar_fields = ("spirvId", "blockTypeId", "storageClass", "hasRuntimeArray")
    byte_fields = ("descriptorCount", "staticOccupiedExtentBytes", "runtimeArrayOffsetBytes",
                   "runtimeArrayStrideBytes", "runtimeElementOccupiedExtentBytes")
    for (set_index, binding_index), facts in sorted(declaration_layouts.items()):
        record = {"set": set_index, "binding": binding_index}
        record.update({field: facts[field] for field in scalar_fields})
        record.update({field + "Hex": f"{facts[field]:016x}" for field in byte_fields})
        declarations.append(record)
    publication = {"type": "SingleEntryShaderArtifact", "contractVersion": 5 if metal_diagnostic else 4 if wgsl_diagnostic else 3,
                   "matrixLayout": "row-major", "entry": entry,
                   "stage": entries[0]["stage"], "files": files,
                   "bufferDeclarationLayout": {"type": "SpirvBufferDeclarationLayouts", "contractVersion": 1,
                       "status": "DeclarationOnly", "spirvSha256": files["spirv"]["sha256"], "bindings": declarations}}
    output = os.path.join(out_dir, f"{prefix}.{entry}.entry.yaml")
    write_utf8_no_bom(output, yaml.safe_dump(publication, sort_keys=False))
    return output


def strip_comments_preserving_offsets(source_text):
    """Blank comments without changing offsets used by source reflection."""
    def blank(match):
        return "".join(
            "\n" if character == "\n" else " "
            for character in match.group(0))

    return re.sub(
        r"//[^\n]*|/\*.*?\*/",
        blank,
        source_text,
        flags=re.DOTALL,
    )


def find_tool(name, env_var, fallback_paths):
    """Resolve a command-line tool: try PATH, env var, then fallback paths."""
    # Try from environment variable
    val = os.environ.get(env_var, "")
    if val and os.path.isfile(val):
        return os.path.normpath(val)

    # Try PATH
    try:
        result = subprocess.run(["which", name], capture_output=True, text=True)
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip()
    except Exception:
        pass

    # On Windows, try 'where'
    try:
        result = subprocess.run(["where", name], capture_output=True, text=True, shell=True)
        if result.returncode == 0 and result.stdout.strip():
            return result.stdout.strip().split("\n")[0].strip()
    except Exception:
        pass

    # Try fallback paths
    for p in fallback_paths:
        if os.path.isfile(p):
            return os.path.normpath(p)

    return None


# ── slangc invocation ──────────────────────────────────────────────────────


def run_slangc(slangc, source, target, profile, entries, output, include_dirs=None,
               ignore_capabilities=False, diagnostic_failure_code=None):
    """Run slangc for one source file with given entries.

    Compilation failure is fatal unless the caller names one exact diagnostic
    code and every emitted error matches it. That path yields no target bytes.
    A capability bypass is explicit and must pass output lowering checks.
    """
    if include_dirs is None:
        include_dirs = [SHADER_INCLUDE_DIR, WOCC_SHADER_DIR]
    # The non-symmetric matrix contract verifies this lowering for each target.
    cmd = [slangc, "-matrix-layout-row-major"]
    for d in include_dirs:
        cmd.extend(["-I", d])
    cmd.extend([source, "-target", target, "-profile", profile, "-o", output])
    if target == "spirv":
        # Slang otherwise renames a single selected SPIR-V entry point to
        # `main`, which disagrees with reflection and the L2 pipeline ABI.
        cmd.append("-fvk-use-entrypoint-name")
    if ignore_capabilities:
        cmd.append("-ignore-capabilities")
    for e in entries:
        cmd.extend(["-entry", e])

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        codes = re.findall(r"error\[(E\d+)\]", result.stderr)
        error_lines = [line.strip() for line in result.stderr.splitlines() if line.strip().startswith("error")]
        if diagnostic_failure_code and codes and len(codes) == len(error_lines) and all(code == diagnostic_failure_code for code in codes):
            if os.path.isfile(output):
                os.remove(output)
            print(f"  [slangc] DiagnosticFailure {diagnostic_failure_code}: {result.stderr}")
            return False
        print(f"[slangc] ERROR compiling {os.path.basename(source)} -> {target}", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        sys.exit(result.returncode)
    print(f"  [slangc] {os.path.basename(source)} -> {target}: {os.path.basename(output)}")
    return True


def lower_subpass_input_source_for_wgsl(source_text, source_path):
    """Lower logical subpass declarations for the WGSL compiler input.

    The source file remains backend-neutral. This is a compiler-side source
    transformation used only for the WGSL artifact: logical input attachments
    become sampled textures, and SubpassLoad becomes a texel load whose
    fragment coordinate is supplied by the WGSL post-pass below.
    """
    source_without_comments = re.sub(
        r"//[^\n]*|/\*.*?\*/", "", source_text, flags=re.DOTALL)

    if not re.search(
            r"\bSUBPASS_INPUT_(?:COLOR|DEPTH)\s*\(", source_without_comments):
        return source_text, False

    # The include only provides the logical macros. Remove it before replacing
    # the declarations so Slang never sees SubpassInput for the WGSL compile.
    transformed = re.sub(
        r"^\s*#include\s*[<\"](?:common/)?subpass_input\.slang[>\"]\s*$",
        "",
        source_text,
        flags=re.MULTILINE,
    )

    subpass_bindings = get_logical_subpass_bindings(source_text)

    def replace_declaration(match):
        kind = match.group(1)
        name = match.group(2)
        binding = subpass_bindings.get(name)
        if binding is None:
            raise RuntimeError(
                f"{source_path}: subpass declaration '{name}' has no logical binding")
        value_type = "float4" if kind == "COLOR" else "float"
        replacement = (
            f"[[vk::binding({binding}, 0)]] "
            f"Texture2D<{value_type}> {name}")
        return replacement

    transformed, declaration_count = re.subn(
        r"\bSUBPASS_INPUT_(COLOR|DEPTH)\s*\(\s*([A-Za-z_]\w*)\s*,\s*\d+\s*\)",
        replace_declaration,
        transformed,
    )
    if declaration_count == 0:
        raise RuntimeError(
            f"{source_path}: subpass input lowering found no declarations")

    transformed = re.sub(
        r"\bsubpassLoadColor\s*\(\s*([A-Za-z_]\w*)\s*\)",
        r"arpSubpassLoadColor(\1, int2(0, 0))",
        transformed,
    )
    transformed = re.sub(
        r"\bsubpassLoadDepth\s*\(\s*([A-Za-z_]\w*)\s*\)",
        r"arpSubpassLoadDepth(\1, int2(0, 0))",
        transformed,
    )

    helper = """
float4 arpSubpassLoadColor(Texture2D<float4> inputTexture, int2 fragmentCoord)
{
    return inputTexture.Load(int3(fragmentCoord, 0));
}

float arpSubpassLoadDepth(Texture2D<float> inputTexture, int2 fragmentCoord)
{
    return inputTexture.Load(int3(fragmentCoord, 0));
}
"""
    transformed = helper + "\n" + transformed

    # A fragment entry point that did not previously consume an interpolated
    # output still needs an explicit current-fragment position builtin.
    if re.search(r"\barpSubpassLoad(?:Color|Depth)\s*\(", transformed):
        fragment_pattern = re.compile(
            r"(\[shader\(\s*[\"']fragment[\"']\s*\)\]\s*"
            r"[A-Za-z_]\w*(?:\s*<[^>]+>)?\s+[A-Za-z_]\w*\s*\()"
            r"([^)]*)(\))",
            re.MULTILINE,
        )

        def add_fragment_position(match):
            params = match.group(2).strip()
            if "SV_Position" in params:
                return match.group(0)
            suffix = ", " if params else ""
            return match.group(1) + params + suffix + \
                "float4 arpSubpassFragmentPosition : SV_Position" + match.group(3)

        transformed = fragment_pattern.sub(add_fragment_position, transformed)

    return transformed, True


def lower_wgsl_subpass_input_depth_types(wgsl_path, source_text):
    """Use texture_depth_2d for logical depth subpass inputs.

    Slang accepts the compiler-side Texture2D<float> source used to avoid a
    target-specific Slang branch, but WebGPU's bind-group ABI distinguishes a
    depth texture from a float color texture. The logical macro kind is the
    source of truth for this final WGSL spelling.
    """
    depth_names = [
        name for kind, name, _attachment_index in re.findall(
            r"\bSUBPASS_INPUT_(COLOR|DEPTH)\s*\(\s*"
            r"([A-Za-z_]\w*)\s*,\s*(\d+)\s*\)",
            re.sub(r"//[^\n]*|/\*.*?\*/", "", source_text, flags=re.DOTALL),
        ) if kind == "DEPTH"
    ]
    if not depth_names:
        return

    text = read_text(wgsl_path)
    changed = False
    for name in depth_names:
        # Slang appends an instance suffix to module-scope resources.
        variable_pattern = (
            rf"(?P<prefix>@binding\(\d+\)\s+@group\(\d+\)\s+"
            rf"var\s+{re.escape(name)}(?:_\d+)?\s*:\s*)"
            rf"texture_2d<f32>(?P<suffix>\s*;)"
        )
        text, count = re.subn(
            variable_pattern,
            lambda match: match.group("prefix")
            + "texture_depth_2d"
            + match.group("suffix"),
            text,
        )
        changed = changed or count > 0
    # The compiler-generated helper receives the texture by value, so its
    # parameter must use the same WGSL resource type as the module binding.
    helper_pattern = (
        r"(fn\s+arpSubpassLoadDepth(?:_\d+)?\s*\([^)]*:\s*)"
        r"texture_2d<f32>"
    )
    text, helper_count = re.subn(
        helper_pattern,
        r"\1texture_depth_2d",
        text,
    )
    changed = changed or helper_count > 0
    if changed:
        write_utf8_no_bom(wgsl_path, text)


def lower_subpass_input_source_for_spirv(source_text, source_path):
    """Inject Vulkan input-attachment semantics into a SPIR-V compile input.

    The logical Slang source intentionally has no Vulkan attributes.  Vulkan's
    descriptor binding and input-attachment index are physical artifact
    details, so they are added only to the compiler-owned SPIR-V input.  This
    keeps the logical interface and the WebGPU/Metal source path identical.
    """
    source_without_comments = re.sub(
        r"//[^\n]*|/\*.*?\*/", "", source_text, flags=re.DOTALL)
    if not re.search(
            r"\bSUBPASS_INPUT_(?:COLOR|DEPTH)\s*\(", source_without_comments):
        return source_text, False

    subpass_bindings = get_logical_subpass_bindings(source_text)

    def replace_declaration(match):
        kind = match.group(1)
        name = match.group(2)
        attachment_index = int(match.group(3))
        binding = subpass_bindings.get(name)
        if binding is None:
            raise RuntimeError(
                f"{source_path}: subpass declaration '{name}' has no logical binding")
        value_type = "float4" if kind == "COLOR" else "float"
        return (
            f"[[vk::binding({binding}, 0)]] "
            f"[[vk::input_attachment_index({attachment_index})]] "
            f"SubpassInput<{value_type}> {name}")

    transformed, declaration_count = re.subn(
        r"\bSUBPASS_INPUT_(COLOR|DEPTH)\s*\(\s*([A-Za-z_]\w*)\s*,\s*(\d+)\s*\)",
        replace_declaration,
        source_text,
    )
    if declaration_count == 0:
        raise RuntimeError(
            f"{source_path}: SPIR-V subpass input lowering found no declarations")
    return transformed, True


def lower_wgsl_subpass_input_current_fragment(wgsl_path):
    """Replace placeholder zero coordinates with fragment-position texels."""
    text = read_text(wgsl_path)
    fragment_start_pattern = re.compile(
        r"@fragment\s+fn\s+[A-Za-z_]\w*\s*\(((?:[^()]|\([^)]*\))*)\)\s*"
        r"(?:->\s*[^\{]+)?\{",
        re.MULTILINE,
    )
    lowered_calls = 0
    search_offset = 0
    while True:
        match = fragment_start_pattern.search(text, search_offset)
        if not match:
            break
        body_start = match.end()
        depth = 1
        index = body_start
        while index < len(text) and depth:
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
            index += 1
        if depth != 0:
            raise RuntimeError(f"{wgsl_path}: unterminated WGSL fragment function")
        signature = match.group(1)
        position_match = re.search(
            r"@builtin\(position\)\s+([A-Za-z_]\w*)\s*:\s*vec4<f32>",
            signature,
        )
        if position_match:
            position_name = position_match.group(1)
            body = text[body_start:index - 1]
            updated_body, count = re.subn(
                r"(\barpSubpassLoad(?:Color|Depth)_\d*\s*\([^,()]+,\s*)"
                r"vec2<i32>\(\s*(?:0|i32\(0\))\s*,\s*(?:0|i32\(0\))\s*\)",
                lambda call: call.group(1)
                + f"vec2<i32>(i32({position_name}.x), i32({position_name}.y))",
                body,
            )
            if count:
                text = text[:body_start] + updated_body + text[index - 1:]
                index = body_start + len(updated_body) + 1
                lowered_calls += count
        search_offset = index

    if lowered_calls == 0:
        raise RuntimeError(
            f"{wgsl_path}: subpass input lowering found no current-fragment load calls")
    write_utf8_no_bom(wgsl_path, text)


def strip_spirv_debug(spirv_path, vulkan_sdk):
    """Patch OpSource SourceLanguage from 11 (Slang, unrecognized by older
    Vulkan validation layers) to 0 (Unknown).  spirv-opt --strip-debug fails
    on the unrecognized value, so we do a direct binary patch instead."""
    import struct
    with open(spirv_path, "rb") as f:
        data = bytearray(f.read())
    if len(data) < 20:
        return
    # SPIR-V header is 5 words (20 bytes). Instructions start at word 5.
    offset = 20
    patched = False
    while offset + 4 <= len(data):
        word = struct.unpack_from("<I", data, offset)[0]
        opcode = word & 0xFFFF
        word_count = (word >> 16) & 0xFFFF
        if word_count == 0:
            break
        if opcode == 3:  # OpSource
            # Word 1 (offset+4) is SourceLanguage
            if offset + 8 <= len(data):
                src_lang = struct.unpack_from("<I", data, offset + 4)[0]
                if src_lang == 11:
                    struct.pack_into("<I", data, offset + 4, 0)
                    patched = True
        offset += word_count * 4
    if patched:
        with open(spirv_path, "wb") as f:
            f.write(data)


# ── WGSL post-processing ───────────────────────────────────────────────────


def push_constant_lowering_contract(source_text, source_path):
    """Return unresolved target data for a logical push-constant block.

    Physical slots are selected from generated artifacts below. They must not
    be selected from a source filename or shader-name table.
    """
    if "[[vk::push_constant]]" not in source_text:
        return None
    if get_push_constant_size_bytes(source_text) == 0:
        raise RuntimeError(
            f"SHADER-PUSH-SOURCE-DECLARATION-UNSUPPORTED: {source_path}: "
            "push declaration must resolve to a nonempty supported struct; "
            "zero-size reflection cannot represent an authored push block")
    return {"webgpuGroup": None, "webgpuBinding": None, "metalBufferIndex": None}


def selected_entry_has_push_constants(spirv_path, spirv_reflect_exe):
    """SingleEntryPushConstantLiveness/1: original binary and reflection agree.

    A source block used only by another entry is absent from the selected
    SPIR-V interface. Never fabricate a Metal/WGSL slot for that absent block.
    Reflection failure or disagreement is an error, not an inactive result.
    """
    from spirv_layout import SpirvLayoutFacts
    facts = SpirvLayoutFacts(spirv_path)
    variables = [v for v in facts.variables if len(v) >= 3 and v[2] == 9]  # PushConstant
    block = _bindings_codegen.reflect_push_constant_block(spirv_reflect_exe, spirv_path)
    if len(variables) > 1 or bool(variables) != (block is not None):
        raise RuntimeError("SHADER-ENTRY-PUSH-REFLECTION-MISMATCH")
    return bool(variables)


def selected_entry_push_stage(spirv_path, spirv_reflect_exe):
    if not selected_entry_has_push_constants(spirv_path, spirv_reflect_exe):
        return None
    from spirv_layout import SpirvLayoutFacts
    entries = [ops for _, op, ops in SpirvLayoutFacts(spirv_path).instructions if op == 15]
    stages = {0: "VS", 4: "FS", 5: "CS"}  # OpEntryPoint execution model
    if len(entries) != 1 or not entries[0] or entries[0][0] not in stages:
        raise RuntimeError("SHADER-ENTRY-PUSH-STAGE")
    return stages[entries[0][0]]


def derive_wgsl_push_constant_contract(wgsl_path):
    """Select a free physical slot from the generated WGSL interface."""
    text = read_text(wgsl_path)
    declaration_pattern = re.compile(
        r'(?:(?:@binding\((\d+)\)\s+@group\((\d+)\))|'
        r'(?:@group\((\d+)\)\s+@binding\((\d+)\)))\s+'
        r'var(?:<[^>]+>)?\s+(\w+)\s*:\s*([^;]+);',
        re.MULTILINE)
    declarations = []
    for match in declaration_pattern.finditer(text):
        binding = int(match.group(1) or match.group(4))
        group = int(match.group(2) or match.group(3))
        type_name = re.sub(r'\s+', '', match.group(6))
        declarations.append((
            group,
            binding,
            re.search(r'PushConstants(?:_std430)?(?:_\d+)?$', type_name)
            is not None,
        ))
    push_declarations = [item for item in declarations if item[2]]
    if len(push_declarations) > 1:
        raise RuntimeError(
            f"{wgsl_path}: generated WGSL contains multiple push-constant blocks")
    occupied_groups = {
        group for group, _binding, is_push in declarations if not is_push}
    # Keep the generated target within WebGPU's portable four-group minimum.
    # The later compact pass moves descriptor declarations away from this
    # compiler-owned slot, so a pre-existing declaration in the selected group
    # is not a collision in the final artifact.
    reserved_group = next(
        (group for group in range(4) if group not in occupied_groups),
        0,
    )
    occupied_bindings = {
        binding for group, binding, is_push in declarations
        if group == reserved_group and not is_push
    }
    if push_declarations and push_declarations[0][0] == reserved_group:
        reserved_binding = push_declarations[0][1]
    else:
        reserved_binding = 0
        while reserved_binding in occupied_bindings:
            reserved_binding += 1
    return {"webgpuGroup": reserved_group, "webgpuBinding": reserved_binding}


def set_wgsl_push_constant_binding(wgsl_path, contract):
    """Add @binding/@group to push constant uniform blocks in WGSL output."""
    text = read_text(wgsl_path)
    replacement = (
        f'@binding({contract["webgpuBinding"]}) '
        f'@group({contract["webgpuGroup"]}) \\1')

    # Normalize an annotation emitted by the target compiler too. The target
    # artifact is authoritative, but its original group may be outside the
    # portable WebGPU range and must participate in the same explicit remap.
    annotated_pattern = (
        r'(?:(?:@binding\(\d+\)\s+@group\(\d+\))|'
        r'(?:@group\(\d+\)\s+@binding\(\d+\)))\s+'
        r'(var<uniform>\s+\w+\s*:\s*\w+PushConstants[^\r\n;]*;)')
    updated = re.sub(
        annotated_pattern, replacement, text, count=1, flags=re.MULTILINE)
    if updated != text:
        write_utf8_no_bom(wgsl_path, updated)
        return

    pattern = r'^(var<uniform>\s+\w+\s*:\s*\w+PushConstants[^\r\n;]*;)'
    updated = re.sub(pattern, replacement, text, count=1, flags=re.MULTILINE)
    if updated == text:
        return  # No push constant block found — ok

    write_utf8_no_bom(wgsl_path, updated)


def lower_wgsl_atomic_storage_buffers(wgsl_path, source_text, *, entry_filtered=False):
    """Lower generated HLSL InterlockedAdd/Or storage arrays to WGSL atomics.

    Slang's WGSL emitter can produce the atomicAdd operation when capability
    checks are bypassed, but it leaves the backing declaration as array<u32>
    and emits ordinary stores/loads for the same resource. WGSL requires an
    array<atomic<u32>> for all accesses to that resource.
    """
    atomic_source_counts = {}
    for match in re.finditer(
        r"\bInterlocked(Add|Or)\s*\(\s*([A-Za-z_]\w*)\s*\[",
        source_text,
    ):
        name, operation = match.group(2), match.group(1)
        atomic_source_counts.setdefault(name, {"Add": 0, "Or": 0})[operation] += 1
    if not atomic_source_counts:
        return

    text = read_text(wgsl_path)
    lowered_names = []
    expected_add_counts = {}
    for source_name in sorted(atomic_source_counts):
        declaration = re.compile(
            rf"(?P<prefix>var<storage,\s*read_write>\s+)"
            rf"(?P<name>{re.escape(source_name)}(?:_\d+)?)"
            rf"(?P<suffix>\s*:\s*)array<u32>;"
        )
        match = declaration.search(text)
        if not match:
            if entry_filtered and not re.search(rf"\b{re.escape(source_name)}(?:_\d+)?\b", text):
                continue
            raise RuntimeError(
                f"{wgsl_path}: atomic storage buffer '{source_name}' "
                "has no WGSL array<u32> declaration")
        actual_name = match.group("name")
        # Slang may inline/clone a helper or eliminate dead call sites. Source
        # and emitted static call counts therefore need not agree. The source
        # authorizes the operation and backing type; this ABI-only rewrite must
        # preserve every atomic call in the authoritative generated target.
        emitted_counts = {}
        for operation, source_count in atomic_source_counts[source_name].items():
            emitted_count = len(re.findall(
                rf"\batomic{operation}\s*\(\s*&\(\s*{re.escape(actual_name)}\s*\[",
                text,
            ))
            if (bool(emitted_count) and not source_count
                    or not entry_filtered and bool(emitted_count) != bool(source_count)):
                raise RuntimeError(
                    f"{wgsl_path}: atomic storage buffer '{actual_name}' "
                    f"has unsupported or missing atomic{operation} operations")
            emitted_counts[operation] = emitted_count
        text = declaration.sub(
            lambda item: (
                item.group("prefix") + actual_name + item.group("suffix")
                + "array<atomic<u32>>;"
            ),
            text,
            count=1,
        )
        lowered_names.append(actual_name)
        expected_add_counts[actual_name] = emitted_counts

    lines = []
    for line in text.splitlines(keepends=True):
        updated = line
        for actual_name in lowered_names:
            if re.search(
                rf"\b{re.escape(actual_name)}\s*\[[^\]]+\]\s*"
                r"(?:[+*/%&|^\-]=|\+\+|--)", updated):
                raise RuntimeError(
                    f"{wgsl_path}: atomic storage buffer '{actual_name}' "
                    "has an unsupported indexed write")
            assignment = re.fullmatch(
                rf"(?P<indent>\s*){re.escape(actual_name)}"
                rf"\[(?P<index>[^\]]+)\]\s*=\s*(?P<value>[^;]+);"
                rf"(?P<newline>\r?\n)?",
                updated,
            )
            if assignment:
                newline = assignment.group("newline") or ""
                updated = (
                    f"{assignment.group('indent')}atomicStore(&({actual_name}["
                    f"{assignment.group('index')}]), {assignment.group('value')});"
                    f"{newline}"
                )
                continue
            if "atomicAdd" in updated or "atomicOr" in updated or "atomicStore" in updated:
                continue
            updated = re.sub(
                rf"\b{re.escape(actual_name)}\[([^\]]+)\]",
                lambda item: f"atomicLoad(&({actual_name}[{item.group(1)}]))",
                updated,
            )
        lines.append(updated)

    validate_wgsl_atomic_storage_buffers(
        wgsl_path, "".join(lines), expected_add_counts)
    write_utf8_no_bom(wgsl_path, "".join(lines))


def validate_wgsl_atomic_storage_buffers(wgsl_path, text, expected_add_counts):
    """Verify that every lowered InterlockedAdd resource is a WGSL atomic.

    This is intentionally an output contract, rather than a text replacement
    smoke test. A stale or partially lowered declaration must never be marked
    WebGPU-available because the browser compiler will reject its pipeline.
    """
    for actual_name, expected_add_count in expected_add_counts.items():
        declaration = re.search(
            rf"var<storage,\s*read_write>\s+{re.escape(actual_name)}\s*:\s*"
            r"array<atomic<u32>>\s*;",
            text,
        )
        if not declaration:
            raise RuntimeError(
                f"{wgsl_path}: atomic storage buffer '{actual_name}' "
                "does not have an array<atomic<u32>> declaration")

        direct_store = re.search(
            rf"\b{re.escape(actual_name)}\s*\[[^\]]+\]\s*=",
            text,
        )
        if direct_store:
            raise RuntimeError(
                f"{wgsl_path}: atomic storage buffer '{actual_name}' "
                "still has a direct WGSL store")

        # Every remaining indexed occurrence must be the operand of one of
        # the three WGSL atomic operations. This catches ordinary loads as
        # well as malformed address expressions after the lowering pass.
        for access in re.finditer(
            rf"\b{re.escape(actual_name)}\s*\[[^\]]+\]", text):
            prefix = text[max(0, access.start() - 40):access.start()]
            if not re.search(
                r"atomic(?:Load|Store|Add|Or)\s*\(\s*&\(\s*$", prefix):
                raise RuntimeError(
                    f"{wgsl_path}: atomic storage buffer '{actual_name}' "
                    "has a non-atomic indexed access")

        for operation, expected_count in expected_add_count.items():
            actual_count = len(re.findall(
                rf"\batomic{operation}\s*\(\s*&\(\s*{re.escape(actual_name)}\s*\[",
                text,
            ))
            if actual_count != expected_count:
                raise RuntimeError(
                    f"{wgsl_path}: atomic storage buffer '{actual_name}' "
                    f"has {actual_count} atomic{operation} operations; expected "
                    f"{expected_count}")


def lower_wgsl_depth_integer_mip_sampling(wgsl_path, depth_bindings):
    """SampledDepth/1 only: preserve a guarded uint mip in WGSL's i32 ABI.

    Callers must supply generated integer-mip helpers, not arbitrary float-LOD
    shaders. Their level < textureNumLevels guard makes the Slang f32 conversion
    exact (a uint extent has at most 32 mip levels). No LOD is fixed or clamped.
    DepthTexture2D already gives the correct target declaration and scalar load.
    """
    if not depth_bindings:
        return
    text = read_text(wgsl_path)
    depths = set(depth_bindings)
    actual_depths = set()
    for match in _WGSL_BINDING_DECLARATION.finditer(text):
        name = re.sub(r'_\d+$', '', match.group(6))
        if name in depths:
            if re.sub(r'\s+', '', match.group(7)) != "texture_depth_2d":
                raise RuntimeError(f"{wgsl_path}: integer-mip binding is not scalar Depth2D")
            actual_depths.add(match.group(6))
    if {re.sub(r'_\d+$', '', n) for n in actual_depths} != depths:
        raise RuntimeError(f"{wgsl_path}: missing integer-mip depth binding")
    edits = []
    for call in re.finditer(r'\btextureSampleLevel\s*\(', text):
        bounds = _find_function_parameter_end(text, "textureSampleLevel", call.start())
        if bounds is None:
            raise RuntimeError(f"{wgsl_path}: unterminated sampleLevel")
        opening, closing = bounds
        arguments = _function_argument_ranges(text, opening, closing)
        resource = text[slice(*arguments[0])].strip().strip('()').strip()
        if resource not in actual_depths:
            continue
        if len(arguments) != 4:
            raise RuntimeError(f"{wgsl_path}: unsupported depth sampleLevel signature")
        start, end = arguments[-1]
        level = text[start:end].strip()
        edits.append((start, end, " i32(" + level + ")"))
    for start, end, replacement in reversed(edits):
        text = text[:start] + replacement + text[end:]
    write_utf8_no_bom(wgsl_path, text)


def set_wgsl_depth_texture_type(wgsl_path, texture_var_name):
    """Replace texture_2d<f32> with texture_depth_2d for depth textures."""
    text = read_text(wgsl_path)

    array_pattern = (
        rf'(var\s+{re.escape(texture_var_name)}\s*:\s*(?:binding_)?array\s*<\s*)'
        rf'texture_2d\s*<\s*f32\s*>(\s*,[^;]+>)')
    array_binding = bool(re.search(array_pattern, text))
    if array_binding:
        text = re.sub(array_pattern, r'\1texture_depth_2d\2', text)
    else:
        var_pattern = rf'var\s+{re.escape(texture_var_name)}\s*:\s*texture_2d<f32>'
        if not re.search(var_pattern, text):
            return
        text = re.sub(var_pattern, f'var {texture_var_name} : texture_depth_2d', text)

    # Depth reads are scalar in every expression context, including comparisons
    # and conditional expressions. Use the shared balanced-call parser so a
    # nested coordinate swizzle or neighboring color load stays unchanged.
    edits = []
    for call in re.finditer(r'\b(textureLoad|textureSample|textureSampleLevel)\s*\(', text):
        bounds = _find_function_parameter_end(text, call.group(1), call.start())
        if bounds is None:
            raise RuntimeError(f"{wgsl_path}: unterminated depth texture call")
        opening, closing = bounds
        arguments = _function_argument_ranges(text, opening, closing)
        start, end = arguments[0]
        resource = text[start:end].strip().strip('()').strip()
        matches_binding = (resource.startswith(texture_var_name + '[') and resource.endswith(']')
            if array_binding else resource == texture_var_name)
        if not matches_binding:
            continue
        suffix = re.match(r'\s*\.x\b', text[closing + 1:])
        if suffix:
            edits.append((closing + 1, closing + 1 + suffix.end()))
    for start, end in reversed(edits):
        text = text[:start] + text[end:]
    if array_binding:
        write_utf8_no_bom(wgsl_path, text)
        return

    # WGSL depth textures require an integer mip level for
    # textureSampleLevel, while Slang emits the float literal used by color
    # textures. Keep the rewrite scoped to this depth binding so color
    # samples continue to use the f32 overload.
    depth_level_pattern = (
        rf'(textureSampleLevel\(\({re.escape(texture_var_name)}\),[^\r\n]*?,\s*)'
        rf'\(0\.0f\)(\))'
    )
    text = re.sub(depth_level_pattern, r'\g<1>0u\2', text)

    write_utf8_no_bom(wgsl_path, text)


def validate_wgsl_vertex_input_interface(text, entry, reflected_attributes):
    """Check VertexInput/1 against the selected emitted entry, without rewriting.

    SPIR-V locations do not prove WGSL locations: the pinned Slang compiler
    allocates some HLSL semantics independently of vk::location. Use the emitted
    input members, including their names, so equal-format permutations also fail.
    This accepts scalar/vector inputs and input structs emitted by our compiler;
    other input grammars fail closed rather than guessing an interface.
    """
    def reject_interface():
        raise RuntimeError('WGSL-VERTEX-INPUT-INTERFACE: ' + entry)

    text = strip_comments_preserving_offsets(text)
    definitions = list(re.finditer(r'@vertex\s+fn\s+' + re.escape(entry) + r'\s*\(', text))
    if len(definitions) != 1:
        reject_interface()
    boundary = _find_function_parameter_end(text, entry, definitions[0].start())
    if boundary is None:
        reject_interface()
    actual = {}

    def inspect_field(field, allow_struct):
        field = field.strip()
        if not field:
            return
        match = re.fullmatch(r'((?:@\w+\s*\([^)]*\)\s*)*)([A-Za-z_]\w*)\s*:\s*(.+)', field, re.DOTALL)
        if match is None:
            reject_interface()
        annotations, name, value_type = match.groups()
        locations = re.findall(r'@location\s*\(\s*(\d+)\s*\)', annotations)
        if re.search(r'@builtin\s*\(', annotations):
            if locations:
                reject_interface()
            return
        if not annotations and allow_struct and re.fullmatch(r'[A-Za-z_]\w*', value_type):
            body = get_struct_body(text, value_type)
            if not body:
                reject_interface()
            wrapped = '(' + body + ')'
            for first, last in _function_argument_ranges(wrapped, 0, len(wrapped) - 1):
                inspect_field(wrapped[first:last], False)
            return
        if len(locations) != 1:
            reject_interface()
        vector = re.fullmatch(r'vec([234])\s*<\s*(f32|u32|i32)\s*>', value_type.strip())
        scalar = value_type.strip() if vector is None else vector[2]
        kind = {'f32': 'Float', 'u32': 'UInt', 'i32': 'Int'}.get(scalar)
        if kind is None:
            reject_interface()
        fmt = kind + (vector[1] if vector is not None else '')
        location = int(locations[0])
        if location in actual:
            reject_interface()
        # Slang appends an ordinal to generated member identifiers. Match the
        # authored member from reflection, not declaration order or type alone.
        actual[location] = (re.sub(r'_\d+$', '', name), fmt)

    for first, last in _function_argument_ranges(text, *boundary):
        inspect_field(text[first:last], True)
    expected = {}
    for attribute in reflected_attributes:
        location = attribute['location']
        if type(location) is not int or location < 0 or location in expected:
            reject_interface()
        expected[location] = (attribute['name'].rsplit('.', 1)[-1], attribute['format'])
    if actual != expected:
        reject_interface()


def set_wgsl_vertex_output_locations(wgsl_path):
    """Add @location() annotations to VertexOutput struct fields in WGSL."""
    text = read_text(wgsl_path)
    struct_pattern = r'struct\s+VertexOutput_\d+\s*\{(.*?)\};'

    def fix_locations(match):
        body = match.group(1)
        location = 0
        lines = body.split("\n")
        updated_lines = []
        for line in lines:
            # Skip lines already annotated with @builtin or @location
            if re.search(r'@(?:builtin|location)\(', line):
                updated_lines.append(line)
                continue
            # Annotate un-annotated fields
            field_match = re.match(r'^(\s*)([A-Za-z_]\w*\s*:\s*[^,\r\n]+,)', line)
            if field_match:
                indent = field_match.group(1)
                field = field_match.group(2)
                updated_lines.append(f'{indent}@location({location}) {field}')
                location += 1
            else:
                updated_lines.append(line)
        new_body = "\n".join(updated_lines)
        return match.group(0).replace(body, new_body)

    updated = re.sub(struct_pattern, fix_locations, text, flags=re.DOTALL)
    if updated != text:
        write_utf8_no_bom(wgsl_path, updated)


def lower_wgsl_webgpu_binding_space(wgsl_path, contract):
    """Apply the authored WebGPU push-constant binding contract."""
    text = read_text(wgsl_path)

    # Replace already-annotated push constant with reserved binding
    updated = re.sub(
        r'(?:(?:@binding\(\d+\)\s+@group\(\d+\))|(?:@group\(\d+\)\s+@binding\(\d+\)))\s+'
        r'(var<uniform>\s+\w+\s*:\s*\w+PushConstants[^\r\n;]*;)',
        f'@binding({contract["webgpuBinding"]}) @group({contract["webgpuGroup"]}) \\1',
        text, flags=re.MULTILINE,
    )
    # Also handle unannotated push constants
    updated = re.sub(
        r'^(var<uniform>\s+\w+\s*:\s*\w+PushConstants[^\r\n;]*;)',
        f'@binding({contract["webgpuBinding"]}) @group({contract["webgpuGroup"]}) \\1',
        updated, flags=re.MULTILINE,
    )
    if updated != text:
        write_utf8_no_bom(wgsl_path, updated)


def lower_wgsl_out_of_reserved_group(wgsl_path, contract):
    """Move non-push resources out of the reflection-selected group.

    The selected group/binding are lowering metadata. Any other resource in
    that group is moved to the first available non-reserved group with a
    deterministic free binding index.

    This is a generalised form of the old lower_wgsl_pbr_lights_binding.
    """
    reserved_group = contract["webgpuGroup"]
    text = read_text(wgsl_path)
    declaration_pattern = (
        r'(?:(?:@binding\((\d+)\)\s+@group\((\d+)\))|'
        r'(?:@group\((\d+)\)\s+@binding\((\d+)\)))\s+var\b')
    declarations = list(re.finditer(declaration_pattern, text))
    occupied_groups = {
        int(match.group(2) or match.group(3)) for match in declarations
    }
    target_group = 0
    while target_group == reserved_group or target_group in occupied_groups:
        target_group += 1

    existing_target_bindings = set()
    for m in re.finditer(
        rf'@binding\((\d+)\)\s+@group\({target_group}\)\s+var\b|'
        rf'@group\({target_group}\)\s+@binding\((\d+)\)\s+var\b',
        text,
    ):
        b = m.group(1) or m.group(2)
        existing_target_bindings.add(int(b))

    # Find the next available binding index for the target group.
    next_binding = 0
    while next_binding in existing_target_bindings:
        next_binding += 1

    updated = re.sub(
        rf'(?:(?:@binding\(\d+\)\s+@group\({reserved_group}\))|'
        rf'(?:@group\({reserved_group}\)\s+@binding\(\d+\)))\s+'
        rf'(var(?:<[^>]+>)?\s+\w+\s*:\s*'
        rf'(?!\w*PushConstants(?:_std430)?(?:_\d+)?\s*;)[^;]+;)',
        lambda m: f'@binding({next_binding}) @group({target_group}) {m.group(1)}',
        text,
    )
    if updated != text:
        write_utf8_no_bom(wgsl_path, updated)


# Kept for compatibility with tooling that imports the old symbol. Callers
# must provide the generated lowering contract.
lower_wgsl_pbr_lights_binding = lower_wgsl_out_of_reserved_group


def validate_wgsl_reserved_group(wgsl_path, contract):
    """Deterministically validate the selected push-constant slot."""
    text = read_text(wgsl_path)
    declarations = re.findall(
        r'(?:(@binding\((\d+)\)\s+@group\((\d+)\))|(@group\((\d+)\)\s+@binding\((\d+)\)))\s+'
        r'(var(?:<[^>]+>)?\s+(\w+)\s*:\s*(\w+)[^;]*;)', text)
    reserved = [(m[5], int(m[1] or m[4]), m[6], m[7]) for m in declarations
                if int(m[2] or m[3]) == contract["webgpuGroup"]]
    expected = (contract["webgpuBinding"])
    valid = (len(reserved) == 1 and reserved[0][1] == expected
             and "var<uniform>" in reserved[0][2]
             and re.search(r':\s*\w+PushConstants\w*\s*;', reserved[0][2]) is not None)
    if not valid:
        shader_name = os.path.basename(wgsl_path)
        print(
            f"  ERROR: reserved-binding collision in {shader_name}: "
            f"expected exactly one PushConstants uniform at @group({contract['webgpuGroup']}) "
            f"@binding({expected}), found {reserved}.",
            file=sys.stderr,
        )
        sys.exit(1)


def validate_wgsl_group0_no_collision(wgsl_path):
    """Verify that artifact lowering did not create duplicate physical slots."""
    text = read_text(wgsl_path)
    occupied_slots = set()
    for m in re.finditer(
        r'(?:(?:@binding\((\d+)\)\s+@group\((\d+)\))|'
        r'(?:@group\((\d+)\)\s+@binding\((\d+)\)))\s+var\b',
        text,
    ):
        slot = (int(m.group(2) or m.group(3)), int(m.group(1) or m.group(4)))
        if slot in occupied_slots:
            shader_name = os.path.basename(wgsl_path)
            print(
                f"  ERROR: physical WGSL binding collision in {shader_name}: "
                f"group {slot[0]} binding {slot[1]} appears multiple times. "
                f"The lowering pass created a duplicate binding assignment.",
                file=sys.stderr,
            )
            sys.exit(1)
        occupied_slots.add(slot)


def _lower_wgsl_resource_array_type(type_name, logical, wgsl_path):
    """Lower a reflected resource array to WGSL's binding-array type.

    WGSL value arrays and resource binding arrays are different contracts. A
    source descriptor array must remain a binding array; treating its emitted
    type as a scalar would make every non-zero RHI array element disappear.
    """
    capacity = int(logical.get("arrayCount", 1))
    if capacity <= 1:
        return type_name
    normalized = re.sub(r"\s+", "", type_name)
    binding_array = re.fullmatch(
        r"binding_array<(.+),(?:i32\()?([0-9]+)\)?>(?:\s*)", normalized)
    if binding_array is not None:
        if int(binding_array.group(2)) != capacity:
            raise RuntimeError(
                f"{wgsl_path}: resource array '{logical['name']}' capacity disagrees with reflection")
        return f"binding_array<{binding_array.group(1)}, {capacity}>"
    value_array = re.fullmatch(
        r"array<(.+),(?:i32\()?([0-9]+)\)?>(?:\s*)", normalized)
    if value_array is None or int(value_array.group(2)) != capacity:
        raise RuntimeError(
            f"{wgsl_path}: resource array '{logical['name']}' has no reflected WGSL binding-array capacity")
    return f"binding_array<{value_array.group(1)}, {capacity}>"


_WGSL_BINDING_DECLARATION = re.compile(
    r'(?:(?:@binding\((\d+)\)\s+@group\((\d+)\))|'
    r'(?:@group\((\d+)\)\s+@binding\((\d+)\)))\s+'
    r'var(?:<([^>]+)>)?\s+(\w+)\s*:\s*([^;]+);',
    re.MULTILINE)


def validate_wgsl_buffer_array_diagnostic_artifact(source_bindings, buffer_arrays, wgsl_path):
    """Prove the emitted unsupported ABI; never infer storage access or repair it."""
    expected = {}
    for binding in source_bindings:
        if not binding.get("stageMask", 0) or binding["type"] not in (
                "UniformBuffer", "ReadOnlyStorageBuffer", "StorageBuffer") or binding["arrayCount"] <= 1:
            continue
        key = (binding["set"], binding["binding"])
        name = re.sub(r'_\d+$', '', binding["name"])
        if buffer_arrays.get(key) != binding["arrayCount"] or name in expected:
            raise RuntimeError("SHADER-WGSL-BUFFER-ARRAY-POLICY-MISMATCH")
        expected[name] = binding
    if not expected:
        raise RuntimeError("SHADER-WGSL-BUFFER-ARRAY-POLICY-MISMATCH")
    if buffer_arrays != {(binding["set"], binding["binding"]): binding["arrayCount"]
                         for binding in expected.values()}:
        raise RuntimeError("SHADER-WGSL-BUFFER-ARRAY-POLICY-MISMATCH")
    seen = set()
    slots = set()
    for match in _WGSL_BINDING_DECLARATION.finditer(
            strip_comments_preserving_offsets(read_text(wgsl_path))):
        name = re.sub(r'_\d+$', '', match.group(6))
        if name not in expected:
            continue
        try:
            # Reuse the existing reflected descriptor-array shape decoder.
            # Some Slang versions emit value-array spelling first; ordinary
            # compaction would turn this into binding_array. No output is rewritten.
            resource_type = _lower_wgsl_resource_array_type(match.group(7), expected[name], wgsl_path)
        except RuntimeError as error:
            raise RuntimeError("SHADER-WGSL-BUFFER-ARRAY-EMISSION-MISMATCH") from error
        resource_type = re.sub(r'\s+', '', resource_type)
        array = re.fullmatch(r"binding_array<(.+),(?:i32\()?([0-9]+)\)?>", resource_type)
        slot = (int(match.group(2) or match.group(3)), int(match.group(1) or match.group(4)))
        if name in seen or slot in slots or array is None or int(array.group(2)) != expected[name]["arrayCount"]:
            raise RuntimeError("SHADER-WGSL-BUFFER-ARRAY-EMISSION-MISMATCH")
        if expected[name]["type"] != "UniformBuffer" and not array.group(1).startswith("array<"):
            raise RuntimeError("SHADER-WGSL-BUFFER-ARRAY-EMISSION-MISMATCH")
        seen.add(name)
        slots.add(slot)
    if seen != set(expected):
        raise RuntimeError("SHADER-WGSL-BUFFER-ARRAY-EMISSION-MISMATCH")


def compact_wgsl_binding_space(wgsl_path, source_bindings, push_contract=None):
    """Emit the final WebGpuBindingSpace/5 physical group and binding map.

    Logical descriptor sets are kept intact as a unit, but several sets may be
    packed into one physical WebGPU group.  Every active declaration gets a
    fresh binding number within that group, so the final WGSL annotation and
    reflection table are generated from exactly the same deterministic map.
    The push-constant group remains reserved when the selected shader has one.
    """
    text = read_text(wgsl_path)
    declaration_pattern = _WGSL_BINDING_DECLARATION
    logical_by_name = {}
    for binding in source_bindings:
        logical_name = re.sub(r'_\d+$', '', str(binding["name"]))
        previous = logical_by_name.get(logical_name)
        if previous is not None and (
                previous.get("set") != binding.get("set")
                or previous.get("binding") != binding.get("binding")):
            raise RuntimeError(
                f"{wgsl_path}: logical resource name '{logical_name}' is ambiguous")
        logical_by_name[logical_name] = binding
    declarations = list(declaration_pattern.finditer(text))
    descriptor_declarations = []
    for match in declarations:
        name = re.sub(r'_\d+$', '', match.group(6))
        type_name = re.sub(r'\s+', '', match.group(7))
        if "PushConstants" in type_name:
            continue
        logical = logical_by_name.get(name)
        if logical is None:
            raise RuntimeError(
                f"{wgsl_path}: WGSL resource '{name}' has no logical binding metadata")
        descriptor_declarations.append((match, logical))

    reserved_group = (
        int(push_contract["webgpuGroup"])
        if push_contract is not None else None)
    logical_sets = sorted({int(logical["set"]) for _, logical in descriptor_declarations})
    # Four groups are portable, but the generated artifact may legitimately
    # need more when logical sets cannot be merged.  Such an artifact is
    # accepted only after the runtime proves maxBindGroups; keeping the
    # allocation dynamic here makes that runtime contract reachable without
    # guessing from logical set numbers.
    physical_group_count = max(
        4,
        len(logical_sets) + (1 if reserved_group is not None else 0),
    )
    physical_groups = [
        group for group in range(physical_group_count)
        if group != reserved_group
    ]
    if descriptor_declarations and not physical_groups:
        raise RuntimeError(
            f"{wgsl_path}: WebGPU compact remap has no descriptor group available")

    set_to_group = {
        logical_set: physical_groups[index % len(physical_groups)]
        for index, logical_set in enumerate(logical_sets)
    }
    # Rebuild each declaration from the match groups so qualifiers and the
    # original resource type remain stable while the annotation is replaced.
    replacement_by_span = {}
    next_binding_by_group = {group: 0 for group in physical_groups}
    for match, logical in sorted(
            descriptor_declarations,
            key=lambda item: (
                int(item[1]["set"]), int(item[1]["binding"]),
                item[0].start())):
        group = set_to_group[int(logical["set"])]
        binding = next_binding_by_group[group]
        next_binding_by_group[group] += 1
        physical_type = _lower_wgsl_resource_array_type(
            match.group(7), logical, wgsl_path)
        replacement_by_span[match.span()] = (
            f"@binding({binding}) @group({group}) "
            f"var{('<' + match.group(5) + '>') if match.group(5) else ''} "
            f"{match.group(6)} : {physical_type};"
        )

    updated_parts = []
    cursor = 0
    for match in declarations:
        replacement = replacement_by_span.get(match.span())
        if replacement is None:
            continue
        updated_parts.append(text[cursor:match.start()])
        updated_parts.append(replacement)
        cursor = match.end()
    if updated_parts:
        updated_parts.append(text[cursor:])
        write_utf8_no_bom(wgsl_path, ''.join(updated_parts))


def get_webgpu_binding_space_lowering(source_bindings, wgsl_path):
    """Return the physical WGSL location or inactive status for each L2 binding.

    Slang's Vulkan binding annotations are the logical RHI contract. WGSL may
    use another physical group/binding assignment (for example to preserve the
    push-constant reservation), but the translation must be explicit and
    versioned. Runtime code must never infer it from a descriptor-layout name.
    """
    text = read_text(wgsl_path)
    declaration_pattern = _WGSL_BINDING_DECLARATION
    physical_by_name = {}
    for match in declaration_pattern.finditer(text):
        physical_binding = int(match.group(1) or match.group(4))
        physical_group = int(match.group(2) or match.group(3))
        # Slang's WGSL emitter adds an instance suffix such as ``gLights_0``.
        name = re.sub(r'_\d+$', '', match.group(6))
        if name in physical_by_name:
            raise RuntimeError(
                f"{wgsl_path}: WGSL emits duplicate resource name '{name}'")
        address_space = (match.group(5) or "").strip()
        type_name = re.sub(r'\s+', '', match.group(7))
        resource_type_name = type_name
        binding_array = re.fullmatch(
            r"binding_array<(.+),(?:i32\()?([0-9]+)\)?>", type_name)
        if binding_array is not None:
            resource_type_name = binding_array.group(1)
        if type_name.startswith("texture_storage"):
            physical_type = "StorageTexture"
        elif resource_type_name.startswith("texture_storage"):
            physical_type = "StorageTexture"
        elif resource_type_name.startswith("texture_"):
            physical_type = "SampledTexture"
        elif resource_type_name.startswith("sampler"):
            physical_type = "Sampler"
        elif address_space.startswith("uniform"):
            physical_type = "UniformBuffer"
        elif address_space.startswith("storage"):
            physical_type = (
                "ReadOnlyStorageBuffer"
                if address_space.replace(" ", "") == "storage,read"
                else "StorageBuffer")
        else:
            raise RuntimeError(
                f"{wgsl_path}: unsupported WGSL resource declaration '{match.group(0).strip()}'")
        # Slang's logical Texture2D<float> does not carry the WebGPU depth
        # sample type. The final WGSL artifact is authoritative for this
        # backend-neutral semantic and must be reflected in the YAML contract.
        for logical in source_bindings:
            if logical["name"] == name and physical_type == "SampledTexture":
                if logical.get("depthTexture", False) and not resource_type_name.startswith("texture_depth"):
                    raise RuntimeError(
                        f"{wgsl_path}: explicit sampled-depth resource '{name}' has a color WGSL artifact")
                logical["depthTexture"] = resource_type_name.startswith("texture_depth")
                break
        logical = next(
            (candidate for candidate in source_bindings
             if candidate["name"] == name),
            None,
        )
        capacity = int(logical.get("arrayCount", 1)) if logical else 1
        if capacity > 1:
            if binding_array is None or int(binding_array.group(2)) != capacity:
                raise RuntimeError(
                    f"{wgsl_path}: resource array '{name}' does not expose binding_array capacity {capacity}")
        physical_by_name[name] = (
            physical_group, physical_binding, physical_type, capacity)

    lowering = []
    occupied_physical_slots = {}
    for logical in source_bindings:
        name = logical["name"]
        location = physical_by_name.get(name)
        if location is None:
            # Slang legitimately removes resources that no selected entry point
            # reads (water's reusable BaseColorTexture is one example). Keep
            # that fact in the artifact rather than fabricating a physical
            # location. Native lowering must reject an attempt to bind it.
            lowering.append({
                "name": name,
                "set": logical["set"],
                "binding": logical["binding"],
                "active": False,
            })
            continue
        physical_group, physical_binding, physical_type, capacity = location
        if capacity != int(logical.get("arrayCount", 1)):
            raise RuntimeError(
                f"{wgsl_path}: resource array '{name}' capacity disagrees with reflection")
        physical_key = (physical_group, physical_binding)
        previous = occupied_physical_slots.get(physical_key)
        if previous is not None:
            raise RuntimeError(
                f"{wgsl_path}: logical bindings '{previous}' and '{name}' map to "
                f"the same WGSL @group({physical_group}) @binding({physical_binding})")
        occupied_physical_slots[physical_key] = name
        entry = {
            "name": name,
            "set": logical["set"],
            "binding": logical["binding"],
            "group": physical_group,
            "webGpuBinding": physical_binding,
            "active": True,
            "physicalType": physical_type,
            "arrayElement": 0,
            "capacity": capacity,
        }
        if logical.get("depthTexture", False):
            entry["depthTexture"] = True
        # This is intentionally metadata, not a Slang target branch. The
        # compiler/RHI lowering owns the semantic conversion from a logical
        # input attachment to a physical textureLoad operation.
        if logical["type"] == "InputAttachment":
            if physical_type != "SampledTexture":
                raise RuntimeError(
                    f"{wgsl_path}: InputAttachment '{name}' did not lower to a sampled texture")
            entry["loadSemantics"] = "CurrentFragment"
            entry["requiresSampler"] = False
            entry["depthTexture"] = bool(logical.get("depthTexture", False))
        lowering.append(entry)
    return lowering


def _metal_resource_kind(descriptor_type):
    return ("buffer" if descriptor_type in {
        "UniformBuffer", "StorageBuffer", "ReadOnlyStorageBuffer"
    } else "sampler" if descriptor_type == "Sampler" else "texture")


def get_metal_binding_space_lowering(source_bindings, metal_path):
    """Return the physical Metal slot for every logical descriptor binding.

    Metal keeps independent namespaces for buffers, textures, and samplers.
    The generated MSL entry-point signatures are therefore the authoritative
    artifact for this target; the Vulkan-style logical binding number must
    never be reused as a Metal index.
    """
    text = read_text(metal_path)
    resource_pattern = re.compile(
        r'\b(?P<name>[A-Za-z_]\w*?)(?:_\d+)?\s*'
        r'\[\[(?P<kind>buffer|texture|sampler)\((?P<slot>\d+)\)\]\]')
    physical_by_name = {}
    for match in resource_pattern.finditer(text):
        name = match.group("name")
        kind = match.group("kind")
        slot = int(match.group("slot"))
        previous = physical_by_name.get(name)
        location = (kind, slot)
        if previous is not None and previous != location:
            raise RuntimeError(
                f"{metal_path}: logical resource '{name}' has inconsistent "
                f"Metal slots {previous} and {location}")
        physical_by_name[name] = location

    occupied_slots = {}
    lowering = []
    for logical in source_bindings:
        name = logical["name"]
        location = physical_by_name.get(name)
        if location is None:
            lowering.append({
                "name": name,
                "set": logical["set"],
                "binding": logical["binding"],
                "active": False,
            })
            continue

        kind, slot = location
        expected_kind = _metal_resource_kind(logical["type"])
        if kind != expected_kind:
            raise RuntimeError(
                f"{metal_path}: logical resource '{name}' has Metal kind "
                f"'{kind}', expected '{expected_kind}'")
        physical_key = (kind, slot)
        previous = occupied_slots.get(physical_key)
        if previous is not None and previous != name:
            raise RuntimeError(
                f"{metal_path}: logical resources '{previous}' and '{name}' "
                f"share Metal {kind}({slot})")
        occupied_slots[physical_key] = name
        lowering.append({
            "name": name,
            "set": logical["set"],
            "binding": logical["binding"],
            "metalSlot": slot,
            "active": True,
            "physicalType": logical["type"],
        })
    return lowering


def lower_metal_subpass_source(source_text, source_path):
    """Prepare a Metal-only source for framebuffer-fetch artifact lowering.

    Slang's logical ``SubpassInput`` type is intentionally backend-neutral and
    is not an executable Metal ABI.  The temporary Metal source therefore
    keeps only typed globals as placeholders; the generated MSL pass below
    removes them and injects the real fragment input attributes.  The source
    file in the repository remains untouched.
    """
    source_without_comments = re.sub(
        r"//[^\n]*|/\*.*?\*/", "", source_text, flags=re.DOTALL)
    declarations = re.findall(
        r"\bSUBPASS_INPUT_(COLOR|DEPTH)\s*\(\s*"
        r"([A-Za-z_]\w*)\s*,\s*(\d+)\s*\)",
        source_without_comments,
    )
    if not declarations:
        return source_text, []

    transformed = re.sub(
        r"^\s*#include\s*[<\"](?:common/)?subpass_input\.slang[>\"]\s*$",
        "",
        source_text,
        flags=re.MULTILINE,
    )
    fetches = []
    logical_bindings = get_logical_subpass_bindings(source_text)
    for kind, name, attachment in declarations:
        value_type = "float4" if kind == "COLOR" else "float"
        transformed, count = re.subn(
            rf"\bSUBPASS_INPUT_{kind}\s*\(\s*{re.escape(name)}\s*,\s*"
            rf"{re.escape(attachment)}\s*\)",
            f"{value_type} {name};",
            transformed,
        )
        if count != 1:
            raise RuntimeError(
                f"{source_path}: Metal subpass declaration '{name}' was not lowered")
        transformed = re.sub(
            rf"\bsubpassLoad(?:Color|Depth)\s*\(\s*{re.escape(name)}\s*\)",
            name,
            transformed,
        )
        fetches.append({
            "name": name,
            "binding": logical_bindings[name],
            "attachment": int(attachment),
            "depth": kind == "DEPTH",
        })
    return transformed, fetches


def _find_function_parameter_end(text, entry_name, start=0):
    match = re.search(rf"\b{re.escape(entry_name)}\s*\(", text[start:])
    if match is None:
        return None
    open_index = start + match.end() - 1
    depth = 1
    index = open_index + 1
    while index < len(text) and depth:
        if text[index] == "(":
            depth += 1
        elif text[index] == ")":
            depth -= 1
        index += 1
    if depth != 0:
        return None
    return open_index, index - 1


def _function_argument_ranges(text, opening, closing):
    """Split a known call boundary without splitting nested expressions."""
    result = []
    start = opening + 1
    stack = []
    for index in range(start, closing):
        char = text[index]
        if char in "([{":
            stack.append(char)
        elif char in ")]}":
            if not stack or '([{'.index(stack.pop()) != ')]}'.index(char):
                raise RuntimeError("Unbalanced function argument")
        elif char == ',' and not stack:
            result.append((start, index)); start = index + 1
    if stack:
        raise RuntimeError("Unbalanced function argument")
    result.append((start, closing))
    return result


def lower_metal_framebuffer_fetch_artifact(
    metal_path, fetches, entry_points):
    """Inject real MSL framebuffer-fetch parameters and evidence markers."""
    if not fetches:
        return None
    text = read_text(metal_path)
    additions = []
    for fetch in fetches:
        name = fetch["name"]
        value_type = "float" if fetch["depth"] else "float4"
        attribute = "[[depth(any)]]" if fetch["depth"] else \
            f"[[color({fetch['attachment']})]]"
        declaration = re.compile(
            rf"\b(?:constant\s+)?{value_type}\s+{re.escape(name)}\s*;")
        text, declaration_count = declaration.subn("", text)
        if declaration_count == 0:
            raise RuntimeError(
                f"{metal_path}: Metal framebuffer-fetch placeholder '{name}' "
                "was not emitted by the compiler")
        additions.append(f"{value_type} {name} {attribute}")

    fragment_entries = [
        entry["name"] for entry in entry_points if entry["stage"] == "Fragment"
    ]
    if not fragment_entries:
        raise RuntimeError(
            f"{metal_path}: framebuffer-fetch declarations require a fragment entry point")
    if len(fragment_entries) != 1:
        raise RuntimeError(
            f"{metal_path}: framebuffer-fetch lowering requires exactly one fragment "
            "entry point so the injected attachment ABI is unambiguous")
    for entry_name in fragment_entries:
        location = _find_function_parameter_end(text, entry_name)
        if location is None:
            raise RuntimeError(
                f"{metal_path}: fragment entry '{entry_name}' was not emitted")
        open_index, close_index = location
        params = text[open_index + 1:close_index].strip()
        suffix = ", " if params else ""
        text = text[:close_index] + suffix + ", ".join(additions) + text[close_index:]

    markers = "\n".join(
        f"/* ARP_METAL_FRAMEBUFFER_FETCH name={fetch['name']} set=0 "
        f"binding={fetch['binding']} attachment={fetch['attachment']} "
        f"color={fetch['attachment'] if not fetch['depth'] else 0xffffffff} "
        f"depth={1 if fetch['depth'] else 0} */"
        for index, fetch in enumerate(fetches)
    )
    write_utf8_no_bom(metal_path, markers + "\n" + text)
    return fetches


def _metal_entry_parameters(text):
    """Yield exact parameter spans and body starts in emitted Metal entries.

    Template/resource arrays contain commas and int(N) expressions. Splitting
    whole signature lines or matching through a comma loses their type/identity.
    Reuse the balanced function-parameter boundary before splitting at depth zero.
    """
    entries = re.finditer(
        r'(?:\[\[(?:vertex|fragment|kernel)\]\]|\b(?:vertex|fragment|kernel))'
        r'\s+\w+\s+(?P<name>\w+)\s*\(', text)
    for entry in entries:
        bounds = _find_function_parameter_end(text, entry.group("name"), entry.start())
        if bounds is None:
            raise RuntimeError("Metal entry has unterminated parameter list")
        opening, closing = bounds
        body = closing + 1
        while body < len(text) and text[body].isspace():
            body += 1
        if body >= len(text) or text[body] != "{":
            raise RuntimeError("Metal entry has no function body")
        start = opening + 1
        depth = 0
        for index in range(start, closing + 1):
            char = text[index]
            if index == closing or (char == ',' and depth == 0):
                left, right = start, index
                while left < right and text[left].isspace(): left += 1
                while right > left and text[right - 1].isspace(): right -= 1
                if left < right:
                    yield text[left:right], left, right, body, entry.start()
                start = index + 1
            elif char in '<([{': depth += 1
            elif char in '>)]}': depth -= 1


def lower_metal_argument_buffer_artifact(metal_path, source_bindings, descriptor_stage_masks=None):
    """Lower explicit Slang resource arrays to Metal argument buffers.

    An array resource is the source-level table ABI.  Its capacity is carried
    by reflection and is never inferred from a backend limit.  Each array gets
    one argument-buffer struct with a single ``[[id(0)]]`` member; table entry
    indices are then added by the native encoder.  Scalar resources stay on
    the direct Metal binding path.
    """
    array_bindings = [
        binding for binding in source_bindings
        if binding.get("arrayCount", 1) > 1
        and binding.get("type") != "InputAttachment"
    ]
    if not array_bindings:
        return None
    text = read_text(metal_path)
    occupied_buffers = {
        int(match.group(1)) for match in re.finditer(
            r"\[\[buffer\((\d+)\)\]\]", text)
    }
    next_buffer = 0
    markers = []
    lowered = []
    for binding in array_bindings:
        name = binding["name"]
        capacity = binding["arrayCount"]
        declaration = re.compile(
            rf"^(?P<type>.+?)\s+(?P<actual>{re.escape(name)}(?:_\d+)?)"
            rf"\s*(?:\[\s*(?P<suffix>\d+)\s*\])?\s*"
            rf"(?:\[\[(?P<kind>buffer|texture|sampler)\((?P<slot>\d+)\)\]\])?$",
            re.DOTALL)
        matches = [(match, start, end, body, entry_start)
                   for parameter, start, end, body, entry_start in _metal_entry_parameters(text)
                   if (match := declaration.fullmatch(parameter)) is not None]
        if not matches:
            if descriptor_stage_masks is not None and (int(binding["set"]), int(binding["binding"])) not in descriptor_stage_masks:
                # Only authoritative target reflection may prove that this
                # selected entry does not consume an authored array.
                continue
            raise RuntimeError(
                f"{metal_path}: array resource '{name}' has no emitted Metal declaration")
        member_type = None
        declaration_signature = None
        readonly_element_type = None
        replacements = []
        aliases = []
        for match, start, end, body, entry_start in matches:
            kind = match.group("kind")
            expected_kind = _metal_resource_kind(binding["type"])
            if kind is not None and kind != expected_kind:
                raise RuntimeError(
                    f"{metal_path}: array resource '{name}' has Metal kind "
                    f"'{kind}', expected '{expected_kind}'")
            emitted_type = match.group("type").strip()
            suffix = match.group("suffix")
            if suffix is not None:
                if int(suffix) != capacity:
                    raise RuntimeError(
                        f"{metal_path}: array resource '{name}' capacity disagrees with reflection")
                candidate_member_type = f"array<{emitted_type}, {capacity}>"
            else:
                capacity_match = re.fullmatch(
                    r"array\s*<(?P<element>.+),\s*(?:int\(\s*(?P<wrapped>\d+)\s*\)|(?P<plain>\d+))\s*>", emitted_type,
                    re.DOTALL)
                if capacity_match is None or int(
                        capacity_match.group("wrapped") or capacity_match.group("plain")) != capacity:
                    raise RuntimeError(
                        f"{metal_path}: array resource '{name}' has no reflected Metal array capacity")
                candidate_member_type = f"array<{capacity_match.group('element').strip()}, {capacity}>"

            if binding["type"] == "ReadOnlyStorageBuffer":
                element_match = re.fullmatch(r"array<(.+),\s*\d+>", candidate_member_type, re.DOTALL)
                element_type = element_match.group(1).strip()
                pointer = re.fullmatch(r"(?:(?:const\s+)?([A-Za-z_]\w*)\s+device|device\s+(?:const\s+)?([A-Za-z_]\w*))\s*\*", element_type)
                if pointer is None:
                    raise RuntimeError(f"{metal_path}: readonly buffer array '{name}' has unsupported native pointer type")
                candidate_member_type = f"array<const {pointer.group(1) or pointer.group(2)} device*, {capacity}>"
                if readonly_element_type is not None and readonly_element_type != element_type:
                    raise RuntimeError(f"{metal_path}: readonly buffer array '{name}' has inconsistent pointer declarations")
                readonly_element_type = element_type

            signature = (
                re.sub(r"\s+", " ", candidate_member_type),
                match.group("kind"),
                int(match.group("slot")) if match.group("slot") is not None else None,
            )
            if declaration_signature is None:
                declaration_signature = signature
                member_type = candidate_member_type
            elif signature != declaration_signature:
                raise RuntimeError(
                    f"{metal_path}: array resource '{name}' has inconsistent declarations "
                    "across Metal entry points")
            replacements.append((start, end))
            aliases.append((body + 1, match.group("actual")))

        while next_buffer in occupied_buffers:
            next_buffer += 1
        if next_buffer >= 31:
            raise RuntimeError(
                f"{metal_path}: no Metal buffer slot is available for argument buffer '{name}'")
        argument_slot = next_buffer
        occupied_buffers.add(argument_slot)
        next_buffer += 1
        struct_name = f"ArpMetalArgumentBuffer_{name}"
        argument_name = f"arpMetalArgs_{name}"
        replacement = (
            f"constant {struct_name}& {argument_name} "
            f"[[buffer({argument_slot})]]")
        edits = [(start, end, replacement) for start, end in replacements]
        edits.extend((body, body, f"\n    auto {actual} = {argument_name}.{name};\n")
                     for body, actual in aliases)
        # Insert at the first entry that actually uses this type. Its pointee
        # structs/aliases are already declared here in the original valid MSL.
        # Inserting after 'using namespace metal' can precede uniform structs;
        # inserting all tables at the first entry can precede later-entry types.
        first_use = min(match[4] for match in matches)
        edits.append((first_use, first_use,
            f"struct {struct_name} {{ {member_type} {name} [[id(0)]]; }};\n"))
        for start, end, value in sorted(edits, reverse=True):
            text = text[:start] + value + text[end:]
        if readonly_element_type:
            # Match only the exact compiler-emitted context array declarations
            # for this logical binding/type/count. auto locals inherit const.
            context = re.compile(rf"array\s*<{re.escape(readonly_element_type)}\s*,\s*(?:int\(\s*{capacity}\s*\)|{capacity})\s*>"
                rf"\s+(?P<actual>{re.escape(name)}(?:_\d+)?)\s*;")
            pointee = re.fullmatch(r"array<const (.+) device\*,\s*\d+>", member_type).group(1)
            text = context.sub(lambda match: f"array<const {pointee} device*, {capacity}> {match.group('actual')};", text)
        markers.append(
            f"/* ARP_METAL_ARGUMENT_BUFFER name={name} set={binding['set']} "
            f"binding={binding['binding']} slot={argument_slot} index=0 "
            f"capacity={capacity} type={binding['type']} */")
        lowered.append(
            {
                "name": name,
                "set": binding["set"],
                "binding": binding["binding"],
                "argumentBufferSlot": argument_slot,
                "argumentIndex": 0,
                "capacity": capacity,
                "active": True,
                "physicalType": binding["type"],
            })
    if not lowered:
        return lowered
    namespace_match = re.search(r"\busing\s+namespace\s+metal\s*;", text)
    if namespace_match is None:
        raise RuntimeError(
            f"{metal_path}: generated MSL has no metal namespace declaration")
    write_utf8_no_bom(
        metal_path,
        "\n".join(markers) + "\n" + text,
    )
    return lowered


def get_metal_argument_buffer_lowering(source_bindings, metal_path):
    """Read the explicit argument-buffer lowering emitted in generated MSL.

    Argument buffers are a separate Metal ABI from direct resource slots.  A
    table is therefore accepted only when the generated artifact carries an
    executable marker for every logical array/table binding.  The marker is
    emitted by the Metal source lowering pass and is deliberately parsed from
    the generated MSL, rather than reconstructed from the Vulkan binding
    number.

    Marker grammar (kept as a comment in MSL so it is compiler-version
    independent):

      /* ARP_METAL_ARGUMENT_BUFFER name=Foo set=0 binding=3 slot=7
         index=2 capacity=32 type=SampledTexture */
    """
    text = read_text(metal_path)
    marker = re.compile(
        r"/\*\s*ARP_METAL_ARGUMENT_BUFFER\s+"
        r"name=(?P<name>[A-Za-z_]\w*)\s+"
        r"set=(?P<set>\d+)\s+"
        r"binding=(?P<binding>\d+)\s+"
        r"slot=(?P<slot>\d+)\s+"
        r"index=(?P<index>\d+)\s+"
        r"capacity=(?P<capacity>\d+)\s+"
        r"type=(?P<type>UniformBuffer|StorageBuffer|ReadOnlyStorageBuffer|"
        r"SampledTexture|StorageTexture|Sampler)\s*\*/")
    by_key = {}
    for match in marker.finditer(text):
        item = {
            "name": match.group("name"),
            "set": int(match.group("set")),
            "binding": int(match.group("binding")),
            "argumentBufferSlot": int(match.group("slot")),
            "argumentIndex": int(match.group("index")),
            "capacity": int(match.group("capacity")),
            "active": True,
            "physicalType": match.group("type"),
        }
        key = (item["set"], item["binding"])
        previous = by_key.get(key)
        if previous is not None and previous != item:
            raise RuntimeError(
                f"{metal_path}: logical argument-buffer binding {key} has "
                "inconsistent lowering metadata")
        by_key[key] = item

    lowering = []
    for logical in source_bindings:
        item = by_key.get((logical["set"], logical["binding"]))
        if item is None:
            lowering.append({
                "name": logical["name"],
                "set": logical["set"],
                "binding": logical["binding"],
                "active": False,
            })
            continue
        if item["name"] != logical["name"]:
            raise RuntimeError(
                f"{metal_path}: argument-buffer binding name disagrees with "
                f"logical binding {logical['name']}")
        if item["physicalType"] != logical["type"]:
            raise RuntimeError(
                f"{metal_path}: argument-buffer binding '{logical['name']}' "
                "has a different physical descriptor type")
        if logical.get("arrayCount", 1) != item["capacity"]:
            raise RuntimeError(
                f"{metal_path}: argument-buffer binding '{logical['name']}' "
                "capacity disagrees with logical reflection")
        if logical.get("arrayCount", 1) <= 1:
            raise RuntimeError(
                f"{metal_path}: scalar binding '{logical['name']}' was lowered "
                "as an argument buffer")
        if item["capacity"] == 0:
            raise RuntimeError(
                f"{metal_path}: argument-buffer binding '{logical['name']}' "
                "has zero capacity")
        # The marker is evidence only when the generated artifact contains
        # the matching argument-buffer ABI. A free-standing comment must not
        # promote a descriptor table to an executable lowering.
        member = re.search(
            r"\b" + re.escape(item["name"]) + r"\b[^;{}]*"
            r"\[\[id\(" + str(item["argumentIndex"]) + r"\)\]\]",
            text,
        )
        buffer_slot = re.search(
            r"\bconstant\s+ArpMetalArgumentBuffer_"
            + re.escape(item["name"])
            + r"\s*&\s+arpMetalArgs_"
            + re.escape(item["name"])
            + r"\s+\[\[buffer\("
            + str(item["argumentBufferSlot"])
            + r"\)\]\]",
            text,
        )
        if member is None or buffer_slot is None:
            raise RuntimeError(
                f"{metal_path}: argument-buffer marker for '{item['name']}' "
                "is not backed by an emitted [[id(...)]] member and "
                "[[buffer(...)]] parameter")
        if item["capacity"] > 1:
            array_member = re.search(
                r"array\s*<[^;{}]*,\s*" + str(item["capacity"])
                + r"\s*>[^;{}]*\b" + re.escape(item["name"])
                + r"\b[^;{}]*\[\[id\(",
                text,
            )
            if array_member is None:
                raise RuntimeError(
                    f"{metal_path}: argument-buffer table '{item['name']}' "
                    "does not expose the reflected array capacity")
        lowering.append(item)
    return lowering


def get_metal_framebuffer_fetch_lowering(source_bindings, metal_path):
    """Read explicit framebuffer-fetch evidence from generated MSL.

    The logical attachment index and color/depth identity come from the
    source reflection; the physical color slot must be stated by the emitted
    Metal artifact.  This prevents a color fetch from being reused for a
    depth input merely because both are descriptor-like values.
    """
    text = read_text(metal_path)
    marker = re.compile(
        r"/\*\s*ARP_METAL_FRAMEBUFFER_FETCH\s+"
        r"name=(?P<name>[A-Za-z_]\w*)\s+"
        r"set=(?P<set>\d+)\s+"
        r"binding=(?P<binding>\d+)\s+"
        r"attachment=(?P<attachment>\d+)\s+"
        r"color=(?P<color>\d+)\s+"
        r"depth=(?P<depth>[01])\s*\*/")
    by_key = {}
    for match in marker.finditer(text):
        item = {
            "name": match.group("name"),
            "set": int(match.group("set")),
            "binding": int(match.group("binding")),
            "logicalAttachmentIndex": int(match.group("attachment")),
            "metalColorSlot": int(match.group("color")),
            "depth": match.group("depth") == "1",
            "active": True,
        }
        key = (item["set"], item["binding"])
        if key in by_key and by_key[key] != item:
            raise RuntimeError(
                f"{metal_path}: framebuffer-fetch binding {key} has inconsistent metadata")
        by_key[key] = item

    lowering = []
    for logical in source_bindings:
        if logical["type"] != "InputAttachment":
            continue
        item = by_key.get((logical["set"], logical["binding"]))
        if item is None:
            lowering.append({
                "name": logical["name"],
                "set": logical["set"],
                "binding": logical["binding"],
                "active": False,
            })
            continue
        if item["name"] != logical["name"] \
                or item["logicalAttachmentIndex"] != logical.get("attachmentIndex") \
                or item["depth"] != bool(logical.get("depthTexture", False)):
            raise RuntimeError(
                f"{metal_path}: framebuffer-fetch metadata disagrees with logical "
                f"InputAttachment '{logical['name']}'")
        if item["depth"]:
            if item["metalColorSlot"] != 0xffffffff:
                raise RuntimeError(
                    f"{metal_path}: depth framebuffer fetch '{item['name']}' "
                    "must not claim a color attachment slot")
            physical = re.search(
                r"\b" + re.escape(item["name"]) + r"\b[^;{}]*"
                r"\[\[depth\(any\)\]\]",
                text,
            )
        else:
            if item["metalColorSlot"] >= 8:
                raise RuntimeError(
                    f"{metal_path}: color framebuffer fetch '{item['name']}' "
                    "has an invalid Metal color slot")
            physical = re.search(
                r"\b" + re.escape(item["name"]) + r"\b[^;{}]*"
                r"\[\[color\(" + str(item["metalColorSlot"])
                + r"\)\]\]",
                text,
            )
        if physical is None:
            raise RuntimeError(
                f"{metal_path}: framebuffer-fetch marker for '{item['name']}' "
                "is not backed by the emitted fragment input attribute")
        lowering.append(item)
    return lowering


def get_metal_push_constant_buffer_index(metal_path):
    """Extract and validate the push-constant buffer slot from generated MSL.

    The compiler contract owns the logical push-constant declaration, while
    the generated MSL owns its physical Metal slot. Keeping the slot in the
    source-name table is only a fallback for old artifacts; new reflection
    must be derived from every emitted entry point and checked for one stable
    value.
    """
    text = read_text(metal_path)
    slots = {
        int(match.group("slot"))
        for match in re.finditer(
            r"\b[A-Za-z_]\w*PushConstants\w*\s+constant\*\s+"
            r"[A-Za-z_]\w*\s*\[\[buffer\((?P<slot>\d+)\)\]\]",
            text,
        )
    }
    if len(slots) != 1:
        raise RuntimeError(
            f"{metal_path}: push-constant MSL lowering must expose exactly one buffer slot")
    return next(iter(slots))


def _metal_function_body(text, function_name):
    """Return one generated MSL function body, or None when it is absent."""
    match = re.search(
        rf"\b{re.escape(function_name)}\s*\([^{{}}]*\)\s*(?:->\s*[^{{}}]+)?\{{",
        text,
        flags=re.DOTALL,
    )
    if not match:
        return None
    body_start = text.find("{", match.start())
    depth = 0
    for index in range(body_start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[body_start:index + 1]
    return None


def get_metal_gpu_counted_indirect_lowering(source_text, metal_path, entry_names):
    """Validate the explicit Metal GPU-counted indirect vertex ABI.

    The source marker names the generated vertex entry point and the count
    buffer. The generated MSL remains authoritative for the physical buffer
    slot and draw-id spelling. The emitter must also carry the explicit mask
    proof marker; metadata alone is never allowed to authorize fixed-record
    replay.

    Marker form:
      ARP_METAL_GPU_COUNTED_INDIRECT_MASK(vertexEntry, countBuffer)
    """
    marker = re.search(
        r"\bARP_METAL_GPU_COUNTED_INDIRECT_MASK\s*\(\s*"
        r"(?P<entry>[A-Za-z_]\w*)\s*,\s*(?P<count>[A-Za-z_]\w*)\s*\)",
        source_text,
    )
    if not marker:
        return None
    entry_name = marker.group("entry")
    count_name = marker.group("count")
    if not any(
        (item[0] if isinstance(item, (tuple, list)) else item) == entry_name
        for item in entry_names
    ):
        raise RuntimeError(
            f"{metal_path}: GPU-counted indirect marker names an uncompiled entry '{entry_name}'")

    text = read_text(metal_path)
    body = _metal_function_body(text, entry_name)
    if body is None:
        raise RuntimeError(
            f"{metal_path}: GPU-counted indirect entry '{entry_name}' was not emitted")
    draw_id = re.search(
        r"(?P<name>[A-Za-z_]\w*)\s*\[\[\s*draw_id\s*\]\]", body)
    if draw_id is None:
        raise RuntimeError(
            f"{metal_path}: GPU-counted indirect entry '{entry_name}' has no [[draw_id]] ABI")
    count_match = re.search(
        rf"\b{re.escape(count_name)}(?:_\d+)?\b[^;{{}}]*"
        r"\[\[\s*buffer\(\s*(?P<slot>\d+)\s*\)\s*\]\]",
        text,
    )
    if count_match is None:
        raise RuntimeError(
            f"{metal_path}: GPU-counted indirect count buffer '{count_name}' has no Metal slot")
    if "ARP_METAL_GPU_COUNTED_INDIRECT_MASK_OUT_OF_RANGE" not in body:
        raise RuntimeError(
            f"{metal_path}: GPU-counted indirect entry '{entry_name}' has no out-of-range mask proof")
    if not re.search(
        rf"\b{re.escape(draw_id.group('name'))}\b\s*(?:<|>=)\s*"
        rf"\b{re.escape(count_name)}(?:_\d+)?\b",
        body,
    ):
        raise RuntimeError(
            f"{metal_path}: GPU-counted indirect entry '{entry_name}' does not compare draw id with count")
    return {
        "contractVersion": "MetalGpuCountedIndirect/1",
        "status": "Available",
        "countBufferIndex": int(count_match.group("slot")),
        "usesDrawId": True,
        "masksOutOfRange": True,
    }


def get_metal_tessellation_lowering(source_text, metal_path, entry_names):
    """Validate the generated Metal tessellation vertex/factor ABI.

    Marker form:
      ARP_METAL_TESSELLATION_ABI(vertexEntry, factorBuffer, Triangle, Half, 8, 8)
    The generated MSL must expose the named entry and factor buffer, and must
    carry the compiler-owned vertex ABI proof marker.
    """
    marker = re.search(
        r"\bARP_METAL_TESSELLATION_ABI\s*\(\s*"
        r"(?P<entry>[A-Za-z_]\w*)\s*,\s*(?P<factor>[A-Za-z_]\w*)\s*,\s*"
        r"(?P<patch>Triangle|Quad)\s*,\s*(?P<format>Half|Float)\s*,\s*"
        r"(?P<bytes>\d+)\s*,\s*(?P<stride>\d+)\s*\)",
        source_text,
    )
    if not marker:
        return None
    entry_name = marker.group("entry")
    if not any(
        (item[0] if isinstance(item, (tuple, list)) else item) == entry_name
        for item in entry_names
    ):
        raise RuntimeError(
            f"{metal_path}: tessellation marker names an uncompiled entry '{entry_name}'")
    bytes_per_patch = int(marker.group("bytes"))
    instance_stride = int(marker.group("stride"))
    if bytes_per_patch == 0 or instance_stride < bytes_per_patch:
        raise RuntimeError(
            f"{metal_path}: tessellation factor stride is smaller than one patch record")
    text = read_text(metal_path)
    if _metal_function_body(text, entry_name) is None:
        raise RuntimeError(
            f"{metal_path}: tessellation vertex entry '{entry_name}' was not emitted")
    factor_match = re.search(
        rf"\b{re.escape(marker.group('factor'))}(?:_\d+)?\b[^;{{}}]*"
        r"\[\[\s*buffer\(\s*(?P<slot>\d+)\s*\)\s*\]\]",
        text,
    )
    if factor_match is None:
        raise RuntimeError(
            f"{metal_path}: tessellation factor buffer has no Metal slot")
    if "ARP_METAL_TESSELLATION_VERTEX_ABI" not in text:
        raise RuntimeError(
            f"{metal_path}: tessellation artifact has no generated vertex ABI proof")
    control_point_match = re.search(
        r"\bARP_TESSELLATION_CONTROL_POINTS\s*\(\s*(?P<count>\d+)\s*\)",
        source_text,
    )
    if control_point_match is None or int(control_point_match.group("count")) == 0:
        raise RuntimeError(
            f"{metal_path}: tessellation ABI has no control-point count contract")
    return {
        "contractVersion": "MetalTessellation/1",
        "status": "Available",
        "vertexEntryPoint": entry_name,
        "factorBufferIndex": int(factor_match.group("slot")),
        "controlPointCount": int(control_point_match.group("count")),
        "patchType": 0 if marker.group("patch") == "Triangle" else 1,
        "factorFormat": 0 if marker.group("format") == "Half" else 1,
        "factorBytesPerPatch": bytes_per_patch,
        "factorInstanceStrideBytes": instance_stride,
    }


def normalize_wgsl_non_location_interpolation(wgsl_path):
    """Remove Slang's orphan interpolation attributes on non-location members.

    WGSL interpolation applies only to location IO. Slang may copy a varying
    struct for a helper, remove its IO locations and leave @interpolate behind.
    Actual location members (including flat integer varyings) stay byte-exact.
    Missing entry IO locations are not synthesized and still fail validation.
    """
    text = read_text(wgsl_path)
    code = strip_comments_preserving_offsets(text)
    removals = []
    for structure in re.finditer(r'\bstruct\s+\w+\s*\{([^{}]*)\}', code):
        for field in re.finditer(r'((?:@\w+\s*(?:\([^()]*\))?\s*)+)\w+\s*:', structure.group(1)):
            attributes = field.group(1)
            if re.search(r'@location\s*\(', attributes):
                continue
            for attribute in re.finditer(r'@interpolate\s*\([^()]*\)', attributes):
                start = structure.start(1) + field.start(1) + attribute.start()
                removals.append((start, start + len(attribute.group(0))))
    for start, end in reversed(removals):
        text = text[:start] + text[end:]
    if removals:
        write_utf8_no_bom(wgsl_path, text)


def normalize_wgsl_struct_member_delimiters(wgsl_path):
    """Convert Slang WGSL struct fields to Tint's comma-delimited grammar."""
    text = read_text(wgsl_path)

    def normalize_struct(match):
        body = match.group(1)
        normalized = re.sub(
            r'^(\s*(?:@\w+(?:\([^\n]*\))\s+)*\w[\w\s]*:\s*[^;\n]+);\s*$',
            r'\1,',
            body,
            flags=re.MULTILINE,
        )
        return match.group(0).replace(body, normalized)

    updated = re.sub(
        r'struct\s+\w+\s*\{(.*?)\}',
        normalize_struct,
        text,
        flags=re.DOTALL,
    )
    if updated != text:
        write_utf8_no_bom(wgsl_path, updated)
    normalize_wgsl_non_location_interpolation(wgsl_path)


def lower_wgsl_implicit_texture_sampling(wgsl_path):
    """Lower implicit-derivative textureSample calls to explicit base LOD."""
    text = read_text(wgsl_path)
    needle = "textureSample("
    result = []
    cursor = 0
    while True:
        start = text.find(needle, cursor)
        if start < 0:
            result.append(text[cursor:])
            break
        result.append(text[cursor:start])
        result.append("textureSampleLevel(")
        open_paren = start + len(needle) - 1
        depth = 1
        scan = open_paren + 1
        while scan < len(text) and depth:
            if text[scan] == "(":
                depth += 1
            elif text[scan] == ")":
                depth -= 1
            scan += 1
        if depth:
            raise RuntimeError(f"unterminated textureSample call in {wgsl_path}")
        result.append(text[open_paren + 1:scan - 1])
        result.append(", 0.0)")
        cursor = scan
    updated = "".join(result)
    if updated != text:
        write_utf8_no_bom(wgsl_path, updated)


def set_wgsl_storage_texture_format_and_access(wgsl_path, variable_stem, fmt, access):
    """Fix storage texture format and access qualifiers in WGSL output."""
    text = read_text(wgsl_path)
    pattern = rf'(@binding\(\d+\)\s+@group\(\d+\)\s+var\s+{re.escape(variable_stem)}\w*\s*:\s*texture_storage_2d<)\w+(,\s*)\w+(\s*>;)'
    desired_pattern = rf'@binding\(\d+\)\s+@group\(\d+\)\s+var\s+{re.escape(variable_stem)}\w*\s*:\s*texture_storage_2d<{re.escape(fmt)},\s*{re.escape(access)}\s*>;'
    if re.search(desired_pattern, text):
        return
    replacement = rf'\1{fmt}\2{access}\3'
    updated = re.sub(pattern, replacement, text)
    if updated == text:
        raise RuntimeError(
            f"WGSL storage texture format/access rewrite failed for {variable_stem} in {wgsl_path}"
        )
    write_utf8_no_bom(wgsl_path, updated)


# ── reflection generation ──────────────────────────────────────────────────


def apply_wgsl_storage_texture_declarations(wgsl_path, source_text, declarations, stage_masks=None):
    """Skip only resources absent from the selected SPIR-V entry interface."""
    for var_stem, fmt, access in declarations:
        if stage_masks is not None:
            declared = [binding for binding in get_shader_bindings(source_text)
                        if binding['name'] == var_stem and binding['type'] == 'StorageTexture']
            if len(declared) != 1:
                raise RuntimeError('SHADER-WGSL-STORAGE-TEXTURE-DECLARATION')
            binding = declared[0]
            if not stage_masks.get((binding['set'], binding['binding']), 0):
                continue
        set_wgsl_storage_texture_format_and_access(wgsl_path, var_stem, fmt, access)


SCALAR_TYPE_SIZES = {
    "float": 4, "float2": 8, "float3": 12, "float4": 16, "float4x4": 64,
    "uint": 4, "uint2": 8, "uint3": 12, "uint4": 16,
    "int": 4, "int2": 8, "int3": 12, "int4": 16,
}

VERTEX_FORMAT_NAMES = {
    "float2": "Float2", "float3": "Float3", "float4": "Float4",
}


def get_struct_body(source_text, struct_name):
    """Extract the body of a struct declaration from Slang source."""
    pattern = rf'struct\s+{re.escape(struct_name)}\s*\{{(.*?)\}};'
    m = re.search(pattern, source_text, re.DOTALL)
    if not m:
        return ""
    return m.group(1)


def get_push_constant_size_bytes(source_text):
    """Calculate push constant block size from Slang source."""
    m = re.search(r'\[\[vk::push_constant\]\]\s*(\w+)\s+\w+\s*;', source_text, re.DOTALL)
    if not m:
        return 0
    struct_name = m.group(1)
    body = get_struct_body(source_text, struct_name)
    size = 0
    # Match fields, ignoring possible row_major/column_major qualifier. Array
    # extents are part of the reflected ABI and must contribute their complete
    # byte range; treating an array as one scalar silently publishes a smaller
    # push-constant contract than SPIRV-Reflect.
    for field in re.finditer(
        r'(?:(?:row_major|column_major)\s+)?'
        r'(float(?:[234](?:x4)?)?|uint[234]?|int[234]?)\s+'
        r'\w+\s*((?:\[\s*\d+\s*\])*)\s*;',
        body,
    ):
        type_name = field.group(1)
        field_size = SCALAR_TYPE_SIZES.get(type_name, 0)
        if field_size == 0:
            continue
        element_count = 1
        for extent in re.findall(r'\[\s*(\d+)\s*\]', field.group(2)):
            element_count *= int(extent)
        size += field_size * element_count
    return size


def get_vertex_attributes(source_text):
    """Extract vertex input attributes from Slang source."""
    body = get_struct_body(source_text, "VertexInput")
    attrs = []
    location = 0
    for field in re.finditer(r'(float[234]?)\s+(\w+)\s*:\s*(\w+)\s*;', body):
        type_name = field.group(1)
        attrs.append({
            "name": field.group(2),
            "semantic": field.group(3),
            "format": VERTEX_FORMAT_NAMES.get(type_name, "Unknown"),
            "location": location,
        })
        location += 1
    return attrs


def get_logical_subpass_bindings(source_text):
    """Return compiler-assigned logical binding slots for subpass macros.

    Subpass declarations have a logical attachment index but no physical
    descriptor slot in the source language.  Allocate the first free set-0
    slots after honoring explicit source bindings.  All target lowerings and
    reflection consume this one allocation result.
    """
    source_without_comments = re.sub(
        r"//[^\n]*|/\*.*?\*/", "", source_text, flags=re.DOTALL)
    explicit_slots = {
        (int(binding), int(set_idx))
        for binding, set_idx, _type_name, _name in re.findall(
            r'\[\[vk::binding\(\s*(\d+)\s*,\s*(\d+)\s*\)\]\]\s*'
            r'([A-Za-z_]\w*(?:\s*<[^;>]+>)?)\s+(\w+)'
            r'(?:\s*\[\s*\d+\s*\])?'
            r'(?:\s*:\s*register\(\s*[A-Za-z]\s*\d+\s*\))?\s*;',
            source_without_comments,
            re.DOTALL,
        )
    }
    result = {}
    next_binding = 0
    for _kind, name, _attachment_index in re.findall(
            r"\bSUBPASS_INPUT_(COLOR|DEPTH)\s*\(\s*"
            r"([A-Za-z_]\w*)\s*,\s*(\d+)\s*\)",
            source_without_comments):
        while (next_binding, 0) in explicit_slots:
            next_binding += 1
        if name in result:
            raise RuntimeError(
                f"duplicate logical subpass binding '{name}'")
        result[name] = next_binding
        explicit_slots.add((next_binding, 0))
        next_binding += 1
    return result


def get_shader_bindings(source_text):
    """Infer descriptor bindings from Slang source content analysis."""
    bindings = []

    def add(name, bind_type, set_idx, bind_idx, attachment_index=None,
            depth_texture=False, array_count=1, stage_mask=0):
        binding = {"name": name, "type": bind_type, "set": set_idx, "binding": bind_idx}
        if array_count <= 0:
            raise RuntimeError(f"resource array '{name}' has an invalid count")
        binding["arrayCount"] = array_count
        # This value is populated only by target reflection. Source-level
        # binding discovery must remain explicitly unknown rather than
        # guessing visibility from entry-point names.
        binding["stageMask"] = stage_mask
        if attachment_index is not None:
            binding["attachmentIndex"] = attachment_index
        if depth_texture:
            binding["depthTexture"] = True
        bindings.append(binding)

    # Attachment indices belong to the logical shader interface and must be
    # reflected before any backend-specific artifact lowering. Strip comments
    # so the usage example in common/subpass_input.slang is not mistaken for
    # a declaration.
    source_without_comments = re.sub(
        r"//[^\n]*|/\*.*?\*/", "", source_text, flags=re.DOTALL)

    # HLSL/Slang spells sampled float color and sampled depth identically as
    # Texture2D<float>. This source-level macro records the L2 depth semantic
    # without relying on a resource-name heuristic; the shader preprocessor
    # still lowers it to an ordinary Texture2D declaration for native targets.
    sampled_depths = re.findall(
        r"\[\[vk::binding\(\s*(\d+)\s*,\s*(\d+)\s*\)\]\]\s*"
        r"ARP_SAMPLED_DEPTH\s*\(\s*([A-Za-z_]\w*)\s*\)\s*(?:\[\s*(\d+)\s*\])?\s*;",
        source_without_comments)
    for binding, set_idx, name, array_count in sampled_depths:
        add(name, "SampledTexture", int(set_idx), int(binding),
            depth_texture=True, array_count=int(array_count) if array_count else 1)

    subpass_bindings = get_logical_subpass_bindings(source_text)
    subpass_inputs = re.findall(
        r"SUBPASS_INPUT_(COLOR|DEPTH)\s*\(\s*([A-Za-z_]\w*)\s*,\s*(\d+)\s*\)",
        source_without_comments)
    for kind, name, attachment_index in subpass_inputs:
        add(name, "InputAttachment", 0, subpass_bindings[name],
            int(attachment_index), kind == "DEPTH")

    # Explicit Slang binding annotations are the source-level L2 contract.
    # Prefer them over the legacy name heuristics below: the latter cannot
    # reliably cover module-specific UBOs (for example PBR wind/weather/
    # skinning) and a partial reflection is unsafe for explicit WebGPU layouts.
    annotated = re.findall(
        r'\[\[vk::binding\(\s*(\d+)\s*,\s*(\d+)\s*\)\]\]\s*'
        r'([A-Za-z_]\w*(?:\s*<[^;>]+>)?)\s+(\w+)'
        r'(?:\s*\[\s*(\d+)\s*\])?'
        r'(?:\s*:\s*register\(\s*[A-Za-z]\s*\d+\s*\))?\s*;',
        source_without_comments,
        re.DOTALL)
    # Sampled-depth macros are explicit declarations too. A macro-only module
    # must not fall through to legacy resource-name inference (Inputs used to
    # fabricate an unrelated storage binding despite a complete depth contract).
    if annotated or sampled_depths:
        for binding, set_idx, type_name, name, array_count in annotated:
            normalized = re.sub(r'\s+', '', type_name)
            if normalized.startswith('Sampler'):
                bind_type = 'Sampler'
            elif normalized.startswith(('RWTexture', 'WTexture')):
                bind_type = 'StorageTexture'
            elif normalized.startswith('Texture'):
                bind_type = 'SampledTexture'
            elif normalized in ('DepthTexture2D', 'DepthTexture2DArray', 'DepthTextureCube'):
                bind_type = 'SampledTexture'
            elif (normalized.startswith('RWStructuredBuffer')
                  or normalized.startswith('RWByteAddressBuffer')
                  or normalized.startswith('RWBuffer')):
                bind_type = 'StorageBuffer'
            elif (normalized.startswith('StructuredBuffer')
                  or normalized.startswith('ByteAddressBuffer')
                  or normalized.startswith('Buffer')):
                bind_type = 'ReadOnlyStorageBuffer'
            elif normalized.startswith('ConstantBuffer'):
                bind_type = 'UniformBuffer'
            else:
                raise RuntimeError(
                    f"unsupported explicitly-bound Slang resource '{type_name} {name}'")
            add(
                name,
                bind_type,
                int(set_idx),
                int(binding),
                depth_texture=normalized in ('DepthTexture2D', 'DepthTexture2DArray', 'DepthTextureCube'),
                array_count=int(array_count) if array_count else 1,
            )
        return sorted(bindings, key=lambda value: (value['set'], value['binding']))

    # SceneConstants (Set 0)
    if "SceneConstants" in source_text:
        add("SceneConstants", "UniformBuffer", 0, 0)
    if "ClusterLightList" in source_text:
        add("ClusterLightList", "StorageBuffer", 0, 1)
    if "GiProbeState" in source_text:
        add("GiProbeState", "StorageBuffer", 0, 2)
    if "GiIrradianceOutput" in source_text and "ProbeIrradianceOut" not in source_text:
        add("GiIrradianceOutput", "StorageTexture", 0, 3)

    # ShadowSkinningMatrices (Set 0, binding 0)
    if "ShadowSkinningMatrices" in source_text:
        add("ShadowSkinningMatrices", "StorageBuffer", 0, 0)

    # Material textures (Set 1)
    _tex_bindings = [
        ("BaseColorTexture", 0), ("NormalTexture", 1), ("MetallicRoughnessTexture", 2),
        ("EmissiveTexture", 3), ("OcclusionTexture", 4), ("LinearSampler", 5),
        ("ClearcoatTexture", 6), ("ClearcoatRoughnessTexture", 7), ("SpecularTexture", 8),
        ("SpecularColorTexture", 9), ("SheenColorTexture", 10), ("SheenRoughnessTexture", 11),
        ("TransmissionTexture", 12), ("ThicknessTexture", 13), ("IridescenceTexture", 14),
        ("IridescenceThicknessTexture", 15), ("ClearcoatNormalTexture", 16),
    ]
    for name, bind in _tex_bindings:
        if name in source_text:
            is_sampler = "Sampler" in name
            add(name, "Sampler" if is_sampler else "SampledTexture", 1, bind)

    # IBL / Shadow set (Set 2)
    if "IrradianceCube" in source_text:
        add("IrradianceCube", "SampledTexture", 2, 0)
    if "PrefilteredSpecularCube" in source_text:
        add("PrefilteredSpecularCube", "SampledTexture", 2, 1)
    if "BrdfIntegrationLut" in source_text:
        add("BrdfIntegrationLut", "SampledTexture", 2, 2)
    if "IblSampler" in source_text:
        add("IblSampler", "Sampler", 2, 3)
    if "ShadowMap" in source_text:
        add("ShadowMap", "SampledTexture", 2, 4)
    if "ShadowSampler" in source_text:
        add("ShadowSampler", "Sampler", 2, 5)
    if "GiIrradiance" in source_text and "ProbeIrradianceOut" not in source_text:
        add("GiIrradiance", "SampledTexture", 2, 6)
    if "GiSampler" in source_text:
        add("GiSampler", "Sampler", 2, 7)
    # TAA prev-camera UBO (Set 2, binding 1): WoccPrevCamera carries the
    # previous frame's un-jittered viewProjection so pbr_scene can emit a
    # per-pixel velocity MRT (curClip vs prevClip). Set 2 binding 1 is the
    # slot reserved for this UBO; the engine-side scene draw plan must
    # upload prevViewProjection row-major (default float4x4 -> SPV RowMajor).
    if "gPrevCamera" in source_text:
        add("gPrevCamera", "UniformBuffer", 2, 1)

    # TAA resolve shader (wocc_taa): all 8 descriptors live in set 0 (scene
    # color, history color, signed velocity, current depth, reactive mask,
    # history depth, history moments, linear-clamp sampler). This is a
    # dedicated block because the generic SceneColorTexture/DepthTexture rules
    # below would otherwise mis-assign them to set 1/set 2 (post-process/scene
    # conventions). HistoryColorTexture is the TAA marker - when present, the
    # generic rules are gated off below to avoid duplicate/wrong-set entries.
    is_taa_shader = "HistoryColorTexture" in source_text
    if is_taa_shader:
        add("SceneColorTexture", "SampledTexture", 0, 0)
        add("HistoryColorTexture", "SampledTexture", 0, 1)
        add("VelocityTexture", "SampledTexture", 0, 2)
        add("DepthTexture", "SampledTexture", 0, 3, depth_texture=True)
        add("ReactiveTexture", "SampledTexture", 0, 4)
        add("HistoryDepthTexture", "SampledTexture", 0, 5, depth_texture=True)
        add("HistoryMomentsTexture", "SampledTexture", 0, 6)
        add("LinearClampSampler", "Sampler", 0, 7)

    # Post process / scene input textures
    if "SceneColorTexture" in source_text and not is_taa_shader:
        add("SceneColorTexture", "SampledTexture", 1, 0)
    if ("SceneDepthTexture" in source_text
            and not any(binding["name"] == "SceneDepthTexture" for binding in bindings)):
        add("SceneDepthTexture", "SampledTexture", 1, 1)
    if "SceneInputSampler" in source_text:
        add("SceneInputSampler", "Sampler", 1, 2)
    if "SceneColorSampler" in source_text:
        add("SceneColorSampler", "Sampler", 1, 1)

    # Startup splash
    if "StartupSplashLogoTexture" in source_text:
        add("StartupSplashLogoTexture", "SampledTexture", 1, 0)
    if "StartupSplashLogoSampler" in source_text:
        add("StartupSplashLogoSampler", "Sampler", 1, 1)

    # Post-process inputs (Set 0)
    if "PostProcessInputTexture" in source_text:
        add("PostProcessInputTexture", "SampledTexture", 0, 0)
    if "PostProcessInputSampler" in source_text:
        add("PostProcessInputSampler", "Sampler", 0, 1)
    if "BloomTexture" in source_text:
        add("BloomTexture", "SampledTexture", 0, 2)

    # Material preview
    if "MainTexture" in source_text:
        add("MainTexture", "SampledTexture", 1, 0)
    if "MainSampler" in source_text:
        add("MainSampler", "Sampler", 1, 1)
    if "LutTexture" in source_text:
        add("LutTexture", "SampledTexture", 0, 3)

    # Light buffer (Set 3)
    if "gLights" in source_text:
        add("gLights", "UniformBuffer", 3, 0)

    # DDGI probes
    if "ProbeIrradianceOut" in source_text:
        add("ProbeIrradianceOut", "StorageTexture", 0, 0)
    if "ProbeIrradianceHistory" in source_text:
        add("ProbeIrradianceHistory", "SampledTexture", 0, 1)
    if "ProbeSampler" in source_text:
        add("ProbeSampler", "Sampler", 0, 2)
    # Phase 2 depth moments atlas (Rgba16Float): ProbeDepthMomentsOut at binding
    # 3 (storage write), ProbeDepthMomentsHistory at binding 8 (sampled read).
    if "ProbeDepthMomentsOut" in source_text:
        add("ProbeDepthMomentsOut", "StorageTexture", 0, 3)
    if "ProbeDepthMomentsHistory" in source_text:
        add("ProbeDepthMomentsHistory", "SampledTexture", 0, 8)
    # Phase 3 probe-state SSBO: ProbeStateBuffer at binding 9 (storage buffer,
    # read_write - authored by the relocate pass, read by update/resolve).
    if "ProbeStateBuffer" in source_text:
        add("ProbeStateBuffer", "StorageBuffer", 0, 9)
    # Phase 4 scene BVH SSBOs: SceneBvhNodes at binding 10 (storage buffer,
    # read-only - flat AABB tree traversed by the probe-update pass) and
    # SceneBvhLeaves at binding 11 (storage buffer, read-only - triangle
    # v0/v1/v2 + face normal + materialIdx, one triangle per BVH leaf).
    if "SceneBvhNodes" in source_text:
        add("SceneBvhNodes", "StorageBuffer", 0, 10)
    if "SceneBvhLeaves" in source_text:
        add("SceneBvhLeaves", "StorageBuffer", 0, 11)

    # Terrain data
    if "TerrainData" in source_text:
        add("TerrainData", "UniformBuffer", 0, 7)

    # WoCC terrain splat textures (Set 0)
    _terrain_splat = [
        ("GrassAlbedoTexture", 0), ("GrassNormalTexture", 1),
        ("DirtAlbedoTexture", 2), ("DirtNormalTexture", 3),
        ("RockAlbedoTexture", 4), ("RockNormalTexture", 5),
        ("SandAlbedoTexture", 6), ("SandNormalTexture", 7),
        ("MacroNoiseTexture", 8), ("TerrainSplatSampler", 9),
    ]
    for name, bind in _terrain_splat:
        if name in source_text:
            is_sampler = "Sampler" in name
            add(name, "Sampler" if is_sampler else "SampledTexture", 0, bind)

    if "TerrainSplatParams" in source_text:
        add("TerrainSplatParams", "UniformBuffer", 0, 10)

    # More GI
    if "GiIrradianceOutput" in source_text:
        add("GiIrradianceOutput", "StorageTexture", 0, 4)
    if "SceneDepth" in source_text:
        add("SceneDepth", "SampledTexture", 0, 5)
    if "DepthSampler" in source_text:
        add("DepthSampler", "Sampler", 0, 6)
    if "DepthTexture" in source_text and not is_taa_shader:
        add("DepthTexture", "SampledTexture", 2, 0)

    if "DepthInputSampler" in source_text:
        add("DepthInputSampler", "Sampler", 0, 2)
    if "ColorInputSampler" in source_text:
        add("ColorInputSampler", "Sampler", 0, 3)

    return bindings


def _yaml_quote_str(s):
    """Quote a YAML string scalar if needed (paths contain backslashes/colons)."""
    if s is None:
        return "null"
    if isinstance(s, str) and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", s):
        return s
    # Quote anything with special chars (':', '#', '\', leading '[', '{', etc.)
    return "\"" + s.replace("\\", "\\\\").replace('"', '\\"') + "\""


def _yaml_scalar(val, indent):
    """Serialize a Python scalar/list/dict to YAML text at the given indent."""
    pad = " " * indent
    if val is None:
        return "null"
    if isinstance(val, bool):
        return "true" if val else "false"
    if isinstance(val, (int, float)):
        return str(val)
    return _yaml_quote_str(str(val))


def _yaml_dump_mapping(data, indent):
    """Hand-written YAML emitter for a flat-ish mapping (values are scalars,
    lists of flat dicts, or nested mappings of scalars). Returns text ending
    with a newline."""
    lines = []
    pad = " " * indent
    for key, val in data.items():
        if val is None:
            lines.append(f"{pad}{key}: null")
        elif isinstance(val, bool):
            lines.append(f"{pad}{key}: {'true' if val else 'false'}")
        elif isinstance(val, (int, float)):
            lines.append(f"{pad}{key}: {val}")
        elif isinstance(val, str):
            lines.append(f"{pad}{key}: {_yaml_quote_str(val)}")
        elif isinstance(val, dict):
            if not val:
                lines.append(f"{pad}{key}: {{}}")
            else:
                lines.append(f"{pad}{key}:")
                for k2, v2 in val.items():
                    if v2 is None:
                        lines.append(f"{pad}  {k2}: null")
                    elif isinstance(v2, bool):
                        lines.append(f"{pad}  {k2}: {'true' if v2 else 'false'}")
                    elif isinstance(v2, (int, float)):
                        lines.append(f"{pad}  {k2}: {v2}")
                    else:
                        lines.append(f"{pad}  {k2}: {_yaml_quote_str(str(v2))}")
        elif isinstance(val, list):
            if not val:
                lines.append(f"{pad}{key}: []")
            else:
                lines.append(f"{pad}{key}:")
                for item in val:
                    if isinstance(item, dict):
                        first = True
                        for k2, v2 in item.items():
                            prefix = f"{pad}  - " if first else f"{pad}    "
                            if v2 is None:
                                lines.append(f"{prefix}{k2}: null")
                            elif isinstance(v2, bool):
                                lines.append(f"{prefix}{k2}: {'true' if v2 else 'false'}")
                            elif isinstance(v2, (int, float)):
                                lines.append(f"{prefix}{k2}: {v2}")
                            else:
                                lines.append(f"{prefix}{k2}: {_yaml_quote_str(str(v2))}")
                            first = False
                    else:
                        lines.append(f"{pad}  - {_yaml_scalar(item, indent)}")
        else:
            lines.append(f"{pad}{key}: {_yaml_quote_str(str(val))}")
    return "\n".join(lines) + "\n"


def derive_entry_point_reflection(source_text, entry_names, source_path):
    """Derive stage metadata for the exact entries passed to slangc.

    The compile list is authoritative. Keeping a second module-name-to-entry
    table allowed reflection to silently drift from the generated WGSL (as
    happened for shadow_cull). Stage inference is deliberately strict so a
    new entry point cannot be published with a guessed stage.
    """
    result = []
    source_for_structure = strip_comments_preserving_offsets(source_text)
    for item in entry_names:
        name = item[0] if isinstance(item, (tuple, list)) else item
        name = str(name)
        definition = re.search(
            rf"\b{re.escape(name)}\s*\([^;{{}}]*\)"
            rf"\s*(?::\s*[^\n{{}}]+)?\s*\{{",
            source_for_structure,
            flags=re.DOTALL,
        )
        if not definition:
            raise RuntimeError(
                f"{source_path}: entry point '{name}' has no function definition")
        # Limit attribute inspection to the current function's declaration;
        # a fixed character window can accidentally miss a valid
        # [numthreads] attribute in a shader with a long preceding comment.
        previous_function_end = source_for_structure.rfind(
            "}", 0, definition.start())
        prefix = source_for_structure[
            previous_function_end + 1:definition.start()]
        signature = definition.group(0)
        lower_name = name.lower()
        prefix_without_comments = re.sub(
            r"//[^\n]*|/\*.*?\*/", "", prefix, flags=re.DOTALL)
        compute_match = re.search(
            r"\[\s*numthreads\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)\s*\]",
            prefix_without_comments,
        )
        if (lower_name.endswith("computemain")
                or compute_match
                or re.search(r"\[\s*shader\s*\(\s*[\"']compute", prefix_without_comments)):
            stage = "Compute"
        elif ("tessellationcontrol" in lower_name
              or "hull" in lower_name
              or re.search(r"shader\s*\(\s*[\"']hull", prefix_without_comments)):
            stage = "TessellationControl"
        elif ("tessellationevaluation" in lower_name
              or "domain" in lower_name
              or re.search(r"shader\s*\(\s*[\"']domain", prefix_without_comments)):
            stage = "TessellationEvaluation"
        elif ("fragment" in lower_name or re.search(r"SV_Target", signature)
              or re.search(r"\[\s*shader\s*\(\s*[\"']fragment[\"']\s*\)\s*\]", prefix_without_comments)):
            stage = "Fragment"
        elif ("vertex" in lower_name or re.search(r"SV_VertexID|SV_InstanceID", signature)
              or re.search(r"\[\s*shader\s*\(\s*[\"']vertex[\"']\s*\)\s*\]", prefix_without_comments)):
            stage = "Vertex"
        else:
            raise RuntimeError(
                f"{source_path}: cannot derive shader stage for entry point '{name}'")
        # Layered rendering is an explicit shader ABI. Accept the canonical
        # Slang semantic and the project marker, but only within this exact
        # entry-point declaration/body so another entry cannot promote it.
        writes_array_index = False
        if stage == "Fragment":
            body_start = source_text.find("{", definition.end() - 1)
            if body_start >= 0:
                depth = 0
                body_end = None
                for body_index in range(body_start, len(source_text)):
                    if source_text[body_index] == "{":
                        depth += 1
                    elif source_text[body_index] == "}":
                        depth -= 1
                        if depth == 0:
                            body_end = body_index + 1
                            break
                if body_end is not None:
                    entry_text = source_text[definition.start():body_end]
                    writes_array_index = bool(re.search(
                        r"SV_RenderTargetArrayIndex|RenderTargetArrayIndex|"
                        r"ARP_WRITES_RENDER_TARGET_ARRAY_INDEX",
                        entry_text,
                    ))
        entry = {
            "name": name,
            "stage": stage,
            "writesRenderTargetArrayIndex": writes_array_index,
        }
        if stage == "Compute":
            if not compute_match:
                raise RuntimeError(
                    f"{source_path}: compute entry point '{name}' has no numthreads dimensions")
            entry.update({
                "computeWorkgroupSizeX": int(compute_match.group(1)),
                "computeWorkgroupSizeY": int(compute_match.group(2)),
                "computeWorkgroupSizeZ": int(compute_match.group(3)),
            })
        result.append(entry)
    return result


def derive_compute_workgroup_size(source_text, entry_names, source_path):
    """Return legacy module-level dimensions only when they are unambiguous."""
    sizes = []
    for item in entry_names:
        if not isinstance(item, dict) or item.get("stage") != "Compute":
            continue
        if all(key in item for key in (
                "computeWorkgroupSizeX", "computeWorkgroupSizeY", "computeWorkgroupSizeZ")):
            sizes.append(tuple(item[key] for key in (
                "computeWorkgroupSizeX", "computeWorkgroupSizeY", "computeWorkgroupSizeZ")))
    if not sizes or any(size != sizes[0] for size in sizes[1:]):
        return None
    return sizes[0]


def fill_compute_workgroup_reflection(source_text, entry_points, source_path):
    """Populate per-entrypoint dimensions for legacy entry-point tables."""
    source_for_structure = strip_comments_preserving_offsets(source_text)
    for entry in entry_points:
        if entry.get("stage") != "Compute":
            continue
        if all(key in entry for key in (
                "computeWorkgroupSizeX", "computeWorkgroupSizeY", "computeWorkgroupSizeZ")):
            continue
        name = entry["name"]
        definition = re.search(
            rf"\b{re.escape(str(name))}\s*\([^;{{}}]*\)"
            rf"\s*(?::\s*[^\n{{}}]+)?\s*\{{",
            source_for_structure,
            flags=re.DOTALL,
        )
        if not definition:
            raise RuntimeError(
                f"{source_path}: entry point '{name}' has no function definition")
        previous_function_end = source_for_structure.rfind(
            "}", 0, definition.start())
        prefix = source_for_structure[
            previous_function_end + 1:definition.start()]
        prefix_without_comments = re.sub(
            r"//[^\n]*|/\*.*?\*/", "", prefix, flags=re.DOTALL)
        match = re.search(
            r"\[\s*numthreads\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)\s*\]",
            prefix_without_comments,
        )
        if not match:
            raise RuntimeError(
                f"{source_path}: compute entry point '{name}' has no numthreads dimensions")
        entry.update({
            "computeWorkgroupSizeX": int(match.group(1)),
            "computeWorkgroupSizeY": int(match.group(2)),
            "computeWorkgroupSizeZ": int(match.group(3)),
        })


def write_shader_reflection(source_path, module_name, spirv_path, metal_path, wgsl_path, output_path, push_contract,
                            spirv_reflect_exe=None, push_constant_members_yaml=None,
                            descriptor_stage_masks=None,
                            webgpu_subpass_lowered=False,
                            webgpu_unavailable_reason=None,
                            entry_names=None, metal_binding_space=None,
                            metal_argument_buffer=None,
                            metal_framebuffer_fetch=None,
                            metal_gpu_counted_indirect=None,
                            metal_tessellation=None, single_entry=False, metal_unavailable_reason=None,
                            selected_push_inactive=False, push_stage_evidence=None):
    """Generate per-shader reflection YAML.

    Descriptor visibility and push-constant layout both come from
    SPIRV-Reflect. Missing target reflection is rejected rather than emitting
    an artifact with guessed stage visibility.
    """
    if not metal_path:
        if metal_unavailable_reason != "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED" or any(
                value is not None for value in (metal_binding_space, metal_argument_buffer,
                    metal_framebuffer_fetch, metal_gpu_counted_indirect, metal_tessellation)):
            raise RuntimeError("SHADER-METAL-RUNTIME-LENGTH-REFLECTION-CONTRACT")
    elif metal_unavailable_reason:
        raise RuntimeError("SHADER-METAL-RUNTIME-LENGTH-OUTPUT-PRESENT")
    source_text = read_text(source_path)
    logical_bindings = get_shader_bindings(source_text)
    if spirv_reflect_exe is None:
        raise RuntimeError(
            f"{spirv_path}: authoritative SPIRV-Reflect descriptor stage metadata is required")
    if descriptor_stage_masks is None:
        # A shader with no logical descriptors is valid and should not require
        # SPIRV-Reflect to emit an empty descriptor section. Any shader that
        # does declare a logical descriptor still takes the strict target
        # reflection path below and fails closed when its stage metadata is
        # absent or ambiguous.
        descriptor_stage_masks = (
            _bindings_codegen.reflect_descriptor_stage_masks(
                spirv_reflect_exe, spirv_path)
            if logical_bindings else {})
    _bindings_codegen.apply_descriptor_stage_masks(
        logical_bindings, descriptor_stage_masks, spirv_path,
        allow_inactive=True)
    # ``bindings`` is the executable RHI descriptor interface.  A source
    # declaration that no selected entry point reads has no authoritative
    # stage visibility and must not be published as an active descriptor.
    # Keep those declarations in the target-specific lowering tables, where
    # they are represented explicitly as ``active: false``.
    executable_bindings = [
        binding for binding in logical_bindings
        if binding.get("stageMask", 0) != 0
    ]
    # Some explicitly non-critical modules are still known not to compile to
    # WGSL (for example shadow_cull uses unsupported atomics). Do not invent a
    # lowering artifact for them: record it as unavailable so the WebGPU
    # rollout gate can keep the feature disabled and fail closed if selected.
    # A logical InputAttachment is not a WebGPU resource declaration. WebGPU
    # needs a separate-pass lowering that changes the logical descriptor to a
    # sampled texture and rewrites the load operation. Until that C++/RHI
    # lowering artifact exists, a WGSL file by itself is not executable
    # evidence. Keep the generated contract unavailable instead of allowing
    # the backend to reinterpret the descriptor type.
    has_logical_input_attachment = any(
        binding["type"] == "InputAttachment"
        for binding in executable_bindings)
    has_wgsl_artifact = bool(wgsl_path) and os.path.isfile(wgsl_path)
    webgpu_wgsl_available = (
        has_wgsl_artifact
        and (not has_logical_input_attachment or webgpu_subpass_lowered))
    webgpu_binding_space = (
        get_webgpu_binding_space_lowering(logical_bindings, wgsl_path)
        if webgpu_wgsl_available else [])

    # Determine entry points per module. Production compilation always passes
    # the same names used for -entry; retain the table only for old fixture
    # callers that have not been migrated yet.
    entry_map = {
        "GlobalIllumination": [
            {"name": "GiProbeUpdate", "stage": "Compute"},
            {"name": "GiIrradianceResolve", "stage": "Compute"},
        ],
        "PostProcess": [
            {"name": "vertexMain", "stage": "Vertex"},
            {"name": "toneMappingFragmentMain", "stage": "Fragment"},
            {"name": "bloomDownsampleFragmentMain", "stage": "Fragment"},
            {"name": "bloomCompositeFragmentMain", "stage": "Fragment"},
            {"name": "lutFragmentMain", "stage": "Fragment"},
            {"name": "heightFogFragmentMain", "stage": "Fragment"},
            {"name": "sharpenFragmentMain", "stage": "Fragment"},
        ],
        "ShadowDepth": [
            {"name": "vertexMain", "stage": "Vertex"},
            {"name": "skinnedVertexMain", "stage": "Vertex"},
            {"name": "fragmentMain", "stage": "Fragment"},
        ],
        "WoccPost": [
            {"name": "vertexMain", "stage": "Vertex"},
            {"name": "bloomDownsampleFragmentMain", "stage": "Fragment"},
            {"name": "bloomCompositeFragmentMain", "stage": "Fragment"},
            {"name": "toneMappingFragmentMain", "stage": "Fragment"},
            {"name": "gradeFragmentMain", "stage": "Fragment"},
        ],
        "WoccPostFx": [
            {"name": "vertexMain", "stage": "Vertex"},
            {"name": "woccFogFragmentMain", "stage": "Fragment"},
            {"name": "woccGodRayFragmentMain", "stage": "Fragment"},
        ],
        "Ddgi": [
            {"name": "giProbeUpdateComputeMain", "stage": "Compute"},
            {"name": "giProbeTraceComputeMain", "stage": "Compute"},
            {"name": "giProbeBlendIrradianceComputeMain", "stage": "Compute"},
            {"name": "giProbeBlendDistanceComputeMain", "stage": "Compute"},
            {"name": "giIrradianceResolveComputeMain", "stage": "Compute"},
            {"name": "giProbeRelocateComputeMain", "stage": "Compute"},
            {"name": "giProbeBorderUpdateComputeMain", "stage": "Compute"},
        ],
        "Ssr": [
            {"name": "ssrTraceComputeMain", "stage": "Compute"},
            {"name": "vertexMain", "stage": "Vertex"},
            {"name": "ssrCompositeFragmentMain", "stage": "Fragment"},
        ],
        "WebGpuComputeSmoke": [
            {"name": "computeSmokeMain", "stage": "Compute"},
            {"name": "computeSmokeVertexMain", "stage": "Vertex"},
            {"name": "computeSmokeFragmentMain", "stage": "Fragment"},
        ],
        "WebGpuSubpassInput": [
            {"name": "subpassWriterVertexMain", "stage": "Vertex"},
            {"name": "subpassWriterFragmentMain", "stage": "Fragment"},
            {"name": "subpassReaderVertexMain", "stage": "Vertex"},
            {"name": "subpassReaderFragmentMain", "stage": "Fragment"},
        ],
        "WoccPbrScene": [
            {"name": "vertexMain", "stage": "Vertex"},
            {"name": "vertexMainInstanced", "stage": "Vertex"},
            {"name": "vertexMainSkinned", "stage": "Vertex"},
            {"name": "fragmentMain", "stage": "Fragment"},
        ],
        "WoccSky": [
            {"name": "vertexMain", "stage": "Vertex"},
            {"name": "fragmentMain", "stage": "Fragment"},
        ],
        "WoccPbrTerrain": [
            {"name": "vertexMain", "stage": "Vertex"},
            {"name": "fragmentMain", "stage": "Fragment"},
        ],
        "WoccDepthPrepass": [
            {"name": "vertexMain", "stage": "Vertex"},
            {"name": "fragmentMain", "stage": "Fragment"},
        ],
    }
    # Default: vertex + fragment
    entry_points = (
        derive_entry_point_reflection(source_text, entry_names, source_path)
        if entry_names is not None else entry_map.get(
            module_name,
            [
                {"name": "vertexMain", "stage": "Vertex"},
                {"name": "fragmentMain", "stage": "Fragment"},
            ],
        )
    )
    fill_compute_workgroup_reflection(source_text, entry_points, source_path)

    if selected_push_inactive and (not single_entry or push_contract is not None):
        raise RuntimeError("SHADER-ENTRY-PUSH-INACTIVE-CONTRACT")
    pc_size = 0 if selected_push_inactive else get_push_constant_size_bytes(source_text)
    compute_workgroup_size = derive_compute_workgroup_size(
        source_text, entry_points, source_path)

    # Derive push-constant stage visibility from shader entry points.
    pc_stages = []
    for ep in entry_points:
        stage = ep.get("stage", "")
        if stage == "Vertex":
            pc_stages.append("VS")
        elif stage == "Fragment":
            pc_stages.append("FS")
        elif stage == "Compute":
            pc_stages.append("CS")
    # Deduplicate and sort
    pc_stages = sorted(set(pc_stages))
    if push_stage_evidence is not None:
        pc_stages = sorted(set(push_stage_evidence))
        if bool(pc_stages) != bool(pc_size):
            raise RuntimeError("SHADER-PUSH-STAGE-EVIDENCE")

    webgpu_physical_groups = [
        int(entry["group"]) for entry in webgpu_binding_space
        if entry.get("active", False)]
    if (push_contract and push_contract.get("webgpuGroup") is not None):
        webgpu_physical_groups.append(int(push_contract["webgpuGroup"]))
    webgpu_group_count = (
        max(webgpu_physical_groups) + 1
        if webgpu_wgsl_available and webgpu_physical_groups else 0)

    metadata = {
        "moduleName": module_name,
        "sourcePath": os.path.normpath(os.path.abspath(source_path)),
        "reflectionContractVersion": "ShaderReflection/3",
        "sourceSha256": hashlib.sha256(
            source_text.encode("utf-8")).hexdigest(),
        "entryPoints": entry_points,
        "bindings": executable_bindings,
        # Versioned, target-specific lowering evidence. ``bindings`` remains
        # the backend-neutral L2 interface; this table records the exact
        # physical WGSL location produced from it and is required for native
        # WebGPU layout/bind lowering.
        "webGpuBindingSpaceContractVersion": (
            "WebGpuBindingSpace/5" if webgpu_wgsl_available else None),
        "webGpuBindingSpaceStatus": (
            "Available" if webgpu_wgsl_available else "Unavailable"),
        "webGpuBindingSpaceUnavailableReason": (
            webgpu_unavailable_reason
            if webgpu_unavailable_reason else
            "logical InputAttachment requires code-level separate-pass lowering"
            if has_logical_input_attachment and not webgpu_subpass_lowered else None),
        "webGpuBindingSpace": webgpu_binding_space,
        "webGpuBindingSpaceGroupCount": webgpu_group_count,
        # ``bindings`` is the logical RHI interface. Metal has independent
        # resource namespaces, so this versioned table is the only source of
        # executable buffer/texture/sampler indices for the backend.
        "metalBindingSpaceContractVersion": (
            "MetalBindingSpace/1" if metal_binding_space is not None else None),
        "metalBindingSpaceStatus": (
            "Available" if metal_binding_space is not None else "Unavailable"),
        "metalBindingSpaceUnavailableReason": (
            None if metal_binding_space is not None
            else metal_unavailable_reason or "generated MSL artifact was not available"),
        "metalBindingSpace": metal_binding_space or [],
        "metalArgumentBufferContractVersion": (
            "MetalArgumentBuffer/1"
            if metal_argument_buffer is not None else None),
        "metalArgumentBufferStatus": (
            "Available" if metal_argument_buffer is not None else "Unavailable"),
        "metalArgumentBufferUnavailableReason": (
            None if metal_argument_buffer is not None
            else metal_unavailable_reason or "generated MSL artifact did not expose an argument-buffer lowering"),
        "metalArgumentBuffer": metal_argument_buffer or [],
        "metalFramebufferFetchContractVersion": (
            "MetalFramebufferFetch/1"
            if metal_framebuffer_fetch is not None else None),
        "metalFramebufferFetchStatus": (
            "Available" if metal_framebuffer_fetch is not None else "Unavailable"),
        "metalFramebufferFetchUnavailableReason": (
            None if metal_framebuffer_fetch is not None
            else metal_unavailable_reason or "generated MSL artifact did not expose framebuffer-fetch lowering"),
        "metalFramebufferFetch": metal_framebuffer_fetch or [],
        "metalGpuCountedIndirectContractVersion": (
            metal_gpu_counted_indirect["contractVersion"]
            if metal_gpu_counted_indirect else None),
        "metalGpuCountedIndirectStatus": (
            metal_gpu_counted_indirect["status"]
            if metal_gpu_counted_indirect else "Unavailable"),
        "metalGpuCountedIndirectCountBufferIndex": (
            metal_gpu_counted_indirect["countBufferIndex"]
            if metal_gpu_counted_indirect else None),
        "metalGpuCountedIndirectUsesDrawId": (
            metal_gpu_counted_indirect["usesDrawId"]
            if metal_gpu_counted_indirect else False),
        "metalGpuCountedIndirectMasksOutOfRange": (
            metal_gpu_counted_indirect["masksOutOfRange"]
            if metal_gpu_counted_indirect else False),
        "metalTessellationContractVersion": (
            metal_tessellation["contractVersion"]
            if metal_tessellation else None),
        "metalTessellationStatus": (
            metal_tessellation["status"]
            if metal_tessellation else "Unavailable"),
        "metalTessellationVertexEntryPoint": (
            metal_tessellation["vertexEntryPoint"]
            if metal_tessellation else None),
        "metalTessellationFactorBufferIndex": (
            metal_tessellation["factorBufferIndex"]
            if metal_tessellation else None),
        "metalTessellationControlPointCount": (
            metal_tessellation["controlPointCount"]
            if metal_tessellation else 0),
        "metalTessellationPatchType": (
            metal_tessellation["patchType"] if metal_tessellation else None),
        "metalTessellationFactorFormat": (
            metal_tessellation["factorFormat"] if metal_tessellation else None),
        "metalTessellationFactorBytesPerPatch": (
            metal_tessellation["factorBytesPerPatch"]
            if metal_tessellation else None),
        "metalTessellationFactorInstanceStrideBytes": (
            metal_tessellation["factorInstanceStrideBytes"]
            if metal_tessellation else None),
        "pushConstantSizeBytes": pc_size,
        # Track A P5: push-constant contract metadata
        "pushConstantContractVersion": "AtomicFullBlockPush/1" if pc_size > 0 else None,
        "pushConstantStages": "|".join(pc_stages) if pc_size > 0 and pc_stages else None,
        "pushConstantWebGpuBinding": {
            "group": push_contract["webgpuGroup"],
            "binding": push_contract["webgpuBinding"],
        } if push_contract and push_contract["webgpuGroup"] is not None else None,
        "pushConstantMetalBufferIndex": (
            push_contract["metalBufferIndex"]
            if push_contract and push_contract["metalBufferIndex"] is not None else None),
        "computeWorkgroupSizeX": compute_workgroup_size[0] if compute_workgroup_size else None,
        "computeWorkgroupSizeY": compute_workgroup_size[1] if compute_workgroup_size else None,
        "computeWorkgroupSizeZ": compute_workgroup_size[2] if compute_workgroup_size else None,
        "tessellationControlPointCount": (
            int(match.group(1))
            if (match := re.search(
                r"ARP_TESSELLATION_CONTROL_POINTS\s*\(\s*(\d+)\s*\)",
                source_text)) else 0),
        "vertexInputContractVersion": "VertexInputInterface/1" if single_entry else "LegacySourceVertexInput/0",
        "vertexAttributes": (_bindings_codegen.reflect_single_entry_vertex_inputs(
            spirv_reflect_exe, spirv_path, entry_points[0]["name"], entry_points[0]["stage"])
            if single_entry else get_vertex_attributes(source_text)),
        "targets": {
            "spirv": os.path.normpath(os.path.abspath(spirv_path)),
            "metal": os.path.normpath(os.path.abspath(metal_path)) if metal_path else None,
            "wgsl": (os.path.normpath(os.path.abspath(wgsl_path))
                     if has_wgsl_artifact else None),
        },
    }

    yaml_text = _yaml_dump_mapping(metadata, 0)
    # Append authoritative member-level layout from SPIRV-Reflect when available.
    if push_constant_members_yaml:
        yaml_text += push_constant_members_yaml

    with open(output_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(yaml_text)



# ── shader source definitions ──────────────────────────────────────────────


# (module_name, prefix, source_rel, entries)
# prefix is the output artifact prefix matching CMake expected filenames.
ShaderSrc = tuple

SHADER_SOURCES = [
    ("BasicDepthReceiver", "basic_depth_receiver", "basic/depth_receiver.slang", ["vertexMain", "fragmentMain"]),
    ("BasicForward", "basic_forward", "basic/forward.slang", ["vertexMain", "fragmentMain"]),
    ("GuiBitmap", "gui_bitmap", "gui/bitmap.slang", ["vertexMain", "fragmentMain"]),
    ("Scene2DSprites", "scene_2d_sprites", "2d/sprites.slang", ["vertexMain", "fragmentMain"]),
    ("Scene2DVertices", "scene_2d_vertices", "2d/sprites.slang", ["vertexMainExpanded", "fragmentMain"]),
    ("BuildAQueenScene", "queen_scene", "examples/build_a_queen.slang", ["vertexMain", "fragmentMain"]),
    ("BuildAQueenPortable", "queen_portable", "examples/build_a_queen_portable.slang", ["vertexMain", "fragmentMain"]),
    ("BuildAQueenPortableSky", "queen_portable_sky", "examples/build_a_queen_portable_sky.slang", ["vertexMain", "fragmentMain"]),
    ("BuildAQueenPortableShadow", "queen_portable_shadow", "examples/build_a_queen_portable_shadow.slang", ["vertexMain", "fragmentMain"]),
    ("BuildAQueenSkinned", "queen_skinned", "examples/build_a_queen_skinned.slang", ["vertexMain", "fragmentMain"]),
    ("BuildAQueenSky", "queen_sky", "examples/build_a_queen_sky.slang", ["vertexMain", "fragmentMain"]),
    ("BuildAQueenShadow", "queen_shadow", "examples/build_a_queen_shadow.slang", ["vertexMain", "fragmentMain"]),
    ("BuildAQueenSkinnedShadow", "queen_skinned_shadow", "examples/build_a_queen_skinned_shadow.slang", ["vertexMain", "fragmentMain"]),
    # GUI
    ("GuiOverlay", "gui_overlay",
     "gui/overlay.slang",
     ["vertexMain", "fragmentMain"]),
    # Samples
    ("ClearSample", "clear",
     "samples/clear.slang",
     ["vertexMain", "fragmentMain"]),
    ("FeatureLabDepthFixture", "feature_lab_depth_fixture",
     "samples/feature_lab_depth_fixture.slang",
     ["vertexMain", "fragmentMain"]),
    ("FeatureLabColorProbe", "feature_lab_color_probe",
     "samples/feature_lab_color_probe.slang",
     ["vertexMain", "fragmentMain"]),
    ("WebGpuComputeSmoke", "webgpu_compute_smoke",
     "samples/webgpu_compute_smoke.slang",
     ["computeSmokeMain", "computeSmokeVertexMain",
      "computeSmokeFragmentMain"]),
    ("ForwardPlusCull", "forward_plus_cull",
     "renderer/forward_plus_cull.slang",
     ["forwardPlusCullMain"]),
    ("FogInject", "fog_inject", "renderer/fog_inject.slang", ["computeMain"]),
    ("CloudRaymarch", "cloud_raymarch", "renderer/cloud_raymarch.slang", ["computeMain"]),
    ("CloudDensitySlice", "cloud_density_slice", "renderer/cloud_density_slice.slang", ["computeMain"]),
    ("CloudTemporal", "cloud_temporal", "renderer/cloud_temporal.slang", ["computeMain"]),
    ("CloudComposite", "cloud_composite", "renderer/cloud_composite.slang", ["vertexMain", "fragmentMain"]),
    ("CloudDemoPresent", "cloud_demo_present", "samples/fog_demo_present.slang", ["vertexMain", "fragmentMain"]),
    ("FogScatter", "fog_scatter", "renderer/fog_scatter.slang", ["computeMain"]),
    ("FogScatterSpatial", "fog_scatter_spatial", "renderer/fog_scatter_spatial.slang", ["computeMain"]),
    ("FogTemporal", "fog_temporal", "renderer/fog_temporal.slang", ["computeMain"]),
    ("FogIntegrate", "fog_integrate", "renderer/fog_integrate.slang", ["computeMain"]),
    ("FogComposite", "fog_composite", "renderer/fog_composite.slang", ["vertexMain", "fragmentMain"]),
    ("FogDemoScene", "fog_demo_scene", "samples/fog_demo_scene.slang", ["vertexMain", "fragmentMain"]),
    ("FogDemoShadow", "fog_demo_shadow", "samples/fog_demo_shadow.slang", ["vertexMain", "fragmentMain"]),
    ("FogDemoGlass", "fog_demo_glass", "samples/fog_demo_glass.slang", ["vertexMain", "fragmentMain"]),
    ("FogDemoPlainGlass", "fog_demo_plain_glass", "samples/fog_demo_plain_glass.slang", ["vertexMain", "fragmentMain"]),
    ("FogDemoPresent", "fog_demo_present", "samples/fog_demo_present.slang", ["vertexMain", "fragmentMain"]),
    ("ForwardPlusDebug", "forward_plus_debug",
     "renderer/forward_plus_debug.slang",
     ["forwardPlusDebugMain"]),
    ("DepthPyramid", "depth_pyramid",
     "renderer/depth_pyramid.slang",
     ["depthPyramidCopyMain", "depthPyramidReduceMain"]),
    # WoCC game
    ("WoccPbrScene", "pbr_scene",
     "wocc/pbr_scene.slang",
     ["vertexMain", "vertexMainInstanced", "vertexMainSkinned", "fragmentMain",
      "fragmentMainFoliage"]),
    ("EditorViewportPbr", "editor_viewport_pbr",
     "editor/viewport_pbr.slang",
     ["vertexMain", "fragmentMain"]),
    ("EditorViewportEnvironment", "editor_viewport_environment",
     "editor/viewport_environment.slang",
     ["vertexMain", "fragmentMain"]),
    ("EditorViewportSprite", "editor_viewport_sprite",
     "editor/viewport_sprite.slang",
     ["vertexMain", "fragmentMain"]),
    ("WoccSky", "wocc_sky",
     "wocc/sky.slang",
     ["vertexMain", "fragmentMain"]),
    ("WoccWater", "wocc_water",
     "wocc/water.slang",
     ["vertexMain", "fragmentMain"]),
    ("WoccWaterBlend", "wocc_water_blend",
     "wocc/water_blend.slang",
     ["vertexMain", "fragmentMain"]),
    ("WoccPost", "wocc_post",
     "wocc/post.slang",
     ["vertexMain", "bloomDownsampleFragmentMain", "bloomBlurFragmentMain",
      "bloomCompositeFragmentMain", "toneMappingFragmentMain", "gradeFragmentMain"]),
    ("WoccPostFx", "wocc_post_fx",
     "wocc/post_fx.slang",
     ["vertexMain", "woccFogFragmentMain", "woccGodRayFragmentMain"]),
    ("Ddgi", "ddgi",
     "renderer/ddgi.slang",
     ["giProbeUpdateComputeMain", "giProbeTraceComputeMain",
      "giProbeBlendIrradianceComputeMain", "giProbeBlendDistanceComputeMain",
      "giIrradianceResolveComputeMain", "giProbeRelocateComputeMain",
      "giProbeBorderUpdateComputeMain"]),
    ("Ssr", "ssr",
     "renderer/ssr.slang",
     ["ssrTraceComputeMain", "vertexMain", "ssrCompositeFragmentMain"]),
    ("WoccPbrTerrain", "pbr_scene_terrain",
     "wocc/pbr_scene_terrain.slang",
     ["vertexMain", "fragmentMain"]),
    ("WoccDepthPrepass", "wocc_depth_prepass",
     "wocc/depth_prepass.slang",
     ["vertexMain", "vertexMainInstanced", "fragmentMain"]),
    ("WoccShadowDepth", "shadow_depth",
     "wocc/shadow_depth.slang",
     ["vertexMain", "vertexMainInstanced", "vertexMainSkinned", "fragmentMain"]),
    ("GpuVisibility", "gpu_visibility",
     "renderer/gpu_visibility.slang",
     ["visibilityResetComputeMain", "visibilityCullComputeMain", "visibilityEmitComputeMain", "visibilityFinalizeComputeMain"]),
    ("WoccShadowCull", "shadow_cull",
     "wocc/shadow_cull.slang",
     ["shadowClearMain", "shadowCullMain", "shadowEmitDrawMain"]),
    ("WoccVegetationCull", "vegetation_cull",
     "wocc/vegetation_cull.slang",
     ["vegetationClearMain", "vegetationCullMain",
      "vegetationEmitDrawMain"]),
    ("Ssao", "ssao",
     "wocc/ssao.slang",
     ["vertexMain", "fragmentMain"]),
    ("Particles", "particles",
     "wocc/particles.slang",
     ["vertexMain", "fragmentMain"]),
    ("WoccFxaa", "fxaa",
     "wocc/fxaa.slang",
     ["vertexMain", "fragmentMain"]),
    ("WoccTaa", "wocc_taa",
     "wocc/taa.slang",
     ["vertexMain", "fragmentMain"]),
]

SHADER_SHARED_SOURCES = (
    "examples/queen_surface.slang",
    "examples/queen_sky_color.slang",
    "common/volumetric_fog.slang",
    "core/noise3.slang",
    "core/box_geometry.slang",
    "renderer/fog_scatter_common.slang",
    "renderer/ddgi_sampling.slang",
    "renderer/ddgi_visibility.slang",
    "renderer/ddgi_depth_cache.slang",
    "samples/fog_scene_geometry.slang",
    "samples/fog_glass_geometry.slang",
)

# Shaders that need WGSL push constant binding annotation (keyed by prefix)
WGSL_PUSH_CONSTANT_SHADERS = {
    "queen_portable_shadow",
    "queen_portable",
    "basic_depth_receiver",
    "basic_forward",
    "gui_bitmap",
    "webgl1_graphics",
    "scene_2d_sprites",
    "scene_2d_vertices",
    "queen_scene",
    "queen_skinned",
    "queen_shadow",
    "queen_skinned_shadow",
    "ordinary_graphics_push",
    "ordinary_graphics_matrix_push",
    "ddgi_bvh_trace_consumer",
    "clear", "gui_overlay",
    "pbr_scene", "wocc_sky", "wocc_water", "wocc_post", "pbr_scene_terrain",
    "wocc_post_fx", "ddgi", "wocc_depth_prepass", "shadow_depth",
    "shadow_cull", "vegetation_cull", "ssao", "particles", "fxaa",
    "depth_pyramid",
    "wocc_taa",
    "editor_viewport_pbr",
    "editor_viewport_environment",
    "trail_beam",
    "ssr",
}

# Shaders that need WGSL vertex output location annotation (keyed by prefix)
WGSL_VERTEX_LOCATION_SHADERS = {
    "cloud_composite",
    "cloud_demo_present",
    "fog_composite",
    "fog_demo_scene", "fog_demo_shadow", "fog_demo_glass", "fog_demo_plain_glass", "fog_demo_present",
    "queen_portable_shadow",
    "queen_portable",
    "queen_portable_sky",
    "basic_depth_receiver",
    "basic_forward",
    "gui_bitmap",
    "scene_2d_sprites",
    "scene_2d_vertices",
    "queen_scene",
    "queen_skinned",
    "editor_viewport_sprite",
    "clear", "gui_overlay",
    "pbr_scene", "wocc_sky", "wocc_water", "wocc_post", "pbr_scene_terrain",
    "fxaa",
    "wocc_taa",
    "editor_viewport_pbr",
    "editor_viewport_environment",
}

# WGSL storage texture format/access declarations (keyed by prefix).
# slangc emits storage textures with rgba32float + read_write by default,
# which mismatches the actual Rgba16Float textures created on the C++ side
# and the WriteOnly access WebGPU requires. Each entry maps a shader prefix
# to a list of (variable_stem, wgsl_format, wgsl_access) tuples that
# set_wgsl_storage_texture_format_and_access rewrites in the emitted WGSL.
# The format MUST match the TextureFormat used at TextureDesc creation time
# and the storageTextureFormat declared in DescriptorBindingDesc.
WGSL_STORAGE_TEXTURE_DECLARATIONS = {
    "conformance_storage_fill": [("StorageOutput", "rgba16float", "write")],
    "original_general_shader_resources": [("StorageOutput", "rgba16float", "write")],
    "original_general_readonly_shader_resources": [("StorageOutput", "rgba16float", "write")],
    "original_general_graphics_shader_resources": [("StorageOutput", "rgba16float", "write")],
    "original_general_graphics_readonly_shader_resources": [("StorageOutput", "rgba16float", "write")],
    "ddgi_atlas_fill": [("AtlasOutput", "rgba16float", "write")],
    "depth_pyramid": [
        ("PyramidDestination", "rgba16float", "write"),
    ],
    "forward_plus_debug": [
        ("ForwardPlusDebugOutput", "rgba8unorm", "write"),
    ],
    "ddgi": [
        ("ProbeIrradianceOut", "rgba16float", "write"),
        # Phase 2 depth moments atlas: Rgba16Float (mean depth, mean depth^2
        # in .xy; .zw are padding). The format is a compile-time constant in
        # WGSL and must match ddgi_renderer.h's storage texture descriptor.
        ("ProbeDepthMomentsOut", "rgba16float", "write"),
        ("GiIrradianceOutput", "rgba16float", "write"),
    ],
    "ssr": [
        ("SsrReflectionOut", "rgba16float", "write"),
    ],
}


# ── main ───────────────────────────────────────────────────────────────────


def find_slangc(args):
    """Find slangc executable."""
    slangc = args.slangc
    if slangc and os.path.isfile(slangc):
        return os.path.normpath(slangc)

    vulkan_sdk = args.vulkan_sdk
    if vulkan_sdk:
        candidate = os.path.join(vulkan_sdk, "Bin", "slangc.exe")
        if os.path.isfile(candidate):
            return os.path.normpath(candidate)

    return find_tool("slangc", "SLANGC_EXECUTABLE", [
        "C:/VulkanSDK/1.4.341.1/Bin/slangc.exe",
    ])


def find_vulkan_sdk(args):
    """Find Vulkan SDK path."""
    sdk = args.vulkan_sdk
    if sdk and os.path.isdir(sdk):
        return os.path.normpath(sdk)
    sdk = os.environ.get("VULKAN_SDK", "")
    if sdk and os.path.isdir(sdk):
        return os.path.normpath(sdk)
    if os.path.isdir("C:/VulkanSDK/1.4.341.1"):
        return "C:/VulkanSDK/1.4.341.1"
    return None


def find_spirv_reflect(vulkan_sdk, requested=None):
    """Find the platform-native SPIRV-Reflect CLI deterministically."""
    candidates = []
    if requested:
        candidates.append(requested)
    env_value = os.environ.get("SPIRV_REFLECT_EXECUTABLE", "")
    if env_value:
        candidates.append(env_value)
    if vulkan_sdk:
        candidates.extend(
            os.path.join(vulkan_sdk, "Bin", name)
            for name in ("spirv-reflect", "spirv-reflect.exe"))
    for candidate in candidates:
        if os.path.isfile(candidate) and os.access(candidate, os.X_OK):
            return os.path.normpath(candidate)
    return find_tool("spirv-reflect", "SPIRV_REFLECT_EXECUTABLE", [])


def reflect_descriptor_stage_masks_per_entry(
    slangc,
    spirv_reflect_exe,
    spirv_source_path,
    entries,
    temp_dir,
    vulkan_sdk,
    push_stage_evidence=None,
):
    """Reflect descriptor visibility for every emitted entry point.

    The SPIRV-Reflect CLI exposes one module-local descriptor list for the
    first entry point when a multi-entry SPIR-V module is supplied. Reflecting
    that module alone would silently omit resources used only by later entry
    points. Compile one temporary, single-entry module per requested entry and
    union only the target-reflected masks; no stage is inferred from source or
    entry-point naming.
    """
    masks = {}
    reflection_dir = os.path.join(temp_dir, "descriptor-reflection")
    os.makedirs(reflection_dir, exist_ok=True)
    for index, entry in enumerate(entries):
        entry_spv = os.path.join(
            reflection_dir, f"descriptor-reflection-{index}.spv")
        run_slangc(
            slangc,
            spirv_source_path,
            "spirv",
            "spirv_1_5",
            [entry],
            entry_spv,
        )
        strip_spirv_debug(entry_spv, vulkan_sdk)
        if push_stage_evidence is not None:
            stage = selected_entry_push_stage(entry_spv, spirv_reflect_exe)
            if stage is not None:
                push_stage_evidence.append(stage)
        entry_masks = _bindings_codegen.reflect_descriptor_stage_masks(
            spirv_reflect_exe, entry_spv, allow_empty=True)
        for key, mask in entry_masks.items():
            masks[key] = masks.get(key, 0) | mask
    return masks


def main():
    parser = argparse.ArgumentParser(description="arp-engine shader compiler")
    parser.add_argument("--slangc", help="Path to slangc.exe")
    parser.add_argument("--vulkan-sdk", help="Path to Vulkan SDK root")
    parser.add_argument("--spirv-reflect", help="Path to the SPIRV-Reflect CLI")
    parser.add_argument("--out-dir", help="Output directory for compiled artifacts",
                        default=os.path.join(ROOT, "generated", "shaders"))
    parser.add_argument("--portable-webgpu-debug-storage", action="store_true",
                        help="Use rgba8unorm for the Forward+ debug storage fixture")
    parser.add_argument("--bindless-variant", help="Explicit finite BindlessShaderVariant YAML")
    parser.add_argument("--bindless-source", help="Slang entries consuming the generated typed helpers")
    parser.add_argument("--bindless-source-library", action="append", default=[],
                        help="Explicit ordered directive-free Slang source library (repeatable)")
    parser.add_argument("--bindless-entry", action="append", help="Entry to compile (repeatable)")
    parser.add_argument("--bindless-mode", choices=["Native", "Emulated"])
    parser.add_argument("--module", help="Select one authoritative shader manifest prefix")
    parser.add_argument("--graphics-module", action="store_true", help="Publish original combined GraphicsShaderModuleArtifact/1 and sealed selected entries")
    parser.add_argument("--preserve-implicit-sampling", action="store_true",
                        help="Preserve real textureSample derivatives in an explicit ordinary publication; native validation must reject non-uniform use")
    parser.add_argument("--single-entry", help="Compile one declared entry and publish SingleEntryShaderArtifact/1")
    parser.add_argument("--glsl-es-100", action="store_true",
                        help="Publish a versioned production ES100 target or deterministic target refusal")
    parser.add_argument("--spirv-cross-library", help="Explicit SPIRV-Cross C API shared library (API 0.68.0)")
    parser.add_argument("--webgl-optional-uniform", action="append", default=[],
                        help="Explicit stage:name uniform allowed to be optimized out (repeatable)")
    parser.add_argument("--wgsl-buffer-array-diagnostic", action="store_true",
                        help="Explicit WgslBufferArrayDiagnostic/1,2 policy; exact E56105 or reflected emitted buffer binding arrays publish unavailable WGSL")
    parser.add_argument("--wgsl-graphics-atomic-diagnostic", action="store_true",
                        help="Explicit WgslGraphicsAtomicDiagnostic/1 policy; requires selected SPIR-V FragmentAtomicWriteProof/1 and actual E36107")
    parser.add_argument("--metal-runtime-length-diagnostic", action="store_true",
                        help="Explicit MetalRuntimeLengthDiagnostic/1; requires actual E36107 and SPIR-V runtime length operations")
    parser.add_argument("--graphics-write-proof-tool", help="Path to arp_fragment_atomic_write_verifier; required by graphics atomic diagnostic policy")
    parser.add_argument("--fixture", choices=["gpu_visibility_layout", "ordinary_pbr_material", "ordinary_terrain27", "conformance_storage_fill", "conformance_storage_mrt", "webgl1_extension_execution", "webgl1_extensions", "webgl1_graphics", "descriptor_array", "descriptor_depth", "ordinary_implicit_2d", "ordinary_implicit_array", "ordinary_graphics_uniform", "ordinary_graphics_uniform_array", "ordinary_graphics_push", "ordinary_graphics_matrix_push", "ordinary_graphics_storage", "ordinary_graphics_storage_array", "ordinary_graphics_storage_write", "ordinary_graphics_storage_read", "ordinary_graphics_uniform_read",
                                            "runtime_length_structured", "runtime_length_rwstructured", "runtime_length_byteaddress", "runtime_length_rwbyteaddress", "runtime_length_graphics", "runtime_length_graphics_probe",
                                            "descriptor_compute", "descriptor_depth_compute", "descriptor_state_slots", "buffer_layout_probe", "buffer_matrix_probe", "ordinary_indirect_producer", "ordinary_indirect_noop", "ordinary_indirect_boundaries",
                                            "ddgi_atlas_fill", "ddgi_atlas_consumer", "ddgi_sampling_consumer", "ddgi_uniform_consumer", "ddgi_shared_input_consumer", "ddgi_bvh_input_consumer", "ddgi_bvh_trace_consumer", "vertex_stream_instancing", "original_vertex_geometry_execution", "original_general_shader_resources", "original_general_readonly_shader_resources", "original_general_graphics_shader_resources", "original_general_graphics_readonly_shader_resources"],
                        help="Compile a test-only fixture through the normal target/reflection pipeline")
    args = parser.parse_args()
    if args.glsl_es_100 and (not (args.single_entry or args.graphics_module) or not args.spirv_cross_library):
        parser.error("--glsl-es-100 requires an original single-entry/graphics module and --spirv-cross-library")
    if args.webgl_optional_uniform and (not args.glsl_es_100 or any(
            not value.startswith(('Vertex:', 'Fragment:')) for value in args.webgl_optional_uniform)):
        parser.error("--webgl-optional-uniform requires ES100 and explicit Vertex:name or Fragment:name")
    if args.preserve_implicit_sampling and (not (args.single_entry or args.graphics_module)
                                          or args.bindless_variant or not (args.module or args.fixture)):
        parser.error("--preserve-implicit-sampling requires an explicit ordinary single-entry or graphics-module publication")
    if args.wgsl_buffer_array_diagnostic and not args.single_entry:
        parser.error("--wgsl-buffer-array-diagnostic requires --single-entry")
    if args.wgsl_graphics_atomic_diagnostic and (not args.single_entry or not args.graphics_write_proof_tool or args.wgsl_buffer_array_diagnostic):
        parser.error("--wgsl-graphics-atomic-diagnostic requires --single-entry and --graphics-write-proof-tool; diagnostic policies are mutually exclusive")
    if args.metal_runtime_length_diagnostic and (not (args.single_entry or args.graphics_module) or args.wgsl_buffer_array_diagnostic or args.wgsl_graphics_atomic_diagnostic or args.bindless_variant):
        parser.error("--metal-runtime-length-diagnostic requires an explicit ordinary publication and exclusive diagnostic policy")
    shader_sources = SHADER_SOURCES if not args.fixture else [
        ("DescriptorFixture", args.fixture, "tests/fixtures/" + args.fixture + ".slang",
         ["computeMain", "cacheReadMain"] if args.fixture == "ddgi_sampling_consumer" else
         ["positiveBaseMain", "zeroInstanceBaseMain", "outOfRangeIndexMain", "outOfRangeInstanceMain"] if args.fixture == "ordinary_indirect_boundaries" else
         ["noopIndexMain", "noopInstanceMain"] if args.fixture == "ordinary_indirect_noop" else
         ["computeMain"] if args.fixture in ("gpu_visibility_layout", "conformance_storage_fill", "runtime_length_structured", "runtime_length_rwstructured", "runtime_length_byteaddress", "runtime_length_rwbyteaddress", "descriptor_compute", "descriptor_depth_compute", "descriptor_state_slots", "buffer_layout_probe", "buffer_matrix_probe", "ordinary_indirect_producer", "original_general_shader_resources", "original_general_readonly_shader_resources", "ddgi_atlas_fill", "ddgi_atlas_consumer", "ddgi_uniform_consumer", "ddgi_shared_input_consumer", "ddgi_bvh_input_consumer", "ddgi_bvh_trace_consumer")
         else ["vertexMain", "fragmentMain"])]
    if args.module:
        if args.fixture:
            parser.error("--module and --fixture are mutually exclusive")
        shader_sources = [item for item in shader_sources if item[1] == args.module]
        if len(shader_sources) != 1:
            parser.error("--module must select exactly one declared shader prefix")
    if args.graphics_module and (args.single_entry or args.bindless_variant or args.wgsl_buffer_array_diagnostic
                                or args.wgsl_graphics_atomic_diagnostic or len(shader_sources) != 1
                                or not (args.module or args.fixture)):
        parser.error("--graphics-module requires one explicit ordinary --module/--fixture without single-entry or diagnostic policies")
    if args.single_entry:
        if len(shader_sources) != 1 or not (args.module or args.fixture):
            parser.error("--single-entry requires one explicit --module or --fixture")
        module, prefix, source, entries = shader_sources[0]
        if args.single_entry not in entries:
            parser.error("--single-entry must name a declared entry")
        shader_sources = [(module, prefix, source, [args.single_entry])]

    slangc = find_slangc(args)
    if not slangc:
        print("ERROR: slangc not found. Set VULKAN_SDK, pass --slangc, or ensure PATH includes slangc.",
              file=sys.stderr)
        sys.exit(1)

    vulkan_sdk = find_vulkan_sdk(args)
    spirv_reflect_exe = find_spirv_reflect(vulkan_sdk, args.spirv_reflect)
    if not spirv_reflect_exe:
        print(
            "ERROR: spirv-reflect is required to emit authoritative descriptor stage metadata.",
            file=sys.stderr)
        sys.exit(1)
    out_dir = os.path.normpath(args.out_dir)
    os.makedirs(out_dir, exist_ok=True)
    if args.single_entry:
        publication_path = os.path.join(out_dir, f"{shader_sources[0][1]}.{args.single_entry}.entry.yaml")
        if os.path.isfile(publication_path):
            os.remove(publication_path)

    if args.graphics_module:
        publication_path = os.path.join(out_dir, f"{shader_sources[0][1]}.graphics.module.yaml")
        if os.path.isfile(publication_path):
            os.remove(publication_path)
        webgl_publication = os.path.join(out_dir, f"{shader_sources[0][1]}.webgl-program.yaml")
        if args.glsl_es_100 and os.path.isfile(webgl_publication):
            os.remove(webgl_publication)

    if any((args.bindless_variant, args.bindless_source, args.bindless_entry, args.bindless_mode, args.bindless_source_library)):
        if not all((args.bindless_variant, args.bindless_source, args.bindless_entry, args.bindless_mode)) or args.fixture or args.module or args.single_entry:
            parser.error("Bindless compilation requires variant, source, entries and mode, without --fixture")
        import yaml
        from bindless_codegen import compile_bindless_package
        with open(args.bindless_variant, encoding="utf-8") as schema:
            variant = yaml.safe_load(schema)
        package = compile_bindless_package(variant, args.bindless_source, args.bindless_entry,
            args.bindless_mode, out_dir, slangc, spirv_reflect_exe, args.bindless_source_library)
        print(f"[shaders] Bindless package: {package}")
        return

    print(f"[shaders] out: {out_dir}")
    print(f"[shaders] slangc: {slangc}")

    for module_name, prefix, source_rel, entries in shader_sources:
        source_path = os.path.join(ROOT if args.fixture else SHADER_INCLUDE_DIR, source_rel)
        if not args.single_entry and not os.path.isfile(source_path):
            # WoCC shaders use source_rel relative to ROOT, not shaders/
            source_path = os.path.join(ROOT, source_rel)
        if not os.path.isfile(source_path):
            if args.single_entry or args.graphics_module:
                raise RuntimeError("SHADER-MODULE-SOURCE-MISSING" if args.graphics_module else "SHADER-ENTRY-SOURCE-MISSING")
            print(f"  WARNING: source not found: {source_path}", file=sys.stderr)
            continue
        source_text = read_text(source_path)
        selected_push_inactive = False
        try:
            push_contract = push_constant_lowering_contract(source_text, source_path)
        except RuntimeError as error:
            print(f"  ERROR: {error}", file=sys.stderr)
            sys.exit(1)

        spirv_out = os.path.join(out_dir, f"{prefix}.spv")
        metal_out = os.path.join(out_dir, f"{prefix}.metal")
        wgsl_out = os.path.join(out_dir, f"{prefix}.wgsl")
        refl_out = os.path.join(out_dir, f"{prefix}.reflection.yaml")

        # A tolerated WGSL compile failure is an explicit unavailable target,
        # never permission to retain a stale artifact from an older compiler
        # run. Removing it before invocation keeps reflection and runtime
        # capability gates truthful for atomic compute shaders.
        if prefix in {"vegetation_cull", "shadow_cull"} and os.path.isfile(wgsl_out):
            os.remove(wgsl_out)

        source_without_comments = re.sub(
            r"//[^\n]*|/\*.*?\*/", "", source_text, flags=re.DOTALL)
        has_logical_subpass_input = bool(re.search(
            r"\bSUBPASS_INPUT_(?:COLOR|DEPTH)\s*\(",
            source_without_comments,
        ))

        # Every target starts from the same logical source.  Target-specific
        # attributes and physical resource types are injected into temporary
        # compiler inputs below; the authored Slang file is never mutated.
        with tempfile.TemporaryDirectory(prefix=f"arp-{prefix}-lowering-") as temp_dir:
            spirv_source_path = source_path
            if has_logical_subpass_input:
                try:
                    transformed_source, _ = lower_subpass_input_source_for_spirv(
                        source_text, source_path)
                except RuntimeError as error:
                    print(f"  ERROR: {error}", file=sys.stderr)
                    sys.exit(1)
                spirv_source_path = os.path.join(
                    temp_dir, f"{os.path.basename(source_path)}.spirv.slang")
                write_utf8_no_bom(spirv_source_path, transformed_source)

            # Compile to SPIR-V.  Vulkan input-attachment decorations are
            # compiler-owned lowering metadata, not source-level backend code.
            run_slangc(
                slangc, spirv_source_path, "spirv", "spirv_1_5", entries, spirv_out)
            strip_spirv_debug(spirv_out, vulkan_sdk)
            single_entry_stage_masks = None
            if args.single_entry:
                single_entry_stage_masks = _bindings_codegen.reflect_descriptor_stage_masks(
                    spirv_reflect_exe, spirv_out, allow_empty=True)
                if push_contract and not selected_entry_has_push_constants(spirv_out, spirv_reflect_exe):
                    selected_push_inactive = True
                    push_contract = None
            buffer_array_diagnostic_allowed = False
            graphics_atomic_diagnostic_allowed = False
            if args.wgsl_graphics_atomic_diagnostic:
                import yaml
                verified = subprocess.run([args.graphics_write_proof_tool, spirv_out,
                    str(os.path.getsize(spirv_out)), args.single_entry], capture_output=True, text=True)
                if verified.returncode:
                    raise RuntimeError("SHADER-WGSL-GRAPHICS-ATOMIC-POLICY-MISMATCH: " + verified.stderr.strip())
                proof = yaml.safe_load(verified.stdout)
                if proof.get("type") != "FragmentAtomicWriteProof" or proof.get("contractVersion") != 1 or proof.get("byteOffset") != 0 or proof.get("byteCount") != 4:
                    raise RuntimeError("SHADER-WGSL-GRAPHICS-ATOMIC-PROOF-CONTRACT")
                authoritative_bindings = get_shader_bindings(source_text)
                _bindings_codegen.apply_descriptor_stage_masks(authoritative_bindings, single_entry_stage_masks,
                    spirv_out, allow_inactive=True)
                active_bindings = [b for b in authoritative_bindings if b.get("stageMask", 0)]
                graphics_atomic_diagnostic_allowed = len(active_bindings) == 1 and active_bindings[0]["type"] == "StorageBuffer" and active_bindings[0]["arrayCount"] == 1 and active_bindings[0]["stageMask"] == 2 and active_bindings[0]["set"] == proof["set"] and active_bindings[0]["binding"] == proof["binding"]
                if not graphics_atomic_diagnostic_allowed:
                    raise RuntimeError("SHADER-WGSL-GRAPHICS-ATOMIC-POLICY-MISMATCH")
                if os.path.isfile(wgsl_out):
                    os.remove(wgsl_out)
            if args.wgsl_buffer_array_diagnostic:
                authoritative_bindings = get_shader_bindings(source_text)
                _bindings_codegen.apply_descriptor_stage_masks(authoritative_bindings, single_entry_stage_masks,
                    spirv_out, allow_inactive=True)
                buffer_arrays = _bindings_codegen.reflect_buffer_array_descriptors(spirv_reflect_exe, spirv_out)
                active_buffer_arrays = [binding for binding in authoritative_bindings
                    if binding.get("stageMask", 0) and binding["type"] in ("UniformBuffer", "ReadOnlyStorageBuffer", "StorageBuffer")
                    and binding["arrayCount"] > 1]
                buffer_array_diagnostic_allowed = bool(active_buffer_arrays) and all(
                    buffer_arrays.get((binding["set"], binding["binding"])) == binding["arrayCount"]
                    for binding in active_buffer_arrays)
                if not buffer_array_diagnostic_allowed:
                    raise RuntimeError("SHADER-WGSL-BUFFER-ARRAY-POLICY-MISMATCH")
                if os.path.isfile(wgsl_out):
                    os.remove(wgsl_out)

            # Compile Metal from a compiler-owned temporary source. Logical
            # subpass declarations are placeholders here; the generated MSL
            # is subsequently rewritten to carry real framebuffer-fetch
            # attributes. The authored Slang source remains backend-neutral.
            metal_source_path = source_path
            metal_fetches = []
            if has_logical_subpass_input:
                try:
                    transformed_source, metal_fetches = \
                        lower_metal_subpass_source(source_text, source_path)
                except RuntimeError as error:
                    print(f"  ERROR: {error}", file=sys.stderr)
                    sys.exit(1)
                metal_source_path = os.path.join(
                    temp_dir, f"{os.path.basename(source_path)}.metal.slang")
                write_utf8_no_bom(metal_source_path, transformed_source)
            if args.metal_runtime_length_diagnostic:
                if os.path.isfile(metal_out):
                    os.remove(metal_out)
                from spirv_layout import SpirvLayoutFacts
                if not SpirvLayoutFacts(spirv_out).runtime_length_operations:
                    raise RuntimeError("SHADER-METAL-RUNTIME-LENGTH-OPERATION-REQUIRED")
            metal_ok = run_slangc(slangc, metal_source_path, "metal", "sm_6_0", entries, metal_out,
                diagnostic_failure_code="E36107" if args.metal_runtime_length_diagnostic else None)
            if args.metal_runtime_length_diagnostic and metal_ok:
                os.remove(metal_out)
                raise RuntimeError("SHADER-METAL-RUNTIME-LENGTH-CONTRACT-REVIEW-REQUIRED")
            metal_unavailable_reason = "SHADER-METAL-RUNTIME-LENGTH-UNSUPPORTED" if not metal_ok else None
            if metal_ok:
                if metal_fetches:
                    try:
                        lower_metal_framebuffer_fetch_artifact(
                            metal_out, metal_fetches, entries)
                    except RuntimeError as error:
                        print(f"  ERROR: {error}", file=sys.stderr)
                        sys.exit(1)
                try:
                    lower_metal_argument_buffer_artifact(
                        metal_out, get_shader_bindings(source_text), single_entry_stage_masks)
                except RuntimeError as error:
                    print(f"  ERROR: {error}", file=sys.stderr)
                    sys.exit(1)

            # Compile to WGSL from a compiler-lowered logical source. Slang's
            # capability checker currently rejects InterlockedAdd for WGSL,
            # even though its ignored-capability output can be repaired by the
            # explicit atomic storage lowering below. These two shaders have a
            # contract-tested lowering path, so invoke that mode directly
            # instead of intentionally compiling once only to emit a warning.
            _wgsl_atomic_compute = {"vegetation_cull", "shadow_cull", "gpu_visibility"}
            webgpu_subpass_lowered = False
            wgsl_source_path = source_path
            if has_logical_subpass_input:
                try:
                    transformed_source, webgpu_subpass_lowered = \
                        lower_subpass_input_source_for_wgsl(source_text, source_path)
                except RuntimeError as error:
                    print(f"  ERROR: {error}", file=sys.stderr)
                    sys.exit(1)
                wgsl_source_path = os.path.join(
                    temp_dir, f"{os.path.basename(source_path)}.wgsl.slang")
                write_utf8_no_bom(wgsl_source_path, transformed_source)
            wgsl_ok = run_slangc(
                slangc,
                wgsl_source_path,
                "wgsl",
                "sm_6_0",
                entries,
                wgsl_out,
                ignore_capabilities=(prefix in _wgsl_atomic_compute),
                diagnostic_failure_code="E56105" if buffer_array_diagnostic_allowed else "E36107" if graphics_atomic_diagnostic_allowed else None,
            )

        webgpu_unavailable_reason = None
        if not wgsl_ok:
            webgpu_unavailable_reason = "SHADER-WGSL-BUFFER-ARRAY-UNSUPPORTED" if buffer_array_diagnostic_allowed else "SHADER-WGSL-GRAPHICS-ATOMIC-UNSUPPORTED" if graphics_atomic_diagnostic_allowed else "WGSL compilation failed"
        elif buffer_array_diagnostic_allowed:
            # Successful compiler emission is not executable WebGPU buffer-array
            # ABI. Classify from exact metadata, never from a caught parser error.
            validate_wgsl_buffer_array_diagnostic_artifact(
                authoritative_bindings, buffer_arrays, wgsl_out)
            os.remove(wgsl_out)
            wgsl_ok = False
            webgpu_unavailable_reason = "SHADER-WGSL-BUFFER-ARRAY-ABI-UNAVAILABLE"
            print("  [wgsl] WgslBufferArrayDiagnostic/2: " + webgpu_unavailable_reason)
        if push_contract and wgsl_ok:
            try:
                push_contract.update(derive_wgsl_push_constant_contract(wgsl_out))
            except RuntimeError as error:
                print(f"  ERROR: {error}", file=sys.stderr)
                sys.exit(1)

        metal_binding_space = metal_framebuffer_fetch = metal_argument_buffer = None
        metal_gpu_counted_indirect = metal_tessellation = None
        if metal_ok:
            try:
                metal_binding_space = get_metal_binding_space_lowering(
                    get_shader_bindings(source_text), metal_out)
            except RuntimeError as error:
                print(f"  ERROR: {error}", file=sys.stderr)
                sys.exit(1)
            try:
                metal_framebuffer_fetch = get_metal_framebuffer_fetch_lowering(
                    get_shader_bindings(source_text), metal_out)
                if not any(binding.get("active") for binding in metal_framebuffer_fetch):
                    metal_framebuffer_fetch = None
            except RuntimeError as error:
                print(f"  ERROR: {error}", file=sys.stderr)
                sys.exit(1)
            try:
                metal_argument_buffer = get_metal_argument_buffer_lowering(
                    get_shader_bindings(source_text), metal_out)
                if not any(binding.get("active") for binding in metal_argument_buffer):
                    metal_argument_buffer = None
            except RuntimeError as error:
                print(f"  ERROR: {error}", file=sys.stderr)
                sys.exit(1)
            try:
                metal_gpu_counted_indirect = get_metal_gpu_counted_indirect_lowering(
                    source_text, metal_out, entries)
                metal_tessellation = get_metal_tessellation_lowering(
                    source_text, metal_out, entries)
            except RuntimeError as error:
                print(f"  ERROR: {error}", file=sys.stderr)
                sys.exit(1)
            if push_contract:
                try:
                    push_contract["metalBufferIndex"] = \
                        get_metal_push_constant_buffer_index(metal_out)
                except RuntimeError as error:
                    print(f"  ERROR: {error}", file=sys.stderr)
                    sys.exit(1)
                descriptor_buffer_slots = {
                    binding.get("metalSlot")
                    for binding in metal_binding_space
                    if binding.get("active")
                    and binding.get("physicalType") in {
                        "UniformBuffer", "StorageBuffer", "ReadOnlyStorageBuffer"
                    }
                }
                if push_contract["metalBufferIndex"] in descriptor_buffer_slots:
                    print(
                        f"  ERROR: {metal_out}: push-constant buffer slot "
                        f"{push_contract['metalBufferIndex']} collides with a "
                        "descriptor buffer slot",
                        file=sys.stderr,
                    )
                    sys.exit(1)

        # WGSL post-processing (skip if WGSL compilation failed)
        if wgsl_ok:
            normalize_wgsl_struct_member_delimiters(wgsl_out)

            if prefix in _wgsl_atomic_compute:
                lower_wgsl_atomic_storage_buffers(wgsl_out, source_text,
                    entry_filtered=(prefix == "gpu_visibility" and bool(args.single_entry)))

            if webgpu_subpass_lowered:
                lower_wgsl_subpass_input_current_fragment(wgsl_out)
                lower_wgsl_subpass_input_depth_types(wgsl_out, source_text)

            if prefix in WGSL_PUSH_CONSTANT_SHADERS and not selected_push_inactive:
                if not push_contract:
                    print(f"  ERROR: {source_path}: WGSL push-constant shader has no lowering contract", file=sys.stderr)
                    sys.exit(1)
                set_wgsl_push_constant_binding(wgsl_out, push_contract)

            # WGSL storage texture format/access rewrite. slangc defaults storage
            # textures to rgba32float + read_write; the C++ side creates Rgba16Float
            # textures and WebGPU requires WriteOnly access, so the WGSL output
            # must be rewritten to match. Each shader with storage textures must
            # register its declarations in WGSL_STORAGE_TEXTURE_DECLARATIONS.
            if prefix in WGSL_STORAGE_TEXTURE_DECLARATIONS:
                apply_wgsl_storage_texture_declarations(wgsl_out, source_text,
                    WGSL_STORAGE_TEXTURE_DECLARATIONS[prefix], single_entry_stage_masks)

            # Vertex output location annotation
            if prefix in WGSL_VERTEX_LOCATION_SHADERS:
                set_wgsl_vertex_output_locations(wgsl_out)

            # WebGPU binding space lowering (for compatible profiles)
            if prefix in WGSL_PUSH_CONSTANT_SHADERS and not selected_push_inactive:
                lower_wgsl_webgpu_binding_space(wgsl_out, push_contract)

            # Some shaders need binding space lowering but not location/PC annotation
            _binding_space_only = {"global_illumination", "wocc_post_fx", "ddgi", "wocc_depth_prepass"}
            if prefix in _binding_space_only:
                lower_wgsl_webgpu_binding_space(wgsl_out, push_contract)

            if not args.preserve_implicit_sampling:
                lower_wgsl_implicit_texture_sampling(wgsl_out)

            # Source-level ARP_SAMPLED_DEPTH is the L2 semantic authority for
            # sampled D32 resources. Slang emits Texture2D<float> as
            # texture_2d<f32>, so rewrite every explicitly declared depth
            # binding before deriving the physical WGSL binding contract.
            for binding in get_shader_bindings(source_text):
                if binding.get("depthTexture", False):
                    set_wgsl_depth_texture_type(
                        wgsl_out, binding["name"] + "_0")

            # Emit the final compact physical interface after every target
            # rewrite. Reflection is generated from this exact WGSL, so the
            # native pipeline and descriptor bind paths consume one mapping.
            try:
                compact_wgsl_binding_space(
                    wgsl_out, get_shader_bindings(source_text), push_contract)
            except RuntimeError as error:
                print(f"  ERROR: {error}", file=sys.stderr)
                sys.exit(1)

            # Deterministic reserved-binding and physical-slot checks happen
            # after the compact remap, never on an intermediate annotation.
            if push_contract:
                validate_wgsl_reserved_group(wgsl_out, push_contract)
            validate_wgsl_group0_no_collision(wgsl_out)

        # Run SPIRV-Reflect codegen for pilot shaders (generates .shader_bindings.h
        # and authoritative push-constant member layout). Non-pilot shaders skip.
        pc_members_yaml = None
        descriptor_stage_masks = None
        push_stage_evidence = [] if args.graphics_module else None
        if spirv_reflect_exe:
            logical_bindings = get_shader_bindings(source_text)
            if logical_bindings or push_stage_evidence is not None:
                try:
                    descriptor_stage_masks = reflect_descriptor_stage_masks_per_entry(
                        slangc,
                        spirv_reflect_exe,
                        spirv_source_path,
                        entries,
                        temp_dir,
                        vulkan_sdk,
                        push_stage_evidence,
                    )
                except RuntimeError as err:
                    print(
                        f"  ERROR: descriptor reflection failed for {prefix}: {err}",
                        file=sys.stderr)
                    sys.exit(1)
            else:
                descriptor_stage_masks = {}
        if spirv_reflect_exe and get_push_constant_size_bytes(source_text) > 0 and not selected_push_inactive:
            try:
                block = _bindings_codegen.reflect_push_constant_block(
                    spirv_reflect_exe, spirv_out)
                if block is None:
                    raise RuntimeError(
                        f"no push-constant block found in {spirv_out}")
                pc_members_yaml = _bindings_codegen.emit_push_constant_members_yaml(block)
                if prefix in _bindings_codegen.PILOT_STRUCT_NAMES:
                    struct_name = _bindings_codegen.PILOT_STRUCT_NAMES[prefix]
                    source_basename = os.path.basename(source_path)
                    spv_basename = os.path.basename(spirv_out)
                    header_path = os.path.join(out_dir, f"{prefix}.shader_bindings.h")
                    block = _bindings_codegen.generate_for_shader(
                        spirv_reflect_exe, spirv_out, header_path,
                        struct_name, source_basename, spv_basename)
                    print(f"  [codegen] {prefix}: {struct_name} ({len(block.members)} members, {block.size}B) -> {os.path.basename(header_path)}")
            except RuntimeError as err:
                print(f"  ERROR: codegen failed for {prefix}: {err}", file=sys.stderr)
                sys.exit(1)
        if args.portable_webgpu_debug_storage and module_name == "ForwardPlusDebug":
            debug_wgsl = read_text(wgsl_out).replace(
                "texture_storage_2d<rgba32float,",
                "texture_storage_2d<rgba8unorm,")
            write_utf8_no_bom(wgsl_out, debug_wgsl)

        runtime_source_path = os.path.join(out_dir, f"{prefix}.slang")
        write_utf8_no_bom(runtime_source_path, source_text)

        # Generate reflection YAML (with member-level layout when codegen ran).
        write_shader_reflection(
            runtime_source_path, module_name, spirv_out, metal_out if metal_ok else None,
            wgsl_out if wgsl_ok else None, refl_out,
            push_contract,
                                spirv_reflect_exe=spirv_reflect_exe,
                                push_constant_members_yaml=pc_members_yaml,
                                descriptor_stage_masks=descriptor_stage_masks,
                                webgpu_subpass_lowered=webgpu_subpass_lowered,
                                webgpu_unavailable_reason=webgpu_unavailable_reason,
                                entry_names=entries,
                                metal_binding_space=metal_binding_space,
                                metal_argument_buffer=metal_argument_buffer,
                                metal_framebuffer_fetch=metal_framebuffer_fetch,
                                metal_gpu_counted_indirect=metal_gpu_counted_indirect,
                                metal_tessellation=metal_tessellation,
                                single_entry=bool(args.single_entry), metal_unavailable_reason=metal_unavailable_reason,
                                selected_push_inactive=selected_push_inactive,
                                push_stage_evidence=push_stage_evidence)
        if args.single_entry:
            if not wgsl_ok and not (buffer_array_diagnostic_allowed or graphics_atomic_diagnostic_allowed):
                raise RuntimeError("SHADER-ENTRY-WGSL-UNAVAILABLE")
            publication = publish_single_entry_artifact(prefix, args.single_entry, out_dir,
                webgpu_unavailable_reason if not wgsl_ok else None, spirv_reflect_exe, metal_unavailable_reason)
            print(f"[shaders] Single entry artifact: {publication}")
            if args.glsl_es_100:
                from webgl_shader_artifact import WebGlShaderTargetCompiler
                with open(publication, encoding="utf-8") as entry_file:
                    import yaml
                    stage = yaml.safe_load(entry_file)["stage"]
                if any(not value.startswith(stage + ':') for value in args.webgl_optional_uniform):
                    raise RuntimeError("SHADER-WEBGL1-OPTIONAL-UNIFORM-STAGE")
                optional = [value.split(':', 1)[1] for value in args.webgl_optional_uniform if value.startswith(stage + ':')]
                target = WebGlShaderTargetCompiler(args.spirv_cross_library, slangc).publish(publication, optional)
                print(f"[shaders] ES100 target artifact: {target}")
        if args.graphics_module:
            if not wgsl_ok:
                raise RuntimeError("SHADER-MODULE-WGSL-UNAVAILABLE")
            import yaml
            with open(refl_out, encoding="utf-8") as file:
                module_reflection = yaml.safe_load(file)
            selected = module_reflection.get("entryPoints", [])
            if len(selected) != 2 or {entry.get("stage") for entry in selected} != {"Vertex", "Fragment"}:
                raise RuntimeError("SHADER-MODULE-ENTRY-COVERAGE")
            child_publications = {}
            for entry in selected:
                child_dir = os.path.join(out_dir, entry["stage"])
                child_args = [sys.executable, os.path.abspath(__file__), "--slangc", slangc,
                              "--spirv-reflect", spirv_reflect_exe, "--single-entry", entry["name"], "--out-dir", child_dir]
                child_args += ["--fixture", args.fixture] if args.fixture else ["--module", args.module]
                if args.preserve_implicit_sampling:
                    child_args.append("--preserve-implicit-sampling")
                if args.glsl_es_100:
                    child_args += ["--glsl-es-100", "--spirv-cross-library", args.spirv_cross_library]
                    for value in args.webgl_optional_uniform:
                        if value.startswith(entry["stage"] + ':'):
                            child_args += ["--webgl-optional-uniform", value]
                if args.metal_runtime_length_diagnostic:
                    child_args.append("--metal-runtime-length-diagnostic")
                subprocess.run(child_args, check=True)
                child_publications[entry["stage"]] = os.path.join(child_dir, f"{prefix}.{entry['name']}.entry.yaml")
            publication = publish_graphics_module_artifact(prefix, out_dir, child_publications, metal_unavailable_reason)
            print(f"[shaders] Graphics module artifact: {publication}")
            if args.glsl_es_100:
                from pathlib import Path
                from webgl_shader_artifact import publish_program
                targets = [Path(child_publications[stage]).with_suffix('.glsl-es-100.yaml')
                           for stage in ('Vertex', 'Fragment')]
                publish_program(*targets, Path(out_dir) / f'{prefix}.webgl-program.yaml')

    # Verify all reflection outputs exist
    print("[shaders] Verifying reflection outputs...")
    for module_name, prefix, source_rel, entries in shader_sources:
        refl_out = os.path.join(out_dir, f"{prefix}.reflection.yaml")
        if not os.path.isfile(refl_out):
            print(f"  ERROR: reflection output missing: {refl_out}", file=sys.stderr)
            sys.exit(1)

    print(f"[shaders] Compile complete: {out_dir}")


if __name__ == "__main__":
    main()
