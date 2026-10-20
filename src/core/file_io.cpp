#include "core/file_io.h"

#include <fstream>

namespace arp::core {

namespace {

pmr::String pathText(const std::filesystem::path& path)
{
    const auto text = path.generic_string();
    return pmr::String(text.data(), text.size(), &defaultCpuMemoryResource());
}

pmr::String diagnosticText(const char* prefix, const std::filesystem::path& path)
{
    pmr::String diagnostics(prefix, &defaultCpuMemoryResource());
    diagnostics += pathText(path);
    return diagnostics;
}

} // namespace

TextFileReadResult readTextFile(const std::filesystem::path& path, std::uint64_t maxBytes)
{
    TextFileReadResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        result.diagnostics = diagnosticText("failed to open text file: ", path);
        return result;
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        result.diagnostics = diagnosticText("failed to size text file: ", path);
        return result;
    }
    file.seekg(0, std::ios::beg);

    if (static_cast<std::uint64_t>(size) > maxBytes) {
        result.diagnostics = diagnosticText("text file exceeds byte budget: ", path);
        return result;
    }

    result.text.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(result.text.data(), size);
        if (!file.good()) {
            result.text.clear();
            result.diagnostics = diagnosticText("failed to read text file: ", path);
            return result;
        }
    }
    result.success = true;
    return result;
}

BinaryFileReadResult readBinaryFile(const std::filesystem::path& path, std::uint64_t maxBytes)
{
    BinaryFileReadResult result;
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        result.diagnostics = diagnosticText("failed to open binary file: ", path);
        return result;
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        result.diagnostics = diagnosticText("failed to size binary file: ", path);
        return result;
    }
    file.seekg(0, std::ios::beg);

    if (static_cast<std::uint64_t>(size) > maxBytes) {
        result.diagnostics = diagnosticText("binary file exceeds byte budget: ", path);
        return result;
    }

    result.bytes.resize(static_cast<std::size_t>(size));
    if (size > 0) {
        file.read(reinterpret_cast<char*>(result.bytes.data()), size);
        if (!file.good()) {
            result.bytes.clear();
            result.diagnostics = diagnosticText("failed to read binary file: ", path);
            return result;
        }
    }
    result.success = true;
    return result;
}

} // namespace arp::core
