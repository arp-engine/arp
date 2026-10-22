#include "editor/remote/editor_remote_protocol.h"

#include "core/text/string_utils.h"
#include "editor/workspace/editor_shell.h"
#include "network/http/uri.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <rapidjson/document.h>

namespace arp::editor {
namespace {
constexpr std::size_t maxScriptBytes = 2u * 1024u * 1024u;

struct alignas(std::max_align_t) JsonAllocationHeader {
    std::size_t size = 0;
    std::pmr::memory_resource* resource = nullptr;
};

class CoreJsonAllocator final {
public:
    static constexpr bool kNeedFree = true;

    explicit CoreJsonAllocator(
        std::pmr::memory_resource* resource = &core::defaultCpuMemoryResource())
        : resource_(resource)
    {}

    CoreJsonAllocator(const CoreJsonAllocator&) = default;
    CoreJsonAllocator& operator=(const CoreJsonAllocator&) = default;

    void* Malloc(std::size_t size)
    {
        const auto payloadSize = std::max<std::size_t>(size, 1u);
        auto* header = static_cast<JsonAllocationHeader*>(resource_->allocate(
            sizeof(JsonAllocationHeader) + payloadSize, alignof(JsonAllocationHeader)));
        header->size = payloadSize;
        header->resource = resource_;
        return header + 1;
    }

    void* Realloc(void* original, std::size_t originalSize, std::size_t newSize)
    {
        if (!original)
            return Malloc(newSize);
        if (newSize == 0) {
            Free(original);
            return nullptr;
        }
        void* replacement = Malloc(newSize);
        std::memcpy(replacement, original, std::min(originalSize, newSize));
        Free(original);
        return replacement;
    }

    static void Free(void* ptr)
    {
        if (!ptr)
            return;
        auto* header = static_cast<JsonAllocationHeader*>(ptr) - 1;
        header->resource->deallocate(
            header, sizeof(JsonAllocationHeader) + header->size, alignof(JsonAllocationHeader));
    }

private:
    std::pmr::memory_resource* resource_;
};

using JsonDocument =
    rapidjson::GenericDocument<rapidjson::UTF8<>, CoreJsonAllocator, CoreJsonAllocator>;
using JsonValue = rapidjson::GenericValue<rapidjson::UTF8<>, CoreJsonAllocator>;

bool constantTimeEquals(std::string_view lhs, std::string_view rhs) noexcept
{
    std::uint32_t difference = static_cast<std::uint32_t>(lhs.size() ^ rhs.size());
    for (std::size_t i = 0; i < 256; ++i) {
        const auto left = i < lhs.size() ? static_cast<unsigned char>(lhs[i]) : 0u;
        const auto right = i < rhs.size() ? static_cast<unsigned char>(rhs[i]) : 0u;
        difference |= left ^ right;
    }
    return difference == 0;
}

std::string_view asView(const JsonValue& value)
{
    return value.IsString() ? std::string_view(value.GetString(), value.GetStringLength())
                            : std::string_view{};
}

const JsonValue* member(const JsonValue& object, const char* name)
{
    if (!object.IsObject())
        return nullptr;
    const auto it = object.FindMember(name);
    return it == object.MemberEnd() ? nullptr : &it->value;
}

bool unsignedValue(const JsonValue* value, std::uint64_t& out)
{
    if (!value || !value->IsUint64())
        return false;
    out = value->GetUint64();
    return true;
}

bool floatValue(const JsonValue* value, double& out)
{
    if (!value || !value->IsNumber())
        return false;
    out = value->GetDouble();
    return std::isfinite(out);
}

bool floatValue(const JsonValue* value, float& out)
{
    if (!value || !value->IsNumber())
        return false;
    const auto number = value->GetDouble();
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
        return false;
    out = static_cast<float>(number);
    return std::isfinite(out);
}

bool hasOnlyMembers(const JsonValue& object, std::initializer_list<std::string_view> allowed)
{
    if (!object.IsObject())
        return false;
    for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it) {
        const std::string_view name(it->name.GetString(), it->name.GetStringLength());
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end())
            return false;
    }
    return true;
}

bool isJsonContentType(const network::http::Request& request)
{
    auto value = request.header("Content-Type");
    constexpr std::string_view type = "application/json";
    if (value.size() < type.size()
        || !core::asciiEqualsIgnoreCase(value.substr(0, type.size()), type))
        return false;
    value.remove_prefix(type.size());
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    return value.empty() || value.front() == ';';
}

bool acceptsJson(const network::http::Request& request)
{
    auto accept = request.header("Accept");
    while (!accept.empty()) {
        const auto comma = accept.find(',');
        auto mediaType = accept.substr(0, comma);
        const auto parameters = mediaType.find(';');
        mediaType = mediaType.substr(0, parameters);
        while (!mediaType.empty() && (mediaType.front() == ' ' || mediaType.front() == '\t'))
            mediaType.remove_prefix(1);
        while (!mediaType.empty() && (mediaType.back() == ' ' || mediaType.back() == '\t'))
            mediaType.remove_suffix(1);
        if (core::asciiEqualsIgnoreCase(mediaType, "application/json")
            || core::asciiEqualsIgnoreCase(mediaType, "application/*")
            || core::asciiEqualsIgnoreCase(mediaType, "*/*"))
            return true;
        if (comma == std::string_view::npos)
            break;
        accept.remove_prefix(comma + 1);
    }
    return false;
}

void appendUnsigned(core::pmr::String& out, std::uint64_t value)
{
    std::array<char, 32> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data()));
}

void appendJsonString(core::pmr::String& out, std::string_view value);

void appendSigned(core::pmr::String& out, std::int64_t value)
{
    std::array<char, 32> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    out.append(buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data()));
}

bool appendJsonRpcId(core::pmr::String& out, const JsonValue* id)
{
    if (!id || id->IsNull()) {
        out += "null";
        return id == nullptr;
    }
    if (id->IsString()) {
        appendJsonString(out, asView(*id));
        return true;
    }
    if (id->IsUint64()) {
        appendUnsigned(out, id->GetUint64());
        return true;
    }
    if (id->IsInt64()) {
        appendSigned(out, id->GetInt64());
        return true;
    }
    if (id->IsDouble()) {
        std::array<char, 64> buffer{};
        const auto converted =
            std::to_chars(buffer.data(), buffer.data() + buffer.size(), id->GetDouble());
        if (converted.ec == std::errc{}) {
            out.append(buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data()));
            return true;
        }
    }
    out += "null";
    return false;
}

void appendMcpToolError(core::pmr::String& out, const JsonValue* id, std::string_view message)
{
    out += "{\"jsonrpc\":\"2.0\",\"id\":";
    appendJsonRpcId(out, id);
    out += ",\"result\":{\"isError\":true,\"content\":[{\"type\":\"text\",\"text\":";
    appendJsonString(out, message);
    out += "}]}}";
}

void appendJsonString(core::pmr::String& out, std::string_view value)
{
    static constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        switch (byte) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (byte < 0x20) {
                out += "\\u00";
                out.push_back(hex[byte >> 4]);
                out.push_back(hex[byte & 0x0f]);
            } else
                out.push_back(character);
        }
    }
    out.push_back('"');
}

const char* statusName(EditorResponseStatus status)
{
    switch (status) {
    case EditorResponseStatus::Completed:
        return "completed";
    case EditorResponseStatus::Rejected:
        return "rejected";
    case EditorResponseStatus::Failed:
        return "failed";
    }
    return "failed";
}

