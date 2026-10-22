#pragma once

namespace arp::core {

// Preallocate the slot/control block before a foreign API creates a borrowed
// object. Adoption receives one already-retained reference and never allocates.
class ExternalObjectSlot final {
public:
    using Release = void (*)(void*) noexcept;

    explicit ExternalObjectSlot(Release release) noexcept : release_(release) {}

    ~ExternalObjectSlot() { reset(); }

    ExternalObjectSlot(const ExternalObjectSlot&) = delete;
    ExternalObjectSlot& operator=(const ExternalObjectSlot&) = delete;

    bool adopt(void* object) noexcept
    {
        if (!release_ || !object || object_)
            return false;
        object_ = object;
        return true;
    }

    void reset() noexcept
    {
        auto* object = object_;
        object_ = nullptr;
        if (object)
            release_(object);
    }

    void* get() const noexcept { return object_; }

private:
    Release release_;
    void* object_ = nullptr;
};

} // namespace arp::core
