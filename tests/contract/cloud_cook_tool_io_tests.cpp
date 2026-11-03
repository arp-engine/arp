#include "assets/cloud/cloud_asset.h"
#include "core/io/file_save_contract.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "resource/cloud_asset_cooked.h"
#include "tests/fixtures/unified_io_fixture.h"
#include "tools/cloud_asset/cloud_cook_session.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using namespace arp;
using namespace core::io;
using namespace tools::cloud_asset;
using unified_io_fixture::Probe;
constexpr std::uint64_t budget = 16384;

core::pmr::String recipe()
{
    assets::cloud::CloudCookDesc desc;
    desc.algorithmVersion = assets::cloud::kCloudCookAlgorithmVersion;
    desc.seed = 137;
    desc.weatherDimension = desc.shapeDimension = 4;
    desc.detailDimension = 2;
    desc.weatherFrequency = desc.shapeFrequency = 4;
    desc.detailFrequency = 6;
    desc.octaves = 3;
    desc.maxOutputBytes = desc.maxNoiseEvaluations = 65536;
    desc.heightProfiles.fill({0.f, .125f, .75f, 1.f});
    core::pmr::String text{&core::defaultCpuMemoryResource()};
    assert(assets::cloud::CloudAssetCooker::serialize(desc, text).success);
    return text;
}

class Host final {
public:
    Host(core::StringView input,
        bool existing = true,
        bool writable = true,
        core::StringView baseline = "old")
        : memory(core::makeShared<MemoryFileBackend>(
              MemoryFileBackendDesc{4, 4 * budget, 1, 2 * budget, 512, writable}))
    {
        mount = mounts.mount("cloud", memory);
        assert(mount);
        assert(memory->registerFile(
                   "recipe.yaml", reinterpret_cast<const std::uint8_t*>(input.data()), input.size())
            == FileIoStatus::Success);
        if (existing)
            assert(memory->registerFile("cooked.yaml",
                       reinterpret_cast<const std::uint8_t*>(baseline.data()),
                       baseline.size())
                == FileIoStatus::Success);
    }

    ~Host()
    {
        files.shutdown();
        mounts.shutdown();
        queue.dispatch(2);
        assert(!queue.pending() && !queue.reserved() && !mounts.telemetry().inFlight);
    }

    CloudCookSessionDesc descriptor() const
    {
        CloudCookSessionDesc desc;
        desc.source.contractVersion = 1;
        desc.source.pathScope = FilePathScope::MountedResource;
        desc.source.mount = desc.outputMount = mount;
        desc.source.path = "recipe.yaml";
        desc.source.maxBytes = desc.maxDocumentBytes = budget;
        desc.outputPath = "cooked.yaml";
        return desc;
    }

    void drive(CloudCookSession& session, CloudCookPhase stop = CloudCookPhase::Complete)
    {
        for (unsigned i = 0; i < 32 && !session.terminal() && session.phase() != stop; ++i) {
            session.advance();
            assert(queue.dispatch(2).success());
        }
        assert(session.terminal() || session.phase() == stop);
    }

    FileReadResult read()
    {
        auto desc = descriptor();
        desc.source.path = desc.outputPath;
        Probe<FileReadResult> probe;
        assert(files.read(desc.source, {Probe<FileReadResult>::complete, &probe})
            == FileIoStatus::Accepted);
        assert(queue.dispatch(2).success() && probe.calls == 1);
        return std::move(probe.result);
    }

    void replace(core::StringView text, bool missing = false)
    {
        FileSaveRequest request;
        request.mount = mount;
        request.pathScope = FilePathScope::MountedResource;
        request.path = "cooked.yaml";
        request.maxWriteBytes = budget;
        request.bytes.assign(text.begin(), text.end());
        if (!missing) {
            FileSaveRequest old;
            old.bytes.assign({'o', 'l', 'd'});
            request.precondition = FileSavePrecondition::ContentSha256;
            request.expectedVersion = FileSaveContract::contentVersion(old);
            request.maxBaselineBytes = budget;
        }
        Probe<FileSaveResult> probe;
        assert(files.save(std::move(request), {Probe<FileSaveResult>::complete, &probe})
            == FileIoStatus::Accepted);
        assert(queue.dispatch(2).success() && probe.calls == 1);
        assert(probe.result.commitState == FileCommitState::Committed);
    }

