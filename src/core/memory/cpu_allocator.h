#pragma once

#include "core/api.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <forward_list>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <new>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace arp::core {

enum class CpuAllocationIntent : std::uint32_t {
    General,
    FrameTemp,
    AssetImport,
    ImageDecode,
    ShaderCompile,
    RhiHostStaging,
    Telemetry,
    Count
};

struct CpuIntentMemoryStats {
    std::uint64_t liveBytes = 0;
    std::uint64_t peakBytes = 0;
    std::uint64_t allocationCount = 0;
    std::uint64_t deallocationCount = 0;
    std::uint64_t failureCount = 0;
};

struct ARP_API CpuMemoryStats {
    std::uint64_t liveBytes = 0;
    std::uint64_t peakBytes = 0;
    std::uint64_t allocationCount = 0;
    std::uint64_t deallocationCount = 0;
    std::uint64_t failureCount = 0;
    std::array<CpuIntentMemoryStats, static_cast<std::size_t>(CpuAllocationIntent::Count)>
        intentStats{};

    const CpuIntentMemoryStats& byIntent(CpuAllocationIntent intent) const;
};

struct CpuAllocationDesc {
    std::size_t sizeBytes = 0;
    std::size_t alignment = alignof(std::max_align_t);
    CpuAllocationIntent intent = CpuAllocationIntent::General;
    std::string_view label;
};

struct CpuAllocationResult {
    void* ptr = nullptr;
    std::string diagnostics;

    bool success() const { return ptr != nullptr; }
};

struct CpuAllocationAlignmentResult {
    bool success = false;
    std::size_t alignedValue = 0;
    const char* diagnostics = nullptr;
};

class ARP_API CpuMemoryQuota final {
public:
    // A zero limit disables rejection while retaining live/peak accounting.
    explicit CpuMemoryQuota(std::uint64_t limitBytes = 0u) noexcept : limitBytes_(limitBytes) {}

    bool tryReserve(std::size_t bytes) noexcept;
    void release(std::size_t bytes) noexcept;

    void setLimitBytes(std::uint64_t limitBytes) noexcept
    {
        limitBytes_.store(limitBytes, std::memory_order_release);
    }

    std::uint64_t liveBytes() const noexcept { return liveBytes_.load(); }

    std::uint64_t peakBytes() const noexcept { return peakBytes_.load(); }

    std::uint64_t limitBytes() const noexcept
    {
        return limitBytes_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::uint64_t> limitBytes_{0u};
    std::atomic<std::uint64_t> liveBytes_{0u};
    std::atomic<std::uint64_t> peakBytes_{0u};
};

// Move-only ownership of an explicit CpuMemoryQuota reservation. Unlike a
// quota scope, a lease is independent of thread-local execution state and can
// be resized atomically by reserving or releasing only the delta.
class ARP_API CpuMemoryQuotaLease final {
public:
    CpuMemoryQuotaLease() noexcept = default;

    CpuMemoryQuotaLease(std::shared_ptr<CpuMemoryQuota> quota, std::size_t bytes) noexcept
    {
        (void)acquire(std::move(quota), bytes);
    }

    ~CpuMemoryQuotaLease() { release(); }

    CpuMemoryQuotaLease(const CpuMemoryQuotaLease&) = delete;
    CpuMemoryQuotaLease& operator=(const CpuMemoryQuotaLease&) = delete;
    CpuMemoryQuotaLease(CpuMemoryQuotaLease&& other) noexcept;
    CpuMemoryQuotaLease& operator=(CpuMemoryQuotaLease&& other) noexcept;

    bool acquire(std::shared_ptr<CpuMemoryQuota> quota, std::size_t bytes) noexcept;
    bool resize(std::size_t bytes) noexcept;
    void release() noexcept;

    const std::shared_ptr<CpuMemoryQuota>& quota() const noexcept { return quota_; }

    std::size_t bytes() const noexcept { return reservedBytes_; }

    bool charged() const noexcept { return reservedBytes_ != 0u; }

private:
    bool transferReservation(std::size_t bytes, std::shared_ptr<CpuMemoryQuota>& quota) noexcept;

    std::shared_ptr<CpuMemoryQuota> quota_;
    std::size_t reservedBytes_ = 0u;

