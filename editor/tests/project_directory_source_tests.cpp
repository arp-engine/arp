#include "core/io/file_io.h"
#include "core/io/file_query_contract.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "editor/project/project_directory_session.h"
#include "editor/tests/fixtures/project_fixture.h"
#include "editor/workspace/project_browser_model.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace core::io;
constexpr ProjectDirectoryBudget directoryBudget{32, 2, 4096, 8192};
constexpr std::uint64_t epoch = 9;

// Delegates real memory IO, altering only query receipts for negative admission
// tests. This decorator is not native or HTTP backend evidence.
class QueryFault final : public ManagedFileBackend {
public:
    QueryFault(core::SharedPtr<ManagedFileBackend> inner, unsigned mode)
        : inner_(std::move(inner)), mode_(mode)
    {}

    FileBackendCapability capability() const noexcept override
    {
        auto result = inner_->capability();
        if (mode_ == 5)
            result.supportsEnumeration = false;
        return result;
    }

    FileReadResult validateRead(const FileReadRequest& request) const override
    {
        return inner_->validateRead(request);
    }

    FileIoStatus readAsync(const FileReadRequest& request,
        FileReadCompletion completion,
        core::runtime::CompletionQueue& queue) noexcept override
    {
        return inner_->readAsync(request, completion, queue);
    }

    void shutdown() noexcept override { inner_->shutdown(); }

    FileIoStatus queryAsync(const FileQueryRequest& request,
        FileQueryCompletion completion,
        core::runtime::CompletionQueue& queue) noexcept override
    {
        struct Pending {
            unsigned mode;
            FileQueryCompletion completion;

            static void finish(void* context, FileQueryResult result)
            {
                core::UniquePtr<Pending> owned{static_cast<Pending*>(context),
                    core::CpuAllocatedDeleter<Pending>{
                        context, &core::destroyCpuAllocatedUnique<Pending>}};
                if (result.status == FileIoStatus::Success) {
                    if (owned->mode == 0)
                        result.next.offset += 1;
                    if (owned->mode == 1)
                        result.totalEntries = UINT64_MAX;
                    if (owned->mode == 2 && result.entries.size() >= 2)
                        result.entries[1].name = result.entries[0].name;
                    if (owned->mode == 3 && !result.entries.empty())
                        result.entries[0].name = "../escape";
                    if (owned->mode == 4)
                        result.metadata.version.kind = FileVersionKind::ContentSha256;
                    if (owned->mode == 6 && !result.entries.empty())
                        result.entries[0].metadata.kind = static_cast<FileEntryKind>(255);
                    if (owned->mode == 7 && result.next.snapshot.kind == FileVersionKind::None)
                        result.next.snapshot.digest[0] = 1;
                }
                owned->completion.complete(owned->completion.userData, std::move(result));
            }
        };

        try {
            auto pending = core::makeUnique<Pending>(Pending{mode_, completion});
            const auto status =
                inner_->queryAsync(request, {Pending::finish, pending.get()}, queue);
            if (status == FileIoStatus::Accepted)
                pending.release();
            return status;
        } catch (...) {
            return FileIoStatus::BudgetExceeded;
        }
    }

private:
    core::SharedPtr<ManagedFileBackend> inner_;
    unsigned mode_;
};

class Fixture final {
public:
    Fixture()
    {
        manifest = editor::tests::ProjectFixture::document("assetPackages: []\nphysics2d: null\n");
        std::filesystem::create_directories(workspace.root / "workspace/nested/child");
        write("project.arp.yaml", manifest);
        write("Z.arp", "// Z\n");
        write("a.arp", "// a\n");
        write("nested/content.arp", "// child\n");
    }

    void write(std::string_view relative, std::string_view bytes)
    {
        std::ofstream file(
            workspace.root / "workspace" / std::filesystem::u8path(relative), std::ios::binary);
        file.write(bytes.data(), bytes.size());
        assert(file.good());
    }

