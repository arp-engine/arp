# VegetationTemplateAddress/1

An authored family is addressed by `familyTemplateBase + variant + lod *
familyLodStride`. Registry metadata at that address must match the requested
category, variant and LOD. Populate LODs outermost and variants innermost.
Never interpret a variant-major registry with LOD-major address arithmetic.
Validate authored metadata before publication; a missing/misaligned entry
fails with `WOCC-VEGETATION-TEMPLATE-ADDRESS-MISMATCH`.

This is backend-independent CPU resource admission. Null, Vulkan, Metal and
WebGPU consume the same validated templates and require no native changes.
Fast address tests cover three variants and three LODs, including the previous
transposed order. The normal native scene smoke validates the actual registry.