    core::runtime::CompletionQueue queue{core::runtime::CompletionQueueDesc{2}};
    core::SharedPtr<MemoryFileBackend> memory;
    MountedFileBackend mounts{MountedFileBackendDesc{1, 1, 2 * budget, 32, 512}};
    FileService files{queue, mounts};
    core::PoolHandle mount;
};

void success(bool existing)
{
    auto input = recipe();
    input.insert(0, "\xef\xbb\xbf");
    Host host(input, existing);
    CloudCookSession session(host.files, host.descriptor());
    host.drive(session);
    assert(session.phase() == CloudCookPhase::Complete);
    assert(session.saveResult().status == FileIoStatus::Success
        && session.saveResult().commitState == FileCommitState::Committed);
    const auto file = host.read();
    assert(file.status == FileIoStatus::Success && file.bytes.size() == session.documentBytes());
    assets::cloud::CloudAsset asset;
    assert(resource::CloudAssetCookedCodec::decode(
        {reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size()},
        budget,
        budget,
        asset)
            .success);
    assert(asset.source.seed == 137 && asset.source.algorithmVersion == 3
        && asset.cells.size() == session.cellCount());
    core::Sha256 hash;
    hash.update(input);
    assert(session.sourceIdentity()
        == FileVersionEncoding::finish(hash, FileVersionKind::ContentSha256));
}

void conflicts(bool existing)
{
    Host host(recipe(), existing);
    CloudCookSession session(host.files, host.descriptor());
    host.drive(session, CloudCookPhase::ReadyToSave);
    host.replace("external", !existing);
    host.drive(session);
    assert(session.phase() == CloudCookPhase::Failed
        && session.saveResult().status == FileIoStatus::VersionMismatch);
    const auto result = host.read();
    assert(core::StringView(reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size())
        == "external");
}

void failures()
{
    const auto rejected = [](core::StringView text, const char* diagnostic) {
        Host host(text);
        CloudCookSession session(host.files, host.descriptor());
        host.drive(session);
        assert(session.phase() == CloudCookPhase::Failed && session.diagnostic());
        assert(std::strstr(session.diagnostic(), diagnostic));
        const auto result = host.read();
        assert(result.bytes.size() == 3 && result.bytes.front() == 'o');
    };
    rejected("#\xff\n", "UTF8");
    rejected(core::StringView("x\0y", 3), "NUL");
    rejected("type: unknown\ncontractVersion: 99\n", "CLOUD");
    for (unsigned fault = 0; fault < 6; ++fault) {
        Host host(recipe(), true, fault != 5);
        auto desc = host.descriptor();
        if (fault == 0)
            desc.contractVersion = 2;
        if (fault == 1)
            desc.source.path = "missing.yaml";
        if (fault == 2)
            desc.source.maxBytes = 1;
        if (fault == 3)
            assert(host.mounts.unmount(host.mount));
        if (fault == 4)
            desc.outputPath = "../escape.yaml";
        CloudCookSession session(host.files, desc);
        host.drive(session);
        assert(session.phase() == CloudCookPhase::Failed && session.diagnostic());
    }
    Host host(recipe());
    auto desc = host.descriptor();
    desc.maxDocumentBytes = desc.source.maxBytes = recipe().size();
    CloudCookSession session(host.files, desc);
    host.drive(session);
    assert(
        session.phase() == CloudCookPhase::Failed && std::strstr(session.diagnostic(), "BUDGET"));
    const core::pmr::String oversized(budget + 1, 'x', &core::defaultCpuMemoryResource());
    Host baselineHost(recipe(), true, true, oversized);
    CloudCookSession baseline(baselineHost.files, baselineHost.descriptor());
    baselineHost.drive(baseline);
    assert(
        baseline.phase() == CloudCookPhase::Failed && std::strstr(baseline.diagnostic(), "BUDGET"));
}

