# OrdinaryOpaqueBinding/1

Status: Contract Complete for the authored Compute selected-entry query. Windows
and Wasm/Node validate original Native/Emulated package Sources. This is CPU
inspection evidence; no new execution or production capability.

WOCC frame/global sets contain ordinary IBL Cube, BRDF/GI 2D images, shadow depth
and comparison samplers in addition to material tables and buffers. Their
declarations already enter PipelineInterface/1, but the selected-entry resource
join must be shared before the backends can admit their public execution.

The original Root query authenticates the live Root, original immutable package,
the pipeline's actually selected Compute or Vertex/Fragment module and one active
ordinary scalar sampled-image/sampler declaration. It joins the exact set/binding
to the Root's retained Descriptor Source/2 and canonical resource source index.
It copies the declaration, selected stage, actual view or sampler fields,
resource/parent identity and registration generations. Image state access uses
the same parent/subresources as the existing Root state projection. Samplers
retain their resource identity but issue no buffer/texture state access.

Outputs are cleared on every failure. Foreign/retired owners, wrong/unselected
entry, inactive or missing binding, indexed-set request, buffer/storage image,
descriptor array and unknown version refuse. Queries allocate nothing and issue
no replacement resource graph, authorization, journal, retained owner or state
token. Source identity and generation cannot be supplied by the caller.

| Backend | Query mapping | Execution authority from this query |
| --- | --- | --- |
| Null | Original package/Root validation and copied metadata | None; validator only |
| Vulkan | Same L2 query over original Sources | None; native format/view/publication/content and completion stay independent |
| Metal | Same L2 query over original Sources | None; stage slots, argument ownership/residency/content and completion stay independent |
| WebGPU | Same L2 query over original Sources | None; scalar binding use, actual format/sampler/device limits/content and callback custody stay independent |

This query does not prove sampled operations or their equivalence. In particular,
a depth declaration and comparison sampler are not evidence of comparison use;
filterable intent is not a queried format feature. No ordinary image executor is
opened until the complete original resource/state/prepared-binding/completion
path is implemented and verified. Existing scalar-buffer and indexed-image
execution profiles preserve their refusal scope.

Validation: actual Native/Emulated package original-Root queries2/2 on Windows
and2/2 in Wasm/Node, including seven genuine retained resources, forged/wrong
entry/indexed/buffer/version refusals, shared-quota hot0 queries, output clearing
and queries against the real usage-retained Root after device retirement. The
focused compiler/resealed-declaration contract passes1/1; SPIR-V declarations4/4,
registered Null hidden-frame/HTTP-port smoke2/2, source421/421, style6/6 and the
pinned formatter pass. Failed fixture/driver runs are retained.

Positive Graphics ordinary opaque queries and current Apple compilation are not
claimed. Ordinary Graphics image payload/content/publication, sampler use, queried
format/device budgets, state/completion custody and actual cross-backend GPU
execution remain the next required engine work. Evidence:
`build/bindless-ordinary-opaque-root1/current-report.json`.
