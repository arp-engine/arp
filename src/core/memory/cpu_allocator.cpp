#include "core/memory/cpu_allocator.h"

#include "core/memory/atomic_capacity.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <utility>

#if !defined(ARP_HAS_MIMALLOC) || !ARP_HAS_MIMALLOC
#error "arp_core requires mimalloc for DefaultCpuAllocator raw allocation"
#endif

#include <mimalloc.h>

#if defined(_WIN32) && defined(__MINGW32__)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace arp::core {

namespace {

constexpr std::uint32_t kAllocationHeaderMagic = 0x41525043u;
constexpr std::size_t kAllocatorNameMaxBytes = 127u;

struct AllocationGroupBlock;

struct AllocationHeader {
    void* raw = nullptr;
    std::size_t sizeBytes = 0;
    std::size_t alignment = alignof(std::max_align_t);
    std::shared_ptr<CpuMemoryQuota> quota;
};

// Every allocation ends with this trailer. Group children need only their
// backing lease; quota, size and raw ownership belong to the backing itself.
struct AllocationTrailer {
    CpuAllocationIntent intent = CpuAllocationIntent::General;
    std::uint32_t magic = kAllocationHeaderMagic;
    AllocationGroupBlock* group = nullptr;
};

constexpr std::size_t kAllocationHeaderBytes = sizeof(AllocationHeader) + sizeof(AllocationTrailer);
static_assert(sizeof(AllocationTrailer) % alignof(AllocationHeader) == 0);
static_assert(alignof(AllocationHeader) <= alignof(void*));
static_assert(alignof(AllocationTrailer) <= alignof(void*));

// TLS only points into the active stack scope. It has no destructor, so
// thread/process teardown never releases allocator-owned state from a TLS
// shared_ptr after static runtime objects have begun shutting down.
struct AllocationThreadState {
    CpuMemoryQuotaScope* quotaScope = nullptr;
    CpuQuotaPrechargedAllocationScope* prechargeScope = nullptr;
    void* group = nullptr;
};

thread_local AllocationThreadState allocationThreadState;

AllocationThreadState& currentAllocationThreadState() noexcept
{
#if defined(_WIN32) && defined(__MINGW32__)
    // MinGW emulated TLS takes a pthread lock even for a warm lookup. Cache
    // the address in a native slot; the C++ TLS runtime still owns storage
    // and thread-exit cleanup. The process owns this one slot permanently,
    // matching the allocator's lifetime and avoiding shutdown-order hazards.
    static const DWORD slot = [] {
        const DWORD index = TlsAlloc();
        if (index == TLS_OUT_OF_INDEXES)
            std::terminate();
        return index;
    }();
    auto* state = static_cast<AllocationThreadState*>(TlsGetValue(slot));
    if (state == nullptr) {
        state = &allocationThreadState;
        if (!TlsSetValue(slot, state))
            std::terminate();
    }
    return *state;
#else
    return allocationThreadState;
#endif
}

struct AllocationGroupBlock {
    std::atomic<std::size_t> references{1}; // The active scope owns one lease.
    AllocationGroupBlock* next = nullptr;
    std::size_t capacity = 0;
    std::size_t used = 0;
};

void releaseAllocationGroupBlock(AllocationGroupBlock* block) noexcept
{
    if (block->references.fetch_sub(1, std::memory_order_acq_rel) != 1)
        return;
    CpuAllocationDesc desc;
    desc.sizeBytes = sizeof(AllocationGroupBlock) + block->capacity;
    desc.alignment = alignof(AllocationGroupBlock);
    block->~AllocationGroupBlock();
    defaultCpuAllocator().deallocate(block, desc);
}

template <class State>
CpuAllocationResult allocateInGroup(DefaultCpuAllocator& allocator,
    State& state,
    const CpuAllocationDesc& desc,
    std::size_t sizeBytes,
    std::size_t alignment)
{
    auto* block = static_cast<AllocationGroupBlock*>(state.current);
    const auto availableAddress = [](AllocationGroupBlock* candidate) {
        return reinterpret_cast<std::uintptr_t>(candidate + 1) + candidate->used;
    };
    const auto fits = [&](AllocationGroupBlock* candidate) {
        if (!candidate)
            return false;
        const auto offset = alignCpuAllocationOffsetUp(
            availableAddress(candidate) + sizeof(AllocationTrailer), alignment);
        if (!offset.success)
            return false;
        const auto prefix = offset.alignedValue - reinterpret_cast<std::uintptr_t>(candidate + 1);
        return prefix <= candidate->capacity && sizeBytes <= candidate->capacity - prefix;
    };
    if (!fits(block)) {
        const auto needed = sizeBytes + sizeof(AllocationTrailer) + alignment - 1u;
        const auto capacity = std::max(state.nextBytes, needed);
        if (capacity > std::numeric_limits<std::size_t>::max() - sizeof(AllocationGroupBlock))
            return {nullptr, "allocation group backing size overflows addressable range"};
        CpuAllocationDesc backing = desc;
        backing.sizeBytes = sizeof(AllocationGroupBlock) + capacity;
        backing.alignment = alignof(AllocationGroupBlock);
        backing.label = "allocation-group";

        struct Pause {
            void* saved = std::exchange(currentAllocationThreadState().group, nullptr);

            ~Pause() { currentAllocationThreadState().group = saved; }
        } pause;

        auto result = allocator.allocate(backing);
        if (!result.ptr)
            return result;
        block = ::new (result.ptr) AllocationGroupBlock{};
        block->capacity = capacity;
        block->next = static_cast<AllocationGroupBlock*>(state.head);
        state.head = state.current = block;
        state.nextBytes =
            capacity <= std::numeric_limits<std::size_t>::max() / 2u ? capacity * 2u : capacity;
    }
    const auto offset =
        alignCpuAllocationOffsetUp(availableAddress(block) + sizeof(AllocationTrailer), alignment);
    auto* allocation = reinterpret_cast<void*>(offset.alignedValue);
    auto* trailer = reinterpret_cast<AllocationTrailer*>(
        static_cast<unsigned char*>(allocation) - sizeof(AllocationTrailer));
    ::new (static_cast<void*>(trailer)) AllocationTrailer{};
    trailer->group = block;
    block->used = offset.alignedValue - reinterpret_cast<std::uintptr_t>(block + 1) + sizeBytes;
    block->references.fetch_add(1, std::memory_order_relaxed);
    return {allocation, {}};
}

std::atomic_bool allocatorProcessShuttingDown{false};

class RawCpuAllocationBackend {
public:
    virtual ~RawCpuAllocationBackend() = default;
    virtual void* allocate(std::size_t sizeBytes) noexcept = 0;
    virtual void deallocate(void* ptr) noexcept = 0;
    virtual const char* name() const noexcept = 0;
};

class MimallocCpuAllocationBackend final : public RawCpuAllocationBackend {
public:
    MimallocCpuAllocationBackend() noexcept
    {
#if defined(__EMSCRIPTEN__)
        // WasmBacking/1: use emmalloc-backed OS allocations directly. Reserving
        // arenas consumes linear memory without virtual address reservation.
        // Backend construction precedes its first raw or ledger allocation.
        mi_option_set(mi_option_arena_reserve, 0);
#endif
    }

