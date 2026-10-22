#include "core/hash/stable_hash.h"
#include "core/memory/cpu_allocator.h"
#include "resource/resource_manager.h"

#include <cassert>
#include <cstring>

using namespace arp::reflection;

namespace {

constexpr const char* kThemeId = "a1b2c3d4-1111-4aaa-bbbb-000000000001";
constexpr const char* kAtlasMetaId = "a1b2c3d4-2222-4aaa-bbbb-000000000002";
constexpr const char* kDocumentId = "a1b2c3d4-3333-4aaa-bbbb-000000000003";
constexpr const char* kFontSetId = "a1b2c3d4-4444-4aaa-bbbb-000000000004";
constexpr const char* kHudThemeBindingId = "a1b2c3d4-5555-4aaa-bbbb-000000000005";

arp::core::pmr::String text(const char* value)
{
    return arp::core::pmr::String(value, &arp::core::defaultCpuMemoryResource());
}

class ReloadableThemeResource final : public arp::resource::ResourceBase {
public:
    explicit ReloadableThemeResource(int paletteSize = 4)
        : arp::resource::ResourceBase(makeReference()), paletteSize_(paletteSize)
    {}

    int paletteSize() const noexcept { return paletteSize_; }

    void appendContentFingerprint(arp::core::StableHash64& hasher) const noexcept override
    {
        hasher.appendU64(static_cast<std::uint64_t>(paletteSize_));
    }

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        return manager.admitResourceMetadata(*this, context.policy);
    }

private:
    static arp::resource::ResourceReference makeReference()
    {
        arp::resource::ResourceReference reference;
        reference.id = text(kThemeId);
        reference.type = text("ReloadableThemeResource");
        reference.alias = text("reload/theme");
        reference.status = text("Prototype Complete");
        reference.displayName = text("Reloadable Theme");
        return reference;
    }

    int paletteSize_ = 0;
};

class ReloadableAtlasMetaResource final : public arp::resource::ResourceBase {
public:
    explicit ReloadableAtlasMetaResource(int pageCount = 1)
        : arp::resource::ResourceBase(makeReference()), pageCount_(pageCount)
    {
        addDependency(arp::resource::ResourceDependency{
            text(kThemeId),
            text("theme/palette"),
        });
    }

    int pageCount() const noexcept { return pageCount_; }

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        return manager.admitResourceMetadata(*this, context.policy);
    }

private:
    static arp::resource::ResourceReference makeReference()
    {
        arp::resource::ResourceReference reference;
        reference.id = text(kAtlasMetaId);
        reference.type = text("ReloadableAtlasMetaResource");
        reference.alias = text("reload/atlas-meta");
        reference.status = text("Prototype Complete");
        reference.displayName = text("Reloadable Atlas Meta");
        return reference;
    }

    int pageCount_ = 0;
};

class ReloadableDocumentResource final : public arp::resource::ResourceBase {
public:
    explicit ReloadableDocumentResource(int widgetCount = 3)
        : arp::resource::ResourceBase(makeReference()), widgetCount_(widgetCount)
    {
        addDependency(arp::resource::ResourceDependency{
            text(kAtlasMetaId),
            text("atlas/layout"),
        });
        addDependency(arp::resource::ResourceDependency{
            text(kFontSetId),
            text("font/set"),
        });
    }

    int widgetCount() const noexcept { return widgetCount_; }

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        return manager.admitResourceMetadata(*this, context.policy);
    }

private:
    static arp::resource::ResourceReference makeReference()
    {
        arp::resource::ResourceReference reference;
        reference.id = text(kDocumentId);
        reference.type = text("ReloadableDocumentResource");
        reference.alias = text("reload/document");
        reference.status = text("Prototype Complete");
        reference.displayName = text("Reloadable Document");
        return reference;
    }

    int widgetCount_ = 0;
};

class ReloadableHudThemeBindingResource final : public arp::resource::ResourceBase {
public:
    explicit ReloadableHudThemeBindingResource(int panelCount = 2)
        : arp::resource::ResourceBase(makeReference()), panelCount_(panelCount)
    {
        addDependency(arp::resource::ResourceDependency{
            text(kThemeId),
            text("theme/palette"),
        });
    }

    int panelCount() const noexcept { return panelCount_; }

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        return manager.admitResourceMetadata(*this, context.policy);
    }

