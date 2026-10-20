#pragma once

#include "core/cpu_allocator.h"
#include "gui/gui.h"
#include "renderer/particles/particle_system.h"
#include "renderer/particles/particle_texture_resource.h"
#include "renderer/scene/scene.h"
#include "renderer/ui/ui_draw_plan.h"
#include "rhi/gpu_resources.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

namespace arp::renderer {

inline constexpr std::size_t kMaxCustomWorkloadResourceContracts = 8u;

enum class CustomWorkloadPayloadKind {
    None,
    SceneDrawPlan,
    ParticleDrawPlan,
    GuiDrawData,
    UiDrawPlan,
    PostprocessSettings,
    GlobalIlluminationPlan
};

struct CustomWorkloadPayloadRef {
    CustomWorkloadPayloadKind kind = CustomWorkloadPayloadKind::None;
    const void* object = nullptr;
    const char* contractName = nullptr;
    std::uint32_t contractVersion = 0;

    bool isValid() const
    {
        return kind != CustomWorkloadPayloadKind::None && object != nullptr
            && contractName != nullptr && contractName[0] != '\0' && contractVersion != 0u;
    }
};

template <typename Payload> struct CustomTypedWorkloadPayloadRef {
    CustomWorkloadPayloadKind kind = CustomWorkloadPayloadKind::None;
    const Payload* object = nullptr;
    const char* contractName = nullptr;
    std::uint32_t contractVersion = 0;

    CustomWorkloadPayloadRef erase() const
    {
        return {
            kind,
            object,
            contractName,
            contractVersion,
        };
    }

    bool isValid() const { return erase().isValid(); }
};

template <typename Payload>
inline CustomTypedWorkloadPayloadRef<Payload> customTypedWorkloadPayloadRef(
    CustomWorkloadPayloadKind kind,
    const Payload& object,
    const char* contractName,
    std::uint32_t contractVersion)
{
    return {
        kind,
        &object,
        contractName,
        contractVersion,
    };
}

inline constexpr const char* kCustomParticleGraphPayloadContract =
    "renderer/CustomParticleGraphPayload";
inline constexpr std::uint32_t kCustomParticleGraphPayloadVersion = 2u;
inline constexpr const char* kCustomGuiGraphPayloadContract = "renderer/CustomGuiGraphPayload";
inline constexpr std::uint32_t kCustomGuiGraphPayloadVersion = 1u;
inline constexpr const char* kCustomUiGraphPayloadContract = "renderer/CustomUiGraphPayload";
inline constexpr std::uint32_t kCustomUiGraphPayloadVersion = 1u;

struct CustomParticleGraphPayload {
    struct TextureBindingRequest {
        struct ExplicitTextureUpload {
            const std::uint8_t* rgba8 = nullptr;
            std::uint32_t width = 0u;
            std::uint32_t height = 0u;
            rhi::TextureFormat format = rhi::TextureFormat::Unknown;
            std::uint32_t bytesPerRow = 0u;
            std::uint32_t rowsPerImage = 0u;

            bool valid() const
            {
                return rgba8 != nullptr && width > 0u && height > 0u
                    && format != rhi::TextureFormat::Unknown && bytesPerRow > 0u
                    && rowsPerImage > 0u;
            }
        };

        rhi::GpuTexture* texture = nullptr;
        const particles::ParticleTextureResourceData* textureResource = nullptr;
        core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
        ExplicitTextureUpload explicitUpload;

        bool valid() const
        {
            return texture != nullptr || textureResource != nullptr || !resourceId.empty()
                || explicitUpload.valid();
        }
    };

    const ParticleSystem* particleSystem = nullptr;
    const std::pmr::vector<ParticleInstance>* particles = nullptr;
    const SceneConstants* scene = nullptr;
    TextureBindingRequest textureBinding;

    bool isValid() const
    {
        const bool hasParticleSystem = particleSystem != nullptr;
        const bool hasExplicitParticles = particles != nullptr;
        return scene != nullptr && hasParticleSystem != hasExplicitParticles;
    }

    const std::pmr::vector<ParticleInstance>& particleInstances() const
    {
        return particleSystem ? particleSystem->particles() : *particles;
    }
};

struct CustomGuiGraphPayload {
    const gui::DebugDrawData* DebugDrawData = nullptr;