// Result-delivery fixture only: does not prove a native unknown-outcome commit.
class UnknownSaveBackend final : public ManagedFileBackend {
public:
    explicit UnknownSaveBackend(core::SharedPtr<MemoryFileBackend> source)
        : source_(std::move(source))
    {}

    FileBackendCapability capability() const noexcept override { return source_->capability(); }

    FileReadResult validateRead(const FileReadRequest& request) const override
    {
        return source_->validateRead(request);
    }

    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        core::runtime::CompletionQueue& queue) noexcept override
    {
        return source_->readAsync(request, completion, queue);
    }

    FileSaveCapability saveCapability(core::PoolHandle) const noexcept override
    {
        return {true, false, false};
    }

    FileIoStatus saveAsync(FileSaveRequest,
        FileSaveCompletion completion,
        core::runtime::CompletionQueue& queue) noexcept override
    {
        ++saves;
        completion_ = completion;
        return queue
                   .post({[](void* context) {
                              auto& self = *static_cast<UnknownSaveBackend*>(context);
                              FileSaveResult result;
                              result.status = FileIoStatus::OutcomeUnknown;
                              result.commitState = FileCommitState::OutcomeUnknown;
                              result.diagnosticId = "CLOUD-TEST-OUTCOME-UNKNOWN";
                              self.completion_.complete(self.completion_.userData, result);
                          },
                       this})
                   .accepted()
            ? FileIoStatus::Accepted
            : FileIoStatus::QueueFull;
    }

    void shutdown() noexcept override { source_->shutdown(); }

    unsigned saves = 0;

private:
    core::SharedPtr<MemoryFileBackend> source_;
    FileSaveCompletion completion_;
};

void unknownOutcome()
{
    Host host(recipe());
    assert(host.mounts.unmount(host.mount));
    auto memory = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{2, 2 * budget, 1, 2 * budget, 512});
    const auto input = recipe();
    assert(memory->registerFile(
               "recipe.yaml", reinterpret_cast<const std::uint8_t*>(input.data()), input.size())
        == FileIoStatus::Success);
    auto backend = core::makeShared<UnknownSaveBackend>(memory);
    host.mount = host.mounts.mount("cloud", backend);
    assert(host.mount);
    CloudCookSession session(host.files, host.descriptor());
    host.drive(session);
    assert(session.phase() == CloudCookPhase::Failed && backend->saves == 1);
    assert(session.saveResult().commitState == FileCommitState::OutcomeUnknown);
}

void retirement()
{
    for (unsigned mode = 0; mode < 4; ++mode) {
        Host host(recipe());
        auto session = core::makeUnique<CloudCookSession>(host.files, host.descriptor());
        if (mode < 2)
            session->advance(); // Accepted read is still queued.
        else {
            host.drive(*session, CloudCookPhase::ReadyToSave);
            session->advance(); // Memory backend commits before queued delivery.
        }
        if (mode % 2 == 0) {
            session->cancel();
            host.queue.dispatch(2);
            assert(session->phase() == CloudCookPhase::Cancelled);
            if (mode == 2)
                assert(session->saveResult().commitState == FileCommitState::Committed);
        } else {
            session.reset();
            host.queue.dispatch(2);
        }
        assert(
            !host.queue.pending() && !host.queue.reserved() && !host.mounts.telemetry().inFlight);
    }
}
} // namespace

int main(int argc, char** argv)
{
    core::installDefaultCpuAllocatorAsPmrDefault();
    success(true);
    success(false);
    if (argc > 1 && !std::strcmp(argv[1], "--smoke")) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            success(i % 2 == 0);
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("Cloud cook core %llu/%llu -> %llu/%llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)after.liveBytes,
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    } else {
        conflicts(true);
        conflicts(false);
        failures();
        retirement();
        unknownOutcome();
    }
    std::puts(
        "PASS cloud cook FileService source, original codec, observed-baseline save and retirement");
}