    friend class CpuQuotaPrechargedAllocationScope;
};

// An exact allocation query for pointers returned by defaultCpuAllocator().
// The function intentionally rejects interior and foreign pointers.
struct CpuAllocationChargeInfo {
    bool found = false;
    const void* allocationIdentity = nullptr;
    std::size_t sizeBytes = 0u;
    const CpuMemoryQuota* quota = nullptr;
};

ARP_API CpuAllocationChargeInfo inspectCpuAllocationCharge(const void* exactAllocation) noexcept;
ARP_API CpuAllocationChargeInfo inspectCpuAllocationContaining(const void* address) noexcept;
// Exact allocation must remain alive during inspection. Checks both quota
// object and owning control block without retaining another domain inventory.
ARP_API bool cpuAllocationOverlapsQuotaOwner(
    const void* exactAllocation, const std::shared_ptr<CpuMemoryQuota>& quota) noexcept;
ARP_API bool transferCpuAllocationCharge(
    void* chargedAllocation, void* deferredAllocation) noexcept;

// The scope is thread-local and nestable. Allocation headers retain the
// shared quota, so values that outlive the execution scope still release the
// exact bytes they reserved.
class ARP_API CpuMemoryQuotaScope final {
public:
    explicit CpuMemoryQuotaScope(std::shared_ptr<CpuMemoryQuota> quota) noexcept;
    ~CpuMemoryQuotaScope();
    CpuMemoryQuotaScope(const CpuMemoryQuotaScope&) = delete;
    CpuMemoryQuotaScope& operator=(const CpuMemoryQuotaScope&) = delete;

    // Compare the installed object and its ownership without reading TLS or
    // retaining another owner. Aliasing shared pointers can share an address
    // while referring to different quota lifetimes.
    bool matchesQuota(const std::shared_ptr<CpuMemoryQuota>& quota) const noexcept
    {
        return quota_.get() == quota.get() && !quota_.owner_before(quota)
            && !quota.owner_before(quota_);
    }

private:
    std::shared_ptr<CpuMemoryQuota> quota_;
    CpuMemoryQuotaScope* previous_ = nullptr;

    friend std::shared_ptr<CpuMemoryQuota> currentCpuMemoryQuota();
};

// Internal bridge used by makeQuotaPrechargedUnique. The reservation is
// transferred into exactly one allocator header; constructor allocations are
// unaffected and continue to use the active thread-local quota.
class ARP_API CpuQuotaPrechargedAllocationScope final {
public:
    explicit CpuQuotaPrechargedAllocationScope(CpuMemoryQuotaLease&& lease) noexcept;
    ~CpuQuotaPrechargedAllocationScope();
    CpuQuotaPrechargedAllocationScope(const CpuQuotaPrechargedAllocationScope&) = delete;
    CpuQuotaPrechargedAllocationScope& operator=(const CpuQuotaPrechargedAllocationScope&) = delete;

private:
    std::shared_ptr<CpuMemoryQuota> consume(std::size_t bytes) noexcept;

    CpuMemoryQuotaLease lease_;
    CpuQuotaPrechargedAllocationScope* previous_ = nullptr;

    friend class DefaultCpuAllocator;
};

// Host-owned allocations can use this lease while a ScriptVM execution is
// active. It charges the current thread-local CpuMemoryQuota when one is
// installed, and keeps that quota alive until the host releases the lease.
// With no active script quota the lease is an unlimited no-op.
class ARP_API CpuExternalAllocationLease final {
public:
    CpuExternalAllocationLease() noexcept = default;

    explicit CpuExternalAllocationLease(std::size_t bytes) noexcept { (void)acquire(bytes); }

    ~CpuExternalAllocationLease() { reset(); }

    CpuExternalAllocationLease(const CpuExternalAllocationLease&) = delete;
    CpuExternalAllocationLease& operator=(const CpuExternalAllocationLease&) = delete;

    CpuExternalAllocationLease(CpuExternalAllocationLease&& other) noexcept
        : lease_(std::move(other.lease_))
    {}

    CpuExternalAllocationLease& operator=(CpuExternalAllocationLease&& other) noexcept
    {
        if (this == &other)
            return *this;
        lease_ = std::move(other.lease_);
        return *this;
    }

    bool acquire(std::size_t bytes) noexcept;
    void reset() noexcept;

    bool charged() const noexcept { return lease_.charged(); }