    bool isValid() const { return DebugDrawData != nullptr; }
};

struct CustomUiGraphPayload {
    const arp::renderer::ui::UiDrawPlanLowerResult* loweredPlan = nullptr;
    const gui::DebugDrawData* debugDrawData = nullptr;

    bool isValid() const
    {
        return (loweredPlan != nullptr && loweredPlan->ok()) || debugDrawData != nullptr;
    }
};

class CustomWorkloadSection final {
public:
    static CustomWorkloadSection disabled() { return {}; }

    static CustomWorkloadSection enabled(CustomWorkloadPayloadRef payload)
    {
        CustomWorkloadSection section;
        section.enabled_ = true;
        section.payload_ = payload;
        return section;
    }

    template <typename Payload>
    static CustomWorkloadSection enabled(CustomTypedWorkloadPayloadRef<Payload> payload)
    {
        return enabled(payload.erase());
    }

    bool enabled() const { return enabled_; }

    const CustomWorkloadPayloadRef& payload() const { return payload_; }

private:
    bool enabled_ = false;
    CustomWorkloadPayloadRef payload_;
};

inline bool customPayloadContractMatches(const CustomWorkloadPayloadRef& payload,
    CustomWorkloadPayloadKind kind,
    const char* contractName,
    std::uint32_t contractVersion)
{
    return payload.kind == kind && payload.object != nullptr && payload.contractName != nullptr
        && std::strcmp(payload.contractName, contractName) == 0
        && payload.contractVersion == contractVersion;
}

template <typename Payload>
inline const Payload* customPayloadObjectAs(const CustomWorkloadPayloadRef& payload,
    CustomWorkloadPayloadKind kind,
    const char* contractName,
    std::uint32_t contractVersion)
{
    if (!customPayloadContractMatches(payload, kind, contractName, contractVersion)) {
        return nullptr;
    }
    return static_cast<const Payload*>(payload.object);
}

inline const CustomParticleGraphPayload* customParticleGraphPayloadFrom(
    const CustomWorkloadSection& section)
{
    if (!section.enabled()) {
        return nullptr;
    }
    const auto* graphPayload = customPayloadObjectAs<CustomParticleGraphPayload>(section.payload(),
        CustomWorkloadPayloadKind::ParticleDrawPlan,
        kCustomParticleGraphPayloadContract,
        kCustomParticleGraphPayloadVersion);
    return graphPayload && graphPayload->isValid() ? graphPayload : nullptr;
}

inline const CustomGuiGraphPayload* customGuiGraphPayloadFrom(const CustomWorkloadSection& section)
{
    if (!section.enabled()) {
        return nullptr;
    }
    const auto* graphPayload = customPayloadObjectAs<CustomGuiGraphPayload>(section.payload(),
        CustomWorkloadPayloadKind::GuiDrawData,
        kCustomGuiGraphPayloadContract,
        kCustomGuiGraphPayloadVersion);
    return graphPayload && graphPayload->isValid() ? graphPayload : nullptr;
}

inline const CustomUiGraphPayload* customUiGraphPayloadFrom(const CustomWorkloadSection& section)
{
    if (!section.enabled()) {
        return nullptr;
    }
    const auto* graphPayload = customPayloadObjectAs<CustomUiGraphPayload>(section.payload(),
        CustomWorkloadPayloadKind::UiDrawPlan,
        kCustomUiGraphPayloadContract,
        kCustomUiGraphPayloadVersion);
    return graphPayload && graphPayload->isValid() ? graphPayload : nullptr;
}

enum class CustomResourceSemantic {
    Unsupported,
    ExplicitTextureUpload,
    ExplicitBufferUpload,
    PrototypeLegacyFallback
};

enum class CustomUploadMetadataSource { None, ExplicitContract, GuessedFallback };

struct CustomResourceContract {
    const char* name = nullptr;
    CustomResourceSemantic semantic = CustomResourceSemantic::Unsupported;
    CustomUploadMetadataSource uploadMetadata = CustomUploadMetadataSource::None;
    rhi::Extent2D extent;
    rhi::TextureFormat format = rhi::TextureFormat::Unknown;
    std::uint32_t bytesPerRow = 0;
    std::uint32_t rowsPerImage = 0;
    std::uint64_t sizeBytes = 0;
    const char* prototypeLegacyFallbackReason = nullptr;
};

struct CustomRenderFrameWorkloadDesc {
    std::optional<rhi::Extent2D> extent;
    std::optional<rhi::TextureFormat> colorFormat;
    std::optional<std::uint64_t> frameSerial;
    std::optional<rhi::ResourceState> backbufferState;
    CustomWorkloadSection scene;
    CustomWorkloadSection legacySceneFallback;
    CustomWorkloadSection particles;
    CustomWorkloadSection gui;
    CustomWorkloadSection postprocess;
    CustomWorkloadSection globalIllumination;
    std::array<CustomResourceContract, kMaxCustomWorkloadResourceContracts> resourceContracts{};
    std::size_t resourceContractCount = 0;
};

struct CustomRenderFrameValidationResult {
    bool success = false;
    std::pmr::string diagnostics;
};

class CustomRenderFrameWorkload final {
public:
    explicit CustomRenderFrameWorkload(CustomRenderFrameWorkloadDesc desc) : desc_(desc) {}

