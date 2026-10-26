#include "editor/workspace/editor_scene_view_state.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace {
using namespace arp;
using namespace arp::editor;
using State = EditorSceneViewState;

class Fixture final {
public:
    Fixture()
    {
        first = document.createObject("Same name");
        child = document.createObject("Same name");
        other = document.createObject("Same name");
        assert(document.scene().reparent(child, first).ok());
    }

    State::Intent intent(scene::GameObjectId id, State::Flags flags) const
    {
        return {epoch, document.ownerTicket(), id, flags};
    }

    void set(scene::GameObjectId id, State::Flags flags)
    {
        State::Prepared prepared;
        const char* diagnostic = nullptr;
        assert(state.prepare(document, epoch, intent(id, flags), prepared, diagnostic));
        assert(!diagnostic);
        const auto stats = core::defaultCpuAllocator().stats();
        {
            auto quota = core::makeShared<core::CpuMemoryQuota>(1);
            core::CpuMemoryQuotaScope scope(quota);
            assert(state.commit(document, epoch, prepared) == State::CommitResult::Changed);
            assert(quota->liveBytes() == 0);
        }
        assert(state.commit(document, epoch, prepared) == State::CommitResult::Consumed);
        (void)stats;
    }

    State::Observation get(scene::GameObjectId id) const
    {
        const auto result = state.observe(document, epoch, id);
        assert(result.valid && !result.diagnostic);
        return result;
    }

    EditorSceneDocument document{false};
    State state;
    scene::GameObjectId first, child, other;
    std::uint64_t epoch = 7;
};

core::pmr::String serialized(const EditorSceneDocument& document)
{
    core::pmr::String yaml, diagnostic;
    assert(EditorSceneAuthoringCodec::encode(document, yaml, diagnostic));
    return yaml;
}

void hierarchyAndIsolation()
{
    Fixture f;
    const auto before = serialized(f.document);
    const auto selection = f.document.selectedObject();
    f.set(f.first, {true, false});
    assert(f.get(f.child).effective.hidden && !f.get(f.child).local.hidden);
    assert(!f.get(f.other).effective.hidden);
    f.set(f.child, {false, true});
    assert(f.get(f.child).effective.hidden && f.get(f.child).effective.locked);
    assert(!f.state.subtreeEditable(f.document, f.epoch, f.first));
    assert(f.state.subtreeEditable(f.document, f.epoch, f.other));
    assert(!f.state.subtreeEditable(f.document, f.epoch, f.document.scene().rootId()));
    assert(f.document.scene().reparent(f.child, f.other).ok());
    assert(!f.get(f.child).effective.hidden && f.get(f.child).effective.locked);
    assert(f.state.subtreeEditable(f.document, f.epoch, f.first));
    assert(f.document.scene().reparent(f.child, f.first).ok());
    f.set(f.first, {});
    assert(!f.get(f.child).effective.hidden && f.get(f.child).effective.locked);
    f.set(f.child, {});
    assert(f.state.storedCount() == 0);
    assert(f.document.selectedObject() == selection && serialized(f.document) == before);
    f.set(f.document.scene().rootId(), {true, true});
    assert(f.get(f.other).effective == (State::Flags{true, true}));
    f.set(f.document.scene().rootId(), {});
    std::puts(
        "PASS inherited hide/edit-lock, reparent recomputation, locked descendant protection, root unlock, unchanged Scene/selection/YAML");
}

