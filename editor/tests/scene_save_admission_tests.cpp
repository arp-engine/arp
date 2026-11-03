#include "core/io/file_io.h"
#include "core/io/file_save_contract.h"
#include "editor/tests/fixtures/scene_persistence_io_fixture.h"
#include "editor/tests/fixtures/source_scene_save_fixture.h"

#include <array>

namespace {
using namespace arp;
using namespace editor;
using namespace editor::testfixtures;
using namespace editor::testfixtures::sources;
using namespace editor::testfixtures::persistence_io;

struct AdmissionCase {
    const char* name;
    core::pmr::String bytes;
    bool accepted;
};

void replaceRaw(Host& host, std::string_view bytes)
{
    const auto previous = read(host, "bad.yaml");
    check(previous.status == FileIoStatus::Success);
    FileSaveRequest request;
    request.mount = host.mount;
    request.pathScope = FilePathScope::MountedResource;
    request.path = "workspace/bad.yaml";
    request.bytes.assign(bytes.begin(), bytes.end());
    request.maxWriteBytes = request.maxBaselineBytes = fileBytes;
    request.precondition = FileSavePrecondition::ContentSha256;
    request.expectedVersion = previous.version;
    const auto expected = core::io::FileSaveContract::contentVersion(request);
    unified_io_fixture::Probe<FileSaveResult> probe;
    check(host.files.save(std::move(request), {decltype(probe)::complete, &probe})
        == FileIoStatus::Accepted);
    unified_io_fixture::wait(host.queue, [&] {
        return probe.calls == 1;
    });
    check(probe.result.status == FileIoStatus::Success
        && probe.result.deliveryStatus == FileIoStatus::Success
        && probe.result.commitState == FileCommitState::Committed
        && probe.result.committedVersion == expected);
}

void cycle(FixtureSource& fixture, unsigned mode, bool report)
{
    core::pmr::String crlf("\xef\xbb\xbf");
    for (const auto byte : fixture.v3) {
        if (byte == '\n')
            crlf += '\r';
        crlf += byte;
    }
    const std::array<AdmissionCase, 10> cases{{
        {"valid", fixture.v3, true},
        {"bom-crlf", crlf, true},
        {"empty", {}, false},
        {"wrong-type", "type: foreign.scene\ncontractVersion: 3\n", false},
        {"unknown-version", "type: arp.editor-scene\ncontractVersion: 999\n", false},
        {"malformed", "type: [unterminated\n", false},
        {"two-documents", fixture.v3 + "\n---\n" + fixture.v3, false},
        {"empty-second-document", fixture.v3 + "\n---\n", false},
        {"duplicate-key", fixture.v3 + "\ncontractVersion: 3\n", false},
        {"scripted-v4", fixture.scene, false},
    }};
    fixture.write("plain.yaml", fixture.v3);
    Host host(fixture, mode, {}, true);
    Service legacy;
    EditorShell shell;
    host.open(shell);
    for (const auto& row : cases) {
        check(host.finish(shell, Host::begin(shell, "plain.yaml")).scenePersistenceTaskStatus
            == ScenePersistenceTaskStatus::Succeeded);
        rename(shell, "Save admission candidate");
        core::pmr::String replacement, diagnostic;
        check(EditorSceneAuthoringCodec::encode(shell.sceneDocument(), replacement, diagnostic),
            diagnostic);
        replaceRaw(host, row.bytes);
        fixture.workspace.write("oracle.yaml", row.bytes);
        const auto result = finish(
            legacy, legacy.saveResolved(fixture.workspace.root / "oracle.yaml", replacement));
        const auto state = host.finish(shell, save(shell, "bad.yaml", true));
        const bool published =
            state.scenePersistenceTaskStatus == ScenePersistenceTaskStatus::Succeeded;
        check(
            result.success == row.accepted && result.committed == row.accepted, result.diagnostic);
        check(published == row.accepted && state.scenePersistenceTaskCommitted == row.accepted,
            state.scenePersistenceTaskDiagnostic);
        check(state.sceneSaveReceipt.published == row.accepted
            && state.sceneSaveReceipt.submitted == row.accepted);
        const auto expected =
            row.accepted ? std::string_view(replacement) : std::string_view(row.bytes);
        const auto mounted = read(host, "bad.yaml");
        check(mounted.status == FileIoStatus::Success);
        check(mounted.bytes.size() == expected.size()
            && (expected.empty()
                || std::memcmp(mounted.bytes.data(), expected.data(), expected.size()) == 0));
        check(core::readTextFile(fixture.workspace.root / "oracle.yaml").text == expected);
        check(shell.sceneDocument().scene().name() == "Save admission candidate"
            && shell.sceneDirty() == !row.accepted);
        if (report)
            std::printf(
                "case=%s legacy_success=%u mounted_success=%u legacy_diagnostic=%s mounted_diagnostic=%s\n",
                row.name,
                unsigned(result.success),
                unsigned(published),
                result.diagnostic.c_str(),
                state.scenePersistenceTaskDiagnostic.c_str());
    }
    noTemporaries(fixture.workspace.root);
    noTemporaries(fixture.fixture.files.root / "workspace");
}
} // namespace

int main(int argc, char** argv)
{
    const auto option = argc > 1 ? std::string_view(argv[1]) : std::string_view{};
    const unsigned mode = option.find("native") != std::string_view::npos ? 1 : 0;
    const bool smoke = option.find("smoke") != std::string_view::npos;
    FixtureSource fixture;
    cycle(fixture, mode, true);
    if (smoke) {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned round = 0; round < 20; ++round) {
            cycle(fixture, mode, false);
            const auto after = core::defaultCpuAllocator().stats();
            check(after.liveBytes == before.liveBytes);
            check(after.allocationCount - after.deallocationCount
                == before.allocationCount - before.deallocationCount);
        }
        std::puts(
            "admission_rows=10 ordinary_cycles=20 core_live_bytes_delta=0 core_live_count_delta=0");
    }
}
