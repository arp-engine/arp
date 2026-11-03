#pragma once

#include "core/encoding/base64.h"
#include "core/encoding/json_document.h"
#include "core/io/file_query.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace arp::core::io {

struct WorkspaceFileWireError {
    const char* diagnostic;
};

// File-domain values of workspace wire / 2. Transport, authentication,
// dispatch, receipts and resource parsers deliberately remain outside this codec.
class WorkspaceFileWire final {
    using Value = core::json::JsonValue;
    using Allocator = core::json::CoreJsonAllocator;

public:
    static constexpr std::uint32_t contractVersion = 2;
    static constexpr const char* endpointPath = "/workspace/v2";

    static bool identifier(std::string_view value) noexcept
    {
        if (value.empty() || value.size() > 64)
            return false;
        for (const char c : value)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                    || c == '-' || c == '_'))
                return false;
        return true;
    }

    static core::pmr::String encode(const Value& value, std::size_t maxBytes)
    {
        rapidjson::GenericStringBuffer<rapidjson::UTF8<>, Allocator> buffer(
            &core::json::jsonAllocator());
        rapidjson::Writer<decltype(buffer), rapidjson::UTF8<>, rapidjson::UTF8<>, Allocator> writer(
            buffer, &core::json::jsonAllocator());
        require(value.Accept(writer) && buffer.GetSize() <= maxBytes, "WORKSPACE-WIRE-BUDGET");
        return {buffer.GetString(), buffer.GetSize(), &core::defaultCpuMemoryResource()};
    }

    static FileVersionToken sessionVersion(
        std::string_view endpoint, std::string_view workspace, std::string_view session)
    {
        core::Sha256 hash;
        hash.update("WorkspaceFileSession/1");
        for (const auto value : {endpoint, workspace, session}) {
            FileVersionEncoding::appendNumber(hash, value.size());
            hash.update(value);
        }
        return FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
    }

    static FileVersionToken requestVersion(std::string_view wireBytes)
    {
        core::Sha256 hash;
        hash.update(wireBytes);
        return FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
    }

    static void require(bool valid, const char* diagnostic = "WORKSPACE-REQUEST-INVALID")
    {
        if (!valid)
            throw WorkspaceFileWireError{diagnostic};
    }

    static const Value& member(const Value& node, const char* key)
    {
        const auto* value = core::json::jsonMember(node, key);
        require(value != nullptr);
        return *value;
    }

    static std::string_view text(const Value& node, const char* key)
    {
        const auto& value = member(node, key);
        require(value.IsString());
        return core::json::jsonText(value);
    }

    static std::uint64_t number(const Value& node, const char* key)
    {
        const auto& value = member(node, key);
        require(value.IsUint64() && value.GetUint64() <= INT64_MAX);
        return value.GetUint64();
    }

    static Value string(std::string_view value, Allocator& allocator)
    {
        require(value.size() <= UINT32_MAX, "WORKSPACE-WIRE-STRING-BUDGET");
        return Value(value.empty() ? "" : value.data(),
            static_cast<rapidjson::SizeType>(value.size()),
            allocator);
    }

    static void field(Value& node, const char* key, std::string_view value, Allocator& allocator)
    {
        node.AddMember(rapidjson::StringRef(key), string(value, allocator), allocator);
    }

    static const char* statusName(FileIoStatus status)
    {
        for (const auto& entry : statuses_)
            if (entry.first == status)
                return entry.second;
        throw WorkspaceFileWireError{"WORKSPACE-RESULT-STATUS"};
    }

    static FileIoStatus parseStatus(std::string_view name)
    {
        for (const auto& entry : statuses_)
            if (entry.second == name)
                return entry.first;
        throw WorkspaceFileWireError{"WORKSPACE-RESULT-STATUS"};
    }

    static const char* versionName(FileVersionKind kind)
    {
        for (const auto& entry : versions_)
            if (entry.first == kind)
                return entry.second;
        throw WorkspaceFileWireError{"WORKSPACE-RESULT-VERSION"};
    }

    static Value version(const FileVersionToken& value, Allocator& allocator)
    {
        require(value.valid(), "WORKSPACE-RESULT-VERSION");
        Value result(rapidjson::kObjectType);
        field(result, "kind", versionName(value.kind), allocator);
        core::ByteBuffer bytes(value.digest.begin(), value.digest.end());
        field(result, "digest", core::Base64::encode(bytes), allocator);
        return result;
    }

    static FileVersionToken parseVersion(const Value& node)
    {
        require(core::json::jsonOnlyUniqueKeys(node, {"kind", "digest"}));
        const auto kind = text(node, "kind");
        FileVersionToken result;
        bool matched = false;
        for (const auto& entry : versions_)
            if (kind == entry.second) {
                result.kind = entry.first;
                matched = true;
            }
        require(matched);
        const auto encoded = text(node, "digest");
        core::ByteBuffer bytes;
        require(encoded.size() == 44 && core::Base64::decode(encoded, bytes)
            && bytes.size() == result.digest.size() && core::Base64::encode(bytes) == encoded);
        std::copy(bytes.begin(), bytes.end(), result.digest.begin());
        require(result.valid());
        return result;
    }

    static Value metadata(const FileMetadata& value, Allocator& allocator)
    {
        require(value.kind == FileEntryKind::RegularFile || value.kind == FileEntryKind::Directory
                || value.kind == FileEntryKind::Link,
            "WORKSPACE-RESULT-KIND");
        Value result(rapidjson::kObjectType);
        const char* kind = value.kind == FileEntryKind::RegularFile ? "file"
            : value.kind == FileEntryKind::Directory                ? "directory"
                                                                    : "link";
        field(result, "kind", kind, allocator);
        require(value.sizeBytes <= INT64_MAX, "WORKSPACE-RESULT-SIZE");
        result.AddMember("size", value.sizeBytes, allocator);
        result.AddMember("version", version(value.version, allocator), allocator);
        return result;
    }

    static FileMetadata parseMetadata(const Value& node, bool directoryEntry = false)
    {
        require(directoryEntry
                ? core::json::jsonOnlyUniqueKeys(node, {"name", "kind", "size", "version"})
                : core::json::jsonOnlyUniqueKeys(node, {"kind", "size", "version"}));
        if (directoryEntry)
            (void)text(node, "name");
        const auto kind = text(node, "kind");
        require(kind == "file" || kind == "directory" || kind == "link", "WORKSPACE-RESULT-KIND");
        FileMetadata result;
        result.kind = kind == "file" ? FileEntryKind::RegularFile
            : kind == "directory"    ? FileEntryKind::Directory
                                     : FileEntryKind::Link;
        result.sizeBytes = number(node, "size");
        result.version = parseVersion(member(node, "version"));
        return result;
    }

