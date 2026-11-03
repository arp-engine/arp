#include "core/encoding/yaml_contract.h"
#include "editor/remote/editor_workspace_server.h"
#include "editor/tests/fixtures/editor_remote_client_fixture.h"
#include "editor/tests/fixtures/workspace_service_fixture.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace editor::tests::workspace;
using Y = core::YamlContract;

void run()
{
    Host fixture(true);
    {
        for (unsigned field = 0; field < 3; ++field) {
            auto invalid = config();
            (field == 0          ? invalid.maxFileBytes
                    : field == 1 ? invalid.maxQueryBytes
                                 : invalid.maxRequestBytes) = SIZE_MAX;
            EditorWorkspaceServer oversized;
            assert(!oversized.start(core::FilePath::utf8(fixture.workspace.root), invalid)
                && oversized.stopped()
                && oversized.diagnostic() == "WORKSPACE-SERVER-BUDGET-OVERFLOW");
        }
        EditorWorkspaceServer missing;
        assert(
            !missing.start(core::FilePath::utf8(fixture.workspace.root / "missing-root"), config())
            && missing.stopped());
        EditorWorkspaceServer fileRoot;
        assert(!fileRoot.start(
                   core::FilePath::utf8(fixture.workspace.root / "folder/rules.arp"), config())
            && fileRoot.stopped());
    }
    {
        EditorWorkspaceServer readonly;
        auto desc = config();
        desc.writeToken.clear();
        assert(readonly.start(core::FilePath::utf8(fixture.workspace.root), desc));
        editor::tests::EditorHttpClientFixture<EditorWorkspaceServer> client(readonly, readToken);
        const auto result = client.send(network::http::Method::Post,
            "/workspace/v2",
            R"({"type":"arp.workspace.request","contractVersion":2,"workspace":"fixture","operation":"info"})",
            true);
        const auto info = Y::parseSingleDocument(result.second);
        assert(result.first == 200 && !Y::boolean(info, "writeAuthorized")
            && !Y::boolean(info, "observedBaselineReplace")
            && !Y::boolean(info, "strictConditionalReplace"));
        client.shutdown();
        readonly.stop();
        assert(readonly.stopped());
    }
    EditorWorkspaceServer server;
    auto descriptor = config();
    assert(server.start(core::FilePath::utf8(fixture.workspace.root), descriptor));
    {
        editor::tests::EditorHttpClientFixture<EditorWorkspaceServer> client(server, writeToken);
        auto result = client.send(network::http::Method::Post,
            "/workspace/v2",
            R"({"type":"arp.workspace.request","contractVersion":2,"workspace":"fixture","operation":"info"})",
            true);
        assert(result.first == 200);
        auto info = Y::parseSingleDocument(result.second);
        assert(
            Y::text(info, "session") == "session-a" && Y::boolean(info, "observedBaselineReplace"));
        const auto replace =
            R"({"type":"arp.workspace.request","contractVersion":2,"workspace":"fixture","session":"session-a","operation":"replace","path":"folder/new.arp","id":1,"precondition":"missing","version":{"kind":"none","digest":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="},"maxWriteBytes":65536,"maxBaselineBytes":0,"bytes":6,"data":"aGVsbG8K"})";
        client.disconnectAfterHeaders("/workspace/v2", replace);
        result = client.send(network::http::Method::Post, "/workspace/v2", replace, true);
        assert(result.first == 200
            && Y::text(Y::parseSingleDocument(result.second), "commit") == "committed");
        const auto saved = Y::parseSingleDocument(result.second);
        const auto receiptWire =
            core::pmr::String(
                R"({"type":"arp.workspace.request","contractVersion":2,"workspace":"fixture","session":"session-a","operation":"receipt","id":1,"receiptScope":)")
            + core::formatUnsigned(Y::uint64(Y::required(info, "receiptScope")))
            + R"(,"requestVersion":{"kind":"content-sha256","digest":")"
            + Y::text(saved["requestVersion"], "digest") + R"("}})";
        result = client.send(network::http::Method::Post, "/workspace/v2", receiptWire, true);
        assert(result.first == 200
            && Y::text(Y::parseSingleDocument(result.second), "commit") == "committed");
        result = client.send(network::http::Method::Post,
            "/workspace/v2",
            R"({"type":"arp.workspace.request","contractVersion":2,"workspace":"fixture","session":"old-session","operation":"receipt","id":1})",
            true);
        assert(result.first == 409);
        result = client.send(network::http::Method::Post, "/workspace/v2", "{}", false);
        assert(result.first == 401);
    }
    server.stop();
    assert(!server.running() && server.stopped());
    core::io::FileReadRequest read;
    read.contractVersion = 1;
    read.mount = fixture.mount;
    read.pathScope = core::io::FilePathScope::MountedResource;
    read.path = "folder/new.arp";
    read.maxBytes = 64;
    unified_io_fixture::Probe<core::io::FileReadResult> disk;
    assert(fixture.files.read(read, {decltype(disk)::complete, &disk})
        == core::io::FileIoStatus::Accepted);
    unified_io_fixture::wait(fixture.queue, [&] {
        return disk.calls == 1;
    });
    assert(disk.result.status == core::io::FileIoStatus::Success
        && std::string_view(
               reinterpret_cast<const char*>(disk.result.bytes.data()), disk.result.bytes.size())
            == "hello\n");
}
} // namespace

int main(int argc, char** argv)
{
    const auto loops = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20u : 1u;
    run();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < loops; ++i)
        run();
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Workspace server %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
