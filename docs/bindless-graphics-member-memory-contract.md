# Original Graphics member memory /1 and /2

Status: Contract Complete; Backend/Platform Validated for authored static
320+16-byte Graphics buffers on Vulkan, Metal and WebGPU, with Null validation.
Member inspection and explicit-request pre-record admission only; no new
resource-content, completion or execution capability.

`ShaderBufferMemberMemory` is copied, backend-neutral evidence of member-memory
correspondence for one selected ordinary static buffer. It contains source
occupied bytes, target minimum bytes and an explicit evidence kind. Equal byte
counts alone never produce it. Runtime arrays and descriptor arrays refuse /1.

The pipeline query uses only factory-frozen selected modules and original
declaration/target facts. The public Root query first performs the existing
original Graphics range resolution, then joins the selected-entry proof to that
same canonical buffer, range, stage and live generation. It publishes facts,
access and source index together only after every check succeeds. Unknown
versions, wrong/unselected entries, missing proof, undersized ranges, foreign
control blocks and retired Roots clear every output. Queries allocate nothing,
perform no native calls and retain no new owners.

The existing mandatory pre-record Graphics buffer admission also consumes this
Root query for modules carrying explicit Native source/rule requests. Failed
member proof cannot publish access/state plans or record the requested work.
Unrequested legacy paths retain their existing scope and issue no such proof.

| Backend | Evidence and refusal |
| --- | --- |
| Null | Validator-only `ValidatedTargetMembers` from the existing original Emulated SPIR-V/WGSL comparison. Native source/rules alone refuse; no driver/GPU proof. |
| Vulkan | `NativeBinaryIdentity` only for the original Native package/module whose exact SPIR-V bytes were consumed by native shader-module creation. No MSL proof or fabricated target graph. |
| Metal | `ValidatedTargetMembers` only when the existing original pipeline has a requested Native source graph, frozen relation and independently queried/compared selected-function member graph at the same physical stage/slot. Range-only DriverQuery and unrequested legacy entries refuse. |
| WebGPU | `ValidatedTargetMembers` from the original selected Emulated module's approved SPIR-V/WGSL comparison, with the existing issued pipeline stage. Native requests refuse. |

WebGL1, unknown backends, ordinary pipelines and unsupported profiles refuse.
The old WGSL-specific inspection and range-only APIs keep their boundaries.
No shader arithmetic, indexed256 skinning, sampling, lifetime, budget or common
production gate may be inferred from these metadata queries.

Validation: Windows4/4, actual Mac6/6, Wasm/Node1/1, Chrome22 lifecycle flows,
22 member receipts and4 numeric receipts. Fresh Vulkan independent replay
confirms32 exact lanes, color/fault and zero validation messages. Actual Metal
submitted capture12 files,8622 frozen source inputs/native artifact hashes,
source418/418, style6/6 and formatter13 pass. Evidence:
`D:/arp/build/bindless-graphics-member-root2/current-report.json`.
Subsequent authored indexed256 palette position arithmetic now passes public
Vulkan/Metal/WebGPU execution, including the same member admission. Evidence:
`D:/arp/build/bindless-production-skinning256-web1/current-report.json`.
This does not broaden query1 to runtime/descriptor arrays. Production material/
instance tables, complete skinning/pass semantics and the common gate remain open.

## Runtime ordinary member query /2

Status: Contract Complete; Backend/Platform Validated for existing GraphicsDomains
runtime uint buffers on Vulkan, actual Metal and Chrome WebGPU. Production material/instance record layout and arithmetic remain open.

Query2 retains the same selected-module, original Root, canonical range and
generation joins. It adds source minimum bytes and runtime tail offsets/strides.
A pipeline reports declaration facts only (element capacity zero); only the Root
copies capacity from the existing Range2 resolution after every fact agrees.
One complete stride must fit. A zero-byte static prefix is legal for a pure
runtime buffer; descriptor arrays remain refused. Static buffers also accept /2.
Query1 retains its static-only scope and zeroes all added fields.

Null uses the original Emulated target member comparison as validator evidence;
Vulkan uses the exact selected SPIR-V native identity; WebGPU uses the original
validated SPIR-V/WGSL relation and issued pipeline. Metal /2 requires the original
Native request2, explicit Value/RuntimeElement source form, independently queried
selected pointer graph/dataSize/alignment and full member/stride correspondence
at the same physical stage/slot. Missing requests, unsupported source framing,
unknown pointee facts or failed comparisons refuse before publication. Static
Metal request1 also accepts query2; query1 retains static-only scope. WebGL and
unknown versions/backends refuse.

No query grants execution. Mandatory explicit Native member admission selects
/2 only for runtime buffers, so missing Metal proof fails before publication.
Failures clear every output, including runtime facts/capacity; hot inspection
allocates nothing, performs no driver queries and creates no owners. Validation
must cover old /1 refusal, exact/undersized ranges, wrong module/owner/stage,
retired generations, pipeline capacity zero and canonical Root capacity.

Windows4/4, actual Mac5/5 and Wasm/Node3/3 pass. Null covers exact16-byte and
undersized3-byte ranges; Vulkan actual public Graphics clean/fault execution
preserves the existing exact output and real completion/retirement. Actual Chrome
composed Compute/Graphics passes44 workflows/8 numeric receipts, including22
runtime member2 receipts; static320+16 regression passes22 flows/4 receipts.
Both browser reports have empty diagnostics/nativeErrors. Source418/418,
style6/6 and formatter13 pass. Mac8631 source inputs and36 native artifacts
are independently checked; unrelated live editor changes are recorded.
Evidence: `D:/arp/build/bindless-runtime-member2-1/current-report.json`.

The newer Metal checkpoint closes the selected UInt runtime element relation:
`D:/arp/build/bindless-metal-runtime-member2-1/current-report.json`.
Windows4/4, actual Mac7/7 and Wasm/Node3/3 pass. Mac20 workflows include16 genuine
submissions with exact875/876/875/876 output, fault0/1, hot0 and zero retirement.
Native request2 source custody, forged owner/legacy refusal and invalid source
forms are tested on all three host runtimes. Driver graph budgets and explicit
form mismatch reject without publishing a pipeline; source pair exhaustion fails
earlier during original load. A submitted-only capture contains10 files. All8632
Mac source inputs and38 native artifacts match the recorded epoch. Current rebuilt
Chrome composed44 workflows/8 numeric receipts and22 runtime member receipts
pass with empty diagnostics/nativeErrors. No independent
Metal replay or common production gate is claimed. Production material/instance
record driver layout/arithmetic, required operations/budgets and complete platform
lifecycle remain open. Prior Vulkan/Chrome runtime evidence retains its own epoch.
