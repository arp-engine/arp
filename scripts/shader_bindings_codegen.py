#!/usr/bin/env python3
"""Shader binding code generator for arp-engine.

Reads a compiled SPIR-V binary, invokes spirv-reflect (shipped with the Vulkan
SDK) to extract authoritative push-constant / UBO block layout (member offsets,
sizes, matrix stride, row-major decorations), and emits:

  1. <prefix>.shader_bindings.h  - strongly-typed C++ struct + compile-time
     size/offset constants. The shader source is the single source of truth
     for layout; this header is generated, never hand-edited.
  2. pushConstantMembers[] section appended to the reflection YAML written by
     compile_shaders.py (the function here returns the member list; the caller
     is responsible for emitting it into the YAML document).

Matrix layout convention (AGENTS.md "Matrix Layout" hard constraint):
  - C++ stores row-major bytes (core::Mat4 is row-major).
  - Explicit row-major compilation maps default ``float4x4`` to SPIR-V ColMajor.
  - Therefore codegen emits ``float <name>[16]`` for matrix fields and the
    shader MUST declare the field as default ``float4x4`` (no ``row_major``
    keyword). The spirv-reflect ``decorations`` flag must contain
    COL_MAJOR flag (0x8) for matrix members; RowMajor flag (0x4) is rejected.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Tuple

# spirv-reflect decoration bit flags (SpvReflectDecoratorBits).
# Only the ones we care about are enumerated here.
_SPV_REFLECT_DECORATION_ROW_MAJOR = 0x00000004
_SPV_REFLECT_DECORATION_COL_MAJOR = 0x00000008

# SPIRV-Reflect type_flags bits (from spirv_reflect.h SpvReflectTypeFlags).
# Used to distinguish float scalars from int/uint scalars in push constants.
# The numeric.scalar.signedness field disambiguates int (1) vs uint (0),
# but does NOT distinguish float from uint (both report signedness=0).
# type_flags is the only reliable discriminator: FLOAT (0x08) vs INT (0x04).
_SPV_REFLECT_TYPE_FLAG_INT = 0x00000004
_SPV_REFLECT_TYPE_FLAG_FLOAT = 0x00000008
_SPV_REFLECT_TYPE_FLAG_VECTOR = 0x00000100
_SPV_REFLECT_TYPE_FLAG_MATRIX = 0x00000200
_SPV_REFLECT_TYPE_FLAG_ARRAY = 0x20000000
_SPV_REFLECT_TYPE_FLAG_STRUCT = 0x00080000


@dataclass
class PushConstantMember:
    name: str
    scalar_width: int = 32
    scalar_signed: int = 0
    # type_flags from the member's type_description (verbosity 1). 0 if the
    # type_description was not parsed (older runs); the codegen falls back
    # to treating scalars as float in that case.
    type_flags: int = 0
    vec_component_count: int = 0
    matrix_column_count: int = 0
    matrix_row_count: int = 0
    matrix_stride: int = 0
    offset: int = 0
    size: int = 0
    decorations: int = 0
    is_matrix: bool = False
    is_vector: bool = False
    array_dims: List[int] = field(default_factory=list)
    array_stride: int = 0


@dataclass
class PushConstantBlock:
    instance_name: str  # e.g. "gSky"
    type_name: str      # e.g. "WoccSkyPushConstants_std430"
    size: int = 0
    members: List[PushConstantMember] = field(default_factory=list)


def _run_spirv_reflect(spirv_reflect_exe: str, spv_path: str) -> str:
    """Invoke spirv-reflect with YAML + verbosity 1 (includes type descriptions)."""
    if not os.path.isfile(spv_path):
        raise FileNotFoundError(f"SPIR-V binary not found: {spv_path}")
    if not os.path.isfile(spirv_reflect_exe):
        raise FileNotFoundError(f"spirv-reflect not found: {spirv_reflect_exe}")
    result = subprocess.run(
        [spirv_reflect_exe, "--yaml", "-v", "1", spv_path],
        capture_output=True, text=True, check=True,
    )
    return result.stdout


class BufferDeclarationLayoutReflector:
    """Strict tooling facts; neither target binding minima nor execution proof.

    The binary supplies declaration relationships/occupied extents. The exact
    module alias list supplies selected descriptor membership. Reflected padding
    remains a separate fact and never replaces a runtime stride or element count.
    """
    @staticmethod
    def integer(node, field):
        value = node.get(field) if isinstance(node, dict) else None
        if type(value) is not int or not 0 <= value <= (1 << 64) - 1:
            raise RuntimeError("SPIRV-BUFFER-REFLECTION-FIELD:" + field)
        return value

    @staticmethod
    def members(node):
        count = BufferDeclarationLayoutReflector.integer(node, "member_count")
        if "members" not in node:
            raise RuntimeError("SPIRV-BUFFER-REFLECTION-MEMBERS")
        values = node["members"]
        if values is None and count == 0:
            return []  # Explicit empty sequence in SPIRV-Reflect YAML.
        if not isinstance(values, list) or len(values) != count:
            raise RuntimeError("SPIRV-BUFFER-REFLECTION-MEMBERS")
        return values

    @classmethod
    def parse(cls, yaml_text, spv_path):
        import yaml
        from spirv_layout import SpirvLayoutFacts
        try:
            root = yaml.safe_load(yaml_text)
        except yaml.YAMLError as error:
            raise RuntimeError("SPIRV-BUFFER-REFLECTION-YAML") from error
        if not isinstance(root, dict) or not isinstance(root.get("module"), dict):
            raise RuntimeError("SPIRV-BUFFER-REFLECTION-MODULE")
        module = root["module"]
        count = cls.integer(module, "descriptor_binding_count")
        descriptors = module.get("descriptor_bindings")
        if descriptors is None and count == 0 and "descriptor_bindings" in module:
            descriptors = []
        if not isinstance(descriptors, list) or len(descriptors) != count:
            raise RuntimeError("SPIRV-BUFFER-REFLECTION-DESCRIPTORS")
        known = root.get("all_descriptor_bindings")
        if known is None and not descriptors and "all_descriptor_bindings" in root:
            known = []
        if not isinstance(known, list) or any(not isinstance(d, dict) for d in known):
            raise RuntimeError("SPIRV-BUFFER-REFLECTION-ALIASES")
        facts = SpirvLayoutFacts(spv_path)
        layouts = facts.buffer_layouts()
        result, keys = {}, set()
        for descriptor in descriptors:
            if not any(descriptor is item for item in known):
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-ALIASES")
            key = (cls.integer(descriptor, "set"), cls.integer(descriptor, "binding"))
            if key in keys:
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-DUPLICATE")
            keys.add(key)
            kind = cls.integer(descriptor, "descriptor_type")
            if kind not in (6, 7):
                if key in layouts:
                    raise RuntimeError("SPIRV-BUFFER-REFLECTION-TYPE")
                continue
            layout = layouts.get(key)
            if layout is None or cls.integer(descriptor, "spirv_id") != layout["spirvId"] or (
                    kind == 6) != (layout["storageClass"] == 2):
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-BINARY-MISMATCH")
            array = descriptor.get("array")
            if not isinstance(array, dict) or not isinstance(array.get("dims"), list) or (
                    cls.integer(array, "dims_count") != len(array["dims"])):
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-ARRAY")
            cardinality = 1
            for dimension in array["dims"]:
                if type(dimension) is not int or dimension <= 0:
                    raise RuntimeError("SPIRV-BUFFER-REFLECTION-ARRAY")
                cardinality = facts._bounded(cardinality * dimension)
            if cardinality != layout["descriptorCount"]:
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-ARRAY")
            block = descriptor.get("block")
            cls._validate_block_tree(root, block, facts, descriptor["spirv_id"])
            size, padded = cls.integer(block, "size"), cls.integer(block, "padded_size")
            if size < layout["staticOccupiedExtentBytes"] or padded < size:
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-BLOCK-SIZE")
            result[key] = dict(layout, reflectedBlockSizeBytes=size, reflectedBlockPaddedSizeBytes=padded)
        if set(result) != set(layouts):
            raise RuntimeError("SPIRV-BUFFER-REFLECTION-BINDING-COVERAGE")
        return result

    @classmethod
    def _validate_block_tree(cls, root, block, facts, variable_id):
        blocks, types = root.get("all_block_variables"), root.get("all_type_descriptions")
        if not isinstance(blocks, list) or not isinstance(types, list) or any(
                not isinstance(item, dict) for item in blocks + types):
            raise RuntimeError("SPIRV-BUFFER-REFLECTION-ALIASES")
        _, variable = facts.definition(variable_id, 59)
        _, pointer = facts.definition(variable[0], 32, 3)
        work, active, completed = [(block, pointer[2], True, False)], set(), set()
        while work:
            current, expected_type, descriptor_root, finish = work.pop()
            identity = id(current)
            key = (identity, expected_type, descriptor_root)
            if finish:
                active.remove(identity)
                completed.add(key)
                continue
            if key in completed:
                continue
            if identity in active:
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-CYCLIC-MEMBER")
            active.add(identity)
            if not any(current is item for item in blocks):
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-ALIASES")
            description = current.get("type_description")
            if not any(description is item for item in types) or (
                    cls.integer(description, "id") != expected_type):
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-TYPE")
            opcode, values = facts.definition(expected_type)
            if cls.integer(description, "op") != opcode:
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-TYPE")
            dimensions, array_type = [], expected_type
            while facts.definition(array_type)[0] in (28, 29):
                array_opcode, array_values = facts.definition(array_type)
                dimensions.append(facts.array_length(array_values[2]) if array_opcode == 28 else 0)
                array_type = array_values[1]
            traits = description.get("traits")
            array = traits.get("array") if isinstance(traits, dict) else None
            if not isinstance(array, dict) or not isinstance(array.get("dims"), list) or any(
                    type(value) is not int for value in array["dims"]) or array["dims"] != dimensions or (
                    cls.integer(array, "dims_count") != len(dimensions)):
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-ARRAY")
            if dimensions:
                stride = 0 if descriptor_root else facts.decoration(expected_type, 6)
                if cls.integer(array, "stride") != stride:
                    raise RuntimeError("SPIRV-BUFFER-REFLECTION-STRIDE")
            opcode, values = facts.definition(array_type)
            children, type_children = cls.members(current), cls.members(description)
            expected_children = list(values[1:]) if opcode == 30 else []
            if len(children) != len(expected_children) or len(type_children) != len(children):
                raise RuntimeError("SPIRV-BUFFER-REFLECTION-MEMBERS")
            work.append((current, expected_type, descriptor_root, True))
            for index, (child, child_type) in enumerate(zip(children, expected_children)):
                if not isinstance(child, dict) or child.get("type_description") is not type_children[index] or (
                        cls.integer(child, "offset") != facts.member_decoration(array_type, index, 35)):
                    raise RuntimeError("SPIRV-BUFFER-REFLECTION-MEMBER-MISMATCH")
                work.append((child, child_type, False, False))


def reflect_buffer_declaration_layouts(spirv_reflect_exe, spv_path):
    return BufferDeclarationLayoutReflector.parse(_run_spirv_reflect(spirv_reflect_exe, spv_path), spv_path)


def reflect_single_entry_vertex_inputs(spirv_reflect_exe, spv_path, entry, stage):
    """VertexInputInterface/1: selected target inputs, never source heuristics."""
    import yaml
    node = yaml.safe_load(_run_spirv_reflect(spirv_reflect_exe, spv_path))
    module = node.get("module", {})
    masks = {"Vertex": 1, "Fragment": 0x10, "Compute": 0x20}
    if module.get("entry_point_name") != entry or module.get("shader_stage") != masks.get(stage):
        raise RuntimeError("SHADER-VERTEX-INPUT-ENTRY-MISMATCH")
    if stage != "Vertex":
        return []
    # Vulkan 1.x format enum values; only the existing L2 float formats.
    formats = {100: "Float", 103: "Float2", 106: "Float3", 109: "Float4"}
    variables = module.get("input_variables") or []
    count = str(module.get("input_variable_count", "")).removesuffix(",")
    if not count.isdecimal() or int(count) != len(variables):
        raise RuntimeError("SHADER-VERTEX-INPUT-COUNT-MISMATCH")
    result = []
    locations = set()
    for variable in variables:
        if variable.get("storage_class") != 1:
            raise RuntimeError("SHADER-VERTEX-INPUT-STORAGE-INVALID")
        if variable.get("built_in") != -1:
            continue
        location = variable.get("location")
        name = variable.get("name")
        if not isinstance(location, int) or not 0 <= location < 0xffffffff or not name:
            raise RuntimeError("SHADER-VERTEX-INPUT-LOCATION-INVALID")
        if location in locations:
            raise RuntimeError("SHADER-VERTEX-INPUT-LOCATION-DUPLICATE")
        if (variable.get("member_count") != 0 or variable.get("array", {}).get("dims_count") != 0
                or variable.get("format") not in formats):
            raise RuntimeError("SHADER-VERTEX-INPUT-TYPE-UNSUPPORTED")
        locations.add(location)
        result.append({"name": name, "semantic": f"LOCATION{location}",
                       "format": formats[variable["format"]], "location": location})
    return sorted(result, key=lambda attribute: attribute["location"])


def _parse_reflection_integer(value: str) -> Optional[int]:
    """Parse a scalar emitted by spirv-reflect's YAML serializer."""
    if value is None:
        return None
    token = value.split(" #", 1)[0].strip().strip('"\'')
    if not token:
        return None
    try:
        return int(token, 0)
    except ValueError:
        return None


