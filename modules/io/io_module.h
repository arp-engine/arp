#pragma once

#include "core/cpu_allocator.h"
#include "reflection/reflection.h"
#include "reflection/reflection_macros.h"

#include <cstdint>

namespace arp::scriptvm::plugins::io {

using arp::core::pmr::String;
using arp::reflection::hlist;

ASTRUCT(Name = "IoFileList")

struct IoFileList final {
    APROP()
    bool success = false;
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};

    IoFileList();

    AFUNC(Name = "count")
    int count() const;
    AFUNC(Name = "at")
    String at(int index) const;

    arp::core::pmr::Vector<String> paths{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT()

struct IoTextResult final {
    APROP()
    bool success = false;
    APROP()
    String text{&arp::core::defaultCpuMemoryResource()};
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT()

struct IoOperationResult final {
    APROP()
    bool success = false;
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT()

struct IoBytesResult final {
    APROP()
    bool success = false;
    APROP()
    hlist bytes = hlist(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

ASTRUCT()

struct IoHashResult final {
    APROP()
    bool success = false;
    APROP()
    String hash{&arp::core::defaultCpuMemoryResource()};
    APROP()
    std::int64_t hashValue = 0;
    APROP()
    String diagnostics{&arp::core::defaultCpuMemoryResource()};
};

AFUNC(Name = "read_text")
IoTextResult readText(const String& path);

AFUNC(Name = "read_bytes")
IoBytesResult readBytes(const String& path);

AFUNC(Name = "write_text")
IoOperationResult writeText(const String& path, const String& text);

AFUNC(Name = "write_bytes")
IoOperationResult writeBytes(const String& path, const hlist& values);

AFUNC(Name = "make_dirs")
IoOperationResult makeDirs(const String& path);

AFUNC(Name = "exists")
bool exists(const String& path);

AFUNC(Name = "list_files", DefaultArguments = false)
hlist listFiles(const String& root, bool recursive);

AFUNC(Name = "list_paths", DefaultArguments = false)
IoFileList listPaths(const String& root, bool recursive);

AFUNC(Name = "hash_file")
IoHashResult hashFile(const String& path);

AFUNC(Name = "hash_text")
IoHashResult hashText(const String& text);

} // namespace arp::scriptvm::plugins::io
