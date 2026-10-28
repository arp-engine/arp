#include "renderer/pipeline/basic_forward_contract.h"

namespace arp::renderer {
const char* BasicForwardContract::camera(const core::Mat4& matrix) noexcept
{
    for (const auto v : matrix.m)
        if (!std::isfinite(v))
            return "BASIC-FORWARD-CAMERA-NONFINITE";
    // An all-zero homogeneous row cannot project any finite input.
    if (matrix.m[12] == 0 && matrix.m[13] == 0 && matrix.m[14] == 0 && matrix.m[15] == 0)
        return "BASIC-FORWARD-CAMERA-HOMOGENEOUS-ROW";
    return nullptr;
}

const char* BasicForwardContract::materialSurface(const resource::MaterialResource& m,
    const resource::ShaderPackageResource& shader,
    std::string_view shaderId,
    BasicForwardMaterialConstants& output)
{
    output = {};
    if (m.materialContractVersion != 1 || m.surfaceModel != kBasicForwardMaterialContract
        || shader.materialInterfaceVersion != 1
        || shader.materialInterfaceContract != kBasicForwardMaterialContract
        || !resource::isResourceUuid(shaderId) || std::string_view(m.shaderPackageId) != shaderId)
        return "BASIC-FORWARD-MATERIAL-SHADER-CONTRACT";
    if (!m.normalTextureId.empty() || !m.metallicRoughnessTextureId.empty()
        || !m.occlusionTextureId.empty() || !m.emissiveTextureId.empty() || m.emissiveR != 0
        || m.emissiveG != 0 || m.emissiveB != 0 || m.metallicFactor != 0 || m.roughnessFactor != 1)
        return "BASIC-FORWARD-ADVANCED-MATERIAL-UNSUPPORTED";
    if (m.alphaMode != "Opaque" && m.alphaMode != "Blend")
        return "BASIC-FORWARD-ALPHA-PROFILE";
    if (m.textureBindingCount != (m.baseColorTextureId.empty() ? 0 : 1)
        || (!m.baseColorTextureId.empty() && !resource::isResourceUuid(m.baseColorTextureId)))
        return "BASIC-FORWARD-TEXTURE-METADATA";
    for (const auto c : {m.baseColorR, m.baseColorG, m.baseColorB, m.baseColorA})
        if (!std::isfinite(c) || c < 0 || c > 1)
            return "BASIC-FORWARD-MATERIAL-COLOR";
    if (m.alphaMode == "Opaque" && m.baseColorA != 1)
        return "BASIC-FORWARD-OPAQUE-ALPHA";
    output.baseColor = {m.baseColorR, m.baseColorG, m.baseColorB, m.baseColorA};
    output.flags = {m.baseColorTextureId.empty() ? 0.f : 1.f,
        m.doubleSided ? 1.f : 0.f,
        0.f,
        m.alphaMode == "Blend" ? 1.f : 0.f};
    return nullptr;
}

const char* BasicForwardContract::lighting(const BasicForwardLighting& light,
    BasicForwardLightingRange range,
    BasicForwardLightingConstants& output) noexcept
{
    output = {};
    if (range != BasicForwardLightingRange::Ldr1 && range != BasicForwardLightingRange::LinearHdr1)
        return "BASIC-FORWARD-LIGHT-RANGE-PROFILE";
    if (light.contractVersion != 1 || light.maxLights != 1 || light.lightCount > light.maxLights
        || !std::isfinite(light.intensity) || light.intensity < 0
        || (!light.lightCount && light.intensity != 0))
        return "BASIC-FORWARD-LIGHT-BUDGET";
    for (const auto v : {light.direction.x,
             light.direction.y,
             light.direction.z,
             light.color.x,
             light.color.y,
             light.color.z,
             light.ambient.x,
             light.ambient.y,
             light.ambient.z})
        if (!std::isfinite(v))
            return "BASIC-FORWARD-LIGHT-NONFINITE";
    const auto length = core::length(light.direction);
    if (light.lightCount && (!std::isfinite(length) || length <= 0))
        return "BASIC-FORWARD-LIGHT-DIRECTION";
    const std::array<float, 3> colors{light.color.x, light.color.y, light.color.z};
    const std::array<float, 3> ambient{light.ambient.x, light.ambient.y, light.ambient.z};
    for (unsigned i = 0; i < 3; ++i)
        if (colors[i] < 0 || ambient[i] < 0
            || !std::isfinite(ambient[i] + colors[i] * light.intensity)
            || (range == BasicForwardLightingRange::Ldr1
                && ambient[i] + colors[i] * light.intensity > 1))
            return range == BasicForwardLightingRange::Ldr1 ? "BASIC-FORWARD-LDR-IRRADIANCE-RANGE"
                                                            : "BASIC-FORWARD-HDR-IRRADIANCE-RANGE";
    output.directionIntensity = light.lightCount ? core::Vec4{light.direction.x / length,
                                                       light.direction.y / length,
                                                       light.direction.z / length,
                                                       light.intensity}
                                                 : core::Vec4{};
    output.lightColor = {light.color.x, light.color.y, light.color.z, 0};
    output.ambientColor = {light.ambient.x, light.ambient.y, light.ambient.z, 0};
    return nullptr;
}

const char* BasicForwardContract::material(const resource::MaterialResource& m,
    const resource::ShaderPackageResource& shader,
    std::string_view shaderId,
    const BasicForwardLighting& light,
    BasicForwardMaterialConstants& output)
{
    if (const auto* id = materialSurface(m, shader, shaderId, output))
        return id;
    BasicForwardLightingConstants illumination;
    if (const auto* id = lighting(light, BasicForwardLightingRange::Ldr1, illumination)) {
        output = {};
        return id;
    }
    output.directionIntensity = illumination.directionIntensity;
    output.lightColor = illumination.lightColor;
    output.ambientColor = illumination.ambientColor;
    output.flags.z = float(light.lightCount);
    return nullptr;
}

const char* BasicForwardContract::mesh(
    const assets::MeshAsset& mesh, const BasicWorldMeshProfile& p)
{
    if (!p.worldSpaceStatic)
        return "BASIC-FORWARD-MESH-PROFILE";
    return meshData(mesh, p);
}

const char* BasicForwardContract::meshData(
    const assets::MeshAsset& mesh, const BasicWorldMeshProfile& p)
{
    if (p.contractVersion != 1 || !p.maxVertices || !p.maxIndices || !p.maxSections
        || !p.materialCount
        || (p.indexPolicy != BasicWorldMeshIndexPolicy::Uint16Exact
            && p.indexPolicy != BasicWorldMeshIndexPolicy::RequireUint32))
        return "BASIC-FORWARD-MESH-PROFILE";
    if (p.instanceCount != 1)
        return "BASIC-FORWARD-INSTANCE-PROFILE-REQUIRED";
    if (mesh.vertices.empty() || mesh.vertices.size() > p.maxVertices
        || mesh.vertices.size() > p.maxVertexBytes / sizeof(assets::MeshVertex)
        || mesh.indices.empty() || mesh.indices.size() > p.maxIndices || mesh.indices.size() % 3
        || mesh.submeshes.empty() || mesh.submeshes.size() > p.maxSections
        || mesh.indices.size()
            > p.maxIndexBytes / (p.indexPolicy == BasicWorldMeshIndexPolicy::Uint16Exact ? 2u : 4u))
        return "BASIC-FORWARD-MESH-BUDGET";
    for (const auto& v : mesh.vertices) {
        for (float x : v.position)
            if (!std::isfinite(x))
                return "BASIC-FORWARD-VERTEX-NONFINITE";
        for (float x : v.normal)
            if (!std::isfinite(x))
                return "BASIC-FORWARD-VERTEX-NONFINITE";
        for (float x : v.uv)
            if (!std::isfinite(x))
                return "BASIC-FORWARD-VERTEX-NONFINITE";
        const auto normalLength = core::length(core::Vec3{v.normal[0], v.normal[1], v.normal[2]});
        if (!std::isfinite(normalLength) || normalLength <= 0)
            return "BASIC-FORWARD-NORMAL-ZERO";
    }
    for (const auto index : mesh.indices) {
        if (index >= mesh.vertices.size())
            return "BASIC-FORWARD-INDEX-RANGE";
        if (p.indexPolicy == BasicWorldMeshIndexPolicy::Uint16Exact && index > UINT16_MAX)
            return "BASIC-FORWARD-UINT32-PROFILE-REQUIRED";
    }
    std::uint64_t covered = 0;
    for (const auto& section : mesh.submeshes) {
        if (section.firstIndex != covered || !section.indexCount || section.indexCount % 3
            || section.indexCount > mesh.indices.size() - covered
            || section.materialSlot >= p.materialCount)
            return "BASIC-FORWARD-SECTION-MATERIAL-RELATION";
        covered += section.indexCount;
    }
    return covered == mesh.indices.size() ? nullptr : "BASIC-FORWARD-SECTION-COVERAGE";
}
} // namespace arp::renderer