def _reflection_stage_mask(value: str) -> int:
    """Convert SPIR-V/Vulkan shader-stage metadata to the RHI bit layout.

    SPIRV-Reflect serializes ``SpvReflectShaderStageFlagBits`` as Vulkan
    stage flags in current SDKs.  A few older serializers emit names instead,
    so both forms are accepted.  Unknown bits are rejected by returning zero;
    the artifact writer then fails closed instead of dropping a stage.
    """
    numeric = _parse_reflection_integer(value)
    if numeric is not None:
        # VkShaderStageFlagBits -> arp::rhi::ShaderStage. Geometry and ray
        # stages have no RHI representation and therefore invalidate metadata.
        known_vulkan = 0x1 | 0x2 | 0x4 | 0x10 | 0x20
        if numeric == 0 or numeric & ~known_vulkan:
            return 0
        result = 0
        if numeric & 0x1:   # VK_SHADER_STAGE_VERTEX_BIT
            result |= 1 << 0
        if numeric & 0x10:  # VK_SHADER_STAGE_FRAGMENT_BIT
            result |= 1 << 1
        if numeric & 0x20:  # VK_SHADER_STAGE_COMPUTE_BIT
            result |= 1 << 2
        if numeric & 0x2:   # VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT
            result |= 1 << 3
        if numeric & 0x4:   # VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT
            result |= 1 << 4
        return result

    result = 0
    names = re.split(r"[|,\s]+", value.strip().strip("[]{}").lower())
    for name in names:
        if not name:
            continue
        if name in {"vertex", "vertexbit", "vs"}:
            result |= 1 << 0
        elif name in {"fragment", "fragmentbit", "pixel", "fs"}:
            result |= 1 << 1
        elif name in {"compute", "computebit", "cs"}:
            result |= 1 << 2
        elif name in {"tessellationcontrol", "tessellation_control", "tesc"}:
            result |= 1 << 3
        elif name in {"tessellationevaluation", "tessellation_evaluation", "tese"}:
            result |= 1 << 4
        else:
            return 0
    return result