    std::size_t bytes() const noexcept { return lease_.bytes(); }

private:
    CpuMemoryQuotaLease lease_;
};

inline bool isValidCpuAllocationAlignment(std::size_t alignment) noexcept
{
    return alignment != 0u && (alignment & (alignment - 1u)) == 0u;
}

inline CpuAllocationAlignmentResult alignCpuAllocationOffsetUp(
    std::size_t value, std::size_t alignment) noexcept
{
    if (!isValidCpuAllocationAlignment(alignment)) {
        return {
            false,
            value,
            "allocation alignment must be a non-zero power of two",
        };
    }
    if (alignment == 1u) {
        return {true, value, nullptr};
    }

    const auto mask = alignment - 1u;
    if (value > std::numeric_limits<std::size_t>::max() - mask) {
        return {
            false,
            value,
            "allocation alignment overflows addressable range",
        };
    }
    return {true, (value + mask) & ~mask, nullptr};
}

class ARP_API CpuAllocator {
public:
    virtual ~CpuAllocator() = default;

    virtual CpuAllocationResult allocate(const CpuAllocationDesc& desc) = 0;
    virtual void deallocate(void* ptr, const CpuAllocationDesc& desc) noexcept = 0;
    virtual CpuMemoryStats stats() const = 0;
    virtual std::string name() const = 0;
    virtual std::string backendName() const = 0;
    virtual std::string lastFailureDiagnostics() const = 0;
};

class ARP_API DefaultCpuAllocator final : public CpuAllocator {
public:
    explicit DefaultCpuAllocator(std::string_view allocatorName = "DefaultCpuAllocator");
    ~DefaultCpuAllocator() override;
    DefaultCpuAllocator(const DefaultCpuAllocator&) = delete;
    DefaultCpuAllocator& operator=(const DefaultCpuAllocator&) = delete;
    DefaultCpuAllocator(DefaultCpuAllocator&&) = delete;
    DefaultCpuAllocator& operator=(DefaultCpuAllocator&&) = delete;

    CpuAllocationResult allocate(const CpuAllocationDesc& desc) override;
    void deallocate(void* ptr, const CpuAllocationDesc& desc) noexcept override;
    CpuMemoryStats stats() const override;
    std::string name() const override;
    std::string backendName() const override;
    std::string lastFailureDiagnostics() const override;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

using CpuAllocateCallback = void* (*)(void* userData, const CpuAllocationDesc& desc);
using CpuDeallocateCallback = void (*)(void* userData, void* ptr, const CpuAllocationDesc& desc);
using CpuStatsCallback = CpuMemoryStats (*)(void* userData);

struct CpuAllocatorCallbacks {
    void* userData = nullptr;
    CpuAllocateCallback allocate = nullptr;
    CpuDeallocateCallback deallocate = nullptr;
    CpuStatsCallback stats = nullptr;
};

class ARP_API CallbackCpuAllocator final : public CpuAllocator {
public:
    CallbackCpuAllocator(std::string allocatorName, CpuAllocatorCallbacks callbacks);
    CallbackCpuAllocator(const CallbackCpuAllocator&) = delete;
    CallbackCpuAllocator& operator=(const CallbackCpuAllocator&) = delete;
    CallbackCpuAllocator(CallbackCpuAllocator&&) = delete;
    CallbackCpuAllocator& operator=(CallbackCpuAllocator&&) = delete;

    CpuAllocationResult allocate(const CpuAllocationDesc& desc) override;
    void deallocate(void* ptr, const CpuAllocationDesc& desc) noexcept override;
    CpuMemoryStats stats() const override;
    std::string name() const override;
    std::string backendName() const override;
    std::string lastFailureDiagnostics() const override;

private:
    void recordLocalAllocation(const CpuAllocationDesc& desc);
    void recordLocalDeallocation(const CpuAllocationDesc& desc) noexcept;
    void recordLocalFailure(const CpuAllocationDesc& desc, const std::string& diagnostics) const;

    std::string name_;
    CpuAllocatorCallbacks callbacks_;
    mutable std::mutex mutex_;
    mutable CpuMemoryStats localStats_;
    mutable std::string lastFailureDiagnostics_;
};

class ARP_API CpuMemoryResource final : public std::pmr::memory_resource {
public:
    CpuMemoryResource(CpuAllocator& allocator, CpuAllocationIntent intent, std::string label = {});
    CpuMemoryResource(const CpuMemoryResource&) = delete;
    CpuMemoryResource& operator=(const CpuMemoryResource&) = delete;
    CpuMemoryResource(CpuMemoryResource&&) = delete;
    CpuMemoryResource& operator=(CpuMemoryResource&&) = delete;

