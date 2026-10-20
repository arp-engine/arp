#include "rhi/frontend/transfer_texture_readback_batch.h"

#include "core/monotonic_id_sequence.h"

namespace arp::rhi {

std::uint64_t allocateTextureReadbackRequestId() noexcept
{
    static core::MonotonicIdSequence identities;
    return identities.tryAcquire();
}

} // namespace arp::rhi