    CustomRenderFrameValidationResult validate() const
    {
        if (!desc_.extent.has_value() || desc_.extent->width == 0u || desc_.extent->height == 0u) {
            return fail("CustomRenderFrameWorkload requires an explicit extent.");
        }

        if (!desc_.colorFormat.has_value()
            || desc_.colorFormat.value() == rhi::TextureFormat::Unknown) {
            return fail("CustomRenderFrameWorkload requires a known color format.");
        }

        if (!desc_.frameSerial.has_value()) {
            return fail("CustomRenderFrameWorkload requires an explicit frame serial.");
        }

        if (!desc_.backbufferState.has_value()
            || desc_.backbufferState.value() == rhi::ResourceState::Undefined) {
            return fail("CustomRenderFrameWorkload requires an explicit backbuffer state.");
        }

        if (const auto result =
                validateSection("scene", desc_.scene, CustomWorkloadPayloadKind::SceneDrawPlan);
            !result.success) {
            return result;
        }

        if (const auto result = validateSection("legacy scene fallback",
                desc_.legacySceneFallback,
                CustomWorkloadPayloadKind::SceneDrawPlan);
            !result.success) {
            return result;
        }

        if (const auto result = validateSection(
                "particles", desc_.particles, CustomWorkloadPayloadKind::ParticleDrawPlan);
            !result.success) {
            return result;
        }

        if (const auto result = validateGuiSection(desc_.gui); !result.success) {
            return result;
        }

        if (const auto result = validateSection(
                "postprocess", desc_.postprocess, CustomWorkloadPayloadKind::PostprocessSettings);
            !result.success) {
            return result;
        }

        if (const auto result = validateSection("global illumination",
                desc_.globalIllumination,
                CustomWorkloadPayloadKind::GlobalIlluminationPlan);
            !result.success) {
            return result;
        }

        if (desc_.resourceContractCount > desc_.resourceContracts.size()) {
            return fail(
                "CustomRenderFrameWorkload resource contract count exceeds the bounded contract capacity.");
        }

        for (std::size_t i = 0; i < desc_.resourceContractCount; ++i) {
            if (const auto result = validateResourceContract(desc_.resourceContracts[i]);
                !result.success) {
                return result;
            }
        }

        return {true, {}};
    }

    rhi::Extent2D extent() const { return desc_.extent.value_or(rhi::Extent2D{}); }

    rhi::TextureFormat colorFormat() const
    {
        return desc_.colorFormat.value_or(rhi::TextureFormat::Unknown);
    }

    std::uint64_t frameSerial() const { return desc_.frameSerial.value_or(0u); }

    rhi::ResourceState backbufferState() const
    {
        return desc_.backbufferState.value_or(rhi::ResourceState::Undefined);
    }

    const CustomWorkloadSection& scene() const { return desc_.scene; }

    const CustomWorkloadSection& legacySceneFallback() const { return desc_.legacySceneFallback; }

    const char* scenePayloadContractName() const
    {
        return desc_.scene.enabled() ? desc_.scene.payload().contractName : nullptr;
    }

    std::uint32_t scenePayloadContractVersion() const
    {
        return desc_.scene.enabled() ? desc_.scene.payload().contractVersion : 0u;
    }

    const CustomWorkloadSection& particles() const { return desc_.particles; }

