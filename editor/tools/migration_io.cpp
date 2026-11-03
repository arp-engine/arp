#include "editor/tools/migration_io.h"

#include "core/io/file_save_contract.h"
#include "core/io/filesystem_path.h"
#include "tools/io/cold_tool_files.h"

#include <exception>

namespace arp::editor {

core::io::FileTextReadResult EditorMigrationIo::readSource(
    const std::filesystem::path& path, std::uint64_t maxBytes)
{
    using namespace core::io;
    FileTextReadResult failure;
    failure.source.status = FileIoStatus::InvalidRequest;
    failure.source.diagnosticId = "EDITOR-MIGRATION-SOURCE-PROFILE";
    if (path.empty() || !tools::io::ColdToolFiles::validBudget(maxBytes))
        return failure;
    try {
        tools::io::ColdToolFiles files(
            maxBytes, 0, tools::io::ColdToolFileScope::NativeHostFilesystem);
        FileTextReadRequest request;
        request.file.contractVersion = 1;
        request.file.pathScope = FilePathScope::HostFilesystem;
        request.file.path =
            core::FilePath::utf8(std::filesystem::absolute(path).lexically_normal());
        request.file.maxBytes = request.text.maxOutputBytes = maxBytes;
        request.text.bom = core::TextBomPolicy::Strip;
        request.text.nul = core::TextNulPolicy::Reject;
        auto result = files.readText(request);
        files.retire();
        if (result.source.status == FileIoStatus::BudgetExceeded && !result.source.diagnosticId)
            result.source.diagnosticId = "EDITOR-MIGRATION-SOURCE-BUDGET";
        return result;
    } catch (const std::bad_alloc&) {
        failure.source.status = FileIoStatus::BudgetExceeded;
        failure.source.diagnosticId = "EDITOR-MIGRATION-SOURCE-ALLOCATION";
    } catch (const std::exception&) {
        failure.source.status = FileIoStatus::BackendUnavailable;
        failure.source.diagnosticId = "EDITOR-MIGRATION-SOURCE-PREPARATION";
    }
    return failure;
}

core::io::FileSaveResult EditorMigrationIo::writeNewFile(const std::filesystem::path& directory,
    core::StringView path,
    core::StringView bytes,
    std::uint64_t maxBytes)
{
    using namespace core::io;
    FileSaveResult failure;
    failure.fail(FileIoStatus::InvalidRequest, "EDITOR-MIGRATION-SAVE-PROFILE");
    if (directory.empty() || !tools::io::ColdToolFiles::validBudget(maxBytes))
        return failure;
    if (bytes.size() > maxBytes) {
        failure.fail(FileIoStatus::BudgetExceeded, "EDITOR-MIGRATION-SAVE-BUDGET");
        return failure;
    }
    try {
        tools::io::ColdToolFiles files(maxBytes, 1);
        const auto mount = files.mountDirectory("migration-output", directory, true);
        if (!mount || !files.service().saveCapability(mount).observedBaselineReplace) {
            failure.fail(FileIoStatus::Unsupported, "EDITOR-MIGRATION-SAVE-UNSUPPORTED");
            return failure;
        }
        FileSaveRequest request;
        request.pathScope = FilePathScope::MountedResource;
        request.mount = mount;
        request.path = path;
        request.maxWriteBytes = maxBytes;
        request.bytes.assign(bytes.begin(), bytes.end());
        const auto expected = FileSaveContract::contentVersion(request);
        auto result = files.save(std::move(request));
        files.retire();
        if (result.status == FileIoStatus::Success
            && (result.commitState != FileCommitState::Committed
                || result.deliveryStatus != FileIoStatus::Success
                || result.committedVersion != expected))
            result.fail(FileIoStatus::IntegrityMismatch, "EDITOR-MIGRATION-SAVE-IDENTITY");
        return result;
    } catch (const std::bad_alloc&) {
        failure.fail(FileIoStatus::BudgetExceeded, "EDITOR-MIGRATION-SAVE-ALLOCATION");
    } catch (const std::exception&) {
        failure.fail(FileIoStatus::BackendUnavailable, "EDITOR-MIGRATION-SAVE-PREPARATION");
    }
    return failure;
}
} // namespace arp::editor
