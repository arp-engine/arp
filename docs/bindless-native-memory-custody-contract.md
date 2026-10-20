# Original Native buffer member memory /1

Status: Contract Complete; source custody validated on Windows, actual Mac and
Wasm/Node. No new execution grant.

PackageMemoryLoadRequest/1 keeps its Emulated-only SPIR-V/WGSL behavior.
PackageMemoryLoadRequest/2 is Native-only and requires an explicit
NativeBufferMemoryLoadRequest/1 for each requested entry. Mixed/hidden target
requests refuse. The existing package loader validates entry identity, hashes,
binary/reflection correspondence, complete ordinary-buffer coverage and
cross-stage memory identity before moving the same parsed source graph into
its immutable module. Every target graph ceiling and relation rule is copied
into that module under the existing core metadata quota. Borrowed request
pointers never survive synchronous loading.

Source matrix rules must name reachable original matrix types; duplicate,
missing, unused or invalid source rules refuse without a package publication.
Target rule identity remains intent until independently checked against the
selected compiled native function graph. An original package/member lookup
authenticates the shared owner and module address before dereferencing it.
Copied metadata has no authority and old WGSL memory inspection remains closed
for Native packages.

| Backend | Source custody and target evidence |
| --- | --- |
| Null | May validate/freeze Native intent. Supplies no MSL driver evidence or GPU execution. |
| Vulkan | May retain portable compiler intent. Native SPIR-V evidence remains distinct from MSL proof. |
| Metal | Must join frozen selected module, exact MSL bytes/function, queried member graph, physical binding/stage and original pipeline before issuing a member-memory result. |
| WebGPU | Native requests refuse on Emulated packages. Existing /1 WGSL proof remains required. |

The original Root must additionally resolve the same canonical original buffer,
range and generation before a pipeline member-memory result can be consumed.
No caller tree, total-size result, arbitrary pipeline or matching reflection can
substitute for these original-owner joins. Existing recording/completion/
retirement, allocator and budget mechanisms remain authoritative.

Verified with the selected-pipeline increment in
`build/bindless-native-pipeline-memory1/current-report.json`: Windows5/5,
Mac6/6, Wasm/Node1/1, source418/418 and style6/6. Tests cover Native/Emulated
request separation, complete binding coverage, missing/invalid matrix rules,
source traversal budgets, foreign module/control-block ownership, caller intent
poisoning and allocation-free lookup with zero residual quota. Actual selected
Metal pipeline comparison now has its own contract; the public Root member
query remains open. See `docs/bindless-native-pipeline-memory-contract.md`.
