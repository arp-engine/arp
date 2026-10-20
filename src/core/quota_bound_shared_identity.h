#pragma once
#include "core/cpu_allocator.h"

#include <type_traits>

namespace arp::core {
// A cold original object may gain shared ownership only under the quota that
// paid for its original metadata. Direct shareUnique is intentionally not an
// admission proof for adapters requiring this extra budget contract.
class QuotaBoundSharedObjectIdentity : public SharedObjectIdentity {
public:
    template <class T> static std::shared_ptr<T> shareOriginal(UniquePtr<T> object)
    {
        static_assert(std::is_base_of_v<QuotaBoundSharedObjectIdentity, T>);
        if (!object || !object->originalQuota_)
            return {};
        CpuMemoryQuotaScope scope(object->originalQuota_);
        auto shared = shareUnique(std::move(object));
        shared->quotaBoundShared_ = true;
        return shared;
    }

    template <class T> bool hasQuotaBoundSharedOwner(const std::shared_ptr<T>& owner) const noexcept
    {
        return quotaBoundShared_ && originalQuota_ && sharedOwnerMatches(owner);
    }

    const auto& originalMetadataQuota() const noexcept { return originalQuota_; }

protected:
    QuotaBoundSharedObjectIdentity() : originalQuota_(currentCpuMemoryQuota()) {}

private:
    const std::shared_ptr<CpuMemoryQuota> originalQuota_;
    bool quotaBoundShared_ = false;
};
} // namespace arp::core
