#include "scriptvm/runtime/module_registry.h"

namespace arp::scriptvm {

bool isReflectedTypeBinding(const Value& value)
{
    return value.is<ReflectedTypeBinding>();
}

const arp::reflection::class_info* asReflectedTypeBinding(const Value& value)
{
    return isReflectedTypeBinding(value) ? value.ptr<ReflectedTypeBinding>()->classInfo : nullptr;
}

bool isModuleObject(const Value& value)
{
    return value.is<ModuleObject>();
}

std::shared_ptr<ModuleExports> asModuleObject(const Value& value)
{
    return isModuleObject(value) ? value.ptr<ModuleObject>()->exports : nullptr;
}

} // namespace arp::scriptvm
