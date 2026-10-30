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

void preservesRawTextAndEnforcesBudget()
{
    const auto path = testRoot() / "raw.txt";
    const char raw[] = {'\xef', '\xbb', '\xbf', 'a', '\r', '\n', '\0', '\xff'};
    writeBytes(path, raw, sizeof(raw));
    const auto text = arp::core::readTextFile(path, sizeof(raw));
    const auto binary = arp::core::readBinaryFile(path, sizeof(raw));
    assert(text.success && binary.success);
    assert(text.text.size() == sizeof(raw) && binary.bytes.size() == sizeof(raw));
    for (std::size_t i = 0; i < sizeof(raw); ++i) {
        assert(static_cast<unsigned char>(text.text[i]) == static_cast<unsigned char>(raw[i]));
        assert(binary.bytes[i] == static_cast<unsigned char>(raw[i]));
    }
    const auto deniedText = arp::core::readTextFile(path, sizeof(raw) - 1);
    const auto deniedBinary = arp::core::readBinaryFile(path, sizeof(raw) - 1);
    assert(!deniedText.success && deniedText.text.empty());
    assert(!deniedBinary.success && deniedBinary.bytes.empty());
    assert(deniedText.diagnostics.find("text file exceeds byte budget: ") == 0);
    assert(deniedBinary.diagnostics.find("binary file exceeds byte budget: ") == 0);
}

} // namespace

int main()
{
    readsTextAndEmptyFiles();
    readsBinaryBytes();
    missingFileReportsDiagnostics();
    preservesRawTextAndEnforcesBudget();
    return 0;
}