    void* allocate(std::size_t sizeBytes) noexcept override { return mi_malloc(sizeBytes); }

    void deallocate(void* ptr) noexcept override { mi_free(ptr); }

    const char* name() const noexcept override { return "mimalloc"; }
};

RawCpuAllocationBackend& defaultRawAllocationBackend()
{
    // Allocations owned by TLS and other static objects can be released during
    // process teardown. Keep the raw backend alive for the full process
    // lifetime so those late releases always have a valid dispatch target.
    alignas(MimallocCpuAllocationBackend) static std::byte
        storage[sizeof(MimallocCpuAllocationBackend)];
    static auto* backend = ::new (static_cast<void*>(storage)) MimallocCpuAllocationBackend();
    return *backend;
}

struct StatsRecorder {
    mutable std::mutex mutex;
    CpuMemoryStats stats;
    std::string lastFailureDiagnostics;
};

struct FixedText {
    std::array<char, kAllocatorNameMaxBytes + 1u> bytes{};
    std::size_t size = 0u;

    void assign(std::string_view text) noexcept
    {
        size = std::min(text.size(), kAllocatorNameMaxBytes);
        if (size > 0u) {
            std::memcpy(bytes.data(), text.data(), size);
        }
        bytes[size] = '\0';
    }

    std::string toString() const { return std::string(bytes.data(), size); }
};

std::size_t intentIndex(CpuAllocationIntent intent)
{
    const auto index = static_cast<std::size_t>(intent);
    const auto count = static_cast<std::size_t>(CpuAllocationIntent::Count);
    return index < count ? index : 0u;
}

std::size_t normalizedAllocationSize(std::size_t sizeBytes)
{
    return sizeBytes == 0u ? 1u : sizeBytes;
}

std::size_t normalizedAlignment(std::size_t alignment)
{
    return std::max(alignment, alignof(void*));
}

bool wouldAllocationOverflow(std::size_t sizeBytes, std::size_t alignment)
{
    const auto max = std::numeric_limits<std::size_t>::max();
    return sizeBytes > max - alignment || sizeBytes + alignment > max - kAllocationHeaderBytes;
}

void recordAllocation(CpuMemoryStats& stats, const CpuAllocationDesc& desc)
{
    const auto bytes = static_cast<std::uint64_t>(normalizedAllocationSize(desc.sizeBytes));
    stats.liveBytes += bytes;
    stats.peakBytes = std::max(stats.peakBytes, stats.liveBytes);
    ++stats.allocationCount;

    auto& intentStats = stats.intentStats[intentIndex(desc.intent)];
    intentStats.liveBytes += bytes;
    intentStats.peakBytes = std::max(intentStats.peakBytes, intentStats.liveBytes);
    ++intentStats.allocationCount;
}

void recordDeallocation(CpuMemoryStats& stats, std::size_t sizeBytes, CpuAllocationIntent intent)
{
    const auto bytes = static_cast<std::uint64_t>(normalizedAllocationSize(sizeBytes));
    stats.liveBytes = bytes > stats.liveBytes ? 0u : stats.liveBytes - bytes;
    ++stats.deallocationCount;

    auto& intentStats = stats.intentStats[intentIndex(intent)];
    intentStats.liveBytes = bytes > intentStats.liveBytes ? 0u : intentStats.liveBytes - bytes;
    ++intentStats.deallocationCount;
}

void recordFailure(CpuMemoryStats& stats,
    std::string& lastFailureDiagnostics,
    CpuAllocationIntent intent,
    const std::string& diagnostics)
{
    ++stats.failureCount;
    ++stats.intentStats[intentIndex(intent)].failureCount;
    lastFailureDiagnostics = diagnostics;
}

CpuAllocationResult failAllocation(
    StatsRecorder& recorder, const CpuAllocationDesc& desc, const std::string& diagnostics)
{
    std::lock_guard<std::mutex> lock(recorder.mutex);
    recordFailure(recorder.stats, recorder.lastFailureDiagnostics, desc.intent, diagnostics);
    return CpuAllocationResult{nullptr, diagnostics};
}

AllocationTrailer* trailerFromAllocation(void* ptr)
{
    auto* bytes = static_cast<unsigned char*>(ptr);
    return reinterpret_cast<AllocationTrailer*>(bytes - sizeof(AllocationTrailer));
}

AllocationHeader* headerFromAllocation(void* ptr)
{
    auto* bytes = static_cast<unsigned char*>(ptr);
    return reinterpret_cast<AllocationHeader*>(bytes - kAllocationHeaderBytes);
}

template <class T> class AllocationLedgerAllocator {
public:
    using value_type = T;

    AllocationLedgerAllocator() noexcept = default;

    template <class U> AllocationLedgerAllocator(const AllocationLedgerAllocator<U>&) noexcept {}

    T* allocate(std::size_t count)
    {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::bad_alloc();
        auto* memory = mi_malloc(count * sizeof(T));
        if (memory == nullptr)
            throw std::bad_alloc();
        return static_cast<T*>(memory);
    }

    void deallocate(T* pointer, std::size_t) noexcept { mi_free(pointer); }

    template <class U> bool operator==(const AllocationLedgerAllocator<U>&) const noexcept
    {
        return true;
    }
};

struct AllocationLedgerRecord final {
    std::size_t sizeBytes = 0u;
    const CpuMemoryQuota* quota = nullptr;
};

struct AllocationLedger final {
    using Entry = std::pair<const std::uintptr_t, AllocationLedgerRecord>;
    std::mutex mutex;
    std::map<std::uintptr_t,
        AllocationLedgerRecord,
        std::less<std::uintptr_t>,
        AllocationLedgerAllocator<Entry>>
        allocations;
};

AllocationLedger* allocationLedger() noexcept
{
    static AllocationLedger* ledger = []() noexcept {
        void* storage = mi_malloc(sizeof(AllocationLedger));
        if (storage == nullptr)
            return static_cast<AllocationLedger*>(nullptr);
        return ::new (storage) AllocationLedger{};
    }();
    return ledger;
}

bool registerAllocationLedger(
    void* allocation, std::size_t sizeBytes, const CpuMemoryQuota* quota) noexcept
{
    try {
        auto* ledger = allocationLedger();
        if (ledger == nullptr)
            return false;
        std::lock_guard<std::mutex> lock(ledger->mutex);
        return ledger->allocations
            .emplace(reinterpret_cast<std::uintptr_t>(allocation),
                AllocationLedgerRecord{sizeBytes, quota})
            .second;
    } catch (...) {
        return false;
    }
}

void unregisterAllocationLedger(void* allocation) noexcept
{
    auto* ledger = allocationLedger();
    if (ledger == nullptr)
        return;
    std::lock_guard<std::mutex> lock(ledger->mutex);
    ledger->allocations.erase(reinterpret_cast<std::uintptr_t>(allocation));
}

void updateAllocationLedgerQuota(void* allocation, const CpuMemoryQuota* quota) noexcept
{
    auto* ledger = allocationLedger();
    if (ledger == nullptr)
        return;
    std::lock_guard<std::mutex> lock(ledger->mutex);
    const auto found = ledger->allocations.find(reinterpret_cast<std::uintptr_t>(allocation));
    if (found != ledger->allocations.end())
        found->second.quota = quota;
}

} // namespace

