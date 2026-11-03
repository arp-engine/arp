#include "core/encoding/yaml_contract.h"
#include "editor/app/editor_workspace_connection_profile.h"
#include "tests/fixtures/workspace_backend_fixture.h"

#include <cstdio>

namespace {
using namespace arp;
using namespace editor;
using Y = core::YamlContract;

Y::Node fixture()
{
    const auto d = workspace_backend_fixture::descriptor("http://127.0.0.1:8094");
    Y::Node root = Y::Node::mapping();
    root["type"] = "arp.editor-workspace-connection";
    root["contractVersion"] = std::int64_t{1};
    root["endpoint"] = d.endpoint.c_str();
    root["credentialOrigin"] = d.credentialOrigin.c_str();
    root["workspaceId"] = d.workspaceId.c_str();
    root["projectRoot"] = "workspace";
    Y::Node io = Y::Node::mapping();
    const auto number = [](Y::Node& node, const char* key, std::uint64_t value) {
        node[key] = static_cast<std::int64_t>(value);
    };
    number(io, "maxPathBytes", d.maxPathBytes);
    number(io, "maxFileBytes", d.maxFileBytes);
    number(io, "maxRequestBytes", d.maxRequestBytes);
    number(io, "maxResponseBytes", d.maxResponseBytes);
    number(io, "preparationBytes", d.preparationBytes);
    number(io, "maxQueryBytes", d.maxQueryBytes);
    number(io, "maxDirectoryEntries", d.maxDirectoryEntries);
    number(io, "maxDirectoryNameBytes", d.maxDirectoryNameBytes);
    number(io, "maxHeaderBytes", d.maxHeaderBytes);
    number(io, "ioChunkBytes", d.ioChunkBytes);
    number(io, "timeoutMs", d.timeoutMs);
    number(io, "cancellationPollMs", d.cancellationPollMs);
    number(io, "maxDnsAddresses", d.maxDnsAddresses);
    root["io"] = std::move(io);
    Y::Node inputs = Y::Node::mapping();
    number(inputs, "maxFiles", d.maxDirectoryEntries);
    number(inputs, "maxFileBytes", d.maxFileBytes);
    number(inputs, "maxTotalBytes", 4 * d.maxFileBytes);
    Y::Node directory = Y::Node::mapping();
    number(directory, "maxEntries", d.maxDirectoryEntries);
    number(directory, "pageSize", 4);
    number(directory, "maxNameBytes", d.maxDirectoryNameBytes);
    number(directory, "maxPageBytes", d.maxQueryBytes);
    Y::Node reload = Y::Node::mapping();
    const ScriptDocumentSession::ReloadBudget r;
    number(reload, "maxHistoryEntries", r.maxHistoryEntries);
    number(reload, "maxHistoryBytes", r.maxHistoryBytes);
    Y::Node editor = Y::Node::mapping();
    editor["inputs"] = std::move(inputs);
    editor["directory"] = std::move(directory);
    editor["scriptReload"] = std::move(reload);
    number(editor, "maxDecodedBytes", 4 * d.maxFileBytes);
    number(editor, "maxDiagnosticBytes", 4096);
    number(editor, "maxHistoryEntries", 256);
    number(editor, "maxScriptDocuments", 64);
    root["editor"] = std::move(editor);
    return root;
}

void cycle()
{
    const auto root = fixture();
    const auto yaml = Y::serialize(root);
    const char* issue = nullptr;
    auto profile = EditorWorkspaceConnectionProfile::decode(
        yaml, workspace_backend_fixture::writeToken, yaml.size(), issue);
    assert(profile && !issue && profile->projectRoot == "workspace"
        && profile->io.token == workspace_backend_fixture::writeToken
        && profile->io.maxFileBytes == 65536 && profile->editor.inputs.maxFileBytes == 65536);
    for (unsigned i = 0; i < 12; ++i) {
        auto bad = root;
        switch (i) {
        case 0:
            bad["contractVersion"] = std::int64_t{2};
            break;
        case 1:
            bad["type"] = "arp.project";
            break;
        case 2:
            bad["token"] = "must not be persisted in configuration";
            break;
        case 3:
            bad["io"]["unknown"] = true;
            break;
        case 4:
            bad["editor"]["unknown"] = true;
            break;
        case 5:
            bad["projectRoot"] = "../outside";
            break;
        case 6:
            bad["projectRoot"] = "D:/wocc";
            break;
        case 7:
            bad["io"]["maxQueryBytes"] = std::int64_t{1};
            break;
        case 8:
            bad["editor"]["directory"]["pageSize"] = std::int64_t{0};
            break;
        case 9:
            bad["io"]["timeoutMs"] = true;
            break;
        case 10:
            bad["io"]["maxDirectoryEntries"] = std::int64_t{4294967296};
            break;
        case 11:
            bad["io"] = Y::Node::mapping();
            break;
        }
        const auto bytes = Y::serialize(bad);
        assert(!EditorWorkspaceConnectionProfile::decode(
                   bytes, workspace_backend_fixture::writeToken, bytes.size(), issue)
            && issue);
    }
    assert(!EditorWorkspaceConnectionProfile::decode(
        yaml, workspace_backend_fixture::writeToken, yaml.size() - 1, issue));
    assert(!EditorWorkspaceConnectionProfile::decode(
        std::string_view("\xc0\x80", 2), workspace_backend_fixture::writeToken, 2, issue));
    auto nul = yaml;
    nul.push_back('\0');
    assert(!EditorWorkspaceConnectionProfile::decode(
        nul, workspace_backend_fixture::writeToken, nul.size(), issue));
    const auto duplicate = yaml + "\ncontractVersion: 1\n";
    assert(!EditorWorkspaceConnectionProfile::decode(
        duplicate, workspace_backend_fixture::writeToken, duplicate.size(), issue));
    const auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope denyAllocation(quota);
        assert(!EditorWorkspaceConnectionProfile::decode(
            yaml, workspace_backend_fixture::writeToken, yaml.size(), issue));
    }
    assert(quota->liveBytes() == 0);
}
} // namespace

int main(int argc, char** argv)
{
    const auto loops = argc > 1 && std::string_view(argv[1]) == "--smoke" ? 20u : 1u;
    cycle();
    const auto before = arp::core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < loops; ++i)
        cycle();
    const auto after = arp::core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
    std::printf("Workspace profile %u cycles: core bytes %llu -> %llu, live %llu -> %llu\n",
        loops,
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