def _parse_reflection_sequence(
    yaml_text: str,
    section_name: str,
) -> List[dict]:
    """Parse scalar fields from a top-level SPIRV-Reflect YAML sequence.

    The tool's YAML uses anchors and aliases, but descriptor entries themselves
    are regular mappings.  This intentionally parses only the fields needed by
    the contract and does not attempt to become a general YAML implementation.
    """
    lines = yaml_text.splitlines()
    start = None
    for index, line in enumerate(lines):
        if line == f"{section_name}:":
            start = index + 1
            break
    if start is None:
        return []

    entries: List[dict] = []
    current: Optional[dict] = None
    entry_indent: Optional[int] = None
    for line in lines[start:]:
        if line and not line[0].isspace() and line.rstrip().endswith(":"):
            break
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip())
        stripped = line.strip()
        if stripped.startswith("-") and (entry_indent is None or indent == entry_indent):
            if current is not None:
                entries.append(current)
            current = {}
            entry_indent = indent
            anchor = re.search(r"&([A-Za-z_][A-Za-z0-9_]*)", stripped)
            if anchor:
                current["_anchor"] = anchor.group(1)
            continue
        if current is None or ":" not in stripped:
            continue
        key, _, value = stripped.partition(":")
        key = key.strip()
        if key in {"set", "binding", "stages", "shader_stage", "stage"}:
            current[key] = value.strip()
        if key in {"descriptor_type", "array"} and indent == entry_indent + 2:
            current[key] = value.strip()
    if current is not None:
        entries.append(current)
    return entries


def _parse_module_descriptor_stage_masks(
    yaml_text: str,
    anchors: Dict[str, Tuple[int, int]],
) -> Dict[Tuple[int, int], int]:
    """Read descriptor aliases and stage from current SPIRV-Reflect YAML.

    Recent SPIRV-Reflect versions put the descriptor definitions under
    ``all_descriptor_bindings`` and expose the module-local descriptor list as
    aliases under ``module.descriptor_bindings``.  The module list is the
    authoritative relationship between a descriptor and the emitted SPIR-V
    stage; the all-descriptor union alone is deliberately insufficient.
    """
    lines = yaml_text.splitlines()
    module_start = next(
        (index + 1 for index, line in enumerate(lines) if line == "module:"),
        None,
    )
    if module_start is None:
        return {}

    masks: Dict[Tuple[int, int], int] = {}
    current_stage = 0
    in_descriptor_list = False
    for line in lines[module_start:]:
        if line and not line[0].isspace() and line.rstrip().endswith(":"):
            break
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("shader_stage:"):
            _, _, raw_stage = stripped.partition(":")
            current_stage = _reflection_stage_mask(raw_stage.strip())
            continue
        if stripped.startswith("descriptor_bindings:"):
            in_descriptor_list = True
            aliases = re.findall(r"\*([A-Za-z_][A-Za-z0-9_]*)", stripped)
            for alias in aliases:
                key = anchors.get(alias)
                if key is not None and current_stage:
                    masks[key] = masks.get(key, 0) | current_stage
            continue
        if in_descriptor_list:
            alias = re.search(r"\*([A-Za-z_][A-Za-z0-9_]*)", stripped)
            if alias:
                key = anchors.get(alias.group(1))
                if key is not None and current_stage:
                    masks[key] = masks.get(key, 0) | current_stage
                continue
            if ":" in stripped and not stripped.startswith("-"):
                in_descriptor_list = False

    return masks


def reflect_buffer_array_descriptors(spirv_reflect_exe: str, spv_path: str) -> Dict[Tuple[int, int], int]:
    """Exact SPIRV-Reflect buffer descriptor cardinality for target policy gates."""
    text = _run_spirv_reflect(spirv_reflect_exe, spv_path)
    descriptors = _parse_reflection_sequence(text, "all_descriptor_bindings")
    if not descriptors:
        descriptors = _parse_reflection_sequence(text, "descriptor_bindings")
    result = {}
    for descriptor in descriptors:
        if _parse_reflection_integer(descriptor.get("descriptor_type", "")) not in (6, 7):
            continue  # VkDescriptorType UniformBuffer / StorageBuffer
        array = descriptor.get("array", "")
        match = re.search(r"dims_count:\s*(\d+),\s*dims:\s*\[([^]]*)\]", array)
        if not match:
            raise RuntimeError(f"{spv_path}: buffer descriptor array metadata missing")
        dims = [int(value.strip()) for value in match.group(2).split(",") if value.strip()]
        if len(dims) != int(match.group(1)) or any(dim <= 0 for dim in dims):
            raise RuntimeError(f"{spv_path}: buffer descriptor array metadata invalid")
        count = 1
        for dim in dims:
            count *= dim
        if count <= 1:
            continue
        key = (_parse_reflection_integer(descriptor.get("set", "")),
               _parse_reflection_integer(descriptor.get("binding", "")))
        if None in key or key in result:
            raise RuntimeError(f"{spv_path}: buffer descriptor array key invalid")
        result[key] = count
    return result


