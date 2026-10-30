#include "core/io/file_io.h"
#include "core/io/http_file_backend.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/text/text_decoder.h"
#include "editor/project/project_source_session.h"
#include "editor/workspace/editor_script_modules.h"
#include "tests/fixtures/script_component_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace core::io;
constexpr std::size_t fileLimit = 64 * 1024;
constexpr ProjectInputBudget budget{4, fileLimit, 2 * fileLimit};
constexpr std::uint64_t epoch = 7;

class Fixture final {
public:
    Fixture()
    {
        assert(domains.install());
        project.id = "00000000-0000-4000-8000-000000000094";
        project.name = "Mounted project";
        project.engineContract = "arp.scene-asset/2";
        ProjectScriptModule declaration;
        declaration.id = "primary";
        declaration.resourceId = "00000000-0000-4000-8000-000000000096";
        declaration.source = "scripts/primary.arp";
        declaration.schemaVersion = 1;
        declaration.authoring = ProjectScriptAuthoring{};
        declaration.authoring->maxSourceBytes = fileLimit;
        declaration.authoring->preparationBytes = 16 * 1024 * 1024;
        declaration.authoring->maxInstances = 32;
        declaration.authoring->execution = tests::ScriptComponentFixture::budget();
        declaration.authoring->imports = {"arp_scene"};
        project.scriptModules.push_back(std::move(declaration));
        const auto input = core::readTextFile(
            std::filesystem::path(ARP_SOURCE_DIR) / "tests/fixtures/script_authoring_primary.arp",
            fileLimit);
        assert(input.success);
        script = "\xef\xbb\xbf"
                 "from arp_scene import main_scene\n"
            + input.text + "\nmain_scene.set_object_name(\"MustNotExecute\")\n";
        ProjectDiagnostic issue;
        assert(ProjectManifestCodec::encode(project, yaml, issue));
    }

    core::SharedPtr<MemoryFileBackend> memory()
    {
        auto result = core::makeShared<MemoryFileBackend>(
            MemoryFileBackendDesc{8, 2 * fileLimit, 1, fileLimit, 1024});
        assert(result->registerDirectory("workspace") == FileIoStatus::Success);
        assert(result->registerDirectory("workspace/scripts") == FileIoStatus::Success);
        assert(result->registerFile("workspace/project.arp.yaml",
                   reinterpret_cast<const std::uint8_t*>(yaml.data()),
                   yaml.size())
            == FileIoStatus::Success);
        assert(result->registerFile("workspace/scripts/primary.arp",
                   reinterpret_cast<const std::uint8_t*>(script.data()),
                   script.size())
            == FileIoStatus::Success);
        return result;
    }

    void write(const std::filesystem::path& directory)
    {
        std::filesystem::create_directories(directory / "workspace/scripts");
        std::ofstream manifest(directory / "workspace/project.arp.yaml", std::ios::binary);
        manifest.write(yaml.data(), yaml.size());
        std::ofstream source(directory / "workspace/scripts/primary.arp", std::ios::binary);
        source.write(script.data(), script.size());
        assert(manifest.good() && source.good());
    }

    tests::ScriptComponentFixture domains;
    ProjectManifest project;
    core::pmr::String yaml{&core::defaultCpuMemoryResource()};
    core::pmr::String script{&core::defaultCpuMemoryResource()};
};

struct Harness {
    core::runtime::CompletionQueue queue{{8}};
    MountedFileBackend mounts{{2, 1, fileLimit, 32, 1024}};
    FileService files{queue, mounts};
    core::PoolHandle mount;

    explicit Harness(core::SharedPtr<ManagedFileBackend> backend)
    {
        mount = mounts.mount("workspace", std::move(backend));
        assert(mount);
    }

    ProjectSourceLocation location() const { return {mount, "workspace", epoch}; }

    void wait(ProjectSourceSession& session)
    {
        unified_io_fixture::wait(queue, [&] {
            return session.state() != ProjectSourceState::Pending;
        });
    }
};

// Fault receipts test admission only; native/HTTP evidence uses real backends.
class ReceiptFault final : public ManagedFileBackend {
public:
    explicit ReceiptFault(unsigned mode) : mode_(mode) {}

    FileBackendCapability capability() const noexcept override
    {
        return {FileBackendKind::NullFileBackend,
            FileBackendStatus::ContractComplete,
            false,
            false,
            true};
    }

    FileReadResult validateRead(const FileReadRequest&) const override { return {}; }