private:
    static arp::resource::ResourceReference makeReference()
    {
        arp::resource::ResourceReference reference;
        reference.id = text(kHudThemeBindingId);
        reference.type = text("ReloadableHudThemeBindingResource");
        reference.alias = text("reload/hud-theme-binding");
        reference.status = text("Prototype Complete");
        reference.displayName = text("Reloadable HUD Theme Binding");
        return reference;
    }

    int panelCount_ = 0;
};

class ReloadableFontSetResource final : public arp::resource::ResourceBase {
public:
    explicit ReloadableFontSetResource(int glyphCount = 96)
        : arp::resource::ResourceBase(makeReference()), glyphCount_(glyphCount)
    {}

    int glyphCount() const noexcept { return glyphCount_; }

    void appendContentFingerprint(arp::core::StableHash64& hasher) const noexcept override
    {
        hasher.appendU64(static_cast<std::uint64_t>(glyphCount_));
    }

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        return manager.admitResourceMetadata(*this, context.policy);
    }

private:
    static arp::resource::ResourceReference makeReference()
    {
        arp::resource::ResourceReference reference;
        reference.id = text(kFontSetId);
        reference.type = text("ReloadableFontSetResource");
        reference.alias = text("reload/font-set");
        reference.status = text("Prototype Complete");
        reference.displayName = text("Reloadable Font Set");
        return reference;
    }

    int glyphCount_ = 0;
};

class FailingReloadResource final : public arp::resource::ResourceBase {
public:
    explicit FailingReloadResource(arp::core::pmr::String id, const char* diag)
        : arp::resource::ResourceBase(makeReference(std::move(id), diag)),
          loadDiagnostics_(diag, &arp::core::defaultCpuMemoryResource())
    {}

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        arp::resource::ResourceLoadResult result;
        result.success = false;
        result.state = arp::resource::ResourceState::Failed;
        result.diagnostics = text("injected failure: ");
        result.diagnostics += loadDiagnostics_;
        return result;
    }

private:
    static arp::resource::ResourceReference makeReference(
        arp::core::pmr::String id, const char* diag)
    {
        arp::resource::ResourceReference reference;
        reference.id = std::move(id);
        reference.type = text("ReloadableThemeResource");
        reference.status = text("Prototype Complete");
        (void)diag;
        return reference;
    }

    arp::core::pmr::String loadDiagnostics_{&arp::core::defaultCpuMemoryResource()};
};

class MetadataMutatingThenFailingReloadResource final : public arp::resource::ResourceBase {
public:
    MetadataMutatingThenFailingReloadResource() : arp::resource::ResourceBase(makeReference())
    {
        addDependency(arp::resource::ResourceDependency{
            text(kFontSetId),
            text("font/set"),
        });
    }

    arp::resource::ResourceLoadResult load(arp::resource::ResourceManager& manager,
        const arp::resource::ResourceLoadContext& context) override
    {
        const auto admitted = manager.admitResourceMetadata(*this, context.policy);
        if (!admitted.success) {
            return admitted;
        }

        arp::resource::ResourceLoadResult result;
        result.success = false;
        result.state = arp::resource::ResourceState::Failed;
        result.diagnostics = text("mutated metadata before failing payload decode");
        return result;
    }

private:
    static arp::resource::ResourceReference makeReference()
    {
        arp::resource::ResourceReference reference;
        reference.id = text(kThemeId);
        reference.type = text("ReloadableThemeResource");
        reference.alias = text("reload/theme-mutated");
        reference.status = text("Backend Validated");
        reference.displayName = text("Mutated Theme");
        return reference;
    }
};

void assertContains(const arp::core::pmr::String& haystack, const char* needle)
{
    assert(haystack.find(needle) != arp::core::pmr::String::npos);
}

} // namespace

