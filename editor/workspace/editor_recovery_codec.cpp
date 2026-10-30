#include "editor/workspace/editor_recovery_codec.h"

#include "core/encoding/base64.h"
#include "core/encoding/yaml_contract.h"
#include "core/io/filesystem_path.h"
#include "core/text/utf8.h"
#include "editor/project/project_manifest.h"
#include "editor/project/project_metadata.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/editor_scene_file_format.h"
#include "editor/workspace/script_document_session.h"

#include <algorithm>
#include <limits>

namespace arp::editor {
namespace {
using Yaml = core::YamlContract;
using Node = Yaml::Node;

bool fail(core::pmr::String& diagnostic, const char* code)
{
    diagnostic = code;
    return false;
}

bool pathText(std::string_view text, std::size_t limit)
{
    return !text.empty() && text.size() <= limit && !core::Utf8Decoder::validate(text)
        && std::none_of(text.begin(), text.end(), [](unsigned char c) {
               return c < 32u;
           });
}

bool relativePath(std::string_view text, std::size_t limit)
{
    return pathText(text, limit) && core::FilePath::isSafeRelativePath(text, limit);
}

bool countBytes(std::size_t bytes, std::size_t& remaining)
{
    if (bytes > remaining)
        return false;
    remaining -= bytes;
    return true;
}

Node textNode(std::string_view text)
{
    return Node(Node::string_type(text.data(), text.size()));
}

Node optionalText(std::string_view text)
{
    return text.empty() ? Node() : textNode(text);
}

Node blob(std::string_view bytes)
{
    auto result = Node::mapping();
    result["encoding"] = "base64";
    result["bytes"] = static_cast<std::uint64_t>(bytes.size());
    const core::ByteBuffer data(bytes.begin(), bytes.end());
    result["data"] = textNode(core::Base64::encode(data));
    return result;
}

// Decode into a private snapshot. Check declared and encoded limits before
// allocating decoded buffers; YAML parser storage is bounded by envelope size.
class RecoveryReader final {
public:
    explicit RecoveryReader(EditorRecoveryBudget budget)
        : budget_(budget), remaining_(budget.maxPayloadBytes)
    {}

    core::pmr::String text(const Node& value, std::size_t limit)
    {
        const auto view = string(value);
        if (view.size() > limit || !countBytes(view.size(), remaining_))
            Yaml::fail();
        return core::pmr::String(view);
    }

    core::pmr::String optional(const Node& value, std::size_t limit)
    {
        if (value.is_null())
            return {};
        auto result = text(value, limit);
        if (result.empty())
            Yaml::fail();
        return result;
    }

    core::pmr::String bytes(const Node& value, std::size_t limit)
    {
        Yaml::onlyKeys(value, {"encoding", "bytes", "data"});
        if (value.size() != 3u || string(Yaml::required(value, "encoding")) != "base64")
            Yaml::fail();
        const auto declared = Yaml::uint64(Yaml::required(value, "bytes"));
        const auto encoded = string(Yaml::required(value, "data"));
        if (declared > limit || !countBytes(static_cast<std::size_t>(declared), remaining_)
            || encoded.size() != ((static_cast<std::size_t>(declared) + 2u) / 3u) * 4u)
            Yaml::fail();
        if (declared == 0u)
            return {};
        core::ByteBuffer decoded;
        if (!core::Base64::decode(encoded, decoded) || decoded.size() != declared
            || core::Base64::encode(decoded) != encoded)
            Yaml::fail();
        return core::pmr::String(reinterpret_cast<const char*>(decoded.data()), decoded.size());
    }

    std::size_t pathLimit() const noexcept { return budget_.maxPathBytes; }

    static std::size_t index(const Node& value)
    {
        const auto result = Yaml::uint64(value);
        if (result > std::numeric_limits<std::size_t>::max())
            Yaml::fail();
        return static_cast<std::size_t>(result);
    }

private:
    static std::string_view string(const Node& value)
    {
        if (!value.is_string())
            Yaml::fail();
        const auto& text = value.get_value_ref<const Node::string_type&>();
        return {text.data(), text.size()};
    }

