#include "tools/cloud_asset/cloud_cook_session.h"

#include "assets/cloud/cloud_asset.h"
#include "core/io/file_read_contract.h"
#include "core/io/file_save_contract.h"
#include "resource/cloud_asset_cooked.h"

namespace arp::tools::cloud_asset {
using namespace core::io;

struct CloudCookSession::State {
    FileService* files = nullptr;
    CloudCookSessionDesc desc;
    CloudCookPhase phase = CloudCookPhase::ReadingRecipe;
    core::runtime::CancellationSource cancellation;
    FileTextReadResult input;
    FileReadResult previous;
    FileSaveResult saved;
    FileVersionToken sourceVersion, baselineVersion, candidateVersion;
    core::pmr::Vector<std::uint8_t> candidate{&core::defaultCpuMemoryResource()};
    const char* diagnostic = nullptr;
    bool submitted = false, delivered = false, missing = false;
    std::uint32_t cells = 0, algorithm = 0;
    std::size_t documentSize = 0;

    bool terminal() const noexcept
    {
        return phase == CloudCookPhase::Complete || phase == CloudCookPhase::Failed
            || phase == CloudCookPhase::Cancelled;
    }

    void fail(const char* id) noexcept
    {
        diagnostic = id ? id : "CLOUD-TOOL-IO-FAILURE";
        phase = CloudCookPhase::Failed;
    }

    using Retained = core::SharedPtr<State>;

    static core::UniquePtr<Retained> consume(void* context) noexcept
    {
        return {static_cast<Retained*>(context),
            core::CpuAllocatedDeleter<Retained>{
                context, &core::destroyCpuAllocatedUnique<Retained>}};
    }

    static void readRecipe(void* context, FileTextReadResult result) noexcept
    {
        auto retained = consume(context);
        auto& s = **retained;
        if (!s.terminal()) {
            s.input = std::move(result);
            s.delivered = true;
        }
    }

    static void readBaseline(void* context, FileReadResult result) noexcept
    {
        auto retained = consume(context);
        auto& s = **retained;
        if (!s.terminal()) {
            s.previous = std::move(result);
            s.delivered = true;
        }
    }

    static void savedOutput(void* context, FileSaveResult result) noexcept
    {
        auto retained = consume(context);
        auto& s = **retained;
        // Cancellation of delivery does not undo an actual file commit.
        s.saved = result;
        if (!s.terminal())
            s.delivered = true;
    }

    static FileVersionToken contentIdentity(const FileReadResult& result)
    {
        core::Sha256 hash;
        hash.update({reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size()});
        return FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256);
    }

    void next(CloudCookPhase value) noexcept
    {
        phase = value;
        submitted = delivered = false;
    }
};

CloudCookSession::CloudCookSession(FileService& files, const CloudCookSessionDesc& desc)
    : state_(core::makeShared<State>())
{
    auto& s = *state_;
    s.files = &files;
    s.desc = desc; // PMR member assignment retains explicit core resources.
    FileReadRequest output;
    output.contractVersion = 1;
    output.pathScope = FilePathScope::MountedResource;
    output.mount = desc.outputMount;
    output.path = desc.outputPath;
    output.maxBytes = desc.maxDocumentBytes;
    if (desc.contractVersion != 1 || !desc.maxDocumentBytes || desc.maxDocumentBytes >= SIZE_MAX
        || desc.maxDocumentBytes >= UINT64_MAX / 2 || desc.source.contractVersion != 1
        || desc.source.pathScope != FilePathScope::MountedResource
        || desc.source.range.mode != FileReadRangeMode::WholeFile
        || desc.source.partialPolicy != FilePartialReadPolicy::DenyPartial
        || desc.source.maxBytes > desc.maxDocumentBytes
        || FileReadContract::validate(desc.source).status != FileIoStatus::Success
        || FileReadContract::validate(output).status != FileIoStatus::Success) {
        s.fail("CLOUD-TOOL-DESCRIPTOR");
    } else if (!files.saveCapability(desc.outputMount).observedBaselineReplace) {
        s.fail("CLOUD-TOOL-SAVE-UNSUPPORTED");
    }
}

CloudCookSession::~CloudCookSession()
{
    cancel();
}

