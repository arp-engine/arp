#include "rhi/frontend/bindless_binding_preparation.h"

#include <array>
#include <limits>

namespace arp::rhi {

const char* bindlessBindingDiagnostic(
    BindlessBindingDomain domain, BindlessBindingDiagnostic reason) noexcept
{
    constexpr std::array compute{"RHI-BINDLESS-PREPARED-COMPUTE-STALE",
        "RHI-BINDLESS-PREPARED-COMPUTE-UNAVAILABLE",
        "RHI-BINDLESS-PREPARED-COMPUTE-BUDGET",
        "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISSING",
        "RHI-BINDLESS-PREPARED-COMPUTE-OWNERS-MISMATCH",
        "RHI-BINDLESS-PREPARED-COMPUTE-AUTHORIZATION-INVALID",
        "RHI-BINDLESS-PREPARED-COMPUTE-PLAN-MISMATCH",
        "RHI-BINDLESS-PREPARED-COMPUTE-FOOTPRINT",
        "RHI-BINDLESS-PREPARED-COMPUTE-POOL-FULL",
        "RHI-BINDLESS-PREPARED-COMPUTE-RESIDENT-BUDGET",
        "RHI-BINDLESS-PREPARED-COMPUTE-PAYLOAD-MISMATCH",
        "RHI-BINDLESS-PREPARED-COMPUTE-PIPELINE-MISMATCH",
        "RHI-BINDLESS-PREPARED-COMPUTE-STAGE-MISMATCH",
        "RHI-BINDLESS-PREPARED-COMPUTE-COMMAND-BUDGET"};
    constexpr std::array graphics{"RHI-BINDLESS-PREPARED-GRAPHICS-STALE",
        "RHI-BINDLESS-PREPARED-GRAPHICS-UNAVAILABLE",
        "RHI-BINDLESS-PREPARED-GRAPHICS-BUDGET",
        "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISSING",
        "RHI-BINDLESS-PREPARED-GRAPHICS-OWNERS-MISMATCH",
        "RHI-BINDLESS-PREPARED-GRAPHICS-AUTHORIZATION-INVALID",
        "RHI-BINDLESS-PREPARED-GRAPHICS-PLAN-MISMATCH",
        "RHI-BINDLESS-PREPARED-GRAPHICS-FOOTPRINT",
        "RHI-BINDLESS-PREPARED-GRAPHICS-POOL-FULL",
        "RHI-BINDLESS-PREPARED-GRAPHICS-RESIDENT-BUDGET",
        "RHI-BINDLESS-PREPARED-GRAPHICS-PAYLOAD-MISMATCH",
        "RHI-BINDLESS-PREPARED-GRAPHICS-PIPELINE-MISMATCH",
        "RHI-BINDLESS-PREPARED-GRAPHICS-STAGE-MISMATCH",
        "RHI-BINDLESS-PREPARED-GRAPHICS-COMMAND-BUDGET"};
    static_assert(compute.size() == graphics.size());
    return (domain == BindlessBindingDomain::Compute ? compute
                                                     : graphics)[static_cast<std::size_t>(reason)];
}

ValidationResult validateBindlessBindingOwners(BindlessBindingDomain domain,
    bool optIn,
    const std::shared_ptr<const BindlessSnapshot>& snapshot,
    const BindlessLoweringPlan& plan,
    std::uint64_t generation,
    const std::shared_ptr<const BindlessAuthorization>& authorization,
    const std::shared_ptr<const BindlessAuthorization>& pipelineAuthorization)
{
    if (!authorization || !authorization->current(authorization))
        return {false, bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::Stale)};
    if (!optIn || !snapshot || pipelineAuthorization != authorization)
        return {
            false, bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::OwnersMismatch)};
    if (!snapshot->sharedOwnerMatches(snapshot) || !authorization->sharedOwnerMatches(authorization)
        || !pipelineAuthorization->sharedOwnerMatches(pipelineAuthorization))
        return {
            false, bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::OwnersMismatch)};
    if (!bindlessPlansMatch(plan, authorization->plan())
        || !bindlessPlansMatch(plan, snapshot->plan()) || generation != snapshot->generation())
        return {false, bindlessBindingDiagnostic(domain, BindlessBindingDiagnostic::PlanMismatch)};
    return snapshot->validate();
}

