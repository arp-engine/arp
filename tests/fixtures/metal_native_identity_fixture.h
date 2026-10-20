#pragma once
#include "rhi/backends/metal/metal_native_resources.h"

#include <array>
#include <cassert>

namespace arp::test {
// Existing real replacement ownership also supplies alias probes with the same
// pointer but a different control block. No altered identity reaches native API.
class MetalNativeIdentityMutationProbe final {
public:
    template <class Rejected>
    static void verifyOwner(
        std::shared_ptr<void>& field, const std::shared_ptr<void>& replacement, Rejected&& rejected)
    {
        const auto saved = field;
        if (saved != replacement || saved.owner_before(replacement)
            || replacement.owner_before(saved)) {
            field = replacement;
            rejected();
            field = saved;
        }
        if (saved) {
            const auto alias = std::shared_ptr<void>(replacement, saved.get());
            assert(alias == saved && (alias.owner_before(saved) || saved.owner_before(alias)));
            field = alias;
            rejected();
            field = saved;
        }
    }

    template <class Rejected>
    static void verify(rhi_metal::MetalRhiNativeResourcePayload& resource,
        const rhi_metal::MetalRhiNativeResourcePayload& replacement,
        Rejected&& rejected)
    {
        const std::array<std::shared_ptr<void>*, 3> fields{
            &resource.nativeDevice, &resource.nativeObject, &resource.allocationReleaseToken};
        const std::array<std::shared_ptr<void>, 3> replacements{
            replacement.nativeDevice, replacement.nativeObject, replacement.allocationReleaseToken};
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const auto saved = *fields[i];
            const auto& other = replacements[i];
            if (saved != other || saved.owner_before(other) || other.owner_before(saved)) {
                *fields[i] = other;
                rejected();
                *fields[i] = saved;
            }
            const auto alias = std::shared_ptr<void>(replacement.nativeObject, saved.get());
            assert(alias == saved && (alias.owner_before(saved) || saved.owner_before(alias)));
            *fields[i] = alias;
            rejected();
            *fields[i] = saved;
        }
    }
};
} // namespace arp::test
