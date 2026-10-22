#pragma once

#include "core/concurrency/callback.h"
#include "core/encoding/yaml_contract.h"
#include "reflection/reflection.h"

namespace arp::reflection {

enum class SerializationProfile { Authoring, LegacyAllPropertiesV1 };

struct SerializationOptions {
    SerializationProfile profile = SerializationProfile::Authoring;
    std::size_t maxDepth = 32;
    std::size_t maxValues = 65536;
    std::size_t maxCollectionLength = 4096;
};

// Runtime languages expose field schemas without inventing C++ payload types.
// Accessors retain VM object identity; the archive owns all YAML traversal.
struct SerializationDescriptor {
    enum class Kind { Scalar, Optional, Sequence, Record, Reference };
    Kind kind = Kind::Scalar;
    const typeinfo* scalarType = nullptr;
    core::pmr::String referenceType{&core::defaultCpuMemoryResource()};
    const class_info* referenceClass = nullptr;
    core::SharedPtr<SerializationDescriptor> element;

    struct Field {
        core::pmr::String name;
        core::SharedPtr<SerializationDescriptor> type;
    };

    core::pmr::Vector<Field> fields{&core::defaultCpuMemoryResource()};
    core::SharedCallback<object(const object&, NameView)> get;
    core::SharedCallback<void(const object&, NameView, const object&)> set;
};

// Reference ownership and lookup belong to the caller. Reflection only sees
// declared property metadata and an external resolver, never domain services.
class SerializationContext {
public:
    virtual ~SerializationContext() = default;
    virtual bool writeReference(
        const property&, const object&, core::YamlContract::Node&, core::pmr::String&) const = 0;
    virtual bool readReference(
        const property&, const core::YamlContract::Node&, object&, core::pmr::String&) const = 0;

    virtual bool writeReference(const SerializationDescriptor&,
        const object&,
        core::YamlContract::Node&,
        core::pmr::String&) const
    {
        return false;
    }

    virtual bool readReference(const SerializationDescriptor&,
        const core::YamlContract::Node&,
        object&,
        core::pmr::String&) const
    {
        return false;
    }
};

struct SerializationDiagnostic {
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    core::pmr::String message{&core::defaultCpuMemoryResource()};
};

// Embedded fields and standalone YAML share this single traversal. Callers
// stage engine-owned authoring state, then apply it through domain validation.
class ARP_API SerializationArchive final {
public:
    explicit SerializationArchive(
        SerializationOptions options = {}, const SerializationContext* context = nullptr);
    bool writeFields(const object&, core::YamlContract::Node&);
    bool readFields(const core::YamlContract::Node&, const object&);
    bool writeValue(const object&, core::YamlContract::Node&);
    bool readValue(const core::YamlContract::Node&, const typeinfo*, object&);
    bool writeValue(const SerializationDescriptor&, const object&, core::YamlContract::Node&);
    bool readValue(const SerializationDescriptor&, const core::YamlContract::Node&, object&);

    void setPath(NameView path) { path_.assign(path); }

    std::size_t valuesConsumed() const { return values_; }

    const SerializationDiagnostic& diagnostic() const { return diagnostic_; }

    core::pmr::String diagnostics() const;

    // Raw pointer properties remain valid only while these retained objects
    // live. Callers using readFields must transfer this ownership alongside
    // the destination; scene authoring states use value/reference fields.
    core::pmr::Vector<object> releaseOwnedObjects() { return std::move(ownedObjects_); }

private:
    bool fields(
        const object&, core::YamlContract::Node&, const core::YamlContract::Node*, std::size_t);
    bool write(const object&, core::YamlContract::Node&, std::size_t);
    bool read(const core::YamlContract::Node&, const typeinfo*, object&, std::size_t);
    bool readObject(const core::YamlContract::Node&, const class_info*, bool, object&, std::size_t);
    bool fail(NameView);
    bool visit(std::size_t);
    bool described(const SerializationDescriptor&,
        object&,
        core::YamlContract::Node&,
        const core::YamlContract::Node*,
        std::size_t);
    SerializationOptions options_;
    const SerializationContext* context_;
    SerializationDiagnostic diagnostic_;
    core::pmr::String path_{"$", &core::defaultCpuMemoryResource()};
    std::size_t values_ = 0;
    core::pmr::Vector<object> ownedObjects_{&core::defaultCpuMemoryResource()};
};

struct ReflectionYamlResult {
    bool success = false;
    core::pmr::String yaml{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct ReflectionObjectResult {
    bool success = false;
    object value;
    core::pmr::Vector<object> ownedObjects = core::pmr::Vector<object>(
        core::pmr::Vector<object>::allocator_type(&core::defaultCpuMemoryResource()));
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

ARP_API ReflectionYamlResult serialize_to_yaml(
    const object& value, SerializationOptions options = {});
ARP_API ReflectionObjectResult deserialize_from_yaml(
    NameView yamlText, SerializationOptions options = {});

} // namespace arp::reflection
