#pragma once

#include "core/memory/cpu_allocator.h"

#include <cassert>
#include <cstdint>

namespace arp::tests {

// Explicit ownership fixture; a metadata payload is not native GPU evidence.
template <class Payload, class Wrap, class... Args>
void verifyRetainedAllocationPayload(Wrap wrap, Args&&... args)
{
    std::shared_ptr<void> charge = core::makeShared<std::uint32_t>(1u);
    const std::weak_ptr<void> lifetime = charge;
    auto payload = core::makeShared<Payload>(std::forward<Args>(args)...);
    auto resource = wrap(charge, payload);
    assert(resource);
    charge.reset();
    resource.reset();
    assert(!lifetime.expired());
    payload.reset();
    assert(lifetime.expired());
}

} // namespace arp::tests