bool CpuMemoryQuota::tryReserve(std::size_t bytes) noexcept
{
    const auto requested = static_cast<std::uint64_t>(bytes == 0u ? 1u : bytes);
    std::uint64_t next = 0u;
    if (!tryReserveAtomicCapacity(liveBytes_, limitBytes_, requested, next, true))
        return false;
    auto peak = peakBytes_.load(std::memory_order_relaxed);
    while (peak < next
        && !peakBytes_.compare_exchange_weak(
            peak, next, std::memory_order_release, std::memory_order_relaxed)) {
    }
    return true;
}

void CpuMemoryQuota::release(std::size_t bytes) noexcept
{
    const auto released = static_cast<std::uint64_t>(bytes == 0u ? 1u : bytes);
    releaseAtomicCapacity(liveBytes_, released);
}

CpuMemoryQuotaLease::CpuMemoryQuotaLease(CpuMemoryQuotaLease&& other) noexcept
    : quota_(std::move(other.quota_)), reservedBytes_(std::exchange(other.reservedBytes_, 0u))
{}

CpuMemoryQuotaLease& CpuMemoryQuotaLease::operator=(CpuMemoryQuotaLease&& other) noexcept
{
    if (this == &other)
        return *this;
    release();
    quota_ = std::move(other.quota_);
    reservedBytes_ = std::exchange(other.reservedBytes_, 0u);
    return *this;
}