const char* sceneTaskStatusName(ScenePersistenceTaskStatus status)
{
    switch (status) {
    case ScenePersistenceTaskStatus::Pending:
        return "pending";
    case ScenePersistenceTaskStatus::Succeeded:
        return "succeeded";
    case ScenePersistenceTaskStatus::Failed:
        return "failed";
    case ScenePersistenceTaskStatus::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* diagnosticName(EditorDiagnosticCode diagnostic)
{
    switch (diagnostic) {
    case EditorDiagnosticCode::None:
        return "none";
    case EditorDiagnosticCode::UnsupportedCommandVersion:
        return "unsupported_command_version";
    case EditorDiagnosticCode::WrongOwnerThread:
        return "wrong_owner_thread";
    case EditorDiagnosticCode::StaleStateVersion:
        return "stale_state_version";
    case EditorDiagnosticCode::StaleDocumentTarget:
        return "stale_document_target";
    case EditorDiagnosticCode::UnsupportedIdempotency:
        return "unsupported_idempotency";
    case EditorDiagnosticCode::IdempotencyKeyConflict:
        return "idempotency_key_conflict";
    case EditorDiagnosticCode::IdempotencyCapacityExceeded:
        return "idempotency_capacity_exceeded";
    case EditorDiagnosticCode::InvalidCommandPayload:
        return "invalid_command_payload";
    }
    return "unknown";
}

void appendSnapshot(core::pmr::String& out, const EditorStateSnapshot& snapshot)
{
    out += "\"snapshot\":{\"workspace\":";
    appendJsonString(out, snapshot.workspace);
    out += ",\"scene\":";
    appendJsonString(out, snapshot.scene);
    out += ",\"selection\":";
    appendJsonString(out, snapshot.selection);
    out += ",\"hasActiveDocument\":";
    out += snapshot.hasActiveDocument ? "true" : "false";
    out += ",\"activeDocumentId\":";
    appendUnsigned(out, snapshot.activeDocumentId);
    out += ",\"openDocumentCount\":";
    appendUnsigned(out, snapshot.openDocumentCount);
    out += ",\"scriptDirty\":";
    out += snapshot.scriptDirty ? "true" : "false";
    out += ",\"scriptRelativePath\":";
    appendJsonString(out, snapshot.scriptRelativePath);
    out += ",\"scriptText\":";
    appendJsonString(out, snapshot.scriptText);
    out += ",\"scenePersistenceTask\":{\"taskId\":";
    appendUnsigned(out, snapshot.scenePersistenceTaskId);
    out += ",\"status\":";
    appendJsonString(out, sceneTaskStatusName(snapshot.scenePersistenceTaskStatus));
    out += ",\"diagnostic\":";
    appendJsonString(out, snapshot.scenePersistenceTaskDiagnostic);
    out += "}";
    out.push_back('}');
}

const char* propertyKindName(SceneInspectorPropertyKind kind)
{
    switch (kind) {
    case SceneInspectorPropertyKind::Unsupported:
        return "unsupported";
    case SceneInspectorPropertyKind::Boolean:
        return "boolean";
    case SceneInspectorPropertyKind::Integer:
        return "integer";
    case SceneInspectorPropertyKind::UnsignedInteger:
        return "unsignedInteger";
    case SceneInspectorPropertyKind::Number:
        return "number";
    case SceneInspectorPropertyKind::Enumeration:
        return "enumeration";
    case SceneInspectorPropertyKind::Vector2:
        return "vector2";
    case SceneInspectorPropertyKind::Vector2List:
        return "vector2List";
    case SceneInspectorPropertyKind::SceneObjectLinkList:
        return "sceneObjectLinkList";
    case SceneInspectorPropertyKind::Vector3:
        return "vector3";
    case SceneInspectorPropertyKind::Color4:
        return "color4";
    case SceneInspectorPropertyKind::String:
        return "string";
    case SceneInspectorPropertyKind::ResourceReference:
        return "resourceReference";
    case SceneInspectorPropertyKind::SceneObjectReference:
        return "sceneObjectReference";
    }
    return "unsupported";
}

void appendFloat(core::pmr::String& out, float value)
{
    std::array<char, 64> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (converted.ec == std::errc{})
        out.append(buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data()));
    else
        out += "null";
}

void appendSceneInspector(core::pmr::String& out, const EditorSceneInspectorSnapshot& snapshot)
{
    out += ",\"sceneInspector\":{\"objectId\":";
    appendUnsigned(out, snapshot.objectId);
    out += ",\"objectName\":";
    appendJsonString(out, snapshot.objectName);
    out += ",\"properties\":[";
    bool first = true;
    for (const auto& property : snapshot.properties) {
        if (!first)
            out.push_back(',');
        first = false;
        out += "{\"objectId\":";
        appendUnsigned(out, property.objectId);
        out += ",\"componentId\":";
        appendUnsigned(out, property.componentId);
        out += ",\"componentType\":";
        appendJsonString(out, property.componentType);
        out += ",\"path\":";
        appendJsonString(out, property.name);
        out += ",\"type\":";
        appendJsonString(out, property.typeName);
        out += ",\"kind\":";
        appendJsonString(out, propertyKindName(property.kind));
        out += ",\"editable\":";
        out += property.editable ? "true" : "false";
        out += ",\"value\":";
        switch (property.kind) {
        case SceneInspectorPropertyKind::Boolean:
            out += property.booleanValue ? "true" : "false";
            break;
        case SceneInspectorPropertyKind::Integer:
        case SceneInspectorPropertyKind::Enumeration:
            appendSigned(out, property.integerValue);
            break;
        case SceneInspectorPropertyKind::UnsignedInteger:
            appendUnsigned(out, property.unsignedIntegerValue);
            break;
        case SceneInspectorPropertyKind::SceneObjectReference:
            appendUnsigned(out, property.sceneObjectReferenceValue);
            break;
        case SceneInspectorPropertyKind::Number:
            appendFloat(out, property.numberValue);
            break;
        case SceneInspectorPropertyKind::Vector2:
            out += "{\"x\":";
            appendFloat(out, property.vector2Value.x);
            out += ",\"y\":";
            appendFloat(out, property.vector2Value.y);
            out.push_back('}');
            break;
        case SceneInspectorPropertyKind::Vector2List:
            out.push_back('[');
            for (std::size_t index = 0; index < property.vector2ListValue.size(); ++index) {
                if (index != 0u)
                    out.push_back(',');
                out += "{\"x\":";
                appendFloat(out, property.vector2ListValue[index].x);
                out += ",\"y\":";
                appendFloat(out, property.vector2ListValue[index].y);
                out.push_back('}');
            }
            out.push_back(']');
            break;
        case SceneInspectorPropertyKind::SceneObjectLinkList:
            out.push_back('[');
            for (std::size_t index = 0; index < property.sceneObjectLinkListValue.size(); ++index) {
                if (index != 0u)
                    out.push_back(',');
                const auto& link = property.sceneObjectLinkListValue[index];
                out += "{\"name\":";
                appendJsonString(out, link.name);
                out += ",\"target\":";
                appendUnsigned(out, link.targetObjectId);
                out.push_back('}');
            }
            out.push_back(']');
            break;
        case SceneInspectorPropertyKind::Vector3:
            out += "{\"x\":";
            appendFloat(out, property.vector3Value.x);
            out += ",\"y\":";
            appendFloat(out, property.vector3Value.y);
            out += ",\"z\":";
            appendFloat(out, property.vector3Value.z);
            out.push_back('}');
            break;
        case SceneInspectorPropertyKind::Color4:
            out += "{\"r\":";
            appendFloat(out, property.color4Value.x);
            out += ",\"g\":";
            appendFloat(out, property.color4Value.y);
            out += ",\"b\":";
            appendFloat(out, property.color4Value.z);
            out += ",\"a\":";
            appendFloat(out, property.color4Value.w);
            out.push_back('}');
            break;
        case SceneInspectorPropertyKind::String:
        case SceneInspectorPropertyKind::ResourceReference:
            appendJsonString(out, property.stringValue);
            break;
        case SceneInspectorPropertyKind::Unsupported:
            out += "null";
            break;
        }
        if (!property.resourceType.empty()) {
            out += ",\"resourceType\":";
            appendJsonString(out, property.resourceType);
            out += ",\"resourceOptional\":";
            out += property.resourceOptional ? "true" : "false";
        }
        if (!property.sceneObjectType.empty()) {
            out += ",\"sceneObjectType\":";
            appendJsonString(out, property.sceneObjectType);
            out += ",\"sceneObjectOptional\":";
            out += property.resourceOptional ? "true" : "false";
        }
        if (!property.resourceDiagnostic.empty()) {
            out += ",\"diagnostic\":";
            appendJsonString(out, property.resourceDiagnostic);
        }
        out += ",\"options\":[";
        for (std::size_t index = 0; index < property.enumOptions.size(); ++index) {
            if (index != 0)
                out.push_back(',');
            const auto& option = property.enumOptions[index];
            out += "{\"name\":";
            appendJsonString(out, option.name);
            out += ",\"value\":";
            appendSigned(out, option.value);
            if (!option.commandValue.empty()) {
                out += ",\"commandValue\":";
                appendJsonString(out, option.commandValue);
            }
            out.push_back('}');
        }
        out += "]}";
    }
    out += "]}";
}

void appendSceneHierarchy(core::pmr::String& out, const SceneHierarchySnapshot& snapshot)
{
    out += ",\"sceneHierarchy\":{\"selectedObjectId\":";
    appendUnsigned(out, snapshot.selectedObjectId);
    out += ",\"entries\":[";
    for (std::size_t index = 0; index < snapshot.entries.size(); ++index) {
        if (index != 0)
            out.push_back(',');
        const auto& entry = snapshot.entries[index];
        out += "{\"objectId\":";
        appendUnsigned(out, entry.objectId);
        out += ",\"parentId\":";
        appendUnsigned(out, entry.parentId);
        out += ",\"depth\":";
        appendUnsigned(out, entry.depth);
        out += ",\"childIndex\":";
        appendUnsigned(out, entry.childIndex);
        out += ",\"name\":";
        appendJsonString(out, entry.name);
        out.push_back('}');
    }
    out += "]}";
}

void appendSceneComponentCatalog(
    core::pmr::String& out, const SceneComponentCatalogSnapshot& snapshot)
{
    out += ",\"sceneComponentCatalog\":{\"entries\":[";
    for (std::size_t index = 0; index < snapshot.entries.size(); ++index) {
        if (index != 0)
            out.push_back(',');
        const auto& entry = snapshot.entries[index];
        out += "{\"typeName\":";
        appendJsonString(out, entry.typeName);
        out += ",\"displayName\":";
        appendJsonString(out, entry.displayName);
        out.push_back('}');
    }
    out += "]}";
}

core::pmr::String serializeResponse(const EditorResponse& response, bool includeSnapshot)
{
    core::pmr::String output(&core::defaultCpuMemoryResource());
    output += "{\"schemaVersion\":";
    appendUnsigned(output, response.schemaVersion);
    output += ",\"commandId\":";
    appendUnsigned(output, response.commandId);
    output += ",\"correlationId\":";
    appendUnsigned(output, response.correlationId);
    output += ",\"status\":";
    appendJsonString(output, statusName(response.status));
    output += ",\"diagnostic\":";
    appendJsonString(output, diagnosticName(response.diagnostic));
    output += ",\"hasStateVersion\":";
    output += response.hasStateVersion ? "true" : "false";
    output += ",\"stateVersion\":";
    appendUnsigned(output, response.stateVersion);
    if (includeSnapshot && response.hasSnapshot) {
        output.push_back(',');
        appendSnapshot(output, response.snapshot);
    }
    if (includeSnapshot && response.hasSceneInspector)
        appendSceneInspector(output, response.sceneInspector);
    if (includeSnapshot && response.hasSceneHierarchy)
        appendSceneHierarchy(output, response.sceneHierarchy);
    if (includeSnapshot && response.hasSceneComponentCatalog)
        appendSceneComponentCatalog(output, response.sceneComponentCatalog);
    output.push_back('}');
    return output;
}

std::uint64_t fingerprint(std::string_view command,
    std::string_view text,
    std::uint64_t documentId,
    bool hasExpectedStateVersion,
    std::uint64_t expectedStateVersion,
    const EditorCommandPayload* payload = nullptr)
{
    std::uint64_t hash = 1469598103934665603ull;
    const auto feed = [&hash](std::string_view data) {
        for (const unsigned char byte : data) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        hash ^= 0xff;
        hash *= 1099511628211ull;
    };
    feed(command);
    feed(text);
    std::array<char, 32> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), documentId);
    feed(std::string_view(buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data())));
    const char hasExpected = hasExpectedStateVersion ? 1 : 0;
    feed(std::string_view(&hasExpected, 1));
    const auto expected =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), expectedStateVersion);
    feed(std::string_view(buffer.data(), static_cast<std::size_t>(expected.ptr - buffer.data())));
    const auto* property =
        payload ? std::get_if<SetSceneComponentPropertyCommand>(payload) : nullptr;
    if (property) {
        feed(property->name);
        const auto feedUnsigned = [&feed](std::uint64_t value) {
            std::array<char, 32> textBuffer{};
            const auto result =
                std::to_chars(textBuffer.data(), textBuffer.data() + textBuffer.size(), value);
            feed(std::string_view(
                textBuffer.data(), static_cast<std::size_t>(result.ptr - textBuffer.data())));
        };
        const auto feedSigned = [&feed](std::int64_t value) {
            std::array<char, 32> textBuffer{};
            const auto result =
                std::to_chars(textBuffer.data(), textBuffer.data() + textBuffer.size(), value);
            feed(std::string_view(
                textBuffer.data(), static_cast<std::size_t>(result.ptr - textBuffer.data())));
        };
        const auto feedFloat = [&feed](float value) {
            std::array<char, 64> textBuffer{};
            const auto result =
                std::to_chars(textBuffer.data(), textBuffer.data() + textBuffer.size(), value);
            feed(std::string_view(
                textBuffer.data(), static_cast<std::size_t>(result.ptr - textBuffer.data())));
        };
        feedUnsigned(property->objectId);
        feedUnsigned(property->componentId);
        feedUnsigned(static_cast<std::uint8_t>(property->valueKind));
        switch (property->valueKind) {
        case ScenePropertyValueKind::Boolean:
            feed(property->booleanValue ? "true" : "false");
            break;
        case ScenePropertyValueKind::Integer:
        case ScenePropertyValueKind::Enumeration:
            feedSigned(property->integerValue);
            break;
        case ScenePropertyValueKind::UnsignedInteger:
            feedUnsigned(property->unsignedIntegerValue);
            break;
        case ScenePropertyValueKind::SceneObjectReference:
            feedUnsigned(property->sceneObjectReferenceValue);
            break;
        case ScenePropertyValueKind::Number:
            feedFloat(property->numberValue);
            break;
        case ScenePropertyValueKind::Vector2:
            feedFloat(property->vector2Value.x);
            feedFloat(property->vector2Value.y);
            break;
        case ScenePropertyValueKind::Vector2List:
            feedUnsigned(property->vector2ListValue.size());
            for (const auto& point : property->vector2ListValue) {
                feedFloat(point.x);
                feedFloat(point.y);
            }
            break;
        case ScenePropertyValueKind::SceneObjectLinkList:
            feedUnsigned(property->sceneObjectLinkListValue.size());
            for (const auto& link : property->sceneObjectLinkListValue) {
                feedUnsigned(link.name.size());
                feed(link.name);
                feedUnsigned(link.targetObjectId);
            }
            break;
        case ScenePropertyValueKind::Vector3:
            feedFloat(property->vector3Value.x);
            feedFloat(property->vector3Value.y);
            feedFloat(property->vector3Value.z);
            break;
        case ScenePropertyValueKind::Color4:
            feedFloat(property->color4Value.x);
            feedFloat(property->color4Value.y);
            feedFloat(property->color4Value.z);
            feedFloat(property->color4Value.w);
            break;
        case ScenePropertyValueKind::String:
        case ScenePropertyValueKind::ResourceReference:
            feed(property->stringValue);
            break;
        }
    }
    if (const auto* reparent =
            payload ? std::get_if<ReparentSceneObjectCommand>(payload) : nullptr) {
        const auto feedUnsigned = [&feed](std::uint64_t value) {
            std::array<char, 32> textBuffer{};
            const auto result =
                std::to_chars(textBuffer.data(), textBuffer.data() + textBuffer.size(), value);
            feed(std::string_view(
                textBuffer.data(), static_cast<std::size_t>(result.ptr - textBuffer.data())));
        };
        feedUnsigned(reparent->objectId);
        feedUnsigned(reparent->parentId);
        feedUnsigned(reparent->childIndex);
    }
    if (const auto* add = payload ? std::get_if<AddSceneComponentCommand>(payload) : nullptr) {
        feed(add->type);
        std::array<char, 32> textBuffer{};
        const auto result =
            std::to_chars(textBuffer.data(), textBuffer.data() + textBuffer.size(), add->objectId);
        feed(std::string_view(
            textBuffer.data(), static_cast<std::size_t>(result.ptr - textBuffer.data())));
        for (const auto& initial : add->initialProperties) {
            EditorCommandPayload propertyPayload = initial;
            const auto propertyHash =
                fingerprint("scene.component.property.set", {}, 0, false, 0, &propertyPayload);
            const auto propertyResult = std::to_chars(
                textBuffer.data(), textBuffer.data() + textBuffer.size(), propertyHash, 16);
            feed(std::string_view(textBuffer.data(),
                static_cast<std::size_t>(propertyResult.ptr - textBuffer.data())));
        }
    }
    if (const auto* remove =
            payload ? std::get_if<RemoveSceneComponentCommand>(payload) : nullptr) {
        std::array<char, 32> textBuffer{};
        const auto feedUnsigned = [&feed, &textBuffer](std::uint64_t value) {
            const auto result =
                std::to_chars(textBuffer.data(), textBuffer.data() + textBuffer.size(), value);
            feed(std::string_view(
                textBuffer.data(), static_cast<std::size_t>(result.ptr - textBuffer.data())));
        };
        feedUnsigned(remove->objectId);
        feedUnsigned(remove->componentId);
    }
    return hash;
}

