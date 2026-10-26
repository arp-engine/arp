"""Replay Preparation/6's two real Compute passes, HDR Draw and explicit copy."""
import hashlib
import json
import math
import os
import struct
import sys
import traceback
from pathlib import Path

import renderdoc as rd

sys.path.insert(0, os.path.join(os.environ["ARP_REPO_ROOT"], "tests", "tools"))
from analyze_ui_text_renderdoc import flatten, descriptor_resource, texture_bytes, write_bmp
from renderdoc_cloud_common import fields, number, succeeded, display


def binding(pipeline, stage, reflection, index):
    matches = [entry.descriptor for entries, declarations in (
        (pipeline.GetReadOnlyResources(stage), reflection.readOnlyResources),
        (pipeline.GetReadWriteResources(stage), reflection.readWriteResources))
        for entry in entries if declarations[entry.access.index].fixedBindNumber == index]
    assert len(matches) == 1, (stage, index, matches)
    return matches[0]


def main():
    path = os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"]
    def checkpoint(stage):
        with open(path + ".replay.log", "a", encoding="utf-8") as log:
            log.write(stage + "\n")
    Path(path + ".replay.log").write_text("RenderDoc " + rd.GetVersionString() + "\n", encoding="utf-8")
    graph_profile = os.environ.get("ARP_CLOUD_CAPTURE_GRAPH") == "1"
    oracle_path = os.environ.get("ARP_CLOUD_ORACLE_PATH")
    oracle = json.loads(Path(oracle_path).read_text(encoding="utf-8")) if oracle_path else None
    foreground = bool(oracle and oracle.get("foreground"))
    sequence = bool(oracle and "rawExpected" in oracle)
    width, height = (oracle["width"], oracle["height"]) if oracle else (4, 4)
    sample_width = oracle.get("sampleWidth", width) if oracle else 1
    sample_height = oracle.get("sampleHeight", height) if oracle else 1
    sample_count = sample_width * sample_height
    spatial = bool(oracle and oracle.get("reconstructionVersion"))
    resolved_width = oracle.get("resolvedWidth", sample_width) if oracle else sample_width
    resolved_height = oracle.get("resolvedHeight", sample_height) if oracle else sample_height
    resolved_count = resolved_width * resolved_height
    if spatial:
        assert oracle["reconstructionVersion"] == 1
        assert (resolved_width, resolved_height) == (width, height)
        assert (sample_width, sample_height) == ((width + 1) // 2, (height + 1) // 2)
    resolve_density = resolve_depth = ray_density = None
    policy_binding = None
    resolved_values = None
    previous_input = None
    previous_error = 0.0
    max_sample_error = max_hdr_error = max_opaque_error = 0.0
    cap = rd.OpenCaptureFile()
    checkpoint("OpenFile")
    assert succeeded(cap.OpenFile(path, "", None))
    checkpoint("OpenCapture")
    options = rd.ReplayOptions()
    options.optimisation = rd.ReplayOptimisationLevel.NoOptimisation
    options.apiValidation = os.environ.get("ARP_RENDERDOC_API_VALIDATION") == "1"
    status, controller = cap.OpenCapture(options, None)
    assert succeeded(status) and controller
    try:
        checkpoint("RootActions")
        actions = list(flatten(controller.GetRootActions()))
        draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall]
        dispatches = [a for a in actions if a.flags & rd.ActionFlags.Dispatch]
        copies = [a for a in actions if a.flags & rd.ActionFlags.Copy]
        assert len(draws) == (2 if foreground else 1) and len(copies) == 1 and len(dispatches) == 2
        composite = draws[-1]
        assert dispatches[0].eventId < dispatches[1].eventId < composite.eventId < copies[0].eventId
        foreground_vertices = None
        if foreground:
            assert draws[0].eventId < dispatches[0].eventId
            checkpoint("Foreground SetFrameEvent")
            controller.SetFrameEvent(draws[0].eventId, True)
            checkpoint("Foreground PostVS")
            mesh = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
            assert mesh.numIndices == 3 and mesh.vertexByteStride >= 16
            data = bytes(controller.GetBufferData(mesh.vertexResourceId, mesh.vertexByteOffset,
                mesh.vertexByteStride * mesh.numIndices))
            foreground_vertices = [struct.unpack_from("<4f", data, i * mesh.vertexByteStride) for i in range(3)]
            assert foreground_vertices == [(-.75, -.75, .5, 1.), (.75, -.75, .5, 1.), (0., .75, .5, 1.)]
        checkpoint("StructuredFile")
        structured = controller.GetStructuredFile()
        visited, command_buffers, calls = set(), set(), {}
        image_barriers, buffer_barriers, sets, viewports = [], [], [], []
        for action in actions:
            for event in action.events:
                if event.eventId in visited:
                    continue
                visited.add(event.eventId)
                chunk = structured.chunks[event.chunkIndex]
                if not chunk.name.startswith("vkCmd"):
                    continue
                native = fields(chunk)
                calls[chunk.name] = calls.get(chunk.name, 0) + 1
                command_buffers.add(number(native["commandBuffer"]))
                if chunk.name == "vkCmdSetViewport":
                    array = native["pViewports"]
                    for index in range(array.NumChildren()):
                        values = fields(array.GetChild(index))
                        viewports.append([float(values[k].data.basic.d) for k in
                            ("x", "y", "width", "height", "minDepth", "maxDepth")])
                if chunk.name == "vkCmdBindDescriptorSets":
                    row = {k: number(native[k]) for k in (
                        "pipelineBindPoint", "firstSet", "setCount", "dynamicOffsetCount")}
                    row["event"] = event.eventId
                    sets.append(row)
                if chunk.name == "vkCmdPipelineBarrier":
                    for array_name, result, keys in (
                        ("pImageMemoryBarriers", image_barriers,
                         ("image", "oldLayout", "newLayout", "srcAccessMask", "dstAccessMask")),
                        ("pBufferMemoryBarriers", buffer_barriers,
                         ("buffer", "offset", "size", "srcAccessMask", "dstAccessMask"))):
                        array = native[array_name]
                        for index in range(array.NumChildren()):
                            values = fields(array.GetChild(index))
                            result.append({"event": event.eventId,
                                "sourceStages": number(native["srcStageMask"]),
                                "destinationStages": number(native["destStageMask"]),
                                **{k: number(values[k]) for k in keys}})
        assert len(command_buffers) == 1, command_buffers
        assert calls["vkCmdDispatch"] == 2 and calls["vkCmdDraw"] == len(draws)
        assert calls["vkCmdCopyImageToBuffer"] == 1
        assert calls["vkCmdBeginRenderPass"] == calls["vkCmdEndRenderPass"] == 2
        expected_buffer_barriers = ((9 if foreground else 8) if graph_profile else 2)
        if sequence:
            assert graph_profile
            expected_buffer_barriers = (8 if oracle["historyUsed"] else 7) + int(foreground)
            if spatial:
                # The policy has one first-use dependency; the full previous
                # spare is distinct from raw even on a history-disabled frame.
                expected_buffer_barriers += 1 + int(not oracle["historyUsed"])
            if "opaqueDistances" in oracle:
                assert len(oracle["opaqueDistances"]) == sample_count
            if foreground:
                assert viewports == [oracle["foregroundViewport"] + [0., 1.], [0., 0., float(width), float(height), 0., 1.]], viewports
                if oracle["historyUsed"] and oracle.get("requireOcclusionChange", True) and not spatial:
                    assert oracle["opaqueRejected"] > 0 and oracle["disoccluded"] > 0 and oracle["newlyOccluded"] > 0
        assert len(image_barriers) == 6 and len(buffer_barriers) == expected_buffer_barriers, (image_barriers, buffer_barriers)
        assert [s["pipelineBindPoint"] for s in sets] == ([0, 1, 1, 0] if foreground else [1, 1, 0])
        assert all([s[k] for k in ("firstSet", "setCount", "dynamicOffsetCount")] == [0, 1, 0]
            for s in sets)
        samples, constants = [], []
        for index, action in enumerate(dispatches):
            checkpoint("Dispatch " + str(index))
            controller.SetFrameEvent(action.eventId, True)
            pipeline = controller.GetPipelineState()
            reflection = pipeline.GetShaderReflection(rd.ShaderStage.Compute)
            assert reflection.entryPoint == "computeMain"
            grid_width, grid_height = (resolved_width, resolved_height) if index else (sample_width, sample_height)
            assert list(action.dispatchDimension) == [(grid_width + 7) // 8, (grid_height + 7) // 8, 1]
            blocks = [b.descriptor for b in pipeline.GetConstantBlocks(rd.ShaderStage.Compute)
                if reflection.constantBlocks[b.access.index].fixedBindNumber == 0]
            assert len(blocks) == 1 and blocks[0].byteSize == 368
            cb = blocks[0]
            data = bytes(controller.GetBufferData(cb.resource, cb.byteOffset, cb.byteSize))
            assert struct.unpack_from("<4I", data, 272) == ((sample_width, sample_height, width, height) if oracle else (1, 1, 4, 4))
            if sequence:
                assert struct.unpack_from("<f", data, 348)[0] == float(oracle["historyUsed"])
                assert struct.unpack_from("<f", data, 364)[0] == float(oracle.get("orthographic", False))
                if "viewSteps" in oracle:
                    assert struct.unpack_from("<2I", data, 288) == (oracle["viewSteps"], oracle["lightSteps"])
            constants.append((int(cb.resource), cb.byteOffset, cb.byteSize))
            if spatial and index:
                policies = [b.descriptor for b in pipeline.GetConstantBlocks(rd.ShaderStage.Compute)
                    if reflection.constantBlocks[b.access.index].fixedBindNumber == 6]
                assert len(policies) == 1 and policies[0].byteSize == 16
                policy = policies[0]
                assert policy.resource != cb.resource and policy.byteOffset == 0
                policy_data = bytes(controller.GetBufferData(policy.resource, policy.byteOffset, 16))
                version, lr, tr, reserved = struct.unpack("<IffI", policy_data)
                assert version == 1 and reserved == 0
                assert all(abs(a-b) < 1.e-8 for a,b in zip((lr,tr), oracle["reconstructionRanges"]))
                policy_binding = {"resource": str(policy.resource), "version": version, "ranges": [lr,tr], "reserved": reserved}
                resolve_density = binding(pipeline, rd.ShaderStage.Compute, reflection, 4)
                resolve_depth = binding(pipeline, rd.ShaderStage.Compute, reflection, 5)
            elif not index:
                ray_density = binding(pipeline, rd.ShaderStage.Compute, reflection, 1)
            output = binding(pipeline, rd.ShaderStage.Compute, reflection, 3)
            count = resolved_count if index else sample_count
            sample = bytes(controller.GetBufferData(output.resource, output.byteOffset, 32 * count))
            assert len(sample) == count * 32
            values = [struct.unpack_from("<8f", sample, i * 32) for i in range(count)]
            if index:
                resolved_values = values
            assert all(math.isfinite(v) for row in values for v in row)
            if oracle:
                expected = oracle[("resolvedExpected" if index else "rawExpected")] if sequence else oracle["expected"]
                assert len(expected) == count
                error = max(abs(row[c] - e[c]) for row, e in zip(values, expected) for c in range(4))
                assert error < oracle["tolerance"], error
                assert all(0.0 <= row[3] <= 1.0 for row in values)
                max_sample_error = max(max_sample_error, error)
                if sequence and "opaqueDistances" not in oracle:
                    assert all(row[5] == struct.unpack_from("<f", data, 328)[0] for row in values)
                if "opaqueDistances" in oracle:
                    opaque = oracle["resolvedOpaqueDistances"] if spatial and index else oracle["opaqueDistances"]
                    assert len(opaque) == count
                    opaque_error = max(abs(row[5] - expected) for row, expected in zip(values, opaque))
                    assert opaque_error < oracle["tolerance"], opaque_error
                    max_opaque_error = max(max_opaque_error, opaque_error)
            else:
                assert values[0][:4] == (0., 0., 0., 1.) and values[0][4:6] == (1000., 1000.)
            actual = values[0]
            if index:
                source = binding(pipeline, rd.ShaderStage.Compute, reflection, 1)
                assert (source.resource, source.byteOffset) == (
                    samples[0]["resource"], samples[0]["offset"])
                if sequence:
                    previous = binding(pipeline, rd.ShaderStage.Compute, reflection, 2)
                    previous_input = {"resource": str(previous.resource), "offset": previous.byteOffset}
                    if oracle["historyUsed"]:
                        assert previous.resource not in (source.resource, output.resource)
                        old_bytes = bytes(controller.GetBufferData(previous.resource, previous.byteOffset, resolved_count * 32))
                        old = [struct.unpack_from("<8f", old_bytes, i * 32) for i in range(resolved_count)]
                        assert len(oracle["previousExpected"]) == resolved_count
                        previous_error = max(abs(row[c]-e[c]) for row, e in zip(old, oracle["previousExpected"]) for c in range(4))
                        assert previous_error < oracle["tolerance"], previous_error
                        assert oracle["historyAccepted"] > 0
                        baseline = oracle["currentExpected"] if spatial else oracle["rawExpected"]
                        assert len(baseline) == count
                        assert max(abs(a[c]-b[c]) for a,b in zip(baseline,oracle["resolvedExpected"]) for c in range(4)) > 1.e-6
                    else:
                        if spatial:
                            assert previous.resource not in (source.resource, output.resource)
                            assert previous.byteOffset == 0
                        else:
                            assert (previous.resource, previous.byteOffset) == (source.resource, source.byteOffset)
            samples.append({"resource": output.resource, "offset": output.byteOffset,
                "event": action.eventId, "sample": actual, "count": count, "groups": list(action.dispatchDimension)})
        assert constants[0] == constants[1]
        for index, sample in enumerate(samples):
            matches = [b for b in buffer_barriers if b["buffer"] == int(sample["resource"])
                and sample["event"] < b["event"]
                < (dispatches[1].eventId if not index else composite.eventId)]
            assert len(matches) == 1 and matches[0]["srcAccessMask"] == 0x60
            assert matches[0]["dstAccessMask"] == 0x20
            if graph_profile:
                # Actual graph batches also contain explicit first-use buffer
                # dependencies. Their stage union must preserve this producer
                # and its exact consumer; all barriers are retained in report.
                required_source, required_destination = 0x800, 0x800 if not index else 0x80
                assert matches[0]["sourceStages"] & required_source == required_source, matches
                assert matches[0]["destinationStages"] & required_destination == required_destination, matches
            else:
                assert matches[0]["sourceStages"] == (0x800 if not index else 0x801), matches
                assert matches[0]["destinationStages"] == (0x800 if not index else 0x480), matches
                if index:
                    joined = [b for b in image_barriers if b["event"] == matches[0]["event"]]
                    assert len(joined) == 1 and joined[0]["oldLayout"] == 0 and joined[0]["newLayout"] == 2
        checkpoint("Composite")
        controller.SetFrameEvent(composite.eventId, True)
        pipeline = controller.GetPipelineState()
        reflection = pipeline.GetShaderReflection(rd.ShaderStage.Fragment)
        resolved = binding(pipeline, rd.ShaderStage.Fragment, reflection, 1)
        assert (resolved.resource, resolved.byteOffset) == (
            samples[1]["resource"], samples[1]["offset"])
        scene = binding(pipeline, rd.ShaderStage.Fragment, reflection, 2)
        depth = binding(pipeline, rd.ShaderStage.Fragment, reflection, 3)
        target = descriptor_resource(pipeline.GetOutputTargets()[0])
        background = (.125, .375, .75, .625)
        foreground_color = (.75, .125, .0625, .875)
        coverage = [foreground and -.75 < 2 * (y + .5) / height - 1 < .75
            and abs(2 * (x + .5) / width - 1) < (.75 - (2 * (y + .5) / height - 1)) * .5
            for y in range(height) for x in range(width)]
        if sequence and "foregroundMask" in oracle:
            coverage = [bool(value) for value in oracle["foregroundMask"]]
            assert len(coverage) == width * height
            if foreground:
                vx, vy, vw, vh = oracle["foregroundViewport"]
                independent_coverage = [-.75 < 2 * (y + .5 - vy) / vh - 1 < .75
                    and abs(2 * (x + .5 - vx) / vw - 1) < (.75 - (2 * (y + .5 - vy) / vh - 1)) * .5
                    for y in range(height) for x in range(width)]
                assert coverage == independent_coverage
        scene_colors = [foreground_color if covered else background for covered in coverage]
        expected_pixel = struct.pack("<4e", *background)
        assert texture_bytes(controller, scene.resource) == b"".join(struct.pack("<4e", *color) for color in scene_colors)
        assert texture_bytes(controller, depth.resource) == b"".join(struct.pack("<f", .5 if covered else 1.) for covered in coverage)
        # Raymarch must consume the same actual D32 owner as the composite.
        controller.SetFrameEvent(dispatches[0].eventId, True)
        ray_pipeline = controller.GetPipelineState()
        ray_reflection = ray_pipeline.GetShaderReflection(rd.ShaderStage.Compute)
        assert binding(ray_pipeline, rd.ShaderStage.Compute, ray_reflection, 2).resource == depth.resource
        if spatial:
            assert resolve_depth.resource == depth.resource
            assert (resolve_density.resource, resolve_density.byteOffset, resolve_density.byteSize) == (
                ray_density.resource, ray_density.byteOffset, ray_density.byteSize)
        controller.SetFrameEvent(composite.eventId, True)
        actual = texture_bytes(controller, target)
        if oracle:
            expected_colors = oracle["expectedHdr"] if sequence else [tuple(row[c] + row[3] * color[c] for c in range(3)) + (color[3],)
                for row, color in zip(oracle["expected"], scene_colors)]
            assert len(expected_colors) == width * height
            actual_colors = [struct.unpack_from("<4e", actual, i * 8) for i in range(width * height)]
            max_hdr_error = max(abs(a[c] - e[c]) for a, e in zip(actual_colors, expected_colors) for c in range(4))
            assert max_hdr_error < oracle["tolerance"], max_hdr_error
        else:
            assert actual == expected_pixel * 16, actual.hex()
            actual_colors = [background] * 16
        quality_report = None
        if oracle and "quality" in oracle:
            q = oracle["quality"]
            assert q["contractVersion"] == 1 and q["referenceSteps"] == [256, 32] and q["convergedSteps"] == [512, 64]
            fine, coarse = oracle["qualityFineReference"], oracle["qualityReference"]
            assert len(fine) == len(coarse) == width * height
            def error_stats(errors):
                errors = list(errors)
                return {"count": len(errors), "rms": math.sqrt(sum(e*e for e in errors) / len(errors)) if errors else 0.,
                        "max": max((abs(e) for e in errors), default=0.)}
            clipped = oracle.get("qualityDepthClipped", False)
            targets = [color if covered and not clipped else tuple(row[c] + row[3]*color[c] for c in range(3)) + (color[3],)
                       for covered, row, color in zip(coverage, fine, scene_colors)]
            quality_hdr = error_stats(a[c]-e[c] for a, e in zip(actual_colors, targets) for c in range(3))
            convergence = error_stats(a[c]-b[c] for a,b in zip(coarse, fine) for c in range(4))
            exposed = oracle["qualityExposedMask"]
            assert len(exposed) == width * height and sum(exposed) == q["exposedPixels"]
            quality_exposed = error_stats(a[c]-e[c] for a,e,mask in zip(actual_colors, targets, exposed) if mask for c in range(3))
            measured_metrics = {"hdr": quality_hdr, "convergence": convergence, "exposedHdr": quality_exposed}
            if spatial:
                integrals = [(0.,0.,0.,1.) if covered and not clipped else row for covered,row in zip(coverage, fine)]
                measured_metrics["radiance"] = error_stats(a[c]-e[c] for a,e in zip(resolved_values,integrals) for c in range(3))
                measured_metrics["transmittance"] = error_stats(a[3]-e[3] for a,e in zip(resolved_values,integrals))
                if clipped:
                    vacuum = oracle["qualityVacuumMask"]
                    assert len(vacuum) == width * height
                    for covered, empty, value, color, scene_color in zip(coverage, vacuum, resolved_values, actual_colors, scene_colors):
                        assert color[3] == scene_color[3]
                        if covered and empty:
                            assert value[:4] == (0.,0.,0.,1.) and color == scene_color
            for name, measured in measured_metrics.items():
                assert measured["count"] == q[name]["count"]
                assert abs(measured["rms"]-q[name]["rms"]) < 1.e-7, (name, measured, q[name])
                assert abs(measured["max"]-q[name]["max"]) < 1.e-7, (name, measured, q[name])
            replay_passed = True
            for name, measured in measured_metrics.items():
                if not measured["count"]:
                    assert name == "exposedHdr" and not q["exposedPixels"]
                    continue
                limits = (q["convergenceRmsLimit"], q["convergenceMaxLimit"]) if name == "convergence" else (q["rmsLimit"], q["maxLimit"])
                replay_passed &= measured["rms"] <= limits[0] and measured["max"] <= limits[1]
            quality_report = {"hdr": quality_hdr, "convergence": convergence, "exposedHdr": quality_exposed,
                              "replayMetrics": measured_metrics,
                              "passed": bool(q["passed"] and replay_passed),
                              "nativeMetrics": {name: q[name] for name in ("radiance", "transmittance", "rawRadiance", "rawTransmittance")},
                              "referenceSteps": q["referenceSteps"], "convergedSteps": q["convergedSteps"],
                              "samplingSteps": [oracle["viewSteps"], oracle["lightSteps"]]}
        controller.SetFrameEvent(copies[0].eventId, True)
        assert copies[0].copySource == target
        row_bytes = oracle["rowBytes"] if oracle else 256
        readback_bytes = row_bytes * height
        readback = bytes(controller.GetBufferData(copies[0].copyDestination, 0, readback_bytes))
        assert len(readback) == readback_bytes
        for y in range(height):
            assert readback[y * row_bytes:y * row_bytes + width * 8] == actual[y * width * 8:(y + 1) * width * 8]
            assert readback[y * row_bytes + width * 8:(y + 1) * row_bytes] == bytes(row_bytes - width * 8)
        image_path = os.path.splitext(path)[0] + ".output.bmp"
        scale = 16 if oracle else 64
        pixels = [tuple(display(v) for v in actual_colors[(y // scale) * width + x // scale][:3]) + (255,)
            for y in range(height * scale) for x in range(width * scale)]
        write_bmp(image_path, width * scale, height * scale, pixels)
        controller.SetFrameEvent(actions[-1].eventId, True)
        messages = [{"severity": str(m.severity), "description": m.description}
            for m in controller.GetDebugMessages()]
        assert not messages, messages
        for sample in samples:
            sample["resource"] = str(sample["resource"])
        report = {"success": quality_report is None or quality_report["passed"], "renderdocVersion": rd.GetVersionString(),
            "replayOptimisation": "NoOptimisation", "apiValidation": options.apiValidation,
            "contract": "Preparation6/Content3/Recording3/Inputs10",
            "opaqueDepthGraphicsVersion": 1 if foreground else 0,
            "scope": os.environ.get("ARP_CLOUD_CAPTURE_SCOPE", "ordinary mixed real cloud passes, vacuum oracle; FrameGraph integration remains separate"),
            "capture": path, "commandBuffers": sorted(command_buffers), "nativeCalls": calls,
            "descriptorBinds": sets, "imageBarriers": image_barriers,
            "bufferBarriers": buffer_barriers, "constantBindings": constants,
            "reconstructionPolicy": policy_binding,
            "reconstructionVersion": oracle.get("reconstructionVersion", 0) if oracle else 0,
            "refinedPixels": oracle.get("refinedPixels", 0) if oracle else 0,
            "samples": samples, "checkedHdrTexels": width * height, "checkedReadbackBytes": readback_bytes,
            "foregroundVertices": foreground_vertices, "foregroundTexels": sum(coverage),
            "viewports": viewports,
            "opaqueDistanceMaxError": max_opaque_error,
            "sequenceFrame": oracle["frame"] if sequence else None,
            "historyUsed": oracle["historyUsed"] if sequence else False,
            "orthographic": oracle.get("orthographic", False) if oracle else False,
            "opaqueRejected": oracle.get("opaqueRejected", 0) if oracle else 0,
            "disoccluded": oracle.get("disoccluded", 0) if oracle else 0,
            "quality": quality_report,
            "previousInput": previous_input, "previousMaxError": previous_error,
            "maxError": max(max_sample_error, max_hdr_error), "sampleMaxError": max_sample_error, "hdrMaxError": max_hdr_error, "debugMessages": messages, "image": image_path,
            "displayTransform": f"Reinhard then sRGB; nearest {scale}x enlargement"}
        root = Path(os.environ["ARP_REPO_ROOT"])
        selected = (
            "src/rhi/frontend/command_usage.h",
            "src/rhi/frontend/command_read_only_graphics.h",
            "src/rhi/semantics/opaque_depth_graphics.h",
            "src/rhi/frontend/command_render_pass_preparation.h",
            "src/rhi/backends/vulkan/vulkan_render_pass_storage.cpp",
            "src/rhi/backends/vulkan/vulkan_command_objects.cpp",
            "tests/integration/rhi_vulkan_original_graphics_tests.cpp",
            os.environ.get("ARP_CLOUD_CAPTURE_EXECUTABLE", "build/volumetric-cloud-debug/arp_rhi_vulkan_original_graphics_tests.exe"),
            "src/renderer/render_graph/original_frame_graph_execution.cpp",
            "src/renderer/volumetric_cloud/volumetric_cloud_prepared_graph.cpp",
            "src/renderer/volumetric_cloud/cloud_prepared_history.cpp",
            "src/renderer/volumetric_cloud/cloud_prepared_history.h",
            "src/renderer/volumetric_cloud/cloud_prepared_pipelines.cpp",
            "src/renderer/volumetric_cloud/cloud_prepared_pipelines.h",
            "src/renderer/volumetric_cloud/cloud_prepared_resources.cpp",
            "src/renderer/volumetric_cloud/cloud_prepared_resources.h",
            "src/renderer/volumetric_cloud/cloud_prepared_descriptors.cpp",
            "src/renderer/volumetric_cloud/cloud_prepared_descriptors.h",
            "src/renderer/volumetric_cloud/cloud_prepared_feature.h",
            "src/renderer/volumetric_cloud/detail/cloud_prepared_submission.h",
            "src/renderer/volumetric_cloud/detail/cloud_prepared_submission.cpp",
            "src/renderer/volumetric_cloud/volumetric_cloud_feature.h",
            "src/renderer/volumetric_cloud/volumetric_cloud_feature.cpp",
            "src/renderer/volumetric_cloud/cloud_frame_parameters.cpp",
            "src/renderer/volumetric_cloud/cloud_reference.cpp",
            "tests/fixtures/cloud/cloud_sequence_oracle.h",
            "tests/fixtures/cloud/cloud_quality_oracle.h",
            "src/renderer/volumetric_cloud/cloud_quality.h",
            "src/renderer/volumetric_cloud/cloud_quality.cpp",
            "tests/fixtures/cloud_foreground.slang",
            "tests/tools/analyze_ordinary_mixed_cloud_renderdoc.py",
            "shaders/renderer/cloud_common.slang",
            "shaders/renderer/cloud_ray_integration.slang",
            "shaders/renderer/cloud_history.slang",
            "shaders/renderer/cloud_composite_impl.slang",
            "shaders/renderer/cloud_spatial_resolve.slang",
            "shaders/renderer/cloud_spatial_composite.slang",
            "src/renderer/volumetric_cloud/cloud_reconstruction.h",
            "src/renderer/volumetric_cloud/cloud_gpu_contract.h",
            "shaders/renderer/cloud_raymarch.slang",
            "shaders/renderer/cloud_temporal.slang",
            "shaders/renderer/cloud_composite.slang",
        )
        if oracle_path:
            report["cpuOracle"] = oracle_path
            report["cpuOracleSha256"] = hashlib.sha256(Path(oracle_path).read_bytes()).hexdigest()
        report["selectedInputSha256"] = {
            name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in selected}
        with open(os.path.splitext(path)[0] + ".json", "w", encoding="utf-8") as destination:
            json.dump(report, destination, indent=2)
        # Preserve the actual output, API validation and all numeric evidence
        # when the frozen quality gate rejects a candidate. A rejected report
        # remains unsuccessful and the analyzer still exits with failure.
        assert report["success"], ("CLOUD-QUALITY-ACCEPTANCE", quality_report)
        if os.path.exists(path + ".error.txt"):
            os.remove(path + ".error.txt")
        checkpoint("Verified")
    finally:
        controller.Shutdown()
        cap.Shutdown()


if __name__ == "__main__":
    try:
        main()
        os._exit(0)
    except Exception:
        with open(os.environ["ARP_RENDERDOC_ANALYZE_CAPTURE"] + ".error.txt", "w", encoding="utf-8") as destination:
            destination.write(traceback.format_exc())
        os._exit(1)