    core::SharedPtr<MemoryFileBackend> memory() const
    {
        auto backend =
            core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{32, 65536, 1, 65536, 1024});
        for (const char* directory : {"workspace", "workspace/nested", "workspace/nested/child"})
            assert(backend->registerDirectory(directory) == FileIoStatus::Success);
        for (const auto* path : {"project.arp.yaml", "Z.arp", "a.arp", "nested/content.arp"}) {
            const auto bytes = core::readBinaryFile(workspace.root / "workspace" / path, 65536);
            assert(bytes.success
                && backend->registerFile(core::pmr::String("workspace/") + path,
                       bytes.bytes.data(),
                       bytes.bytes.size())
                    == FileIoStatus::Success);
        }
        return backend;
    }

    unified_io_fixture::Workspace workspace;
    core::pmr::String manifest;
};

class Harness final {
public:
    explicit Harness(core::SharedPtr<ManagedFileBackend> backend)
    {
        mount = mounts.mount("project", std::move(backend));
        assert(mount);
        ProjectSourceSession sources(files, {mount, "workspace", epoch}, {4, 4096, 8192});
        assert(sources.read("project.arp.yaml", 4096) == FileIoStatus::Accepted);
        unified_io_fixture::wait(queue, [&] {
            return sources.state() != ProjectSourceState::Pending;
        });
        project = sources.finish(epoch);
        assert(project);
    }

    core::SharedPtr<const ProjectDirectorySnapshot> directory(std::string_view relative = {})
    {
        ProjectDirectorySession session(files, project, relative, directoryBudget);
        while (session.state() == ProjectDirectoryState::Ready) {
            assert(session.next(epoch) == FileIoStatus::Accepted);
            assert(session.next(epoch) == FileIoStatus::QueueFull);
            unified_io_fixture::wait(queue, [&] {
                return session.state() != ProjectDirectoryState::Pending;
            });
        }
        if (session.state() != ProjectDirectoryState::Complete)
            std::fprintf(stderr, "directory: %s\n", session.diagnostic());
        auto result = session.finish(epoch);
        assert(result && session.state() == ProjectDirectoryState::Sealed);
        return result;
    }

    core::runtime::CompletionQueue queue{{8}};
    MountedFileBackend mounts{{2, 1, 128 * 1024, 64, 1024}};
    FileService files{queue, mounts};
    core::PoolHandle mount;
    core::SharedPtr<const ProjectSourceSnapshot> project;
};

void browserContracts(Harness& host)
{
    using Navigation = ProjectBrowserModel::SourceNavigation;
    const auto root = host.directory();
    assert(root->entries().size() == 4
        && root->version().kind == FileVersionKind::DirectorySnapshotSha256);
    ProjectBrowserModel empty, browser;
    assert(!empty.prepareSources(root, host.files, epoch, Navigation::Navigate, browser, 2));
    assert(browser.entries().front().name == "nested" && browser.entries()[1].name == "a.arp");
    assert(browser.sourceSnapshot() == root && !browser.refresh() && !browser.back()
        && !browser.forward());
    assert(browser.navigate("nested") == ProjectBrowserModel::NavigationResult::Rejected);
    core::pmr::String target = "sentinel";
    assert(!browser.sourceTarget(Navigation::Back, {}, target) && target == "sentinel");
    assert(
        !browser.sourceTarget(Navigation::Navigate, "../escape", target) && target == "sentinel");
    assert(browser.sourceTarget(Navigation::Navigate, "nested", target) && target == "nested");
    const auto nested = host.directory(target);
    assert(!browser.prepareSources(nested, host.files, epoch, Navigation::Navigate, browser, 2));
    assert(browser.currentDirectory() == "workspace/nested");
    assert(browser.sourceTarget(Navigation::Back, {}, target) && target.empty());
    assert(!browser.prepareSources(root, host.files, epoch, Navigation::Back, browser, 2));
    assert(browser.sourceTarget(Navigation::Forward, {}, target) && target == "nested");
    assert(!browser.prepareSources(nested, host.files, epoch, Navigation::Forward, browser, 2));
    auto retained = browser.sourceSnapshot();
    assert(browser.prepareSources(root, host.files, epoch, Navigation::Refresh, browser, 2));
    assert(
        browser.sourceSnapshot() == retained && browser.currentDirectory() == "workspace/nested");
    assert(browser.prepareSources(nested, host.files, epoch + 1, Navigation::Refresh, browser, 2));
    for (auto bytes : {1u, 64u}) {
        auto quota = core::makeShared<core::CpuMemoryQuota>(bytes);
        {
            core::CpuMemoryQuotaScope scope(quota);
            assert(browser.prepareSources(root, host.files, epoch, Navigation::Back, browser, 2));
        }
        assert(quota->liveBytes() == 0 && browser.sourceSnapshot() == retained);
    }
    browser.setSearch("CONTENT");
    assert(browser.visibleEntryIndices().size() == 1
        && browser.entries()[browser.visibleEntryIndices()[0]].name == "content.arp");
    std::size_t untouched = 987;
    assert(browser.prepareReveal("workspace/nested/content.arp", empty, untouched)
        && untouched == 987);
    const char* issue = nullptr;
    assert(root->validate(host.files, epoch, issue));
    const auto child = host.directory("nested/child");
    assert(child->entries().empty());
    assert(!browser.prepareSources(child, host.files, epoch, Navigation::Navigate, browser, 2));
    assert(browser.prepareSources(root, host.files, epoch, Navigation::Navigate, browser, 2));
    assert(browser.sourceSnapshot() == child);
}

