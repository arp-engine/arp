#include "core/diagnostics/diagnostic_error.h"
#include "core/ownership/monotonic_id_sequence.h"
#include "rhi/gpu_resources.h"

namespace arp::rhi {

GpuResourceId allocateGpuResourceId()
{
    static core::MonotonicIdSequence identities;
    const auto value = identities.tryAcquire();
    if (value == 0u) {
        throw core::DiagnosticError("RHI-RESOURCE-ID-EXHAUSTED");
    }
    return {value};
}

} // namespace arp::rhi
