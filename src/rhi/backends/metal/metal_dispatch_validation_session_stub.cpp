#include "rhi/backends/metal/metal_dispatch_validation_session.h"

namespace arp::rhi_metal {
MetalDispatchValidationSession::~MetalDispatchValidationSession() = default;

std::shared_ptr<rhi::OriginalComputeDispatchValidation> MetalDispatchValidationSession::prepareL2(
    rhi::GpuDevice&,
    const rhi::ComputeDispatchValidationRequest&,
    const Budget&,
    Quotas,
    const char*& diagnostic)
{
    diagnostic = "RHI-DISPATCH-SESSION-NATIVE-UNAVAILABLE";
    return {};
}

std::shared_ptr<const rhi::ComputeDispatchValidationDomain>
MetalDispatchValidationSession::issueDomain(rhi::GpuDevice&,
    const rhi::ComputeDispatchValidationDomainBudget&,
    std::shared_ptr<core::CpuMemoryQuota>,
    const char*& diagnostic) const
{
    diagnostic = "RHI-DISPATCH-SESSION-NATIVE-UNAVAILABLE";
    return {};
}

std::shared_ptr<MetalDispatchValidationSession> MetalDispatchValidationSession::prepare(
    rhi::GpuDevice&,
    const rhi::ComputeDispatchValidationRequest&,
    const Budget&,
    Quotas,
    const char*& diagnostic)
{
    diagnostic = "RHI-DISPATCH-SESSION-NATIVE-UNAVAILABLE";
    return {};
}

const char* MetalDispatchValidationSession::advance()
{
    return "RHI-DISPATCH-SESSION-NATIVE-UNAVAILABLE";
}

const char* MetalDispatchValidationSession::cancel() noexcept
{
    return "RHI-DISPATCH-SESSION-NATIVE-UNAVAILABLE";
}

MetalDispatchValidationSession::Observation MetalDispatchValidationSession::observe() const noexcept
{
    return {"RHI-DISPATCH-SESSION-NATIVE-UNAVAILABLE"};
}
} // namespace arp::rhi_metal
