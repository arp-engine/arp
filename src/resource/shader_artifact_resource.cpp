#include "resource/shader_artifact_resource.h"

#include "resource/shader_artifact_source.h"

#include <type_traits>

namespace arp::resource {
namespace {

template <class Budget> const auto& entryBudget(const Budget& budget)
{
    if constexpr (std::is_same_v<Budget, shader::WebGlShaderLoadBudget>)
        return budget.original;
    else
        return budget;
}

template <class Budget> bool needsConfigurationOwner(const Budget& budget)
{
    if constexpr (std::is_same_v<Budget, shader::OriginalDepthTableExpansionArtifact::Budget>
        || std::is_same_v<Budget, shader::GeneratedShaderArtifactSetBudget>)
        return false;
    else if constexpr (std::is_same_v<Budget, shader::GraphicsShaderModuleLoadBudget>)
        return budget.targetMemory || budget.runtimeLength || budget.nativeMemory[0]
            || budget.nativeMemory[1];
    else {
        const auto& b = entryBudget(budget);
        return b.targetMemory || b.runtimeLength || b.nativeMemory;
    }
}

template <class Budget> std::uint64_t totalInputLimit(const Budget& budget)
{
    if constexpr (std::is_same_v<Budget, shader::GraphicsShaderModuleLoadBudget>)
        return budget.maxTotalInputBytes;
    else
        return entryBudget(budget).maxTotalBytes;
}
} // namespace

ShaderArtifactResource::ShaderArtifactResource(
    ResourceReference reference, ShaderArtifactResourceDesc desc)
    : ResourceBase(std::move(reference)), desc_(std::move(desc))
{}

ShaderArtifactResource::Prepared ShaderArtifactResource::prepare(const ResourceLoadContext& context,
    core::pmr::Vector<ResourceFileReadIntent>* pending,
    core::pmr::String& diagnostic) const
{
    Prepared result;
    diagnostic.clear();
    if (!context.requirePreloadedSources || !context.sources || !context.maxDecodedBytes
        || context.maxDecodedBytes == SIZE_MAX) {
        diagnostic = "SHADER-IO-SOURCE-CONTEXT";
        return result;
    }
    AcquiredShaderArtifactSource source(context, desc_.origin, pending);
    std::visit(
        [&](auto budget) {
            using Budget = decltype(budget);
            const auto& b = entryBudget(budget);
            if (b.source || (needsConfigurationOwner(budget) && !desc_.configurationOwner)) {
                diagnostic = "SHADER-IO-CONFIGURATION-OWNER";
                return;
            }
            if (!b.maxFileBytes || b.maxFileBytes > SIZE_MAX || b.maxFileBytes == UINT64_MAX
                || !b.maxCpuMetadataBytes || !totalInputLimit(budget)
                || totalInputLimit(budget) == UINT64_MAX
                || b.maxCpuMetadataBytes > context.maxDecodedBytes) {
                diagnostic = "SHADER-IO-LOAD-BUDGET";
                return;
            }
            auto quota = core::makeShared<core::CpuMemoryQuota>(b.maxCpuMetadataBytes);
            if constexpr (std::is_same_v<Budget, shader::SingleEntryShaderLoadBudget>) {
                budget.source = &source;
                result.single = shader::SingleEntryShaderArtifact::load(
                    desc_.publication, budget, quota, diagnostic);
            } else if constexpr (std::is_same_v<Budget, shader::GraphicsShaderModuleLoadBudget>) {
                budget.source = &source;
                result.graphics = shader::GraphicsShaderModuleArtifact::load(
                    desc_.publication, budget, quota, diagnostic);
            } else if constexpr (std::is_same_v<Budget, shader::WebGlShaderLoadBudget>) {
                budget.original.source = &source;
                result.webgl = shader::WebGlShaderProgramArtifact::load(
                    desc_.publication, budget, quota, diagnostic);
            } else if constexpr (std::is_same_v<Budget, shader::GeneratedShaderArtifactSetBudget>) {
                budget.source = &source;
                result.generated = shader::GeneratedShaderArtifactSet::load(
                    desc_.publication, budget, quota, diagnostic);
            } else {
                budget.source = &source;
                result.depth = shader::OriginalDepthTableExpansionArtifact::load(
                    desc_.publication, budget, quota, diagnostic);
            }
        },
        desc_.budget);
    return result;
}

bool ShaderArtifactResource::planFileReadIntents(ResourceManager&,
    const ResourceLoadContext& context,
    core::pmr::Vector<ResourceFileReadIntent>& intents,
    core::pmr::String& diagnostic) const
{
    const auto before = intents.size();
    const auto result = prepare(context, &intents, diagnostic);
    if (result.ready())
        return true;
    if (intents.size() == before + 1) {
        diagnostic.clear();
        return true;
    }
    return false;
}

ResourceLoadResult ShaderArtifactResource::load(
    ResourceManager& manager, const ResourceLoadContext& context)
{
    core::pmr::String diagnostic;
    auto candidate = prepare(context, nullptr, diagnostic);
    if (!candidate.ready())
        return {
            false, ResourceState::Failed, reference().id, reference().type, std::move(diagnostic)};
    prepared_ = std::move(candidate);
    return manager.admitResourceMetadata(*this, context.policy);
}

std::size_t ShaderArtifactResource::processedByteCount() const noexcept
{
    if (prepared_.single)
        return prepared_.single->quota()->liveBytes();
    if (prepared_.graphics)
        return prepared_.graphics->quota()->liveBytes();
    if (prepared_.webgl)
        return prepared_.webgl->originalMetadataQuota()->liveBytes();
    if (prepared_.depth)
        return prepared_.depth->quota()->liveBytes();
    if (prepared_.generated)
        return prepared_.generated->quota()->liveBytes();
    return 0;
}

void ShaderArtifactResource::appendContentFingerprint(core::StableHash64& hash) const noexcept
{
    hash.appendU64(desc_.budget.index());
    std::visit(
        [&](const auto& budget) {
            using Budget = std::decay_t<decltype(budget)>;
            const auto& b = entryBudget(budget);
            hash.appendU64(b.maxCpuMetadataBytes);
            const auto proof = [&](const auto* request) {
                hash.appendU64(request ? request->contractVersion : 0u);
            };
            if constexpr (std::is_same_v<Budget,
                              shader::OriginalDepthTableExpansionArtifact::Budget>) {
                hash.appendU64(budget.contractVersion);
                hash.appendU64(budget.explicitOptIn);
            } else if constexpr (std::is_same_v<Budget, shader::GeneratedShaderArtifactSetBudget>) {
                hash.appendU64(budget.maxBindings);
                hash.appendU64(budget.maxEntryPoints);
            } else {
                proof(b.targetMemory);
                proof(b.runtimeLength);
                if constexpr (std::is_same_v<Budget, shader::GraphicsShaderModuleLoadBudget>) {
                    proof(b.nativeMemory[0]);
                    proof(b.nativeMemory[1]);
                } else
                    proof(b.nativeMemory);
            }
        },
        desc_.budget);
    const auto append = [&](const auto& pointer) {
        if (pointer)
            hash.appendBytes(pointer->identity().data(), pointer->identity().size());
    };
    append(prepared_.single);
    append(prepared_.graphics);
    append(prepared_.webgl);
    append(prepared_.depth);
    append(prepared_.generated);
}
} // namespace arp::resource