bool allowedRemoteActivation(EditorShell& shell, std::string_view source)
{
    if (source == "workspace_Game" || source == "workspace_Script" || source == "dock_Inspector"
        || source == "dock_Signals" || source == "dock_Groups" || source == "scene_level"
        || source == "scene_player" || source == "scene_menu")
        return true;
    if (source.size() <= 5 || source.substr(0, 5) != "node_")
        return false;
    core::pmr::String sourceId(source, &core::defaultCpuMemoryResource());
    return shell.id(sourceId).valid();
}

bool parseSceneProperty(
    const JsonValue& arguments, SetSceneComponentPropertyCommand& command, bool requireExactMembers)
{
    const auto* property = member(arguments, "property");
    const auto* kind = member(arguments, "kind");
    const auto* value = member(arguments, "value");
    if ((requireExactMembers && !hasOnlyMembers(arguments, {"property", "kind", "value"}))
        || !property || !property->IsString() || property->GetStringLength() == 0
        || property->GetStringLength() > 256 || !kind || !kind->IsString() || !value)
        return false;
    command.name.assign(property->GetString(), property->GetStringLength());
    const auto kindName = asView(*kind);
    if (kindName == "boolean" && value->IsBool()) {
        command.valueKind = ScenePropertyValueKind::Boolean;
        command.booleanValue = value->GetBool();
    } else if (kindName == "integer" && value->IsInt()) {
        command.valueKind = ScenePropertyValueKind::Integer;
        command.integerValue = value->GetInt();
    } else if (kindName == "unsignedInteger" && value->IsUint64()) {
        command.valueKind = ScenePropertyValueKind::UnsignedInteger;
        command.unsignedIntegerValue = value->GetUint64();
    } else if (kindName == "number" && floatValue(value, command.numberValue)) {
        command.valueKind = ScenePropertyValueKind::Number;
    } else if (kindName == "enumeration" && value->IsInt()) {
        command.valueKind = ScenePropertyValueKind::Enumeration;
        command.integerValue = value->GetInt();
    } else if (kindName == "vector2" && hasOnlyMembers(*value, {"x", "y"})
        && floatValue(member(*value, "x"), command.vector2Value.x)
        && floatValue(member(*value, "y"), command.vector2Value.y)) {
        command.valueKind = ScenePropertyValueKind::Vector2;
    } else if (kindName == "vector2List" && value->IsArray()
        && value->Size() <= kMaxSceneInspectorVector2ListElements) {
        command.valueKind = ScenePropertyValueKind::Vector2List;
        for (const auto& entry : value->GetArray()) {
            core::Vec2 point;
            if (!hasOnlyMembers(entry, {"x", "y"}) || !floatValue(member(entry, "x"), point.x)
                || !floatValue(member(entry, "y"), point.y))
                return false;
            command.vector2ListValue.push_back(point);
        }
    } else if (kindName == "sceneObjectLinkList" && value->IsArray()
        && value->Size() <= kMaxSceneInspectorObjectLinks) {
        command.valueKind = ScenePropertyValueKind::SceneObjectLinkList;
        for (const auto& entry : value->GetArray()) {
            const auto* name = member(entry, "name");
            const auto* target = member(entry, "target");
            if (!entry.IsObject() || !hasOnlyMembers(entry, {"name", "target"}) || !name
                || !name->IsString() || name->GetStringLength() == 0
                || name->GetStringLength() > 128 || !target || !target->IsUint64()
                || target->GetUint64() == 0u)
                return false;
            command.sceneObjectLinkListValue.emplace_back();
            auto& link = command.sceneObjectLinkListValue.back();
            link.name.assign(name->GetString(), name->GetStringLength());
            link.targetObjectId = target->GetUint64();
        }
    } else if (kindName == "vector3" && hasOnlyMembers(*value, {"x", "y", "z"})
        && floatValue(member(*value, "x"), command.vector3Value.x)
        && floatValue(member(*value, "y"), command.vector3Value.y)
        && floatValue(member(*value, "z"), command.vector3Value.z)) {
        command.valueKind = ScenePropertyValueKind::Vector3;
    } else if (kindName == "color4" && hasOnlyMembers(*value, {"r", "g", "b", "a"})
        && floatValue(member(*value, "r"), command.color4Value.x)
        && floatValue(member(*value, "g"), command.color4Value.y)
        && floatValue(member(*value, "b"), command.color4Value.z)
        && floatValue(member(*value, "a"), command.color4Value.w)) {
        command.valueKind = ScenePropertyValueKind::Color4;
    } else if (kindName == "sceneObjectReference" && value->IsUint64()) {
        command.valueKind = ScenePropertyValueKind::SceneObjectReference;
        command.sceneObjectReferenceValue = value->GetUint64();
    } else if ((kindName == "string" || kindName == "resourceReference") && value->IsString()
        && value->GetStringLength() <= 4096) {
        command.valueKind = kindName == "string" ? ScenePropertyValueKind::String
                                                 : ScenePropertyValueKind::ResourceReference;
        command.stringValue.assign(value->GetString(), value->GetStringLength());
    } else {
        return false;
    }
    return true;
}

