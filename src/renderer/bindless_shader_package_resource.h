#pragma once

#include "resource/resource_manager.h"
#include "rhi/frontend/bindless_shader_package.h"

#include <filesystem>

namespace arp::renderer {

struct BindlessShaderPackageResourceDesc {
    std::filesystem::path publication;
    resource::ResourceFileOrigin origin;
    rhi::BindlessPackageLoadBudget budget;
    std::size_t maxCpuMetadataBytes = 0;
    // Owns every nested ordinary-memory/push request until preparation completes.
    core::SharedPtr<const void> configurationOwner;
};

// Renderer composes immutable RHI package custody with ResourceGraph acquisition.
// A Ready resource grants no backend execution authority or GPU capability.
class ARP_API BindlessShaderPackageResource final : public resource::ResourceBase {
public:
    static constexpr const char* resourceTypeName() noexcept
    {
        return "BindlessShaderPackageResource";
    }

    BindlessShaderPackageResource(
        resource::ResourceReference reference, BindlessShaderPackageResourceDesc desc);

    resource::ResourceLoadResult load(
        resource::ResourceManager&, const resource::ResourceLoadContext&) override;
    bool planFileReadIntents(resource::ResourceManager&,
        const resource::ResourceLoadContext&,
        core::pmr::Vector<resource::ResourceFileReadIntent>&,
        core::pmr::String&) const override;
    std::size_t processedByteCount() const noexcept override;
    void appendContentFingerprint(core::StableHash64&) const noexcept override;

    const auto& package() const noexcept { return package_; }

private:
    core::SharedPtr<const rhi::BindlessShaderPackage> prepare(const resource::ResourceLoadContext&,
        core::pmr::Vector<resource::ResourceFileReadIntent>*,
        core::pmr::String&) const;
    BindlessShaderPackageResourceDesc desc_;
    core::SharedPtr<const rhi::BindlessShaderPackage> package_;
};

} // namespace arp::renderer
