"""Production ES100 target artifact generation from original sealed stage bytes.

This is distinct from webgl_shader_codegen.py's ProbeOnly publication. Native
program creation, profile admission and draw execution remain backend work.
"""
from __future__ import annotations

import ctypes as C
import hashlib
from pathlib import Path
import subprocess

import yaml

from spirv_cross_api import SpirvCrossCompiler as Cross
from spirv_layout import SpirvLayoutFacts


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def file_role(path, parent):
    path = Path(path)
    if not path.is_file() or not path.stat().st_size:
        raise RuntimeError('SHADER-WEBGL1-FILE-EMPTY')
    return dict(path=path.relative_to(parent).as_posix(), sha256=digest(path))


class WebGlShaderTargetCompiler:
    """Versioned conservative subset; an unknown operation is a target refusal."""
    # SPIR-V core opcodes. These declarations/operations have an ES100 mapping.
    # No LoopMerge, atomics, bitwise, storage access, image fetch or runtime uint.
    ALLOWED = {0, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 14, 15, 16, 17,
        19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 32, 33,
        41, 42, 43, 44, 54, 55, 56, 57, 59, 61, 62, 65, 66, 71, 72,
        79, 80, 81, 82, 83, 84, 86, 87, 88, 109, 110, 111,
        124, 126, 127, 128, 129, 130, 131, 132, 133, 135, 136, 138,
        140, 141, 142, 143, 144, 145, 146, 147, 148, 154, 155, 156, 157,
        164, 165, 166, 167, 168, 169, 170, 171, 173, 175, 177, 179,
        180, 182, 184, 186, 188, 190, 207, 208, 209, 245, 247, 248,
        249, 250, 253, 254, 317, 330}
    UNTYPED = {0, 2, 3, 4, 5, 6, 7, 8, 10, 11, 14, 15, 16, 17,
        19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 32, 33,
        56, 62, 71, 72, 247, 248, 249, 250, 253, 254, 317, 330}

    def __init__(self, library, slangc, max_leaves=256):
        self.library = Path(library).resolve()
        self.slangc = Path(slangc).resolve()
        self.max_leaves = max_leaves
        if type(max_leaves) is not int or not 0 < max_leaves <= 65536:
            raise RuntimeError('SHADER-WEBGL1-LEAF-BUDGET')

    @classmethod
    def validate_binary(cls, facts, stage):
        if stage not in ('Vertex', 'Fragment'):
            raise RuntimeError('SHADER-WEBGL1-STAGE-UNSUPPORTED')
        uints, constants, definitions, glsl_sets = set(), set(), {}, set()
        glsl_literal = b'GLSL.std.450\0'
        glsl_literal += b'\0' * (-len(glsl_literal) % 4)
        for _, op, a in facts.instructions:
            if op == 11 and b''.join(word.to_bytes(4, 'little') for word in a[1:]) == glsl_literal:
                glsl_sets.add(a[0])
            if op in (59, 61):
                definitions[a[1]] = (op, a)
            if op == 21 and a[1:] != (32, 1):
                uints.add(a[0])
            if op == 23 and a[1] in uints:
                uints.add(a[0])
            if op == 43:
                constants.add(a[1])
        for _, op, a in facts.instructions:
            if op not in cls.ALLOWED:
                raise RuntimeError(f'SHADER-WEBGL1-OPCODE-UNSUPPORTED:{op}')
            if op not in cls.UNTYPED and op != 43 and a and a[0] in uints:
                raise RuntimeError('SHADER-WEBGL1-UINT-UNSUPPORTED')
            if op == 22 and a[1] != 32:
                raise RuntimeError('SHADER-WEBGL1-FLOAT-WIDTH')
            if op == 21 and a[1] != 32:
                raise RuntimeError('SHADER-WEBGL1-INT-WIDTH')
            if op in (65, 66) and any(index not in constants for index in a[3:]):
                raise RuntimeError('SHADER-WEBGL1-DYNAMIC-INDEX-UNPROVEN')
            if op == 71 and a[1] == 11 and a[2] not in (0, 17):
                # FragCoord needs a separate origin lowering; Position/FrontFacing do not.
                raise RuntimeError('SHADER-WEBGL1-BUILTIN-UNSUPPORTED')
            if op == 71 and a[1] in (13, 14, 16):
                raise RuntimeError('SHADER-WEBGL1-INTERPOLATION-UNSUPPORTED')
            if op == 59 and a[2] not in (0, 1, 2, 3, 6, 7, 9):
                raise RuntimeError('SHADER-WEBGL1-STORAGE-UNSUPPORTED')
            if op == 88 and (len(a) != 6 or a[4] != 2):
                raise RuntimeError('SHADER-WEBGL1-EXPLICIT-SAMPLE-OPERANDS')
            if op == 86:
                for identifier in a[2:4]:
                    load = definitions.get(identifier)
                    variable = definitions.get(load[1][2]) if load and load[0] == 61 else None
                    if not variable or variable[0] != 59 or variable[1][2] != 0:
                        raise RuntimeError('SHADER-WEBGL1-SAMPLER-DATAFLOW')
            if op == 208:
                # GL and L2 screen Y differ. Only sign-invariant abs(ddy) is
                # admitted until a versioned signed-derivative IR lowering exists.
                result = a[1]
                for _, consumer, operands in facts.instructions:
                    # Declaration/debug operands are not computational uses.
                    # OpExtInst is below 41 but its arguments are executable.
                    if (consumer < 54 and consumer != 12) or consumer in (54, 55, 56, 71, 72, 317, 330):
                        continue
                    first = 4 if consumer == 12 else 0 if consumer in cls.UNTYPED else 2
                    if result in operands[first:] and not (
                            consumer == 12 and len(operands) == 5 and operands[2] in glsl_sets
                            and operands[3] == 4 and operands[4] == result):
                        raise RuntimeError('SHADER-WEBGL1-SIGNED-DDY-UNPROVEN')

    @staticmethod
    def key(c, resource):
        return (c.call('get_decoration', resource.id, Cross.DESCRIPTOR_SET),
                c.call('get_decoration', resource.id, Cross.BINDING))

    @staticmethod
    def scalar_type(c, t):
        base, bits = c.type_value('basetype', t), c.type_value('bit_width', t)
        width, columns = c.type_value('vector_size', t), c.type_value('columns', t)
        if base not in (Cross.FP32, Cross.INT32) or bits != 32 or not 1 <= width <= 4:
            raise RuntimeError('SHADER-WEBGL1-UNIFORM-TYPE')
        if columns != 1 and (base != Cross.FP32 or columns != 4 or width != 4):
            raise RuntimeError('SHADER-WEBGL1-MATRIX-TYPE')
        return ('Float32' if base == Cross.FP32 else 'Int32'), width, columns

    def uniform_leaves(self, c, resource, kind):
        t = c.call('get_type_handle', resource.base_type_id)
        size = C.c_size_t()
        c.check(c.call('get_declared_struct_size', t, C.byref(size)))
        key = self.key(c, resource) if kind == 'Uniform' else (0, 0)
        root = f'arpUbo_{key[0]}_{key[1]}' if kind == 'Uniform' else 'arpPush'
        c.call('set_name', resource.id, root.encode())
        c.call('set_name', resource.base_type_id, (root + '_Type').encode())
        leaves = []

        def visit(type_id, name, offset, array_stride=0, matrix_stride=0, element=False, depth=0):
            if depth > 32:
                raise RuntimeError('SHADER-WEBGL1-TYPE-DEPTH')
            t = c.call('get_type_handle', type_id)
            dimensions = c.type_value('num_array_dimensions', t)
            if dimensions and not element:
                if dimensions != 1 or not c.api('type_array_dimension_is_literal')(t, 0):
                    raise RuntimeError('SHADER-WEBGL1-ARRAY-TYPE')
                count = c.type_value('array_dimension', t, 0)
                if not array_stride or not 0 < count <= self.max_leaves:
                    raise RuntimeError('SHADER-WEBGL1-ARRAY-LAYOUT')
                for i in range(count):
                    visit(type_id, f'{name}[{i}]', offset + i * array_stride,
                          matrix_stride=matrix_stride, element=True, depth=depth + 1)
            elif c.type_value('basetype', t) == Cross.STRUCT:
                base_id = c.type_value('base_type_id', t)
                for i in range(c.type_value('num_member_types', t)):
                    child = c.type_value('member_type', t, i)
                    child_type = c.call('get_type_handle', child)
                    c.call('set_member_name', base_id, i, f'm{i}'.encode())
                    at = offset + c.member_value('offset', t, i)
                    array = c.member_value('array_stride', t, i) if c.type_value('num_array_dimensions', child_type) else 0
                    matrix = 0
                    if c.type_value('columns', child_type) > 1:
                        if not c.call('has_member_decoration', base_id, i, Cross.COL_MAJOR):
                            raise RuntimeError('SHADER-WEBGL1-MATRIX-DECORATION')
                        matrix = c.member_value('matrix_stride', t, i)
                    visit(child, f'{name}.m{i}', at, array, matrix, depth=depth + 1)
            else:
                scalar, components, columns = self.scalar_type(c, t)
                if columns > 1 and matrix_stride != 16:
                    raise RuntimeError('SHADER-WEBGL1-MATRIX-STRIDE')
                size_bytes = components * columns * 4
                if offset + size_bytes > size.value or len(leaves) >= self.max_leaves:
                    raise RuntimeError('SHADER-WEBGL1-LEAF-BUDGET-OR-RANGE')
                leaves.append(dict(name=name, source=kind, set=key[0], binding=key[1],
                    offset=offset, size=size_bytes, scalar=scalar, components=components,
                    columns=columns, matrixStride=matrix_stride, required=True))
        visit(resource.base_type_id, root, 0)
        return dict(source=kind, set=key[0], binding=key[1], size=size.value, leaves=leaves)

    def generate(self, original_entry, optional_uniforms=()):
        original_entry = Path(original_entry).resolve()
        parent = original_entry.parent
        original = yaml.safe_load(original_entry.read_text(encoding='utf-8'))
        if original.get('type') != 'SingleEntryShaderArtifact' or original.get('contractVersion') != 3:
            raise RuntimeError('SHADER-WEBGL1-ORIGINAL-ARTIFACT')
        for role in original['files'].values():
            if set(role) != {'path', 'sha256'} or not Path(role['path']).name == role['path']:
                raise RuntimeError('SHADER-WEBGL1-ORIGINAL-ROLE')
            if digest(parent / role['path']) != role['sha256']:
                raise RuntimeError('SHADER-WEBGL1-ORIGINAL-HASH')
        spirv = parent / original['files']['spirv']['path']
        stage = original['stage']
        facts = SpirvLayoutFacts(spirv)
        self.validate_binary(facts, stage)
        with Cross(self.library, spirv) as c:
            if c.version() != [0, 68, 0]:
                raise RuntimeError('SHADER-WEBGL1-CONVERTER-API-VERSION')
            c.configure()
            for kind in (Cross.STORAGE_BUFFER, Cross.SUBPASS, Cross.STORAGE_IMAGE,
                         Cross.SAMPLED_IMAGE, Cross.ATOMIC_COUNTER):
                if c.resources(kind):
                    raise RuntimeError('SHADER-WEBGL1-RESOURCE-TYPE')
            interface = {}
            for kind, direction in ((Cross.INPUT, 'inputs'), (Cross.OUTPUT, 'outputs')):
                interface[direction] = []
                for resource in c.resources(kind):
                    t = c.call('get_type_handle', resource.type_id)
                    if c.type_value('num_array_dimensions', t):
                        raise RuntimeError('SHADER-WEBGL1-INTERFACE-ARRAY')
                    scalar, components, columns = self.scalar_type(c, t)
                    if scalar != 'Float32' or columns != 1:
                        raise RuntimeError('SHADER-WEBGL1-INTERFACE-TYPE')
                    location = c.call('get_decoration', resource.id, Cross.LOCATION)
                    if stage == 'Fragment' and direction == 'outputs' and (location or components != 4):
                        raise RuntimeError('SHADER-WEBGL1-FRAGMENT-OUTPUT')
                    name = (f'arpAttribute{location}' if stage == 'Vertex' and direction == 'inputs'
                            else f'arpOutput{location}' if stage == 'Fragment' and direction == 'outputs'
                            else f'arpVarying{location}')
                    c.call('set_name', resource.id, name.encode())
                    interface[direction].append(dict(name=name, location=location, components=components))
                interface[direction].sort(key=lambda item: item['location'])
                if len({item['location'] for item in interface[direction]}) != len(interface[direction]):
                    raise RuntimeError('SHADER-WEBGL1-INTERFACE-DUPLICATE')
            blocks = [self.uniform_leaves(c, r, 'Uniform') for r in c.resources(Cross.UNIFORM_BUFFER)]
            blocks += [self.uniform_leaves(c, r, 'Push') for r in c.resources(Cross.PUSH)]
            if len({(b['source'], b['set'], b['binding']) for b in blocks}) != len(blocks):
                raise RuntimeError('SHADER-WEBGL1-BLOCK-DUPLICATE')
            all_leaves = [leaf for b in blocks for leaf in b['leaves']]
            if len(all_leaves) > self.max_leaves or len(set(optional_uniforms)) != len(optional_uniforms):
                raise RuntimeError('SHADER-WEBGL1-LEAF-BUDGET')
            if set(optional_uniforms) - {leaf['name'] for leaf in all_leaves}:
                raise RuntimeError('SHADER-WEBGL1-OPTIONAL-UNIFORM-UNKNOWN')
            for leaf in all_leaves:
                leaf['required'] = leaf['name'] not in optional_uniforms
            images = {r.id: r for r in c.resources(Cross.IMAGE)}
            samplers = {r.id: r for r in c.resources(Cross.SAMPLER)}
            pairs = []
            for pair in c.combined_samplers():
                if pair.image_id not in images or pair.sampler_id not in samplers:
                    raise RuntimeError('SHADER-WEBGL1-SAMPLER-PAIR')
                image, sampler = images[pair.image_id], samplers[pair.sampler_id]
                t = c.call('get_type_handle', image.type_id)
                sampled = c.call('get_type_handle', c.type_value('image_sampled_type', t))
                if (c.type_value('num_array_dimensions', t) or c.type_value('image_dimension', t) != 1
                        or c.type_value('basetype', sampled) != Cross.FP32
                        or c.type_value('bit_width', sampled) != 32
                        or any(c.type_value(name, t) for name in ('image_is_depth', 'image_arrayed',
                                                               'image_multisampled', 'image_is_storage'))):
                    raise RuntimeError('SHADER-WEBGL1-TEXTURE-TYPE')
                ikey, skey = self.key(c, image), self.key(c, sampler)
                name = f'arpTexture_{ikey[0]}_{ikey[1]}_{skey[0]}_{skey[1]}'
                c.call('set_name', pair.combined_id, name.encode())
                pairs.append(dict(name=name, textureSet=ikey[0], textureBinding=ikey[1],
                                  samplerSet=skey[0], samplerBinding=skey[1]))
            text = c.compile()
            extensions = sorted(c.extensions())
            if any(ext not in ('GL_OES_standard_derivatives', 'GL_EXT_shader_texture_lod') for ext in extensions):
                raise RuntimeError('SHADER-WEBGL1-EXTENSION-UNSUPPORTED')
            path = parent / (original_entry.stem + '.glsl-es-100')
            with path.open('w', encoding='utf-8', newline='\n') as stream:
                stream.write(text)
            return dict(type='WebGlShaderTargetArtifact', contractVersion=1,
                target='glsl-es-100', status='Available', stage=stage, entry=original['entry'],
                matrixLayout='SlangRowMajorTransposedLogicalMatrix/1',
                coordinates='WebGlShaderCoordinates/1', precision='HighpFloatAndInt/1',
                original=file_role(original_entry, parent), glsl=file_role(path, parent),
                generatorSha256=digest(__file__), adapterSha256=digest(Path(__file__).with_name('spirv_cross_api.py')),
                converter=dict(sha256=digest(self.library), apiVersion=c.version()),
                slang=dict(sha256=digest(self.slangc), version=subprocess.check_output(
                    [str(self.slangc), '-version'], text=True, stderr=subprocess.STDOUT).strip(),
                    matrixLayout='row-major'),
                options={name: value for name, (_, value) in Cross.OPTIONS.items()},
                interface=interface, blocks=blocks, samplerPairs=sorted(pairs, key=lambda p:p['name']),
                uniformVectors=sum(leaf['columns'] for leaf in all_leaves) + len(pairs),
                requiredExtensions=extensions)

    def publish(self, original_entry, optional_uniforms=()):
        original_entry = Path(original_entry)
        output = original_entry.with_suffix('.glsl-es-100.yaml')
        output.unlink(missing_ok=True)
        stale = original_entry.parent / (original_entry.stem + '.glsl-es-100')
        stale.unlink(missing_ok=True)
        try:
            result = self.generate(original_entry, optional_uniforms)
        except RuntimeError as error:
            if not str(error).startswith('SHADER-WEBGL1-'):
                raise
            if stale.exists():
                stale.unlink()
            result = dict(type='WebGlShaderTargetArtifact', contractVersion=1,
                target='glsl-es-100', status='DiagnosticFailure', diagnosticId=str(error),
                original=file_role(original_entry, original_entry.parent))
        output.write_text(yaml.safe_dump(result, sort_keys=False), encoding='utf-8')
        return output


def publish_program(vertex, fragment, output):
    output = Path(output).resolve()
    # A failed composition cannot leave an older executable-looking publication.
    output.unlink(missing_ok=True)
    stages = [yaml.safe_load(Path(p).read_text(encoding='utf-8')) for p in (vertex, fragment)]
    if any(s['status'] != 'Available' for s in stages) or [s['stage'] for s in stages] != ['Vertex', 'Fragment']:
        raise RuntimeError('SHADER-WEBGL1-PROGRAM-TARGET-UNAVAILABLE')
    if stages[0]['interface']['outputs'] != stages[1]['interface']['inputs']:
        raise RuntimeError('SHADER-WEBGL1-VARYING-INTERFACE')
    pairs = sorted({p['name'] for s in stages for p in s['samplerPairs']})
    result = dict(type='WebGlShaderProgramArtifact', contractVersion=1,
        profile='WebGl1Graphics/1', targets=[file_role(Path(p).resolve(), output.parent) for p in (vertex, fragment)],
        textureUnits=[dict(name=name, unit=i) for i,name in enumerate(pairs)],
        nativeExecutionProven=False)
    output.write_text(yaml.safe_dump(result, sort_keys=False), encoding='utf-8')
    return output