    CpuAllocator& allocator() const;
    CpuAllocationIntent intent() const;
    const std::string& label() const;

private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override;
    void do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment) override;
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    CpuAllocator& allocator_;
    CpuAllocationIntent intent_ = CpuAllocationIntent::General;
    std::string label_;
};

namespace pmr {

template <class T> using Vector = std::pmr::vector<T>;

using String = std::pmr::string;

template <class Key, class Value, class Hash = std::hash<Key>, class KeyEqual = std::equal_to<Key>>
using UnorderedMap = std::pmr::unordered_map<Key, Value, Hash, KeyEqual>;

template <class Key, class Value, class Compare = std::less<Key>>
using Map = std::pmr::map<Key, Value, Compare>;

template <class Key, class Compare = std::less<Key>> using Set = std::pmr::set<Key, Compare>;

template <class Key, class Hash = std::hash<Key>, class KeyEqual = std::equal_to<Key>>
using UnorderedSet = std::pmr::unordered_set<Key, Hash, KeyEqual>;

template <class T> using Deque = std::pmr::deque<T>;

template <class T> using List = std::pmr::list<T>;

template <class T> using ForwardList = std::pmr::forward_list<T>;

} // namespace pmr

ARP_API const char* cpuAllocationIntentName(CpuAllocationIntent intent);
ARP_API DefaultCpuAllocator& defaultCpuAllocator();
ARP_API std::pmr::memory_resource& defaultCpuMemoryResource();
ARP_API std::shared_ptr<CpuMemoryQuota> currentCpuMemoryQuota();
ARP_API void notifyCpuAllocatorProcessShutdown() noexcept;
ARP_API void installDefaultCpuAllocatorAsPmrDefault();
ARP_API std::string toStdString(const std::pmr::string& text);
ARP_API std::pmr::string toPmrString(const std::string& text);

class SharedObjectIdentity;

template <class T> using SharedPtr = std::shared_ptr<T>;
template <class T> using WeakPtr = std::weak_ptr<T>;

template <class T, class... Args> std::shared_ptr<T> makeShared(Args&&... args)
{
    auto result =
        std::allocate_shared<T>(std::pmr::polymorphic_allocator<T>{&defaultCpuMemoryResource()},
            std::forward<Args>(args)...);
    if constexpr (std::is_base_of_v<SharedObjectIdentity, T>)
        result->bindSharedOwner(result);
    return result;
}

template <class T> class CpuAllocatedDeleter {
public:
    using DestroyFn = void (*)(void*) noexcept;

    CpuAllocatedDeleter() noexcept = default;

    CpuAllocatedDeleter(void* allocatedObject, DestroyFn destroy) noexcept
        : allocatedObject_(allocatedObject), destroy_(destroy)
    {}

    template <class U, class = std::enable_if_t<std::is_convertible<U*, T*>::value>>
    CpuAllocatedDeleter(const CpuAllocatedDeleter<U>& other) noexcept
        : allocatedObject_(other.allocatedObject_), destroy_(other.destroy_)
    {}

    void operator()(T* ptr) const noexcept
    {
        if (ptr == nullptr || destroy_ == nullptr) {
            return;
        }
        destroy_(allocatedObject_ != nullptr ? allocatedObject_ : static_cast<void*>(ptr));
    }

private:
    template <class U> friend class CpuAllocatedDeleter;

    void* allocatedObject_ = nullptr;
    DestroyFn destroy_ = nullptr;
};

template <class T> using UniquePtr = std::unique_ptr<T, CpuAllocatedDeleter<T>>;

// Canonical weak ownership for objects created/shared through core factories.
// Handles the aliasing holder used by shareUnique without adding an allocation
// or relying on enable_shared_from_this, which alias constructors do not bind.
class SharedObjectIdentity {
public:
    template <class T> bool sharedOwnerMatches(const std::shared_ptr<T>& owner) const noexcept
    {
        if (!owner || static_cast<const SharedObjectIdentity*>(owner.get()) != this)
            return false;
        const auto canonical = owner_.lock();
        return canonical && !canonical.owner_before(owner) && !owner.owner_before(canonical);
    }

protected:
    SharedObjectIdentity() = default;
    SharedObjectIdentity(const SharedObjectIdentity&) = delete;
    SharedObjectIdentity& operator=(const SharedObjectIdentity&) = delete;

private:
    template <class T, class... Args> friend std::shared_ptr<T> makeShared(Args&&...);
    template <class T> friend std::shared_ptr<T> shareUnique(UniquePtr<T>);