private:
    inline static constexpr std::array<std::pair<FileIoStatus, const char*>, 19> statuses_{{
        {FileIoStatus::Accepted, "accepted"},
        {FileIoStatus::Success, "success"},
        {FileIoStatus::MissingFile, "missing"},
        {FileIoStatus::PermissionDenied, "permission-denied"},
        {FileIoStatus::InvalidPathScope, "invalid-scope"},
        {FileIoStatus::InvalidRange, "invalid-range"},
        {FileIoStatus::PartialReadDenied, "partial-denied"},
        {FileIoStatus::BackendUnavailable, "backend-unavailable"},
        {FileIoStatus::Cancelled, "cancelled"},
        {FileIoStatus::QueueFull, "queue-full"},
        {FileIoStatus::ServiceClosed, "closed"},
        {FileIoStatus::InvalidRequest, "invalid-request"},
        {FileIoStatus::BudgetExceeded, "budget-exceeded"},
        {FileIoStatus::Unsupported, "unsupported"},
        {FileIoStatus::VersionMismatch, "version-mismatch"},
        {FileIoStatus::IntegrityMismatch, "integrity-mismatch"},
        {FileIoStatus::Timeout, "timeout"},
        {FileIoStatus::StaleLocation, "stale-location"},
        {FileIoStatus::OutcomeUnknown, "outcome-unknown"},
    }};
    inline static constexpr std::array<std::pair<FileVersionKind, const char*>, 4> versions_{{
        {FileVersionKind::None, "none"},
        {FileVersionKind::ContentSha256, "content-sha256"},
        {FileVersionKind::MetadataObservation, "metadata"},
        {FileVersionKind::DirectorySnapshotSha256, "directory-sha256"},
    }};
};
} // namespace arp::core::io
