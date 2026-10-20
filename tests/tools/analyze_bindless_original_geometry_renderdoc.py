"""Replay genuine indexed RootGeometry draws and inspect exact fetched inputs."""
import hashlib
import json
import os
import traceback
import renderdoc as rd

output = os.environ["ARP_BINDLESS_CAPTURE_OUTPUT"]
package_dir = os.environ["ARP_BINDLESS_GEOMETRY_PACKAGE_DIR"]
expected_submissions = int(os.environ.get("ARP_BINDLESS_GEOMETRY_SUBMISSIONS", "1"))
report = {"scope": "private original indexed geometry; vertex/instance clean/fault",
          "submissionsPerCase": expected_submissions}
capture = None
controller = None
try:
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ["ARP_BINDLESS_CAPTURE_PATH"], "", None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status.code != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))

    def walk(nodes):
        for action in nodes:
            yield action
            yield from walk(action.children)

    def shader_hash(path):
        with open(path, "rb") as source:
            return hashlib.sha256(source.read()).hexdigest()

    # Replay the capture once to index actual shader bytes. Replaying every
    # action for each family made this small regression exceed the deadline.
    draws_by_shader = {}
    for action in walk(controller.GetRootActions()):
        if not action.flags & rd.ActionFlags.Drawcall:
            continue
        controller.SetFrameEvent(action.eventId, True)
        state = controller.GetPipelineState()
        vertex = state.GetShaderReflection(rd.ShaderStage.Vertex)
        fragment = state.GetShaderReflection(rd.ShaderStage.Fragment)
        if vertex is not None and fragment is not None:
            key = (hashlib.sha256(bytes(vertex.rawBytes)).hexdigest(),
                   hashlib.sha256(bytes(fragment.rawBytes)).hexdigest())
            draws_by_shader.setdefault(key, []).append(action)

    def matching_draws(vertex_hash, fragment_hash):
        return draws_by_shader.get((vertex_hash, fragment_hash), [])

    draws = []
    for mode in ("Vertex", "Instance"):
        family = "RootGeometry" + mode + ".native."
        vertex_hash = shader_hash(os.path.join(package_dir, family + "vertexMain.spv"))
        for entry, fault in (("fragmentMain", 0), ("faultFragment", 1)):
            fragment_hash = shader_hash(os.path.join(package_dir, family + entry + ".spv"))
            matches = matching_draws(vertex_hash, fragment_hash)
            if len(matches) != expected_submissions:
                raise RuntimeError(mode + "/" + entry + " indexed draw count mismatch: " + str(len(matches)))
            original_target = None
            for reuse, action in enumerate(matches):
                controller.SetFrameEvent(action.eventId, True)
                state = controller.GetPipelineState()
                if not action.flags & rd.ActionFlags.Indexed or (action.numIndices, action.numInstances,
                    action.indexOffset, action.vertexOffset, action.instanceOffset) != (3, 1, 0, 0, 1):
                    raise RuntimeError("indexed draw fields differ from authored firstInstance1")
                vertices = state.GetVBuffers()
                index = state.GetIBuffer()
                if len(vertices) != 1 or (vertices[0].byteOffset, vertices[0].byteStride) != (16, 16) or \
                   (index.byteOffset, index.byteStride) != (2, 2):
                    raise RuntimeError("bound geometry offset/stride mismatch")
                vertex_data = bytes(controller.GetBufferData(vertices[0].resourceId, 0, 64))
                index_data = bytes(controller.GetBufferData(index.resourceId, 0, 8))
                if index_data != bytes([255, 255, 0, 0, 2, 0, 1, 0]):
                    raise RuntimeError("initialized index bytes mismatch")
                import struct
                expected_vertices = struct.pack("<16f", 99,99,99,99,-1,-1,0,1,3,-1,0,1,-1,3,0,1)
                if vertex_data != expected_vertices:
                    raise RuntimeError("initialized vertex bytes mismatch")
                targets = [target for target in state.GetOutputTargets() if target.resource != rd.ResourceId.Null()]
                if len(targets) != 1:
                    raise RuntimeError("requires one original color attachment")
                if original_target is None:
                    original_target = targets[0].resource
                elif original_target != targets[0].resource:
                    raise RuntimeError("repeated submission replaced the original attachment")
                pixels = bytes(controller.GetTextureData(targets[0].resource, rd.Subresource(0, 0, 0)))
                expected = bytes(16) if fault else bytes([32,128,64,255,16,40,32,255] * 2)
                if pixels != expected:
                    raise RuntimeError(mode + "/" + entry + " color mismatch: " + str(list(pixels)))
                image = os.path.join(output, "root-geometry-" + mode.lower() + "-" + entry + "-" + str(reuse) + ".png")
                save = rd.TextureSave()
                save.resourceId = targets[0].resource
                save.destType = rd.FileType.PNG
                controller.SaveTexture(save, image)
                draws.append(dict(mode=mode, entry=entry, completedReuse=bool(reuse), eventId=action.eventId, vertexSpirvSha256=vertex_hash,
                    fragmentSpirvSha256=fragment_hash, outputResourceId=str(targets[0].resource), firstInstance=action.instanceOffset, pixels=list(pixels),
                    vertexBytes=vertex_data.hex(), indexBytes=index_data.hex(), image=image))
    if os.environ.get("ARP_BINDLESS_BUFFER_GRAPHICS_REPLAY") == "1":
        buffer_draws = []
        for family, count in (("RootGraphicsBuffers", 1), ("RootGraphicsRecords", 2)):
            vertex_hash = shader_hash(os.path.join(package_dir, family + ".native.vertexMain.spv"))
            for entry in ("cleanFragment", "faultFragment"):
                fragment_hash = shader_hash(os.path.join(package_dir, family + ".native." + entry + ".spv"))
                matches = matching_draws(vertex_hash, fragment_hash)
                if len(matches) != count:
                    raise RuntimeError(family + "/" + entry + " draw count mismatch: " + str(len(matches)))
                for ordinal, action in enumerate(matches):
                    controller.SetFrameEvent(action.eventId, True)
                    if action.flags & rd.ActionFlags.Indexed or (action.numIndices, action.numInstances,
                            action.vertexOffset, action.instanceOffset) != (3, 1, 0, 0):
                        raise RuntimeError("scalar Graphics draw fields mismatch")
                    targets = [target for target in controller.GetPipelineState().GetOutputTargets()
                               if target.resource != rd.ResourceId.Null()]
                    if len(targets) != 1:
                        raise RuntimeError("scalar Graphics requires one color target")
                    pixels = bytes(controller.GetTextureData(targets[0].resource, rd.Subresource(0, 0, 0)))
                    # Authored colors are (0.25, 0.5, 0.75, 1) and their
                    # reversal. Only 0.5 * 255 lies exactly between two
                    # UNORM codes; accept those two complete images, with
                    # every other channel exact and one consistent tie rule.
                    expected = tuple(bytes([64,g,191,255,191,g,64,255] * 2)
                                     for g in (127, 128))
                    if pixels not in expected:
                        raise RuntimeError(family + "/" + entry + " scalar Graphics pixels: " + str(list(pixels)))
                    path = os.path.join(output, family + "-" + entry + "-" + str(ordinal) + ".png")
                    save = rd.TextureSave()
                    save.resourceId = targets[0].resource
                    save.destType = rd.FileType.PNG
                    controller.SaveTexture(save, path)
                    buffer_draws.append(dict(family=family, entry=entry, ordinal=ordinal, eventId=action.eventId,
                        vertexSpirvSha256=vertex_hash, fragmentSpirvSha256=fragment_hash, pixels=list(pixels), image=path))
        report["scalarBufferDraws"] = buffer_draws
    report["draws"] = draws
    report["validationMessages"] = [message.description for message in controller.GetDebugMessages()]
    if report["validationMessages"]:
        raise RuntimeError("Vulkan validation messages")
    report["success"] = True
except Exception:
    report["success"] = False
    report["error"] = traceback.format_exc()
finally:
    with open(os.path.join(output, "root-geometry-renderdoc-report.json"), "w") as destination:
        json.dump(report, destination, indent=2)
    if controller:
        controller.Shutdown()
    if capture:
        capture.Shutdown()
    os._exit(0 if report.get("success") else 1)
