#pragma once

#include "assets/asset_pipeline.h"
#include "core/index_buffer_encoding.h"
#include "core/math.h"
#include "resource/prefab_asset_resources.h"

namespace arp::renderer {
inline constexpr const char* kBasicForwardMaterialContract = "arp/material/basic-forward-lambert";

struct BasicForwardMaterialConstants {
    core::Vec4 baseColor;
    core::Vec4 directionIntensity;
    core::Vec4 lightColor;
    core::Vec4 ambientColor;
    core::Vec4 flags;
};

static_assert(sizeof(BasicForwardMaterialConstants) == 80);
static_assert(offsetof(BasicForwardMaterialConstants, flags) == 64);

struct BasicForwardLighting {
    std::uint32_t contractVersion = 0, maxLights = 0, lightCount = 0;
    core::Vec3 direction;
    float intensity = 0;
    core::Vec3 color, ambient;
};

enum class BasicWorldMeshIndexPolicy { Uint16Exact, RequireUint32 };

struct BasicWorldMeshProfile {
    std::uint32_t contractVersion = 0;
    bool worldSpaceStatic = false;
    BasicWorldMeshIndexPolicy indexPolicy = BasicWorldMeshIndexPolicy::Uint16Exact;
    std::uint32_t maxVertices = 0, maxIndices = 0, maxSections = 0, materialCount = 0;
    std::uint32_t instanceCount = 0;
    std::uint64_t maxVertexBytes = 0, maxIndexBytes = 0;
};

class BasicForwardContract final {
public:
    static const char* material(const resource::MaterialResource&,
        const resource::ShaderPackageResource&,
        std::string_view shaderPackageId,
        const BasicForwardLighting&,
        BasicForwardMaterialConstants&);
    static const char* mesh(const assets::MeshAsset&, const BasicWorldMeshProfile&);
    static const char* camera(const core::Mat4&) noexcept;
};
} // namespace arp::renderer