bool makeCommand(std::string_view name,
    const JsonValue& arguments,
    EditorShell& shell,
    EditorCommandPayload& payload,
    std::uint64_t& documentId,
    std::string_view& text)
{
    if (name == "editor.state.get") {
        payload = GetEditorStateQuery{};
        return true;
    }
    if (name == "scene.inspector.get") {
        std::uint64_t objectId = 0;
        if (const auto* value = member(arguments, "objectId");
            value && !unsignedValue(value, objectId))
            return false;
        payload = GetSceneInspectorQuery{objectId};
        return true;
    }
    if (name == "scene.hierarchy.get") {
        payload = GetSceneHierarchyQuery{};
        return true;
    }
    if (name == "scene.component.catalog.get") {
        payload = GetSceneComponentCatalogQuery{};
        return true;
    }
    if (name == "scene.component.add") {
        std::uint64_t objectId = 0;
        const auto* type = member(arguments, "type");
        if (!unsignedValue(member(arguments, "objectId"), objectId) || objectId == 0 || !type
            || !type->IsString() || type->GetStringLength() == 0 || type->GetStringLength() > 128)
            return false;
        AddSceneComponentCommand command(asView(*type));
        command.objectId = objectId;
        if (const auto* properties = member(arguments, "properties")) {
            if (!properties->IsArray() || properties->Size() > 64u)
                return false;
            for (const auto& entry : properties->GetArray()) {
                SetSceneComponentPropertyCommand property("initial");
                if (!entry.IsObject() || !parseSceneProperty(entry, property, true))
                    return false;
                command.initialProperties.push_back(std::move(property));
            }
        }
        payload = std::move(command);
        return true;
    }
    if (name == "scene.component.remove") {
        std::uint64_t objectId = 0;
        std::uint64_t componentId = 0;
        if (!unsignedValue(member(arguments, "objectId"), objectId) || objectId == 0
            || !unsignedValue(member(arguments, "componentId"), componentId) || componentId == 0)
            return false;
        payload = RemoveSceneComponentCommand{objectId, componentId};
        return true;
    }
    if (name == "scene.component.property.set") {
        std::uint64_t objectId = 0;
        std::uint64_t componentId = 0;
        const auto* object = member(arguments, "objectId");
        const auto* component = member(arguments, "componentId");
        const auto* property = member(arguments, "property");
        if (!unsignedValue(object, objectId) || objectId == 0
            || !unsignedValue(component, componentId) || componentId == 0 || !property
            || !property->IsString() || property->GetStringLength() == 0
            || property->GetStringLength() > 256)
            return false;
        SetSceneComponentPropertyCommand command(asView(*property));
        command.objectId = objectId;
        command.componentId = componentId;
        if (!parseSceneProperty(arguments, command, false))
            return false;
        payload = std::move(command);
        return true;
    }
    if (name == "editor.activate") {
        const auto* source = member(arguments, "source");
        if (!source || !source->IsString() || !allowedRemoteActivation(shell, asView(*source)))
            return false;
        payload = ActivateControlCommand(asView(*source));
        return true;
    }
    if (name == "editor.search") {
        const auto* source = member(arguments, "source");
        const auto* value = member(arguments, "value");
        if (!source || !source->IsString() || !value || !value->IsString()
            || value->GetStringLength() > 512)
            return false;
        const auto sourceText = asView(*source);
        if (sourceText != "scene_search" && sourceText != "file_search"
            && sourceText != "property_search")
            return false;
        payload = SetTextValueCommand(sourceText, asView(*value));
        return true;
    }
    if (name == "scene.reparent") {
        std::uint64_t objectId = 0u, parentId = 0u, childIndex = 0u;
        if (!unsignedValue(member(arguments, "objectId"), objectId) || objectId == 0u
            || !unsignedValue(member(arguments, "parentId"), parentId) || parentId == 0u
            || !unsignedValue(member(arguments, "childIndex"), childIndex)
            || childIndex > std::numeric_limits<std::uint32_t>::max())
            return false;
        payload =
            ReparentSceneObjectCommand{objectId, parentId, static_cast<std::uint32_t>(childIndex)};
        return true;
    }
    if (name == "scene.undo") {
        payload = UndoSceneCommand{};
        return true;
    }
    if (name == "scene.redo") {
        payload = RedoSceneCommand{};
        return true;
    }
    if (name == "script.replace" || name == "script.save" || name == "script.undo"
        || name == "script.redo") {
        if (!unsignedValue(member(arguments, "documentId"), documentId) || documentId == 0)
            return false;
        if (name == "script.replace") {
            const auto* textValue = member(arguments, "text");
            if (!textValue || !textValue->IsString()
                || textValue->GetStringLength() > maxScriptBytes)
                return false;
            text = asView(*textValue);
            payload = ReplaceScriptTextCommand(documentId, text);
        } else if (name == "script.save")
            payload = SaveScriptCommand{documentId};
        else if (name == "script.undo")
            payload = UndoScriptCommand{documentId};
        else
            payload = RedoScriptCommand{documentId};
        return true;
    }
    if (name == "scene.save" || name == "scene.load") {
        const auto* path = member(arguments, "path");
        if (!path || !path->IsString() || path->GetStringLength() == 0
            || path->GetStringLength() > 512)
            return false;
        text = asView(*path);
        payload = name == "scene.save" ? EditorCommandPayload{SaveSceneCommand(text)}
                                       : EditorCommandPayload{LoadSceneCommand(text)};
        return true;
    }
    if (name == "scene.task.get") {
        if (!unsignedValue(member(arguments, "taskId"), documentId) || documentId == 0)
            return false;
        payload = GetScenePersistenceTaskQuery{documentId};
        return true;
    }
    return false;
}