    const CustomWorkloadSection& gui() const { return desc_.gui; }

    const CustomWorkloadSection& postprocess() const { return desc_.postprocess; }

    const CustomWorkloadSection& globalIllumination() const { return desc_.globalIllumination; }

    std::size_t resourceContractCount() const { return desc_.resourceContractCount; }

    const CustomResourceContract& resourceContract(std::size_t index) const
    {
        return desc_.resourceContracts[index];
    }

    bool allowsPrototypeLegacyFallback() const
    {
        for (std::size_t i = 0; i < desc_.resourceContractCount; ++i) {
            if (desc_.resourceContracts[i].semantic
                == CustomResourceSemantic::PrototypeLegacyFallback) {
                return true;
            }
        }
        return false;
    }

private:
    static CustomRenderFrameValidationResult fail(const char* diagnostic)
    {
        return {false, diagnostic};
    }

    static bool hasText(const char* text) { return text != nullptr && text[0] != '\0'; }

    static CustomRenderFrameValidationResult validateSection(const char* name,
        const CustomWorkloadSection& section,
        CustomWorkloadPayloadKind expectedKind)
    {
        if (!section.enabled()) {
            return {true, {}};
        }

        const auto& payload = section.payload();
        if (!payload.isValid()) {
            std::pmr::string diagnostic = "CustomRenderFrameWorkload requires ";
            diagnostic += name;
            diagnostic += " payload when that section is enabled.";
            return {false, diagnostic};
        }

        if (payload.kind != expectedKind) {
            std::pmr::string diagnostic = "CustomRenderFrameWorkload ";
            diagnostic += name;
            diagnostic += " payload kind does not match its section contract.";
            return {false, diagnostic};
        }

        return {true, {}};
    }

    static CustomRenderFrameValidationResult validateGuiSection(
        const CustomWorkloadSection& section)
    {
        if (!section.enabled()) {
            return {true, {}};
        }
        const auto& payload = section.payload();
        if (!payload.isValid()) {
            return {false,
                "CustomRenderFrameWorkload requires GUI payload when that section is enabled."};
        }
        if (payload.kind != CustomWorkloadPayloadKind::GuiDrawData
            && payload.kind != CustomWorkloadPayloadKind::UiDrawPlan) {
            return {false,
                "CustomRenderFrameWorkload GUI payload kind does not match its section contract."};
        }
        return {true, {}};
    }

    static CustomRenderFrameValidationResult validateResourceContract(
        const CustomResourceContract& resource)
    {
        if (!hasText(resource.name)) {
            return fail("CustomRenderFrameWorkload resource contract requires an explicit name.");
        }

        if (resource.semantic == CustomResourceSemantic::Unsupported) {
            return fail("CustomRenderFrameWorkload rejects unsupported resource semantics.");
        }

        if (resource.semantic == CustomResourceSemantic::PrototypeLegacyFallback) {
            if (!hasText(resource.prototypeLegacyFallbackReason)) {
                return fail(
                    "CustomRenderFrameWorkload Prototype/Legacy fallback requires an explicit diagnostic label.");
            }
            return {true, {}};
        }

        if (resource.uploadMetadata != CustomUploadMetadataSource::ExplicitContract) {
            return fail(
                "CustomRenderFrameWorkload requires explicit upload metadata; guessed fallback metadata is rejected.");
        }

        if (resource.semantic == CustomResourceSemantic::ExplicitTextureUpload) {
            if (resource.extent.width == 0u || resource.extent.height == 0u
                || resource.format == rhi::TextureFormat::Unknown || resource.bytesPerRow == 0u
                || resource.rowsPerImage == 0u) {
                return fail(
                    "CustomRenderFrameWorkload texture upload contract requires explicit extent, format, row pitch, and rows per image.");
            }
            return {true, {}};
        }

        if (resource.semantic == CustomResourceSemantic::ExplicitBufferUpload) {
            if (resource.sizeBytes == 0u) {
                return fail(
                    "CustomRenderFrameWorkload buffer upload contract requires an explicit byte size.");
            }
            return {true, {}};
        }

        return fail("CustomRenderFrameWorkload rejects unsupported resource semantics.");
    }

    CustomRenderFrameWorkloadDesc desc_;
};

} // namespace arp::renderer
