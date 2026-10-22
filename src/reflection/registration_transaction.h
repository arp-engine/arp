#pragma once

#include "core/api.h"
#include "core/memory/cpu_allocator.h"
#include "reflection/def.h"

#include <cstddef>
#include <cstdint>

namespace arp::reflection {
struct CollectionAccess;

class class_info;
class db;
class typeinfo;

enum class RegistrationOwnership : std::uint8_t {
    Unspecified,
    Engine,
    Value,
};

enum class RegistrationLifecycle : std::uint8_t {
    Unspecified,
    Explicit,
    Value,
};

struct RegistrationContractRecord {
    core::pmr::String key{&core::defaultCpuMemoryResource()};
    core::pmr::String contract{&core::defaultCpuMemoryResource()};
};

struct ReflectionRegistrationContract {
    core::pmr::String moduleIdentity{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<RegistrationContractRecord> records{&core::defaultCpuMemoryResource()};

    ARP_API core::pmr::String canonicalText() const;
    ARP_API core::pmr::String fingerprint() const;
};

struct RegistrationClassReceipt {
    core::pmr::String typeName{&core::defaultCpuMemoryResource()};
    core::pmr::String reflectedName{&core::defaultCpuMemoryResource()};
    value_t valueType = others_t;
    std::size_t size = 0u;
    std::size_t alignment = 0u;
    core::pmr::Vector<core::pmr::String> bases{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::pmr::String> members{&core::defaultCpuMemoryResource()};
    RegistrationOwnership ownership = RegistrationOwnership::Unspecified;
    RegistrationLifecycle lifecycle = RegistrationLifecycle::Unspecified;
    core::pmr::String moduleExport{&core::defaultCpuMemoryResource()};
};

struct RegistrationReceipt {
    core::pmr::String ownerIdentity{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<RegistrationClassReceipt> classes{&core::defaultCpuMemoryResource()};
    ReflectionRegistrationContract declarations;

    ARP_API core::pmr::String canonicalText() const;
    ARP_API core::pmr::String fingerprint() const;
    ARP_API core::pmr::String reflectionContractFingerprint() const;
};

// Isolates reflection declarations until the caller has validated every
// related module/export contract. A failed transaction owns and destroys all
// staged metadata, so no callback into an unloaded DLL reaches the global DB.
class ARP_API RegistrationTransaction final {
public:
    explicit RegistrationTransaction(NameView ownerIdentity);
    RegistrationTransaction(NameView moduleIdentity, NameView ownerIdentity);
    ~RegistrationTransaction();

    RegistrationTransaction(const RegistrationTransaction&) = delete;
    RegistrationTransaction& operator=(const RegistrationTransaction&) = delete;

    bool valid() const noexcept { return valid_ && !committed_; }

    bool committed() const noexcept { return committed_; }

    const core::pmr::String& diagnostics() const noexcept { return diagnostics_; }

    class_info* findStagedClass(NameView typeName) const;
    const typeinfo* findStagedType(NameView typeName) const;
    class_info* registerReferencedClass(NameView typeName,
        value_t type,
        std::size_t size,
        std::size_t alignment,
        get_derived_info_func getDerivedInfo);
    class_info* declareClass(NameView typeName,
        value_t type,
        std::size_t size,
        std::size_t alignment,
        get_derived_info_func getDerivedInfo);
    const typeinfo* registerType(NameView typeName, class_info* cls, std::uint64_t mask);
    bool setOwner(std::shared_ptr<void> owner);

    void requireDeclaredContracts() noexcept { requireDeclaredContracts_ = true; }

    bool declareContractRecord(NameView key, NameView contract);
    bool declareClassContract(NameView key, NameView contract, const class_info* cls);
    bool declareClassContract(NameView key,
        NameView contract,
        const class_info* cls,
        RegistrationOwnership ownership,
        RegistrationLifecycle lifecycle,
        NameView moduleExport);

    RegistrationReceipt receipt() const;
    const class_info* publicationClass(const class_info* staged) const;
    bool commit(RegistrationReceipt& receipt, core::pmr::String& diagnostics);
    bool commit(
        NameView expectedFingerprint, RegistrationReceipt& receipt, core::pmr::String& diagnostics);
    void reject(NameView diagnostics);
    void preserveSerializationMetadata(const typeinfo& type);

private:
    friend class db;

    bool ownsClass(const class_info* value) const;
    bool validate(core::pmr::String& diagnostics) const;

    core::pmr::String ownerIdentity_{&core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, core::UniquePtr<class_info>> classes_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, core::UniquePtr<typeinfo>> types_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Set<core::pmr::String> declaredClasses_{&core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, std::shared_ptr<void>> owners_{
        &core::defaultCpuMemoryResource()};
    ReflectionRegistrationContract declarations_;
    core::pmr::Map<core::pmr::String, const class_info*> contractClasses_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Map<core::pmr::String, core::pmr::String> contractClassSnapshots_{
        &core::defaultCpuMemoryResource()};

    struct ClassSemantics {
        RegistrationOwnership ownership = RegistrationOwnership::Unspecified;
        RegistrationLifecycle lifecycle = RegistrationLifecycle::Unspecified;
        core::pmr::String moduleExport{&core::defaultCpuMemoryResource()};
    };

    core::pmr::Map<const class_info*, ClassSemantics> classSemantics_{
        &core::defaultCpuMemoryResource()};

    struct SerializationMetadata {
        const CollectionAccess* collection = nullptr;
        core::pmr::Vector<int> enumValues{&core::defaultCpuMemoryResource()};
    };

    core::pmr::Map<const typeinfo*, SerializationMetadata> serializationMetadata_{
        &core::defaultCpuMemoryResource()};
    bool requireDeclaredContracts_ = false;
    core::pmr::String diagnostics_{&core::defaultCpuMemoryResource()};
    bool valid_ = true;
    bool committed_ = false;
};

ARP_API bool declareRegistrationContractRecord(NameView key, NameView contract);
ARP_API core::pmr::String canonicalRegistrationTypeContract(NameView sourceType,
    const core::pmr::Map<core::pmr::String, core::pmr::String>& reflectedNames);
ARP_API core::pmr::String registrationTypeContract(
    const typeinfo* type, bool isConst = false, bool isVolatile = false);
ARP_API bool declareRegistrationClassContract(
    NameView key, NameView contract, const class_info* cls);
ARP_API bool declareRegistrationClassContract(NameView key,
    NameView contract,
    const class_info* cls,
    RegistrationOwnership ownership,
    RegistrationLifecycle lifecycle,
    NameView moduleExport);

class ARP_API RegistrationScope final {
public:
    explicit RegistrationScope(RegistrationTransaction& transaction) noexcept;
    ~RegistrationScope();

    RegistrationScope(const RegistrationScope&) = delete;
    RegistrationScope& operator=(const RegistrationScope&) = delete;

private:
    RegistrationTransaction* transaction_ = nullptr;
    RegistrationTransaction* previous_ = nullptr;
};

namespace detail {

ARP_API RegistrationTransaction* currentRegistrationTransaction() noexcept;

} // namespace detail

} // namespace arp::reflection