def reflect_descriptor_stage_masks(
    spirv_reflect_exe: str,
    spv_path: str,
    allow_empty: bool = False,
) -> Dict[Tuple[int, int], int]:
    """Return authoritative descriptor visibility from SPIRV-Reflect.

    Visibility is intentionally read from the target SPIR-V, never inferred
    from source names or entry-point tables.  If an SDK omits per-descriptor
    stage fields, entry-point descriptor aliases are used as the equivalent
    authoritative relationship.  Missing/ambiguous metadata is a hard error.
    """
    yaml_text = _run_spirv_reflect(spirv_reflect_exe, spv_path)
    descriptors = _parse_reflection_sequence(yaml_text, "descriptor_bindings")
    if not descriptors:
        # Current SPIRV-Reflect emits a module-local alias list plus a
        # top-level descriptor union. Keep accepting the older top-level
        # descriptor_bindings form above for SDK compatibility.
        descriptors = _parse_reflection_sequence(
            yaml_text, "all_descriptor_bindings")
    if not descriptors:
        if allow_empty:
            return {}
        raise RuntimeError(
            f"{spv_path}: SPIRV-Reflect did not emit descriptor binding metadata")

    masks: Dict[Tuple[int, int], int] = {}
    anchors: Dict[str, Tuple[int, int]] = {}
    for descriptor in descriptors:
        set_value = _parse_reflection_integer(descriptor.get("set", ""))
        binding_value = _parse_reflection_integer(descriptor.get("binding", ""))
        if set_value is None or binding_value is None:
            raise RuntimeError(
                f"{spv_path}: reflected descriptor is missing set/binding metadata")
        key = (set_value, binding_value)
        stage_value = descriptor.get(
            "stages", descriptor.get("shader_stage", descriptor.get("stage", "")))
        mask = _reflection_stage_mask(stage_value)
        if mask:
            previous = masks.get(key, 0)
            if previous and previous != mask:
                mask |= previous
            masks[key] = mask
        anchor = descriptor.get("_anchor")
        if anchor:
            anchors[anchor] = key

    # In the current YAML layout the module-local aliases carry the only
    # trustworthy stage relationship. Do not assign the module stage to the
    # entire all-descriptor union: descriptors used by another entry point
    # must remain absent and fail closed if this SPIR-V does not expose them.
    for key, mask in _parse_module_descriptor_stage_masks(yaml_text, anchors).items():
        masks[key] = masks.get(key, 0) | mask

    # Older SPIRV-Reflect YAML versions expose visibility through entry-point
    # descriptor aliases instead of a per-binding ``stages`` field.
    entry_points = _parse_reflection_sequence(yaml_text, "entry_points")
    if any(key not in masks for key in anchors.values()) and entry_points:
        lines = yaml_text.splitlines()
        section_start = next(
            (index + 1 for index, line in enumerate(lines)
             if line == "entry_points:"), None)
        if section_start is not None:
            current_stage = 0
            in_descriptor_list = False
            for line in lines[section_start:]:
                if line and not line[0].isspace() and line.rstrip().endswith(":"):
                    break
                stripped = line.strip()
                if stripped.startswith("-") and "&" in stripped:
                    current_stage = 0
                    in_descriptor_list = False
                if "shader_stage:" in stripped or re.match(r"stage:", stripped):
                    _, _, raw_stage = stripped.partition(":")
                    current_stage = _reflection_stage_mask(raw_stage.strip())
                if stripped.startswith("descriptor_bindings:"):
                    in_descriptor_list = True
                    for alias in re.findall(
                            r"\*([A-Za-z_][A-Za-z0-9_]*)", stripped):
                        if current_stage:
                            key = anchors.get(alias)
                            if key is not None:
                                masks[key] = masks.get(key, 0) | current_stage
                    continue
                if in_descriptor_list:
                    alias = re.search(r"\*([A-Za-z_][A-Za-z0-9_]*)", stripped)
                    if alias and current_stage:
                        key = anchors.get(alias.group(1))
                        if key is not None:
                            masks[key] = masks.get(key, 0) | current_stage
                    if stripped and not stripped.startswith("-") \
                            and ":" in stripped and not stripped.startswith("descriptor_bindings:"):
                        in_descriptor_list = False

    return masks


def apply_descriptor_stage_masks(
    logical_bindings: List[dict],
    stage_masks: Dict[Tuple[int, int], int],
    spv_path: str,
    allow_inactive: bool = False,
) -> None:
    """Attach target-reflected visibility to source-discovered logical bindings.

    A source declaration that is absent from every selected executable entry
    point is a valid inactive declaration, but it is not part of the
    executable reflection interface.  Keep its zero mask explicit for
    target-specific lowering, while callers that do not model inactive
    declarations continue to fail closed by default.
    """
    for binding in logical_bindings:
        key = (int(binding["set"]), int(binding["binding"]))
        mask = stage_masks.get(key, 0)
        if mask == 0 and not allow_inactive:
            raise RuntimeError(
                f"{spv_path}: logical descriptor {key} has no authoritative stage metadata")
        binding["stageMask"] = mask


