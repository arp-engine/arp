#include "arp_import_host_module.h"

#include "core/cpu_allocator.h"
#include "scriptvm/runtime/cpp_plugin_api.h"
#include "scriptvm/runtime/value.h"

#include <filesystem>
#include <utility>

namespace arp::scriptvm::plugins::import_host {

using arp::core::pmr::String;

bool hasParentTraversal(const std::filesystem::path& path)
{
    for (const auto& component : path) {
        if (component == "..") {
            return true;
        }
    }
    return false;
}

PathResult resolvePath(const String& root, const String& relative)
{
    const std::filesystem::path relativePath(relative.c_str());
    if (relativePath.empty() || relativePath.is_absolute() || hasParentTraversal(relativePath)) {
        PathResult result;
        result.diagnostics =
            String("arp_import_host.resolve_path requires a non-empty relative path without '..'",
                &arp::core::defaultCpuMemoryResource());
        return result;
    }

    const auto resolved = std::filesystem::path(root.c_str()) / relativePath;
    PathResult result;
    result.success = true;
    result.path = String(
        resolved.lexically_normal().generic_string(), &arp::core::defaultCpuMemoryResource());
    return result;
}

String pathExtension(const String& path)
{
    return String(std::filesystem::path(path.c_str()).extension().generic_string(),
        &arp::core::defaultCpuMemoryResource());
}

String pathReplaceExtension(const String& path, const String& extension)
{
    auto value = std::filesystem::path(path.c_str());
    value.replace_extension(extension.c_str());
    return String(value.generic_string(), &arp::core::defaultCpuMemoryResource());
}

} // namespace arp::scriptvm::plugins::import_host

namespace arp::reflection::generated {
bool register_arp_import_host_reflection(arp::scriptvm::ModuleRegistration* module);
} // namespace arp::reflection::generated

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_import_host(ArpScriptPluginHostV2* host)
{
    if (host == nullptr || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2
        || host->registrar == nullptr) {
        return 0;
    }
    return arp::reflection::generated::register_arp_import_host_reflection(
               host->registrar->reflectionModule())
        ? 1
        : 0;
}
