#include "io_module.h"

#include "core/file_io.h"
#include "core/stable_hash.h"
#include "scriptvm/runtime/cpp_plugin_api.h"
#include "scriptvm/runtime/value.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <utility>

namespace arp::scriptvm::plugins::io {

using arp::scriptvm::Value;

IoFileList::IoFileList()
    : diagnostics(&arp::core::defaultCpuMemoryResource()),
      paths(&arp::core::defaultCpuMemoryResource())
{}

int IoFileList::count() const
{
    return static_cast<int>(paths.size());
}

String IoFileList::at(int index) const
{
    if (index < 0 || index >= static_cast<int>(paths.size())) {
        return String(&arp::core::defaultCpuMemoryResource());
    }
    return paths[static_cast<std::size_t>(index)];
}

IoTextResult makeTextResult(const arp::core::TextFileReadResult& read)
{
    IoTextResult result;
    result.success = read.success;
    result.text = read.text;
    result.diagnostics = read.diagnostics;
    return result;
}

IoBytesResult makeBytesResult(const arp::core::BinaryFileReadResult& read)
{
    IoBytesResult result;
    result.success = read.success;
    result.diagnostics = read.diagnostics;
    result.bytes.reserve(read.bytes.size());
    for (const auto byte : read.bytes) {
        result.bytes.emplace_back(arp::scriptvm::makeInt(static_cast<int>(byte)));
    }
    return result;
}

IoTextResult readText(const String& path)
{
    return makeTextResult(arp::core::readTextFile(std::filesystem::path(path.c_str())));
}

IoBytesResult readBytes(const String& path)
{
    return makeBytesResult(arp::core::readBinaryFile(std::filesystem::path(path.c_str())));
}

IoOperationResult writeText(const String& path, const String& text)
{
    IoOperationResult result;
    std::ofstream file(std::filesystem::path(path.c_str()), std::ios::binary);
    if (!file.good()) {
        result.diagnostics =
            String("io.write_text failed to open path", &arp::core::defaultCpuMemoryResource());
        return result;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    result.success = file.good();
    if (!result.success) {
        result.diagnostics =
            String("io.write_text failed to write path", &arp::core::defaultCpuMemoryResource());
    }
    return result;
}

IoOperationResult writeBytes(const String& path, const hlist& values)
{
    IoOperationResult result;
    std::ofstream file(std::filesystem::path(path.c_str()), std::ios::binary);
    if (!file.good()) {
        result.diagnostics =
            String("io.write_bytes failed to open path", &arp::core::defaultCpuMemoryResource());
        return result;
    }
    for (const auto& value : values) {
        int byte = 0;
        if (!arp::scriptvm::tryGetInt(value, byte) || byte < 0 || byte > 255) {
            result.diagnostics = String(
                "io.write_bytes received a non-byte value", &arp::core::defaultCpuMemoryResource());
            return result;
        }
        const auto octet = static_cast<char>(byte);
        file.write(&octet, 1);
    }
    result.success = file.good();
    if (!result.success) {
        result.diagnostics =
            String("io.write_bytes failed to write path", &arp::core::defaultCpuMemoryResource());
    }
    return result;
}

IoOperationResult makeDirs(const String& path)
{
    IoOperationResult result;
    std::error_code error;
    std::filesystem::create_directories(path.c_str(), error);
    result.success = !error;
    if (error) {
        result.diagnostics = String("io.make_dirs failed", &arp::core::defaultCpuMemoryResource());
    }
    return result;
}

bool exists(const String& path)
{
    return std::filesystem::exists(path.c_str());
}

hlist listFiles(const String& root, bool recursive)
{
    hlist files(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    std::error_code error;
    if (recursive) {
        for (const auto& entry :
            std::filesystem::recursive_directory_iterator(root.c_str(), error)) {
            if (error)
                break;
            if (entry.is_regular_file(error) && !error) {
                files.emplace_back(
                    String(entry.path().generic_string(), &arp::core::defaultCpuMemoryResource()));
            }
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(root.c_str(), error)) {
            if (error)
                break;
            if (entry.is_regular_file(error) && !error) {
                files.emplace_back(
                    String(entry.path().generic_string(), &arp::core::defaultCpuMemoryResource()));
            }
        }
    }
    if (error) {
        return hlist(hlist::allocator_type(&arp::core::defaultCpuMemoryResource()));
    }
    std::sort(files.begin(), files.end(), [](const Value& lhs, const Value& rhs) {
        return lhs.cast<const String&>() < rhs.cast<const String&>();
    });
    return files;
}

IoFileList listPaths(const String& root, bool recursive)
{
    IoFileList result;
    std::error_code error;
    const auto appendEntry = [&](const std::filesystem::directory_entry& entry) {
        if (entry.is_regular_file(error) && !error) {
            result.paths.emplace_back(
                String(entry.path().generic_string(), &arp::core::defaultCpuMemoryResource()));
        }
    };
    if (recursive) {
        for (const auto& entry :
            std::filesystem::recursive_directory_iterator(root.c_str(), error)) {
            if (error)
                break;
            appendEntry(entry);
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(root.c_str(), error)) {
            if (error)
                break;
            appendEntry(entry);
        }
    }
    if (error) {
        result.diagnostics = String("io.list_paths failed", &arp::core::defaultCpuMemoryResource());
        return result;
    }
    std::sort(result.paths.begin(), result.paths.end());
    result.success = true;
    return result;
}

String hashBytes(const arp::core::pmr::Vector<std::uint8_t>& bytes)
{
    arp::core::StableHash64 hash;
    hash.appendBytes(bytes.data(), bytes.size());
    char buffer[17]{};
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), hash.value(), 16);
    return String(buffer, converted.ptr, &arp::core::defaultCpuMemoryResource());
}

std::uint32_t hashValue(const void* data, std::size_t size)
{
    arp::core::StableHash64 hash;
    hash.appendBytes(data, size);
    return static_cast<std::uint32_t>(hash.value());
}

String hashValueText(const void* data, std::size_t size)
{
    arp::core::StableHash64 hash;
    hash.appendBytes(data, size);
    char buffer[17]{};
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), hash.value(), 16);
    return String(buffer, converted.ptr, &arp::core::defaultCpuMemoryResource());
}

IoHashResult hashFile(const String& path)
{
    IoHashResult result;
    const auto read = arp::core::readBinaryFile(std::filesystem::path(path.c_str()));
    if (!read.success) {
        result.diagnostics = read.diagnostics;
        return result;
    }
    result.success = true;
    result.hash = hashBytes(read.bytes);
    result.hashValue = static_cast<std::int64_t>(hashValue(read.bytes.data(), read.bytes.size()));
    return result;
}

IoHashResult hashText(const String& text)
{
    IoHashResult result;
    result.success = true;
    result.hash = hashValueText(text.data(), text.size());
    result.hashValue = static_cast<std::int64_t>(hashValue(text.data(), text.size()));
    return result;
}

} // namespace arp::scriptvm::plugins::io

namespace arp::reflection::generated {
bool register_io_reflection(arp::scriptvm::ModuleRegistration* module);
} // namespace arp::reflection::generated

extern "C" ARP_SCRIPT_PLUGIN_EXPORT int arp_register_io(ArpScriptPluginHostV2* host)
{
    if (host == nullptr || host->abiVersion != ARP_SCRIPT_PLUGIN_ABI_VERSION_V2
        || host->registrar == nullptr) {
        return 0;
    }

    return arp::reflection::generated::register_io_reflection(host->registrar->reflectionModule())
        ? 1
        : 0;
}