network::http::Response jsonResponse(std::uint16_t status, std::string_view body)
{
    network::http::Response response;
    response.set_status(network::http::StatusCode(status));
    response.set_reason(status == 200 ? "OK"
            : status == 202           ? "Accepted"
            : status == 400           ? "Bad Request"
            : status == 401           ? "Unauthorized"
            : status == 404           ? "Not Found"
            : status == 405           ? "Method Not Allowed"
            : status == 406           ? "Not Acceptable"
            : status == 413           ? "Content Too Large"
            : status == 415           ? "Unsupported Media Type"
            : status == 409           ? "Conflict"
                                      : "Internal Server Error");
    response.set_content_type(network::http::ContentType::Json);
    response.set_body_text(body);
    return response;
}

network::http::Response protocolError(std::uint16_t status, std::string_view message)
{
    core::pmr::String body(&core::defaultCpuMemoryResource());
    body += "{\"error\":";
    appendJsonString(body, message);
    body.push_back('}');
    return jsonResponse(status, body);
}
} // namespace

EditorRemoteProtocol::EditorRemoteProtocol(std::string_view bearerToken)
    : bearerToken_(bearerToken, &core::defaultCpuMemoryResource())
{}

bool EditorRemoteProtocol::authorized(const network::http::Request& request) const noexcept
{
    constexpr std::string_view prefix = "Bearer ";
    const auto authorization = request.header("Authorization");
    if (authorization.size() <= prefix.size() || authorization.substr(0, prefix.size()) != prefix)
        return false;
    return constantTimeEquals(authorization.substr(prefix.size()), bearerToken_);
}

