#include "editor/remote/editor_remote_protocol.h"

#include "core/text/number_format.h"
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
#include <stdexcept>

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

// New complete-setting envelopes reject ambiguous duplicate keys as well.
bool hasUniqueAllowedMembers(
    const JsonValue& object, std::initializer_list<std::string_view> allowed)
{
    if (!object.IsObject() || object.MemberCount() > allowed.size()
        || !hasOnlyMembers(object, allowed))
        return false;
    for (const auto name : allowed) {
        unsigned count = 0;
        for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it)
            if (asView(it->name) == name && ++count > 1)
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
    case EditorDiagnosticCode::ResourceDefinitionUnavailable:
        return "resource_definition_unavailable";
    case EditorDiagnosticCode::ResourceNavigationPresentationFailed:
        return "resource_navigation_presentation_failed";
    case EditorDiagnosticCode::SceneReferenceUnavailable:
        return "scene_reference_unavailable";
    case EditorDiagnosticCode::SceneObjectLocked:
        return "scene_object_locked";
    case EditorDiagnosticCode::ScriptPresentationFailed:
        return "script_presentation_failed";
    case EditorDiagnosticCode::ScriptModalPresentationFailed:
        return "script_modal_presentation_failed";
    case EditorDiagnosticCode::SceneModalPresentationFailed:
        return "scene_modal_presentation_failed";
    case EditorDiagnosticCode::UnsupportedIdempotency:
        return "unsupported_idempotency";
    case EditorDiagnosticCode::IdempotencyKeyConflict:
        return "idempotency_key_conflict";
    case EditorDiagnosticCode::IdempotencyCapacityExceeded:
        return "idempotency_capacity_exceeded";
    case EditorDiagnosticCode::SceneHistoryRestoreRejected:
        return "scene_history_restore_rejected";
    case EditorDiagnosticCode::SceneHistoryBudgetExceeded:
        return "scene_history_budget_exceeded";
    case EditorDiagnosticCode::CommandPreparationFailed:
        return "command_preparation_failed";
    case EditorDiagnosticCode::ResponsePayloadUnavailable:
        return "response_payload_unavailable";
    case EditorDiagnosticCode::InvalidCommandPayload:
        return "invalid_command_payload";
    case EditorDiagnosticCode::WorkspaceBusy:
        return "workspace_busy";
    case EditorDiagnosticCode::UnsavedChanges:
        return "unsaved_changes";
    case EditorDiagnosticCode::ProjectOperationFailed:
        return "project_operation_failed";
    case EditorDiagnosticCode::SceneOverwriteRequired:
        return "scene_overwrite_required";
    case EditorDiagnosticCode::ScriptExternalConflict:
        return "script_external_conflict";
    case EditorDiagnosticCode::ScriptFileOperationFailed:
        return "script_file_operation_failed";
    case EditorDiagnosticCode::SceneRootActiveUnsupported:
        return "scene_root_active_unsupported";
    }
    return "unknown";
}

void appendFloat(core::pmr::String& out, float value);

// The same canonical seven fields serve queries, snapshots and idempotency.
void appendTransformSettings(core::pmr::String& out, const EditorSceneTransformSettings& settings)
{
    if (!settings.valid())
        throw std::logic_error("invalid editor transform settings");
    out += "{\"tool\":";
    appendJsonString(out, EditorSceneTransformSettings::toolName(settings.tool));
    out += ",\"space\":";
    appendJsonString(out, EditorSceneTransformSettings::spaceName(settings.space));
    out += ",\"pivot\":";
    appendJsonString(out, EditorSceneTransformSettings::pivotName(settings.pivot));
    out += ",\"snapEnabled\":";
    out += settings.snap.enabled ? "true" : "false";
    out += ",\"moveStep\":";
    if (!core::appendFloating(out, settings.snap.moveStep))
        throw std::logic_error("invalid editor move step");
    out += ",\"rotateStepRadians\":";
    if (!core::appendFloating(out, settings.snap.rotateStepRadians))
        throw std::logic_error("invalid editor rotation step");
    out += ",\"scaleStep\":";
    if (!core::appendFloating(out, settings.snap.scaleStep))
        throw std::logic_error("invalid editor scale step");
    out += '}';
}

