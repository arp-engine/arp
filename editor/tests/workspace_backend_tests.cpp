#include "editor/remote/editor_workspace_server.h"
#include "editor/tests/fixtures/workspace_wire_host.h"
#include "tests/fixtures/workspace_backend_fixture.h"

namespace {
using namespace arp;

void lateDelivery(editor::tests::WorkspaceWireHost& host)
{
    using namespace workspace_backend_fixture;
    {
        Client client(descriptor(host.origin()));
        client.connect([&] {
            host.pump();
        });
        runtime::CancellationSource cancellation;
        auto request = client.save("folder/cancel-after.arp", "committed before cancellation");
        request.cancellation = cancellation.token();
        Probe<FileSaveResult> saved;
        assert(client.files.save(std::move(request), {decltype(saved)::complete, &saved})
            == FileIoStatus::Accepted);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((!host.committedBeforeCancel || !client.queue.pending())
            && std::chrono::steady_clock::now() < deadline) {
            host.pump();
            std::this_thread::yield();
        }
        assert(host.committedBeforeCancel && client.queue.pending() && saved.calls == 0);
        cancellation.cancel();
        client.queue.dispatch(8);
        assert(saved.calls == 1 && saved.result.commitState == FileCommitState::Committed
            && saved.result.deliveryStatus == FileIoStatus::Cancelled
            && saved.result.receipt.valid());
    }
    Probe<FileReadResult> late;
    {
        Client client(descriptor(host.origin()));
        client.connect([&] {
            host.pump();
        });
        assert(client.files.read(client.read("folder/rules.arp"), {decltype(late)::complete, &late})
            == FileIoStatus::Accepted);
    }
    assert(late.calls == 1 && late.result.status == FileIoStatus::ServiceClosed
        && late.result.bytes.empty());
}

void serverRestart()
{
    using namespace workspace_backend_fixture;
    editor::tests::workspace::Host files(true);
    editor::EditorWorkspaceServer first;
    auto d = editor::tests::workspace::config();
    assert(first.start(FilePath::utf8(files.workspace.root), d));
    const auto port = first.port();
    const auto origin = pmr::String("http://127.0.0.1:") + formatUnsigned(port);
    Client client(descriptor(origin));
    client.connect([&] {
        first.pump();
    });
    Probe<FileSaveResult> saved;
    assert(client.files.save(client.save("folder/restart.arp", "survives restart"),
               {decltype(saved)::complete, &saved})
        == FileIoStatus::Accepted);
    wait(
        client.queue,
        [&] {
            return saved.calls == 1;
        },
        [&] {
            first.pump();
        });
    assert(saved.result.commitState == FileCommitState::Committed);
    first.stop();
    assert(first.stopped());
    editor::EditorWorkspaceServer second;
    d.sessionId = "session-b";
    assert(second.start(FilePath::utf8(files.workspace.root), d, port));
    Probe<FileReadResult> read;
    assert(client.files.read(client.read("folder/restart.arp"), {decltype(read)::complete, &read})
        == FileIoStatus::Accepted);
    wait(
        client.queue,
        [&] {
            return read.calls == 1;
        },
        [&] {
            second.pump();
        });
    assert(read.result.status == FileIoStatus::StaleLocation && !client.backend->connected()
        && !client.files.capability(client.mount).supportsRead);
    client.connect([&] {
        second.pump();
    });
    Probe<FileSaveReceiptResult> old;
    assert(
        client.files.receipt(client.receipt(saved.result.receipt), {decltype(old)::complete, &old})
        == FileIoStatus::StaleLocation);
    assert(old.calls == 0);
    assert(client.files.read(client.read("folder/restart.arp"), {decltype(read)::complete, &read})
        == FileIoStatus::Accepted);
    wait(
        client.queue,
        [&] {
            return read.calls == 2;
        },
        [&] {
            second.pump();
        });
    assert(read.result.status == FileIoStatus::Success
        && std::string_view(
               reinterpret_cast<const char*>(read.result.bytes.data()), read.result.bytes.size())
            == "survives restart");
    second.stop();
}

void cycle()
{
    editor::tests::WorkspaceWireHost host;
    workspace_backend_fixture::exercise(
        host.origin(),
        "folder",
        [&] {
            host.pump();
        },
        true);
    assert(host.dropped == 1 && host.corrupted == 2);
    const auto disk = [&](std::string_view name, std::string_view expected) {
        workspace_backend_fixture::Client client(
            workspace_backend_fixture::descriptor(host.origin()));
        client.connect([&] {
            host.pump();
        });
        unified_io_fixture::Probe<core::io::FileReadResult> probe;
        assert(client.files.read(client.read(name), {decltype(probe)::complete, &probe})
            == core::io::FileIoStatus::Accepted);
        workspace_backend_fixture::wait(
            client.queue,
            [&] {
                return probe.calls == 1;
            },
            [&] {
                host.pump();
            });
        assert(probe.result.status == core::io::FileIoStatus::Success
            && std::string_view(reinterpret_cast<const char*>(probe.result.bytes.data()),
                   probe.result.bytes.size())
                == expected);
    };
    disk("folder/lost.arp", "# lost response\n");
    disk("folder/new.arp", "# updated\n");
    lateDelivery(host);
}
} // namespace

int main(int argc, char** argv)
{
    const auto loops = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20u : 1u;
    cycle();
    serverRestart();
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < loops; ++i)
        cycle();
    const auto after = arp::core::defaultCpuAllocator().stats();
    std::printf(
        "Workspace actual HTTP backend %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
