#include "core/encoding/yaml_contract.h"
#include "core/io/file_io.h"
#include "editor/tests/fixtures/recovery_fixture.h"
#include "editor/workspace/editor_recovery_session.h"

#include <atomic>
#include <limits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace arp;
using namespace arp::editor;
using tests::RecoveryFixture;
using Submit = EditorRecoverySubmitStatus;
using Status = EditorRecoveryWriteStatus;
using Clock = std::chrono::steady_clock;

bool openSession(
    EditorRecoverySession& owner, const std::filesystem::path& path, core::pmr::String& diagnostic)
{
    const auto opened = owner.open(path, diagnostic);
    if (!opened)
        std::fprintf(stderr,
            "session open %s: %s\n",
            core::FilePath::utf8(path).c_str(),
            diagnostic.c_str());
    if (opened && owner.regionPublicationAttempts() > 1u)
        std::fprintf(stderr,
            "session exclusive publication attempts=%u\n",
            owner.regionPublicationAttempts());
    return opened;
}

bool openRegion(EditorRecoveryRegion& region,
    const std::filesystem::path& path,
    EditorRecoveryBudget budget,
    core::pmr::String& diagnostic)
{
    const auto opened = region.open(path, budget, diagnostic);
    if (!opened)
        std::fprintf(
            stderr, "region open %s: %s\n", core::FilePath::utf8(path).c_str(), diagnostic.c_str());
    return opened;
}

class SessionFixture final {
public:
    SessionFixture() : region(workspace.root / "recovery") {}

    EditorRecoverySnapshot snapshot(const EditorRecoverySession& owner) const
    {
        auto value = workspace.snapshot();
        value.sessionId = owner.sessionId();
        value.sequence = owner.nextSequence();
        return value;
    }

    static void finish(EditorRecoverySession& owner)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        while (owner.pending() && Clock::now() < deadline) {
            owner.poll();
            std::this_thread::yield();
        }
        assert(!owner.pending());
    }

    static EditorRecoverySnapshot decoded(const std::filesystem::path& directory, std::size_t slot)
    {
        EditorRecoverySnapshot value;
        core::pmr::String diagnostic;
        assert(EditorRecoveryCodec::decode(
            RecoveryFixture::read(
                directory / std::filesystem::u8path(EditorRecoveryRegion::slotName(slot))),
            value,
            diagnostic));
        return value;
    }

    RecoveryFixture workspace;
    std::filesystem::path region;
};

void isolatedOwnersAndSlots()
{
    SessionFixture fixture;
    core::pmr::String diagnostic;
    EditorRecoverySession a, b;
    assert(
        openSession(a, fixture.region, diagnostic) && openSession(b, fixture.region, diagnostic));
    assert(a.sessionId() != b.sessionId() && a.directory() != b.directory());
    assert(!openSession(a, fixture.region, diagnostic));
    auto value = fixture.snapshot(a);
    const auto time = Clock::now();
    assert(a.submit(value, time) == Submit::Accepted);
    assert(!a.discardOwned(diagnostic) && diagnostic == "EDITOR-RECOVERY-SESSION-BUSY");
    const auto sequence = a.result().submittedSequence;
    assert(a.submit(fixture.snapshot(b), time) == Submit::Busy
        && a.result().submittedSequence == sequence && a.result().diagnostic.empty());
    value.scripts[0].text = "later edit";
    ++value.workspaceEpoch;
    SessionFixture::finish(a);
    assert(a.result().status == Status::Saved && a.result().savedSequence == 1);
    const auto first = SessionFixture::decoded(a.directory(), 0);
    assert(first.scripts[0].text != value.scripts[0].text && first.workspaceEpoch == 7);
    value.sequence = a.nextSequence();
    assert(a.submit(value, time + std::chrono::seconds(9)) == Submit::NotDue);
    assert(a.submit(value, time + std::chrono::seconds(10)) == Submit::Accepted);
    SessionFixture::finish(a);
    assert(SessionFixture::decoded(a.directory(), 1).sequence == 2
        && SessionFixture::decoded(a.directory(), 0).sequence == 1);
    value.sequence = a.nextSequence();
    assert(a.submit(value, time + std::chrono::seconds(11)) == Submit::Unchanged);
    ++value.workspaceEpoch;
    value.sequence = 99;
    assert(a.submit(value, time + std::chrono::seconds(20)) == Submit::Rejected);
    value.sequence = a.nextSequence();
    assert(a.submit(value, time) == Submit::Rejected
        && a.result().diagnostic == "EDITOR-RECOVERY-CLOCK-REGRESSED");
    value.sessionId = b.sessionId();
    assert(a.submit(value, time + std::chrono::seconds(20)) == Submit::Rejected);
    value.sessionId = a.sessionId();
    assert(a.submit(value, time + std::chrono::seconds(20)) == Submit::Accepted);
    SessionFixture::finish(a);
    assert(a.result().savedSequence == 3 && SessionFixture::decoded(a.directory(), 0).sequence == 3
        && SessionFixture::decoded(a.directory(), 1).sequence == 2);
    auto other = fixture.snapshot(b);
    assert(b.submit(other, time) == Submit::Accepted);
    SessionFixture::finish(b);
    const auto otherPath = b.directory();
    const auto otherBytes = RecoveryFixture::read(otherPath / "slot-a.arp.yaml");
    const auto ownPath = a.directory();
    assert(a.discardOwned(diagnostic) && !std::filesystem::exists(ownPath));
    assert(RecoveryFixture::read(otherPath / "slot-a.arp.yaml") == otherBytes);
    assert(b.discardOwned(diagnostic));
    assert(!std::filesystem::exists(fixture.workspace.root / "authoring.arp.yaml")
        && !std::filesystem::exists(fixture.workspace.root / "rules.arp"));
    std::puts(
        "PASS independent owners, alternating immutable slots, interval/identity/busy gates, exact own-only discard");
}