void appendSnapshot(core::pmr::String& out, const EditorStateSnapshot& snapshot)
{
    out += "\"snapshot\":{\"workspace\":";
    appendJsonString(out, snapshot.workspace);
    out += ",\"scene\":";
    appendJsonString(out, snapshot.scene);
    out += ",\"selection\":";
    appendJsonString(out, snapshot.selection);
    out += ",\"sceneTransformSettings\":";
    appendTransformSettings(out, snapshot.sceneTransformSettings);
    out += ",\"sceneCamera\":{\"target\":{\"x\":";
    appendFloat(out, snapshot.sceneCameraTarget.x);
    out += ",\"y\":";
    appendFloat(out, snapshot.sceneCameraTarget.y);
    out += ",\"z\":";
    appendFloat(out, snapshot.sceneCameraTarget.z);
    out += "},\"yawDegrees\":";
    appendFloat(out, snapshot.sceneCameraYaw);
    out += ",\"pitchDegrees\":";
    appendFloat(out, snapshot.sceneCameraPitch);
    out += ",\"distance\":";
    appendFloat(out, snapshot.sceneCameraDistance);
    out += '}';
    out += ",\"hasActiveDocument\":";
    out += snapshot.hasActiveDocument ? "true" : "false";
    out += ",\"activeDocumentId\":";
    appendUnsigned(out, snapshot.activeDocumentId);
    out += ",\"openDocumentCount\":";
    appendUnsigned(out, snapshot.openDocumentCount);
    out += ",\"scriptDirty\":";
    out += snapshot.scriptDirty ? "true" : "false";
    out += ",\"scriptPresentationFailed\":";
    out += snapshot.scriptPresentationFailed ? "true" : "false";
    out += ",\"scriptRelativePath\":";
    appendJsonString(out, snapshot.scriptRelativePath);
    out += ",\"scriptText\":";
    appendJsonString(out, snapshot.scriptText);
    out += ",\"exitRequested\":";
    out += snapshot.exitRequested ? "true" : "false";
    out += ",\"hasProject\":";
    out += snapshot.hasProject ? "true" : "false";
    out += ",\"projectDirty\":";
    out += snapshot.projectDirty ? "true" : "false";
    out += ",\"sceneDirty\":";
    out += snapshot.sceneDirty ? "true" : "false";
    out += ",\"scenePath\":";
    appendJsonString(out, snapshot.scenePath);
    out += ",\"sceneIoPending\":";
    out += snapshot.sceneIoPending ? "true" : "false";
    out += ",\"sceneExportPending\":";
    out += snapshot.sceneExportPending ? "true" : "false";
    out += ",\"projectSavePending\":";
    out += snapshot.projectSavePending ? "true" : "false";
    out += ",\"projectId\":";
    appendJsonString(out, snapshot.projectId);
    out += ",\"projectName\":";
    appendJsonString(out, snapshot.projectName);
    out += ",\"projectEntryScene\":";
    appendJsonString(out, snapshot.projectEntryScene);
    out += ",\"sceneRuntimeId\":";
    appendJsonString(out, snapshot.sceneRuntimeId);
    out += ",\"projectRoot\":";
    appendJsonString(out, snapshot.projectRoot);
    out += ",\"projectDiagnostic\":";
    appendJsonString(out, snapshot.projectDiagnostic);
    out += ",\"scenePersistenceTask\":{\"taskId\":";
    appendUnsigned(out, snapshot.scenePersistenceTaskId);
    out += ",\"status\":";
    appendJsonString(out, sceneTaskStatusName(snapshot.scenePersistenceTaskStatus));
    out += ",\"diagnostic\":";
    appendJsonString(out, snapshot.scenePersistenceTaskDiagnostic);
    out += "}";
    const auto& task = snapshot.sceneExportTask;
    out += ",\"sceneExportTask\":{\"taskId\":";
    appendUnsigned(out, task.taskId);
    out += ",\"status\":";
    appendJsonString(out,
        task.status == SceneExportStatus::Pending                    ? "pending"
            : task.status == SceneExportStatus::ExportedUnregistered ? "exported_unregistered"
            : task.status == SceneExportStatus::Failed               ? "failed"
                                                                     : "unknown");
    out += ",\"registered\":false,\"resourceId\":";
    appendJsonString(out, task.resourceId);
    out += ",\"contentHash\":";
    appendJsonString(out, task.contentHash);
    out += ",\"authoringHash\":";
    appendJsonString(out, task.authoringHash);
    out += ",\"path\":";
    appendJsonString(out, task.relativePath);
    out += ",\"outputPath\":";
    appendJsonString(out, task.outputPath);
    out += ",\"projectId\":";
    appendJsonString(out, task.projectId);
    out += ",\"documentEpoch\":";
    appendUnsigned(out, task.documentEpoch);
    out += ",\"diagnostic\":";
    appendJsonString(out, task.diagnostic);
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

void appendSceneInspectorBody(core::pmr::String& out, const EditorSceneInspectorSnapshot& snapshot)
{
    out += "{\"objectId\":";
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

void appendSceneInspector(core::pmr::String& out, const EditorSceneInspectorSnapshot& snapshot)
{
    out += ",\"sceneInspector\":";
    appendSceneInspectorBody(out, snapshot);
}

void appendSceneInspectorSelection(
    core::pmr::String& out, const EditorSceneInspectorSelection::Snapshot& snapshot)
{
    out +=
        ",\"sceneInspectorSelection\":{\"readOnly\":true,\"writeReason\":\"group_edit_not_available\",\"documentEpoch\":";
    appendUnsigned(out, snapshot.documentEpoch);
    out += ",\"resourceRevision\":";
    appendUnsigned(out, snapshot.resourceRevision);
    out += ",\"selectedCount\":";
    appendUnsigned(out, snapshot.selectedCount);
    out += ",\"diagnostic\":";
    appendJsonString(out, snapshot.diagnostic);
    out += ",\"objects\":[";
    for (std::size_t i = 0; i < snapshot.objects.size(); ++i) {
        if (i)
            out.push_back(',');
        out += "{\"inspector\":";
        appendSceneInspectorBody(out, snapshot.objects[i].inspector);
        out += ",\"components\":[";
        bool first = true;
        for (const auto& component : snapshot.objects[i].components) {
            if (!first)
                out.push_back(',');
            first = false;
            out += "{\"componentId\":";
            appendUnsigned(out, component.id);
            out += ",\"type\":";
            appendJsonString(out, component.type);
            out.push_back('}');
        }
        out += "]}";
    }
    out += "],\"fields\":[";
    for (std::size_t i = 0; i < snapshot.fields.size(); ++i) {
        if (i)
            out.push_back(',');
        const auto& field = snapshot.fields[i];
        out += "{\"componentType\":";
        appendJsonString(out, field.componentType);
        out += ",\"path\":";
        appendJsonString(out, field.path);
        out += ",\"status\":";
        using Status = EditorSceneInspectorSelection::Status;
        appendJsonString(out,
            field.status == Status::Common             ? "common"
                : field.status == Status::Missing      ? "missing"
                : field.status == Status::Incompatible ? "incompatible"
                                                       : "unsupported");
        out += field.mixed ? ",\"mixed\":true,\"members\":[" : ",\"mixed\":false,\"members\":[";
        bool first = true;
        for (const auto member : field.members) {
            if (!first)
                out.push_back(',');
            first = false;
            const auto& property =
                snapshot.objects[member.objectIndex].inspector.properties[member.propertyIndex];
            out += "{\"objectId\":";
            appendUnsigned(out, property.objectId);
            out += ",\"componentId\":";
            appendUnsigned(out, property.componentId);
            out += ",\"objectIndex\":";
            appendUnsigned(out, member.objectIndex);
            out += ",\"propertyIndex\":";
            appendUnsigned(out, member.propertyIndex);
            out.push_back('}');
        }
        out += "]}";
    }
    out += "]}";
}

void appendSceneHierarchy(core::pmr::String& out, const SceneHierarchySnapshot& snapshot)
{
    out += ",\"sceneHierarchy\":{\"documentEpoch\":";
    appendUnsigned(out, snapshot.documentEpoch);
    out += ",\"selectedObjectId\":";
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
        out += ",\"hidden\":";
        out += entry.hidden ? "true" : "false";
        out += ",\"locked\":";
        out += entry.locked ? "true" : "false";
        out += ",\"effectiveHidden\":";
        out += entry.effectiveHidden ? "true" : "false";
        out += ",\"effectiveLocked\":";
        out += entry.effectiveLocked ? "true" : "false";
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
    if (includeSnapshot && response.hasSceneInspectorSelection)
        appendSceneInspectorSelection(output, response.sceneInspectorSelection);
    if (response.hasSceneTransformSettings) {
        output += ",\"sceneTransformSettings\":";
        appendTransformSettings(output, response.sceneTransformSettings);
    }
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
    if (payload) {
        if (const auto* assignment = std::get_if<AssignSceneReferenceCommand>(payload)) {
            const auto& value = assignment->intent;
            feed(value.sourceKind == EditorSceneReferenceAssignment::SourceKind::Object
                    ? "object"
                    : "resource");
            feed(value.property);
            feed(value.sourceResourceId);
            for (const auto id : {value.objectId,
                     value.componentId,
                     value.documentEpoch,
                     value.resourceRevision,
                     value.sourceObjectId}) {
                std::array<char, 32> digits{};
                const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), id);
                feed(std::string_view(digits.data(), result.ptr - digits.data()));
            }
        } else if (const auto* value = std::get_if<LocateSceneResourceCommand>(payload)) {
            feed(value->property);
            for (const auto id : {value->objectId,
                     value->componentId,
                     value->documentEpoch,
                     value->resourceRevision}) {
                std::array<char, 32> digits{};
                const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), id);
                feed(std::string_view(digits.data(), result.ptr - digits.data()));
            }
        } else if (const auto* value = std::get_if<FocusSceneReferenceCommand>(payload)) {
            feed(value->property);
            for (const auto id : {value->objectId, value->componentId, value->documentEpoch}) {
                std::array<char, 32> digits{};
                const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), id);
                feed(std::string_view(digits.data(), result.ptr - digits.data()));
            }
        } else if (const auto* value = std::get_if<SetSceneTransformSettingsCommand>(payload)) {
            core::pmr::String canonical;
            appendTransformSettings(canonical, value->settings);
            feed(canonical);
        } else if (const auto* value = std::get_if<SetSceneTransformPivotCommand>(payload)) {
            feed(EditorSceneTransformSettings::pivotName(value->pivot));
        } else if (const auto* value = std::get_if<OpenProjectCommand>(payload)) {
            feed(value->root);
            feed(value->discardChanges ? "discard" : "protect");
        } else if (const auto* value = std::get_if<CreateProjectCommand>(payload)) {
            feed(value->root);
            core::pmr::String yaml;
            ProjectDiagnostic diagnostic;
            feed(value->templateKind == ProjectTemplateKind::Game2D      ? "2d"
                    : value->templateKind == ProjectTemplateKind::Game3D ? "3d"
                                                                         : "empty");
            // makeCommand already validated the manifest; encode its canonical identity.
            if (ProjectManifestCodec::encode(value->manifest, yaml, diagnostic))
                feed(yaml);
            feed(value->discardChanges ? "discard" : "protect");
        } else if (const auto* value = std::get_if<RequestEditorExitCommand>(payload)) {
            feed(value->discardChanges ? "discard" : "protect");
        } else if (const auto* value = std::get_if<CloseProjectCommand>(payload)) {
            feed(value->discardChanges ? "discard" : "protect");
        } else if (const auto* value = std::get_if<RenameProjectCommand>(payload)) {
            feed(value->name);
        } else if (const auto* value = std::get_if<SetSceneObjectNameCommand>(payload)) {
            feed(value->name);
            std::array<char, 32> textBuffer{};
            const auto converted = std::to_chars(
                textBuffer.data(), textBuffer.data() + textBuffer.size(), value->objectId);
            feed(std::string_view(textBuffer.data(), converted.ptr - textBuffer.data()));
        } else if (const auto* value = std::get_if<SetSceneObjectViewCommand>(payload)) {
            feed(value->hidden ? "true" : "false");
            feed(value->locked ? "true" : "false");
            for (const auto id : {value->objectId, value->documentEpoch}) {
                std::array<char, 32> digits{};
                const auto result = std::to_chars(digits.data(), digits.data() + digits.size(), id);
                feed(std::string_view(digits.data(), result.ptr - digits.data()));
            }
        } else if (const auto* value = std::get_if<SetSceneObjectActiveCommand>(payload)) {
            feed(value->active ? "true" : "false");
            std::array<char, 32> textBuffer{};
            const auto converted = std::to_chars(
                textBuffer.data(), textBuffer.data() + textBuffer.size(), value->objectId);
            feed(std::string_view(textBuffer.data(), converted.ptr - textBuffer.data()));
        } else if (const auto* value = std::get_if<SetProjectSettingsCommand>(payload)) {
            feed(value->name);
            feed(value->entryScene);
        } else if (const auto* value = std::get_if<CloseScriptCommand>(payload)) {
            feed(value->discardChanges ? "discard" : "protect");
        } else if (const auto* value = std::get_if<ReloadScriptCommand>(payload)) {
            feed(value->discardChanges ? "discard" : "protect");
        } else if (const auto* value = std::get_if<LoadSceneCommand>(payload)) {
            feed(value->discardChanges ? "discard" : "protect");
        } else if (const auto* value = std::get_if<NewSceneCommand>(payload)) {
            feed(value->discardChanges ? "discard" : "protect");
        } else if (const auto* value = std::get_if<SaveSceneCommand>(payload)) {
            feed(value->overwriteExisting ? "overwrite" : "protect-existing");
        } else if (const auto* value = std::get_if<ExportSceneCommand>(payload)) {
            feed(value->relativeDirectory);
        }
    }
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
    for (const auto control : {"menu_Project",
             "project_ui_new",
             "project_ui_open",
             "project_ui_settings",
             "project_ui_close",
             "project_ui_empty",
             "project_ui_2d",
             "project_ui_3d",
             "project_ui_entry",
             "project_ui_submit",
             "project_ui_discard",
             "project_ui_cancel",
             "menu_Scene",
             "scene_ui_new",
             "scene_object_add",
             "scene_object_delete",
             "open_scene",
             "save_scene",
             "save_scene_as",
             "duplicate_scene",
             "export_scene",
             "run_play",
             "run_stop",
             "close_menu",
             "scene_ui_submit",
             "scene_ui_retry",
             "scene_file_retry",
             "scene_ui_confirm",
             "exit_ui_request",
             "exit_ui_confirm",
             "exit_ui_cancel",
             "script_ui_close",
             "script_ui_reload",
             "script_ui_confirm",
             "script_ui_cancel",
             "script_presentation_retry",
             "scene_ui_cancel",
             "scene_transform_settings",
             "transform_ui_tool",
             "transform_ui_space",
             "transform_ui_pivot",
             "transform_ui_snap",
             "transform_ui_apply",
             "transform_ui_cancel"}) {
        if (source == control)
            return true;
    }
    if (source == "workspace_3D" || source == "workspace_Game" || source == "workspace_Script"
        || source == "dock_Inspector" || source == "dock_Signals" || source == "dock_Groups"
        || source == "scene_level" || source == "scene_player" || source == "scene_menu")
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
    bool discardChanges = false;
    if (const auto* discard = member(arguments, "discardChanges")) {
        if (!discard->IsBool())
            return false;
        discardChanges = discard->GetBool();
    }
    if (name == "project.create" || name == "project.open") {
        const auto* path = member(arguments, "path");
        if (!path || !path->IsString() || path->GetStringLength() == 0
            || path->GetStringLength() > 512)
            return false;
        if (name == "project.open") {
            payload = OpenProjectCommand{core::pmr::String(asView(*path)), discardChanges};
            return true;
        }
        const auto* yaml = member(arguments, "manifestYaml");
        ProjectManifest manifest;
        ProjectDiagnostic diagnostic;
        if (!yaml || !yaml->IsString()
            || !ProjectManifestCodec::decode(asView(*yaml), manifest, diagnostic))
            return false;
        ProjectTemplateKind kind = ProjectTemplateKind::Empty;
        if (const auto* value = member(arguments, "template")) {
            if (!value->IsString())
                return false;
            const auto name = asView(*value);
            if (name == "2d")
                kind = ProjectTemplateKind::Game2D;
            else if (name == "3d")
                kind = ProjectTemplateKind::Game3D;
            else if (name != "empty")
                return false;
        }
        payload = CreateProjectCommand{
            core::pmr::String(asView(*path)), std::move(manifest), discardChanges, kind};
        return true;
    }
    if (name == "editor.exit") {
        payload = RequestEditorExitCommand{discardChanges};
        return true;
    }
    if (name == "project.close") {
        payload = CloseProjectCommand{discardChanges};
        return true;
    }
    if (name == "scene.new") {
        payload = NewSceneCommand{discardChanges};
        return true;
    }
    if (name == "scene.duplicate") {
        payload = DuplicateSceneCommand{};
        return true;
    }
    if (name == "scene.transform.pivot.set") {
        const auto* value = member(arguments, "pivot");
        if (!value || !value->IsString())
            return false;
        SetSceneTransformPivotCommand command;
        if (!EditorSceneTransformSettings::parsePivot(asView(*value), command.pivot))
            return false;
        payload = command;
        return true;
    }
    if (name == "scene.transform.settings.get") {
        payload = GetSceneTransformSettingsQuery{};
        return true;
    }
    if (name == "scene.transform.settings.set") {
        const auto* settings = member(arguments, "settings");
        if (!settings || !settings->IsObject() || settings->MemberCount() != 7
            || !hasOnlyMembers(*settings,
                {"tool",
                    "space",
                    "pivot",
                    "snapEnabled",
                    "moveStep",
                    "rotateStepRadians",
                    "scaleStep"}))
            return false;
        const auto* tool = member(*settings, "tool");
        const auto* space = member(*settings, "space");
        const auto* pivot = member(*settings, "pivot");
        const auto* enabled = member(*settings, "snapEnabled");
        SetSceneTransformSettingsCommand command;
        auto& value = command.settings;
        if (!tool || !tool->IsString() || !space || !space->IsString() || !pivot
            || !pivot->IsString() || !enabled || !enabled->IsBool()
            || !EditorSceneTransformSettings::parseTool(asView(*tool), value.tool)
            || !EditorSceneTransformSettings::parseSpace(asView(*space), value.space)
            || !EditorSceneTransformSettings::parsePivot(asView(*pivot), value.pivot)
            || !floatValue(member(*settings, "moveStep"), value.snap.moveStep)
            || !floatValue(member(*settings, "rotateStepRadians"), value.snap.rotateStepRadians)
            || !floatValue(member(*settings, "scaleStep"), value.snap.scaleStep))
            return false;
        value.snap.enabled = enabled->GetBool();
        if (!value.valid())
            return false;
        payload = command;
        return true;
    }
    if (name == "scene.selection.transform") {
        const auto* tool = member(arguments, "tool");
        const auto* space = member(arguments, "space");
        const auto* pivot = member(arguments, "pivot");
        const auto* amount = member(arguments, "amount");
        std::uint64_t axis = 0;
        if (!tool || !tool->IsString() || !space || !space->IsString() || !pivot
            || !pivot->IsString() || !amount || !amount->IsNumber()
            || !std::isfinite(amount->GetDouble())
            || !unsignedValue(member(arguments, "axis"), axis) || axis > 2)
            return false;
        TransformSceneSelectionCommand command;
        const auto t = asView(*tool), s = asView(*space), p = asView(*pivot);
        if (t == "move")
            command.tool = EditorSceneTransformTool::Move;
        else if (t == "rotate")
            command.tool = EditorSceneTransformTool::Rotate;
        else if (t == "scale")
            command.tool = EditorSceneTransformTool::Scale;
        else
            return false;
        if (s == "world")
            command.space = EditorSceneTransformSpace::World;
        else if (s == "local")
            command.space = EditorSceneTransformSpace::Local;
        else
            return false;
        if (p == "active")
            command.pivot = EditorSceneTransformPivot::Active;
        else if (p == "center")
            command.pivot = EditorSceneTransformPivot::Center;
        else if (p == "individual")
            command.pivot = EditorSceneTransformPivot::Individual;
        else
            return false;
        command.axis = static_cast<unsigned>(axis);
        command.amount = amount->GetDouble();
        payload = command;
        return true;
    }
    if (name == "scene.selection.focus") {
        payload = FocusSceneSelectionCommand{};
        return true;
    }
    if (name == "scene.reference.focus") {
        FocusSceneReferenceCommand command;
        const auto* property = member(arguments, "property");
        if (!unsignedValue(member(arguments, "objectId"), command.objectId) || !command.objectId
            || !unsignedValue(member(arguments, "componentId"), command.componentId)
            || !command.componentId
            || !unsignedValue(member(arguments, "documentEpoch"), command.documentEpoch)
            || !command.documentEpoch || !property || !property->IsString()
            || property->GetStringLength() == 0
            || property->GetStringLength()
                > EditorSceneInspectorSelection::editorBudget.maxStringBytes)
            return false;
        command.property = asView(*property);
        payload = std::move(command);
        return true;
    }
    if (name == "scene.reference.assign") {
        AssignSceneReferenceCommand command;
        auto& intent = command.intent;
        const auto* property = member(arguments, "property");
        const auto* kind = member(arguments, "sourceKind");
        if (!unsignedValue(member(arguments, "objectId"), intent.objectId) || !intent.objectId
            || !unsignedValue(member(arguments, "componentId"), intent.componentId)
            || !intent.componentId
            || !unsignedValue(member(arguments, "documentEpoch"), intent.documentEpoch)
            || !intent.documentEpoch
            || !unsignedValue(member(arguments, "resourceRevision"), intent.resourceRevision)
            || !intent.resourceRevision || !property || !property->IsString()
            || property->GetStringLength() == 0
            || property->GetStringLength() > EditorSceneDocument::maxPropertyNameBytes || !kind
            || !kind->IsString())
            return false;
        intent.property = asView(*property);
        if (asView(*kind) == "object") {
            intent.sourceKind = EditorSceneReferenceAssignment::SourceKind::Object;
            if (member(arguments, "sourceResourceId")
                || !unsignedValue(member(arguments, "sourceObjectId"), intent.sourceObjectId)
                || !intent.sourceObjectId)
                return false;
        } else if (asView(*kind) == "resource") {
            intent.sourceKind = EditorSceneReferenceAssignment::SourceKind::Resource;
            const auto* id = member(arguments, "sourceResourceId");
            if (member(arguments, "sourceObjectId") || !id || !id->IsString()
                || !resource::isResourceUuid(asView(*id)))
                return false;
            intent.sourceResourceId = asView(*id);
        } else
            return false;
        payload = std::move(command);
        return true;
    }
    if (name == "scene.resource.locate") {
        LocateSceneResourceCommand command;
        const auto* property = member(arguments, "property");
        if (!unsignedValue(member(arguments, "objectId"), command.objectId) || !command.objectId
            || !unsignedValue(member(arguments, "componentId"), command.componentId)
            || !command.componentId
            || !unsignedValue(member(arguments, "documentEpoch"), command.documentEpoch)
            || !command.documentEpoch
            || !unsignedValue(member(arguments, "resourceRevision"), command.resourceRevision)
            || !command.resourceRevision || !property || !property->IsString()
            || property->GetStringLength() == 0
            || property->GetStringLength()
                > EditorSceneInspectorSelection::editorBudget.maxStringBytes)
            return false;
        command.property = asView(*property);
        payload = std::move(command);
        return true;
    }
    if (name == "project.save") {
        payload = SaveProjectCommand{};
        return true;
    }
    if (name == "project.rename") {
        const auto* value = member(arguments, "name");
        if (!value || !value->IsString())
            return false;
        payload = RenameProjectCommand{core::pmr::String(asView(*value))};
        return true;
    }
    if (name == "project.settings.set") {
        const auto* value = member(arguments, "name");
        const auto* entry = member(arguments, "entryScene");
        if (!value || !value->IsString() || !entry || !entry->IsString())
            return false;
        payload = SetProjectSettingsCommand{
            core::pmr::String(asView(*value)), core::pmr::String(asView(*entry))};
        return true;
    }
    if (name == "editor.state.get") {
        payload = GetEditorStateQuery{};
        return true;
    }
    if (name == "scene.inspector.selection.get") {
        payload = GetSceneInspectorSelectionQuery{};
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
    if (name == "editor.search" || name == "editor.text.set") {
        const auto* source = member(arguments, "source");
        const auto* value = member(arguments, "value");
        if (!source || !source->IsString() || !value || !value->IsString()
            || value->GetStringLength() > 512u)
            return false;
        const auto sourceText = asView(*source);
        if (name == "editor.text.set") {
            if ((sourceText != "scene_ui_path" && sourceText != "project_ui_path"
                    && sourceText != "project_ui_name"
                    && !EditorSceneTransformPanel::textSource(sourceText))
                || asView(*value).find_first_of("\r\n") != std::string_view::npos
                || asView(*value).find('\0') != std::string_view::npos)
                return false;
        } else if (sourceText != "scene_search" && sourceText != "file_search"
            && sourceText != "property_search")
            return false;
        payload = SetTextValueCommand(sourceText, asView(*value));
        return true;
    }
    if (name == "scene.select") {
        std::uint64_t objectId = 0;
        if (!unsignedValue(member(arguments, "objectId"), objectId) || objectId == 0)
            return false;
        payload = SelectSceneObjectCommand{objectId};
        return true;
    }
    if (name == "scene.object.view.set") {
        SetSceneObjectViewCommand command;
        const auto* hidden = member(arguments, "hidden");
        const auto* locked = member(arguments, "locked");
        if (!unsignedValue(member(arguments, "objectId"), command.objectId) || !command.objectId
            || !unsignedValue(member(arguments, "documentEpoch"), command.documentEpoch)
            || !command.documentEpoch || !hidden || !hidden->IsBool() || !locked
            || !locked->IsBool())
            return false;
        command.hidden = hidden->GetBool();
        command.locked = locked->GetBool();
        payload = command;
        return true;
    }
    if (name == "scene.active.set") {
        std::uint64_t objectId = 0;
        const auto* value = member(arguments, "active");
        if (!unsignedValue(member(arguments, "objectId"), objectId) || objectId == 0 || !value
            || !value->IsBool())
            return false;
        payload = SetSceneObjectActiveCommand{objectId, value->GetBool()};
        return true;
    }
    if (name == "scene.rename") {
        std::uint64_t objectId = 0;
        const auto* value = member(arguments, "name");
        if (!unsignedValue(member(arguments, "objectId"), objectId) || objectId == 0 || !value
            || !value->IsString() || !EditorSceneDocument::validObjectName(asView(*value)))
            return false;
        SetSceneObjectNameCommand command(asView(*value));
        command.objectId = objectId;
        payload = std::move(command);
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
    if (name == "script.replace" || name == "script.save" || name == "script.reload"
        || name == "script.close" || name == "script.undo" || name == "script.redo") {
        if (!unsignedValue(member(arguments, "documentId"), documentId) || documentId == 0)
            return false;
        if (name == "script.replace") {
            const auto* textValue = member(arguments, "text");
            if (!textValue || !textValue->IsString()
                || textValue->GetStringLength() > maxScriptBytes)
                return false;
            text = asView(*textValue);
            payload = ReplaceScriptTextCommand(documentId, text);
        } else if (name == "script.close")
            payload = CloseScriptCommand{documentId, discardChanges};
        else if (name == "script.reload")
            payload = ReloadScriptCommand{documentId, discardChanges};
        else if (name == "script.save")
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
        if (name == "scene.save") {
            SaveSceneCommand command(text);
            if (const auto* value = member(arguments, "overwriteExisting")) {
                if (!value->IsBool())
                    return false;
                command.overwriteExisting = value->GetBool();
            }
            payload = std::move(command);
        } else {
            LoadSceneCommand command(text);
            command.discardChanges = discardChanges;
            payload = std::move(command);
        }
        return true;
    }
    if (name == "scene.export") {
        const auto* path = member(arguments, "path");
        if (!path || !path->IsString() || path->GetStringLength() == 0
            || path->GetStringLength() > 512)
            return false;
        payload = ExportSceneCommand{core::pmr::String(asView(*path))};
        return true;
    }
    if (name == "scene.export.task.get") {
        if (!unsignedValue(member(arguments, "taskId"), documentId) || documentId == 0)
            return false;
        payload = GetSceneExportTaskQuery{documentId};
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
        ",\"capabilities\":[\"project.create\",\"project.open\",\"project.close\",\"project.rename\",\"project.settings.set\",\"project.save\",\"editor.state.get\",\"editor.exit\",\"editor.activate\",\"editor.search\",\"editor.text.set\",\"scene.inspector.get\",\"scene.inspector.selection.get\",\"scene.hierarchy.get\",\"scene.component.catalog.get\",\"scene.component.add\",\"scene.component.remove\",\"scene.component.property.set\",\"scene.undo\",\"scene.redo\",\"scene.reparent\",\"scene.rename\",\"scene.active.set\",\"scene.object.view.set\",\"scene.select\",\"scene.selection.focus\",\"scene.reference.focus\",\"scene.resource.locate\",\"scene.selection.transform\",\"scene.transform.pivot.set\",\"scene.transform.settings.get\",\"scene.transform.settings.set\",\"scene.new\",\"scene.duplicate\",\"scene.export\",\"scene.export.task.get\",\"scene.save\",\"scene.load\",\"scene.task.get\",\"script.replace\",\"script.save\",\"script.reload\",\"script.close\",\"script.undo\",\"script.redo\"]}";
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
    document.Parse<rapidjson::kParseFullPrecisionFlag>(body.data());
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
                "documentEpoch",
                "resourceRevision",
                "sourceKind",
                "sourceObjectId",
                "sourceResourceId",
                "text",
                "path",
                "manifestYaml",
                "template",
                "name",
                "entryScene",
                "discardChanges",
                "overwriteExisting",
                "taskId",
                "source",
                "value",
                "objectId",
                "active",
                "hidden",
                "locked",
                "parentId",
                "childIndex",
                "componentId",
                "type",
                "properties",
                "property",
                "kind",
                "tool",
                "space",
                "pivot",
                "axis",
                "amount",
                "settings"}))
        return protocolError(400, "unknown_envelope_field");
    if (!name || !name->IsString())
        return protocolError(400, "missing_command");
    const auto commandName = asView(*name);
    if (commandName != "scene.object.view.set"
        && (member(document, "hidden") || member(document, "locked")))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.object.view.set"
        && !hasUniqueAllowedMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "objectId",
                "documentEpoch",
                "hidden",
                "locked"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.active.set" && member(document, "active"))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.save" && member(document, "overwriteExisting"))
        return protocolError(400, "invalid_command_fields");
    const bool projectCommand = commandName == "project.create" || commandName == "project.open"
        || commandName == "project.close" || commandName == "project.rename"
        || commandName == "project.save" || commandName == "project.settings.set";
    if (projectCommand) {
        const bool valid = commandName == "project.create" ? hasOnlyMembers(document,
                                                                 {"schemaVersion",
                                                                     "command",
                                                                     "commandId",
                                                                     "correlationId",
                                                                     "expectedStateVersion",
                                                                     "idempotencyKey",
                                                                     "path",
                                                                     "manifestYaml",
                                                                     "template",
                                                                     "discardChanges"})
            : commandName == "project.open"                ? hasOnlyMembers(document,
                                                                 {"schemaVersion",
                                                                     "command",
                                                                     "commandId",
                                                                     "correlationId",
                                                                     "expectedStateVersion",
                                                                     "idempotencyKey",
                                                                     "path",
                                                                     "discardChanges"})
            : commandName == "project.close"               ? hasOnlyMembers(document,
                                                                 {"schemaVersion",
                                                                     "command",
                                                                     "commandId",
                                                                     "correlationId",
                                                                     "expectedStateVersion",
                                                                     "idempotencyKey",
                                                                     "discardChanges"})
            : commandName == "project.settings.set"        ? hasOnlyMembers(document,
                                                                 {"schemaVersion",
                                                                     "command",
                                                                     "commandId",
                                                                     "correlationId",
                                                                     "expectedStateVersion",
                                                                     "idempotencyKey",
                                                                     "name",
                                                                     "entryScene"})
            : commandName == "project.rename"              ? hasOnlyMembers(document,
                                                                 {"schemaVersion",
                                                                     "command",
                                                                     "commandId",
                                                                     "correlationId",
                                                                     "expectedStateVersion",
                                                                     "idempotencyKey",
                                                                     "name"})
                                                           : hasOnlyMembers(document,
                                                                 {"schemaVersion",
                                                                     "command",
                                                                     "commandId",
                                                                     "correlationId",
                                                                     "expectedStateVersion",
                                                                     "idempotencyKey"});
        if (!valid)
            return protocolError(400, "invalid_command_fields");
    }
    if ((commandName != "project.settings.set" && member(document, "entryScene"))
        || (!projectCommand
            && (member(document, "manifestYaml")
                || (commandName != "scene.rename" && member(document, "name"))))
        || (commandName != "project.create" && commandName != "project.open"
            && commandName != "project.close" && commandName != "scene.load"
            && commandName != "scene.new" && commandName != "script.reload"
            && commandName != "script.close" && commandName != "editor.exit"
            && member(document, "discardChanges")))
        return protocolError(400, "invalid_command_fields");
    if ((commandName == "scene.new" || commandName == "editor.exit")
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "discardChanges"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.reference.focus"
        && !hasUniqueAllowedMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "objectId",
                "componentId",
                "property",
                "documentEpoch"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.reference.assign"
        && !hasUniqueAllowedMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "objectId",
                "componentId",
                "property",
                "documentEpoch",
                "resourceRevision",
                "sourceKind",
                "sourceObjectId",
                "sourceResourceId"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.reference.assign"
        && (member(document, "sourceKind") || member(document, "sourceObjectId")
            || member(document, "sourceResourceId")))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.resource.locate"
        && !hasUniqueAllowedMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "objectId",
                "componentId",
                "property",
                "documentEpoch",
                "resourceRevision"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.reference.focus" && commandName != "scene.resource.locate"
        && commandName != "scene.reference.assign" && commandName != "scene.object.view.set"
        && member(document, "documentEpoch"))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.resource.locate" && commandName != "scene.reference.assign"
        && member(document, "resourceRevision"))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.inspector.get"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "objectId"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.inspector.selection.get"
        && !hasOnlyMembers(document,
            {"schemaVersion", "command", "commandId", "correlationId", "expectedStateVersion"}))
        return protocolError(400, "invalid_command_fields");
    if ((commandName == "scene.export"
            && !hasOnlyMembers(document,
                {"schemaVersion",
                    "command",
                    "commandId",
                    "correlationId",
                    "expectedStateVersion",
                    "idempotencyKey",
                    "path"}))
        || (commandName == "scene.export.task.get"
            && !hasOnlyMembers(document,
                {"schemaVersion",
                    "command",
                    "commandId",
                    "correlationId",
                    "expectedStateVersion",
                    "taskId"})))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.transform.settings.get"
        && !hasUniqueAllowedMembers(document,
            {"schemaVersion", "command", "commandId", "correlationId", "expectedStateVersion"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.transform.settings.set"
        && !hasUniqueAllowedMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "settings"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.transform.settings.set" && member(document, "settings"))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.transform.pivot.set"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "pivot"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.selection.transform"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "tool",
                "space",
                "pivot",
                "axis",
                "amount"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.selection.transform"
        && (member(document, "tool") || member(document, "space")
            || (member(document, "pivot") && commandName != "scene.transform.pivot.set")
            || member(document, "axis") || member(document, "amount")))
        return protocolError(400, "invalid_command_fields");
    if ((commandName == "scene.duplicate" || commandName == "scene.selection.focus")
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey"}))
        return protocolError(400, "invalid_command_fields");
    if ((commandName == "scene.hierarchy.get" || commandName == "scene.component.catalog.get")
        && !hasOnlyMembers(document,
            {"schemaVersion", "command", "commandId", "correlationId", "expectedStateVersion"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.select"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "objectId"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.active.set"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "objectId",
                "active"}))
        return protocolError(400, "invalid_command_fields");
    if (commandName == "scene.rename"
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "objectId",
                "name"}))
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
        && commandName != "scene.reference.focus" && commandName != "scene.resource.locate"
        && commandName != "scene.reference.assign" && commandName != "scene.hierarchy.get"
        && commandName != "scene.component.catalog.get" && commandName != "scene.active.set"
        && commandName != "scene.rename" && commandName != "scene.select"
        && commandName != "scene.object.view.set" && commandName != "scene.reparent"
        && commandName != "scene.component.add" && commandName != "scene.component.remove"
        && (member(document, "objectId") || member(document, "componentId")
            || member(document, "property") || member(document, "kind")))
        return protocolError(400, "invalid_command_fields");
    if (commandName != "scene.export" && commandName != "scene.export.task.get"
        && commandName != "scene.save" && commandName != "scene.load"
        && commandName != "scene.task.get" && commandName != "project.create"
        && commandName != "project.open"
        && (member(document, "path") || member(document, "taskId")))
        return protocolError(400, "invalid_command_fields");
    if ((commandName == "script.reload" || commandName == "script.close")
        && !hasOnlyMembers(document,
            {"schemaVersion",
                "command",
                "commandId",
                "correlationId",
                "expectedStateVersion",
                "idempotencyKey",
                "documentId",
                "discardChanges"}))
        return protocolError(400, "invalid_command_fields");
    if ((commandName == "editor.state.get"
            && (member(document, "documentId") || member(document, "text")
                || member(document, "source") || member(document, "value")))
        || (commandName == "editor.activate"
            && (member(document, "documentId") || member(document, "text")
                || member(document, "value")))
        || ((commandName == "editor.search" || commandName == "editor.text.set")
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
            || response.diagnostic == EditorDiagnosticCode::WorkspaceBusy
            || response.diagnostic == EditorDiagnosticCode::UnsavedChanges
            || response.diagnostic == EditorDiagnosticCode::SceneOverwriteRequired
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
    document.Parse<rapidjson::kParseFullPrecisionFlag>(body.data());
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
        // clang-format off
        output += R"json(,"result":{"tools":[
{"name":"project.create","description":"Create and open an explicit empty, 2d or 3d project. Templates require manifest.entryScene UUID.","inputSchema":{"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":512},"manifestYaml":{"type":"string","maxLength":1048576},"template":{"type":"string","enum":["empty","2d","3d"]},"discardChanges":{"type":"boolean"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false,"required":["path","manifestYaml"]}},
{"name":"project.open","description":"Validate a candidate project before replacing the workspace.","inputSchema":{"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":512},"discardChanges":{"type":"boolean"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false,"required":["path"]}},
{"name":"project.close","description":"Close the project, protecting all unsaved documents.","inputSchema":{"type":"object","properties":{"discardChanges":{"type":"boolean"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"project.rename","description":"Edit the project name draft.","inputSchema":{"type":"object","properties":{"name":{"type":"string"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false,"required":["name"]}},
{"name":"project.settings.set","description":"Edit project name and Ready SceneAsset startup reference together; empty entryScene explicitly clears it. Save separately.","inputSchema":{"type":"object","properties":{"name":{"type":"string"},"entryScene":{"type":"string","maxLength":36},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false,"required":["name","entryScene"]}},
{"name":"project.save","description":"Queue an atomic project settings snapshot save; observe completion through editor.state.get.","inputSchema":{"type":"object","properties":{"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"editor.state.get","description":"Read current editor and active script state.","inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
{"name":"scene.inspector.get","description":"Read typed Inspector properties and stable object/component IDs for a scene object (defaults to selection).","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1}},"additionalProperties":false}},
{"name":"scene.inspector.selection.get","description":"Read the complete current selection, common and mixed fields, original per-object values and a read-only reason. Does not edit any object.","inputSchema":{"type":"object","properties":{"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"scene.hierarchy.get","description":"Read the scene hierarchy with current document epoch, stable object and parent IDs, depth, sibling order, selection and local/effective hidden/locked flags.","inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
{"name":"scene.component.catalog.get","description":"Read the explicit allowlisted component factory catalog.","inputSchema":{"type":"object","properties":{},"additionalProperties":false}},
{"name":"scene.component.add","description":"Add one allowlisted component, optionally applying typed initial Inspector properties atomically.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"type":{"type":"string","minLength":1,"maxLength":128},"properties":{"type":"array","maxItems":64,"items":{"type":"object","properties":{"property":{"type":"string","minLength":1,"maxLength":256},"kind":{"type":"string","enum":["boolean","integer","unsignedInteger","number","enumeration","vector2","vector2List","vector3","color4","string","resourceReference","sceneObjectReference"]},"value":{}},"required":["property","kind","value"],"additionalProperties":false}},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","type"],"additionalProperties":false}},
{"name":"scene.component.remove","description":"Remove one non-Transform component by stable object and component IDs.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"componentId":{"type":"integer","minimum":1},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","componentId"],"additionalProperties":false}},
{"name":"scene.component.property.set","description":"Set one typed scene component Inspector property through the editor command history.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"componentId":{"type":"integer","minimum":1},"property":{"type":"string","minLength":1,"maxLength":256},"kind":{"type":"string","enum":["boolean","integer","unsignedInteger","number","enumeration","vector2","vector2List","vector3","color4","string","resourceReference","sceneObjectReference"]},"value":{},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","componentId","property","kind","value"],"additionalProperties":false}},
{"name":"scene.select","description":"Select an existing scene object by exact ID using the shared Inspector selection command. Selection does not edit authoring content or Undo history.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId"],"additionalProperties":false}},
{"name":"scene.selection.focus","description":"Move the editor camera target to the center of selected world origins, preserving orientation, distance, scene contents and selection. This does not fit geometry bounds. Modal, gesture and workspace busy guards apply.","inputSchema":{"type":"object","properties":{"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"scene.reference.focus","description":"Focus the current object reference world origin while preserving Inspector selection and history. Explicit source document epoch required; stale or invalid fields fail. Each new command resolves the current target.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"componentId":{"type":"integer","minimum":1},"documentEpoch":{"type":"integer","minimum":1},"property":{"type":"string","minLength":1,"maxLength":512},"expectedStateVersion":{"type":"integer","minimum":0},"idempotencyKey":{"type":"string","maxLength":128}},"required":["objectId","componentId","documentEpoch","property"],"additionalProperties":false}},
{"name":"scene.reference.assign","description":"Assign one explicit object/resource reference on the current single selection through shared property history. Requires current document/resource revisions, exact metadata and Ready resource identity; rejects mixed/empty sources.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"componentId":{"type":"integer","minimum":1},"documentEpoch":{"type":"integer","minimum":1},"resourceRevision":{"type":"integer","minimum":1},"property":{"type":"string","minLength":1,"maxLength":128},"sourceKind":{"type":"string","enum":["object","resource"]},"sourceObjectId":{"type":"integer","minimum":1},"sourceResourceId":{"type":"string","minLength":36,"maxLength":36},"expectedStateVersion":{"type":"integer","minimum":0},"idempotencyKey":{"type":"string","maxLength":128}},"required":["objectId","componentId","documentEpoch","resourceRevision","property","sourceKind"],"oneOf":[{"properties":{"sourceKind":{"const":"object"}},"required":["sourceObjectId"],"not":{"required":["sourceResourceId"]}},{"properties":{"sourceKind":{"const":"resource"}},"required":["sourceResourceId"],"not":{"required":["sourceObjectId"]}}],"additionalProperties":false}},
{"name":"scene.resource.locate","description":"Reveal the current resource reference definition envelope in Files. Requires explicit document and resource publication revisions. Never guesses original source files or opens resources.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"componentId":{"type":"integer","minimum":1},"documentEpoch":{"type":"integer","minimum":1},"resourceRevision":{"type":"integer","minimum":1},"property":{"type":"string","minLength":1,"maxLength":512},"expectedStateVersion":{"type":"integer","minimum":0},"idempotencyKey":{"type":"string","maxLength":128}},"required":["objectId","componentId","documentEpoch","resourceRevision","property"],"additionalProperties":false}},
{"name":"scene.selection.transform","description":"Atomically transform the complete current selection with one Undo entry. Explicit relative amount: world distance, radians, or scale delta. No implicit snapping; busy and stale-version guards apply.","inputSchema":{"type":"object","properties":{"tool":{"type":"string","enum":["move","rotate","scale"]},"space":{"type":"string","enum":["world","local"]},"pivot":{"type":"string","enum":["active","center","individual"]},"axis":{"type":"integer","minimum":0,"maximum":2},"amount":{"type":"number"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["tool","space","pivot","axis","amount"],"additionalProperties":false}},
{"name":"scene.transform.settings.get","description":"Read exact editor transform preferences without modifying Scene or Undo.","inputSchema":{"type":"object","properties":{"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"scene.transform.settings.set","description":"Atomically replace all seven editor transform preferences. Rotation step uses radians; all steps must be finite and positive even when snapping is disabled. Busy guards apply; does not modify Scene or Undo.","inputSchema":{"type":"object","properties":{"settings":{"type":"object","properties":{"tool":{"type":"string","enum":["select","move","rotate","scale"]},"space":{"type":"string","enum":["world","local"]},"pivot":{"type":"string","enum":["active","center","individual"]},"snapEnabled":{"type":"boolean"},"moveStep":{"type":"number","exclusiveMinimum":0},"rotateStepRadians":{"type":"number","exclusiveMinimum":0},"scaleStep":{"type":"number","exclusiveMinimum":0}},"required":["tool","space","pivot","snapEnabled","moveStep","rotateStepRadians","scaleStep"],"additionalProperties":false},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["settings"],"additionalProperties":false}},
{"name":"scene.transform.pivot.set","description":"Set the editor gizmo pivot preference for subsequent gestures. Active, center of effective origins, or individual origins. Does not modify Scene or Undo; busy guards apply.","inputSchema":{"type":"object","properties":{"pivot":{"type":"string","enum":["active","center","individual"]},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["pivot"],"additionalProperties":false}},
{"name":"scene.object.view.set","description":"Set session-only hidden and locked flags by exact object ID and current document epoch. Ancestors propagate both flags. Preserves Scene active, selection, authoring, dirty state and Undo/Redo. Root unlock is supported. Rendering and local controls are not yet integrated.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"documentEpoch":{"type":"integer","minimum":1},"hidden":{"type":"boolean"},"locked":{"type":"boolean"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","documentEpoch","hidden","locked"],"additionalProperties":false}},
{"name":"scene.active.set","description":"Set an existing child GameObject active flag by exact ID, preserving selection and recording Undo. Scene root is unsupported by the persistence contract. Does not advance the Passive Scene lifecycle.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"active":{"type":"boolean"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","active"],"additionalProperties":false}},
{"name":"scene.rename","description":"Rename an existing scene object by exact ID, preserving selection and recording Undo. UTF-8 name budget is 128 bytes; no-op preserves history/stateVersion.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"name":{"type":"string","minLength":1,"maxLength":128},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","name"],"additionalProperties":false}},
{"name":"scene.reparent","description":"Move an object to a parent at a zero-based child index through the scene command history.","inputSchema":{"type":"object","properties":{"objectId":{"type":"integer","minimum":1},"parentId":{"type":"integer","minimum":1},"childIndex":{"type":"integer","minimum":0},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["objectId","parentId","childIndex"],"additionalProperties":false}},
{"name":"editor.activate","description":"Activate an allowlisted editor workspace, dock, scene tab or node.","inputSchema":{"type":"object","properties":{"source":{"type":"string"}},"required":["source"],"additionalProperties":false}},
{"name":"editor.search","description":"Set a bounded query on an editor search field.","inputSchema":{"type":"object","properties":{"source":{"type":"string","enum":["scene_search","file_search","property_search"]},"value":{"type":"string","maxLength":512}},"required":["source","value"],"additionalProperties":false}},
{"name":"script.replace","description":"Replace full text in the targeted open script.","inputSchema":{"type":"object","properties":{"documentId":{"type":"integer","minimum":1},"text":{"type":"string"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["documentId","text"],"additionalProperties":false}},
{"name":"script.save","description":"Atomically save the targeted open script after checking its exact disk baseline. External changes or an unreadable baseline reject the save and preserve local edits.","inputSchema":{"type":"object","properties":{"documentId":{"type":"integer","minimum":1},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["documentId"],"additionalProperties":false}},
{"name":"script.close","description":"Close one exact open document ID without saving/deleting its disk file. Dirty buffers require explicit discardChanges. Other documents and their histories stay intact; stale IDs reject.","inputSchema":{"type":"object","properties":{"documentId":{"type":"integer","minimum":1},"discardChanges":{"type":"boolean"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["documentId"],"additionalProperties":false}},
{"name":"script.reload","description":"Reload the targeted active script from disk. Dirty buffers require explicit discardChanges=true. Successful reload clears local history; failures preserve the document.","inputSchema":{"type":"object","properties":{"documentId":{"type":"integer","minimum":1},"discardChanges":{"type":"boolean"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["documentId"],"additionalProperties":false}},
{"name":"script.undo","description":"Undo the latest edit in the targeted open script.","inputSchema":{"type":"object","properties":{"documentId":{"type":"integer","minimum":1},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["documentId"],"additionalProperties":false}},
{"name":"script.redo","description":"Redo the latest undone edit in the targeted open script.","inputSchema":{"type":"object","properties":{"documentId":{"type":"integer","minimum":1},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["documentId"],"additionalProperties":false}},
{"name":"scene.undo","description":"Undo the latest supported scene component, property, transform or hierarchy edit.","inputSchema":{"type":"object","properties":{"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"scene.redo","description":"Redo the latest undone supported scene component, property, transform or hierarchy edit.","inputSchema":{"type":"object","properties":{"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"editor.text.set","description":"Set a visible, enabled Project/Scene/Transform dialog text draft; this does not submit IO or apply transform preferences.","inputSchema":{"type":"object","properties":{"source":{"type":"string","enum":["project_ui_name","project_ui_path","scene_ui_path","transform_ui_move","transform_ui_rotate","transform_ui_scale"]},"value":{"type":"string","maxLength":512}},"required":["source","value"],"additionalProperties":false}},
{"name":"editor.exit","description":"Request Editor shutdown. Unsaved scene, project or any open script requires explicit discardChanges. Active tasks/Play reject exit. Acceptance freezes authoring; the response is sent before native shutdown.","inputSchema":{"type":"object","properties":{"discardChanges":{"type":"boolean"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"scene.new","description":"Replace the authoring document with an unsaved empty Passive scene. Busy and unsaved guards apply; scripts and project resources are retained.","inputSchema":{"type":"object","properties":{"discardChanges":{"type":"boolean"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"scene.export","description":"Publish an immutable scene-only package in a new project-relative directory. Does not register resources or change the project entry/save point. Single job; query the returned taskId.","inputSchema":{"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":512},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["path"],"additionalProperties":false}},
{"name":"scene.export.task.get","description":"Query the latest scene export task. A newly accepted export retires the previous result; exported_unregistered means no project registration occurred.","inputSchema":{"type":"object","properties":{"taskId":{"type":"integer","minimum":1},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["taskId"],"additionalProperties":false}},
{"name":"scene.duplicate","description":"Adopt the complete current draft as a new unsaved scene identity, preserving contents and referenced resources. Clears the authoring path and old history; source files stay unchanged. Busy guard applies.","inputSchema":{"type":"object","properties":{"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"additionalProperties":false}},
{"name":"scene.save","description":"Save the current authoring scene to a project-relative .arp.yaml path. Returns a taskId.","inputSchema":{"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":512},"overwriteExisting":{"type":"boolean"},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["path"],"additionalProperties":false}},
{"name":"scene.load","description":"Load and validate a project-relative authoring scene. Returns a taskId.","inputSchema":{"type":"object","properties":{"path":{"type":"string","minLength":1,"maxLength":512},"idempotencyKey":{"type":"string","maxLength":128},"expectedStateVersion":{"type":"integer","minimum":0},"discardChanges":{"type":"boolean"}},"required":["path"],"additionalProperties":false}},
{"name":"scene.task.get","description":"Read the status and diagnostic of a scene save/load task.","inputSchema":{"type":"object","properties":{"taskId":{"type":"integer","minimum":1},"expectedStateVersion":{"type":"integer","minimum":0}},"required":["taskId"],"additionalProperties":false}}
]}})json";
        // clang-format on
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
            : toolName == "project.create"                         ? hasOnlyMembers(*arguments,
                                                                         {"path",
                                                                             "manifestYaml",
                                                                             "template",
                                                                             "discardChanges",
                                                                             "idempotencyKey",
                                                                             "expectedStateVersion"})
            : toolName == "project.open"
            ? hasOnlyMembers(
                  *arguments, {"path", "discardChanges", "idempotencyKey", "expectedStateVersion"})
            : (toolName == "project.close" || toolName == "scene.new" || toolName == "editor.exit")
            ? hasOnlyMembers(
                  *arguments, {"discardChanges", "idempotencyKey", "expectedStateVersion"})
            : toolName == "project.rename"
            ? hasOnlyMembers(*arguments, {"name", "idempotencyKey", "expectedStateVersion"})
            : toolName == "project.settings.set"
            ? hasOnlyMembers(
                  *arguments, {"name", "entryScene", "idempotencyKey", "expectedStateVersion"})
            : (toolName == "project.save" || toolName == "scene.duplicate"
                  || toolName == "scene.selection.focus")
            ? hasOnlyMembers(*arguments, {"idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.reference.focus"  ? hasUniqueAllowedMembers(*arguments,
                                                         {"objectId",
                                                             "componentId",
                                                             "property",
                                                             "documentEpoch",
                                                             "expectedStateVersion",
                                                             "idempotencyKey"})
            : toolName == "scene.reference.assign" ? hasUniqueAllowedMembers(*arguments,
                                                         {"objectId",
                                                             "componentId",
                                                             "property",
                                                             "documentEpoch",
                                                             "resourceRevision",
                                                             "sourceKind",
                                                             "sourceObjectId",
                                                             "sourceResourceId",
                                                             "expectedStateVersion",
                                                             "idempotencyKey"})
            : toolName == "scene.resource.locate"  ? hasUniqueAllowedMembers(*arguments,
                                                         {"objectId",
                                                             "componentId",
                                                             "property",
                                                             "documentEpoch",
                                                             "resourceRevision",
                                                             "expectedStateVersion",
                                                             "idempotencyKey"})
            : toolName == "scene.transform.settings.get"
            ? hasUniqueAllowedMembers(*arguments, {"expectedStateVersion"})
            : toolName == "scene.transform.settings.set"
            ? hasUniqueAllowedMembers(
                  *arguments, {"settings", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.transform.pivot.set"
            ? hasOnlyMembers(*arguments, {"pivot", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.selection.transform" ? hasOnlyMembers(*arguments,
                                                            {"tool",
                                                                "space",
                                                                "pivot",
                                                                "axis",
                                                                "amount",
                                                                "idempotencyKey",
                                                                "expectedStateVersion"})
            : toolName == "scene.inspector.get"
            ? hasOnlyMembers(*arguments, {"objectId", "expectedStateVersion"})
            : (toolName == "scene.hierarchy.get" || toolName == "scene.component.catalog.get"
                  || toolName == "scene.inspector.selection.get")
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
            : toolName == "scene.select"
            ? hasOnlyMembers(*arguments, {"objectId", "expectedStateVersion"})
            : toolName == "scene.rename"
            ? hasOnlyMembers(
                  *arguments, {"objectId", "name", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.object.view.set" ? hasUniqueAllowedMembers(*arguments,
                                                        {"objectId",
                                                            "documentEpoch",
                                                            "hidden",
                                                            "locked",
                                                            "idempotencyKey",
                                                            "expectedStateVersion"})
            : toolName == "scene.active.set"
            ? hasOnlyMembers(
                  *arguments, {"objectId", "active", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.component.property.set" ? hasOnlyMembers(*arguments,
                                                               {"objectId",
                                                                   "componentId",
                                                                   "property",
                                                                   "kind",
                                                                   "value",
                                                                   "idempotencyKey",
                                                                   "expectedStateVersion"})
            : toolName == "editor.activate"              ? hasOnlyMembers(*arguments, {"source"})
            : (toolName == "editor.search" || toolName == "editor.text.set")
            ? hasOnlyMembers(*arguments, {"source", "value"})
            : toolName == "scene.load"
            ? hasOnlyMembers(
                  *arguments, {"path", "discardChanges", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.export"
            ? hasOnlyMembers(*arguments, {"path", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.export.task.get"
            ? hasOnlyMembers(*arguments, {"taskId", "expectedStateVersion"})
            : toolName == "scene.save"
            ? hasOnlyMembers(*arguments,
                  {"path", "overwriteExisting", "idempotencyKey", "expectedStateVersion"})
            : toolName == "scene.task.get"
            ? hasOnlyMembers(*arguments, {"taskId", "expectedStateVersion"})
            : (toolName == "script.reload" || toolName == "script.close")
            ? hasOnlyMembers(*arguments,
                  {"documentId", "discardChanges", "idempotencyKey", "expectedStateVersion"})
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