bool CpuMemoryQuotaLease::acquire(std::shared_ptr<CpuMemoryQuota> quota, std::size_t bytes) noexcept
{
    release();
    const auto reserved = bytes == 0u ? 1u : bytes;
    if (quota == nullptr || !quota->tryReserve(reserved))
        return false;
    quota_ = std::move(quota);
    reservedBytes_ = reserved;
    return true;
}

bool CpuMemoryQuotaLease::resize(std::size_t bytes) noexcept
{
    if (quota_ == nullptr)
        return bytes == 0u;
    const auto requested = bytes == 0u ? 0u : bytes;
    if (requested == reservedBytes_)
        return true;
    if (requested < reservedBytes_) {
        quota_->release(reservedBytes_ - requested);
        reservedBytes_ = requested;
        if (reservedBytes_ == 0u)
            quota_.reset();
        return true;
    }
    const auto delta = requested - reservedBytes_;
    if (!quota_->tryReserve(delta))
        return false;
    reservedBytes_ = requested;
    return true;
}

void CpuMemoryQuotaLease::release() noexcept
{
    if (quota_ != nullptr && reservedBytes_ != 0u)
        quota_->release(reservedBytes_);
    quota_.reset();
    reservedBytes_ = 0u;
}

bool CpuMemoryQuotaLease::transferReservation(
    std::size_t bytes, std::shared_ptr<CpuMemoryQuota>& quota) noexcept
{
    if (quota_ == nullptr || reservedBytes_ != bytes)
        return false;
    quota = std::move(quota_);
    reservedBytes_ = 0u;
    return true;
}

CpuAllocationChargeInfo inspectCpuAllocationCharge(const void* exactAllocation) noexcept
{
    const auto result = inspectCpuAllocationContaining(exactAllocation);
    if (!result.found || result.allocationIdentity != exactAllocation)
        return {};
    return result;
}

