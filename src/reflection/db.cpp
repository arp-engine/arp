// db.cpp -- mirrors ae::hybrid/src/db.cpp, adapted for arp::reflection.
//
// The original used spinlock + unordered_map<string_view, T> (values stored
// by value). arp uses std::mutex + PMR maps keyed by core::pmr::String (because
// string_view keys would dangle) with values owned via core::UniquePtr.

#include "reflection/db.h"

#include "reflection/builtin.h"
#include "reflection/class_info.h"
#include "reflection/internal/type_name.h"
#include "reflection/registration_transaction.h"
#include "reflection/type.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <new>

namespace arp::reflection {

namespace {

// Builtin registration can construct function wrappers whose return type is
// reflected through type_of<void>(). Keep recursive calls on the same thread
// from re-entering registration, while a mutex/atomic pair serializes the
// first registration across independent runtime threads.
// 0 = not started, 1 = registering, 2 = complete.
thread_local bool builtinRegistrationInProgress = false;

std::atomic_int& builtinRegistrationState()
{
    static std::atomic_int state{0};
    return state;
}

std::mutex& builtinRegistrationMutex()
{
    // Reflection can be entered by global registrars in other translation
    // units. Construct on first use and retain the mutex through DLL teardown.
    alignas(std::mutex) static std::byte storage[sizeof(std::mutex)];
    static auto* mutex = ::new (static_cast<void*>(storage)) std::mutex();
    return *mutex;
}

} // namespace

db& db::rawInstance()
{
    // The database is shared by static registrars and dynamically loaded AOT
    // packages. In a DLL, CRT teardown order can run mimalloc shutdown before
    // the database destructor, so keep this process-lifetime object alive.
    alignas(db) static std::byte storage[sizeof(db)];
    static auto* value = ::new (static_cast<void*>(storage)) db();
    return *value;
}

db& db::instance()
{
    db& value = rawInstance();
    const auto state = builtinRegistrationState().load(std::memory_order_acquire);
    if (state == 2) {
        return value;
    }
    if (state == 1 && builtinRegistrationInProgress) {
        // Builtin wrappers may reflect void while registration is in progress;
        // the registering thread must re-enter the raw database without
        // taking the registration mutex again.
        return value;
    }
    if (state == 1) {
        // A different thread waits for the owner to publish the complete
        // state instead of observing partially registered builtin classes.
        std::lock_guard<std::mutex> lock(builtinRegistrationMutex());
        return value;
    }
    if (!builtinRegistrationInProgress) {
        std::lock_guard<std::mutex> lock(builtinRegistrationMutex());
        if (builtinRegistrationState().load(std::memory_order_relaxed) == 0) {
            // type_of<void>() is needed while builtin function wrappers are
            // being constructed. Seed its class/type entry before entering
            // that registration so the recursive lookup is read-only.
            const auto voidName = detail::typeName<void>();
            auto* voidClass = value.find_class(voidName);
            if (voidClass == nullptr) {
                voidClass = value.register_class(voidName, undefined_t, 0, 0, nullptr);
            }
            if (value.find_type(voidName) == nullptr) {
                value.register_type(voidName, voidClass, 0);
            }
            builtinRegistrationState().store(1, std::memory_order_release);
            builtinRegistrationInProgress = true;
            registerBuiltinReflectionTypes();
            builtinRegistrationInProgress = false;
            builtinRegistrationState().store(2, std::memory_order_release);
        }
    }
    return value;
}

db::db() = default;

class_info* db::find_class(NameView name)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = detail::makeString(name);
    auto found = classes_.find(key);
    if (found != classes_.end()) {
        return found->second.get();
    }
    for (auto& entry : classes_) {
        if (entry.second && entry.second->name() == key) {
            return entry.second.get();
        }
    }
    return nullptr;
}

const typeinfo* db::find_type(NameView name) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = types_.find(detail::makeString(name));
    return found == types_.end() ? nullptr : found->second.get();
}

class_info* db::register_class(NameView name,
    value_t type,
    std::size_t size,
    std::size_t alignment,
    get_derived_info_func getDerivedInfo)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = detail::makeString(name);
    auto found = classes_.find(key);
    if (found != classes_.end()) {
        return found->second.get();
    }
    auto info = core::makeUnique<class_info>(key, type, size, alignment, getDerivedInfo);
    class_info* ptr = info.get();
    classes_.emplace(std::move(key), std::move(info));
    return ptr;
}

const typeinfo* db::register_type(NameView name, class_info* cls, std::uint64_t mask)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto key = detail::makeString(name);
    auto found = types_.find(key);
    if (found != types_.end()) {
        return found->second.get();
    }
    auto info = core::makeUnique<typeinfo>(cls, mask);
    const typeinfo* ptr = info.get();
    types_.emplace(std::move(key), std::move(info));
    return ptr;
}

bool db::commitRegistrationTransaction(
    RegistrationTransaction& transaction, core::pmr::String& diagnostics)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [key, info] : transaction.classes_) {
        if (classes_.find(key) != classes_.end()) {
            diagnostics = "reflection registration commit found a class conflict";
            diagnostics += ": ";
            diagnostics += key;
            return false;
        }
        for (const auto& existing : classes_) {
            if (existing.second != nullptr && info != nullptr
                && existing.second->name() == info->name()) {
                diagnostics = "reflection registration commit found a reflected-name conflict";
                diagnostics += ": ";
                diagnostics += info->name();
                return false;
            }
        }
    }
    for (const auto& [key, unused] : transaction.types_) {
        (void)unused;
        if (types_.find(key) != types_.end()) {
            diagnostics = "reflection registration commit found a type conflict";
            diagnostics += ": ";
            diagnostics += key;
            return false;
        }
    }
    for (const auto& [key, unused] : transaction.owners_) {
        (void)unused;
        if (owners_.find(key) != owners_.end()) {
            diagnostics = "reflection registration commit found an owner conflict";
            return false;
        }
    }

    // Both maps use the process default PMR resource. std::map::merge moves
    // existing nodes without allocating, so after preflight publication is
    // all-or-nothing under the database mutex.
    classes_.merge(transaction.classes_);
    types_.merge(transaction.types_);
    owners_.merge(transaction.owners_);
    if (!transaction.classes_.empty() || !transaction.types_.empty()
        || !transaction.owners_.empty()) {
        diagnostics = "reflection registration commit did not publish every staged node";
        return false;
    }
    return true;
}

} // namespace arp::reflection
