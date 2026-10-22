#pragma once

#include "core/runtime/async_runtime.h"
#include "reflection/reflection_macros.h"
#include "resource/resource_manager.h"
#include "scriptvm/plugin/cpp_plugin_api.h"
#include "scriptvm/runtime/module_registry.h"
#include "scriptvm/runtime/value.h"

#include <memory>
#include <mutex>

namespace arp::scriptvm {

inline constexpr const char* kResourceManagerService = "arp.resources.manager";
inline constexpr const char* kAsyncRuntimeService = "arp.runtime.async";
inline constexpr std::uint32_t kResourceManagerServiceVersion = 1u;
inline constexpr std::uint32_t kAsyncRuntimeServiceVersion = 1u;

// v2 plugin entry point used by both the dynamic importer and static catalog.
int registerResourceScriptPlugin(ArpScriptPluginHostV2* host);

} // namespace arp::scriptvm
