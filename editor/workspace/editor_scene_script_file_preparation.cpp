#include "editor/workspace/editor_scene_script_file_preparation.h"

#include "core/io/filesystem_path.h"
#include "editor/workspace/editor_scene_persistence.h"

#include <exception>

namespace arp::editor {
namespace {
bool projectSnapshot(
    const EditorSceneScriptInputs& input, core::pmr::String& yaml, core::pmr::String& diagnostic)
{
    ProjectDiagnostic issue;
    if (!ProjectManifestCodec::encode(input.project, yaml, issue)) {
        diagnostic = "EDITOR-SCRIPT-FILE-PROJECT-INVALID";
        return false;
    }
    return true;
}

bool resolve(const EditorSceneScriptInputs& input,
    std::string_view relative,
    bool existed,
    core::pmr::String& path,
    core::pmr::String& diagnostic)
{
    if (relative.empty() || relative.size() > ProjectInputBudget::maxPathBytes) {
        diagnostic = "EDITOR-SCRIPT-FILE-PATH-BUDGET";
        return false;
    }
    std::filesystem::path resolved;
    const char* issue = nullptr;
    if (!EditorScenePersistence::resolvePath(
            input.projectRoot, relative, existed, resolved, issue)) {
        diagnostic = issue ? issue : "EDITOR-SCRIPT-FILE-PATH";
        return false;
    }
    path = core::FilePath::utf8(resolved);
    return true;
}
} // namespace

bool EditorSceneScriptFilePreparation::prepare(const EditorSceneDocument& document,
    const EditorSceneScriptInputs& input,
    Request request,
    core::UniquePtr<Prepared>& output,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        if (!input.preparationBytes) {
            diagnostic = "EDITOR-SCRIPT-FILE-PREPARATION-BUDGET";
            return false;
        }
        core::CpuMemoryQuotaScope quota(
            core::makeShared<core::CpuMemoryQuota>(input.preparationBytes));
        if ((!request.targetExisted && !request.previousBytes.empty())
            || (request.targetExisted && request.previousBytes.empty())
            || request.previousBytes.size() > EditorSceneAuthoringCodec::maxDocumentBytes) {
            diagnostic = "EDITOR-SCRIPT-FILE-PREVIOUS-BYTES";
            return false;
        }
        auto prepared = core::makeUnique<Prepared>();
        if (!resolve(
                input, request.relativePath, request.targetExisted, prepared->path_, diagnostic)
            || !projectSnapshot(input, prepared->project_, diagnostic))
            return false;
        if (request.targetExisted) {
            core::UniquePtr<EditorSceneDocument> previous;
            if (!EditorSceneScriptCodec::prepare(
                    request.previousBytes, document, input, previous, diagnostic))
                return false;
        }
        core::pmr::String yaml;
        if (!EditorSceneScriptCodec::encode(document, input, yaml, diagnostic)
            || !EditorSceneScriptCodec::contentHash(document, input, prepared->hash_, diagnostic))
            return false;
        prepared->previous_.assign(request.previousBytes.begin(), request.previousBytes.end());
        prepared->replacement_.assign(yaml.begin(), yaml.end());
        prepared->relativePath_ = request.relativePath;
        prepared->document_ = document.ownerTicket();
        prepared->targetExisted_ = request.targetExisted;
        prepared->ready_ = true;
        if (!prepared->current(document, input, diagnostic))
            return false;
        output = std::move(prepared);
        return true;
    } catch (const core::YamlContractError& error) {
        diagnostic = error.diagnostic;
        return false;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        return false;
    }
}

bool EditorSceneScriptFilePreparation::Prepared::current(const EditorSceneDocument& document,
    const EditorSceneScriptInputs& input,
    core::pmr::String& diagnostic) const
{
    diagnostic.clear();
    try {
        if (!ready_ || !document_.sameCurrentEpoch(document.ownerTicket())) {
            diagnostic = "EDITOR-SCRIPT-FILE-DOCUMENT-STALE";
            return false;
        }
        if (!input.preparationBytes) {
            diagnostic = "EDITOR-SCRIPT-FILE-PREPARATION-BUDGET";
            return false;
        }
        core::CpuMemoryQuotaScope quota(
            core::makeShared<core::CpuMemoryQuota>(input.preparationBytes));
        core::pmr::String project, path, hash;
        if (!projectSnapshot(input, project, diagnostic)
            || !resolve(input, relativePath_, targetExisted_, path, diagnostic))
            return false;
        if (path != path_ || project != project_) {
            diagnostic = "EDITOR-SCRIPT-FILE-CONTEXT-STALE";
            return false;
        }
        if (!EditorSceneScriptCodec::contentHash(document, input, hash, diagnostic))
            return false;
        if (hash != hash_) {
            diagnostic = "EDITOR-SCRIPT-FILE-CONTENT-STALE";
            return false;
        }
        // Hash equality detects edits, but never substitutes for durable
        // admission: Ready resources and the actual cold domain are rechecked.
        core::pmr::String verified;
        return EditorSceneScriptCodec::encode(document, input, verified, diagnostic);
    } catch (const core::YamlContractError& error) {
        diagnostic = error.diagnostic;
        return false;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        return false;
    }
}
} // namespace arp::editor
