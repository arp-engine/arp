#include "core/io/file_io.h"
#include "core/io/memory_file_backend.h"
#include "examples/common/native_startup_files.h"
#include "examples/survivors/record_session.h"
#include "tests/fixtures/queen_source_runtime.h"

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <thread>

namespace {
using namespace arp;
using namespace core::io;
using survivors::Record;
using survivors::RecordPhase;
using survivors::RecordSchema;
using survivors::RecordSession;

bool same(const Record& a, const Record& b)
{
    return a.bestKills == b.bestKills && a.bestSeconds == b.bestSeconds && a.wins == b.wins
        && a.unlockPoints == b.unlockPoints && a.volume == b.volume;
}

void settle(core::runtime::AsyncRuntime& runtime, RecordSession& session)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (session.phase() == RecordPhase::Loading || session.phase() == RecordPhase::Saving) {
        assert(std::chrono::steady_clock::now() < deadline);
        assert(runtime.dispatch().success());
        std::this_thread::yield();
    }
    assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
}

struct Host {
    core::SharedPtr<MemoryFileBackend> memory;
    queen_source_fixture::Runtime runtime;

    explicit Host(std::string_view input = {}, bool writable = true)
        : memory(core::makeShared<MemoryFileBackend>(MemoryFileBackendDesc{1,
              2 * RecordSession::maxFileBytes,
              1,
              2 * RecordSession::maxFileBytes,
              512,
              writable})),
          runtime(queen_source_fixture::create(memory, 2 * RecordSession::maxFileBytes))
    {
        if (!input.empty())
            assert(memory->registerFile("record.yaml",
                       reinterpret_cast<const std::uint8_t*>(input.data()),
                       input.size())
                == FileIoStatus::Success);
    }
};

void validCycle()
{
    Host host;
    auto& runtime = *host.runtime.owner;
    RecordSession session(runtime.fileService(), host.runtime.mount);
    assert(!session.save({}));
    assert(session.load() && !session.load());
    settle(runtime, session);
    assert(session.phase() == RecordPhase::Ready && same(session.record(), {}));
    for (const Record wanted : {Record{12, 21, 1, 8, .25}, Record{21, 90, 2, 11, .75}}) {
        const auto previous = session.record();
        assert(session.save(wanted));
        assert(same(session.record(), previous) && !session.save({}));
        settle(runtime, session);
        assert(session.phase() == RecordPhase::Ready && same(session.record(), wanted));
        assert(session.saveResult().commitState == FileCommitState::Committed);
        RecordSession reopened(runtime.fileService(), host.runtime.mount);
        assert(reopened.load());
        settle(runtime, reopened);
        assert(reopened.phase() == RecordPhase::Ready && same(reopened.record(), wanted));
    }
}

void invalidInputs()
{
    const auto valid = RecordSchema::encode({12, 30, 1, 9, .5});
    for (unsigned mode = 0; mode < 7; ++mode) {
        auto input = valid;
        if (mode == 0)
            input.replace(
                input.find("arp.survivors-record"), std::strlen("arp.survivors-record"), "wrong");
        if (mode == 1)
            input.replace(input.find("contractVersion: 1"), 18, "contractVersion: 9");
        if (mode == 2)
            input[0] = static_cast<char>(0xff);
        if (mode == 3)
            input[0] = '\0';
        if (mode == 4)
            input = "invalid: [";
        if (mode == 5)
            input.append(RecordSession::maxFileBytes, ' ');
        if (mode == 6)
            input += "\n---\nother: document\n";
        Host host(input);
        auto& runtime = *host.runtime.owner;
        RecordSession session(runtime.fileService(), host.runtime.mount);
        assert(session.load());
        settle(runtime, session);
        assert(session.phase() == RecordPhase::Failed && session.diagnostic());
        assert(!session.save({}) && same(session.record(), {}));
    }
    for (double volume : {-1., 2., std::numeric_limits<double>::quiet_NaN()}) {
        Host host;
        auto& runtime = *host.runtime.owner;
        RecordSession session(runtime.fileService(), host.runtime.mount);
        assert(session.load());
        settle(runtime, session);
        Record bad;
        bad.volume = volume;
        assert(!session.save(bad) && same(session.record(), {}));
    }
}