void negatives(Fixture& fixture)
{
    for (unsigned mode : {0u, 1u, 2u, 3u, 4u, 6u}) {
        Harness host(core::makeShared<QueryFault>(fixture.memory(), mode));
        ProjectDirectorySession session(host.files, host.project, {}, directoryBudget);
        assert(session.next(epoch) == FileIoStatus::Accepted);
        host.queue.dispatch(8);
        assert(session.state() == ProjectDirectoryState::Failed && !session.finish(epoch));
    }
    Harness host(fixture.memory());
    {
        Harness invalid(core::makeShared<QueryFault>(fixture.memory(), 7));
        ProjectDirectorySession session(
            invalid.files, invalid.project, "nested/child", directoryBudget);
        assert(session.next(epoch) == FileIoStatus::Accepted);
        invalid.queue.dispatch(8);
        assert(session.state() == ProjectDirectoryState::Failed && !session.finish(epoch));
        assert(core::StringView(session.diagnostic()) == "PROJECT-DIRECTORY-CURSOR");
    }
    {
        std::weak_ptr<const ProjectDirectorySnapshot> lifetime;
        {
            const auto snapshot = host.directory("nested/child");
            lifetime = snapshot;
        }
        assert(lifetime.expired()); // Completion must release its private state.
    }
    {
        Harness unsupported(core::makeShared<QueryFault>(fixture.memory(), 5));
        ProjectDirectorySession session(
            unsupported.files, unsupported.project, "", directoryBudget);
        assert(session.next(epoch) == FileIoStatus::Unsupported);
        assert(session.state() == ProjectDirectoryState::Failed && !session.finish(epoch));
        assert(unsupported.queue.pending() == 0);
    }
    {
        auto memory = fixture.memory();
        Harness changed(memory);
        ProjectDirectorySession session(changed.files, changed.project, "", directoryBudget);
        assert(session.next(epoch) == FileIoStatus::Accepted);
        changed.queue.dispatch(8);
        assert(session.state() == ProjectDirectoryState::Ready);
        const std::uint8_t byte = 1;
        assert(memory->registerFile("workspace/new.arp", &byte, 1) == FileIoStatus::Success);
        assert(session.next(epoch) == FileIoStatus::Accepted);
        changed.queue.dispatch(8);
        assert(session.state() == ProjectDirectoryState::Failed && !session.finish(epoch));
        assert(core::StringView(session.diagnostic()) == "IO-QUERY-DIRECTORY-CHANGED");
    }
    {
        ProjectDirectorySession session(host.files, host.project, "nested/child", directoryBudget);
        assert(session.next(epoch) == FileIoStatus::Accepted);
        host.queue.dispatch(8);
        assert(session.state() == ProjectDirectoryState::Complete && !session.finish(epoch + 1));
        assert(session.state() == ProjectDirectoryState::Failed);
    }
    {
        ProjectDirectorySession session(host.files, host.project, "", directoryBudget);
        for (unsigned i = 0; i < host.queue.capacity(); ++i)
            assert(host.queue.post({[](void*) {}, nullptr}).accepted());
        assert(session.next(epoch) == FileIoStatus::QueueFull);
        host.queue.dispatch(8);
        assert(session.state() == ProjectDirectoryState::Failed);
    }
    for (bool destroy : {false, true}) {
        auto session = core::makeUnique<ProjectDirectorySession>(
            host.files, host.project, "", directoryBudget);
        assert(session->next(epoch) == FileIoStatus::Accepted);
        if (destroy)
            session.reset();
        else
            session->cancel();
        host.queue.dispatch(8);
        assert(host.mounts.telemetry().inFlight == 0 && host.queue.reserved() == 0);
        if (session)
            assert(!session->finish(epoch) && session->state() == ProjectDirectoryState::Cancelled);
    }
    {
        ProjectDirectorySession session(host.files, host.project, "../escape", directoryBudget);
        assert(session.state() == ProjectDirectoryState::Failed);
    }
    {
        ProjectDirectorySession session(host.files, host.project, "", {2, 1, 4096, 8192});
        assert(session.next(epoch) == FileIoStatus::Accepted);
        host.queue.dispatch(8);
        assert(session.state() == ProjectDirectoryState::Failed && !session.finish(epoch));
    }
    const auto snapshot = host.directory();
    assert(host.mounts.unmount(host.mount));
    assert(host.mounts.mount("project", fixture.memory()));
    const char* issue = nullptr;
    assert(!snapshot->validate(host.files, epoch, issue));
    ProjectBrowserModel original, output;
    assert(original.prepareSources(
        snapshot, host.files, epoch, ProjectBrowserModel::SourceNavigation::Navigate, output));
    assert(output.entries().empty());
}

