"""Replay the graph resource smoke in qrenderdoc; inspect native masks and pixels."""
import json
import os
import traceback
import renderdoc as rd

root = os.environ["ARP_GRAPH_SYNC_CAPTURE_OUTPUT"]
report = {"scope": "graph-barrier-lowering-and-clear-output-not-bindless-shader-execution"}
capture = None
controller = None
try:
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.path.join(root, "graph_capture.rdc"), "", None)
    assert status.code == rd.ResultCode.Succeeded, str(status)
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    assert status.code == rd.ResultCode.Succeeded, str(status)
    structured = controller.GetStructuredFile()

    def children(node):
        return {node.GetChild(i).name: node.GetChild(i) for i in range(node.NumChildren())}

    def number(node):
        return int(node.data.basic.u)

    barriers = []
    for chunk in structured.chunks:
        if chunk.name != "vkCmdPipelineBarrier":
            continue
        fields = children(chunk)
        entry = {"sourceStages": number(fields["srcStageMask"]),
                 "destinationStages": number(fields["destStageMask"]), "memory": [], "images": [], "buffers": []}
        for source, dest in (("pMemoryBarriers", "memory"), ("pImageMemoryBarriers", "images"),
                             ("pBufferMemoryBarriers", "buffers")):
            array = fields[source]
            for i in range(array.NumChildren()):
                item = children(array.GetChild(i))
                entry[dest].append({name: number(item[name]) for name in
                    ("srcAccessMask", "dstAccessMask", "oldLayout", "newLayout") if name in item})
        barriers.append(entry)
    report["barriers"] = barriers
    # Vulkan specification bits: VERTEX_SHADER=0x8, COMPUTE_SHADER=0x800,
    # COLOR_ATTACHMENT_WRITE=0x100, SHADER_READ=0x20. State-derived image
    # lowering would silently replace the authored vertex scope with compute.
    report["explicitVertexImageBarriers"] = sum(
        bool(b["destinationStages"] & 0x8) and not (b["destinationStages"] & 0x800)
        and any(i["dstAccessMask"] == 0x20 for i in b["images"]) for b in barriers)
    report["sameStateAttachmentDependencies"] = sum(
        any(m["srcAccessMask"] & 0x100 and m["dstAccessMask"] & 0x100
            for m in b["memory"]) for b in barriers)
    assert report["explicitVertexImageBarriers"] == 4
    assert report["sameStateAttachmentDependencies"] == 4
    # DRAW_INDIRECT=0x2, INDIRECT_COMMAND_READ=0x1, FRAGMENT_SHADER=0x80.
    report["persistentBufferIndirectDependencies"] = sum(
        bool(b["destinationStages"] & 0x2) and bool(b["sourceStages"] & 0x80)
        and any(i["dstAccessMask"] == 0x1 and i["srcAccessMask"] & 0x40
                for i in b["buffers"]) for b in barriers)
    assert report["persistentBufferIndirectDependencies"] == 4

    actions = []
    def walk(nodes):
        for action in nodes:
            actions.append(action)
            walk(action.children)
    walk(controller.GetRootActions())
    # AliasClearPreservation/1: this fixture has clear-only attachment passes.
    # Bind facts and output identities, never debug names, establish the alias.
    image_bindings = {}
    for chunk in structured.chunks:
        assert chunk.name not in ("vkBindImageMemory2", "vkBindImageMemory2KHR"), \
            "AliasClearPreservation/1 has no bind-memory2 schema"
        if chunk.name != "vkBindImageMemory":
            continue
        fields = children(chunk)
        image = number(fields["image"])
        assert image not in image_bindings, "duplicate native image binding"
        image_bindings[image] = {name: number(fields[name])
                                for name in ("device", "memory", "memoryOffset")}
    textures = {texture.resourceId: texture for texture in controller.GetTextures()}
    attachment_passes = [action for action in actions
                         if action.GetName(structured).startswith("vkCmdBeginRenderPass(")]
    assert len(attachment_passes) == 16, "fixture requires four passes in each of four frames"
    report["aliasClearPreservation"] = {"contract": "AliasClearPreservation/1", "frames": []}
    report["actualDrawCount"] = sum(bool(action.flags & rd.ActionFlags.Drawcall) for action in actions)
    assert report["actualDrawCount"] == 0, "clear-only fixture scope changed"

    def output_ids(action, count):
        controller.SetFrameEvent(action.eventId, True)
        outputs = [output.resource for output in controller.GetPipelineState().GetOutputTargets()
                   if output.resource != rd.ResourceId.Null()]
        assert len(outputs) == count, "unexpected attachment layout"
        assert all(resource in textures for resource in outputs)
        return outputs

    def rgba_observation(resource, action, expected, extent, filename):
        controller.SetFrameEvent(action.eventId, True)
        texture = textures[resource]
        assert texture.format.Name() == "R8G8B8A8_UNORM"
        assert (texture.width, texture.height, texture.depth, texture.mips, texture.arraysize) \
            == (extent, extent, 1, 1, 1)
        data = bytes(controller.GetTextureData(resource, rd.Subresource(0, 0, 0)))
        assert len(data) == extent * extent * 4
        assert data == data[:4] * (extent * extent), "nonuniform clear/load output"
        assert all(abs(actual - wanted) <= 1 for actual, wanted in zip(data[:4], expected)), \
            "clear/load color mismatch"
        save = rd.TextureSave()
        save.resourceId = resource
        save.destType = rd.FileType.PNG
        status = controller.SaveTexture(save, os.path.join(root, filename))
        assert status.code == rd.ResultCode.Succeeded, str(status)
        return {"event": action.eventId, "resource": int(resource),
                "width": extent, "height": extent, "rgba": list(data[:4]),
                "bytes": len(data), "uniform": True, "png": filename}

    for frame in range(4):
        produce_a, preserve_c, produce_b, composite = attachment_passes[frame * 4:frame * 4 + 4]
        a, c = output_ids(produce_a, 2)
        assert output_ids(preserve_c, 1) == [c]
        b, = output_ids(produce_b, 1)
        final, = output_ids(composite, 1)
        assert textures[final].format.Name() == "B8G8R8A8_UNORM"
        assert (textures[final].width, textures[final].height) == (320, 180)
        assert len({a, b, c}) == 3
        native = {role: image_bindings[int(resource)] for role, resource in (("A", a), ("B", b), ("C", c))}
        assert native["A"] == native["B"], "A/B do not share original device/memory/offset"
        assert native["A"]["device"] == native["C"]["device"] != 0
        assert native["A"]["memory"] != native["C"]["memory"] != 0
        assert native["A"]["memory"] != 0
        extent = 1 if frame == 3 else 64
        observations = {}
        for role, resource, action, expected in (
                ("A", a, produce_a, (32, 64, 128, 255)),
                ("C", c, produce_a, (128, 64, 32, 255)),
                ("C-load", c, preserve_c, (128, 64, 32, 255)),
                ("B", b, produce_b, (64, 128, 32, 255)),
                ("C-after-B", c, produce_b, (128, 64, 32, 255))):
            observations[role] = rgba_observation(
                resource, action, expected, extent, "alias-frame%d-%s.png" % (frame, role))
        report["aliasClearPreservation"]["frames"].append(
            {"frame": frame, "bindings": native, "observations": observations})


    controller.SetFrameEvent(max(a.eventId for a in actions), True)
    names = {r.resourceId: r.name for r in controller.GetResources()}
    images = [t for t in controller.GetTextures() if names[t.resourceId].startswith("Swapchain Image")]
    report["presentedImages"] = []
    for texture in images:
        assert texture.format.Name() == "B8G8R8A8_UNORM"
        data = bytes(controller.GetTextureData(texture.resourceId, rd.Subresource(0, 0, 0)))
        expected = bytes((191, 128, 32, 255))
        # Every swapchain image has been acquired/cleared in the four-frame smoke.
        report["presentedImages"].append({"name": names[texture.resourceId], "bgra": list(data[:4]), "bytes": len(data)})
        # UNORM conversion permits the adjacent quantized value at 0.5.
        assert data == data[:4] * (texture.width * texture.height)
        assert all(abs(a - b) <= 1 for a, b in zip(data[:4], expected))
        save = rd.TextureSave()
        save.resourceId = texture.resourceId
        save.destType = rd.FileType.PNG
        controller.SaveTexture(save, os.path.join(root, "present.png"))
    assert images
    report["validationMessages"] = [m.description for m in controller.GetDebugMessages()]
    assert not report["validationMessages"]
    report["success"] = True
except Exception:
    report["error"] = traceback.format_exc()
    report["success"] = False
finally:
    with open(os.path.join(root, "capture-report.json"), "w") as output:
        json.dump(report, output, indent=2)
    if controller:
        controller.Shutdown()
    if capture:
        capture.Shutdown()
    os._exit(0 if report.get("success") else 1)