CpuAllocationChargeInfo inspectCpuAllocationContaining(const void* address) noexcept
{
    if (address == nullptr)
        return {};
    const auto target = reinterpret_cast<std::uintptr_t>(address);
    auto* ledger = allocationLedger();
    if (ledger == nullptr)
        return {};
    std::lock_guard<std::mutex> lock(ledger->mutex);
    auto found = ledger->allocations.upper_bound(target);
    if (found == ledger->allocations.begin())
        return {};
    --found;
    const auto base = found->first;
    if (target < base || target - base >= found->second.sizeBytes)
        return {};
    return CpuAllocationChargeInfo{
        true, reinterpret_cast<const void*>(base), found->second.sizeBytes, found->second.quota};
}

bool cpuAllocationOverlapsQuotaOwner(
    const void* exactAllocation, const std::shared_ptr<CpuMemoryQuota>& quota) noexcept
{
    if (!quota || !inspectCpuAllocationCharge(exactAllocation).found)
        return false;
    const auto* header = headerFromAllocation(const_cast<void*>(exactAllocation));
    const auto& original = header->quota;
    return original
        && (original.get() == quota.get()
            || (!original.owner_before(quota) && !quota.owner_before(original)));
}

bool transferCpuAllocationCharge(void* chargedAllocation, void* deferredAllocation) noexcept
{
    if (chargedAllocation == nullptr || deferredAllocation == nullptr
        || chargedAllocation == deferredAllocation)
        return false;
    if (!inspectCpuAllocationCharge(chargedAllocation).found
        || !inspectCpuAllocationCharge(deferredAllocation).found)
        return false;
    auto* source = headerFromAllocation(chargedAllocation);
    auto* target = headerFromAllocation(deferredAllocation);
    if (trailerFromAllocation(chargedAllocation)->magic != kAllocationHeaderMagic
        || trailerFromAllocation(deferredAllocation)->magic != kAllocationHeaderMagic
        || source->raw == nullptr || target->raw == nullptr || source->quota == nullptr
        || target->quota != nullptr)
        return false;

    auto quota = source->quota;
    if (target->sizeBytes > source->sizeBytes
        && !quota->tryReserve(target->sizeBytes - source->sizeBytes))
        return false;
    source->quota.reset();
    target->quota = std::move(quota);
    if (source->sizeBytes > target->sizeBytes)
        target->quota->release(source->sizeBytes - target->sizeBytes);
    updateAllocationLedgerQuota(chargedAllocation, nullptr);
    updateAllocationLedgerQuota(deferredAllocation, target->quota.get());
    return true;
}

CpuMemoryQuotaScope::CpuMemoryQuotaScope(std::shared_ptr<CpuMemoryQuota> quota) noexcept
    : quota_(std::move(quota)), previous_(currentAllocationThreadState().quotaScope)
{
    currentAllocationThreadState().quotaScope = this;
}

CpuMemoryQuotaScope::~CpuMemoryQuotaScope()
{
    currentAllocationThreadState().quotaScope = previous_;
}

CpuQuotaPrechargedAllocationScope::CpuQuotaPrechargedAllocationScope(
    CpuMemoryQuotaLease&& lease) noexcept
    : lease_(std::move(lease)), previous_(currentAllocationThreadState().prechargeScope)
{
    currentAllocationThreadState().prechargeScope = this;
}

CpuQuotaPrechargedAllocationScope::~CpuQuotaPrechargedAllocationScope()
{
    currentAllocationThreadState().prechargeScope = previous_;
}

std::shared_ptr<CpuMemoryQuota> CpuQuotaPrechargedAllocationScope::consume(
    std::size_t bytes) noexcept
{
    std::shared_ptr<CpuMemoryQuota> quota;
    (void)lease_.transferReservation(bytes, quota);
    return quota;
}

bool CpuExternalAllocationLease::acquire(std::size_t bytes) noexcept
{
    reset();
    auto quota = currentCpuMemoryQuota();
    if (quota == nullptr)
        return true;
    return lease_.acquire(std::move(quota), bytes);
}

void CpuExternalAllocationLease::reset() noexcept
{
    lease_.release();
}

struct DefaultCpuAllocator::Impl {
    Impl(std::string_view allocatorName, RawCpuAllocationBackend& rawBackend) : backend(rawBackend)
    {
        name.assign(allocatorName);
    }

    FixedText name;
    RawCpuAllocationBackend& backend;
    StatsRecorder recorder;
};

CpuAllocationGroupScope::CpuAllocationGroupScope(bool enabled) noexcept
    : previous_(currentAllocationThreadState().group)
{
    if (!enabled)
        currentAllocationThreadState().group = nullptr;
    else if (!previous_ && currentCpuMemoryQuota() == nullptr)
        currentAllocationThreadState().group = &state_;
}