void nativeChange(Fixture& fixture)
{
    auto desc = fixture.workspace.descriptor();
    Harness host(core::makeShared<NativeDirectoryFileBackend>(desc));
    ProjectDirectorySession session(host.files, host.project, "", directoryBudget);
    assert(session.next(epoch) == FileIoStatus::Accepted);
    unified_io_fixture::wait(host.queue, [&] {
        return session.state() != ProjectDirectoryState::Pending;
    });
    assert(session.state() == ProjectDirectoryState::Ready);
    fixture.write("new.arp", "// changed\n");
    assert(session.next(epoch) == FileIoStatus::Accepted);
    unified_io_fixture::wait(host.queue, [&] {
        return session.state() != ProjectDirectoryState::Pending;
    });
    assert(session.state() == ProjectDirectoryState::Failed && !session.finish(epoch));
    assert(std::filesystem::remove(fixture.workspace.root / "workspace/new.arp"));
    assert(core::StringView(session.diagnostic()) == "IO-QUERY-DIRECTORY-CHANGED");
}
} // namespace

int main(int argc, char** argv)
{
    Fixture fixture;
    const bool native = argc == 2 && core::StringView(argv[1]) == "--native";
    const bool smoke = argc == 2 && core::StringView(argv[1]) == "--smoke";
    assert(argc == 1 || native || smoke);
    const auto run = [&] {
        core::SharedPtr<ManagedFileBackend> backend;
        if (native) {
            auto desc = fixture.workspace.descriptor();
            desc.ioChunkBytes = 4096;
            backend = core::makeShared<NativeDirectoryFileBackend>(desc);
        } else
            backend = fixture.memory();
        Harness host(std::move(backend));
        browserContracts(host);
    };
    if (!native && !smoke) {
        run();
        negatives(fixture);
    } else {
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned i = 0; i < 20; ++i)
            run();
        const auto after = core::defaultCpuAllocator().stats();
        assert(before.liveBytes == after.liveBytes
            && before.allocationCount - before.deallocationCount
                == after.allocationCount - after.deallocationCount);
        std::printf("Directories 20+20 core bytes %llu -> %llu; live %llu -> %llu\n",
            (unsigned long long)before.liveBytes,
            (unsigned long long)after.liveBytes,
            (unsigned long long)(before.allocationCount - before.deallocationCount),
            (unsigned long long)(after.allocationCount - after.deallocationCount));
    }
    if (native)
        nativeChange(fixture);
    std::puts(
        "PASS versioned directory pages, private callbacks, source browser candidates and failure preservation");
}