    void shutdown() noexcept override {}

    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        core::runtime::CompletionQueue& queue) noexcept override
    {
        struct Delivery {
            FileReadResult result;
            FileReadCompletion target;
        };

        try {
            auto delivery = core::makeUnique<Delivery>();
            delivery->target = completion;
            delivery->result.bytes = {1, 2};
            delivery->result.version.kind = FileVersionKind::ContentSha256;
            if (mode_ == 0)
                delivery->result.version.kind = FileVersionKind::DirectorySnapshotSha256;
            if (mode_ == 2)
                delivery->result.bytes.resize(request.maxBytes + 1);
            if (mode_ == 3)
                delivery->result.version.kind = static_cast<FileVersionKind>(99);
            if (mode_ == 4)
                delivery->result.status = FileIoStatus::MissingFile;
            const auto posted = queue.post(
                {[](void* context) {
                     core::UniquePtr<Delivery> owner{static_cast<Delivery*>(context),
                         core::CpuAllocatedDeleter<Delivery>{
                             context, &core::destroyCpuAllocatedUnique<Delivery>}};
                     owner->target.complete(owner->target.userData, std::move(owner->result));
                 },
                    delivery.get()});
            if (!posted.accepted())
                return FileIoStatus::QueueFull;
            delivery.release();
            return FileIoStatus::Accepted;
        } catch (...) {
            return FileIoStatus::BudgetExceeded;
        }
    }

private:
    unsigned mode_;
};

core::SharedPtr<const ProjectSourceSnapshot> acquire(Harness& host)
{
    ProjectSourceSession session(host.files, host.location(), budget);
    assert(session.read("project.arp.yaml", fileLimit) == FileIoStatus::Accepted);
    assert(!session.acquired("project.arp.yaml") && !session.finish(epoch));
    assert(session.read("scripts/primary.arp", fileLimit) == FileIoStatus::QueueFull);
    host.wait(session);
    const auto* manifest = session.acquired("project.arp.yaml");
    assert(manifest);
    core::TextDecodeDesc decode;
    decode.bom = core::TextBomPolicy::Strip;
    decode.maxOutputBytes = fileLimit;
    const auto& bytes = manifest->receipt.bytes;
    const auto text = core::TextDecoder::decode(
        {reinterpret_cast<const char*>(bytes.data()), bytes.size()}, decode);
    assert(text.status == core::TextDecodeStatus::Success);
    ProjectManifest parsed;
    ProjectDiagnostic issue;
    assert(ProjectManifestCodec::decode(text.text, parsed, issue));
    assert(parsed.scriptModules.size() == 1);
    assert(session.read(parsed.scriptModules.front().source, fileLimit) == FileIoStatus::Accepted);
    host.wait(session);
    const auto snapshot = session.finish(epoch);
    if (!snapshot)
        std::fprintf(stderr, "source failure: %s\n", session.diagnostic());
    assert(snapshot && snapshot->size() == 2 && session.state() == ProjectSourceState::Sealed);
    assert(session.read("other", 1) == FileIoStatus::InvalidRequest);
    return snapshot;
}

