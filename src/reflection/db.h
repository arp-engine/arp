#pragma once

// db -- mirrors ae::hybrid/db.h, adapted for arp::reflection.
//
// The original uses spinlock + unordered_map<string_view, T>.
// arp uses std::mutex (PMR constraint: no custom spinlock) + PMR maps
// keyed by core::pmr::String (string_view would dangle).
//
// Owns class_info and typeinfo instances via core::UniquePtr.

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "reflection/def.h"

#include <mutex>

namespace arp::reflection {

class class_info;
class RegistrationTransaction;
class typeinfo;

class ARP_API db final {
public:
    // Returns the database storage without triggering builtin registration.
    // Typeinfo construction uses this path so it cannot recursively enter the
    // builtin registration pass that may itself construct typeinfo objects.
    static db& rawInstance();
    static db& instance();

    db();
    db(const db&) = delete;
    db& operator=(const db&) = delete;

    class_info* find_class(NameView name);
    const typeinfo* find_type(NameView name) const;
    class_info* register_class(NameView name,
        value_t type,
        std::size_t size,
        std::size_t alignment,
        get_derived_info_func getDerivedInfo);
    const typeinfo* register_type(NameView name, class_info* cls, std::uint64_t mask);
    bool commitRegistrationTransaction(
        RegistrationTransaction& transaction, core::pmr::String& diagnostics);

private:
    friend class RegistrationTransaction;
    mutable std::mutex mutex_;
    core::pmr::Map<core::pmr::String, core::UniquePtr<class_info>> classes_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, core::UniquePtr<typeinfo>> types_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, std::shared_ptr<void>> owners_{
        &core::defaultCpuMemoryResource()};
};

} // namespace arp::reflection

#include "reflection/internal/db_impl.h"
