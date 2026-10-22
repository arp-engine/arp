#pragma once
#include "reflection/serialization.h"
#include "scriptvm/compile/ast.h"

namespace arp::scriptvm {
using Descriptor = reflection::SerializationDescriptor;

inline core::SharedPtr<Descriptor> serializationDescriptor(const TypeNode& type)
{
    auto result = core::makeShared<Descriptor>();
    if (type.kind == TypeKind::Optional || type.kind == TypeKind::List) {
        if (!type.elem)
            return {};
        result->kind = type.kind == TypeKind::Optional ? Descriptor::Kind::Optional
                                                       : Descriptor::Kind::Sequence;
        result->element = serializationDescriptor(*type.elem);
        return result->element ? result : nullptr;
    }
    if (type.kind == TypeKind::Class) {
        result->kind = Descriptor::Kind::Reference;
        result->referenceType = type.name;
        result->referenceClass = type.reflectedClass;
    } else if (type.name == "int")
        result->scalarType = reflection::type_of<int>();
    else if (type.name == "int64")
        result->scalarType = reflection::type_of<std::int64_t>();
    else if (type.name == "float")
        result->scalarType = reflection::type_of<double>();
    else if (type.name == "bool")
        result->scalarType = reflection::type_of<bool>();
    else if (type.name == "string")
        result->scalarType = reflection::type_of<core::pmr::String>();
    else
        return {};
    return result;
}

} // namespace arp::scriptvm