void cycle(Fixture& fixture, core::SharedPtr<ManagedFileBackend> backend)
{
    Harness host(std::move(backend));
    const auto snapshot = acquire(host);
    assert(snapshot->find("scripts/primary.arp")->receipt.bytes.size() == fixture.script.size());
    core::SharedPtr<const EditorScriptModules> prepared;
    core::pmr::String issue{&core::defaultCpuMemoryResource()};
    const auto name = fixture.domains.mainScene.name();
    assert(EditorScriptModules::prepareSources(*snapshot,
        host.files,
        epoch,
        fixture.project,
        fixture.domains.scope.modules(),
        prepared,
        issue));
    assert(prepared->find({fixture.project.scriptModules.front().resourceId, "Mover", 1}));
    assert(fixture.domains.mainScene.name() == name);
    assert(prepared->entries().front().source.substr(0, 3) != "\xef\xbb\xbf");
    assert(prepared->verifySources(*snapshot, host.files, epoch, issue));
    assert(!prepared->verifySources(std::filesystem::path{}, issue));
    const auto retained = prepared;
    auto invalid = fixture.project;
    invalid.scriptModules.front().authoring->maxSourceBytes = 1;
    assert(
        !EditorScriptModules::prepareSources(
            *snapshot, host.files, epoch, invalid, fixture.domains.scope.modules(), prepared, issue)
        && prepared == retained);
    invalid = fixture.project;
    invalid.scriptModules.front().source = "scripts/not-acquired.arp";
    assert(
        !EditorScriptModules::prepareSources(
            *snapshot, host.files, epoch, invalid, fixture.domains.scope.modules(), prepared, issue)
        && prepared == retained);
    assert(issue == "EDITOR-SCRIPT-SOURCE-NOT-ACQUIRED");
    scriptvm::ModuleRegistry unprepared;
    unsigned resolverCalls = 0;
    unprepared.setDeclaredSourceResolver([&](const auto&) -> scriptvm::ModuleResolution {
        ++resolverCalls;
        return {scriptvm::ModuleResolutionStatus::Ready,
            fixture.domains.scope.modules().loadModule("arp_scene"),
            {}};
    });
    assert(!EditorScriptModules::prepareSources(
               *snapshot, host.files, epoch, fixture.project, unprepared, prepared, issue)
        && prepared == retained && resolverCalls == 0);
    assert(issue.find("EDITOR-SCRIPT-IMPORT-CONTRACT-UNAVAILABLE") == 0);
    assert(!EditorScriptModules::prepareSources(*snapshot,
               host.files,
               epoch + 1,
               fixture.project,
               fixture.domains.scope.modules(),
               prepared,
               issue)
        && prepared == retained);
    assert(host.mounts.unmount(host.mount));
    const auto replacement = host.mounts.mount("workspace", fixture.memory());
    assert(replacement && !(replacement == host.mount));
    assert(!EditorScriptModules::prepareSources(*snapshot,
               host.files,
               epoch,
               fixture.project,
               fixture.domains.scope.modules(),
               prepared,
               issue)
        && prepared == retained);
    assert(!prepared->verifySources(*snapshot, host.files, epoch, issue));
    assert(!host.mounts.telemetry().inFlight && host.queue.pending() == 0
        && host.queue.reserved() == 0);
}