def _parse_block_variables(yaml_text: str) -> List[dict]:
    """Parse the ``all_block_variables:`` sequence from spirv-reflect YAML.

    Returns a list of dicts keyed by the scalar leaf fields we care about. We
    intentionally avoid a full YAML parser dependency: the spirv-reflect output
    is highly regular, and we only need flat scalar fields plus the ``members``
    anchor references (which we resolve separately by index).
    """
    # Slice the all_block_variables block: from the header line to the next
    # top-level key (a line starting at column 0 with a bare identifier and
    # a trailing colon).
    lines = yaml_text.splitlines()
    start = None
    end = len(lines)
    for i, ln in enumerate(lines):
        if ln.startswith("all_block_variables:"):
            start = i + 1
            break
    if start is None:
        return []
    for j in range(start, len(lines)):
        ln = lines[j]
        if ln and not ln[0].isspace() and not ln.startswith("#") and ln.rstrip().endswith(":"):
            end = j
            break
    block_lines = lines[start:end]

    # Each block variable entry begins with "  - &bvN" or "  - *bvN" (anchor/alias)
    # or just "  - " for entries without anchors. We parse the *defined* entries
    # (those with a body), collecting scalar fields. Alias-only entries
    # ("- *bvN") have no body and are resolved later by index.
    entries: List[dict] = []
    aliases: List[Optional[int]] = []  # parallel list; None for defined entries
    cur: Optional[dict] = None
    cur_members: List[str] = []
    anchor_re = re.compile(r"^\s*-\s+&bv(\d+)\s*$")
    alias_re = re.compile(r"^\s*-\s+\*bv(\d+)\s*(?:#\s*.*)?$")
    bare_re = re.compile(r"^\s*-\s+(\S.*)$")

    def _flush():
        nonlocal cur, cur_members
        if cur is not None:
            cur["members_raw"] = cur_members
            entries.append(cur)
        cur = None
        cur_members = []

    for ln in block_lines:
        if not ln.strip():
            continue
        m_anchor = anchor_re.match(ln)
        m_alias = alias_re.match(ln)
        if m_anchor:
            _flush()
            cur = {"_anchor": int(m_anchor.group(1))}
            aliases.append(None)
        elif m_alias:
            # This is a member-reference entry inside a parent's members list,
            # OR a top-level alias. Inside members: we capture the alias index.
            if cur is not None and ln.startswith("      - "):
                cur_members.append(m_alias.group(1))
            else:
                _flush()
                aliases.append(int(m_alias.group(1)))
        elif bare_re.match(ln) and cur is not None and not ln.lstrip().startswith("name:"):
            # A nested "- ..." line under members that isn't an alias; skip
            # (we only follow anchor/alias references).
            pass
        elif cur is not None:
            # A key: value line within the current entry. Detect 2-space vs
            # 4-space indentation to distinguish entry fields from nested
            # numeric/image/array sub-mappings.
            stripped = ln.strip()
            if stripped.startswith("#") or not stripped:
                continue
            # Parse "key: value" or "key:" (mapping start).
            if ":" in stripped:
                key, _, val = stripped.partition(":")
                key = key.strip()
                val = val.strip()
                # Strip trailing YAML comment.
                if " #" in val:
                    val = val.split(" #", 1)[0].rstrip()
                if val == "":
                    continue  # nested mapping start (numeric/image/array); ignore
                # Strip surrounding quotes.
                if len(val) >= 2 and val[0] == '"' and val[-1] == '"':
                    val = val[1:-1]
                elif len(val) >= 2 and val[0] == "'" and val[-1] == "'":
                    val = val[1:-1]
                cur[key] = val

    _flush()

    # Resolve members_raw alias indices to entry anchors.
    anchor_index = {}
    for idx, e in enumerate(entries):
        if "_anchor" in e:
            anchor_index[e["_anchor"]] = idx

    resolved: List[dict] = []
    for e in entries:
        members_idx = []
        for ref in e.get("members_raw", []):
            try:
                members_idx.append(anchor_index[int(ref)])
            except (KeyError, ValueError):
                pass
        e["members"] = members_idx
        resolved.append(e)
    return resolved


def _to_int(val: str, default: int = 0) -> int:
    if not val:
        return default
    val = val.strip()
    try:
        return int(val, 0)  # handles 0x... and decimal
    except ValueError:
        return default


def _parse_type_descriptions(yaml_text: str) -> Dict[int, dict]:
    """Parse the ``all_type_descriptions:`` sequence from spirv-reflect YAML.

    Returns a dict keyed by type-description anchor index (the N in ``&tdN``)
    to a dict of the type's scalar fields. We only need ``type_flags`` to
    distinguish float scalars from int/uint scalars; other fields are
    captured for completeness.

    This runs at verbosity 1 (the codegen already invokes spirv-reflect
    with ``-v 1``), which is required to emit the type_description section
    and the ``type_description: *tdN`` back-references on block variables.
    Without type_flags, scalar signedness alone cannot distinguish ``uint``
    (signedness=0) from ``float`` (signedness=0); both report the same
    signedness, so the codegen would emit ``float`` for ``uint`` fields
    and bit-reinterpret the value when the C++ side writes 1.0f and the
    shader reads uint 0x3F800000.
    """
    lines = yaml_text.splitlines()
    start = None
    end = len(lines)
    for i, ln in enumerate(lines):
        if ln.startswith("all_type_descriptions:"):
            start = i + 1
            break
    if start is None:
        return {}
    for j in range(start, len(lines)):
        ln = lines[j]
        if ln and not ln[0].isspace() and not ln.startswith("#") and ln.rstrip().endswith(":"):
            end = j
            break
    td_lines = lines[start:end]

    # Each type description entry begins with "  - &tdN".
    entries: Dict[int, dict] = {}
    cur: Optional[Dict[str, Any]] = None
    cur_anchor: Optional[int] = None
    anchor_re = re.compile(r"^\s*-\s+&td(\d+)\s*$")
    for ln in td_lines:
        if not ln.strip():
            continue
        m = anchor_re.match(ln)
        if m:
            if cur is not None and cur_anchor is not None:
                entries[cur_anchor] = cur
            cur_anchor = int(m.group(1))
            cur = {}
            continue
        if cur is None:
            continue
        stripped = ln.strip()
        if stripped.startswith("#") or not stripped:
            continue
        if ":" in stripped:
            key, _, val = stripped.partition(":")
            key = key.strip()
            val = val.strip()
            if " #" in val:
                val = val.split(" #", 1)[0].rstrip()
            if val == "":
                continue
            cur[key] = val
    if cur is not None and cur_anchor is not None:
        entries[cur_anchor] = cur
    return entries


def _resolve_type_flags(entry: dict, type_descs: Dict[int, dict]) -> int:
    """Resolve the ``type_description: *tdN`` reference on a block variable.

    Returns the type_flags integer (0 if the reference is absent or the
    type description was not parsed). Only the INT (0x4) and FLOAT (0x8)
    bits are meaningful for scalar members; vector/matrix members carry
    additional VECTOR/MATRIX bits but the codegen treats them as float
    arrays regardless (matrix layout is governed by decorations).
    """
    ref = entry.get("type_description", "")
    if not ref or not ref.startswith("*td"):
        return 0
    try:
        td_idx = int(ref[3:])
    except ValueError:
        return 0
    td = type_descs.get(td_idx)
    if td is None:
        return 0
    return _to_int(td.get("type_flags", "0"), 0)


