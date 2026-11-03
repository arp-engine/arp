#include "core/io/file_io.h"
#include "core/io/filesystem_path.h"
#include "core/io/memory_file_backend.h"
#include "core/io/mounted_file_backend.h"
#include "core/runtime/async_runtime.h"
#include "examples/build_a_queen/web_font_source_task.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace {
using namespace arp;
using namespace core::io;
using Task = build_a_queen::WebFontSourceTask;
using Font = build_a_queen::WebFontResources;
using Phase = build_a_queen::WebFontSourcePhase;
enum class Fault {
    None,
    Missing,
    Version,
    Png,
    Hash,
    CancelBefore,
    CancelQueued,
    DestroyQueued,
    Stale,
    Closed,
    External
};

void cycle(const std::filesystem::path& root, Fault fault)
{
    auto memory = core::makeShared<MemoryFileBackend>(
        MemoryFileBackendDesc{3, Font::maxSourceBytes, 1, Font::maxFileBytes, 512});
    for (const auto* name : Font::inputs) {
        if (fault == Fault::Missing && std::string_view(name) == "font.png")
            continue;
        auto raw = core::readBinaryFile(root / name, Font::maxFileBytes);
        assert(raw.success);
        if (fault == Fault::Png && std::string_view(name) == "font.png")
            raw.bytes[0] ^= 1;
        if ((fault == Fault::Version || fault == Fault::Hash)
            && std::string_view(name) == "font.yaml") {
            auto text = core::pmr::String(
                reinterpret_cast<const char*>(raw.bytes.data()), raw.bytes.size());
            const auto token = fault == Fault::Version ? "contractVersion: 1" : "rgba8Sha256: ";
            const auto at = text.find(token);
            assert(at != text.npos);
            if (fault == Fault::Version)
                text.replace(at, std::string_view(token).size(), "contractVersion: 99");
            else {
                // Corrupt a declared digest character, preserving valid YAML.
                const auto digest = at + std::string_view(token).size();
                assert(digest != text.npos);
                text[digest] = text[digest] == '0' ? '1' : '0';
            }
            raw.bytes.assign(text.begin(), text.end());
        }
        assert(memory->registerFile(name, raw.bytes.data(), raw.bytes.size())
            == FileIoStatus::Success);
    }
    auto mounts = core::makeShared<MountedFileBackend>(
        MountedFileBackendDesc{1, 1, Font::maxFileBytes, 32, 512});
    const auto mount = mounts->mount("font", memory);
    assert(mount);
    core::runtime::AsyncRuntimeDesc desc;
    desc.enableWork = desc.enableThreadPoolFileBackend = false;
    desc.fileBackend = mounts;
    desc.completionQueueCapacity = desc.completionBudget = 2;
    core::runtime::AsyncRuntime runtime(std::move(desc));
    assert(runtime.valid());
    core::runtime::CancellationSource external;
    auto task = core::makeUnique<Task>(
        runtime.fileService(), runtime.completionQueue(), mount, external.token());
    assert(task->phase() == Phase::Pending && !task->resources());
    if (fault == Fault::CancelBefore)
        task->cancel();
    if (fault == Fault::Closed)
        runtime.fileService().shutdown();
    task->advance();
    if (fault == Fault::CancelQueued)
        task->cancel();
    if (fault == Fault::DestroyQueued)
        task.reset();
    bool remounted = false;
    for (unsigned dispatches = 0; task && task->phase() == Phase::Pending && dispatches < 1000;
        ++dispatches) {
        assert(runtime.dispatch(1).success());
        if (fault == Fault::External)
            external.cancel();
        if (fault == Fault::Stale && !remounted && runtime.telemetry().fileService.submitted) {
            assert(mounts->unmount(mount));
            assert(mounts->mount("font", memory));
            remounted = true;
        }
        task->advance();
        std::this_thread::yield();
    }
    runtime.shutdown();
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
    assert(!memory->telemetry().inFlight && !mounts->telemetry().inFlight);
    if (!task)
        return;
    if (fault != Fault::None) {
        assert(task->phase() == Phase::Failed || task->phase() == Phase::Cancelled);
        assert(!task->resources() && !task->diagnostic().empty());
        return;
    }
    assert(task->phase() == Phase::Ready && task->readCount() == Font::inputs.size());
    auto owner = task->resources();
    task.reset();
    assert(owner && owner->font() && owner->font()->valid());
    const auto* prepared = owner->font();
    // Compare the original local loader against the acquired CPU resource after
    // task destruction and IO shutdown, including every glyph's exact metrics.
    resource::ResourceManager reference;
    assert(reference
            .load<scene2d::SpriteAtlasResource>({},
                core::pmr::String(Font::atlasId),
                core::FilePath::utf8(root / Font::inputs[0]),
                core::FilePath::utf8(root / Font::inputs[1]),
                Font::maxFileBytes)
            .success);
    assert(reference
            .load<ui::UiBitmapFontResource>({},
                core::pmr::String(Font::fontId),
                core::FilePath::utf8(root / Font::inputs[2]),
                core::pmr::String(Font::atlasId),
                128 * 1024,
                95)
            .success);
    const auto* original = reference.find<ui::UiBitmapFontResource>(Font::fontId).get();
    assert(original && prepared->fontToken() == original->fontToken()
        && prepared->sizePx() == original->sizePx() && prepared->page() == original->page());
    assert(prepared->atlas().get()->image().rgba8 == original->atlas().get()->image().rgba8);
    for (unsigned c = 32; c < 127; ++c) {
        const auto* a = prepared->glyph(c);
        const auto* b = original->glyph(c);
        assert(a && b && a->sliceName == b->sliceName && a->advanceX == b->advanceX
            && a->bearingX == b->bearingX && a->bearingY == b->bearingY);
        const auto* sa = prepared->atlas().get()->slice(a->sliceName);
        const auto* sb = original->atlas().get()->slice(b->sliceName);
        assert(sa && sb && sa->x == sb->x && sa->y == sb->y && sa->width == sb->width
            && sa->height == sb->height && sa->pivot.x == sb->pivot.x && sa->pivot.y == sb->pivot.y
            && sa->pixelsPerUnit == sb->pixelsPerUnit);
    }
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc >= 2);
    { // Only existing process-resident reflection metadata precedes the baseline.
        resource::ResourceManager metadata;
        resource::ResourceBase carrier(resource::ResourceReference{});
        (void)reflection::object::undefined();
    }
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < (argc > 2 ? 20u : 1u); ++i)
        for (const auto fault : {Fault::None,
                 Fault::Missing,
                 Fault::Version,
                 Fault::Png,
                 Fault::Hash,
                 Fault::CancelBefore,
                 Fault::CancelQueued,
                 Fault::DestroyQueued,
                 Fault::Stale,
                 Fault::Closed,
                 Fault::External})
            cycle(argv[1], fault);
    const auto after = core::defaultCpuAllocator().stats();
    std::printf("Queen font core baseline=%llu/%llu retired=%llu/%llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)after.liveBytes,
        (unsigned long long)(after.allocationCount - after.deallocationCount));
    assert(before.liveBytes == after.liveBytes
        && before.allocationCount - before.deallocationCount
            == after.allocationCount - after.deallocationCount);
}
