#include "core/encoding/yaml_contract.h"
#include "core/hash/sha256.h"
#include "editor/remote/editor_workspace_protocol.h"
#include "editor/tests/fixtures/lost_receipt_backend.h"
#include "editor/tests/fixtures/workspace_service_fixture.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using namespace editor::tests::workspace;
using Y = core::YamlContract;
using Node = Y::Node;
using Response = network::http::Response;
constexpr const char* noneVersion =
    R"({"kind":"none","digest":"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="})";
constexpr const char* base =
    R"("type":"arp.workspace.request","contractVersion":2,"workspace":"fixture","session":"session-a",)";

network::http::Request request(std::string_view fields, const char* credential = readToken)
{
    network::http::Request value;
    value.set_method(network::http::Method::Post);
    value.set_target("/workspace/v2");
    value.add_header("Host", "127.0.0.1:8094");
    value.add_header("Authorization", core::pmr::String("Bearer ") + credential);
    value.set_content_type(network::http::ContentType::Json);
    value.set_body_text(core::pmr::String("{") + base + core::pmr::String(fields) + "}");
    return value;
}

Response send(Host& host, EditorWorkspaceProtocol& protocol, network::http::Request value)
{
    Response response;
    unsigned calls = 0;
    protocol.handle(value, [&](Response result) {
        response = std::move(result);
        ++calls;
    });
    unified_io_fixture::wait(host.queue, [&] {
        return calls == 1;
    });
    assert(calls == 1);
    return response;
}

Node parsed(const Response& response)
{
    if (response.status().value() != 200)
        std::fprintf(stderr,
            "Unexpected workspace response %u: %.*s\n",
            response.status().value(),
            int(response.body_text().size()),
            response.body_text().data());
    assert(response.status().value() == 200);
    return Y::parseSingleDocument(response.body_text());
}

core::pmr::String readFields(std::string_view path = "folder/rules.arp",
    unsigned offset = 0,
    unsigned size = 0,
    std::string_view version = noneVersion,
    unsigned versionBytes = 0)
{
    return core::pmr::String(R"("operation":"read","path":")") + core::pmr::String(path)
        + R"(","maxBytes":65536,"offset":)" + core::formatUnsigned(offset) + R"(,"size":)"
        + core::formatUnsigned(size) + R"(,"maxVersionBytes":)" + core::formatUnsigned(versionBytes)
        + R"(,"version":)" + core::pmr::String(version);
}

core::pmr::String versionJson(const Node& node)
{
    return core::pmr::String(R"({"kind":")") + Y::text(node, "kind") + R"(","digest":")"
        + Y::text(node, "digest") + R"("})";
}

core::pmr::String replaceFields(unsigned id, std::string_view data)
{
    core::ByteBuffer bytes(data.begin(), data.end());
    return core::pmr::String(R"("operation":"replace","path":"folder/new.arp","id":)")
        + core::formatUnsigned(id) + R"(,"precondition":"missing","version":)" + noneVersion
        + R"(,"maxWriteBytes":65536,"maxBaselineBytes":0,"bytes":)"
        + core::formatUnsigned(data.size()) + R"(,"data":")" + core::Base64::encode(bytes) + '"';
}

core::pmr::String receiptFields(
    std::string_view operation, std::uint64_t id, std::uint64_t scope, std::string_view version)
{
    return core::pmr::String(R"("operation":")") + core::pmr::String(operation) + R"(","id":)"
        + core::formatUnsigned(id) + R"(,"receiptScope":)" + core::formatUnsigned(scope)
        + R"(,"requestVersion":)" + core::pmr::String(version);
}

core::pmr::String rawVersion(std::string_view wire)
{
    return core::pmr::String(R"({"kind":"content-sha256","digest":")")
        + core::Base64::encode(core::Sha256::digest(wire)) + R"("})";
}

void admissionAndShutdown(bool native)
{
    Host host(native);
    auto desc = config(host.mount);
    desc.preparationBytes = 1;
    {
        EditorWorkspaceProtocol limited(host.files, desc);
        assert(limited.valid());
        const auto result =
            send(host, limited, request(replaceFields(1, "# not accepted\n"), writeToken));
        assert(result.status().value() == 503 && !limited.pending()
            && host.mounts.telemetry().inFlight == 0 && host.queue.reserved() == 0);
    }
    desc = config(host.mount);
    desc.writeToken.clear();
    {
        EditorWorkspaceProtocol readonly(host.files, desc);
        assert(readonly.valid());
        assert(
            send(host, readonly, request(replaceFields(1, "# denied\n"))).status().value() == 403);
    }
    unsigned calls = 0;
    Response terminal;
    {
        EditorWorkspaceProtocol closing(host.files, config(host.mount));
        closing.handle(request(readFields()), [&](Response value) {
            terminal = std::move(value);
            ++calls;
        });
        assert(closing.pending() && calls == 0);
        // Destroy the protocol while an accepted callback retains its private
        // state. The FileService and completion queue deliberately outlive it.
    }
    unified_io_fixture::wait(host.queue, [&] {
        return calls == 1;
    });
    assert(calls == 1 && terminal.status().value() == 200 && host.mounts.telemetry().inFlight == 0
        && host.queue.reserved() == 0);
}

