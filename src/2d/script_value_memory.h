#pragma once
#include "physics2d/physics_world_2d.h"
#include "scriptvm/aot/script_value_memory_accountant.h"

namespace arp::scene2d {
template <class T>
inline bool describeScalarScriptValue(
    const scriptvm::Value& value, scriptvm::ScriptValueMemoryDescriptor& out) noexcept
{
    if (const auto* data = scriptvm::checkedOwnedValuePayload<T>(value)) {
        out.allocationIdentity = data;
        out.retainedBytes = 0;
        return true;
    }
    return false;
}

// Owned native values embed scalar fields in their allocator-accounted Value
// carrier. ShapeDesc additionally owns one explicit PMR point buffer.
inline bool describeScriptValueMemory(
    void*, const scriptvm::Value& value, scriptvm::ScriptValueMemoryDescriptor& out) noexcept
{
    if (value.is_native_reference())
        return false;
    if (const auto* shape = scriptvm::checkedOwnedValuePayload<physics2d::ShapeDesc>(value)) {
        out.allocationIdentity = shape->points.capacity()
            ? static_cast<const void*>(shape->points.data())
            : static_cast<const void*>(shape);
        out.retainedBytes = shape->points.capacity() * sizeof(core::Vec2);
        return true;
    }
    return describeScalarScriptValue<core::Vec2>(value, out)
        || describeScalarScriptValue<core::Vec3>(value, out)
        || describeScalarScriptValue<core::Vec4>(value, out)
        || describeScalarScriptValue<core::Quat>(value, out)
        || describeScalarScriptValue<scene::Transform>(value, out)
        || describeScalarScriptValue<physics2d::BodyDesc>(value, out)
        || describeScalarScriptValue<physics2d::BodyState>(value, out)
        || describeScalarScriptValue<physics2d::CollisionFilter>(value, out)
        || describeScalarScriptValue<physics2d::JointDesc>(value, out)
        || describeScalarScriptValue<physics2d::PhysicsQueryHit>(value, out)
        || describeScalarScriptValue<physics2d::BodyType>(value, out)
        || describeScalarScriptValue<physics2d::ShapeType>(value, out)
        || describeScalarScriptValue<physics2d::JointType>(value, out);
}
} // namespace arp::scene2d
