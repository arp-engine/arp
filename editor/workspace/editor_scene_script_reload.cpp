#include "editor/workspace/editor_scene_script_reload.h"

#include "core/hash/sha256.h"

namespace arp::editor {

core::UniquePtr<EditorSceneScriptReload> EditorSceneScriptReload::prepare(
    const EditorSceneDocument& document,
    const EditorSceneScriptInputs& input,
    std::string_view relative,
    std::string_view previousBytes,
    const EditorSceneScriptReload* previousProof,
    core::pmr::String& diagnostic)
{
    diagnostic.clear();
    try {
        const auto* retained = document.retainedScriptSources();
        const char* issue = nullptr;
        if (!input.preparationBytes || !input.sources || !input.sources->validate(issue)
            || !retained || !retained->validate(issue) || retained->files != input.sources->files
            || !(retained->snapshot->location().mount == input.sources->snapshot->location().mount)
            || retained->snapshot->location().root != input.sources->snapshot->location().root
            || retained->epoch != input.sources->epoch) {
            diagnostic = issue ? issue : "EDITOR-SCRIPT-RELOAD-SOURCE-OWNER";
            return {};
        }
        core::CpuMemoryQuotaScope quota(
            core::makeShared<core::CpuMemoryQuota>(input.preparationBytes));
        if (previousBytes.size() > EditorSceneAuthoringCodec::maxDocumentBytes
            || (!relative.empty() && !ProjectInputSnapshot::validLogicalPath(relative))
            || (!previousBytes.empty() && relative.empty())) {
            diagnostic = "EDITOR-SCRIPT-RELOAD-BASELINE";
            return {};
        }
        if (!previousBytes.empty()
            && !(previousProof
                && previousProof->allowsPrevious(document, input, relative, previousBytes))) {
            // Prove the exact saved archive against the retained old acquired
            // corpus. These bytes are not asserted to be current on disk; the
            // later conditional save still checks its full raw baseline.
            auto old = input;
            old.sources = retained;
            core::UniquePtr<EditorSceneDocument> verified;
            if (!EditorSceneScriptCodec::prepare(
                    previousBytes, document, old, verified, diagnostic))
                return {};
        }
        auto result = core::makeUnique<EditorSceneScriptReload>();
        if (!EditorSceneScriptCodec::prepareReload(document, input, result->candidate_, diagnostic))
            return {};
        result->scene_ = &result->candidate_->scene();
        result->catalog_ = result->candidate_->scriptCatalog().get();
        result->files_ = input.sources->files;
        result->location_ = input.sources->snapshot->location();
        result->relative_ = relative;
        if (!previousBytes.empty())
            result->previousHash_ = core::Sha256::hexDigest(previousBytes);
        return result;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        return {};
    }
}

void EditorSceneScriptReload::published(const EditorSceneDocument& document) noexcept
{
    if (published_ || !candidate_ || scene_ != &document.scene()
        || catalog_ != document.scriptCatalog().get())
        return;
    owner_ = document.ownerTicket();
    published_ = true;
    candidate_.reset();
}

bool EditorSceneScriptReload::allowsPrevious(const EditorSceneDocument& document,
    const EditorSceneScriptInputs& input,
    std::string_view relative,
    std::string_view bytes) const
{
    return published_ && !previousHash_.empty() && owner_.sameCurrentEpoch(document.ownerTicket())
        && scene_ == &document.scene() && catalog_ == document.scriptCatalog().get()
        && input.sources && input.sources->files == files_ && input.sources->snapshot
        && input.sources->epoch == location_.epoch
        && input.sources->snapshot->location().mount == location_.mount
        && input.sources->snapshot->location().root == location_.root && relative == relative_
        && bytes.size() <= EditorSceneAuthoringCodec::maxDocumentBytes
        && core::Sha256::hexDigest(bytes) == previousHash_;
}
} // namespace arp::editor