void cycle(bool native, bool lost = false)
{
    Host host(native);
    if (lost) {
        auto backend = host.backend;
        assert(host.mounts.unmount(host.mount));
        host.backend =
            core::makeShared<editor::testfixtures::LostReceiptBackend>(std::move(backend));
        host.mount = host.mounts.mount("workspace", host.backend);
    }
    EditorWorkspaceProtocol protocol(host.files, config(host.mount));
    assert(protocol.valid());
    {
        auto oldRoute = request(readFields());
        oldRoute.set_target("/workspace/v1");
        const auto routeRejected = send(host, protocol, std::move(oldRoute));
        assert(routeRejected.status().value() == 400
            && Y::text(Y::parseSingleDocument(routeRejected.body_text()), "diagnostic")
                == "WORKSPACE-ROUTE");
        auto oldVersion = request("");
        oldVersion.set_body_text(
            R"({"type":"arp.workspace.request","contractVersion":1,"workspace":"fixture","operation":"info"})");
        assert(send(host, protocol, std::move(oldVersion)).status().value() == 400);
        assert(!protocol.pending() && host.queue.reserved() == 0);
    }
    auto infoRequest = request("");
    infoRequest.set_body_text(
        R"({"type":"arp.workspace.request","contractVersion":2,"workspace":"fixture","operation":"info"})");
    const auto info = parsed(send(host, protocol, std::move(infoRequest)));
    const auto scope = Y::uint64(Y::required(info, "receiptScope"));
    assert(scope && Y::boolean(info, "supportsRead") && Y::boolean(info, "supportsEnumeration"));
    auto current = parsed(send(host, protocol, request(readFields())));
    core::ByteBuffer bytes;
    assert(core::Base64::decode(Y::text(current, "data"), bytes));
    assert(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())
        == std::string_view(editor::tests::workspace::saved));
    auto list =
        core::pmr::String(
            R"("operation":"list","path":"folder","maxResultBytes":8192,"maxScanEntries":16,"maxScanNameBytes":1024,"pageSize":2,"offset":0,"version":)")
        + noneVersion;
    auto listing = parsed(send(host, protocol, request(list)));
    assert(Y::uint64(Y::required(listing, "totalEntries")) == 4);
    assert(Y::sequence(listing, "entries").size() == 2);
    auto denied = send(host, protocol, request(replaceFields(1, "# new\n")));
    assert(denied.status().value() == 403);
    auto saved = parsed(send(host, protocol, request(replaceFields(1, "# new\n"), writeToken)));
    assert(Y::text(saved, "commit") == (lost ? "unknown" : "committed"));
    {
        const auto original = request(replaceFields(1, "# new\n"), writeToken);
        core::Sha256 hash;
        hash.update(original.body_text());
        const auto digest =
            core::io::FileVersionEncoding::finish(hash, core::io::FileVersionKind::ContentSha256);
        const core::ByteBuffer bytes(digest.digest.begin(), digest.digest.end());
        assert(Y::text(saved["requestVersion"], "kind") == "content-sha256"
            && Y::text(saved["requestVersion"], "digest") == core::Base64::encode(bytes));
    }
    auto duplicate = parsed(send(host, protocol, request(replaceFields(1, "# new\n"), writeToken)));
    assert(Y::text(duplicate, "commit") == Y::text(saved, "commit"));
    assert(send(host, protocol, request(replaceFields(1, "# different\n"), writeToken))
               .status()
               .value()
        == 409);
    auto receipt = parsed(send(host,
        protocol,
        request(receiptFields("receipt", 1, scope, versionJson(saved["requestVersion"])))));
    assert(Y::text(receipt, "commit") == Y::text(saved, "commit"));
    assert(
        Y::text(receipt["requestVersion"], "digest") == Y::text(saved["requestVersion"], "digest"));
    assert(send(host,
               protocol,
               request(receiptFields("forget", 1, scope, versionJson(saved["requestVersion"])),
                   writeToken))
               .status()
               .value()
        == 200);
    assert(send(host, protocol, request(replaceFields(1, "# new\n"), writeToken)).status().value()
        == 409);
    assert(send(host,
               protocol,
               request(receiptFields("receipt", 1, scope, versionJson(saved["requestVersion"]))))
               .status()
               .value()
        == 410);
    auto discovery = request("");
    discovery.set_body_text(
        R"({"type":"arp.workspace.request","contractVersion":2,"workspace":"fixture","operation":"info"})");
    const auto discovered = parsed(send(host, protocol, std::move(discovery)));
    assert(Y::uint64(Y::required(discovered, "lastRequestId")) == 1);
    auto bad = request(readFields());
    bad.add_header("Origin", "http://hostile.example");
    assert(send(host, protocol, std::move(bad)).status().value() == 403);
    bad = request(readFields());
    bad.add_header("Authorization", core::pmr::String("Bearer ") + readToken);
    assert(send(host, protocol, std::move(bad)).status().value() == 401);
    bad = request(readFields());
    bad.set_body_text(core::pmr::String("{") + base + readFields() + R"(,"operation":"read"})");
    assert(send(host, protocol, std::move(bad)).status().value() == 400);
    auto escaped = parsed(send(host, protocol, request(readFields("../outside/secret.bin"))));
    assert(Y::text(escaped, "ioStatus") != "success" && !escaped.contains("data"));
    auto ranged = parsed(send(host, protocol, request(readFields("folder/rules.arp", 3, 2))));
    core::ByteBuffer slice;
    assert(core::Base64::decode(Y::text(ranged, "data"), slice)
        && std::string_view(reinterpret_cast<const char*>(slice.data()), slice.size()) == "# ");
    auto stat = parsed(send(host,
        protocol,
        request(
            R"("operation":"stat","path":"folder/rules.arp","maxResultBytes":8192,"contentVersion":true,"maxVersionBytes":65536)")));
    auto hash = versionJson(stat["metadata"]["version"]);
    assert(Y::text(stat["metadata"]["version"], "kind") == "content-sha256");
    auto pinned =
        parsed(send(host, protocol, request(readFields("folder/rules.arp", 0, 0, hash, 65536))));
    assert(Y::text(pinned, "ioStatus") == "success");
    auto changedListing =
        core::pmr::String(
            R"("operation":"list","path":"folder","maxResultBytes":8192,"maxScanEntries":16,"maxScanNameBytes":1024,"pageSize":2,"offset":2,"version":)")
        + versionJson(listing["next"]["version"]);
    assert(Y::text(parsed(send(host, protocol, request(changedListing))), "ioStatus")
        == "version-mismatch");
    auto cors = request(readFields());
    cors.add_header("Origin", "http://127.0.0.1:8093");
    auto corsResult = send(host, protocol, std::move(cors));
    assert(corsResult.header("Access-Control-Allow-Origin") == "http://127.0.0.1:8093");
    cors = request(readFields());
    cors.set_body_text("{}");
    cors.add_header("Origin", "http://127.0.0.1:8093");
    corsResult = send(host, protocol, std::move(cors));
    assert(corsResult.status().value() == 400
        && corsResult.header("Access-Control-Allow-Origin") == "http://127.0.0.1:8093");
    network::http::Request preflight;
    preflight.set_target("/workspace/v2");
    preflight.set_method(network::http::Method::Options);
    preflight.add_header("Host", "127.0.0.1:8094");
    preflight.add_header("Origin", "http://127.0.0.1:8093");
    preflight.add_header("Access-Control-Request-Method", "POST");
    assert(send(host, protocol, std::move(preflight)).status().value() == 204);
    unsigned throwCalls = 0;
    protocol.handle(request(readFields()), [&](Response) {
        ++throwCalls;
        throw 1;
    });
    unified_io_fixture::wait(host.queue, [&] {
        return throwCalls == 1;
    });
    assert(throwCalls == 1 && !protocol.pending());