CpuAllocationGroupScope::~CpuAllocationGroupScope()
{
    currentAllocationThreadState().group = previous_;
    auto* block = static_cast<AllocationGroupBlock*>(state_.head);
    while (block) {
        auto* next = block->next;
        releaseAllocationGroupBlock(block);
        block = next;
    }
}

const CpuIntentMemoryStats& CpuMemoryStats::byIntent(CpuAllocationIntent intent) const
{
    return intentStats[intentIndex(intent)];
}

DefaultCpuAllocator::DefaultCpuAllocator(std::string_view allocatorName)
{
    auto& backend = defaultRawAllocationBackend();
    void* storage = backend.allocate(sizeof(Impl));
    if (!storage) {
        throw std::bad_alloc();
    }
    impl_ = new (storage) Impl(std::move(allocatorName), backend);
}

DefaultCpuAllocator::~DefaultCpuAllocator()
{
    if (!impl_ || allocatorProcessShuttingDown.load(std::memory_order_acquire)) {
        return;
    }
    auto& backend = impl_->backend;
    impl_->~Impl();
    backend.deallocate(impl_);
}

CpuAllocationResult DefaultCpuAllocator::allocate(const CpuAllocationDesc& desc)
{
    if (!impl_) {
        return CpuAllocationResult{nullptr, "allocator implementation is unavailable"};
    }
    if (!isValidCpuAllocationAlignment(desc.alignment)) {
        return failAllocation(
            impl_->recorder, desc, "allocation alignment must be a non-zero power of two");
    }

    const auto sizeBytes = normalizedAllocationSize(desc.sizeBytes);
    const auto alignment = normalizedAlignment(desc.alignment);
    if (wouldAllocationOverflow(sizeBytes, alignment)) {
        return failAllocation(
            impl_->recorder, desc, "allocation size overflows allocator header reservation");
    }

    auto& threadState = currentAllocationThreadState();
    if (threadState.group != nullptr && threadState.prechargeScope == nullptr
        && desc.intent == CpuAllocationIntent::General
        && (threadState.quotaScope == nullptr || threadState.quotaScope->matchesQuota(nullptr))
        && this == &defaultCpuAllocator()) {
        auto& group = *static_cast<CpuAllocationGroupScope::State*>(threadState.group);
        return allocateInGroup(*this, group, desc, sizeBytes, alignment);
    }

    const auto totalBytes = sizeBytes + alignment - 1u + kAllocationHeaderBytes;
    auto quota = currentCpuMemoryQuota();
    bool quotaWasPrecharged = false;
    if (currentAllocationThreadState().prechargeScope != nullptr) {
        auto prechargedQuota = currentAllocationThreadState().prechargeScope->consume(sizeBytes);
        if (prechargedQuota != nullptr) {
            quota = std::move(prechargedQuota);
            quotaWasPrecharged = true;
        }
    }
    if (quota != nullptr && !quotaWasPrecharged && !quota->tryReserve(sizeBytes)) {
        return failAllocation(impl_->recorder, desc, "script memory quota exceeded");
    }
    void* raw = impl_->backend.allocate(totalBytes);
    if (!raw) {
        if (quota != nullptr)
            quota->release(sizeBytes);
        return failAllocation(impl_->recorder, desc, "mimalloc allocation returned null");
    }

    const auto rawAddress = reinterpret_cast<std::uintptr_t>(raw);
    const auto aligned = alignCpuAllocationOffsetUp(
        static_cast<std::size_t>(rawAddress + kAllocationHeaderBytes), alignment);
    if (!aligned.success) {
        if (quota != nullptr)
            quota->release(sizeBytes);
        impl_->backend.deallocate(raw);
        return failAllocation(impl_->recorder,
            desc,
            aligned.diagnostics ? aligned.diagnostics : "allocation alignment failed");
    }
    const auto alignedAddress = static_cast<std::uintptr_t>(aligned.alignedValue);
    auto* header = reinterpret_cast<AllocationHeader*>(alignedAddress - kAllocationHeaderBytes);
    ::new (static_cast<void*>(header)) AllocationHeader{};
    header->raw = raw;
    header->sizeBytes = sizeBytes;
    header->alignment = alignment;
    header->quota = std::move(quota);
    auto* trailer = trailerFromAllocation(reinterpret_cast<void*>(alignedAddress));
    ::new (static_cast<void*>(trailer)) AllocationTrailer{};
    trailer->intent = desc.intent;

    auto* allocation = reinterpret_cast<void*>(alignedAddress);
    if (!registerAllocationLedger(allocation, sizeBytes, header->quota.get())) {
        auto failedQuota = std::move(header->quota);
        trailer->magic = 0u;
        trailer->~AllocationTrailer();
        header->~AllocationHeader();
        impl_->backend.deallocate(raw);
        if (failedQuota != nullptr)
            failedQuota->release(sizeBytes);
        return failAllocation(impl_->recorder, desc, "CPU allocation ledger registration failed");
    }

    {
        std::lock_guard<std::mutex> lock(impl_->recorder.mutex);
        recordAllocation(impl_->recorder.stats, desc);
    }
    return CpuAllocationResult{
        allocation,
        {},
    };
}

