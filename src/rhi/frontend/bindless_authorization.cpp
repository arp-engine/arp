#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/gpu_bindless.h"

namespace arp::rhi {

bool BindlessAuthorization::current() const noexcept
{
    return lifetime_.isCurrent() && package_ && package_->sharedOwnerMatches(package_);
}

ValidationResult BindlessSnapshot::validatePackage(
    const BindlessCrossExtension& extension, const BindlessShaderArtifact& packageArtifact) const
{
    const auto live = validate();
    if (!live.success)
        return live;
    auto artifact = packageArtifact;
    artifact.snapshotBytes = plan_.snapshotBytes;
    const auto reproduced = extension.plan(requirements_, artifact);
    if (!bindlessPlansMatch(reproduced, plan_))
        return {false, "RHI-BINDLESS-AUTHORIZATION-PLAN-MISMATCH"};
    return {true, {}};
}

std::shared_ptr<const BindlessAuthorization> BindlessCrossExtension::authorize(
    std::shared_ptr<const BindlessShaderPackage> package,
    const BindlessSnapshot& snapshot,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    if (!package) {
        diagnostic = "RHI-BINDLESS-AUTHORIZATION-PACKAGE-MISSING";
        return {};
    }
    if (!package->sharedOwnerMatches(package)) {
        diagnostic = "RHI-BINDLESS-AUTHORIZATION-PACKAGE-OWNER-MISMATCH";
        return {};
    }
    const auto validation = snapshot.validatePackage(*this, package->artifact());
    if (!validation.success) {
        diagnostic = validation.diagnostics;
        return {};
    }
    const auto issuance = validateAuthorization(*package, snapshot);
    if (!issuance.success) {
        diagnostic = issuance.diagnostics;
        return {};
    }
    return core::makeShared<BindlessAuthorization>(BindlessAuthorization::ConstructionKey{},
        this,
        lifetime_.ticket(),
        std::move(package),
        snapshot.plan());
}

core::UniquePtr<ComputePipeline> BindlessCrossExtension::createComputePipeline(
    std::shared_ptr<const BindlessAuthorization>,
    core::StringView,
    core::pmr::String& diagnostic) const
{
    diagnostic = "RHI-BINDLESS-PIPELINE-EXECUTION-UNAVAILABLE";
    return {};
}

core::UniquePtr<RenderPipeline> BindlessCrossExtension::createRenderPipeline(
    std::shared_ptr<const BindlessAuthorization>,
    const RenderPipelineDesc&,
    core::StringView,
    core::StringView,
    core::pmr::String& diagnostic) const
{
    diagnostic = "RHI-BINDLESS-PIPELINE-EXECUTION-UNAVAILABLE";
    return {};
}

core::UniquePtr<RenderPipeline> BindlessCrossExtension::createRenderPipeline(
    std::shared_ptr<const BindlessAuthorization>,
    const RenderPipelineDesc&,
    core::StringView,
    core::StringView,
    const BindlessGraphicsPushRequest& request,
    core::pmr::String& diagnostic) const
{
    const auto* id = request.diagnostic();
    diagnostic = id ? id : "RHI-BINDLESS-GRAPHICS-PUSH-PIPELINE-UNAVAILABLE";
    return {};
}

} // namespace arp::rhi
