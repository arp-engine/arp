#pragma once

// instance -- mirrors ae::hybrid/instance.h, adapted for arp::reflection.
// Uses PMR Map instead of std::map.

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/def.h"
#include "reflection/object.h"

namespace arp::reflection {

// Immutable schema names are shared; every instance still owns stable Values.
class ARP_API InstanceFieldLayout final {
public:
    explicit InstanceFieldLayout(const core::pmr::Vector<core::pmr::String>& fields);

    const core::pmr::Vector<core::pmr::String>& names() const noexcept { return names_; }

    std::size_t find(NameView name) const noexcept;

private:
    core::pmr::Vector<core::pmr::String> names_{&core::defaultCpuMemoryResource()};
};

class ARP_API instance final {
public:
    using FieldNameLess = NameLess;

    using FieldMap = core::pmr::Map<core::pmr::String, object, FieldNameLess>;

    struct FieldEntry {
        const core::pmr::String& first;
        const object& second;
    };

    class ARP_API FieldRange final {
    public:
        class ARP_API Iterator final {
        public:
            struct Arrow {
                FieldEntry entry;

                const FieldEntry* operator->() const noexcept { return &entry; }
            };

            FieldEntry operator*() const noexcept;

            Arrow operator->() const noexcept { return {operator*()}; }

            Iterator& operator++() noexcept;
            bool operator!=(const Iterator& other) const noexcept;

            bool operator==(const Iterator& other) const noexcept { return !(*this != other); }

        private:
            friend class FieldRange;
            Iterator(const instance* owner, std::size_t index, FieldMap::const_iterator dynamic);
            bool declaredFirst() const noexcept;
            const instance* owner_;
            std::size_t index_;
            FieldMap::const_iterator dynamic_;
        };

        Iterator begin() const noexcept;
        Iterator end() const noexcept;
        Iterator find(NameView name) const noexcept;
        std::size_t size() const noexcept;

        bool empty() const noexcept { return size() == 0; }

    private:
        friend class instance;

        explicit FieldRange(const instance* owner) : owner_(owner) {}

        const instance* owner_;
    };

    explicit instance(const class_info* cls);
    // Declared fields start as shared nil without allocating discarded Values.
    instance(const class_info* cls, const core::pmr::Vector<core::pmr::String>& fields);
    instance(const class_info* cls, std::shared_ptr<const InstanceFieldLayout> layout);
    instance(const instance& other);
    instance(instance&& other) noexcept;
    instance& operator=(const instance& other);
    instance& operator=(instance&& other) noexcept;
    ~instance();
    const class_info* class_() const;
    object __getitem__(NameView key);
    object __setitem__(NameView key, const object& value);
    object operator[](NameView key);
    object& slot(NameView key);
    object* findSlot(NameView key) noexcept;
    // An index is meaningful only in the exact immutable layout that proved it.
    // A mismatch returns nullptr so callers retain ordinary dynamic lookup.
    object* findDeclaredSlot(const InstanceFieldLayout* layout, std::size_t index) noexcept;

    bool alive() const noexcept { return alive_; }

    void invalidate() noexcept;

    FieldRange slots() const noexcept;
    const void* fieldStorageAllocation() const noexcept;
    const void* fieldLayoutAllocation() const noexcept;

private:
    const class_info* cls_ = nullptr;
    bool alive_ = true;
    struct FieldStorage;
    FieldStorage& fields();
    core::UniquePtr<FieldStorage> storage_;
};

} // namespace arp::reflection