    void bindSharedOwner(const std::shared_ptr<const void>& owner) noexcept
    {
        owner_ = std::shared_ptr<const void>(owner, this);
    }

    std::weak_ptr<const void> owner_;
};

// Preserve the existing core-owned object and allocate only its shared control
// holder through the same allocator. Aliasing shared_ptr adds no allocation.
template <class T> std::shared_ptr<T> shareUnique(UniquePtr<T> object)
{
    if (!object)
        return {};
    auto holder = makeShared<UniquePtr<T>>(std::move(object));
    auto result = std::shared_ptr<T>(holder, holder->get());
    if constexpr (std::is_base_of_v<SharedObjectIdentity, T>)
        result->bindSharedOwner(result);
    return result;
}

// Replace an object in an already-owned CPU storage slot. This changes only
// object lifetime, never storage ownership or allocation. Move construction
// preserves allocator-bearing members without fallible memberwise assignment.
template <class T> void replaceCpuObjectByMove(T& destination, T& source) noexcept
{
    static_assert(std::is_nothrow_move_constructible_v<T>);
    static_assert(std::is_nothrow_destructible_v<T>);
    if (std::addressof(destination) == std::addressof(source))
        return;
    auto* slot = std::addressof(destination);
    slot->~T();
    ::new (static_cast<void*>(slot)) T(std::move(source));
}

template <class T> void destroyCpuAllocatedUnique(void* ptr) noexcept
{
    auto* object = static_cast<T*>(ptr);
    std::pmr::polymorphic_allocator<T> allocator{&defaultCpuMemoryResource()};
    object->~T();
    allocator.deallocate(object, 1);
}

template <class T, class... Args> UniquePtr<T> makeUnique(Args&&... args)
{
    std::pmr::polymorphic_allocator<T> allocator{&defaultCpuMemoryResource()};
    auto* object = allocator.allocate(1);
    try {
        ::new (static_cast<void*>(object)) T(std::forward<Args>(args)...);
    } catch (...) {
        allocator.deallocate(object, 1);
        throw;
    }
    return UniquePtr<T>{
        object,
        CpuAllocatedDeleter<T>{object, &destroyCpuAllocatedUnique<T>},
    };
}

template <class T, class... Args>
UniquePtr<T> makeQuotaPrechargedUnique(CpuMemoryQuotaLease&& lease, Args&&... args)
{
    if (lease.bytes() != sizeof(T) || lease.quota() == nullptr)
        throw std::bad_alloc();
    std::pmr::polymorphic_allocator<T> allocator{&defaultCpuMemoryResource()};
    CpuQuotaPrechargedAllocationScope precharged{std::move(lease)};
    auto* object = allocator.allocate(1);
    try {
        ::new (static_cast<void*>(object)) T(std::forward<Args>(args)...);
    } catch (...) {
        allocator.deallocate(object, 1);
        throw;
    }
    return UniquePtr<T>{
        object,
        CpuAllocatedDeleter<T>{object, &destroyCpuAllocatedUnique<T>},
    };
}

// Creates the outer object without charging the active quota. The caller must
// publish it only after transferCpuAllocationCharge moves an existing charge
// to this exact allocation. Constructor/internal PMR allocations still use
// the active quota because only allocator.allocate() is scoped out.
template <class T, class... Args> UniquePtr<T> makeQuotaDeferredUnique(Args&&... args)
{
    std::pmr::polymorphic_allocator<T> allocator{&defaultCpuMemoryResource()};
    T* object = nullptr;
    {
        CpuMemoryQuotaScope outerAllocationScope{nullptr};
        object = allocator.allocate(1);
    }
    try {
        ::new (static_cast<void*>(object)) T(std::forward<Args>(args)...);
    } catch (...) {
        allocator.deallocate(object, 1);
        throw;
    }
    return UniquePtr<T>{
        object,
        CpuAllocatedDeleter<T>{object, &destroyCpuAllocatedUnique<T>},
    };
}

struct ARP_API CpuPmrDefaultResourceBootstrap {
    CpuPmrDefaultResourceBootstrap();
};

inline const CpuPmrDefaultResourceBootstrap cpuPmrDefaultResourceBootstrap{};

} // namespace arp::core