def _build_block(entry: dict, all_entries: List[dict],
                 type_descs: Optional[Dict[int, dict]] = None) -> PushConstantBlock:
    block = PushConstantBlock(
        instance_name=entry.get("name", ""),
        type_name="",  # filled from type_description anchor; leave blank if absent
        size=_to_int(entry.get("size", "0")),
    )
    for midx in entry.get("members", []):
        m = all_entries[midx]
        member = PushConstantMember(
            name=m.get("name", ""),
            offset=_to_int(m.get("offset", "0")),
            size=_to_int(m.get("size", "0")),
            decorations=_to_int(m.get("decorations", "0")),
        )
        if type_descs is not None:
            member.type_flags = _resolve_type_flags(m, type_descs)
        # numeric.matrix / numeric.vector / numeric.scalar are nested mappings
        # we did not capture above; re-scan raw text is not available here.
        # Instead infer matrix/vector from decorations + size heuristics is
        # unreliable, so we re-parse the raw YAML slice for numeric info.
        member.is_matrix = bool(member.decorations & (_SPV_REFLECT_DECORATION_ROW_MAJOR | _SPV_REFLECT_DECORATION_COL_MAJOR))
        # size-based vector inference: 16 bytes => float4, 12 => float3, 8 => float2.
        # Only valid for 32-bit scalars; matrix members have size = rows*cols*4.
        if not member.is_matrix and member.size in (4, 8, 12, 16):
            member.is_vector = True
            member.vec_component_count = member.size // 4
        # Slang's explicit row-major lowering transposes the SPIR-V matrix
        # representation so row-major CPU bytes preserve M * v semantics.
        if member.is_matrix:
            if member.decorations & _SPV_REFLECT_DECORATION_ROW_MAJOR:
                raise RuntimeError(
                    f"shader layout contract violation: member '{member.name}' in "
                    f"push-constant block '{block.instance_name}' has RowMajor "
                    f"decoration; expected ColMajor from explicit row-major compilation."
                )
            if not (member.decorations & _SPV_REFLECT_DECORATION_COL_MAJOR):
                raise RuntimeError(
                    f"shader layout contract violation: matrix member '{member.name}' "
                    f"has neither RowMajor nor ColMajor decoration."
                )
        block.members.append(member)
    return block


def _reparse_numeric(yaml_text: str, all_entries: List[dict]) -> None:
    """Second pass: walk the raw YAML text for each block-variable entry and
    fill in numeric.scalar/vector/matrix fields by locating the entry body.

    The flat parse in _parse_block_variables skips nested mappings (numeric:
    is a mapping). We re-scan the original text to recover matrix dimensions
    and stride, which are needed for codegen (float4x4 -> float[16], etc.).
    """
    lines = yaml_text.splitlines()
    # Build a list of (anchor_idx, start_line, end_line) for each defined entry.
    entry_spans: List[Optional[tuple]] = [None] * len(all_entries)
    anchor_re = re.compile(r"^\s*-\s+&bv(\d+)\s*$")
    for i, ln in enumerate(lines):
        m = anchor_re.match(ln)
        if m:
            aidx = int(m.group(1))
            # find this anchor in all_entries
            for eidx, e in enumerate(all_entries):
                if e.get("_anchor") == aidx:
                    # end = next anchor/alias at same indent or next top-level key
                    end = len(lines)
                    for j in range(i + 1, len(lines)):
                        inner = lines[j]
                        if inner.startswith("  - &bv") or inner.startswith("  - *bv"):
                            end = j
                            break
                        if inner and not inner[0].isspace() and not inner.startswith("#"):
                            end = j
                            break
                    entry_spans[eidx] = (i, end)
                    break

    for eidx, span in enumerate(entry_spans):
        if span is None:
            continue
        start, end = span
        body = "\n".join(lines[start:end])
        e = all_entries[eidx]
        # matrix
        m = re.search(r"matrix:\s*\{\s*column_count:\s*(\d+),\s*row_count:\s*(\d+),\s*stride:\s*(\d+)\s*\}", body)
        if m:
            e["_matrix_column_count"] = int(m.group(1))
            e["_matrix_row_count"] = int(m.group(2))
            e["_matrix_stride"] = int(m.group(3))
        # vector
        m = re.search(r"vector:\s*\{\s*component_count:\s*(\d+)\s*\}", body)
        if m:
            e["_vec_component_count"] = int(m.group(1))
        # scalar
        m = re.search(r"scalar:\s*\{\s*width:\s*(\d+),\s*signedness:\s*(\d+)\s*\}", body)
        if m:
            e["_scalar_width"] = int(m.group(1))
            e["_scalar_signed"] = int(m.group(2))
        # array
        m = re.search(r"array:\s*\{\s*dims_count:\s*(\d+),\s*dims:\s*\[([^\]]*)\],\s*stride:\s*(\d+)\s*\}", body)
        if m:
            e["_array_dims_count"] = int(m.group(1))
            dims_raw = m.group(2).strip()
            e["_array_dims"] = [int(x) for x in dims_raw.split(",") if x.strip().isdigit()] if dims_raw else []
            e["_array_stride"] = int(m.group(3))


def _normalize_transparent_push_members(all_entries, type_descs, member_indices, layout_facts):
    # Slang can represent an authored matrix array as a transparent struct
    # containing one full-range array member. Follow explicit reflected member
    # links only; do not infer type or stride from the authored field's size.
    for midx in member_indices:
        outer = all_entries[midx]
        current = outer
        visited = set()
        while current.get("members"):
            if id(current) in visited or len(current["members"]) != 1:
                raise RuntimeError("shader layout contract: unsupported nested push member")
            visited.add(id(current))
            child = all_entries[current["members"][0]]
            if _to_int(child.get("offset")) != 0 or child.get("size") != current.get("size") or current.get("_array_dims"):
                raise RuntimeError("shader layout contract: non-transparent push member wrapper")
            if child.get("_matrix_row_count"):
                ref = current.get("type_description", "")
                if not ref.startswith("*td") or int(ref[3:]) not in type_descs:
                    raise RuntimeError("shader layout contract: wrapper type identity missing")
                type_id = _to_int(type_descs[int(ref[3:])].get("id"))
                stride = layout_facts.matrix_stride(type_id, 0)
                if child.get("_matrix_stride", 0) not in (0, stride):
                    raise RuntimeError("shader layout contract: reflected matrix stride conflicts with SPIR-V")
                child = dict(child)
                child["_matrix_stride"] = stride
            current = child
        if current is not outer:
            name, offset = outer["name"], outer["offset"]
            all_entries[midx] = dict(current, name=name, offset=offset)


