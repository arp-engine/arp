#include "editor/app/editor_workspace_connection_profile.h"

#include "core/encoding/yaml_contract.h"
#include "core/text/utf8.h"

#include <limits>

namespace arp::editor {
namespace {
using Y = core::YamlContract;

std::size_t size(const Y::Node& node, const char* key)
{
    const auto value = Y::uint64(Y::required(node, key));
    if (value > SIZE_MAX)
        throw core::YamlContractError{"EDITOR-WORKSPACE-PROFILE-SIZE"};
    return static_cast<std::size_t>(value);
}
} // namespace

bool EditorWorkspaceConnectionProfile::validBudget(
    const core::io::WorkspaceFileBackendDesc& d, const EditorProjectIoBudget& b) noexcept
{
    return EditorProjectIoTask::validBudget(b) && d.maxFileBytes >= b.inputs.maxFileBytes
        && d.maxQueryBytes >= b.directory.maxPageBytes
        && d.maxDirectoryEntries >= b.directory.maxEntries
        && d.maxDirectoryNameBytes >= b.directory.maxNameBytes
        && d.maxPathBytes >= ProjectInputBudget::maxPathBytes
        && d.maxFileBytes <= std::numeric_limits<std::uint64_t>::max() / 2;
}

core::UniquePtr<EditorWorkspaceConnectionProfile> EditorWorkspaceConnectionProfile::decode(
    std::string_view yaml,
    std::string_view token,
    std::size_t maxBytes,
    const char*& diagnostic) noexcept
{
    diagnostic = "EDITOR-WORKSPACE-PROFILE-BYTE-BUDGET";
    if (!maxBytes || yaml.empty() || yaml.size() > maxBytes)
        return {};
    diagnostic = "EDITOR-WORKSPACE-PROFILE-ENCODING";
    if (yaml.find('\0') != std::string_view::npos
        || core::Utf8Decoder::visit(yaml, [](std::uint32_t) noexcept {}) != nullptr)
        return {};
    try {
        const auto root = Y::parseSingleDocument(yaml);
        Y::onlyKeys(root,
            {"type",
                "contractVersion",
                "endpoint",
                "credentialOrigin",
                "workspaceId",
                "projectRoot",
                "io",
                "editor"});
        diagnostic = "EDITOR-WORKSPACE-PROFILE-CONTRACT";
        if (Y::text(root, "type") != "arp.editor-workspace-connection"
            || Y::uint64(Y::required(root, "contractVersion")) != 1)
            return {};
        auto value = core::makeUnique<EditorWorkspaceConnectionProfile>();
        auto& d = value->io;
        auto& b = value->editor;
        d.endpoint = Y::text(root, "endpoint");
        d.credentialOrigin = Y::text(root, "credentialOrigin");
        d.workspaceId = Y::text(root, "workspaceId");
        d.token = token;
        value->projectRoot = Y::text(root, "projectRoot");
        diagnostic = "EDITOR-WORKSPACE-PROFILE-PROJECT-ROOT";
        if (!value->projectRoot.empty()
            && !ProjectInputSnapshot::validLogicalPath(value->projectRoot))
            return {};
        const auto& io = Y::required(root, "io");
        Y::onlyKeys(io,
            {"maxPathBytes",
                "maxFileBytes",
                "maxRequestBytes",
                "maxResponseBytes",
                "preparationBytes",
                "maxQueryBytes",
                "maxDirectoryEntries",
                "maxDirectoryNameBytes",
                "maxHeaderBytes",
                "ioChunkBytes",
                "timeoutMs",
                "cancellationPollMs",
                "maxDnsAddresses"});
        d.maxPathBytes = size(io, "maxPathBytes");
        d.maxFileBytes = size(io, "maxFileBytes");
        d.maxRequestBytes = size(io, "maxRequestBytes");
        d.maxResponseBytes = size(io, "maxResponseBytes");
        d.preparationBytes = size(io, "preparationBytes");
        d.maxQueryBytes = size(io, "maxQueryBytes");
        d.maxDirectoryEntries = Y::uint32(io, "maxDirectoryEntries");
        d.maxDirectoryNameBytes = size(io, "maxDirectoryNameBytes");
        d.maxHeaderBytes = size(io, "maxHeaderBytes");
        d.ioChunkBytes = size(io, "ioChunkBytes");
        d.timeoutMs = Y::uint32(io, "timeoutMs");
        d.cancellationPollMs = Y::uint32(io, "cancellationPollMs");
        d.maxDnsAddresses = Y::uint32(io, "maxDnsAddresses");
        const auto& editor = Y::required(root, "editor");
        Y::onlyKeys(editor,
            {"inputs",
                "directory",
                "maxDecodedBytes",
                "maxDiagnosticBytes",
                "maxHistoryEntries",
                "maxScriptDocuments",
                "scriptReload"});
        const auto& inputs = Y::required(editor, "inputs");
        Y::onlyKeys(inputs, {"maxFiles", "maxFileBytes", "maxTotalBytes"});
        b.inputs = {
            size(inputs, "maxFiles"), size(inputs, "maxFileBytes"), size(inputs, "maxTotalBytes")};
        const auto& directory = Y::required(editor, "directory");
        Y::onlyKeys(directory, {"maxEntries", "pageSize", "maxNameBytes", "maxPageBytes"});
        b.directory = {Y::uint32(directory, "maxEntries"),
            Y::uint32(directory, "pageSize"),
            size(directory, "maxNameBytes"),
            size(directory, "maxPageBytes")};
        b.maxDecodedBytes = size(editor, "maxDecodedBytes");
        b.maxDiagnosticBytes = size(editor, "maxDiagnosticBytes");
        b.maxHistoryEntries = size(editor, "maxHistoryEntries");
        b.maxScriptDocuments = size(editor, "maxScriptDocuments");
        const auto& reload = Y::required(editor, "scriptReload");
        Y::onlyKeys(reload, {"maxHistoryEntries", "maxHistoryBytes"});
        b.scriptReload = {size(reload, "maxHistoryEntries"), size(reload, "maxHistoryBytes")};
        diagnostic = "EDITOR-WORKSPACE-PROFILE-INCONSISTENT-BUDGET";
        if (!validBudget(d, b))
            return {};
        // This is metadata preparation. WorkspaceFileBackend remains the single
        // transport/origin/token validator at connect, before any network IO.
        diagnostic = nullptr;
        return value;
    } catch (const std::bad_alloc&) {
        diagnostic = "EDITOR-WORKSPACE-PROFILE-ALLOCATION";
    } catch (...) {
        diagnostic = "EDITOR-WORKSPACE-PROFILE-YAML";
    }
    return {};
}
} // namespace arp::editor
