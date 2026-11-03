#include "core/io/file_io.h"
#include "editor/tests/fixtures/scene_persistence_io_fixture.h"
#include "tests/fixtures/unified_io_fixture.h"

#include <array>

namespace {
using namespace arp;
using namespace arp::editor;
using Service = EditorScenePersistence;
using namespace arp::editor::testfixtures::persistence_io;

void cycle()
{
    unified_io_fixture::Workspace files;
    core::pmr::String yaml, diagnostic;
    {
        EditorSceneDocument document(false);
        assert(EditorSceneAuthoringCodec::encode(document, yaml, diagnostic));
    }
    const auto next = yaml + "\n# next revision\n";
    const auto target = files.root / "scene.yaml";
    const auto marker = files.root / "marker.yaml";
    Service service;
    // The marker witnesses autonomous FIFO progress without taking any result.
    const std::array<core::PoolHandle, 4> handles{service.saveResolved(target, yaml),
        service.readResolved(target),
        service.saveResolved(target, next),
        service.saveResolved(marker, next)};
    for (const auto handle : handles)
        assert(handle);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!std::filesystem::exists(marker) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    assert(std::filesystem::exists(marker));
    assert(!service.readScript(files.root, "scene.yaml"));
    assert(std::string_view(service.diagnostic()) == "ARP-FILE-REQUEST-CAPACITY");
    const auto savedMarker = finish(service, handles[3]);
    assert(savedMarker.success && savedMarker.committed && equals(savedMarker, next));
    const auto latest = service.readResolved(target);
    assert(latest);
    const auto old = finish(service, handles[1]);
    assert(old.success && !old.committed && equals(old, yaml));
    for (const auto index : {0, 2}) {
        const auto saved = finish(service, handles[index]);
        assert(saved.success && saved.committed);
    }
    assert(equals(finish(service, latest), next));
    Result stale;
    assert(!service.take(handles[1], stale));
    const auto checked = finish(service, service.load(files.root, "scene.yaml"));
    assert(checked.success && equals(checked, next));
    const auto missing = finish(service, service.readResolved(files.root / "missing.yaml"));
    assert(!missing.success && missing.missing && !missing.committed && missing.bytes.empty());
    assert(missing.diagnostic == "ARP-FILE-MISSING");
    files.write("empty.yaml", {});
    const auto empty = finish(service, service.readScript(files.root, "empty.yaml"));
    assert(empty.success && empty.bytes.empty() && !empty.committed);
    assert(!finish(service, service.load(files.root, "empty.yaml")).success);
    const auto multiple = yaml + "\n---\n";
    files.write("multi.yaml", multiple);
    const auto invalid = finish(service, service.load(files.root, "multi.yaml"));
    assert(!invalid.success && invalid.diagnostic == "ARP-SAVE-CORRUPT-YAML");
    assert(equals(finish(service, service.readScript(files.root, "multi.yaml")), multiple));
    const core::pmr::String raw("\xef\xbb\xbftype: arp.editor-scene\r\ncontractVersion: 4\r\n");
    files.write("raw.yaml", raw);
    assert(equals(finish(service, service.readScript(files.root, "raw.yaml")), raw));
    const auto unsupported = finish(service, service.load(files.root, "raw.yaml"));
    assert(!unsupported.success && unsupported.diagnostic == "ARP-SAVE-UNSUPPORTED-CONTRACT");
    assert(!service.readScript(files.root, "../outside/secret.bin"));
    assert(std::string_view(service.diagnostic()) == "EDITOR-SCENE-PATH-UNSAFE");
    assert(!service.readScript(files.root / "absent", "scene.yaml"));
    assert(std::string_view(service.diagnostic()) == "EDITOR-SCENE-PROJECT-ROOT");
    assert(!service.saveResolved(target, {}));
    assert(std::string_view(service.diagnostic()) == "EDITOR-SCENE-DOCUMENT-LIMIT");
    // Transport failure must not expose a partial file, even at the exact size limit + 1.
    files.write("large.bin", std::string(EditorSceneAuthoringCodec::maxDocumentBytes + 1, 'x'));
    const auto large = finish(service, service.readResolved(files.root / "large.bin"));
    assert(!large.success && !large.committed && large.bytes.empty() && !large.diagnostic.empty());
    assert(large.diagnostic == "IO-READ-BYTE-BUDGET");
    files.write("exact.bin", std::string(EditorSceneAuthoringCodec::maxDocumentBytes, 'x'));
    const auto exact = finish(service, service.readResolved(files.root / "exact.bin"));
    assert(exact.success && exact.bytes.size() == EditorSceneAuthoringCodec::maxDocumentBytes);
    noTemporaries(files.root);
    // Accepted writes drain at destruction even when no owner polls or takes.
    std::filesystem::remove(marker);
    {
        Service draining;
        assert(draining.saveResolved(target, yaml));
        assert(draining.readResolved(target));
        assert(draining.saveResolved(target, next));
        assert(draining.saveResolved(marker, next));
    }
    assert(core::readTextFile(target).text == next);
    assert(core::readTextFile(marker).text == next);
    noTemporaries(files.root);
}
} // namespace

int main(int argc, char** argv)
{
    cycle();
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") {
        const auto before = core::defaultCpuAllocator().stats();
        for (unsigned round = 0; round < 20; ++round) {
            cycle();
            const auto after = core::defaultCpuAllocator().stats();
            assert(after.liveBytes == before.liveBytes);
            assert(after.allocationCount - after.deallocationCount
                == before.allocationCount - before.deallocationCount);
        }
        std::printf("ordinary_cycles=20 live_bytes_delta=0 live_count_delta=0\n");
    }
}