void negatives(Fixture& fixture)
{
    for (unsigned scenario = 0; scenario < 9; ++scenario) {
        Harness host(fixture.memory());
        ProjectSourceSession session(host.files, host.location(), budget);
        if (scenario == 0) {
            assert(session.read("../escape", 1) == FileIoStatus::InvalidRequest);
        } else if (scenario == 1) {
            assert(session.read("project.arp.yaml", fileLimit + 1) == FileIoStatus::BudgetExceeded);
        } else if (scenario == 2) {
            assert(session.read("missing", 1) == FileIoStatus::Accepted);
            host.wait(session);
            assert(session.state() == ProjectSourceState::Failed);
        } else {
            assert(session.read("project.arp.yaml", fileLimit) == FileIoStatus::Accepted);
            if (scenario == 3)
                session.cancel();
            if (scenario == 4)
                assert(host.mounts.unmount(host.mount));
            if (scenario == 5) {
                auto quota = core::makeShared<core::CpuMemoryQuota>(1);
                {
                    core::CpuMemoryQuotaScope scope(quota);
                    host.queue.dispatch(8);
                }
                assert(quota->liveBytes() == 0);
            }
            host.wait(session);
            host.queue.dispatch(8);
            if (scenario == 6) {
                assert(!session.finish(epoch + 1));
            } else if (scenario == 7) {
                assert(session.read("project.arp.yaml", fileLimit) == FileIoStatus::InvalidRequest);
            } else if (scenario == 8) {
                host.files.shutdown();
                assert(!session.finish(epoch));
            }
        }
        assert(!session.finish(epoch));
        assert(session.diagnostic());
        assert(host.queue.pending() == 0 && host.queue.reserved() == 0);
        assert(!host.mounts.telemetry().inFlight);
    }
    Harness host(fixture.memory());
    {
        ProjectSourceSession pending(host.files, host.location(), budget);
        assert(pending.read("project.arp.yaml", fileLimit) == FileIoStatus::Accepted);
    }
    host.queue.dispatch(8);
    assert(host.queue.pending() == 0 && !host.mounts.telemetry().inFlight);
    auto location = host.location();
    location.epoch = 0;
    ProjectSourceSession invalid(host.files, location, budget);
    assert(invalid.state() == ProjectSourceState::Failed);
    for (unsigned mode = 0; mode < 5; ++mode) {
        Harness broken(core::makeShared<ReceiptFault>(mode));
        ProjectSourceSession rejected(broken.files, broken.location(), budget);
        const auto submitted = rejected.read("project.arp.yaml", 2);
        if (submitted != FileIoStatus::Accepted)
            std::fprintf(stderr,
                "fault mode %u submit=%u diagnostic=%s\n",
                mode,
                static_cast<unsigned>(submitted),
                rejected.diagnostic());
        assert(submitted == FileIoStatus::Accepted);
        broken.wait(rejected);
        assert(rejected.state() == ProjectSourceState::Failed && !rejected.finish(epoch));
        assert(!rejected.acquired("project.arp.yaml"));
        constexpr const char* expected[] = {"PROJECT-SOURCE-VERSION",
            "PROJECT-SOURCE-INTEGRITY",
            "PROJECT-SOURCE-BUDGET",
            "PROJECT-SOURCE-VERSION",
            "PROJECT-SOURCE-MISSING"};
        assert(core::StringView(rejected.diagnostic()) == expected[mode]);
    }
    {
        Harness versioned(fixture.memory());
        ProjectSourceSession session(versioned.files, versioned.location(), budget);
        FileVersionToken wrong;
        wrong.kind = FileVersionKind::ContentSha256;
        assert(session.read("project.arp.yaml", fileLimit, wrong, fileLimit)
            == FileIoStatus::Accepted);
        versioned.wait(session);
        assert(session.state() == ProjectSourceState::Failed && !session.finish(epoch));
    }
    {
        Harness denied(fixture.memory());
        ProjectSourceSession session(denied.files, denied.location(), budget);
        auto quota = core::makeShared<core::CpuMemoryQuota>(1);
        {
            core::CpuMemoryQuotaScope scope(quota);
            assert(session.read("project.arp.yaml", 1) == FileIoStatus::BudgetExceeded);
        }
        assert(quota->liveBytes() == 0 && denied.queue.pending() == 0);
    }
    for (bool fileCount : {false, true}) {
        Harness bounded(fixture.memory());
        const ProjectInputBudget limits{fileCount ? 1u : 4u,
            fileLimit,
            fileCount ? fileLimit : fileLimit + fixture.yaml.size() - 1};
        ProjectSourceSession session(bounded.files, bounded.location(), limits);
        assert(session.read("project.arp.yaml", fileLimit) == FileIoStatus::Accepted);
        bounded.wait(session);
        assert(session.read("scripts/primary.arp", fileLimit) == FileIoStatus::BudgetExceeded);
        assert(!session.finish(epoch) && !bounded.mounts.telemetry().inFlight);
    }
    {
        Harness full(fixture.memory());
        for (unsigned i = 0; i < full.queue.capacity(); ++i)
            assert(full.queue.post({[](void*) {}, nullptr}).accepted());
        ProjectSourceSession session(full.files, full.location(), budget);
        assert(session.read("project.arp.yaml", fileLimit) == FileIoStatus::QueueFull);
        assert(session.state() == ProjectSourceState::Failed && !full.mounts.telemetry().inFlight);
        full.queue.dispatch(full.queue.capacity());
        assert(full.queue.pending() == 0 && full.queue.reserved() == 0);
    }
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    if (argc == 3 && core::StringView(argv[1]) == "--write-fixtures") {
        fixture.write(argv[2]);
        return 0;
    }
    const bool http = argc == 3 && core::StringView(argv[1]) == "--http";
    const bool quick = argc == 2 && core::StringView(argv[1]) == "--quick";
    const bool native = argc == 2 && core::StringView(argv[1]) == "--native";
    assert(argc == 1 || quick || http || native);
    unified_io_fixture::Workspace workspace;
    if (native)
        fixture.write(workspace.root);
    const auto run = [&] {
        if (http) {
            HttpFileBackendDesc desc;
            desc.baseUrl = argv[2];
            desc.maxInFlight = 1;
            desc.maxBytesInFlight = 2 * fileLimit;
            desc.maxPathBytes = 1024;
            desc.maxHeaderBytes = 8192;
            desc.ioChunkBytes = 1024;
            desc.timeoutMs = 3000;
            desc.cancellationPollMs = 2;
            cycle(fixture, core::makeShared<HttpFileBackend>(desc));
        } else if (native) {
            auto desc = workspace.descriptor();
            desc.ioChunkBytes = 1024;
            cycle(fixture, core::makeShared<NativeDirectoryFileBackend>(desc));
        } else {
            cycle(fixture, fixture.memory());
            negatives(fixture);
        }
    };
    if (quick) {
        run();
        std::puts("ProjectSourceSession/1 quick PASS");
        return 0;
    }
    for (unsigned i = 0; i < 20; ++i)
        run();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < 20; ++i)
        run();
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Project sources 20+20: core bytes %llu -> %llu; live %llu -> %llu\n",
        static_cast<unsigned long long>(before.liveBytes),
        static_cast<unsigned long long>(after.liveBytes),
        static_cast<unsigned long long>(before.allocationCount - before.deallocationCount),
        static_cast<unsigned long long>(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