void preparationFailures()
{
    Fixture f;
    State limited(1);
    const char* issue = nullptr;
    State::Prepared first;
    assert(limited.prepare(f.document, f.epoch, f.intent(f.first, {true, false}), first, issue));
    assert(limited.commit(f.document, f.epoch, first) == State::CommitResult::Changed);
    const auto revision = limited.revision();
    State::Prepared full;
    assert(!limited.prepare(f.document, f.epoch, f.intent(f.other, {true, true}), full, issue));
    assert(std::string_view(issue) == "EDITOR-VIEW-STATE-CAPACITY");
    assert(limited.revision() == revision && limited.storedCount() == 1);
    State::Prepared noOp;
    assert(limited.prepare(f.document, f.epoch, f.intent(f.first, {true, false}), noOp, issue));
    assert(limited.commit(f.document, f.epoch, noOp) == State::CommitResult::Unchanged);
    assert(limited.revision() == revision);
    assert(f.document.scene().destroyObject(f.first).ok());
    State::Prepared pruned;
    assert(limited.prepare(f.document, f.epoch, f.intent(f.other, {false, true}), pruned, issue));
    assert(limited.commit(f.document, f.epoch, pruned) == State::CommitResult::Changed);
    assert(limited.storedCount() == 1);
    assert(!limited.observe(f.document, f.epoch, f.first).valid);
    State::Prepared failed;
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    const auto oldRevision = f.state.revision();
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(
            !f.state.prepare(f.document, f.epoch, f.intent(f.other, {true, false}), failed, issue));
    }
    assert(std::string_view(issue) == "EDITOR-VIEW-STATE-ALLOCATION-FAILED");
    assert(quota->liveBytes() == 0 && f.state.revision() == oldRevision);
    assert(f.state.prepare(f.document, f.epoch, f.intent(f.other, {true, false}), failed, issue));
    assert(f.state.commit(f.document, f.epoch, failed) == State::CommitResult::Changed);
    for (const auto capacity : {std::size_t(0), State::maxEntries + 1}) {
        State invalid(capacity);
        State::Prepared rejected;
        assert(!invalid.prepare(f.document, f.epoch, f.intent(f.other, {}), rejected, issue));
        assert(!invalid.observe(f.document, f.epoch, f.other).valid);
    }
    std::puts(
        "PASS exact capacity, no-op revision, deleted ID pruning, OOM retry and invalid budget rejection");
}

void lifetimeGates()
{
    Fixture f;
    const char* issue = nullptr;
    State::Prepared a, b;
    assert(f.state.prepare(f.document, f.epoch, f.intent(f.first, {true, false}), a, issue));
    assert(f.state.prepare(f.document, f.epoch, f.intent(f.other, {false, true}), b, issue));
    assert(f.state.commit(f.document, f.epoch, a) == State::CommitResult::Changed);
    assert(f.state.commit(f.document, f.epoch, b) == State::CommitResult::Stale);
    State::Prepared deletion;
    assert(f.state.prepare(f.document, f.epoch, f.intent(f.other, {true, false}), deletion, issue));
    assert(f.document.scene().destroyObject(f.other).ok());
    assert(f.state.commit(f.document, f.epoch, deletion) == State::CommitResult::Stale);
    const auto oldIntent = f.intent(f.child, {true, false});
    State::Prepared provider;
    assert(f.state.prepare(f.document, f.epoch, oldIntent, provider, issue));
    f.document.setResourceManagerProvider({});
    assert(f.state.commit(f.document, f.epoch, provider) == State::CommitResult::Stale);
    State::Prepared staleOwner;
    assert(!f.state.prepare(f.document, f.epoch, oldIntent, staleOwner, issue));
    assert(!f.get(f.first).effective.hidden);
    f.set(f.first, {true, true});
    EditorSceneDocument replacement(false);
    const auto sameId = replacement.createObject("Same name");
    assert(sameId == f.first);
    assert(!f.state.observe(replacement, f.epoch, sameId).effective.hidden);
    assert(f.document.replaceWith(std::move(replacement)));
    assert(!f.get(sameId).effective.locked);
    State::Prepared epoch;
    assert(f.state.prepare(f.document, f.epoch, f.intent(sameId, {true, false}), epoch, issue));
    ++f.epoch;
    assert(f.state.commit(f.document, f.epoch, epoch) == State::CommitResult::Stale);
    f.state.reset();
    assert(f.state.storedCount() == 0 && !f.get(sameId).effective.hidden);
    std::puts(
        "PASS conflicting prepare, deleted target, stale provider/Scene/epoch, repeated numeric ID isolation and reset");
}

