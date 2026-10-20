#pragma once

#include "core/cpu_allocator.h"
#include "reflection/reflection_macros.h"

namespace arp::scriptvm::plugins::import_host {

using arp::core::pmr::String;

ASTRUCT()

struct PathResult final {
    APROP()
    bool success = false;
    APROP()
    String path{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

AFUNC(Name = "resolve_path")
PathResult resolvePath(const String& root, const String& relative);

AFUNC(Name = "path_extension")
String pathExtension(const String& path);

AFUNC(Name = "path_replace_extension")
String pathReplaceExtension(const String& path, const String& extension);

} // namespace arp::scriptvm::plugins::import_host
