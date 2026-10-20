"""Independently replay the original public Graphics matrix clean/fault draws."""
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import traceback

import renderdoc as rd

report = {'scope': 'explicit public Graphics workload'}
capture = controller = None
try:
    sys.path.insert(0, os.environ['ARP_BINDLESS_GRAPHICS_ORACLE_DIR'])
    from bindless_graphics_readback_oracles import MatrixOrdinaryOracle, Skinning256Oracle, Instance64Oracle, PbrMaterialOracle, Terrain27Oracle, OrdinaryColorGiOracle, OrdinaryHdrGiOracle, OrdinaryWoccIblOracle, verify_numeric_readback
    workload = os.environ.get('ARP_BINDLESS_GRAPHICS_WORKLOAD', 'matrix')
    if workload not in ('matrix', 'skinning256', 'instance64', 'pbr-material', 'terrain27', 'ordinary-color-gi', 'ordinary-hdr-gi', 'wocc-ibl'):
        raise RuntimeError('Unsupported explicit Graphics readback workload')
    oracle = OrdinaryWoccIblOracle(os.environ['ARP_BINDLESS_IBL_INPUT_DIR']) if workload == 'wocc-ibl' else OrdinaryHdrGiOracle() if workload == 'ordinary-hdr-gi' else OrdinaryColorGiOracle() if workload == 'ordinary-color-gi' else Terrain27Oracle() if workload == 'terrain27' else PbrMaterialOracle() if workload == 'pbr-material' else Instance64Oracle() if workload == 'instance64' else Skinning256Oracle() if workload == 'skinning256' else MatrixOrdinaryOracle()
    report.update(scope=oracle.scope, ordinaryBufferBytes=list(oracle.ordinary_buffers))
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ['ARP_BINDLESS_CAPTURE_PATH'], '', None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    package = Path(os.environ['ARP_BINDLESS_MATRIX_PACKAGE_DIR'])
    hashes = {hashlib.sha256((package / (oracle.package_name + '.native.' + entry + '.spv')).read_bytes()).hexdigest(): entry
              for entry in ['vertexMain', 'cleanFragment', 'faultFragment']}
    reference_hashes = {}
    if workload == 'wocc-ibl':
        reference_package = Path(os.environ['ARP_BINDLESS_REFERENCE_PACKAGE_DIR'])
        reference_hashes = {hashlib.sha256((reference_package / ('GraphicsWoccIblReference.native.' + entry + '.spv')).read_bytes()).hexdigest(): entry
                            for entry in ['vertexMain', 'cleanFragment']}
    direct_reference = None
    traditional_prefix = os.environ.get('ARP_BINDLESS_TRADITIONAL_REFERENCE_PREFIX')
    traditional_hash = traditional_reference = traditional_color = None
    if traditional_prefix:
        if workload not in ('pbr-material', 'terrain27'):
            raise RuntimeError('Traditional material reference requires an authored material workload')
        sys.path.insert(0, str(Path(os.environ['ARP_BINDLESS_GRAPHICS_ORACLE_DIR']).parents[1] / 'scripts'))
        from spirv_layout import SpirvLayoutFacts
        traditional_spv = Path(traditional_prefix + '.spv')
        traditional_hash = hashlib.sha256(traditional_spv.read_bytes()).hexdigest()
        instructions = SpirvLayoutFacts(traditional_spv).instructions
        samples = 12 if workload == 'pbr-material' else 27
        if sum(opcode == 87 for _, opcode, _ in instructions) != samples:
            raise RuntimeError('Traditional reference must retain every implicit SPIR-V sample')
        if workload == 'pbr-material' and not any(opcode == 252 for _, opcode, _ in instructions):
            raise RuntimeError('Traditional PBR reference must retain actual discard')
    expected = oracle.expected
    result_bytes = len(expected) * 4

    def walk(actions):
        for action in actions:
            yield action
            yield from walk(action.children)

    actions = list(walk(controller.GetRootActions()))
    lengths = {b.resourceId: b.length for b in controller.GetBuffers()}
    evidence = []
    for action in actions:
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        controller.SetFrameEvent(action.eventId, True)
        pipe = controller.GetPipelineState()
        reflections = [pipe.GetShaderReflection(stage) for stage in [rd.ShaderStage.Vertex, rd.ShaderStage.Fragment]]
        shader_hashes = [hashlib.sha256(bytes(shader.rawBytes)).hexdigest() if shader else None for shader in reflections]
        is_traditional = traditional_hash is not None and shader_hashes == [traditional_hash, traditional_hash]
        is_reference = shader_hashes[1] in reference_hashes
        selected_hashes = reference_hashes if is_reference else hashes
        entries = [shader.entryPoint for shader in reflections] if is_traditional else [selected_hashes.get(value) for value in shader_hashes]
        if entries[1] not in (('fragmentMain',) if is_traditional else ('cleanFragment', 'faultFragment')):
            continue
        if entries[0] != 'vertexMain':
            raise RuntimeError('Wrong authenticated Vertex binary')
        used = list(pipe.GetAllUsedDescriptors())
        descriptors = [d.descriptor for d in used]
        if workload == 'wocc-ibl':
            # Bind by the authenticated shader's reflected resource, not just
            # its size: reference receipt and DirectResults are both1696 bytes.
            result_name = 'DirectResults' if is_reference else 'Results'
            result_bindings = [(i, resource) for i, resource in enumerate(reflections[1].readWriteResources)
                               if resource.name == result_name]
            if len(result_bindings) != 1:
                raise RuntimeError('Missing authenticated IBL result declaration')
            index, binding = result_bindings[0]
            if (binding.fixedBindSetOrSpace, binding.fixedBindNumber) != ((0, 4) if is_reference else (1, 3)):
                raise RuntimeError('Wrong authenticated IBL result binding')
            result_buffers = {u.descriptor.resource for u in used
                              if u.access.stage == rd.ShaderStage.Fragment
                              and u.access.type == rd.DescriptorType.ReadWriteBuffer
                              and u.access.index == index and u.access.arrayElement == 0
                              and lengths.get(u.descriptor.resource) == result_bytes
                              and u.descriptor.byteOffset == 0
                              and u.descriptor.byteSize in (result_bytes, (1 << 64) - 1)}
        else:
            result_buffers = {d.resource for d in descriptors if lengths.get(d.resource) == result_bytes}
        if len(result_buffers) != 1:
            raise RuntimeError('Missing exact bindless Results buffer')
        result = next(iter(result_buffers))
        result_data = bytes(controller.GetBufferData(result, 0, result_bytes))
        if is_reference:
            if direct_reference is not None or evidence:
                raise RuntimeError('Duplicate or out-of-order direct IBL reference')
            reference_checks = oracle.verify_direct_reference(result_data)
            values = list(struct.unpack('<424f', result_data))
            output_bits = list(struct.unpack('<424I', result_data))
        elif workload == 'wocc-ibl':
            if direct_reference is None:
                raise RuntimeError('Missing independently validated direct IBL reference')
            values, output_bits = oracle.verify_direct_parity(result_data, direct_reference)
        else:
            values, output_bits = verify_numeric_readback(oracle, result_data)
            if traditional_hash is not None:
                if is_traditional:
                    if traditional_reference is not None or evidence:
                        raise RuntimeError('Duplicate or out-of-order Traditional draw')
                elif traditional_reference is None:
                    raise RuntimeError('Missing independent Traditional draw before Root')
                else:
                    reference_values = struct.unpack('<' + str(len(expected)) + 'f', traditional_reference)
                    if any(abs(actual - reference) > oracle.tolerance * (1 + abs(reference))
                           for actual, reference in zip(values, reference_values)):
                        raise RuntimeError('Root/Traditional numeric mismatch')
        for size, authored in oracle.ordinary_buffers.items():
            resources = {d.resource for d in descriptors if lengths.get(d.resource) == size}
            if len(resources) != 1:
                raise RuntimeError('Missing ordinary buffer of size ' + str(size))
            data = bytes(controller.GetBufferData(next(iter(resources)), 0, size))
            if data != authored:
                raise RuntimeError('Ordinary uploaded CPU bytes differ')
        texture_mips = getattr(oracle, 'texture_mips', None)
        if texture_mips is not None:
            textures = {t.resourceId: t for t in controller.GetTextures()}
            resources = {d.resource for d in descriptors if d.resource in textures
                         and textures[d.resource].mips == 2}
            actual_mips = []
            for resource in resources:
                actual_mips.append(tuple(bytes(controller.GetTextureData(resource, rd.Subresource(mip, 0, 0)))
                                         for mip in range(2)))
            if sorted(actual_mips) != sorted(texture_mips):
                raise RuntimeError('Five-role uploaded texture mip bytes differ')
        texture_layers = getattr(oracle, 'texture_layers', None)
        if texture_layers is not None:
            textures = {t.resourceId: t for t in controller.GetTextures()}
            resources = {d.resource for d in descriptors if d.resource in textures
                         and textures[d.resource].mips == 2}
            actual_layers = [tuple(tuple(bytes(controller.GetTextureData(resource,
                rd.Subresource(mip, layer, 0))) for mip in range(2))
                for layer in range(textures[resource].arraysize)) for resource in resources]
            if sorted(actual_layers) != sorted(texture_layers):
                raise RuntimeError('Authored uploaded texture/layer/mip bytes differ')
            report['sampledTextureCount'] = len(resources)
            report['sampledArrayCount'] = sum(textures[r].arraysize == 2 for r in resources)
        texture_chains = getattr(oracle, 'texture_chains', None)
        if texture_chains is not None:
            textures = {t.resourceId: t for t in controller.GetTextures()}
            resources = {d.resource for d in descriptors if d.resource in textures}
            actual_chains = []
            for resource in resources:
                texture = textures[resource]
                if (texture.format.compCount != 4 or texture.format.compByteWidth != 2
                        or texture.format.compType != rd.CompType.Float):
                    raise RuntimeError('Actual-size WOCC IBL requires RGBA16F format')
                layers = tuple(tuple(bytes(controller.GetTextureData(resource,
                    rd.Subresource(mip, layer, 0))) for mip in range(texture.mips))
                    for layer in range(texture.arraysize))
                actual_chains.append((texture.width, texture.height, texture.mips,
                                      texture.arraysize, layers))
            if sorted(actual_chains) != sorted(texture_chains):
                raise RuntimeError('Actual-size WOCC IBL shape/uploaded layer/mip bytes differ')
            report['sampledTextureCount'] = len(resources)
            report['sampledTextureShapes'] = sorted([list(row[:4]) for row in actual_chains])
            report['uploadedTextureBytes'] = sum(len(mip) for row in actual_chains
                                                  for layer in row[4] for mip in layer)
            report['coldBakedInputHashes'] = oracle.input_hashes
        targets = [target for target in pipe.GetOutputTargets() if target.resource != rd.ResourceId.Null()]
        if len(targets) != 1:
            raise RuntimeError('Missing color attachment')
        color = bytes(controller.GetTextureData(targets[0].resource, rd.Subresource(0, 0, 0)))
        # UNORM conversion may choose either adjacent value at a half-way tie.
        expected_color = getattr(oracle, 'color_pixels', [64, 128, 191, 255] * 4)
        if len(color) != 16 or any(abs(value - expected_color[i]) > 1
                                   for i, value in enumerate(color)):
            raise RuntimeError('Unexpected original color output: ' + color.hex())
        bits = None
        if not is_traditional:
            if traditional_hash is not None and color != traditional_color:
                raise RuntimeError('Root/Traditional attachment mismatch')
            copied = next((a for a in actions if a.eventId > action.eventId and a.flags & rd.ActionFlags.Copy), None)
            if copied is None or lengths.get(copied.copyDestination) != 4:
                raise RuntimeError('Missing original fault readback copy')
            controller.SetFrameEvent(copied.eventId, True)
            bits = struct.unpack('<I', bytes(controller.GetBufferData(copied.copyDestination, 0, 4)))[0]
            if bits != int(entries[1] == 'faultFragment'):
                raise RuntimeError('Wrong fault bits')
        row = {'eventId': action.eventId, 'entries': entries, 'values': values,
                         'shaderHashes': shader_hashes,
                         'outputBits': output_bits, 'arithmeticTolerance': getattr(oracle, 'tolerance', 0),
                         'faultBits': bits, 'colorRgba8': list(color)}
        if is_traditional:
            traditional_reference = result_data
            traditional_color = color
            report['directTraditionalReference'] = dict(row,
                executionPath='ordinary public RHI descriptor sets; genuine fence completion',
                implicitSpirvSamples=samples, actualDiscardRequired=workload == 'pbr-material',
                traditionalExecutionClaimed=True)
        elif is_reference:
            direct_reference = result_data
            report['directShaderReference'] = dict(row, **reference_checks,
                executionPath='original public Root; receipt-only indexed store',
                traditionalExecutionClaimed=False)
        else:
            evidence.append(row)
    if [e['entries'][1] for e in evidence] != ['cleanFragment', 'faultFragment']:
        raise RuntimeError('Missing clean/fault draws')
    if traditional_hash is not None and traditional_reference is None:
        raise RuntimeError('Missing authenticated ordinary Traditional reference')
    report['executions'] = evidence
    report['validationMessages'] = [m.description for m in controller.GetDebugMessages()]
    if report['validationMessages']:
        raise RuntimeError('Vulkan validation reported messages')
    report['success'] = True
except Exception:
    report['success'] = False
    report['error'] = traceback.format_exc()
finally:
    if controller:
        controller.Shutdown()
    if capture:
        capture.Shutdown()
    Path(os.environ['ARP_BINDLESS_CAPTURE_OUTPUT']).write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
    os._exit(0 if report.get('success') else 1)
