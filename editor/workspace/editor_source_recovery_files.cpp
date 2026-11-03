#include "editor/workspace/editor_source_recovery_files.h"

#include "editor/project/project_file_read_task.h"
#include "editor/workspace/editor_scene_authoring_codec.h"
#include "editor/workspace/script_document_session.h"

#include <algorithm>

namespace arp::editor {
using namespace core::io;

struct EditorSourceRecoveryFiles::State {
    ProjectSourceContext context;
    ProjectInputBudget budget;
    core::pmr::Vector<EditorRecoverySourceFile> files;
    core::pmr::Vector<std::size_t> limits;
    core::UniquePtr<ProjectFileReadTask> read;
    RecoverySourceFilesState phase = RecoverySourceFilesState::Pending;
    const char* diagnostic = nullptr;
    std::size_t next = 0, retainedBytes = 0;

    void fail(const char* issue) noexcept
    {
        read.reset();
        phase = RecoverySourceFilesState::Failed;
        diagnostic = issue ? issue : "EDITOR-RECOVERY-SOURCE-FILE-READ";
        core::pmr::Vector<EditorRecoverySourceFile>{}.swap(files);
        core::pmr::Vector<std::size_t>{}.swap(limits);
        context = {};
    }

    bool append(std::string_view path, std::string_view baseline, std::size_t schemaLimit)
    {
        if (!ProjectInputSnapshot::validLogicalPath(path) || files.size() >= budget.maxFiles
            || baseline.size() > budget.maxFileBytes
            || baseline.size() > budget.maxTotalBytes - retainedBytes)
            return false;
        for (const auto& file : files)
            if (file.relativePath == path)
                return false;
        EditorRecoverySourceFile file;
        file.relativePath = path;
        file.savedFileBytes = baseline;
        if (file.savedFileBytes.capacity() > budget.maxTotalBytes - retainedBytes)
            return false;
        retainedBytes += file.savedFileBytes.capacity();
        file.binding.contractVersion = 1;
        file.binding.pathScope = FilePathScope::MountedResource;
        file.binding.mount = context.snapshot->location().mount;
        file.binding.path = context.snapshot->location().pathFor(path);
        files.push_back(std::move(file));
        limits.push_back(std::min(schemaLimit, budget.maxFileBytes));
        return true;
    }
};

EditorSourceRecoveryFiles::EditorSourceRecoveryFiles(ProjectSourceContext context,
    const EditorRecoverySnapshot& record,
    std::string_view admittedAlias,
    ProjectInputBudget budget,
    EditorRecoveryBudget recovery)
    : state_(core::makeShared<State>())
{
    auto& s = *state_;
    s.context = std::move(context);
    s.budget = budget;
    const char* issue = nullptr;
    core::pmr::String detail;
    if (!s.context.validate(issue) || !ProjectInputSnapshot::validBudget(budget)) {
        s.fail(issue ? issue : "EDITOR-RECOVERY-SOURCE-FILE-BUDGET");
        return;
    }
    if (!EditorRecoveryCodec::validate(record, detail, recovery) || !record.source
        || record.source->alias != admittedAlias
        || record.source->root != s.context.snapshot->location().root
        || !ProjectInputSnapshot::equal(record.source->inputs, s.context.snapshot->identities())) {
        s.fail("EDITOR-RECOVERY-SOURCE-FILE-RECORD");
        return;
    }
    const auto count = record.scripts.size() + (record.scene.path.empty() ? 0u : 1u);
    if (count > budget.maxFiles) {
        s.fail("EDITOR-RECOVERY-SOURCE-FILE-BUDGET");
        return;
    }
    s.files.reserve(count);
    s.limits.reserve(count);
    if (!record.scene.path.empty()
        && !s.append(record.scene.path,
            record.scene.savedFileBytes,
            EditorSceneAuthoringCodec::maxDocumentBytes)) {
        s.fail("EDITOR-RECOVERY-SOURCE-FILE-BUDGET-OR-ALIAS");
        return;
    }
    for (const auto& script : record.scripts)
        if (!s.append(script.path, script.savedFileBytes, ScriptDocumentSession::maxScriptBytes)) {
            s.fail("EDITOR-RECOVERY-SOURCE-FILE-BUDGET-OR-ALIAS");
            return;
        }
}

EditorSourceRecoveryFiles::~EditorSourceRecoveryFiles()
{
    cancel();
}

RecoverySourceFilesState EditorSourceRecoveryFiles::state() const noexcept
{
    return state_->phase;
}

const char* EditorSourceRecoveryFiles::diagnostic() const noexcept
{
    return state_->diagnostic;
}

const core::pmr::Vector<EditorRecoverySourceFile>* EditorSourceRecoveryFiles::observations(
    std::uint64_t currentEpoch, const char*& diagnostic) const noexcept
{
    diagnostic = nullptr;
    if (state_->phase != RecoverySourceFilesState::Ready || currentEpoch != state_->context.epoch) {
        diagnostic = "EDITOR-RECOVERY-SOURCE-FILES-NOT-READY-OR-EPOCH";
        return nullptr;
    }
    return state_->context.validate(diagnostic) ? &state_->files : nullptr;
}

void EditorSourceRecoveryFiles::cancel() noexcept
{
    if (state_->phase == RecoverySourceFilesState::Pending) {
        state_->fail("EDITOR-RECOVERY-SOURCE-FILES-CANCELLED");
        state_->phase = RecoverySourceFilesState::Cancelled;
    }
}

void EditorSourceRecoveryFiles::advance(std::uint64_t currentEpoch) noexcept
{
    auto& s = *state_;
    if (s.phase != RecoverySourceFilesState::Pending)
        return;
    try {
        const char* issue = nullptr;
        if (currentEpoch != s.context.epoch || !s.context.validate(issue)) {
            s.fail(issue ? issue : "EDITOR-RECOVERY-SOURCE-FILE-EPOCH");
            return;
        }
        if (s.read) {
            s.read->advance(currentEpoch);
            if (s.read->state() == ProjectFileReadState::Pending)
                return;
            if (s.read->state() != ProjectFileReadState::Complete) {
                s.fail(s.read->diagnostic());
                return;
            }
            auto& file = s.files[s.next];
            file.binding = s.read->binding();
            file.observation = s.read->takeResult();
            s.read.reset();
            const auto& result = file.observation;
            if (result.bytes.capacity() > s.budget.maxTotalBytes - s.retainedBytes
                || result.bytes.size() > file.binding.maxBytes) {
                s.fail("EDITOR-RECOVERY-SOURCE-FILE-BUDGET");
                return;
            }
            if (result.status == FileIoStatus::MissingFile) {
                if (!result.bytes.empty()) {
                    s.fail("EDITOR-RECOVERY-SOURCE-MISSING-WITH-BYTES");
                    return;
                }
                file.conflict = RecoverySourceFileConflict::Missing;
            } else if (result.status == FileIoStatus::Success) {
                const std::string_view actual(
                    result.bytes.empty() ? "" : reinterpret_cast<const char*>(result.bytes.data()),
                    result.bytes.size());
                file.conflict = actual == file.savedFileBytes ? RecoverySourceFileConflict::None
                                                              : RecoverySourceFileConflict::Changed;
            } else {
                s.fail(result.diagnosticId);
                return;
            }
            s.retainedBytes += result.bytes.capacity();
            ++s.next;
        }
        if (s.next == s.files.size()) {
            s.phase = RecoverySourceFilesState::Ready;
            return;
        }
        const auto remaining = s.budget.maxTotalBytes - s.retainedBytes;
        auto& request = s.files[s.next].binding;
        request.maxBytes = std::min(s.limits[s.next], remaining);
        if (!request.maxBytes) {
            s.fail("EDITOR-RECOVERY-SOURCE-FILE-BUDGET");
            return;
        }
        s.read = core::makeUnique<ProjectFileReadTask>(*s.context.files,
            s.context.snapshot->location(),
            s.files[s.next].relativePath,
            request.maxBytes);
        s.read->advance(currentEpoch);
        if (s.read->state() == ProjectFileReadState::Failed)
            s.fail(s.read->diagnostic());
    } catch (...) {
        s.fail("EDITOR-RECOVERY-SOURCE-FILE-PREPARATION");
    }
}

} // namespace arp::editor