void CloudCookSession::advance() noexcept
{
    auto& s = *state_;
    if (s.terminal())
        return;
    if (s.desc.source.cancellation.cancelled()) {
        cancel();
        return;
    }
    try {
        if (s.phase == CloudCookPhase::ReadingRecipe) {
            if (s.submitted) {
                if (!s.delivered)
                    return;
                if (s.input.status() != FileIoStatus::Success) {
                    s.fail(s.input.diagnosticId());
                    return;
                }
                s.sourceVersion = State::contentIdentity(s.input.source);
                s.next(CloudCookPhase::ReadingBaseline);
                return;
            }
            FileTextReadRequest request;
            request.file = s.desc.source;
            request.file.cancellation = s.cancellation.token();
            request.text.bom = core::TextBomPolicy::Strip;
            request.text.nul = core::TextNulPolicy::Reject;
            request.text.maxOutputBytes = s.desc.maxDocumentBytes;
            auto retained = core::makeUnique<State::Retained>(state_);
            if (s.files->readText(request, {State::readRecipe, retained.get()})
                != FileIoStatus::Accepted) {
                s.fail("CLOUD-TOOL-READ-ADMISSION");
                return;
            }
            retained.release();
            s.submitted = true;
        } else if (s.phase == CloudCookPhase::ReadingBaseline) {
            if (s.submitted) {
                if (!s.delivered)
                    return;
                s.missing = s.previous.status == FileIoStatus::MissingFile;
                if (!s.missing && s.previous.status != FileIoStatus::Success) {
                    s.fail(s.previous.diagnosticId);
                    return;
                }
                if (!s.missing)
                    s.baselineVersion = State::contentIdentity(s.previous);
                s.previous = {};
                s.next(CloudCookPhase::Preparing);
                return;
            }
            FileReadRequest request;
            request.contractVersion = 1;
            request.pathScope = FilePathScope::MountedResource;
            request.mount = s.desc.outputMount;
            request.path = s.desc.outputPath;
            request.maxBytes = s.desc.maxDocumentBytes;
            request.cancellation = s.cancellation.token();
            auto retained = core::makeUnique<State::Retained>(state_);
            if (s.files->read(request, {State::readBaseline, retained.get()})
                != FileIoStatus::Accepted) {
                s.fail("CLOUD-TOOL-BASELINE-ADMISSION");
                return;
            }
            retained.release();
            s.submitted = true;
        } else if (s.phase == CloudCookPhase::Preparing) {
            assets::cloud::CloudCookDesc recipe;
            auto result = assets::cloud::CloudAssetCooker::parse(
                s.input.decoded.text, s.desc.maxDocumentBytes, recipe);
            assets::cloud::CloudAsset asset;
            if (result.success)
                result = assets::cloud::CloudAssetCooker::cook(recipe, asset);
            core::pmr::String cooked{&core::defaultCpuMemoryResource()};
            if (result.success)
                result =
                    resource::CloudAssetCookedCodec::encode(asset, s.desc.maxDocumentBytes, cooked);
            if (!result.success) {
                s.fail(result.diagnostic);
                return;
            }
            s.candidate.assign(cooked.begin(), cooked.end());
            s.cells = static_cast<std::uint32_t>(asset.cells.size());
            s.algorithm = recipe.algorithmVersion;
            s.documentSize = cooked.size();
            s.input = {};
            s.next(CloudCookPhase::ReadyToSave);
        } else if (s.phase == CloudCookPhase::ReadyToSave) {
            FileSaveRequest request;
            request.pathScope = FilePathScope::MountedResource;
            request.mount = s.desc.outputMount;
            request.path = s.desc.outputPath;
            request.bytes = std::move(s.candidate);
            request.precondition = s.missing ? FileSavePrecondition::MustBeMissing
                                             : FileSavePrecondition::ContentSha256;
            request.expectedVersion = s.baselineVersion;
            request.maxWriteBytes = s.desc.maxDocumentBytes;
            request.maxBaselineBytes = s.missing ? 0 : s.desc.maxDocumentBytes;
            request.cancellation = s.cancellation.token();
            s.candidateVersion = FileSaveContract::contentVersion(request);
            auto retained = core::makeUnique<State::Retained>(state_);
            if (s.files->save(std::move(request), {State::savedOutput, retained.get()})
                != FileIoStatus::Accepted) {
                s.fail("CLOUD-TOOL-SAVE-ADMISSION");
                return;
            }
            retained.release();
            s.next(CloudCookPhase::Saving);
        } else if (s.phase == CloudCookPhase::Saving && s.delivered) {
            if (s.saved.status != FileIoStatus::Success
                || s.saved.deliveryStatus != FileIoStatus::Success
                || s.saved.commitState != FileCommitState::Committed) {
                s.fail(s.saved.diagnosticId);
            } else if (s.saved.committedVersion != s.candidateVersion) {
                s.fail("CLOUD-TOOL-COMMIT-IDENTITY");
            } else {
                s.next(CloudCookPhase::Complete);
            }
        }
    } catch (const std::bad_alloc&) {
        s.fail("CLOUD-TOOL-ALLOCATION-BUDGET");
    } catch (...) {
        s.fail("CLOUD-TOOL-PREPARATION-FAILED");
    }
}

void CloudCookSession::cancel() noexcept
{
    auto& s = *state_;
    if (!s.terminal()) {
        s.cancellation.cancel();
        s.diagnostic = "CLOUD-TOOL-CANCELLED";
        s.phase = CloudCookPhase::Cancelled;
    }
}

CloudCookPhase CloudCookSession::phase() const noexcept
{
    return state_->phase;
}

bool CloudCookSession::terminal() const noexcept
{
    return state_->terminal();
}

const char* CloudCookSession::diagnostic() const noexcept
{
    return state_->diagnostic;
}

FileSaveResult CloudCookSession::saveResult() const noexcept
{
    return state_->saved;
}

FileVersionToken CloudCookSession::sourceIdentity() const noexcept
{
    return state_->sourceVersion;
}

std::uint32_t CloudCookSession::cellCount() const noexcept
{
    return state_->cells;
}

std::uint32_t CloudCookSession::algorithmVersion() const noexcept
{
    return state_->algorithm;
}

std::size_t CloudCookSession::documentBytes() const noexcept
{
    return state_->documentSize;
}

} // namespace arp::tools::cloud_asset
