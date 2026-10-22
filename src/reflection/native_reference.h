#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace arp::reflection {

class class_info;
class typeinfo;

namespace detail {

// NativeObjectControl is an engine-only lifetime record.  It is deliberately
// not a reflected type: scripts see only the reflection::object that carries
// this record, never the control or its raw address.
class ARP_API NativeObjectControl final {
public:
    NativeObjectControl(
        void* pointer, const class_info* dynamicClass, const typeinfo* pointerType) noexcept
        : pointer_(pointer), dynamicClass_(dynamicClass), pointerType_(pointerType)
    {}

    NativeObjectControl(const NativeObjectControl&) = delete;
    NativeObjectControl& operator=(const NativeObjectControl&) = delete;

    void retain() noexcept { references_.fetch_add(1u, std::memory_order_relaxed); }

    void release() noexcept
    {
        if (references_.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
            core::destroyCpuAllocatedUnique<NativeObjectControl>(this);
        }
    }

    void invalidate() noexcept
    {
        alive_.store(false, std::memory_order_release);
        pointer_.store(nullptr, std::memory_order_release);
    }

    bool alive() const noexcept
    {
        return alive_.load(std::memory_order_acquire)
            && pointer_.load(std::memory_order_acquire) != nullptr;
    }

    void* pointer() const noexcept
    {
        return alive() ? pointer_.load(std::memory_order_acquire) : nullptr;
    }

    const class_info* dynamicClass() const noexcept { return dynamicClass_; }

    const typeinfo* pointerType() const noexcept { return pointerType_; }

    // Resolves a native pointer for a requested reflected class.  The
    // implementation is in object.cpp because class_info is intentionally
    // opaque at this layer.
    void* pointerFor(const class_info* requestedClass) const noexcept;

private:
    std::atomic<std::uint32_t> references_{1u};
    std::atomic<void*> pointer_{nullptr};
    std::atomic<bool> alive_{true};
    const class_info* dynamicClass_ = nullptr;
    const typeinfo* pointerType_ = nullptr;
};

// Small RAII value stored inside variant's inline storage.  Its only payload
// is the control pointer; copies retain the control and moves transfer it.
class NativeObjectReference final {
public:
    NativeObjectReference() noexcept = default;

    NativeObjectReference(const NativeObjectReference& other) noexcept : control_(other.control_)
    {
        if (control_ != nullptr)
            control_->retain();
    }

    NativeObjectReference(NativeObjectReference&& other) noexcept
        : control_(std::exchange(other.control_, nullptr))
    {}

    ~NativeObjectReference()
    {
        if (control_ != nullptr)
            control_->release();
    }

    NativeObjectReference& operator=(const NativeObjectReference& other) noexcept
    {
        if (this == &other)
            return *this;
        auto* next = other.control_;
        if (next != nullptr)
            next->retain();
        if (control_ != nullptr)
            control_->release();
        control_ = next;
        return *this;
    }

    NativeObjectReference& operator=(NativeObjectReference&& other) noexcept
    {
        if (this == &other)
            return *this;
        if (control_ != nullptr)
            control_->release();
        control_ = std::exchange(other.control_, nullptr);
        return *this;
    }

    static NativeObjectReference make(
        void* pointer, const class_info* dynamicClass, const typeinfo* pointerType)
    {
        auto owner = core::makeUnique<NativeObjectControl>(pointer, dynamicClass, pointerType);
        auto* control = owner.release();
        return NativeObjectReference(control, AdoptTag{});
    }

    NativeObjectControl* control() noexcept { return control_; }

    const NativeObjectControl* control() const noexcept { return control_; }

private:
    struct AdoptTag {};

    NativeObjectReference(NativeObjectControl* control, AdoptTag) noexcept : control_(control) {}

    NativeObjectControl* control_ = nullptr;
};

} // namespace detail
} // namespace arp::reflection
