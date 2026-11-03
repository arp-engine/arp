#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/memory_file_backend.h"
#include "examples/build_a_queen/native_hud_source_task.h"
#include "tests/fixtures/queen_source_runtime.h"
#include "ui/ui_font_source.h"

#include <array>
#include <cassert>
#include <cstdio>

namespace {
using namespace arp;
using namespace core::io;
using Sources = build_a_queen::NativeHudSources;
using Task = build_a_queen::NativeHudSourceTask;
using Phase = build_a_queen::StartupResourcePhase;
constexpr std::array<const char*, 3> files{Sources::metadataFile, "DroidSans.ttf", "white.png"};
enum class Fault {
    None,
    Missing,
    Font,
    Png,
    Version,
    Escape,
    Token,
    Cancel,
    Destroy,
    Stale,
    Closed,
    External
};

void compare(const Sources& input, const std::filesystem::path& root)
{
    assert(input.valid() && input.face.desc.family == "Droid Sans" && input.face.desc.sizePx == 18
        && input.face.desc.lineHeightPx == 22);
    assert(input.atlas->width == 1 && input.atlas->height == 1 && input.atlas->srgb
        && input.atlas->rgba8 == core::pmr::Vector<std::uint8_t>({255, 255, 255, 255}));
    auto referenceDesc = input.face.desc;
    referenceDesc.source.reset();
    referenceDesc.sourcePath = root / "DroidSans.ttf";
    const auto reference = ui::UiFontFaceLoader().load(referenceDesc);
    assert(reference.ok());
    const auto a = input.face.desc.source->bytes(), b = reference.face().desc.source->bytes();
    assert(a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin()));
    ui::UiFontRasterizeDesc raster;
    raster.face = input.face.desc;
    for (unsigned c = 32; c < 127; ++c)
        raster.text.push_back(static_cast<char>(c));
    raster.maxX = 1280;
    raster.maxY = 720;
    const auto prepared = ui::UiFontTextRasterizer().rasterize(raster);
    raster.face = reference.face().desc;
    const auto original = ui::UiFontTextRasterizer().rasterize(raster);
    assert(prepared.ok() && original.ok() && prepared.text().glyphCount == 95);
    assert(prepared.text().advanceX == original.text().advanceX
        && prepared.text().pixels.size() == original.text().pixels.size());
    for (std::size_t i = 0; i < prepared.text().pixels.size(); ++i) {
        const auto& x = prepared.text().pixels[i];
        const auto& y = original.text().pixels[i];
        assert(x.x == y.x && x.y == y.y && x.width == y.width && x.height == y.height
            && x.alpha == y.alpha);
    }
}

void cycle(
    const std::array<core::ByteBuffer, 3>& inputs, const std::filesystem::path& root, Fault fault)
{
    auto memory = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{3, Sources::maxSourceBytes, 1, Sources::maxFileBytes, 512});
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (fault == Fault::Missing && i == 1)
            continue;
        auto bytes = inputs[i];
        if ((fault == Fault::Font && i == 1) || (fault == Fault::Png && i == 2))
            bytes[0] ^= 1;
        if (i == 0
            && (fault == Fault::Version || fault == Fault::Escape || fault == Fault::Token)) {
            auto text =
                core::pmr::String(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            const auto from = fault == Fault::Version ? "ui.atlas/v2"
                : fault == Fault::Escape              ? "DroidSans.ttf"
                                                      : "queen.font";
            const auto to = fault == Fault::Version ? "ui.atlas/v99"
                : fault == Fault::Escape            ? "../outside.ttf"
                                                    : "other.font";
            const auto at = text.find(from);
            assert(at != text.npos);
            text.replace(at, std::string_view(from).size(), to);
            bytes.assign(text.begin(), text.end());
        }
        assert(memory->registerFile(files[i], bytes.data(), bytes.size()) == FileIoStatus::Success);
    }
    auto fixture = queen_source_fixture::create(memory, Sources::maxFileBytes);
    auto& runtime = *fixture.owner;
    core::runtime::CancellationSource cancellation;
    auto task = core::makeUnique<Task>(
        runtime.fileService(), runtime.completionQueue(), fixture.mount, cancellation.token());
    assert(!task->sources() && !task->readCount());
    if (fault == Fault::Closed)
        runtime.fileService().shutdown();
    task->advance();
    if (fault == Fault::Cancel)
        task->cancel();
    if (fault == Fault::Destroy)
        task.reset();
    bool remounted = false;
    for (unsigned n = 0; task && task->phase() == Phase::Pending && n < 10000; ++n) {
        assert(runtime.dispatch(1).success());
        if (fault == Fault::External)
            cancellation.cancel();
        if (fault == Fault::Stale && !remounted && runtime.telemetry().fileService.submitted) {
            assert(fixture.mounts->unmount(fixture.mount));
            assert(fixture.mounts->mount("queen-probe", memory));
            remounted = true;
        }
        task->advance();
    }
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
    assert(!memory->telemetry().inFlight && !fixture.mounts->telemetry().inFlight);
    if (!task)
        return;
    if (fault != Fault::None) {
        assert(task->phase() == Phase::Failed || task->phase() == Phase::Cancelled);
        assert(!task->sources() && !task->diagnostic().empty());
        return;
    }
    if (task->phase() != Phase::Ready)
        std::fprintf(
            stderr, "HUD: %.*s\n", int(task->diagnostic().size()), task->diagnostic().data());
    assert(task->phase() == Phase::Ready && task->readCount() == 3);
    auto prepared = task->sources();
    task.reset();
    fixture.owner.reset();
    fixture.mounts.reset();
    memory.reset();
    // Every IO and graph owner is gone before FreeType consumes this face.
    compare(*prepared, root);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    const auto root = std::filesystem::path(argv[1]);
    std::array<core::ByteBuffer, 3> inputs;
    for (std::size_t i = 0; i < files.size(); ++i) {
        auto raw = core::readBinaryFile(root / files[i], Sources::maxFileBytes);
        assert(raw.success);
        inputs[i] = std::move(raw.bytes);
    }
    // Atlas envelope reflection registers persistent type metadata on first use.
    // A complete retired warm cycle precedes the measured ordinary lifecycle.
    cycle(inputs, root, Fault::None);
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < (argc > 2 ? 20u : 1u); ++i)
        for (const auto fault : {Fault::None,
                 Fault::Missing,
                 Fault::Font,
                 Fault::Png,
                 Fault::Version,
                 Fault::Escape,
                 Fault::Token,
                 Fault::Cancel,
                 Fault::Destroy,
                 Fault::Stale,
                 Fault::Closed,
                 Fault::External})
            cycle(inputs, root, fault);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Native HUD core=%llu/%llu -> %llu/%llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)after.liveBytes,
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
