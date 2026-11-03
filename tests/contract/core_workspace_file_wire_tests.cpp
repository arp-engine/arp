#include "core/io/workspace_file_wire.h"

#include <cassert>
#include <cstdio>
#include <limits>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace {
using namespace arp::core;
using namespace arp::core::io;
using Wire = WorkspaceFileWire;

template <class Action> void rejected(Action&& action)
{
    bool failed = false;
    try {
        action();
    } catch (const WorkspaceFileWireError& error) {
        assert(error.diagnostic);
        failed = true;
    }
    assert(failed);
}

void cycle()
{
    assert(Wire::requestVersion("wire A") != Wire::requestVersion("wire B"));
    for (const auto kind : {FileVersionKind::None,
             FileVersionKind::ContentSha256,
             FileVersionKind::MetadataObservation,
             FileVersionKind::DirectorySnapshotSha256}) {
        FileVersionToken token;
        token.kind = kind;
        if (kind != FileVersionKind::None)
            for (unsigned i = 0; i < token.digest.size(); ++i)
                token.digest[i] = static_cast<std::uint8_t>(i * 7);
        json::JsonDocument document;
        auto node = Wire::version(token, document.GetAllocator());
        assert(Wire::parseVersion(node) == token);
        FileMetadata metadata{FileEntryKind::RegularFile, 42, token};
        node = Wire::metadata(metadata, document.GetAllocator());
        auto decoded = Wire::parseMetadata(node);
        assert(
            decoded.kind == metadata.kind && decoded.sizeBytes == 42 && decoded.version == token);
        Wire::field(node, "name", "rules.arp", document.GetAllocator());
        assert(Wire::parseMetadata(node, true).version == token);
        rejected([&] {
            Wire::parseMetadata(node);
        });
    }
    // Each enumerator and wire spelling is tested; unknown values never
    // acquire an implicit Success/default enum entry.
    for (unsigned value = 0; value <= unsigned(FileIoStatus::OutcomeUnknown); ++value) {
        const auto status = static_cast<FileIoStatus>(value);
        assert(Wire::parseStatus(Wire::statusName(status)) == status);
    }
    rejected([] {
        Wire::parseStatus("future-status");
    });
    rejected([] {
        Wire::statusName(static_cast<FileIoStatus>(255));
    });
    for (const auto* source :
        {R"({"kind":"none","digest":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=","kind":"none"})",
            R"({"kind":"future","digest":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="})",
            R"({"kind":"none","digest":"AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="})",
            R"({"kind":"content-sha256","digest":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB="})",
            R"({"kind":"none","digest":0})",
            R"({"kind":"none"})"}) {
        json::JsonDocument document;
        document.Parse(source);
        assert(!document.HasParseError());
        rejected([&] {
            Wire::parseVersion(document);
        });
    }
    for (const auto* source :
        {R"({"kind":"future","size":0,"version":{"kind":"none","digest":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="}})",
            R"({"kind":"file","size":-1,"version":{"kind":"none","digest":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="}})",
            R"({"kind":"file","size":9223372036854775808,"version":{"kind":"none","digest":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="}})",
            R"({"kind":"file","size":0,"size":1,"version":{"kind":"none","digest":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="}})"}) {
        json::JsonDocument document;
        document.Parse(source);
        assert(!document.HasParseError());
        rejected([&] {
            Wire::parseMetadata(document);
        });
    }
    {
        json::JsonDocument document;
        rejected([&] {
            Wire::metadata({FileEntryKind::RegularFile, UINT64_MAX, {}}, document.GetAllocator());
        });
    }
    // Both DOM and writer use core allocators, including their stack storage.
    {
        json::JsonDocument document;
        document.SetObject();
        Wire::field(document, "message", u8"雪", document.GetAllocator());
        rapidjson::GenericStringBuffer<rapidjson::UTF8<>, json::CoreJsonAllocator> buffer(
            &json::jsonAllocator());
        rapidjson::
            Writer<decltype(buffer), rapidjson::UTF8<>, rapidjson::UTF8<>, json::CoreJsonAllocator>
                writer(buffer, &json::jsonAllocator());
        assert(document.Accept(writer));
        json::JsonDocument parsed;
        parsed.Parse<rapidjson::kParseValidateEncodingFlag>(buffer.GetString(), buffer.GetSize());
        assert(!parsed.HasParseError() && Wire::text(parsed, "message") == u8"雪");
    }
    {
        json::CoreJsonAllocator allocator;
        bool failed = false;
        try {
            (void)allocator.Malloc(std::numeric_limits<std::size_t>::max());
        } catch (const std::bad_alloc&) {
            failed = true;
        }
        assert(failed);
    }
}
} // namespace

int main(int argc, char** argv)
{
    const auto loops = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20u : 1u;
    cycle();
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < loops; ++i)
        cycle();
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Workspace wire %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