void authoringBoundary()
{
    EditorSceneDocument document(false);
    State state;
    const auto root = document.scene().rootId();
    for (std::size_t i = 0; i < EditorSceneAuthoringCodec::maxObjects; ++i)
        assert(document.createObject("bounded").valid());
    assert(document.scene().objects().size() == EditorSceneAuthoringCodec::maxObjects);
    const State::Intent intent{1, document.ownerTicket(), root, {true, true}};
    State::Prepared prepared;
    const char* diagnostic = nullptr;
    assert(state.prepare(document, 1, intent, prepared, diagnostic));
    assert(state.commit(document, 1, prepared) == State::CommitResult::Changed);
    assert(state.observe(document, 1, root).effective.locked);
    assert(document.createObject("over budget").valid());
    State::Prepared rejected;
    assert(!state.prepare(document, 1, intent, rejected, diagnostic));
    assert(!state.observe(document, 1, root).valid);
    assert(state.storedCount() == 1);
    std::puts("PASS exact 512 authored children plus root; 513th child fails closed");
}

void snapshotProjection()
{
    Fixture f;
    const auto source = f.document.scene().extractTraversalSnapshot({State::maxEntries});
    assert(source.ok());
    const char* diagnostic = nullptr;
    f.set(f.first, {true, true});
    const auto filtered = f.state.visibleSnapshot(f.document, f.epoch, source, diagnostic);
    assert(filtered && !diagnostic && filtered->nodes().size() + 2 == source.nodes().size());
    assert(!filtered->findNode(f.first) && !filtered->findNode(f.child));
    assert(filtered->findNode(f.other) && source.findNode(f.first) && source.findNode(f.child));
    const auto* preserved = filtered->findNode(f.other);
    const auto* original = source.findNode(f.other);
    assert(preserved->parentId == original->parentId && preserved->active == original->active
        && preserved->activeInHierarchy == original->activeInHierarchy
        && preserved->componentIds == original->componentIds);
    auto quota = core::makeShared<core::CpuMemoryQuota>(1);
    {
        core::CpuMemoryQuotaScope scope(quota);
        assert(!f.state.visibleSnapshot(f.document, f.epoch, source, diagnostic));
        assert(std::string_view(diagnostic) == "EDITOR-VIEW-STATE-SNAPSHOT-ALLOCATION-FAILED");
    }
    assert(quota->liveBytes() == 0 && source.findNode(f.child));
    f.set(f.document.scene().rootId(), {true, false});
    const auto empty = f.state.visibleSnapshot(f.document, f.epoch, source, diagnostic);
    assert(empty && empty->ok() && empty->nodes().empty());
    core::pmr::Vector<scene::SceneTraversalNode> duplicated(source.nodes());
    duplicated.back() = duplicated.front();
    const auto invalid = scene::SceneTraversalSnapshot::success(std::move(duplicated));
    assert(!f.state.visibleSnapshot(f.document, f.epoch, invalid, diagnostic));
    assert(std::string_view(diagnostic) == "EDITOR-VIEW-STATE-SNAPSHOT");
    std::puts(
        "PASS inherited detached visibility snapshot, unchanged source/IDs/active, bounded OOM, empty root view and duplicate rejection");
}
} // namespace

int main(int argc, char** argv)
{
    auto baseline = core::defaultCpuAllocator().stats();
    const bool smoke = argc == 2 && std::string_view(argv[1]) == "--smoke";
    for (unsigned cycle = 0; cycle <= (smoke ? 20u : 0u); ++cycle) {
        hierarchyAndIsolation();
        preparationFailures();
        lifetimeGates();
        snapshotProjection();
        if (!cycle)
            authoringBoundary();
        const auto current = core::defaultCpuAllocator().stats();
        if (cycle == 0)
            baseline = current;
        assert(current.liveBytes == baseline.liveBytes);
        assert(current.allocationCount - current.deallocationCount
            == baseline.allocationCount - baseline.deallocationCount);
    }
    if (smoke)
        std::puts(
            "PASS 20 warmed ordinary Editor view-state ownership cycles; core live bytes/count delta=0");
}
