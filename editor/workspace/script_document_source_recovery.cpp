#include "core/io/file_read_contract.h"
#include "editor/workspace/script_document_session.h"

namespace arp::editor {
const ProjectSourceLocation* ScriptDocumentSession::Document::sourceLocation() const noexcept
{
    if (sourceSnapshot && !missingSource)
        return &sourceSnapshot->location();
    if (missingSource && !sourceSnapshot && missingSource->context_.snapshot)
        return &missingSource->context_.snapshot->location();
    return nullptr;
}

bool ScriptDocumentSession::Document::sourceConflict() const noexcept
{
    if (missingSource)
        return true;
    if (!sourceSnapshot)
        return false;
    const auto* entry = sourceSnapshot->entryAt(0);
    if (!entry || sourceSnapshot->size() != 1)
        return true;
    const auto& bytes = entry->receipt.bytes;
    return std::string_view(
               bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data()), bytes.size())
        != savedFileBytes;
}

bool ScriptDocumentSession::Document::validateSource(
    core::io::FileService& files, std::uint64_t epoch, const char*& diagnostic) const noexcept
{
    diagnostic = "EDITOR-SCRIPT-SOURCE-BINDING";
    try {
        if (!sourceLocation())
            return false;
        if (sourceSnapshot) {
            const auto* entry = sourceSnapshot->entryAt(0);
            return sourceSnapshot->size() == 1 && entry
                && path == std::filesystem::u8path(entry->binding.path)
                && sourceSnapshot->validate(files, epoch, diagnostic);
        }
        const auto& source = *missingSource;
        if (source.context_.files != &files || source.context_.epoch != epoch
            || source.observation_.status != core::io::FileIoStatus::MissingFile
            || !source.observation_.bytes.empty() || source.binding_.cancellation.cancelled()
            || path != std::filesystem::u8path(source.binding_.path)
            || !source.context_.validate(diagnostic))
            return false;
        const auto admission = files.validateRead(source.binding_);
        if (admission.status != core::io::FileIoStatus::Success) {
            diagnostic = admission.diagnosticId ? admission.diagnosticId
                                                : "EDITOR-SCRIPT-MISSING-SOURCE-STALE";
            return false;
        }
        diagnostic = nullptr;
        return true;
    } catch (...) {
        diagnostic = "EDITOR-SCRIPT-SOURCE-BINDING-ALLOCATION";
        return false;
    }
}

bool ScriptDocumentSession::prepareMissingSourceFile(ProjectSourceContext context,
    std::string_view relative,
    const core::io::FileReadRequest& binding,
    const core::io::FileReadResult& observation,
    core::UniquePtr<Document>& destination,
    const char*& diagnostic)
{
    using namespace core::io;
    diagnostic = "EDITOR-SCRIPT-MISSING-SOURCE-OBSERVATION";
    try {
        if (!context.validate(diagnostic) || observation.status != FileIoStatus::MissingFile
            || !observation.bytes.empty() || !isScriptPath(std::filesystem::u8path(relative))
            || !ProjectInputSnapshot::validLogicalPath(relative)
            || binding.pathScope != FilePathScope::MountedResource
            || !(binding.mount == context.snapshot->location().mount)
            || binding.path != context.snapshot->location().pathFor(relative)
            || binding.range.mode != FileReadRangeMode::WholeFile
            || binding.partialPolicy != FilePartialReadPolicy::DenyPartial
            || binding.cancellation.cancelled() || binding.maxBytes > maxScriptBytes
            || FileReadContract::validate(binding).diagnostic) {
            if (!diagnostic)
                diagnostic = "EDITOR-SCRIPT-MISSING-SOURCE-OBSERVATION";
            return false;
        }
        auto missing = core::makeShared<MissingSourceBinding>();
        missing->context_ = std::move(context);
        missing->binding_ = binding;
        missing->observation_ = observation;
        auto candidate = core::makeUnique<Document>();
        candidate->path = std::filesystem::u8path(binding.path);
        candidate->missingSource = std::move(missing);
        if (!candidate->validateSource(*candidate->missingSource->context_.files,
                candidate->missingSource->context_.epoch,
                diagnostic))
            return false;
        destination = std::move(candidate);
        diagnostic = nullptr;
        return true;
    } catch (...) {
        diagnostic = "EDITOR-SCRIPT-MISSING-SOURCE-PREPARATION";
        return false;
    }
}

core::UniquePtr<ScriptDocumentSession::PreparedRecovery>
ScriptDocumentSession::prepareRecoveredSourceDocuments(const core::pmr::Vector<Document>& prepared,
    std::size_t activeIndex,
    ProjectSourceContext sources,
    core::pmr::String& diagnostic) const
{
    return prepareRecovery(prepared, activeIndex, &sources, diagnostic);
}

ScriptDocumentSession::RecoveryCommitResult ScriptDocumentSession::commitRecoveredSourceDocuments(
    PreparedRecovery& prepared, const ProjectSourceContext& sources, core::pmr::String& diagnostic)
{
    const auto readiness = recoveryCommitReadiness(prepared);
    if (readiness != RecoveryCommitResult::SourceValidationRequired)
        return readiness == RecoveryCommitResult::Ready ? RecoveryCommitResult::Incomplete
                                                        : readiness;
    const char* issue = nullptr;
    if (prepared.sources_.snapshot != sources.snapshot || prepared.sources_.files != sources.files
        || prepared.sources_.epoch != sources.epoch || !sources.validate(issue)) {
        diagnostic = issue ? issue : "EDITOR-SCRIPT-RECOVERY-SOURCE-OWNER";
        return RecoveryCommitResult::Stale;
    }
    for (const auto& document : prepared.documents_)
        if (!document.validateSource(*sources.files, sources.epoch, issue)) {
            diagnostic = issue;
            return RecoveryCommitResult::Stale;
        }
    diagnostic.clear();
    return publishRecoveredDocuments(prepared);
}
} // namespace arp::editor
