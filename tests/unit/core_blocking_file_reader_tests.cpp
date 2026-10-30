#include "core/io/blocking_file_reader.h"
#include "core/io/filesystem_path.h"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path testRoot()
{
    return std::filesystem::temp_directory_path() / "arp_blocking_file_reader_tests";
}

void writeFixture(const std::filesystem::path& path)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << "abcdef";
}

arp::core::io::FileReadRequest requestFor(const std::filesystem::path& path)
{
    arp::core::io::FileReadRequest request;
    request.pathScope = arp::core::io::FilePathScope::HostFilesystem;
    request.path = arp::core::FilePath::utf8(path);
    return request;
}

} // namespace

int main()
{
    using namespace arp::core::io;

    const auto path = testRoot() / "fixture.bin";
    writeFixture(path);

    BlockingFileReader reader;
    auto whole = reader.read(requestFor(path));
    assert(whole.status == FileIoStatus::Success);
    assert(whole.bytes.size() == 6);
    assert(whole.bytes[0] == 'a');
    assert(whole.bytes[5] == 'f');

    auto exactRequest = requestFor(path);
    exactRequest.range = FileReadRange::exactRange(2, 3);
    auto exact = reader.read(exactRequest);
    assert(exact.status == FileIoStatus::Success);
    assert(exact.bytes.size() == 3);
    assert(exact.bytes[0] == 'c');
    assert(exact.bytes[2] == 'e');

    auto invalidOffset = requestFor(path);
    invalidOffset.range = FileReadRange::exactRange(99, 1);
    assert(reader.read(invalidOffset).status == FileIoStatus::InvalidRange);

    auto denied = requestFor(path);
    denied.range = FileReadRange::exactRange(4, 8);
    denied.partialPolicy = FilePartialReadPolicy::DenyPartial;
    assert(reader.read(denied).status == FileIoStatus::PartialReadDenied);

    auto missing = requestFor(testRoot() / "missing.bin");
    assert(reader.read(missing).status == FileIoStatus::MissingFile);

    const auto unicodePath = testRoot() / std::filesystem::u8path(u8"路径/文件.bin");
    writeFixture(unicodePath);
    const auto unicode = reader.read(requestFor(unicodePath));
    assert(unicode.status == FileIoStatus::Success && unicode.bytes == whole.bytes);

    return 0;
}