    EditorRecoveryBudget budget_;
    std::size_t remaining_;
};
} // namespace

bool EditorRecoveryCodec::validateBudget(
    const EditorRecoveryBudget& budget, core::pmr::String& diagnostic)
{
    diagnostic.clear();
    const EditorRecoveryBudget hard;
    if (!budget.maxEnvelopeBytes || budget.maxEnvelopeBytes > hard.maxEnvelopeBytes
        || !budget.maxPayloadBytes || budget.maxPayloadBytes > hard.maxPayloadBytes
        || !budget.maxScripts || budget.maxScripts > hard.maxScripts || !budget.maxPathBytes
        || budget.maxPathBytes > hard.maxPathBytes || budget.slotsPerSession != hard.slotsPerSession
        || !budget.maxSessions || budget.maxSessions > hard.maxSessions
        || budget.maxRegionBytes <= EditorRecoveryBudget::maxRegionMetadataBytes
        || budget.maxRegionBytes > hard.maxRegionBytes || !budget.snapshotIntervalSeconds
        || budget.snapshotIntervalSeconds > hard.snapshotIntervalSeconds
        || !budget.maxRegionPublishAttempts
        || budget.maxRegionPublishAttempts > hard.maxRegionPublishAttempts
        || budget.regionPublishRetryMilliseconds > hard.regionPublishRetryMilliseconds
        || budget.maxEnvelopeBytes
            > (budget.maxRegionBytes - EditorRecoveryBudget::maxRegionMetadataBytes)
                / (budget.slotsPerSession + 1u) / budget.maxSessions)
        return fail(diagnostic, "EDITOR-RECOVERY-BUDGET-INVALID");
    return true;
}

bool EditorRecoveryCodec::validate(
    const EditorRecoverySnapshot& value, core::pmr::String& diagnostic, EditorRecoveryBudget budget)
{
    if (!validateBudget(budget, diagnostic))
        return false;
    try {
        if (!resource::isResourceUuid(value.sessionId) || !value.sequence || !value.workspaceEpoch
            || value.sequence > INT64_MAX || value.workspaceEpoch > INT64_MAX)
            return fail(diagnostic, "EDITOR-RECOVERY-IDENTITY-INVALID");
        if (!pathText(value.projectRoot, budget.maxPathBytes))
            return fail(diagnostic, "EDITOR-RECOVERY-ROOT-INVALID");
        const auto root = std::filesystem::u8path(value.projectRoot);
        if (!root.is_absolute() || root != root.lexically_normal())
            return fail(diagnostic, "EDITOR-RECOVERY-ROOT-INVALID");
        if ((value.workspace != "Game" && value.workspace != "3D" && value.workspace != "Script")
            || value.scripts.size() > budget.maxScripts
            || (value.activeScript && *value.activeScript >= value.scripts.size())
            || (value.workspace == "Script" && !value.activeScript))
            return fail(diagnostic, "EDITOR-RECOVERY-WORKSPACE-INVALID");

        auto remaining = budget.maxPayloadBytes;
        const auto count = [&](std::string_view text, std::size_t limit) {
            return text.size() <= limit && countBytes(text.size(), remaining);
        };
        if (!count(value.sessionId, 36u) || !count(value.projectRoot, budget.maxPathBytes)
            || !count(value.workspace, 6u))
            return fail(diagnostic, "EDITOR-RECOVERY-PAYLOAD-LIMIT");
        if (value.project) {
            const auto& project = *value.project;
            if (!count(project.id, 36u)
                || !count(project.draftYaml, ProjectManifestBudget{}.maxDocumentBytes)
                || !count(project.savedFileBytes, ProjectManifestBudget{}.maxDocumentBytes)
                || project.packages.size() > ProjectManifestBudget{}.maxAssetPackages)
                return fail(diagnostic, "EDITOR-RECOVERY-PROJECT-LIMIT");
            ProjectManifest draft, saved;
            ProjectDiagnostic issue;
            if (!ProjectManifestCodec::decode(project.draftYaml, draft, issue)
                || !ProjectManifestCodec::decode(project.savedFileBytes, saved, issue)
                || project.id != draft.id || project.id != saved.id
                || draft.assetPackages != saved.assetPackages
                || project.packages.size() != saved.assetPackages.size())
                return fail(diagnostic, "EDITOR-RECOVERY-PROJECT-INVALID");
            if (draft.editorMetadata != saved.editorMetadata
                || project.metadata.has_value() != !saved.editorMetadata.empty())
                return fail(diagnostic, "EDITOR-RECOVERY-METADATA-BINDING-INVALID");
            if (project.metadata) {
                const auto& metadata = *project.metadata;
                ProjectMetadata parsed;
                if (metadata.path != saved.editorMetadata
                    || !relativePath(metadata.path, budget.maxPathBytes)
                    || !count(metadata.path, budget.maxPathBytes)
                    || !count(metadata.fileBytes, maxMetadataBytes)
                    || !ProjectMetadataCodec::decode(metadata.fileBytes, parsed, issue))
                    return fail(diagnostic, "EDITOR-RECOVERY-METADATA-INVALID");
            }
            if (!ProjectInputSnapshot::validateBindings(project.inputFiles, diagnostic))
                return false;
            for (const auto& input : project.inputFiles)
                if (!relativePath(input.path, budget.maxPathBytes)
                    || !count(input.path, budget.maxPathBytes) || !count(input.fingerprint, 71u))
                    return fail(diagnostic, "EDITOR-RECOVERY-INPUT-LIMIT");
            for (std::size_t i = 0; i < project.packages.size(); ++i) {
                const auto& package = project.packages[i];
                if (!relativePath(package.directory, budget.maxPathBytes)
                    || package.directory != saved.assetPackages[i]
                    || !EditorSceneAuthoringCodec::isContentHash(package.fingerprint)
                    || !count(package.directory, budget.maxPathBytes)
                    || !count(package.fingerprint, 71u))
                    return fail(diagnostic, "EDITOR-RECOVERY-PACKAGE-INVALID");
            }
        }
        const auto& scene = value.scene;
        if (!count(scene.yaml, EditorSceneAuthoringCodec::maxDocumentBytes)
            || !count(scene.savedFileBytes, EditorSceneAuthoringCodec::maxDocumentBytes)
            || !count(scene.path, budget.maxPathBytes)
            || !count(scene.boundTarget, budget.maxPathBytes)
            || !count(scene.savedContentHash, 71u))
            return fail(diagnostic, "EDITOR-RECOVERY-SCENE-LIMIT");
        const auto sceneVersion = EditorSceneFileFormat::version(scene.yaml);
        if (sceneVersion == EditorSceneScriptCodec::contractVersion && !value.project)
            return fail(diagnostic, "EDITOR-RECOVERY-SCRIPT-PROJECT-REQUIRED");
        if (!EditorSceneFileFormat::validateStructure(scene.yaml, diagnostic))
            return false;
        const bool bound = !scene.path.empty();
        if (bound != !scene.boundTarget.empty() || bound != !scene.savedContentHash.empty()
            || bound != !scene.savedFileBytes.empty()
            || (bound
                && (!relativePath(scene.path, budget.maxPathBytes)
                    || !relativePath(scene.boundTarget, budget.maxPathBytes)
                    || !EditorSceneAuthoringCodec::isContentHash(scene.savedContentHash))))
            return fail(diagnostic, "EDITOR-RECOVERY-SCENE-BINDING-INVALID");
        // A saved hash describes the decoded live document, not wire YAML
        // spelling. Resource-ready candidate preparation must check it against
        // the restored baseline; this codec only validates its schema.
        if (bound) {
            if (EditorSceneFileFormat::version(scene.savedFileBytes) != sceneVersion)
                return fail(diagnostic, "EDITOR-RECOVERY-SCENE-FORMAT-MISMATCH");
            if (!EditorSceneFileFormat::validateStructure(scene.savedFileBytes, diagnostic))
                return false;
        }
        for (std::size_t i = 0; i < value.scripts.size(); ++i) {
            const auto& script = value.scripts[i];
            if (!relativePath(script.path, budget.maxPathBytes)
                || !ScriptDocumentSession::isScriptPath(std::filesystem::u8path(script.path))
                || !count(script.path, budget.maxPathBytes)
                || !count(script.text, ScriptDocumentSession::maxScriptBytes)
                || !count(script.savedText, ScriptDocumentSession::maxScriptBytes)
                || !count(script.savedFileBytes, ScriptDocumentSession::maxScriptBytes)
                || !count(script.newline, 2u))
                return fail(diagnostic, "EDITOR-RECOVERY-SCRIPT-LIMIT-OR-PATH");
            if (script.cursor > script.text.size() || script.anchor > script.text.size()
                || script.scrollLine > INT64_MAX
                || (script.newline != "\n" && script.newline != "\r\n")
                || ScriptDocumentSession::normalizeFileBytes(
                       script.savedFileBytes, core::TextNulPolicy::Preserve)
                    != script.savedText
                || script.newline
                    != (script.savedFileBytes.find("\r\n") != core::pmr::String::npos ? "\r\n"
                                                                                      : "\n"))
                return fail(diagnostic, "EDITOR-RECOVERY-SCRIPT-STATE-INVALID");
            for (std::size_t j = 0; j < i; ++j)
                if (value.scripts[j].path == script.path)
                    return fail(diagnostic, "EDITOR-RECOVERY-SCRIPT-DUPLICATE");
        }
        return true;
    } catch (...) {
        return fail(diagnostic, "EDITOR-RECOVERY-VALIDATION-FAILED");
    }
}

bool EditorRecoveryCodec::encode(const EditorRecoverySnapshot& value,
    core::pmr::String& destination,
    core::pmr::String& diagnostic,
    EditorRecoveryBudget budget)
{
    if (!validate(value, diagnostic, budget))
        return false;
    try {
        auto root = Node::mapping();
        root["type"] = "arp.editor-recovery";
        root["contractVersion"] = contractVersion;
        root["sessionId"] = textNode(value.sessionId);
        root["sequence"] = value.sequence;
        root["workspaceEpoch"] = value.workspaceEpoch;
        root["projectRoot"] = textNode(value.projectRoot);
        root["workspace"] = textNode(value.workspace);
        root["activeScript"] =
            value.activeScript ? Node(static_cast<std::uint64_t>(*value.activeScript)) : Node();
        auto& project = root["project"];
        if (value.project) {
            project = Node::mapping();
            project["id"] = textNode(value.project->id);
            project["draft"] = blob(value.project->draftYaml);
            project["disk"] = blob(value.project->savedFileBytes);
            project["metadata"] = Node();
            if (value.project->metadata) {
                auto& metadata = project["metadata"];
                metadata = Node::mapping();
                metadata["path"] = textNode(value.project->metadata->path);
                metadata["disk"] = blob(value.project->metadata->fileBytes);
            }
            project["inputs"] = Node::sequence();
            for (const auto& input : value.project->inputFiles) {
                auto item = Node::mapping();
                item["path"] = textNode(input.path);
                item["bytes"] = static_cast<std::uint64_t>(input.byteCount);
                item["fingerprint"] = textNode(input.fingerprint);
                project["inputs"].get_value_ref<Node::sequence_type&>().push_back(std::move(item));
            }
            project["packages"] = Node::sequence();
            for (const auto& package : value.project->packages) {
                auto item = Node::mapping();
                item["directory"] = textNode(package.directory);
                item["fingerprint"] = textNode(package.fingerprint);
                project["packages"].get_value_ref<Node::sequence_type&>().push_back(
                    std::move(item));
            }
        }
        auto& scene = root["scene"];
        scene = Node::mapping();
        scene["draft"] = blob(value.scene.yaml);
        scene["savedContentHash"] = optionalText(value.scene.savedContentHash);
        scene["path"] = optionalText(value.scene.path);
        scene["boundTarget"] = optionalText(value.scene.boundTarget);
        scene["disk"] = value.scene.path.empty() ? Node() : blob(value.scene.savedFileBytes);
        root["scripts"] = Node::sequence();
        for (const auto& script : value.scripts) {
            auto item = Node::mapping();
            item["path"] = textNode(script.path);
            item["text"] = blob(script.text);
            item["savedText"] = blob(script.savedText);
            item["disk"] = blob(script.savedFileBytes);
            item["newline"] = textNode(script.newline);
            item["cursor"] = static_cast<std::uint64_t>(script.cursor);
            item["anchor"] = static_cast<std::uint64_t>(script.anchor);
            item["scrollLine"] = static_cast<std::uint64_t>(script.scrollLine);
            root["scripts"].get_value_ref<Node::sequence_type&>().push_back(std::move(item));
        }
        auto encoded = Yaml::serialize(root);
        if (encoded.size() > budget.maxEnvelopeBytes)
            return fail(diagnostic, "EDITOR-RECOVERY-ENVELOPE-LIMIT");
        destination = std::move(encoded);
        return true;
    } catch (...) {
        return fail(diagnostic, "EDITOR-RECOVERY-ENCODE-FAILED");
    }
}

bool EditorRecoveryCodec::decode(std::string_view yaml,
    EditorRecoverySnapshot& destination,
    core::pmr::String& diagnostic,
    EditorRecoveryBudget budget)
{
    if (!validateBudget(budget, diagnostic))
        return false;
    if (yaml.empty() || yaml.size() > budget.maxEnvelopeBytes)
        return fail(diagnostic, "EDITOR-RECOVERY-ENVELOPE-LIMIT");
    const char* stage = "EDITOR-RECOVERY-FIELD-INVALID";
    try {
        const auto root = Yaml::parseSingleDocument(yaml);
        if (Yaml::text(root, "type") != "arp.editor-recovery"
            || Yaml::uint32(root, "contractVersion") != contractVersion)
            return fail(diagnostic, "EDITOR-RECOVERY-CONTRACT-UNSUPPORTED");
        Yaml::onlyKeys(root,
            {"type",
                "contractVersion",
                "sessionId",
                "sequence",
                "workspaceEpoch",
                "projectRoot",
                "workspace",
                "activeScript",
                "project",
                "scene",
                "scripts"});
        RecoveryReader reader(budget);
        EditorRecoverySnapshot candidate;
        candidate.sessionId = reader.text(Yaml::required(root, "sessionId"), 36u);
        candidate.sequence = Yaml::uint64(Yaml::required(root, "sequence"));
        candidate.workspaceEpoch = Yaml::uint64(Yaml::required(root, "workspaceEpoch"));
        candidate.projectRoot =
            reader.text(Yaml::required(root, "projectRoot"), reader.pathLimit());
        candidate.workspace = reader.text(Yaml::required(root, "workspace"), 6u);
        const auto& active = Yaml::required(root, "activeScript");
        if (!active.is_null())
            candidate.activeScript = RecoveryReader::index(active);
        const auto& project = Yaml::required(root, "project");
        if (!project.is_null()) {
            Yaml::onlyKeys(project, {"id", "draft", "disk", "packages", "metadata", "inputs"});
            candidate.project.emplace();
            auto& value = *candidate.project;
            value.id = reader.text(Yaml::required(project, "id"), 36u);
            value.draftYaml = reader.bytes(
                Yaml::required(project, "draft"), ProjectManifestBudget{}.maxDocumentBytes);
            value.savedFileBytes = reader.bytes(
                Yaml::required(project, "disk"), ProjectManifestBudget{}.maxDocumentBytes);
            const auto& metadata = Yaml::required(project, "metadata");
            if (!metadata.is_null()) {
                Yaml::onlyKeys(metadata, {"path", "disk"});
                value.metadata.emplace();
                value.metadata->path =
                    reader.text(Yaml::required(metadata, "path"), reader.pathLimit());
                value.metadata->fileBytes =
                    reader.bytes(Yaml::required(metadata, "disk"), maxMetadataBytes);
            }
            const auto& inputs = Yaml::sequence(project, "inputs");
            if (inputs.size() > ProjectInputBudget{}.maxFiles)
                Yaml::fail();
            for (const auto& item : inputs) {
                Yaml::onlyKeys(item, {"path", "bytes", "fingerprint"});
                ProjectInputBinding input;
                input.path = reader.text(Yaml::required(item, "path"), reader.pathLimit());
                input.byteCount = RecoveryReader::index(Yaml::required(item, "bytes"));
                input.fingerprint = reader.text(Yaml::required(item, "fingerprint"), 71u);
                value.inputFiles.push_back(std::move(input));
            }
            const auto& packages = Yaml::sequence(project, "packages");
            if (packages.size() > ProjectManifestBudget{}.maxAssetPackages)
                Yaml::fail();
            for (const auto& item : packages) {
                Yaml::onlyKeys(item, {"directory", "fingerprint"});
                EditorRecoveryPackage package;
                package.directory =
                    reader.text(Yaml::required(item, "directory"), reader.pathLimit());
                package.fingerprint = reader.text(Yaml::required(item, "fingerprint"), 71u);
                value.packages.push_back(std::move(package));
            }
        }
        stage = "EDITOR-RECOVERY-SCENE-FIELD-INVALID";
        const auto& scene = Yaml::required(root, "scene");
        Yaml::onlyKeys(scene, {"draft", "savedContentHash", "path", "boundTarget", "disk"});
        candidate.scene.yaml = reader.bytes(
            Yaml::required(scene, "draft"), EditorSceneAuthoringCodec::maxDocumentBytes);
        candidate.scene.savedContentHash =
            reader.optional(Yaml::required(scene, "savedContentHash"), 71u);
        candidate.scene.path = reader.optional(Yaml::required(scene, "path"), reader.pathLimit());
        candidate.scene.boundTarget =
            reader.optional(Yaml::required(scene, "boundTarget"), reader.pathLimit());
        const auto& sceneDisk = Yaml::required(scene, "disk");
        if (!sceneDisk.is_null()) {
            if (candidate.scene.path.empty())
                Yaml::fail();
            candidate.scene.savedFileBytes =
                reader.bytes(sceneDisk, EditorSceneAuthoringCodec::maxDocumentBytes);
        }
        stage = "EDITOR-RECOVERY-SCRIPT-FIELD-INVALID";
        const auto& scripts = Yaml::sequence(root, "scripts");
        if (scripts.size() > budget.maxScripts)
            Yaml::fail();
        for (const auto& item : scripts) {
            Yaml::onlyKeys(item,
                {"path", "text", "savedText", "disk", "newline", "cursor", "anchor", "scrollLine"});
            EditorRecoveryScript script;
            script.path = reader.text(Yaml::required(item, "path"), reader.pathLimit());
            script.text =
                reader.bytes(Yaml::required(item, "text"), ScriptDocumentSession::maxScriptBytes);
            script.savedText = reader.bytes(
                Yaml::required(item, "savedText"), ScriptDocumentSession::maxScriptBytes);
            script.savedFileBytes =
                reader.bytes(Yaml::required(item, "disk"), ScriptDocumentSession::maxScriptBytes);
            script.newline = reader.text(Yaml::required(item, "newline"), 2u);
            script.cursor = RecoveryReader::index(Yaml::required(item, "cursor"));
            script.anchor = RecoveryReader::index(Yaml::required(item, "anchor"));
            script.scrollLine = RecoveryReader::index(Yaml::required(item, "scrollLine"));
            candidate.scripts.push_back(std::move(script));
        }
        if (!validate(candidate, diagnostic, budget))
            return false;
        destination = std::move(candidate);
        return true;
    } catch (...) {
        return fail(diagnostic, stage);
    }
}

bool EditorRecoveryCodec::migrateVersion1(std::string_view yaml,
    core::pmr::String& destination,
    core::pmr::String& diagnostic,
    EditorRecoveryBudget budget)
{
    if (!validateBudget(budget, diagnostic))
        return false;
    if (yaml.empty() || yaml.size() > budget.maxEnvelopeBytes)
        return fail(diagnostic, "EDITOR-RECOVERY-ENVELOPE-LIMIT");
    try {
        auto root = Yaml::parseSingleDocument(yaml);
        if (Yaml::text(root, "type") != "arp.editor-recovery"
            || Yaml::uint32(root, "contractVersion") != 1u)
            return fail(diagnostic, "EDITOR-RECOVERY-MIGRATION-SOURCE-UNSUPPORTED");
        Yaml::required(root, "project");
        auto& project = root["project"];
        if (!project.is_null()) {
            Yaml::onlyKeys(project, {"id", "draft", "disk", "packages"});
            project["metadata"] = Node();
            project["inputs"] = Node::sequence();
        }
        root["contractVersion"] = contractVersion;
        EditorRecoverySnapshot candidate;
        if (!decode(Yaml::serialize(root), candidate, diagnostic, budget))
            return false;
        if (candidate.project) {
            ProjectManifest saved, draft;
            ProjectDiagnostic issue;
            if (!ProjectManifestCodec::decode(candidate.project->savedFileBytes, saved, issue)
                || !ProjectManifestCodec::decode(candidate.project->draftYaml, draft, issue)
                || !saved.editorMetadata.empty() || !draft.editorMetadata.empty()
                || !saved.scriptModules.empty() || !draft.scriptModules.empty())
                return fail(diagnostic, "EDITOR-RECOVERY-V1-EXTERNAL-INPUTS-UNBOUND");
        }
        return encode(candidate, destination, diagnostic, budget);
    } catch (...) {
        return fail(diagnostic, "EDITOR-RECOVERY-MIGRATION-FAILED");
    }
}

} // namespace arp::editor