void changedAndFailedWrites()
{
    SessionFixture fixture;
    core::pmr::String diagnostic;
    EditorRecoverySession owner;
    assert(openSession(owner, fixture.region, diagnostic));
    auto value = fixture.snapshot(owner);
    auto time = Clock::now();
    for (unsigned i = 0; i < 2; ++i) {
        value.sequence = owner.nextSequence();
        ++value.workspaceEpoch;
        assert(owner.submit(value, time) == Submit::Accepted);
        SessionFixture::finish(owner);
        time += std::chrono::seconds(10);
    }
    const auto a = owner.directory() / "slot-a.arp.yaml";
    const auto b = owner.directory() / "slot-b.arp.yaml";
    const auto original = RecoveryFixture::read(a);
    const auto previous = RecoveryFixture::read(b);
    auto changed = original;
    changed[0] = changed[0] == 'x' ? 'y' : 'x';
    RecoveryFixture::write(a, changed);
    value.sequence = owner.nextSequence();
    ++value.workspaceEpoch;
    assert(owner.submit(value, time) == Submit::Accepted);
    SessionFixture::finish(owner);
    assert(owner.result().status == Status::Failed
        && owner.result().diagnostic == "EDITOR-RECOVERY-SLOT-CONTENT-CHANGED"
        && owner.result().savedSequence == 2 && owner.nextSequence() == 4);
    assert(RecoveryFixture::read(a) == changed && RecoveryFixture::read(b) == previous);
    assert(!owner.discardOwned(diagnostic) && std::filesystem::exists(a)
        && std::filesystem::exists(b));
    RecoveryFixture::write(a, original);

#ifdef _WIN32
    // A real OS sharing failure must preserve the previous complete slot.
    const auto locked = CreateFileW(b.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    assert(locked != INVALID_HANDLE_VALUE);
    value.sequence = owner.nextSequence();
    assert(owner.submit(value, time + std::chrono::seconds(10)) == Submit::Accepted);
    SessionFixture::finish(owner);
    assert(owner.result().status == Status::Failed && owner.result().savedSequence == 2);
    assert(CloseHandle(locked));
    assert(RecoveryFixture::read(b) == previous);
#endif
    value.sequence = owner.nextSequence();
    assert(owner.submit(value, time + std::chrono::seconds(20)) == Submit::Accepted);
    SessionFixture::finish(owner);
    assert(
        owner.result().status == Status::Saved && owner.result().savedSequence == value.sequence);

    const auto unknown = owner.directory() / "do-not-delete.txt";
    RecoveryFixture::write(unknown, "external bytes");
    value.sequence = owner.nextSequence();
    ++value.workspaceEpoch;
    assert(owner.submit(value, time + std::chrono::seconds(30)) == Submit::Rejected);
    assert(!owner.discardOwned(diagnostic) && RecoveryFixture::read(unknown) == "external bytes");
    assert(std::filesystem::remove(unknown));
    const auto keptA = RecoveryFixture::read(a);
    const auto keptB = RecoveryFixture::read(b);
    assert(std::filesystem::remove(b));
    assert(owner.submit(value, time + std::chrono::seconds(30)) == Submit::Rejected
        && RecoveryFixture::read(a) == keptA);
    RecoveryFixture::write(b, keptB);
#ifdef _WIN32
    const auto cleanupLock = CreateFileW(b.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    assert(cleanupLock != INVALID_HANDLE_VALUE);
    assert(!owner.discardOwned(diagnostic) && diagnostic == "EDITOR-RECOVERY-SLOT-REMOVE-FAILED"
        && !std::filesystem::exists(a) && std::filesystem::exists(b));
    assert(owner.submit(value, time + std::chrono::seconds(40)) == Submit::Rejected
        && owner.result().diagnostic == "EDITOR-RECOVERY-DISCARD-IN-PROGRESS");
    assert(CloseHandle(cleanupLock));
#endif
    assert(owner.discardOwned(diagnostic));
    std::puts(
        "PASS exact-byte conflict, failed atomic replacement/retry, missing/unknown preservation and last saved sequence");
}

void regionContracts()
{
    SessionFixture fixture;
    core::pmr::String diagnostic;
    EditorRecoveryRegion region;
    assert(!openRegion(region, "relative", {}, diagnostic));
    assert(openRegion(region, fixture.region, {}, diagnostic));
    const auto metadataPath = fixture.region / "region.arp.yaml";
    const auto original = RecoveryFixture::read(metadataPath);
    auto previousVersion = core::YamlContract::parseSingleDocument(original);
    previousVersion["contractVersion"] = 1;
    const auto oldBytes = core::YamlContract::serialize(previousVersion);
    RecoveryFixture::write(metadataPath, oldBytes);
    EditorRecoveryRegion old;
    assert(!openRegion(old, fixture.region, {}, diagnostic)
        && RecoveryFixture::read(metadataPath) == oldBytes);
    RecoveryFixture::write(metadataPath, original);
    EditorRecoveryBudget different;
    different.maxSessions = 3;
    EditorRecoveryRegion wrong;
    assert(!openRegion(wrong, fixture.region, different, diagnostic)
        && RecoveryFixture::read(metadataPath) == original);
    RecoveryFixture::write(metadataPath, "type: unsupported\ncontractVersion: 2\n");
    EditorRecoveryRegion unsupported;
    assert(!openRegion(unsupported, fixture.region, {}, diagnostic));
    assert(RecoveryFixture::read(metadataPath) != original);
    RecoveryFixture::write(metadataPath, original);
    core::pmr::Vector<EditorRecoveryRegionEntry> entries;
    entries.push_back({"sentinel", {}, 0, 0});
    const auto unknown = fixture.region / "unknown-file";
    RecoveryFixture::write(unknown, "preserve");
    assert(!region.inspect(entries, diagnostic) && entries.size() == 1
        && entries[0].sessionId == "sentinel");
    assert(std::filesystem::remove(unknown));
    EditorRecoveryRegionEntry entry;
    assert(region.claim(entry, diagnostic));
    const auto corrupt = entry.directory / "slot-a.arp.yaml";
    RecoveryFixture::write(corrupt, "invalid recovery data");
    assert(
        region.inspect(entries, diagnostic) && entries.size() == 1 && entries[0].usedBytes == 21);
    EditorRecoverySnapshot decoded;
    assert(!EditorRecoveryCodec::decode(RecoveryFixture::read(corrupt), decoded, diagnostic));
    assert(RecoveryFixture::read(corrupt) == "invalid recovery data");
    const auto temporary = entry.directory / "slot-b.arp.yaml.arp-tmp-123-456";
    RecoveryFixture::write(temporary, "unfinished bytes");
    assert(region.inspect(entries, diagnostic) && entries[0].temporaryFiles == 1);
    assert(RecoveryFixture::read(temporary) == "unfinished bytes");
    const auto badTemporary = entry.directory / "slot-b.arp.yaml.arp-tmp-invalid";
    RecoveryFixture::write(badTemporary, "unknown");
    assert(!region.inspect(entries, diagnostic) && std::filesystem::exists(badTemporary));
    assert(std::filesystem::remove(badTemporary));
    const auto secondTemporary = entry.directory / "slot-a.arp.yaml.arp-tmp-123-457";
    RecoveryFixture::write(secondTemporary, "second incomplete write");
    assert(!region.inspect(entries, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-SESSION-TEMPORARY-LIMIT");
    assert(std::filesystem::remove(secondTemporary));
    const auto alias = entry.directory / "slot-b.arp.yaml";
    std::error_code linkError;
    std::filesystem::create_hard_link(corrupt, alias, linkError);
    assert(!linkError);
    assert(!region.inspect(entries, diagnostic)
        && RecoveryFixture::read(corrupt) == "invalid recovery data");
    assert(std::filesystem::remove(alias));
    auto small = EditorRecoveryBudget{};
    small.maxEnvelopeBytes = 4096;
    small.maxRegionBytes = small.maxSessions * 3u * small.maxEnvelopeBytes
        + EditorRecoveryBudget::maxRegionMetadataBytes;
    EditorRecoveryRegion bounded;
    const auto boundedPath = fixture.workspace.root / "bounded";
    assert(openRegion(bounded, boundedPath, small, diagnostic));
    EditorRecoveryRegionEntry large;
    assert(bounded.claim(large, diagnostic));
    const auto oversized = large.directory / "slot-a.arp.yaml";
    RecoveryFixture::write(oversized, core::pmr::String(4097, 'x'));
    assert(!bounded.inspect(entries, diagnostic)
        && diagnostic == "EDITOR-RECOVERY-SESSION-DISK-LIMIT"
        && std::filesystem::file_size(oversized) == 4097);
    std::puts(
        "PASS versioned immutable region budget, unknown/corrupt/temp/oversize records preserved and atomic inspection");
}

void concurrentClaims()
{
    SessionFixture fixture;
    EditorRecoveryBudget budget;
    budget.maxSessions = 2;
    EditorRecoveryRegion region;
    core::pmr::String diagnostic;
    const auto opened = openRegion(region, fixture.region, budget, diagnostic);
    if (!opened)
        std::fprintf(stderr,
            "concurrent region open %s: %s\n",
            core::FilePath::utf8(fixture.region).c_str(),
            diagnostic.c_str());
    if (opened && region.publicationAttempts() > 1u)
        std::fprintf(
            stderr, "region exclusive publication attempts=%u\n", region.publicationAttempts());
    assert(opened);
    std::array<std::thread, 4> workers;
    std::array<bool, 4> accepted{};
    std::array<EditorRecoveryRegionEntry, 4> entries;
    std::atomic<unsigned> ready{0};
    std::atomic<bool> begin{false};
    for (std::size_t i = 0; i < workers.size(); ++i)
        workers[i] = std::thread([&, i] {
            core::pmr::String message;
            ++ready;
            while (!begin.load())
                std::this_thread::yield();
            // Filesystem/UUID only: workers never consult Scene reflection.
            accepted[i] = region.claim(entries[i], message);
        });
    while (ready.load() != workers.size())
        std::this_thread::yield();
    begin.store(true);
    for (auto& worker : workers)
        worker.join();
    core::pmr::Vector<EditorRecoveryRegionEntry> inspected;
    assert(region.inspect(inspected, diagnostic) && inspected.size() <= budget.maxSessions);
    std::size_t count = 0;
    for (const auto result : accepted)
        count += result ? 1u : 0u;
    assert(inspected.size() == count);
    while (count < budget.maxSessions) {
        EditorRecoveryRegionEntry added;
        assert(region.claim(added, diagnostic));
        ++count;
    }
    EditorRecoveryRegionEntry rejected;
    assert(!region.claim(rejected, diagnostic) && diagnostic == "EDITOR-RECOVERY-SESSION-CAPACITY");
    assert(region.inspect(inspected, diagnostic) && inspected.size() == 2);
    std::puts(
        "PASS concurrent UUID claims bounded by reserved sessions, no inferred dead-owner cleanup");
}

void retentionAndClockBoundary()
{
    SessionFixture fixture;
    core::pmr::String diagnostic;
    std::filesystem::path retained;
    {
        EditorRecoverySession owner;
        assert(openSession(owner, fixture.region, diagnostic));
        retained = owner.directory();
        assert(owner.submit(fixture.snapshot(owner), Clock::time_point::max()) == Submit::Accepted);
        // Destruction must drain this already submitted replacement.
    }
    assert(SessionFixture::decoded(retained, 0).sequence == 1);
    EditorRecoverySession fresh;
    assert(openSession(fresh, fixture.region, diagnostic) && fresh.directory() != retained);
    auto value = fixture.snapshot(fresh);
    assert(fresh.submit(value, Clock::time_point::max()) == Submit::Accepted);
    SessionFixture::finish(fresh);
    ++value.workspaceEpoch;
    value.sequence = fresh.nextSequence();
    assert(fresh.submit(value, Clock::time_point::max()) == Submit::NotDue);
    assert(fresh.discardOwned(diagnostic));
    assert(SessionFixture::decoded(retained, 0).sequence == 1);
    std::puts(
        "PASS destructor drains submitted IO, default retained records, fresh owner isolation and clock overflow boundary");
}

void newSlotPathAndMetadataChanges()
{
    SessionFixture fixture;
    core::pmr::String diagnostic;
    EditorRecoverySession owner;
    assert(openSession(owner, fixture.region, diagnostic));
    const auto slot = owner.directory() / "slot-a.arp.yaml";
    RecoveryFixture::write(slot, "foreign slot");
    const auto value = fixture.snapshot(owner);
    assert(owner.submit(value, Clock::now()) == Submit::Rejected
        && RecoveryFixture::read(slot) == "foreign slot");
    assert(!owner.discardOwned(diagnostic));
    assert(std::filesystem::remove(slot));
    assert(std::filesystem::create_directory(slot));
    assert(owner.submit(value, Clock::now()) == Submit::Rejected);
    assert(std::filesystem::remove(slot));
    const auto metadata = fixture.region / "region.arp.yaml";
    const auto baseline = RecoveryFixture::read(metadata);
    RecoveryFixture::write(metadata, baseline + "# externally modified\n");
    assert(owner.submit(value, Clock::now()) == Submit::Rejected
        && owner.result().diagnostic == "EDITOR-RECOVERY-REGION-METADATA-CHANGED");
    RecoveryFixture::write(metadata, baseline);
    assert(owner.discardOwned(diagnostic));
    std::puts(
        "PASS unexpected new-slot files/directories and exact region metadata changes fail closed");
}

void smoke()
{
    SessionFixture fixture;
    auto baseline = core::defaultCpuAllocator().stats();
    for (unsigned cycle = 0; cycle <= 20; ++cycle) {
        {
            EditorRecoverySession owner;
            core::pmr::String diagnostic;
            assert(openSession(owner, fixture.region, diagnostic));
            auto value = fixture.snapshot(owner);
            auto time = Clock::now();
            for (unsigned step = 0; step < 3; ++step) {
                value.sequence = owner.nextSequence();
                ++value.workspaceEpoch;
                assert(owner.submit(value, time) == Submit::Accepted);
                SessionFixture::finish(owner);
                assert(owner.result().status == Status::Saved);
                time += std::chrono::seconds(10);
            }
            assert(SessionFixture::decoded(owner.directory(), 0).sequence == 3);
            assert(owner.discardOwned(diagnostic));
        }
        const auto current = core::defaultCpuAllocator().stats();
        if (!cycle)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes
            && current.allocationCount - current.deallocationCount
                == baseline.allocationCount - baseline.deallocationCount);
    }
    std::puts(
        "PASS recovery session ordinary 20 warmed create/alternate/read/discard/destroy cycles: core live bytes/count delta=0");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--smoke")
        smoke();
    else {
        isolatedOwnersAndSlots();
        changedAndFailedWrites();
        regionContracts();
        concurrentClaims();
        retentionAndClockBoundary();
        newSlotPathAndMetadataChanges();
    }
}