BindlessBindingPreparationPool::BindlessBindingPreparationPool(const BindlessCrossExtension& issuer,
    BindlessBindingBudget budget,
    BindlessBindingDomain domain)
    : issuer_(&issuer), lifetime_(issuer.lifetime_.ticket()), budget_(budget), domain_(domain),
      counts_(core::makeShared<core::AtomicCapacityBudget>(budget.maxBindings)),
      bytes_(core::makeShared<core::AtomicCapacityBudget>(budget.maxResidentBytes))
{}

BindlessBindingReservation BindlessBindingPreparationPool::prepare(bool optIn,
    const std::shared_ptr<const BindlessSnapshot>& snapshot,
    bool pipelinePresent,
    const BindlessLoweringPlan& plan,
    std::uint64_t generation,
    std::shared_ptr<const BindlessAuthorization> authorization,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const auto fail = [&](BindlessBindingDiagnostic reason) -> BindlessBindingReservation {
        diagnostic = bindlessBindingDiagnostic(domain_, reason);
        return {};
    };
    if (!optIn) {
        diagnostic = "RHI-BINDLESS-OPT-IN-REQUIRED";
        return {};
    }
    if (!lifetime_.isCurrent())
        return fail(BindlessBindingDiagnostic::Stale);
    const auto& caps = issuer_->capabilities();
    const auto version = domain_ == BindlessBindingDomain::Compute
        ? caps.preparedComputeBindingContractVersion
        : caps.preparedGraphicsBindingContractVersion;
    if (version != 1u)
        return fail(BindlessBindingDiagnostic::Unavailable);
    if (!budget_.maxBindings || !budget_.maxResidentBytes || !budget_.maxBindingBytes
        || budget_.maxBindingBytes > budget_.maxResidentBytes || !budget_.maxArgumentBuffers
        || !budget_.maxArgumentBufferBytes)
        return fail(BindlessBindingDiagnostic::Budget);
    if (!snapshot || !pipelinePresent)
        return fail(BindlessBindingDiagnostic::OwnersMissing);
    if (!snapshot->sharedOwnerMatches(snapshot))
        return fail(BindlessBindingDiagnostic::OwnersMismatch);
    if (!authorization || !authorization->issuedBy(issuer_, authorization))
        return fail(BindlessBindingDiagnostic::AuthorizationInvalid);
    if (!bindlessPlansMatch(plan, authorization->plan())
        || !bindlessPlansMatch(plan, snapshot->plan()) || generation != snapshot->generation())
        return fail(BindlessBindingDiagnostic::PlanMismatch);
    const auto valid = snapshot->validatePackage(*issuer_, authorization->package().artifact());
    if (!valid.success) {
        diagnostic = valid.diagnostics;
        return {};
    }
    const auto sourceBytes = authorization->package().sourceBytes();
    if (sourceBytes > std::numeric_limits<std::uint64_t>::max() - plan.snapshotBytes)
        return fail(BindlessBindingDiagnostic::Footprint);
    const auto retained = plan.snapshotBytes + sourceBytes;
    if (retained > budget_.maxBindingBytes
        || budget_.maxArgumentBufferBytes > budget_.maxBindingBytes - retained)
        return fail(BindlessBindingDiagnostic::Footprint);
    auto count = core::makeShared<core::AtomicCapacityLease>(counts_, 1u);
    if (!count->charged())
        return fail(BindlessBindingDiagnostic::PoolFull);
    auto bytes = core::makeShared<core::AtomicCapacityLease>(bytes_, budget_.maxBindingBytes);
    if (!bytes->charged())
        return fail(BindlessBindingDiagnostic::ResidentBudget);
    return {std::move(authorization), std::move(count), std::move(bytes)};
}

ValidationResult BindlessBindingPreparationPool::validatePayload(
    bool ownersMatch, std::uint32_t argumentCount, std::uint64_t argumentBytes) const
{
    if (!ownersMatch || argumentCount > budget_.maxArgumentBuffers
        || argumentBytes > budget_.maxArgumentBufferBytes)
        return {
            false, bindlessBindingDiagnostic(domain_, BindlessBindingDiagnostic::PayloadMismatch)};
    return {true, {}};
}

} // namespace arp::rhi
