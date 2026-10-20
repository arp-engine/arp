# RenderOutputObservation/1

P4-20 retires legacy counter-derived render-output success claims. Actual queue,
render-pass, resource, acquire/present, resize and lifecycle counters retain their
meaning. A hash of those counters or surface dimensions is not a hash of output
pixels. Existing output-hash fields remain source-compatible but false/zero,
with `RHI-OUTPUT-ORIGINAL-OBSERVATION-UNAVAILABLE` when no original observation exists.

The production-submit validation facade still requires output evidence. If
native work has already been accepted, the report retains all execution facts
and fails output validation; it never retries, rolls back, fabricates pixels or
promotes readiness. Custom-graph submission success continues to mean accepted
native execution, with independent false output-evidence fields. Metal's actual
buffer/texture-transfer readback checks remain separate from rendered output.

| Backend | Ordinary legacy output reports | Native execution/readback |
|---|---|---|
| Null | No output evidence | Validator only |
| Vulkan | Counter hash removed; false/zero | Original execution counters retained; borrowed texture readback remains retired |
| Metal | Descriptor/present hash removed; false/zero | Actual Apple transfer-readback facts retained; host/stub is not Apple validation |
| WebGPU | Command/canvas hash removed; false/zero | Existing private original output path is unchanged and is not imported into these reports |
| WebGL | No legacy hash promotion | OriginalConformanceOutput/1 cold opt-in seals actual native pixels; ordinary production capability remains closed |

Fast contracts cover absent evidence and source barriers against counter-hash
reintroduction. Null hidden-frame and true Vulkan execution smoke distinguish
accepted work from missing output. Apple/WebGPU native validation is reported
at its actual host/platform boundary. RenderDoc captures remain external visual
evidence and cannot populate these L2 report fields or grant execution authority.