network::http::Response EditorRemoteProtocol::handle(
    const network::http::Request& request, EditorShell& shell)
{
    if (request.body().size() > maxRequestBytes)
        return protocolError(413, "request_too_large");
    if (!authorized(request))
        return protocolError(401, "unauthorized");

    const auto target = request.target();
    const auto pathEnd = target.find('?');
    const auto path = target.substr(0, pathEnd);
    if (path == "/api/v1/discovery" && request.method() == network::http::Method::Get)
        return discovery(shell);
    if (request.method() == network::http::Method::Options)
        return protocolError(405, "cors_not_supported");
    if (path == "/api/v1/commands" && request.method() == network::http::Method::Post) {
        if (!isJsonContentType(request))
            return protocolError(415, "application_json_required");
        return restCommand(request, shell);
    }
    if (path == "/mcp") {
        if (request.method() != network::http::Method::Post)
            return protocolError(405, "mcp_method_not_supported");
        if (!isJsonContentType(request))
            return protocolError(415, "application_json_required");
        if (!acceptsJson(request))
            return protocolError(406, "application_json_accept_required");
        return mcpMessage(request, shell);
    }
    return protocolError(404, "route_not_found");
}

network::http::Response EditorRemoteProtocol::discovery(EditorShell& shell)
{
    core::pmr::String body(&core::defaultCpuMemoryResource());
    body += "{\"schemaVersion\":1,\"stateVersion\":";
    appendUnsigned(body, shell.stateVersion());
    body +=
        ",\"capabilities\":[\"editor.state.get\",\"editor.activate\",\"editor.search\",\"scene.inspector.get\",\"scene.hierarchy.get\",\"scene.component.catalog.get\",\"scene.component.add\",\"scene.component.remove\",\"scene.component.property.set\",\"scene.undo\",\"scene.redo\",\"scene.reparent\",\"scene.save\",\"scene.load\",\"scene.task.get\",\"script.replace\",\"script.save\",\"script.undo\",\"script.redo\"]}";
    return jsonResponse(200, body);
}

network::http::Response EditorRemoteProtocol::restCommand(
    const network::http::Request& request, EditorShell& shell)
{
    const auto source = request.body_text();
    core::pmr::String body(source.data(), source.size(), &core::defaultCpuMemoryResource());
    body.push_back('\0');
    CoreJsonAllocator allocator(&core::defaultCpuMemoryResource());
    JsonDocument document(&allocator, 256, &allocator);
    document.Parse(body.data());
    if (document.HasParseError() || !document.IsObject())
        return protocolError(400, "invalid_json_envelope");

    const auto* version = member(document, "schemaVersion");
    const auto* name = member(document, "command");
    if (!version || !version->IsUint()
        || version->GetUint() != EditorCommandEnvelope::currentVersion)
        return protocolError(400, "unsupported_schema_version");
    if (!hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "documentId",
                "text",
                "path",
                "taskId",
                "source",
                "value",
                "objectId",
                "parentId",
                "childIndex",
                "componentId",
                "type",
                "properties",
                "property",
                "kind"}))
        return protocolError(400, "unknown_envelope_field");
    if (!name || !name->IsString())
        return protocolError(400, "missing_command");
    const auto commandName = asView(*name);
    if (commandName == "scene.inspector.get"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "objectId"}))
        return protocolError(400, "invalid_command_fields");
    if ((commandName == "scene.hierarchy.get" || commandName == "scene.component.catalog.get")
        && !hasOnlyMembers(document,
            {"schemaVersion", "command", "commandId", "correlationId", "expectedStateVersion"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.reparent"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "objectId",
                "parentId",
                "childIndex"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.component.add"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "objectId",
                "type",
                "properties"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.component.remove"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "objectId",
                "componentId"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.reparent"
        && (member(document, "parentId") || member(document, "childIndex")))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.component.property.set"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "objectId",
                "componentId",
                "property",
                "kind",
                "value"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.inspector.get" && commandName != "scene.component.property.set"
        && commandName != "scene.hierarchy.get" && commandName != "scene.component.catalog.get"
        && commandName != "scene.reparent" && commandName != "scene.component.add"
        && commandName != "scene.component.remove"
        && (member(document, "objectId") || member(document, "componentId")
            || member(document, "property") || member(document, "kind")))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.save" && commandName != "scene.load"
        && commandName != "scene.task.get"
        && (member(document, "path") || member(document, "taskId")))
        return protocolError(400, "invalid_command_fields");
    if ((commandName == "editor.state.get"
            && (member(document, "documentId") || member(document, "text")
                || member(document, "source") || member(document, "value")))
        || (commandName == "editor.activate"
            && (member(document, "documentId") || member(document, "text")
                || member(document, "value")))
        || (commandName == "editor.search"
            && (member(document, "documentId") || member(document, "text")))
        || ((commandName == "scene.undo" || commandName == "scene.redo")
            && (member(document, "documentId") || member(document, "text")
                || member(document, "source") || member(document, "value")))
        || ((commandName == "scene.save" || commandName == "scene.load")
            && (member(document, "documentId") || member(document, "taskId")
                || member(document, "text") || member(document, "source")
                || member(document, "value")))
        || (commandName == "scene.task.get"
            && (member(document, "documentId") || member(document, "path")
                || member(document, "text") || member(document, "source")
                || member(document, "value")))
        || ((commandName == "script.save" || commandName == "script.undo"
                || commandName == "script.redo")
            && (member(document, "text") || member(document, "source")
                || member(document, "value")))
        || (commandName == "script.replace"
            && (member(document, "source") || member(document, "value")))
        || (commandName == "scene.inspector.get"
            && (member(document, "componentId") || member(document, "property")
                || member(document, "kind") || member(document, "value")))
        || (commandName == "scene.component.add"
            && (member(document, "componentId") || member(document, "property")
                || member(document, "kind") || member(document, "value")))
        || (commandName == "scene.component.remove"
            && (member(document, "type") || member(document, "property") || member(document, "kind")
                || member(document, "value")))
        || (commandName == "scene.component.property.set"
            && (member(document, "documentId") || member(document, "text")
                || member(document, "path") || member(document, "taskId")
                || member(document, "source"))))
        return protocolError(400, "invalid_command_fields");

    std::uint64_t correlationId = 0, commandId = 0, expectedStateVersion = 0;
    if (const auto* value = member(document, "correlationId");
        value && !unsignedValue(value, correlationId))
        return protocolError(400, "invalid_correlation_id");
    if (const auto* value = member(document, "commandId");
        value && !unsignedValue(value, commandId))
        return protocolError(400, "invalid_command_id");
    const auto* expected = member(document, "expectedStateVersion");
    if (expected && !unsignedValue(expected, expectedStateVersion))
        return protocolError(400, "invalid_expected_state_version");

    std::uint64_t documentId = 0;
    std::string_view text;
    EditorCommandPayload payload = GetEditorStateQuery{};
    if (!makeCommand(asView(*name), document, shell, payload, documentId, text))
        return protocolError(400, "invalid_or_unsupported_command");

    EditorCommandEnvelope envelope(std::move(payload));
    envelope.origin = EditorCommandOrigin::HttpRest;
    envelope.commandId = commandId;
    envelope.correlationId = correlationId ? correlationId : nextCorrelationId_++;
    envelope.sourceSequence = nextSourceSequence_++;
    envelope.hasExpectedStateVersion = expected != nullptr;
    envelope.expectedStateVersion = expectedStateVersion;
    if (const auto* key = member(document, "idempotencyKey")) {
        if (!key->IsString() || key->GetStringLength() == 0 || key->GetStringLength() > 128)
            return protocolError(400, "invalid_idempotency_key");
        envelope.idempotencyKey.assign(key->GetString(), key->GetStringLength());
        const auto hash = fingerprint(asView(*name),
            text,
            documentId,
            expected != nullptr,
            expectedStateVersion,
            &envelope.payload);
        std::array<char, 32> encoded{};
        const auto result =
            std::to_chars(encoded.data(), encoded.data() + encoded.size(), hash, 16);
        envelope.idempotencyFingerprint.assign(encoded.data(), result.ptr);
    }

    const auto response = shell.submitCommand(std::move(envelope));
    const auto status = response.diagnostic == EditorDiagnosticCode::StaleStateVersion
            || response.diagnostic == EditorDiagnosticCode::StaleDocumentTarget
            || response.diagnostic == EditorDiagnosticCode::IdempotencyKeyConflict
        ? 409
        : response.status == EditorResponseStatus::Completed ? 200
        : response.status == EditorResponseStatus::Rejected  ? 400
                                                             : 500;
    const auto serialized = serializeResponse(response, true);
    return jsonResponse(static_cast<std::uint16_t>(status), serialized);
}