void DefaultCpuAllocator::deallocate(void* ptr, const CpuAllocationDesc& desc) noexcept
{
    if (!ptr || !impl_ || allocatorProcessShuttingDown.load(std::memory_order_acquire)) {
        return;
    }

    auto* trailer = trailerFromAllocation(ptr);
    if (trailer->magic != kAllocationHeaderMagic
        || (trailer->group == nullptr && !headerFromAllocation(ptr)->raw)) {
        std::lock_guard<std::mutex> lock(impl_->recorder.mutex);
        recordFailure(impl_->recorder.stats,
            impl_->recorder.lastFailureDiagnostics,
            desc.intent,
            "allocation header is invalid for default CPU allocator");
        return;
    }

    if (trailer->group != nullptr) {
        auto* block = trailer->group;
        trailer->magic = 0u;
        trailer->~AllocationTrailer();
        releaseAllocationGroupBlock(block);
        return;
    }

    auto* header = headerFromAllocation(ptr);
    const auto sizeBytes = header->sizeBytes;
    const auto intent = trailer->intent;
    auto quota = std::move(header->quota);
    void* raw = header->raw;
    unregisterAllocationLedger(ptr);
    trailer->magic = 0u;

    {
        std::lock_guard<std::mutex> lock(impl_->recorder.mutex);
        recordDeallocation(impl_->recorder.stats, sizeBytes, intent);
    }
    if (quota != nullptr)
        quota->release(sizeBytes);
    trailer->~AllocationTrailer();
    header->~AllocationHeader();
    impl_->backend.deallocate(raw);
}

CpuMemoryStats DefaultCpuAllocator::stats() const
{
    if (!impl_) {
        return {};
    }
    std::lock_guard<std::mutex> lock(impl_->recorder.mutex);
    return impl_->recorder.stats;
}

std::string DefaultCpuAllocator::name() const
{
    return impl_ ? impl_->name.toString() : std::string{};
}

std::string DefaultCpuAllocator::backendName() const
{
    return impl_ ? impl_->backend.name() : std::string{};
}

std::string DefaultCpuAllocator::lastFailureDiagnostics() const
{
    if (!impl_) {
        return "allocator implementation is unavailable";
    }
    std::lock_guard<std::mutex> lock(impl_->recorder.mutex);
    return impl_->recorder.lastFailureDiagnostics;
}

CallbackCpuAllocator::CallbackCpuAllocator(
    std::string allocatorName, CpuAllocatorCallbacks callbacks)
    : name_(std::move(allocatorName)), callbacks_(callbacks)
{}

CpuAllocationResult CallbackCpuAllocator::allocate(const CpuAllocationDesc& desc)
{
    if (!callbacks_.allocate) {
        recordLocalFailure(desc, "missing allocate callback");
        return CpuAllocationResult{nullptr, "missing allocate callback"};
    }

    void* ptr = callbacks_.allocate(callbacks_.userData, desc);
    if (!ptr) {
        recordLocalFailure(desc, "allocate callback returned null");
        return CpuAllocationResult{nullptr, "allocate callback returned null"};
    }

    recordLocalAllocation(desc);
    return CpuAllocationResult{ptr, {}};
}

void CallbackCpuAllocator::deallocate(void* ptr, const CpuAllocationDesc& desc) noexcept
{
    if (!ptr) {
        return;
    }
    if (!callbacks_.deallocate) {
        recordLocalFailure(desc, "missing deallocate callback");
        return;
    }

    callbacks_.deallocate(callbacks_.userData, ptr, desc);
    recordLocalDeallocation(desc);
}

CpuMemoryStats CallbackCpuAllocator::stats() const
{
    if (callbacks_.stats) {
        return callbacks_.stats(callbacks_.userData);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    return localStats_;
}

std::string CallbackCpuAllocator::name() const
{
    return name_;
}

std::string CallbackCpuAllocator::backendName() const
{
    return "callback";
}

std::string CallbackCpuAllocator::lastFailureDiagnostics() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return lastFailureDiagnostics_;
}

