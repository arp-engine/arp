#include "renderer/pipeline/basic_forward_contract.h"
#include "renderer/pipeline/basic_forward_pipeline.h"
#include "rhi/backends/null/null_backend.h"

#include <cassert>
#include <cstring>
#include <limits>

int main()
{
    using namespace arp;
    using namespace renderer;
    {
        rhi::semantics::PreparedGraphicsCommandsRequest request{
            3, true, 7, 2048, 24, {1, true, 32, 160, 128, 1024 * 1024, 512}, 5, 2, 2, 32};
        using Contract = rhi::semantics::PreparedGraphicsCommandsContract;
        assert(!Contract::validate(request));
        assert(Contract::pipelineSwitch(request, false, false));
        request.contractVersion = 4;
        assert(!Contract::validate(request) && !Contract::pipelineSwitch(request, false, false));
        assert(Contract::pipelineSwitch(request, false, true));
        assert(!Contract::pipelineSwitch(request, true, true));
        request.contractVersion = 5;
        assert(Contract::validate(request));
    }
    {
        auto device = rhi::createNullDevice();
        BasicForwardRenderPipeline pipeline({});
        assert(!pipeline.frameInputsReady(0));
        assert(!pipeline
                .create({*device,
                    rhi::TextureFormat::Rgba8Unorm,
                    rhi::TextureFormat::Depth24Stencil8,
                    {64, 64}})
                .success);
        assert(!pipeline.resize({64, 64}).success);
        std::uint32_t id = 0;
        resource::ShaderPackageResource shader;
        assert(!pipeline.registerMesh({}, {}, {}, shader, {}, {}, id).success && id == UINT32_MAX);
    }
    resource::MaterialResource material;
    resource::ShaderPackageResource shader;
    constexpr const char* shaderId = "00000000-0000-4000-8000-000000000008";
    material.materialContractVersion = shader.materialInterfaceVersion = 1;
    material.surfaceModel = shader.materialInterfaceContract = kBasicForwardMaterialContract;
    material.shaderPackageId = shaderId;
    material.metallicFactor = 0;
    material.roughnessFactor = 1;
    material.alphaMode = "Opaque";
    BasicForwardLighting light{1, 1, 1, {0, 0, -2}, .5f, {1, 1, 1}, {.25f, .25f, .25f}};
    BasicForwardMaterialConstants push;
    const auto valid = [&] {
        return BasicForwardContract::material(material, shader, shaderId, light, push);
    };
    assert(!valid() && push.directionIntensity.z == -1 && push.flags.z == 1 && push.flags.w == 0);
    material.surfaceModel = resource::kPbrMetallicRoughnessContract;
    assert(valid());
    material.surfaceModel = kBasicForwardMaterialContract;
    shader.materialInterfaceVersion = 2;
    assert(valid());
    shader.materialInterfaceVersion = 1;
    material.shaderPackageId = "missing";
    assert(valid());
    material.shaderPackageId = shaderId;
    material.normalTextureId = shaderId;
    assert(valid());
    material.normalTextureId.clear();
    material.metallicFactor = 1;
    assert(valid());
    material.metallicFactor = 0;
    material.alphaMode = "Mask";
    assert(valid());
    material.alphaMode = "Blend";
    material.baseColorA = .5f;
    material.doubleSided = true;
    assert(!valid() && push.baseColor.w == .5f && push.flags.y == 1 && push.flags.w == 1);
    material.textureBindingCount = 1;
    assert(valid());
    material.baseColorTextureId = shaderId;
    assert(!valid() && push.flags.x == 1);
    light.lightCount = 2;
    assert(valid());
    light.lightCount = 1;
    light.direction = {};
    assert(valid());
    light.direction = {0, 0, -1};
    light.intensity = 1;
    assert(valid());
    light.intensity = .5f;
    light.lightCount = 0;
    assert(valid());
    light.intensity = 0;
    assert(!valid() && push.flags.z == 0);
    light.ambient.x = std::numeric_limits<float>::infinity();
    assert(valid());
    light.ambient.x = .25f;

    assets::MeshAsset mesh = assets::MeshAsset::debugTriangle();
    BasicWorldMeshProfile profile{1,
        true,
        BasicWorldMeshIndexPolicy::Uint16Exact,
        3,
        3,
        1,
        1,
        1,
        3 * sizeof(assets::MeshVertex),
        6};
    assert(!BasicForwardContract::mesh(mesh, profile));
    profile.maxVertexBytes--;
    assert(BasicForwardContract::mesh(mesh, profile));
    profile.maxVertexBytes++;
    profile.maxIndexBytes--;
    assert(BasicForwardContract::mesh(mesh, profile));
    profile.maxIndexBytes++;
    profile.instanceCount = 2;
    assert(BasicForwardContract::mesh(mesh, profile));
    profile.instanceCount = 1;
    profile.worldSpaceStatic = false;
    assert(BasicForwardContract::mesh(mesh, profile));
    profile.worldSpaceStatic = true;
    mesh.submeshes[0].materialSlot = 1;
    assert(BasicForwardContract::mesh(mesh, profile));
    mesh.submeshes[0].materialSlot = 0;
    mesh.submeshes[0].firstIndex = UINT32_MAX;
    assert(BasicForwardContract::mesh(mesh, profile));
    mesh.submeshes[0].firstIndex = 0;
    mesh.vertices[0].normal[2] = 0;
    assert(BasicForwardContract::mesh(mesh, profile));
    mesh.vertices[0].normal[2] = 1;
    mesh.indices[2] = 3;
    assert(BasicForwardContract::mesh(mesh, profile));
    mesh.indices[2] = 2;

    core::pmr::Vector<std::uint8_t> encoded{&core::defaultCpuMemoryResource()};
    const std::uint32_t indices[]{0, 65535, 65536};
    assert(!core::IndexBufferEncoding::encode(
        {indices, 2}, 65536, core::IndexElementWidth::Uint16, 4, encoded));
    std::uint16_t last = 0;
    std::memcpy(&last, encoded.data() + 2, 2);
    assert(last == 65535);
    assert(core::IndexBufferEncoding::encode(
               indices, 65537, core::IndexElementWidth::Uint16, 6, encoded)
        && encoded.empty());
    assert(!core::IndexBufferEncoding::encode(
        indices, 65537, core::IndexElementWidth::Uint32, 12, encoded));
    std::uint32_t wide = 0;
    std::memcpy(&wide, encoded.data() + 8, 4);
    assert(wide == 65536);
    assert(core::IndexBufferEncoding::encode(
               indices, 65537, core::IndexElementWidth::Uint32, 11, encoded)
        && encoded.empty());
    assert(core::IndexBufferEncoding::encode(
               indices, 65536, core::IndexElementWidth::Uint32, 12, encoded)
        && encoded.empty());
    assert(!BasicForwardContract::camera(core::Mat4::identity()));
    assert(BasicForwardContract::camera(core::Mat4{}));
}