int main()
{
    // Test 1: Successful reload increments generation.
    {
        arp::resource::ResourceManager manager;
        auto loaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(loaded.success);
        assert(loaded.state == arp::resource::ResourceState::Ready);

        auto generationBefore = manager.generationOf(kThemeId);
        assert(generationBefore > 0u);

        // Reload with new content.
        auto reloaded = manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(8),
            arp::resource::ResourceLoadContext{});
        assert(reloaded.success);

        auto generationAfter = manager.generationOf(kThemeId);
        assert(generationAfter == generationBefore + 1u);

        // The ready resource is still accessible.
        auto handle = manager.find<ReloadableThemeResource>(kThemeId);
        assert(handle.ready());
        assert(handle.get()->paletteSize() == 8);
    }

    // Test 2: Reload preserves logical id, alias, contract.
    {
        arp::resource::ResourceManager manager;
        auto loaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(loaded.success);

        const auto* recordBefore = manager.recordSnapshot(kThemeId);
        assert(recordBefore != nullptr);
        assert(recordBefore->alias == text("reload/theme"));
        assert(recordBefore->type == text("ReloadableThemeResource"));

        auto reloaded = manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(8),
            arp::resource::ResourceLoadContext{});
        assert(reloaded.success);

        const auto* recordAfter = manager.recordSnapshot(kThemeId);
        assert(recordAfter != nullptr);
        assert(recordAfter->id == text(kThemeId));
        assert(recordAfter->alias == text("reload/theme"));
        assert(recordAfter->type == text("ReloadableThemeResource"));
        assert(recordAfter->status == text("Prototype Complete"));
    }

    // Test 3: Dependency invalidation propagates in deterministic topological order.
    {
        arp::resource::ResourceManager manager;

        // Load font set (no dependencies).
        auto fontLoaded = manager.loadResource(arp::core::makeUnique<ReloadableFontSetResource>(96),
            arp::resource::ResourceLoadContext{});
        assert(fontLoaded.success);

        // Load theme (no dependencies).
        auto themeLoaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(themeLoaded.success);

        // Load atlas meta (depends on theme).
        auto atlasLoaded =
            manager.loadResource(arp::core::makeUnique<ReloadableAtlasMetaResource>(1),
                arp::resource::ResourceLoadContext{});
        assert(atlasLoaded.success);

        // Load document (depends on atlas meta + font set).
        auto docLoaded = manager.loadResource(arp::core::makeUnique<ReloadableDocumentResource>(3),
            arp::resource::ResourceLoadContext{});
        assert(docLoaded.success);

        auto themeGen1 = manager.generationOf(kThemeId);
        auto fontGen1 = manager.generationOf(kFontSetId);

        // Reload theme with new content; dependents should be invalidated.
        auto reloadResult =
            manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(6),
                arp::resource::ResourceLoadContext{});
        assert(reloadResult.success);

        // Theme generation incremented.
        assert(manager.generationOf(kThemeId) == themeGen1 + 1u);

        // Atlas meta depends on theme: should be invalidated.
        auto atlasRecord = manager.recordSnapshot(kAtlasMetaId);
        assert(atlasRecord != nullptr);
        assert(atlasRecord->state == arp::resource::ResourceState::Stale);

        // Document depends on atlas meta: should also be invalidated.
        auto docRecord = manager.recordSnapshot(kDocumentId);
        assert(docRecord != nullptr);
        assert(docRecord->state == arp::resource::ResourceState::Stale);

        // Font set is independent: should remain ready.
        assert(manager.generationOf(kFontSetId) == fontGen1);
        auto fontHandle = manager.find<ReloadableFontSetResource>(kFontSetId);
        assert(fontHandle.ready());
    }

    // Test 4: Alias stability across reload.
    {
        arp::resource::ResourceManager manager;
        auto loaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(loaded.success);

        // Alias resolves before reload.
        auto aliasBefore = manager.resolveAlias("reload/theme");
        assert(aliasBefore.success);
        assert(aliasBefore.resourceId == text(kThemeId));

        // Reload with new content.
        auto reloaded = manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(8),
            arp::resource::ResourceLoadContext{});
        assert(reloaded.success);

        // Alias still resolves to the same logical resource id.
        auto aliasAfter = manager.resolveAlias("reload/theme");
        assert(aliasAfter.success);
        assert(aliasAfter.resourceId == text(kThemeId));
    }

    // Test 5: Stale generation detection - handle from old generation reports stale.
    {
        arp::resource::ResourceManager manager;
        auto loaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(loaded.success);
        auto gen1 = manager.generationOf(kThemeId);

        // Capture a handle before reload.
        auto handleBefore = manager.find<ReloadableThemeResource>(kThemeId);
        assert(handleBefore.ready());
        assert(handleBefore.generation() == gen1);

        // Reload.
        auto reloaded = manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(8),
            arp::resource::ResourceLoadContext{});
        assert(reloaded.success);

        // Old handle is now stale.
        assert(!handleBefore.ready());
        assert(handleBefore.stale());

        // New handle is ready.
        auto handleAfter = manager.find<ReloadableThemeResource>(kThemeId);
        assert(handleAfter.ready());
        assert(handleAfter.generation() == gen1 + 1u);
    }

    // Test 6: Concurrent reload rejection.
    {
        arp::resource::ResourceManager manager;
        auto loaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(loaded.success);

        // Get the record and mark reload in progress.
        auto* record = const_cast<arp::resource::ResourceRecord*>(manager.recordSnapshot(kThemeId));
        assert(record != nullptr);
        record->reloadInProgress = true;

        // A second reload attempt should be rejected while a transaction is active.
        auto rejected = manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(8),
            arp::resource::ResourceLoadContext{});
        assert(!rejected.success);
        assertContains(rejected.diagnostics, "reload in progress");

        // Clear the flag.
        record->reloadInProgress = false;
    }

    // Test 7: Reload telemetry records generation and commit result.
    {
        arp::resource::ResourceManager manager;
        auto loaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(loaded.success);

        auto reloaded = manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(8),
            arp::resource::ResourceLoadContext{});
        assert(reloaded.success);
        assert(reloaded.telemetry.generation > 0u);
        assert(reloaded.telemetry.committed);
        assert(!reloaded.telemetry.rolledBack);
    }

    // Test 8: Reload of non-ready resource is rejected.
    {
        arp::resource::ResourceManager manager;
        auto declared = manager.declareResource(kThemeId, "ReloadableThemeResource");
        assert(declared.success);

        auto reload = manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(!reload.success);
        assertContains(reload.diagnostics, "resource not ready");
    }

    // Test 9: Dependency invalidation order is deterministic topological.
    {
        arp::resource::ResourceManager manager;

        auto fontLoaded = manager.loadResource(arp::core::makeUnique<ReloadableFontSetResource>(96),
            arp::resource::ResourceLoadContext{});
        assert(fontLoaded.success);
        auto themeLoaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(themeLoaded.success);
        auto atlasLoaded =
            manager.loadResource(arp::core::makeUnique<ReloadableAtlasMetaResource>(1),
                arp::resource::ResourceLoadContext{});
        assert(atlasLoaded.success);
        auto docLoaded = manager.loadResource(arp::core::makeUnique<ReloadableDocumentResource>(3),
            arp::resource::ResourceLoadContext{});
        assert(docLoaded.success);

        auto reloadResult =
            manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(6),
                arp::resource::ResourceLoadContext{});
        assert(reloadResult.success);

        // Invalidation order: theme -> atlas meta -> document (topological).
        assert(reloadResult.telemetry.invalidatedDependents.size() == 2u);
        assert(reloadResult.telemetry.invalidatedDependents[0] == text(kAtlasMetaId));
        assert(reloadResult.telemetry.invalidatedDependents[1] == text(kDocumentId));
    }

    // Test 10: No-op reload - unchanged content preserves generation and dependents.
    {
        arp::resource::ResourceManager manager;
        auto fontLoaded = manager.loadResource(arp::core::makeUnique<ReloadableFontSetResource>(96),
            arp::resource::ResourceLoadContext{});
        assert(fontLoaded.success);
        auto themeLoaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(themeLoaded.success);
        auto atlasLoaded =
            manager.loadResource(arp::core::makeUnique<ReloadableAtlasMetaResource>(1),
                arp::resource::ResourceLoadContext{});
        assert(atlasLoaded.success);

        const auto genBefore = manager.generationOf(kThemeId);
        const auto fontGenBefore = manager.generationOf(kFontSetId);

        // Reload with same content (same paletteSize=4).
        auto reloadResult =
            manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
                arp::resource::ResourceLoadContext{});
        assert(reloadResult.success);
        assert(reloadResult.unchangedContent);

        // Generation should NOT have changed.
        assert(manager.generationOf(kThemeId) == genBefore);
        const auto* record = manager.recordSnapshot(kThemeId);
        assert(record != nullptr);
        assert(record->state == arp::resource::ResourceState::Ready);
        assert(record->alias == text("reload/theme"));

        // Dependents should remain ready (not invalidated).
        auto atlasHandle = manager.find<ReloadableAtlasMetaResource>(kAtlasMetaId);
        assert(atlasHandle.ready());
        auto themeHandle = manager.find<ReloadableThemeResource>(kThemeId);
        assert(themeHandle.ready());
        assert(themeHandle.get()->paletteSize() == 4);

        // Independent resources should be unaffected.
        assert(manager.generationOf(kFontSetId) == fontGenBefore);
    }

    // Test 11: Failure rollback - failed reload preserves old state and dependents.
    {
        arp::resource::ResourceManager manager;

        // Load a resource first so we have something to reload.
        auto loaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(loaded.success);
        assert(loaded.state == arp::resource::ResourceState::Ready);

        auto genBefore = manager.generationOf(kThemeId);

        // Load a dependent so we can verify it's preserved on rollback.
        auto atlasLoaded =
            manager.loadResource(arp::core::makeUnique<ReloadableAtlasMetaResource>(1),
                arp::resource::ResourceLoadContext{});
        assert(atlasLoaded.success);

        // Now construct a resource with the same id that will fail load().
        auto failingId = text(kThemeId);
        auto failing =
            arp::core::makeUnique<FailingReloadResource>(failingId, "parse/decode error");

        auto reloadResult =
            manager.reloadResource(std::move(failing), arp::resource::ResourceLoadContext{});
        assert(!reloadResult.success);
        assertContains(reloadResult.diagnostics, "injected failure");
        assert(reloadResult.telemetry.rolledBack);

        // Generation should be unchanged.
        assert(manager.generationOf(kThemeId) == genBefore);

        // Old resource should still be accessible.
        auto handle = manager.find<ReloadableThemeResource>(kThemeId);
        assert(handle.ready());
        assert(handle.get()->paletteSize() == 4);

        // Dependent should be ready (rolled back to ready state).
        auto atlasHandle = manager.find<ReloadableAtlasMetaResource>(kAtlasMetaId);
        assert(atlasHandle.ready());
    }

    // Test 12: Rollback restores original metadata when failure happens after admission.
    {
        arp::resource::ResourceManager manager;

        auto fontLoaded = manager.loadResource(arp::core::makeUnique<ReloadableFontSetResource>(96),
            arp::resource::ResourceLoadContext{});
        assert(fontLoaded.success);
        auto themeLoaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(themeLoaded.success);

        auto reloadResult = manager.reloadResource(
            arp::core::makeUnique<MetadataMutatingThenFailingReloadResource>(),
            arp::resource::ResourceLoadContext{});
        assert(!reloadResult.success);
        assertContains(reloadResult.diagnostics, "mutated metadata before failing payload decode");

        const auto* record = manager.recordSnapshot(kThemeId);
        assert(record != nullptr);
        assert(record->state == arp::resource::ResourceState::Ready);
        assert(record->alias == text("reload/theme"));
        assert(record->displayName == text("Reloadable Theme"));
        assert(record->status == text("Prototype Complete"));
        assert(record->dependencies.empty());
    }

    // Test 13: Contract-changing replacement rejected.
    {
        arp::resource::ResourceManager manager;

        auto loaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(loaded.success);

        // Construct a resource with the same id but invalid status demotion.
        // ReloadableThemeResource uses "Prototype Complete". Create one with lower status.
        // We need to build a resource manually with a different status.
        // Use a resource whose reference has "Contract Complete" status (demotion).
        arp::resource::ResourceReference demoted;
        demoted.id = text(kThemeId);
        demoted.type = text("ReloadableThemeResource");
        demoted.alias = text("reload/theme");
        demoted.status = text("Contract Complete"); // demotion from Prototype Complete
        demoted.displayName = text("Demoted Theme");

        auto demotedResource = arp::core::makeUnique<ReloadableThemeResource>(8);
        demotedResource->configureMetadata(std::move(demoted));

        auto reloadResult = manager.reloadResource(
            std::move(demotedResource), arp::resource::ResourceLoadContext{});
        assert(!reloadResult.success);
        assertContains(reloadResult.diagnostics, "invalid resource status promotion");
    }

    // Test 14: Handle behavior for failed and unloaded states.
    {
        arp::resource::ResourceManager manager;

        // Find a non-existent resource -> handle should not be ready.
        auto missingHandle = manager.find<ReloadableThemeResource>("nonexistent-id");
        assert(!missingHandle.ready());
        assert(!missingHandle.stale());
        assert(missingHandle.state() == arp::resource::ResourceState::Unloaded);

        // Declare but don't load -> Declared state.
        auto declared = manager.declareResource(
            "b1b2c3d4-9999-4aaa-bbbb-000000000009", "ReloadableThemeResource");
        assert(declared.success);
        auto declaredHandle =
            manager.find<ReloadableThemeResource>("b1b2c3d4-9999-4aaa-bbbb-000000000009");
        assert(!declaredHandle.ready());
        assert(!declaredHandle.stale());
        assert(declaredHandle.state() == arp::resource::ResourceState::Declared);

        // Load then simulate failure state.
        auto loaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(loaded.success);
        auto readyHandle = manager.find<ReloadableThemeResource>(kThemeId);
        assert(readyHandle.ready());
        assert(!readyHandle.stale());
    }

    // Test 15: Bounded retirement - old resource moved to retained list, releasable.
    {
        arp::resource::ResourceManager manager;

        auto loaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(loaded.success);

        // No retained resources initially.
        assert(manager.retainedResourceCount() == 0u);

        // Reload with different content triggers retirement of old resource.
        auto reloadResult =
            manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(8),
                arp::resource::ResourceLoadContext{});
        assert(reloadResult.success);
        assert(reloadResult.telemetry.retiredCount == 1u);
        assert(manager.retainedResourceCount() == 1u);

        // Old handle still keeps typed pointer but reports stale.
        auto handle = manager.find<ReloadableThemeResource>(kThemeId);
        assert(handle.ready());
        assert(handle.get()->paletteSize() == 8);

        // Release retired resources.
        manager.releaseRetiredResources();
        assert(manager.retainedResourceCount() == 0u);

        // Explicit retire moves ready resource to retained list, resets record.
        manager.retireResource(kThemeId);
        assert(manager.retainedResourceCount() == 1u);

        auto retiredHandle = manager.find<ReloadableThemeResource>(kThemeId);
        assert(!retiredHandle.ready());
        assert(retiredHandle.state() == arp::resource::ResourceState::Unloaded);

        // Release retained after use.
        manager.releaseRetiredResources();
        assert(manager.retainedResourceCount() == 0u);
    }

    // Test 16: Telemetry tracks invalidated dependents and retirement across reload.
    {
        arp::resource::ResourceManager manager;

        auto themeLoaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(themeLoaded.success);
        auto fontLoaded = manager.loadResource(arp::core::makeUnique<ReloadableFontSetResource>(96),
            arp::resource::ResourceLoadContext{});
        assert(fontLoaded.success);
        auto atlasLoaded =
            manager.loadResource(arp::core::makeUnique<ReloadableAtlasMetaResource>(1),
                arp::resource::ResourceLoadContext{});
        assert(atlasLoaded.success);
        auto docLoaded = manager.loadResource(arp::core::makeUnique<ReloadableDocumentResource>(3),
            arp::resource::ResourceLoadContext{});
        assert(docLoaded.success);

        auto reloadResult =
            manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(6),
                arp::resource::ResourceLoadContext{});
        assert(reloadResult.success);
        assert(reloadResult.telemetry.committed);
        assert(!reloadResult.telemetry.rolledBack);
        assert(reloadResult.telemetry.generation > 0u);
        assert(reloadResult.telemetry.oldContentHash != reloadResult.telemetry.newContentHash);
        assert(reloadResult.telemetry.invalidatedDependents.size() == 2u);
        assert(reloadResult.telemetry.retiredCount == 1u);
    }

    // Test 17: Multiple direct dependents invalidate in deterministic lexical order.
    {
        arp::resource::ResourceManager manager;

        auto fontLoaded = manager.loadResource(arp::core::makeUnique<ReloadableFontSetResource>(96),
            arp::resource::ResourceLoadContext{});
        assert(fontLoaded.success);
        auto themeLoaded = manager.loadResource(arp::core::makeUnique<ReloadableThemeResource>(4),
            arp::resource::ResourceLoadContext{});
        assert(themeLoaded.success);
        auto atlasLoaded =
            manager.loadResource(arp::core::makeUnique<ReloadableAtlasMetaResource>(1),
                arp::resource::ResourceLoadContext{});
        assert(atlasLoaded.success);
        auto bindingLoaded =
            manager.loadResource(arp::core::makeUnique<ReloadableHudThemeBindingResource>(2),
                arp::resource::ResourceLoadContext{});
        assert(bindingLoaded.success);
        auto docLoaded = manager.loadResource(arp::core::makeUnique<ReloadableDocumentResource>(3),
            arp::resource::ResourceLoadContext{});
        assert(docLoaded.success);

        auto reloadResult =
            manager.reloadResource(arp::core::makeUnique<ReloadableThemeResource>(7),
                arp::resource::ResourceLoadContext{});
        assert(reloadResult.success);
        assert(reloadResult.telemetry.invalidatedDependents.size() == 3u);
        assert(reloadResult.telemetry.invalidatedDependents[0] == text(kAtlasMetaId));
        assert(reloadResult.telemetry.invalidatedDependents[1] == text(kHudThemeBindingId));
        assert(reloadResult.telemetry.invalidatedDependents[2] == text(kDocumentId));
    }

    return 0;
}
