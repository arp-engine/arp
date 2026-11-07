#pragma once
#include "scriptvm/runtime/detail/indexed_reference_domain.h"
#include "scriptvm/runtime/value.h"

namespace arp::scriptvm::detail {
#ifdef ARP_VM_REFERENCE_DIAGNOSTICS
struct ReferenceDomainCounters {
    std::size_t imports = 0, adoptions = 0, clones = 0, exports = 0, drops = 0;
};

inline thread_local ReferenceDomainCounters referenceDomainCounters;
#define ARP_VM_REFERENCE_EVENT(event) (++::arp::scriptvm::detail::referenceDomainCounters.event)
#else
#define ARP_VM_REFERENCE_EVENT(event) ((void)0)
#endif

struct ReferenceValueTraits {
    static bool unique(const Value& value) { return value.hasUniqueStorage(); }

    static const void* identity(const Value& value) { return value.storageIdentity(); }
};

// Owned by one pooled Frame; all public slot/Frame moves publish ordinary Values.
// Indices returned to slots never name a public Carrier. A reserved destination
// cannot be located by destructor callbacks, even while the old owner retires.
class ExecutionReferenceDomain final {
    using Domain = PrivateDomain<Value, ReferenceValueTraits, 0, 0>;

public:
    static constexpr std::size_t absent = std::numeric_limits<std::size_t>::max();

    explicit ExecutionReferenceDomain(const std::shared_ptr<core::CpuMemoryQuota>* quota)
        : domain_(&core::defaultCpuMemoryResource(), quota && *quota), quota_(quota)
    {}

    bool onOwnerThread() const noexcept { return domain_.onOwnerThread(); }

    bool prepare(std::size_t count, const std::shared_ptr<core::CpuMemoryQuota>* quota)
    {
        assert(empty());
        quota_ = quota; // Refresh after an inactive Vm move; never retain its old member address.
        return prepare(count);
    }

    bool prepare(std::size_t count)
    {
        if (!domain_.ready() || !onOwnerThread())
            return false;
        return grow(count);
    }

    ~ExecutionReferenceDomain() { assert(live_ == 0); }

    bool empty() const { return live_ == 0; }

    const Value& inspect(std::size_t index) const { return domain_.inspectPrivate(index); }

    std::size_t import(const Value& value)
    {
        ARP_VM_REFERENCE_EVENT(imports);
        const auto index = reserve();
        if (index == absent)
            return absent;
        const bool ok = domain_.import(index, value);
        assert(ok);
        return index;
    }

    std::size_t adopt(Value&& value)
    {
        ARP_VM_REFERENCE_EVENT(adoptions);
        const auto index = reserve();
        if (index == absent)
            return absent;
        const bool ok = domain_.adoptEmpty(index, std::move(value));
        assert(ok);
        return index;
    }

    std::size_t clone(std::size_t source)
    {
        ARP_VM_REFERENCE_EVENT(clones);
        const auto index = reserve();
        if (index == absent)
            return absent;
        const bool ok = domain_.copy(source, index);
        assert(ok);
        return index;
    }

    Value take(std::size_t index) noexcept
    {
        ARP_VM_REFERENCE_EVENT(exports);
        auto result = domain_.takeOut(index);
        releaseIndex(index);
        return result;
    }

    void drop(std::size_t index) noexcept
    {
        ARP_VM_REFERENCE_EVENT(drops);
        // Keep the index leased until its destructor/reentry has completed.
        domain_.drop(index);
        releaseIndex(index);
    }

private:
    bool grow(std::size_t count)
    {
        if (count <= domain_.slotCount())
            return true;
        if ((quota_ && *quota_) || core::currentCpuMemoryQuota())
            return false;
        if (count > free_.max_size())
            return false;
        try {
            free_.reserve(count);
        } catch (const std::bad_alloc&) {
            return false;
        }
        const auto old = domain_.slotCount();
        if (!domain_.growMetadata(count, count, quota_ && *quota_))
            return false;
        for (auto i = count; i > old; --i)
            free_.push_back(i - 1);
        return true;
    }

    std::size_t reserve()
    {
        if (free_.empty()) {
            const auto old = domain_.slotCount();
            if (old > std::numeric_limits<std::size_t>::max() / 2)
                return absent;
            if (!grow(old ? old * 2 : 1))
                return absent;
        }
        auto index = free_.back();
        free_.pop_back();
        ++live_;
        return index;
    }

    void releaseIndex(std::size_t index) noexcept
    {
        assert(live_ && free_.size() < free_.capacity());
        --live_;
        free_.push_back(index);
    }

    Domain domain_;
    core::pmr::Vector<std::size_t> free_{&core::defaultCpuMemoryResource()};
    const std::shared_ptr<core::CpuMemoryQuota>* quota_;
    std::size_t live_ = 0;
};
} // namespace arp::scriptvm::detail

#undef ARP_VM_REFERENCE_EVENT
