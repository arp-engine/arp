#pragma once

#include "core/concurrency/callback.h"
#include "core/encoding/yaml_contract.h"
#include "reflection/reflection.h"
#include "reflection/serialization_value.h"

namespace arp::reflection {

enum class SerializationProfile { Authoring, LegacyAllPropertiesV1 };

struct SerializationOptions {
    SerializationProfile profile = SerializationProfile::Authoring;
    std::size_t maxDepth = 32;
    std::size_t maxValues = 65536;
    std::size_t maxCollectionLength = 4096;
    std::size_t maxBytes = 64u * 1024u * 1024u;
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
class ARP_API SerializationContext {
public:
    virtual ~SerializationContext() = default;

    virtual bool writeReference(
        const property&, const object&, core::YamlContract::Node&, core::pmr::String&) const
    {
        return false;
    }

    virtual bool readReference(
        const property&, const core::YamlContract::Node&, object&, core::pmr::String&) const
    {
        return false;
    }

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

    virtual bool writeReferenceValue(
        const property&, const object&, SerializationValue&, core::pmr::String&) const;
    virtual bool readReferenceValue(
        const property&, const SerializationValue&, object&, core::pmr::String&) const;
    virtual bool writeReferenceValue(const SerializationDescriptor&,
        const object&,
        SerializationValue&,
        core::pmr::String&) const;
    virtual bool readReferenceValue(const SerializationDescriptor&,
        const SerializationValue&,
        object&,
        core::pmr::String&) const;
};

ARP_API SerializationValue serialization_value_from_yaml(
    const core::YamlContract::Node&, SerializationOptions = {});

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

    bool writeFields(const object&, SerializationValue&);
    bool readFields(const SerializationValue&, const object&);
    bool writeValue(const object&, SerializationValue&);
    bool readValue(const SerializationValue&, const typeinfo*, object&);
    bool writeValue(const SerializationDescriptor&, const object&, SerializationValue&);
    bool readValue(const SerializationDescriptor&, const SerializationValue&, object&);

    void setPath(NameView path) { path_.assign(path); }

    std::size_t valuesConsumed() const { return values_; }

    const SerializationDiagnostic& diagnostic() const { return diagnostic_; }

    core::pmr::String diagnostics() const;

    // Raw pointer properties remain valid only while these retained objects
    // live. Callers using readFields must transfer this ownership alongside
    // the destination; scene authoring states use value/reference fields.
    core::pmr::Vector<object> releaseOwnedObjects() { return std::move(ownedObjects_); }

private:
    bool fields(const object&, SerializationValue&, const SerializationValue*, std::size_t);
    bool write(const object&, SerializationValue&, std::size_t);
    bool read(const SerializationValue&, const typeinfo*, object&, std::size_t);
    bool readObject(const SerializationValue&, const class_info*, bool, object&, std::size_t);
    bool fail(NameView);
    bool visit(std::size_t);
    bool described(const SerializationDescriptor&,
        object&,
        SerializationValue&,
        const SerializationValue*,
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