#if defined(_WIN32)
    if (native) {
        assert(host.workspace.createJunction());
        const auto junction = parsed(send(host, protocol, request(readFields("link/secret.bin"))));
        assert(Y::text(junction, "ioStatus") != "success" && !junction.contains("data"));
    }
#endif
    auto fields = replaceFields(2, "# two\n");
    fields.replace(fields.find("folder/new.arp"),
        std::string_view("folder/new.arp").size(),
        "folder/new2.arp");
    unsigned pendingCalls = 0;
    Response terminal;
    protocol.handle(request(fields, writeToken), [&](Response value) {
        terminal = std::move(value);
        ++pendingCalls;
    });
    assert(protocol.pending() && pendingCalls == 0);
    const auto second = request(fields, writeToken);
    auto pending = send(host,
        protocol,
        request(receiptFields("receipt", 2, scope, rawVersion(second.body_text()))));
    assert(pending.status().value() == 202);
    assert(send(host, protocol, request(readFields())).status().value() == 503);
    unified_io_fixture::wait(host.queue, [&] {
        return pendingCalls == 1;
    });
    assert(Y::text(parsed(terminal), "commit") == "committed");
    fields = replaceFields(3, "# three\n");
    fields.replace(fields.find("folder/new.arp"),
        std::string_view("folder/new.arp").size(),
        "folder/new3.arp");
    assert(Y::text(parsed(send(host, protocol, request(fields, writeToken))), "commit")
        == "committed");
    assert(
        send(host, protocol, request(replaceFields(4, "# denied\n"), writeToken)).status().value()
        == 429);
    protocol.shutdown();
    assert(send(host, protocol, request(readFields())).status().value() == 503);
}
} // namespace

int main(int argc, char** argv)
{
    bool native = false, smoke = false;
    for (int i = 1; i < argc; ++i) {
        native |= std::string_view(argv[i]) == "--native";
        smoke |= std::string_view(argv[i]) == "--smoke";
    }
    cycle(native);
    cycle(native, true);
    admissionAndShutdown(native);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < (smoke ? 20u : 1u); ++i) {
        cycle(native);
        cycle(native, true);
        admissionAndShutdown(native);
    }
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Workspace protocol %u cycles: core bytes %llu -> %llu; live %llu -> %llu\n",
        smoke ? 20u : 1u,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
