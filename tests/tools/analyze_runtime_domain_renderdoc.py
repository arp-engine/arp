"""qrenderdoc --python: inspect fixed-input terrain/vegetation migration captures."""

import collections
import hashlib
import json
import os
import traceback

import renderdoc as rd


class RuntimeDomainCaptureAnalysis:
    def __init__(self, capture, output):
        self.capture = capture
        self.output = output
        self.report = {}
        self.file = None
        self.controller = None

    @staticmethod
    def flatten(actions):
        result = []
        for action in actions:
            result.append(action)
            result.extend(RuntimeDomainCaptureAnalysis.flatten(action.children))
        return result

    def run(self):
        os.makedirs(self.output, exist_ok=True)
        self.file = rd.OpenCaptureFile()
        status = self.file.OpenFile(self.capture, "", None)
        assert status.code == rd.ResultCode.Succeeded, str(status)
        status, self.controller = self.file.OpenCapture(rd.ReplayOptions(), None)
        assert status.code == rd.ResultCode.Succeeded, str(status)
        self.report["replay"] = str(status)
        actions = self.flatten(self.controller.GetRootActions())
        draws = [a for a in actions if a.flags & rd.ActionFlags.Drawcall]
        self.report["drawCount"] = len(draws)
        self.report["dispatchCount"] = sum(bool(a.flags & rd.ActionFlags.Dispatch) for a in actions)
        entries = collections.Counter()
        signatures = []
        buffer_hashes = {}
        target = None
        for action in draws:
            self.controller.SetFrameEvent(action.eventId, True)
            pipeline = self.controller.GetPipelineState()
            vertex = pipeline.GetShaderReflection(rd.ShaderStage.Vertex)
            fragment = pipeline.GetShaderReflection(rd.ShaderStage.Fragment)
            entry = vertex.entryPoint if vertex else "<none>"
            entries[entry] += 1
            buffers = []
            for buffer in pipeline.GetVBuffers():
                if buffer.resourceId == rd.ResourceId.Null():
                    continue
                # Inspect immutable geometry/instance input, bounded to 256 KiB
                # per unique range. This is evidence, not a full-resource hash.
                count = min(int(buffer.byteSize), 256 * 1024)
                key = (str(buffer.resourceId), int(buffer.byteOffset), count)
                if key not in buffer_hashes:
                    data = bytes(self.controller.GetBufferData(buffer.resourceId, buffer.byteOffset, count))
                    buffer_hashes[key] = hashlib.sha256(data).hexdigest()
                buffers.append([int(buffer.byteStride), count, buffer_hashes[key]])
            signature = {"vertex": entry, "fragment": fragment.entryPoint if fragment else None,
                               "indices": int(action.numIndices), "instances": int(action.numInstances),
                               "buffers": buffers}
            # Hash only the initialized records actually consumed by this draw.
            # The larger diagnostic buffer sample can include unused capacity.
            vertex_buffers = pipeline.GetVBuffers()
            if entry == "vertexMainInstanced" and len(vertex_buffers) > 1:
                instance_buffer = vertex_buffers[1]
                assert instance_buffer.byteStride == 64
                offset = int(instance_buffer.byteOffset) + int(action.instanceOffset) * 64
                size = int(action.numInstances) * 64
                # RenderDoc length=0 means "remaining buffer", while a draw
                # with zero instances consumes no records.
                records = bytes(self.controller.GetBufferData(instance_buffer.resourceId, offset, size)) if size else b""
                if len(records) != size:
                    self.report["instanceRangeDiagnostic"] = {
                        "event": int(action.eventId), "offset": offset, "requested": size,
                        "returned": len(records), "bindingOffset": int(instance_buffer.byteOffset),
                        "bindingSize": int(instance_buffer.byteSize), "firstInstance": int(action.instanceOffset),
                        "resource": str(instance_buffer.resourceId)}
                    raise RuntimeError("instance range requires explicit interpretation: " + str(self.report["instanceRangeDiagnostic"]))
                digest = hashlib.sha256(records).hexdigest()
                signature["instanceOffset"] = int(action.instanceOffset)
                signature["consumedInstanceHash"] = digest
                with open(os.path.join(self.output, "instances-" + digest + ".bin"), "wb") as stream:
                    stream.write(records)
            signatures.append(signature)
            targets = pipeline.GetOutputTargets()
            if targets and targets[0].resource != rd.ResourceId.Null():
                target = targets[0].resource
        self.report["vertexEntries"] = dict(entries)
        self.report["drawSignatures"] = signatures
        assert target is not None and len(draws) > 0
        data = bytes(self.controller.GetTextureData(target, rd.Subresource(0, 0, 0)))
        self.report["finalColorBytes"] = len(data)
        self.report["finalColorSha256"] = hashlib.sha256(data).hexdigest()
        with open(os.path.join(self.output, "final-color.bin"), "wb") as stream:
            stream.write(data)
        save = rd.TextureSave()
        save.resourceId = target
        save.destType = rd.FileType.PNG
        self.controller.SaveTexture(save, os.path.join(self.output, "final-color.png"))
        self.report["messages"] = [m.description for m in self.controller.GetDebugMessages()]
        self.report["success"] = True

    def close(self):
        if self.controller:
            self.controller.Shutdown()
        if self.file:
            self.file.Shutdown()


analysis = RuntimeDomainCaptureAnalysis(os.environ["ARP_RUNTIME_DOMAIN_CAPTURE"],
                                      os.environ["ARP_RUNTIME_DOMAIN_CAPTURE_OUTPUT"])
try:
    analysis.run()
except Exception:
    analysis.report["success"] = False
    analysis.report["error"] = traceback.format_exc()
finally:
    os.makedirs(analysis.output, exist_ok=True)
    with open(os.path.join(analysis.output, "report.json"), "w") as stream:
        json.dump(analysis.report, stream, indent=2)
    analysis.close()
    os._exit(0 if analysis.report.get("success") else 1)