network::http::Response EditorRemoteProtocol::mcpMessage(
    const network::http::Request& request, EditorShell& shell)
{
    const auto source = request.body_text();
    core::pmr::String body(source.data(), source.size(), &core::defaultCpuMemoryResource());
    body.push_back('\0');
    CoreJsonAllocator allocator(&core::defaultCpuMemoryResource());
    JsonDocument document(&allocator, 256, &allocator);
    document.Parse(body.data());
    if (document.HasParseError() || !document.IsObject())
        return protocolError(400, "invalid_json_rpc_message");
    const auto* jsonrpc = member(document, "jsonrpc");
    const auto* methodValue = member(document, "method");
    if (!jsonrpc || asView(*jsonrpc) != "2.0" || !methodValue || !methodValue->IsString())
        return protocolError(400, "invalid_json_rpc_envelope");
    const auto method = asView(*methodValue);
    const auto* id = member(document, "id");
    if (method != "initialize" && request.header("MCP-Protocol-Version") != mcpProtocolVersion)
        return protocolError(400, "missing_or_unsupported_mcp_protocol_version");
    if (method == "notifications/initialized")
        return jsonResponse(202, {});

    core::pmr::String output(&core::defaultCpuMemoryResource());
    if (method == "initialize") {
        const auto* params = member(document, "params");
        const auto* version = params ? member(*params, "protocolVersion") : nullptr;
        if (!version || asView(*version) != mcpProtocolVersion)
            return protocolError(400, "unsupported_mcp_protocol_version");
        output += "{\"jsonrpc\":\"2.0\",\"id\":";
        appendJsonRpcId(output, id);
        output += ",\"result\":{\"protocolVersion\":";
        appendJsonString(output, mcpProtocolVersion);
        output +=
            ",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"arp-editor\",\"version\":\"1\"}}}";
    } else if (method == "ping") {
        output += "{\"jsonrpc\":\"2.0\",\"id\":";
        appendJsonRpcId(output, id);
        output += ",\"result\":{}}";
    } else if (method == "tools/list") {
        output += "{\"jsonrpc\":\"2.0\",\"id\":";
        appendJsonRpcId(output, id);
        output += R"json(,"result":{"tools":[
{"name":"editor.state.get","description":"Read current editor and active script state.","inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
{"name":"scene.inspector.get","description":"Read typed Inspector properties and stable object/component IDs for a scene object (defaults to selection).","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1}},"additionalProperties":false}},
{"name":"scene.hierarchy.get","description":"Read the scene hierarchy with stable object and parent IDs, depth, sibling order and selected object.","inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
{"name":"scene.component.catalog.get","description":"Read the explicit allowlisted component factory catalog.","inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
{"name":"scene.component.add","description":"Add one allowlisted component, optionally applying typed initial Inspector properties atomically.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"type":{"type":"string","minLength":1,"maxLength":128},"properties":{"type":"array","maxItems":64,"items":{"type":"object","properties":{"property":{"type":"string","minLength":1,"maxLength":256},"kind":{"type":"string","enum":["boolean","integer","unsignedInteger","number","enumeration","vector2","vector2List","vector3","color4","string","resourceReference","sceneObjectReference"]},"value":{}},"required":["property","kind","value"],"additionalProperties":false}},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","type"],"additionalProperties":false}},
{"name":"scene.component.remove","description":"Remove one non-Transform component by stable object and component IDs.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"componentId":{"type":"integer","minimum":1},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","componentId"],"additionalProperties":false}},
{"name":"scene.component.property.set","description":"Set one typed scene component Inspector property through the editor command history.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"componentId":{"type":"integer","minimum":1},"property":{"type":"string","minLength":1,"maxLength":256},"kind":{"type":"string","enum":["boolean","integer","unsignedInteger","number","enumeration","vector2","vector2List","vector3","color4","string","resourceReference","sceneObjectReference"]},"value":{},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","componentId","property","kind","value"],"additionalProperties":false}},
{"name":"scene.reparent","description":"Move an object to a parent at a zero-based child index through the scene command history.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"parentId":{"type":"integer","minimum":1},"childIndex":{"type":"integer","minimum":0},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","parentId","childIndex"],"additionalProperties":false}},
{"name":"editor.activate","description":"Activate an allowlisted editor workspace, dock, scene tab or node.","inputSchema":{"type":"object","properties":{"source":{"type":"string"}},"required":["source"],"additionalProperties":false}},
{"name":"editor.search","description":"Set a bounded query on an editor search field.","inputSchema":{"type":"object","properties":{"source":{"type":"string","enum":["scene_search","file_search","property_search"]},"value":{"type":"string","maxLength":512}},"required":["source","value"],"additionalProperties":false}},
{"name":"script.replace","description":"Replace full text in the targeted open script.","inputSchema":{"type":"object","properties":{"documentId":{"type":"integer","minimum":1},"text":{"type":"string"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["documentId","text"],"additionalProperties":false}},
{"name":"script.save","description":"Save the targeted open script.","inputSchema":{"type":"object","properties":{"documentId":{"type":"integer","minimum":1},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["documentId"],"additionalProperties":false}},
{"name":"script.undo","description":"Undo the latest edit in the targeted open script.","inputSchema":{"type":"object","properties":{"documentId":{"type":"integer","minimum":1},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["documentId"],"additionalProperties":false}},
{"name":"script.redo","description":"Redo the latest undone edit in the targeted open script.","inputSchema":{"type":"object","properties":{"documentId":{"type":"integer","minimum":1},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["documentId"],"additionalProperties":false}},
{"name":"scene.undo","description":"Undo the latest supported scene component, property, transform or hierarchy edit.","inputSchema":{"type":"object","properties":{"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"scene.redo","description":"Redo the latest undone supported scene component, property, transform or hierarchy edit.","inputSchema":{"type":"object","properties":{"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"scene.save","description":"Save the current authoring scene to a project-relative .arp.yaml path. Returns a taskId.","inputSchema":{"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":512},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["path"],"additionalProperties":false}},
{"name":"scene.load","description":"Load and validate a project-relative authoring scene. Returns a taskId.","inputSchema":{"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":512},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["path"],"additionalProperties":false}},
{"name":"scene.task.get","description":"Read the status and diagnostic of a scene save/load task.","inputSchema":{"type":"object","properties":{"taskId":{"type":"integer","minimum":1},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["taskId"],"additionalProperties":false}}
]}})json";
    } else if (method == "tools/call") {
        core::pmr::String serializedId(&core::defaultCpuMemoryResource());
        if (!id || !appendJsonRpcId(serializedId, id))
            return protocolError(400, "tools_call_requires_numeric_id");
        const auto* params = member(document, "params");
        const auto* name = params ? member(*params, "name") : nullptr;
        const auto* arguments = params ? member(*params, "arguments") : nullptr;
        if (!name || !name->IsString() || !arguments || !arguments->IsObject()) {
            appendMcpToolError(output, id, "invalid_tools_call");
            auto response = jsonResponse(200, output);
            response.add_header("MCP-Protocol-Version", mcpProtocolVersion);
            return response;
        }
        const auto toolName = asView(*name);
        const bool validArguments = toolName == "editor.state.get" ? hasOnlyMembers(*arguments, {})
            : toolName == "scene.inspector.get"
            ? hasOnlyMembers(*arguments, {"objectId", "expectedStateVersion"})
            : (toolName == "scene.hierarchy.get" || toolName == "scene.component.catalog.get")
            ? hasOnlyMembers(*arguments, {"expectedStateVersion"})
            : toolName == "scene.component.add"
            ? hasOnlyMembers(*arguments,
                  {"objectId", "type", "properties", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.component.remove"
            ? hasOnlyMembers(
                  *arguments, {"objectId", "componentId", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.reparent"
            ? hasOnlyMembers(*arguments,
                  {"objectId", "parentId", "childIndex", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.component.property.set" ? hasOnlyMembers(*arguments,
                                                               {"objectId",
                                                                   "componentId",
                                                                   "property",
                                                                   "kind",
                                                                   "value",
                                                                   "idempotencyKey",
                                                                   "expectedStateVersion"})
            : toolName == "editor.activate"              ? hasOnlyMembers(*arguments, {"source"})
            : toolName == "editor.search" ? hasOnlyMembers(*arguments, {"source", "value"})
            : (toolName == "scene.save" || toolName == "scene.load")
            ? hasOnlyMembers(*arguments, {"path", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.task.get"
            ? hasOnlyMembers(*arguments, {"taskId", "expectedStateVersion"})
            : toolName == "script.replace"
            ? hasOnlyMembers(
                  *arguments, {"documentId", "text", "idempotencyKey", "expectedStateVersion"})
            : hasOnlyMembers(*arguments, {"documentId", "idempotencyKey", "expectedStateVersion"});
        if (!validArguments) {
            appendMcpToolError(output, id, "unknown_tool_argument");
            auto response = jsonResponse(200, output);
            response.add_header("MCP-Protocol-Version", mcpProtocolVersion);
            return response;
        }
        std::uint64_t documentId = 0;
        std::string_view text;
        EditorCommandPayload payload = GetEditorStateQuery{};
        if (!makeCommand(asView(*name), *arguments, shell, payload, documentId, text)) {
            appendMcpToolError(output, id, "invalid_or_unsupported_command");
            auto response = jsonResponse(200, output);
            response.add_header("MCP-Protocol-Version", mcpProtocolVersion);
            return response;
        }
        EditorCommandEnvelope envelope(std::move(payload));
        envelope.origin = EditorCommandOrigin::Mcp;
        envelope.correlationId = id->IsUint64() ? id->GetUint64() : nextCorrelationId_++;
        envelope.sourceSequence = nextSourceSequence_++;
        std::uint64_t expectedStateVersion = 0;
        if (const auto* expected = member(*arguments, "expectedStateVersion")) {
            if (!unsignedValue(expected, expectedStateVersion))
                return protocolError(400, "invalid_expected_state_version");
            envelope.hasExpectedStateVersion = true;
            envelope.expectedStateVersion = expectedStateVersion;
        }
        if (const auto* key = member(*arguments, "idempotencyKey")) {
            if (!key->IsString() || key->GetStringLength() == 0 || key->GetStringLength() > 128)
                return protocolError(400, "invalid_idempotency_key");
            envelope.idempotencyKey.assign(key->GetString(), key->GetStringLength());
            const auto hash = fingerprint(asView(*name),
                text,
                documentId,
                envelope.hasExpectedStateVersion,
                expectedStateVersion,
                &envelope.payload);
            std::array<char, 32> encoded{};
            const auto converted =
                std::to_chars(encoded.data(), encoded.data() + encoded.size(), hash, 16);
            envelope.idempotencyFingerprint.assign(encoded.data(), converted.ptr);
        }
        const auto response = shell.submitCommand(std::move(envelope));
        const auto serialized = serializeResponse(response, true);
        output += "{\"jsonrpc\":\"2.0\",\"id\":";
        appendJsonRpcId(output, id);
        output += ",\"result\":{\"isError\":";
        output += response.status == EditorResponseStatus::Completed ? "false" : "true";
        output += ",\"structuredContent\":";
        output += serialized;
        output += ",\"content\":[{\"type\":\"text\",\"text\":";
        appendJsonString(output, serialized);
        output += "}]}}";
    } else {
        output += "{\"jsonrpc\":\"2.0\",\"id\":";
        appendJsonRpcId(output, id);
        output += ",\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}";
    }

    auto response = jsonResponse(200, output);
    response.add_header("MCP-Protocol-Version", mcpProtocolVersion);
    return response;
}
} // namespace arp::editor
