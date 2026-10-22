#include "core/io/file_io.h"

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path testRoot()
{
    return std::filesystem::temp_directory_path() / "arp_core_file_io_tests";
}

void writeBytes(const std::filesystem::path& path, const char* bytes, std::size_t size)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file.write(bytes, static_cast<std::streamsize>(size));
}

void readsTextAndEmptyFiles()
{
    const auto root = testRoot();
    const auto textPath = root / "hello.txt";
    writeBytes(textPath, "hello\n", 6u);

    const auto text = arp::core::readTextFile(textPath);
    assert(text.success);
    assert(text.text == "hello\n");
    assert(text.diagnostics.empty());

    const auto emptyPath = root / "empty.txt";
    writeBytes(emptyPath, "", 0u);
    const auto empty = arp::core::readTextFile(emptyPath);
    assert(empty.success);
    assert(empty.text.empty());
}

void readsBinaryBytes()
{
    const auto path = testRoot() / "bytes.bin";
    const char bytes[] = {'\0', '\x7f', '\xff'};
    writeBytes(path, bytes, sizeof(bytes));

    const auto binary = arp::core::readBinaryFile(path);
    assert(binary.success);
    assert(binary.bytes.size() == 3u);
    assert(binary.bytes[0] == 0u);
    assert(binary.bytes[1] == 0x7fu);
    assert(binary.bytes[2] == 0xffu);
}

void missingFileReportsDiagnostics()
{
    const auto text = arp::core::readTextFile(testRoot() / "missing.txt");
    assert(!text.success);
    assert(!text.diagnostics.empty());

    const auto binary = arp::core::readBinaryFile(testRoot() / "missing.bin");
    assert(!binary.success);
    assert(!binary.diagnostics.empty());
}

} // namespace

int main()
{
    readsTextAndEmptyFiles();
    readsBinaryBytes();
    missingFileReportsDiagnostics();
    return 0;
}
