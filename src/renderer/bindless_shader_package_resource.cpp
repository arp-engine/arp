#include "renderer/bindless_shader_package_resource.h"

#include "resource/shader_artifact_source.h"

namespace arp::renderer {

BindlessShaderPackageResource::BindlessShaderPackageResource(
    resource::ResourceReference reference, BindlessShaderPackageResourceDesc desc)
    : ResourceBase(std::move(reference)), desc_(std::move(desc))
{}

core::SharedPtr<const rhi::BindlessShaderPackage> BindlessShaderPackageResource::prepare(
    const resource::ResourceLoadContext& context,
    core::pmr::Vector<resource::ResourceFileReadIntent>* pending,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    const auto& budget = desc_.budget;
    if (!context.requirePreloadedSources || !context.sources || !context.maxDecodedBytes
        || context.maxDecodedBytes == SIZE_MAX) {
        diagnostic = "SHADER-IO-SOURCE-CONTEXT";
        return {};
    }
    if (budget.source || ((budget.ordinaryMemory || budget.push) && !desc_.configurationOwner)) {
        diagnostic = "SHADER-IO-CONFIGURATION-OWNER";
        return {};
    }
    if (!desc_.maxCpuMetadataBytes || desc_.maxCpuMetadataBytes == SIZE_MAX
        || desc_.maxCpuMetadataBytes > context.maxDecodedBytes || !budget.maxFileBytes
        || budget.maxFileBytes > SIZE_MAX || budget.maxFileBytes == UINT64_MAX
        || !budget.maxTotalBytes || budget.maxTotalBytes == UINT64_MAX) {
        diagnostic = "SHADER-IO-LOAD-BUDGET";
        return {};
    }
    resource::AcquiredShaderArtifactSource source(context, desc_.origin, pending);
    auto selected = budget;
    selected.source = &source;
    const auto quota = core::makeShared<core::CpuMemoryQuota>(desc_.maxCpuMetadataBytes);
    core::CpuMemoryQuotaScope scope(quota);
    return rhi::BindlessShaderPackage::load(desc_.publication, selected, diagnostic);
}

bool BindlessShaderPackageResource::planFileReadIntents(resource::ResourceManager&,
    const resource::ResourceLoadContext& context,
    core::pmr::Vector<resource::ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostic) const
{
    const auto before = intents.size();
    if (prepare(context, &intents, diagnostic))
        return true;
    if (intents.size() == before + 1) {
        diagnostic.clear();
        return true;
    }
    return false;
}

resource::ResourceLoadResult BindlessShaderPackageResource::load(
    resource::ResourceManager& manager, const resource::ResourceLoadContext& context)
{
    core::pmr::String diagnostic;
    auto prepared = prepare(context, nullptr, diagnostic);
    if (!prepared)
        return {false,
            resource::ResourceState::Failed,
            reference().id,
            reference().type,
            std::move(diagnostic)};
    auto result = manager.admitResourceMetadata(*this, context.policy);
    if (result.success)
        package_ = std::move(prepared);
    return result;
}

std::size_t BindlessShaderPackageResource::processedByteCount() const noexcept
{
    return package_ && package_->quota() ? package_->quota()->liveBytes() : 0;
}

void BindlessShaderPackageResource::appendContentFingerprint(
    core::StableHash64& hash) const noexcept
{
    hash.appendU64(desc_.maxCpuMetadataBytes);
    hash.appendU64(desc_.budget.ordinaryMemory ? desc_.budget.ordinaryMemory->contractVersion : 0);
    hash.appendU64(desc_.budget.push ? desc_.budget.push->contractVersion : 0);
    if (package_)
        hash.appendString(package_->sha256());
}

} // namespace arp::renderer