def reflect_push_constant_block(spirv_reflect_exe: str, spv_path: str) -> Optional[PushConstantBlock]:
    """Reflect the push-constant block of a SPIR-V binary.

    Returns None if the shader has no push-constant block.
    """
    yaml_text = _run_spirv_reflect(spirv_reflect_exe, spv_path)
    all_entries = _parse_block_variables(yaml_text)
    _reparse_numeric(yaml_text, all_entries)
    type_descs = _parse_type_descriptions(yaml_text)

    # Find the push-constant block: it is the block variable referenced under
    # ``module.push_constants``. We locate it by scanning for the
    # ``push_constants:`` key and reading the following alias reference.
    pc_alias_idx: Optional[int] = None
    lines = yaml_text.splitlines()
    for i, ln in enumerate(lines):
        s = ln.strip()
        if s.startswith("push_constants:"):
            # Next non-empty line should be "    - *bvN"
            for j in range(i + 1, min(i + 4, len(lines))):
                inner = lines[j].strip()
                m = re.match(r"-\s+\*bv(\d+)", inner)
                if m:
                    pc_alias_idx = int(m.group(1))
                    break
            break
    if pc_alias_idx is None:
        return None

    # Map alias idx -> entry index.
    target_eidx = None
    for eidx, e in enumerate(all_entries):
        if e.get("_anchor") == pc_alias_idx:
            target_eidx = eidx
            break
    if target_eidx is None:
        return None

    from spirv_layout import SpirvLayoutFacts
    _normalize_transparent_push_members(all_entries, type_descs,
        all_entries[target_eidx].get("members", []), SpirvLayoutFacts(spv_path))

    block = _build_block(all_entries[target_eidx], all_entries, type_descs)
    # Enrich members with numeric info from the second pass.
    for member, midx in zip(block.members, all_entries[target_eidx].get("members", [])):
        me = all_entries[midx]
        member.scalar_width = int(me.get("_scalar_width", 32))
        member.scalar_signed = int(me.get("_scalar_signed", 0))
        member.vec_component_count = int(me.get("_vec_component_count", 0))
        member.matrix_column_count = int(me.get("_matrix_column_count", 0))
        member.matrix_row_count = int(me.get("_matrix_row_count", 0))
        member.matrix_stride = int(me.get("_matrix_stride", 0))
        member.array_dims = list(me.get("_array_dims", []))
        member.array_stride = int(me.get("_array_stride", 0))
        member.is_matrix = bool(member.decorations & (_SPV_REFLECT_DECORATION_ROW_MAJOR | _SPV_REFLECT_DECORATION_COL_MAJOR))
        member.is_vector = (member.vec_component_count > 0) and not member.is_matrix
    return block


# ---------------------------------------------------------------------------
# C++ header emission
# ---------------------------------------------------------------------------

def _cpp_array_suffix(member: PushConstantMember) -> str:
    """Return the C++ array suffix for a member, e.g. ``[16]`` or ``[4]``."""
    elements = member.matrix_row_count * member.matrix_column_count if member.is_matrix else member.vec_component_count if member.is_vector else 1
    if member.array_dims:
        # Multi-dim arrays are rare in push constants; emit the product.
        total = elements
        for d in member.array_dims:
            total *= d
        return f"[{total}]"
    return f"[{elements}]" if member.is_matrix or member.is_vector else ""


def _cpp_field_comment(member: PushConstantMember) -> str:
    parts = [f"offset {member.offset}", f"size {member.size}"]
    if member.is_matrix:
        parts.append(f"RowMajor stride {member.matrix_stride}")
    elif member.is_vector:
        parts.append(f"vec{member.vec_component_count}")
    return ", ".join(parts)


def _cpp_scalar_type(member: PushConstantMember) -> str:
    """Return the C++ scalar type for a push-constant member's element.

    Matrix and vector members are always emitted as ``float`` arrays (the
    engine only produces 32-bit float matrices/vectors, and the matrix
    layout is governed by SPIR-V decorations, not the element type). For
    scalar members, the SPIR-V ``type_flags`` from the member's
    type_description distinguishes:

      - FLOAT (0x8): 32-bit float -> ``float``
      - INT (0x4) with signedness=1: 32-bit signed int -> ``std::int32_t``
      - INT (0x4) with signedness=0: 32-bit unsigned int -> ``std::uint32_t``

    SPIRV-Reflect's ``numeric.scalar.signedness`` is 0 for BOTH ``float``
    and ``uint``; only ``type_flags`` disambiguates them. Without this
    check, a ``uint cascadeIndex`` field would be emitted as
    ``float cascadeIndex`` in C++, and writing ``1.0f`` would be read by
    the shader as ``uint 0x3F800000 = 1065353216`` (clamped to cascade 2).

    If ``type_flags`` is 0 (older reflection run without verbosity 1),
    fall back to ``float`` for backward compatibility - the struct size
    static_assert still catches layout drift, and no existing shader
    relied on int push-constant members before this fix.
    """
    # Matrix/vector members: always float (engine contract).
    if member.is_matrix or member.is_vector:
        return "float"
    # Array members: element type follows the same scalar rules, but the
    # engine only produces float arrays today (matrix arrays are float[16*N],
    # vector arrays are float[comp*N]). Keep float for arrays unless a future
    # int array shader appears (would need an explicit contract test).
    if member.array_dims:
        return "float"
    # Scalar member: use type_flags to pick the C++ type.
    if member.type_flags & _SPV_REFLECT_TYPE_FLAG_INT:
        if member.scalar_signed:
            return "std::int32_t"
        return "std::uint32_t"
    # Default (FLOAT flag set, or type_flags missing on old runs): float.
    return "float"


def _constant_name(struct_name: str, field_name: str, suffix: str) -> str:
    # CamelCase the field name: first char upper, rest preserved.
    cap = field_name[0].upper() + field_name[1:] if field_name else field_name
    return f"k{struct_name}{cap}{suffix}"


def emit_shader_bindings_header(
    block: PushConstantBlock,
    struct_name: str,
    source_basename: str,
    spv_basename: str,
) -> str:
    """Emit the C++ header text for a push-constant block.

    Args:
      block: reflected push-constant block.
      struct_name: the C++ struct name to emit (e.g. ``WoccSkyPushConstants``).
      source_basename: e.g. ``sky.slang`` (for the header comment).
      spv_basename: e.g. ``wocc_sky.spv`` (for the header comment).
    """
    out: List[str] = []
    out.append(f"// AUTO-GENERATED from {source_basename} by shader_bindings_codegen.py - DO NOT EDIT.")
    out.append(f"// Layout source of truth: SPIRV-Reflect on {spv_basename}.")
    out.append("// Matrix convention (AGENTS.md): C++ stores row-major bytes; shader declares")
    out.append("// default float4x4 with explicit row-major compile (-> SPV ColMajor).")
    out.append("#pragma once")
    out.append("#include <cstdint>")
    out.append("namespace arp::shader::gen {")
    out.append(f"struct {struct_name} {{")
    for m in block.members:
        suffix = _cpp_array_suffix(m)
        cpp_type = _cpp_scalar_type(m)
        out.append(f"    {cpp_type} {m.name}{suffix}{{}};          // {_cpp_field_comment(m)}")
    out.append("};")
    out.append(f'static_assert(sizeof({struct_name}) == {block.size}, "shader layout mismatch: {struct_name} vs {spv_basename}");')
    out.append(f"constexpr std::uint32_t k{struct_name}Size = {block.size};")
    for m in block.members:
        out.append(f"constexpr std::uint32_t {_constant_name(struct_name, m.name, 'Offset')} = {m.offset};")
    out.append("} // namespace arp::shader::gen")
    out.append("")
    return "\n".join(out)