void CallbackCpuAllocator::recordLocalAllocation(const CpuAllocationDesc& desc)
{
    std::lock_guard<std::mutex> lock(mutex_);
    recordAllocation(localStats_, desc);
}

void CallbackCpuAllocator::recordLocalDeallocation(const CpuAllocationDesc& desc) noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    recordDeallocation(localStats_, desc.sizeBytes, desc.intent);
}

void CallbackCpuAllocator::recordLocalFailure(
    const CpuAllocationDesc& desc, const std::string& diagnostics) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    recordFailure(localStats_, lastFailureDiagnostics_, desc.intent, diagnostics);
}

CpuMemoryResource::CpuMemoryResource(
    CpuAllocator& allocator, CpuAllocationIntent intent, std::string label)
    : allocator_(allocator), intent_(intent), label_(std::move(label))
{}

CpuAllocator& CpuMemoryResource::allocator() const
{
    return allocator_;
}

CpuAllocationIntent CpuMemoryResource::intent() const
{
    return intent_;
}

const std::string& CpuMemoryResource::label() const
{
    return label_;
}

void* CpuMemoryResource::do_allocate(std::size_t bytes, std::size_t alignment)
{
    CpuAllocationDesc desc;
    desc.sizeBytes = bytes;
    desc.alignment = alignment;
    desc.intent = intent_;
    desc.label = std::string_view(label_);

    const auto result = allocator_.allocate(desc);
    if (!result.ptr) {
        throw std::bad_alloc();
    }
    return result.ptr;
}

void CpuMemoryResource::do_deallocate(void* ptr, std::size_t bytes, std::size_t alignment)
{
    CpuAllocationDesc desc;
    desc.sizeBytes = bytes;
    desc.alignment = alignment;
    desc.intent = intent_;
    desc.label = std::string_view(label_);
    allocator_.deallocate(ptr, desc);
}

bool CpuMemoryResource::do_is_equal(const std::pmr::memory_resource& other) const noexcept
{
    return this == &other;
}

const char* cpuAllocationIntentName(CpuAllocationIntent intent)
{
    switch (intent) {
    case CpuAllocationIntent::General:
        return "General";
    case CpuAllocationIntent::FrameTemp:
        return "FrameTemp";
    case CpuAllocationIntent::AssetImport:
        return "AssetImport";
    case CpuAllocationIntent::ImageDecode:
        return "ImageDecode";
    case CpuAllocationIntent::ShaderCompile:
        return "ShaderCompile";
    case CpuAllocationIntent::RhiHostStaging:
        return "RhiHostStaging";
    case CpuAllocationIntent::Telemetry:
        return "Telemetry";
    case CpuAllocationIntent::Count:
        return "Count";
    }
    return "General";
}

DefaultCpuAllocator& defaultCpuAllocator()
{
    static DefaultCpuAllocator* allocator = []() {
        void* storage = defaultRawAllocationBackend().allocate(sizeof(DefaultCpuAllocator));
        if (!storage) {
            throw std::bad_alloc();
        }
        return new (storage) DefaultCpuAllocator{"DefaultCpuAllocator"};
    }();
    return *allocator;
}

std::pmr::memory_resource& defaultCpuMemoryResource()
{
    static CpuMemoryResource resource{
        defaultCpuAllocator(),
        CpuAllocationIntent::General,
        "DefaultCpuPmrResource",
    };
    return resource;
}

std::shared_ptr<CpuMemoryQuota> currentCpuMemoryQuota()
{
    return currentAllocationThreadState().quotaScope != nullptr
        ? currentAllocationThreadState().quotaScope->quota_
        : std::shared_ptr<CpuMemoryQuota>{};
}

void notifyCpuAllocatorProcessShutdown() noexcept
{
    allocatorProcessShuttingDown.store(true, std::memory_order_release);
}

void installDefaultCpuAllocatorAsPmrDefault()
{
    std::pmr::set_default_resource(&defaultCpuMemoryResource());
}

std::string toStdString(const std::pmr::string& text)
{
    return std::string(text.data(), text.size());
}

std::pmr::string toPmrString(const std::string& text)
{
    return std::pmr::string(text.data(), text.size());
}

CpuPmrDefaultResourceBootstrap::CpuPmrDefaultResourceBootstrap()
{
    installDefaultCpuAllocatorAsPmrDefault();
}

} // namespace arp::core