void conflict(bool initiallyMissing)
{
    const Record original{2, 3, 0, 1, .5}, external{40, 50, 2, 8, .75};
    Host host(initiallyMissing ? core::pmr::String{} : RecordSchema::encode(original));
    auto& runtime = *host.runtime.owner;
    RecordSession first(runtime.fileService(), host.runtime.mount);
    RecordSession second(runtime.fileService(), host.runtime.mount);
    assert(first.load());
    settle(runtime, first);
    assert(second.load());
    settle(runtime, second);
    assert(second.save(external));
    settle(runtime, second);
    assert(second.phase() == RecordPhase::Ready);
    assert(first.save({}));
    settle(runtime, first);
    assert(first.phase() == RecordPhase::Failed
        && first.saveResult().status == FileIoStatus::VersionMismatch);
    assert(same(first.record(), initiallyMissing ? Record{} : original));
    RecordSession reopened(runtime.fileService(), host.runtime.mount);
    assert(reopened.load());
    settle(runtime, reopened);
    assert(same(reopened.record(), external));
}

void rejectedAndDestroyed()
{
    {
        Host host({}, false);
        auto& runtime = *host.runtime.owner;
        RecordSession session(runtime.fileService(), host.runtime.mount);
        assert(session.load());
        settle(runtime, session);
        assert(!session.save({}) && session.phase() == RecordPhase::Failed);
    }
    {
        Host host;
        auto& runtime = *host.runtime.owner;
        const auto stale = host.runtime.mount;
        assert(host.runtime.mounts->unmount(stale));
        RecordSession session(runtime.fileService(), stale);
        assert(!session.load() && session.phase() == RecordPhase::Failed);
    }
    for (bool saving : {false, true}) {
        Host host;
        auto& runtime = *host.runtime.owner;
        {
            RecordSession session(runtime.fileService(), host.runtime.mount);
            assert(session.load());
            if (saving) {
                settle(runtime, session);
                assert(session.save({10, 20, 1, 2, .3}));
            }
        }
        runtime.shutdown();
        assert(!runtime.completionQueue().pending() && !runtime.completionQueue().reserved());
        assert(!host.memory->telemetry().inFlight);
    }
}

void native(const std::filesystem::path& directory)
{
    const Record expected{123, 300, 2, 91, .3};
    for (unsigned phase = 0; phase < 2; ++phase) {
        if (phase == 1) {
            auto padded = RecordSchema::encode(expected);
            padded.append(RecordSession::maxFileBytes - padded.size(), ' ');
            std::ofstream external(directory / "record.yaml", std::ios::binary);
            external.write(padded.data(), static_cast<std::streamsize>(padded.size()));
            assert(external.good());
        }
        examples::NativeStartupFileDesc desc;
        desc.directory = directory;
        desc.mountName = "record";
        desc.maxFileBytes = RecordSession::maxFileBytes;
        desc.allowObservedBaselineReplace = true;
        desc.maxBackendSaveScratchBytes = RecordSession::maxBackendSaveScratchBytes;
        examples::NativeStartupFiles files(
            std::move(desc), {"record-config", "record-mount", "record-runtime"});
        auto& runtime = files.runtime();
        RecordSession session(runtime.fileService(), files.mount());
        assert(session.load());
        settle(runtime, session);
        assert(session.phase() == RecordPhase::Ready);
        assert(session.save(expected));
        settle(runtime, session);
        assert(session.phase() == RecordPhase::Ready);
        assert(same(session.record(), expected));
        if (phase == 1) {
            // An external corrupt writer after load must not be overwritten.
            {
                std::ofstream external(directory / "record.yaml", std::ios::binary);
                external << "invalid: [";
                assert(external.good());
            }
            assert(session.save({}));
            settle(runtime, session);
            assert(session.phase() == RecordPhase::Failed
                && session.saveResult().status == FileIoStatus::VersionMismatch);
            assert(same(session.record(), expected));
            const auto bytes =
                core::readTextFile(directory / "record.yaml", RecordSession::maxFileBytes);
            assert(bytes.success && bytes.text == "invalid: [");
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--native") == 0) {
        native(argv[2]);
        std::puts("PASS native mounted record create/save/reopen");
        return 0;
    }
    auto cycle = [] {
        validCycle();
        invalidInputs();
        conflict(false);
        conflict(true);
        rejectedAndDestroyed();
    };
    cycle();
    const auto before = core::defaultCpuAllocator().stats();
    for (unsigned i = 0; i < (argc > 1 ? 20u : 1u); ++i)
        cycle();
    const auto after = core::defaultCpuAllocator().stats();
    assert(before.liveBytes == after.liveBytes);
    assert(before.allocationCount - before.deallocationCount
        == after.allocationCount - after.deallocationCount);
    std::printf("PASS record session lifecycle core %llu -> %llu; live %llu -> %llu\n",
        (unsigned long long)before.liveBytes,
        (unsigned long long)after.liveBytes,
        (unsigned long long)(before.allocationCount - before.deallocationCount),
        (unsigned long long)(after.allocationCount - after.deallocationCount));
}