# ---------------------------------------------------------------------------
# YAML reflection member emission (appended to .reflection.yaml by caller)
# ---------------------------------------------------------------------------

def _yaml_scalar_type(member: PushConstantMember) -> str:
    """YAML scalarType string matching the C++ type from _cpp_scalar_type.

    The runtime reflection loader (shader_reflection_cache.cpp) reads
    scalarType to populate ShaderPushConstantMember.scalarType. Today the
    runtime only inspects the string for equality ("float" vs non-"float")
    in a few places, but emitting the correct type keeps the YAML
    consistent with the C++ header and lets future contract tests assert
    int vs uint vs float without re-running spirv-reflect.
    """
    if member.is_matrix or member.is_vector or member.array_dims:
        return "float"
    if member.type_flags & _SPV_REFLECT_TYPE_FLAG_INT:
        if member.scalar_signed:
            return "int32"
        return "uint32"
    return "float"


def emit_push_constant_members_yaml(block: PushConstantBlock, indent: str = "  ") -> str:
    """Emit the ``pushConstantMembers:`` YAML block for a reflection file."""
    if not block.members:
        return "pushConstantMembers: []\n"
    lines = ["pushConstantMembers:"]
    for m in block.members:
        lines.append(f"  - name: {_yaml_str(m.name)}")
        lines.append(f"    scalarType: {_yaml_scalar_type(m)}")
        lines.append(f"    scalarWidth: {m.scalar_width}")
        if m.is_matrix:
            lines.append(f"    matrixRows: {m.matrix_row_count}")
            lines.append(f"    matrixCols: {m.matrix_column_count}")
            lines.append(f"    matrixStride: {m.matrix_stride}")
            lines.append(f"    rowMajor: true")
        elif m.is_vector:
            lines.append(f"    vecSize: {m.vec_component_count}")
        if m.array_dims:
            lines.append(f"    arrayDims: [{', '.join(str(d) for d in m.array_dims)}]")
            lines.append(f"    arrayStride: {m.array_stride}")
        lines.append(f"    offset: {m.offset}")
        lines.append(f"    size: {m.size}")
        lines.append(f"    decorations: 0x{m.decorations:08x}")
    return "\n".join(lines) + "\n"


def _yaml_str(s: str) -> str:
    """Quote a YAML string scalar if it contains characters that need quoting."""
    if s and re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", s):
        return s
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


# ---------------------------------------------------------------------------
# CLI entry point (used by compile_shaders.py)
# ---------------------------------------------------------------------------

def generate_for_shader(
    spirv_reflect_exe: str,
    spv_path: str,
    header_out_path: str,
    struct_name: str,
    source_basename: str,
    spv_basename: str,
) -> PushConstantBlock:
    """Reflect a shader and write the .shader_bindings.h header.

    Returns the reflected PushConstantBlock so the caller can emit the
    ``pushConstantMembers`` section into the reflection YAML.

    Raises RuntimeError on any contract violation (e.g. ColMajor matrix) so
    the build fails fast rather than emitting a wrong-layout header.
    """
    block = reflect_push_constant_block(spirv_reflect_exe, spv_path)
    if block is None:
        raise RuntimeError(f"no push-constant block found in {spv_path}")
    header_text = emit_shader_bindings_header(block, struct_name, source_basename, spv_basename)
    os.makedirs(os.path.dirname(os.path.abspath(header_out_path)), exist_ok=True)
    with open(header_out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header_text)
    return block


# Map of shader prefix -> (C++ struct name). Only pilots listed here get a
# .shader_bindings.h generated; others are skipped with a log line. Expanding
# this map is how subsequent phases roll out codegen to more shaders.
#
# The C++ struct name does NOT need to match the Slang struct name: SPIRV-Reflect
# keys off the SPIR-V type name (which Slang mangles to "<SlangName>_std430"),
# so the emitted C++ struct can carry any name we choose. This lets two shaders
# that declare structurally-identical blocks (e.g. pbr_scene and
# pbr_scene_terrain both declare ``struct WoccPbrPushConstants``) get distinct
# C++ types (``WoccPbrPushConstants`` vs ``WoccPbrTerrainPushConstants``) so
# both can be included in the same TU without an ODR clash, and each keeps its
# own contract test.
PILOT_STRUCT_NAMES = {
    "wocc_sky": "WoccSkyPushConstants",
    "wocc_water": "WoccWaterPushConstants",
    "pbr_scene": "WoccPbrPushConstants",
    "pbr_scene_terrain": "WoccPbrTerrainPushConstants",
    "editor_viewport_pbr": "EditorViewportPbrPushConstants",
    "wocc_depth_prepass": "WoccDepthPrepassPushConstants",
    "shadow_depth": "WoccShadowDepthPushConstants",
    "wocc_post": "WoccPostPushConstants",
    "wocc_post_fx": "WoccPostFxPushConstants",
    "ddgi": "DdgiPushConstants",
    "ssr": "SsrPushConstants",
    "wocc_taa": "TaaPushConstants",
}


def main(argv: List[str]) -> int:
    import argparse
    parser = argparse.ArgumentParser(description="arp-engine shader binding codegen")
    parser.add_argument("--spirv-reflect", required=True, help="Path to spirv-reflect.exe")
    parser.add_argument("--spv", required=True, help="Path to compiled .spv")
    parser.add_argument("--prefix", required=True, help="Shader prefix (e.g. wocc_sky)")
    parser.add_argument("--out-dir", required=True, help="Output directory for .shader_bindings.h")
    parser.add_argument("--source-basename", required=True, help="Source .slang basename (e.g. sky.slang)")
    args = parser.parse_args(argv)

    if args.prefix not in PILOT_STRUCT_NAMES:
        print(f"[shader_bindings_codegen] skipping {args.prefix} (not in pilot set)", file=sys.stderr)
        return 0

    struct_name = PILOT_STRUCT_NAMES[args.prefix]
    spv_basename = os.path.basename(args.spv)
    header_path = os.path.join(args.out_dir, f"{args.prefix}.shader_bindings.h")
    block = generate_for_shader(
        args.spirv_reflect, args.spv, header_path,
        struct_name, args.source_basename, spv_basename,
    )
    print(f"[shader_bindings_codegen] wrote {header_path} "
          f"(struct {struct_name}, {len(block.members)} members, {block.size} bytes)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
