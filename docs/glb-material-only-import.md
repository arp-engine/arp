# GLB material-only import

`assets::importGlbMaterialsFromFile()` reads GLB chunks, material metadata,
texture/image references and owned encoded image payloads through the same
shared parsers used by static/named mesh import. It preserves glTF material
indices and the `arpLeafSurface/v1` contract. The result owns its data after
the file/chunk buffers are released; the outer result containers use core PMR.

This API validates material assets, not drawable scene geometry. It does not
expand vertex accessors, walk node transforms, generate tangents or validate
mesh topology. A renderer must separately resolve a ready MeshAssetResource.
It is appropriate when geometry is already loaded and only GPU material
creation remains. No GPU ABI, texture sampling/color-space rules, image
decoder, backend capability or rendering fallback is changed.

Missing files diagnose `ASSET-GLB-MATERIALS-FILE-READ-FAILED`. Invalid JSON roots
diagnose `ASSET-GLB-MATERIALS-JSON-INVALID`. Chunk and declared material metadata
errors preserve the shared parser's diagnostics, including invalid leaf extras.
There is no alternate parser or procedural material substitution on failure.

WOCC's assertion-enabled `arp_wocc_glb_material_import_tests` checks
full-import equality of texture bindings, encoded bytes, PBR/alpha/leaf data,
rejection of invalid declared metadata and deliberate independence from invalid
geometry accessors. WOCC's Vulkan fixed-camera capture checks the consuming
renderer. The CPU parser is shared across platforms; Windows execution is
validated, while Web/macOS runtime images remain unverified.
