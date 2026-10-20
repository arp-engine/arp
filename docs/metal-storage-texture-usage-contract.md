# MetalStorageTextureUsage/1

L2 TextureUsage::Storage authorizes a storage descriptor, independently of
TextureUsage::Sampled. Resource usage and shader binding access are separate:
the backend allocation must satisfy the selected shader's native signature,
including Slang RWTexture parameters lowered to Metal access::read_write even
when a particular entry point only writes or leaves that parameter unused.
This does not grant Sampled descriptor permission to a Storage-only resource,
change shader access validation, or introduce an optional capability.

| Backend | Allocation lowering and access boundary |
| --- | --- |
| Null | Validate Storage usage for storage descriptors; no native claim |
| Vulkan | VK_IMAGE_USAGE_STORAGE_BIT permits native storage loads/stores; pipeline/shader access and barriers remain mandatory |
| Metal | Storage maps to MTLTextureUsageShaderRead plus ShaderWrite; Sampled maps to ShaderRead only; both ordinary and placed textures use the existing shared descriptor builder |
| WebGPU | WGPUTextureUsage_StorageBinding; existing write-only storage layout/shader contract remains unchanged. This allocation mapping does not claim read-write storage binding support; incompatible shader access must fail pipeline validation/creation |

No Sampled bit is added to L2 resource/view metadata and no renderer/game
special case is needed. Existing format capabilities, dimensions, view usage,
resource state, descriptor identity/lifetime, and budget gates stay mandatory.
Copy and attachment flags retain their existing independent mappings.

Tests inspect actual native texture/view usage for Storage-only, Sampled-only,
and combined usage, with R32Float and Rgba16Float. Negative view creation proves
native ShaderRead does not authorize a Sampled view on Storage-only L2 textures.
The existing Slang DDGI production compute path under Metal API Validation
provides dispatch evidence; native flag inspection alone does not prove output.
