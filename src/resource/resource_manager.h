#pragma once

#include "core/api.h"
#include "core/cpu_allocator.h"
#include "core/io/file_service.h"
#include "core/runtime/async_runtime.h"
#include "core/runtime/work_service.h"
#include "core/stable_hash.h"
#include "reflection/reflection.h"
#include "reflection/reflection_macros.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace arp::resource {

using NameView = reflection::NameView;

AENUM(Name = "ResourceState")
enum class ResourceState {
    Declared,
    Loading,
    Ready,
    Failed,
    Unloaded,
    Stale,
};

using ResourceGeneration = std::uint64_t;

enum class ResourceDependencyPolicy { RequireDeclared, AllowUnresolvedPrototype };

enum class ResourceGraphStage : std::uint8_t {
    Validation,
    FileRead,
    YamlParse,
    ImageDecode,
    TypedProcessing,
    Admission,
    Completion,
};

struct ResourceFileReadIntent {
    core::io::FilePathScope pathScope = core::io::FilePathScope::Unspecified;
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    core::pmr::String role{&core::defaultCpuMemoryResource()};
    std::size_t maxBytes = 0u;
};

struct ResourceSourceBuffer {
    core::pmr::String path{&core::defaultCpuMemoryResource()};
    core::pmr::String role{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<std::uint8_t> bytes{&core::defaultCpuMemoryResource()};
};

class ARP_API ResourceSourceSet final {
public:
    bool add(ResourceSourceBuffer source);
    const ResourceSourceBuffer* find(NameView path) const;
    void clear();
    std::size_t totalBytes() const noexcept;

private:
    core::pmr::Vector<ResourceSourceBuffer> sources_{&core::defaultCpuMemoryResource()};
    std::size_t totalBytes_ = 0u;
};

struct ResourceLoadPolicy {
    ResourceDependencyPolicy dependencyPolicy = ResourceDependencyPolicy::RequireDeclared;
};

struct ResourceBudgetConfig {
    std::size_t maxGraphNodes = 0u;
    std::size_t maxDecodedBytes = 0u;
    std::size_t maxUploadBytes = 0u;
    std::size_t maxGlyphPages = 0u;
    std::size_t maxReloadGenerations = 0u;
    std::size_t maxRetainedRecoveryBytes = 0u;
    std::size_t maxCompletionDispatchCount = 0u;
    std::size_t maxDiagnosticsLength = 0u;
};

struct ResourceDependency {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    core::pmr::String usage{&core::defaultCpuMemoryResource()};
};

struct ResourceEnvelope {
    core::pmr::String format{&core::defaultCpuMemoryResource()};
    std::uint32_t version = 0;
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String resourceType{&core::defaultCpuMemoryResource()};
    core::pmr::String alias{&core::defaultCpuMemoryResource()};
    core::pmr::String displayName{&core::defaultCpuMemoryResource()};
    core::pmr::String status{&core::defaultCpuMemoryResource()};
    std::uint32_t contentHash = 0;
    core::pmr::Vector<ResourceDependency> dependencies{&core::defaultCpuMemoryResource()};
    core::pmr::String payloadYaml{&core::defaultCpuMemoryResource()};
};

ASTRUCT(Name = "ResourceAliasResult", Construct = Default, Ownership = Value, Lifecycle = Value)

struct ResourceAliasResult {
    APROP(Runtime)
    bool success = false;
    APROP(Runtime)
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    APROP(Runtime)
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct ResourceHashResult {
    bool success = false;
    std::uint32_t hash = 0;
    core::pmr::String canonicalPayloadYaml{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

ASTRUCT(Name = "ResourceLoadResult", Construct = Default, Ownership = Value, Lifecycle = Value)

struct ResourceLoadResult {
    APROP(Runtime)
    bool success = false;
    APROP(Runtime)
    ResourceState state = ResourceState::Failed;
    APROP(Runtime)
    core::pmr::String resourceId{&core::defaultCpuMemoryResource()};
    APROP(Runtime)
    core::pmr::String resourceType{&core::defaultCpuMemoryResource()};
    APROP(Runtime)
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

struct ResourceBatchLoadResult {
    bool success = false;
    bool rolledBack = false;
    std::size_t loadedCount = 0u;
    core::pmr::String failedResourceId{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

using ResourceLoadCompleteFn = void (*)(void*, ResourceLoadResult);

struct ResourceAsyncLoadCompletion {
    ResourceLoadCompleteFn complete = nullptr;
    void* userData = nullptr;

    bool valid() const noexcept { return complete != nullptr; }
};

struct ResourceAsyncLoadRequest {
    core::io::FileReadRequest envelopeFile;
    ResourceLoadPolicy policy{};
};

AENUM(Name = "ResourceOperationState")
enum class ResourceOperationState : std::uint8_t {
    Pending,
    Ready,
    Failed,
    Destroyed,
};

struct ResourceOperationData {
    mutable std::mutex mutex;
    ResourceOperationState state = ResourceOperationState::Pending;
    ResourceLoadResult result;
    bool awaitConsumed = false;
};

class ResourceManager;
class ResourceBase;

// Engine-owned asynchronous resource request. The manager registry owns the
// object; scripts receive the canonical reflection reference and may inspect
// or explicitly destroy it without owning a task handle or proxy.
ACLASS(Name = "ResourceOperation", Ownership = Engine, Lifecycle = Explicit)

class ARP_API ResourceOperation final {
public:
    ResourceOperation(
        ResourceManager* manager = nullptr, core::runtime::AsyncRuntime* runtime = nullptr);
    ~ResourceOperation();

    const arp::reflection::object& nativeReflectionObject() const noexcept
    {
        return reflectionObject_;
    }

    AFUNC(Name = "state")
    ResourceOperationState state() const;
    AFUNC(Name = "is_complete")
    bool isComplete() const;
    AFUNC(Name = "is_success")
    bool isSuccess() const;
    AFUNC(Name = "result")
    ResourceLoadResult result() const;
    AFUNC(Name = "error")
    core::pmr::String error() const;
    AFUNC(Name = "resource")
    ResourceBase* resource() const;
    AFUNC(Name = "destroy")
    bool destroy();

    // Native-only bridges used by ResourceManager and the shared await ABI.
    bool startEnvelope(const core::pmr::String& path);
    arp::reflection::object makeAwaitableNative() const;

    std::shared_ptr<ResourceOperationData> sharedData() const { return data_; }

private:
    friend class ResourceManager;
    ResourceManager* owner_ = nullptr;
    core::runtime::AsyncRuntime* runtime_ = nullptr;
    std::shared_ptr<ResourceOperationData> data_;
    arp::reflection::object reflectionObject_;
};

class ResourceManager;

class ResourceLifecycleObserver {
public:
    virtual ~ResourceLifecycleObserver() = default;
    virtual bool beforeResourceUnload(NameView resourceId) noexcept = 0;
    virtual bool beforeRetiredResourcesRelease() noexcept = 0;
};

struct ResourceReference {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    core::pmr::String type{&core::defaultCpuMemoryResource()};
    core::pmr::String alias{&core::defaultCpuMemoryResource()};
    core::pmr::String status{&core::defaultCpuMemoryResource()};
    core::pmr::String displayName{&core::defaultCpuMemoryResource()};

    bool valid() const noexcept { return !id.empty() && !type.empty(); }
};

struct ResourceLoadContext {
    ResourceLoadPolicy policy{};
    const ResourceSourceSet* sources = nullptr;
    bool requirePreloadedSources = false;
    std::size_t maxDecodedBytes = 0u;
};

ACLASS(Name = "ResourceBase", Ownership = Engine, Lifecycle = Explicit)

class ARP_API ResourceBase {
public:
    ResourceBase() : reflectionObject_(arp::reflection::object::native_reference(this)) {}

    explicit ResourceBase(ResourceReference reference)
        : reference_(std::move(reference)),
          reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    ResourceBase(const ResourceBase& rhs)
        : reference_(rhs.reference_), dependencies_(rhs.dependencies_),
          reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    ResourceBase& operator=(const ResourceBase& rhs)
    {
        if (this != &rhs) {
            reference_ = rhs.reference_;
            dependencies_ = rhs.dependencies_;
        }
        return *this;
    }

    ResourceBase(ResourceBase&& rhs) noexcept
        : reference_(std::move(rhs.reference_)), dependencies_(std::move(rhs.dependencies_)),
          reflectionObject_(arp::reflection::object::native_reference(this))
    {}

    ResourceBase& operator=(ResourceBase&& rhs) noexcept
    {
        if (this != &rhs) {
            reference_ = std::move(rhs.reference_);
            dependencies_ = std::move(rhs.dependencies_);
        }
        return *this;
    }

    virtual ~ResourceBase();

    const ResourceReference& reference() const noexcept { return reference_; }
    AFUNC(Name = "resource_id")

    core::pmr::String resource_id() const
    {
        return core::pmr::String(reference_.id, &core::defaultCpuMemoryResource());
    }
    AFUNC(Name = "resource_type")

    core::pmr::String resource_type() const
    {
        return core::pmr::String(reference_.type, &core::defaultCpuMemoryResource());
    }
    AFUNC(Name = "resource_alias")

    core::pmr::String resource_alias() const
    {
        return core::pmr::String(reference_.alias, &core::defaultCpuMemoryResource());
    }
    AFUNC(Name = "resource_valid")

    bool resource_valid() const { return reference_.valid(); }

    const arp::reflection::object& nativeReflectionObject() const noexcept
    {
        return reflectionObject_;
    }

    const core::pmr::Vector<ResourceDependency>& dependencies() const noexcept
    {
        return dependencies_;
    }

    void addDependency(ResourceDependency dependency)
    {
        for (const auto& existing : dependencies_) {
            if (existing.id == dependency.id && existing.usage == dependency.usage) {
                return;
            }
        }
        dependencies_.push_back(std::move(dependency));
    }

    void configureMetadata(ResourceReference reference)
    {
        reference_ = std::move(reference);
        dependencies_.clear();
    }

    void configureMetadata(
        ResourceReference reference, core::pmr::Vector<ResourceDependency> dependencies)
    {
        reference_ = std::move(reference);
        dependencies_ = std::move(dependencies);
    }

    virtual ResourceLoadResult load(ResourceManager& manager, const ResourceLoadContext& context);
    virtual bool collectFileReadIntents(ResourceManager& manager,
        core::pmr::Vector<ResourceFileReadIntent>& intents,
        core::pmr::String& diagnostics) const;
    virtual ResourceGraphStage processingStage() const noexcept;
    virtual std::size_t processedByteCount() const noexcept;

    virtual void appendContentFingerprint(core::StableHash64& hasher) const noexcept;

private:
    friend class ResourceManager;

    ResourceReference reference_;
    core::pmr::Vector<ResourceDependency> dependencies_{&core::defaultCpuMemoryResource()};
    arp::reflection::object reflectionObject_;
};

struct ResourceGraphNode {
    core::UniquePtr<ResourceBase> resource;
    core::pmr::Vector<ResourceDependency> dependencies{&core::defaultCpuMemoryResource()};
};

struct ResourceGraphRequest {
    core::pmr::String rootResourceId{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ResourceGraphNode> nodes{&core::defaultCpuMemoryResource()};
    ResourceLoadContext context{};
};

struct ResourceRecord {
    core::pmr::String id{&core::defaultCpuMemoryResource()};
    core::pmr::String type{&core::defaultCpuMemoryResource()};
    core::pmr::String alias{&core::defaultCpuMemoryResource()};
    core::pmr::String displayName{&core::defaultCpuMemoryResource()};
    core::pmr::String status{&core::defaultCpuMemoryResource()};
    ResourceState state = ResourceState::Declared;
    bool metadataAdmitted = false;
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<ResourceDependency> dependencies{&core::defaultCpuMemoryResource()};
    core::UniquePtr<ResourceBase> ownedResource;
    // Transitional reflection compatibility storage for envelope admission.
    reflection::object value;
    core::pmr::Vector<reflection::object> ownedObjects = core::pmr::Vector<reflection::object>(
        core::pmr::Vector<reflection::object>::allocator_type(&core::defaultCpuMemoryResource()));
    ResourceGeneration generation = 0u;
    std::uint32_t contentHash = 0u;
    bool reloadInProgress = false;
};

struct ResourceReferenceSnapshotEntry {
    ResourceReference reference;
    ResourceState state = ResourceState::Declared;
    ResourceGeneration generation = 0u;
};

struct ResourceReferenceSnapshotResult {
    bool success = false;
    core::pmr::Vector<ResourceReferenceSnapshotEntry> entries{&core::defaultCpuMemoryResource()};
    core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
};

template <typename T> class ResourceHandle {
public:
    ResourceHandle() = default;

    bool ready() const
    {
        return record_ != nullptr && typed_ != nullptr && record_->state == ResourceState::Ready
            && generation_ == record_->generation;
    }

    bool stale() const { return record_ != nullptr && record_->generation != generation_; }

    const T* get() const { return ready() ? typed_ : nullptr; }

    core::pmr::String id() const
    {
        return record_ != nullptr ? record_->id
                                  : core::pmr::String(&core::defaultCpuMemoryResource());
    }

    ResourceState state() const
    {
        return record_ != nullptr ? record_->state : ResourceState::Unloaded;
    }

    ResourceGeneration generation() const { return generation_; }

    const core::pmr::String& diagnostics() const { return diagnostics_; }

private:
    friend class ResourceManager;

    ResourceHandle(const ResourceRecord* record,
        const T* typed,
        core::pmr::String diagnostics,
        ResourceGeneration generation)
        : record_(record), typed_(typed), diagnostics_(std::move(diagnostics)),
          generation_(generation)
    {}

    const ResourceRecord* record_ = nullptr;
    const T* typed_ = nullptr;
    core::pmr::String diagnostics_{&core::defaultCpuMemoryResource()};
    ResourceGeneration generation_ = 0u;
};

ARP_API ResourceHashResult computeResourcePayloadHash(NameView payloadYaml);
ARP_API bool isResourceUuid(NameView value);

ACLASS(Name = "ResourceManager", Export = "manager", Ownership = Engine, Lifecycle = Explicit)

class ARP_API ResourceManager final {
public:
    ResourceManager();
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) = delete;
    ResourceManager& operator=(ResourceManager&&) = delete;

    ResourceLoadResult declareResource(NameView resourceId, NameView resourceType);
    ResourceLoadResult admitResourceMetadata(
        const ResourceBase& resource, ResourceLoadPolicy policy = {});
    ResourceLoadResult loadResource(
        core::UniquePtr<ResourceBase>&& resource, ResourceLoadContext context = {});
    // Synchronously admits a prepared batch as one ResourceManager registry
    // transaction. Loaders must not perform irreversible external side effects.
    // On failure, manager records, aliases and budgets return to their old state.
    ResourceBatchLoadResult loadResourcesAtomically(
        core::pmr::Vector<core::UniquePtr<ResourceBase>> resources,
        ResourceLoadContext context = {});
    ResourceLoadResult loadGraph(ResourceGraphRequest request);
    core::runtime::WorkServiceStatus loadGraphAsync(core::runtime::WorkService& workService,
        ResourceGraphRequest request,
        ResourceAsyncLoadCompletion completion) noexcept;

    template <typename ResourceT, typename... Args>
    ResourceLoadResult load(ResourceLoadContext context, Args&&... args)
    {
        static_assert(std::is_base_of_v<ResourceBase, ResourceT>,
            "ResourceManager::load<T> requires T to derive from ResourceBase");
        try {
            core::UniquePtr<ResourceT> typed =
                core::makeUnique<ResourceT>(std::forward<Args>(args)...);
            // Safe upcast: ResourceT derives from ResourceBase (static_assert
            // above), and UniquePtr<ResourceT> converts to UniquePtr<ResourceBase>
            // because CpuAllocatedDeleter has a converting constructor that
            // preserves the original destroy function (which remembers the
            // concrete ResourceT type for correct destruction). This is the
            // same pattern used by loadAsync<T>() below.
            core::UniquePtr<ResourceBase> base{std::move(typed)};
            return loadResource(std::move(base), std::move(context));
        } catch (const std::exception& err) {
            ResourceLoadResult result;
            result.success = false;
            result.state = ResourceState::Failed;
            result.diagnostics = makeDiagnostic("resource typed load raised exception: ");
            result.diagnostics += err.what();
            return result;
        } catch (...) {
            ResourceLoadResult result;
            result.success = false;
            result.state = ResourceState::Failed;
            result.diagnostics = makeDiagnostic("resource typed load raised unknown exception");
            return result;
        }
    }

    ResourceLoadResult loadFromEnvelope(NameView envelopeYaml, ResourceLoadPolicy policy = {});
    AFUNC(Name = "load")

    ResourceLoadResult load_script(const core::pmr::String& envelopeYaml)
    {
        return loadFromEnvelope(envelopeYaml);
    }

    // The async runtime is bound once during Plugin registration. Script
    // calls then execute directly on this manager object and return an
    // engine-owned ResourceOperation.
    void bindScriptAsyncRuntime(core::runtime::AsyncRuntime* runtime) noexcept
    {
        scriptAsyncRuntime_ = runtime;
    }
    AFUNC(Name = "load_async", Async)
    arp::reflection::object load_async(const core::pmr::String& path);
    core::io::FileIoStatus loadEnvelopeFileAsync(core::io::FileService& fileService,
        const ResourceAsyncLoadRequest& request,
        ResourceAsyncLoadCompletion completion) noexcept;
    core::runtime::WorkServiceStatus loadAsyncResource(core::runtime::WorkService& workService,
        core::UniquePtr<ResourceBase> resource,
        ResourceAsyncLoadCompletion completion,
        ResourceLoadContext context = {}) noexcept;

    template <typename ResourceT, typename... Args>
    core::runtime::WorkServiceStatus loadAsync(core::runtime::WorkService& workService,
        ResourceAsyncLoadCompletion completion,
        ResourceLoadContext context,
        Args&&... args) noexcept
    {
        static_assert(std::is_base_of_v<ResourceBase, ResourceT>,
            "ResourceManager::loadAsync<T> requires T to derive from ResourceBase");
        try {
            core::UniquePtr<ResourceT> typed =
                core::makeUnique<ResourceT>(std::forward<Args>(args)...);
            core::UniquePtr<ResourceBase> base{std::move(typed)};
            return loadAsyncResource(workService, std::move(base), completion, std::move(context));
        } catch (...) {
            return core::runtime::WorkServiceStatus::InvalidWorkItem;
        }
    }

    ResourceLoadResult loadObjectFromEnvelope(
        NameView envelopeYaml, reflection::object value, ResourceLoadPolicy policy = {});
    ResourceLoadResult unload(NameView resourceId);
    AFUNC(Name = "unload")

    ResourceLoadResult unload_script(const core::pmr::String& resourceId)
    {
        return unload(resourceId);
    }

    void addLifecycleObserver(ResourceLifecycleObserver& observer);
    void removeLifecycleObserver(ResourceLifecycleObserver& observer);

    // Reload API
    struct ResourceReloadTelemetry {
        ResourceGeneration generation = 0u;
        std::uint32_t oldContentHash = 0u;
        std::uint32_t newContentHash = 0u;
        bool committed = false;
        bool rolledBack = false;
        core::pmr::Vector<core::pmr::String> invalidatedDependents{
            &core::defaultCpuMemoryResource()};
        std::uint64_t retiredCount = 0u;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
    };

    struct ResourceReloadResult {
        bool success = false;
        bool unchangedContent = false;
        ResourceState state = ResourceState::Failed;
        core::pmr::String diagnostics{&core::defaultCpuMemoryResource()};
        ResourceReloadTelemetry telemetry;
    };

    ResourceGeneration generationOf(NameView resourceId) const;
    AFUNC(Name = "generation")

    ResourceGeneration generation_script(const core::pmr::String& resourceId) const
    {
        return generationOf(resourceId);
    }

    ResourceReloadResult reloadResource(
        core::UniquePtr<ResourceBase> resource, ResourceLoadContext context = {});

    void retireResource(NameView resourceId);
    void releaseRetiredResources();
    std::size_t retainedResourceCount() const noexcept;

    ResourceState stateOf(NameView resourceId) const;
    AFUNC(Name = "state")

    ResourceState state_script(const core::pmr::String& resourceId) const
    {
        return stateOf(resourceId);
    }

    core::pmr::String diagnosticsOf(NameView resourceId) const;
    AFUNC(Name = "diagnostics")

    core::pmr::String diagnostics_script(const core::pmr::String& resourceId) const
    {
        return diagnosticsOf(resourceId);
    }

    ResourceAliasResult resolveAlias(NameView alias) const;
    AFUNC(Name = "resolve_alias")

    ResourceAliasResult resolve_alias_script(const core::pmr::String& alias) const
    {
        return resolveAlias(alias);
    }
    AFUNC(Name = "find")
    ResourceBase* find_resource(const core::pmr::String& resourceId);
    AFUNC(Name = "find_object")
    arp::reflection::object find_object(const core::pmr::String& resourceId) const;

    const arp::reflection::object& nativeReflectionObject() const noexcept
    {
        return reflectionObject_;
    }

    const ResourceRecord* recordSnapshot(NameView resourceId) const;
    ResourceReferenceSnapshotResult snapshotReferences(
        NameView resourceType, std::size_t maxEntries) const;

    // Budget and telemetry API
    struct ResourceManagerTelemetry {
        ResourceBudgetConfig budget{};
        std::uint64_t budgetExhaustionEvents = 0u;
    };

    void setBudgetConfig(ResourceBudgetConfig config);
    ResourceManagerTelemetry telemetry() const noexcept;

    template <typename T> ResourceHandle<std::remove_cv_t<T>> find(NameView resourceId) const
    {
        using Raw = std::remove_cv_t<T>;
        const ResourceRecord* record = findRecord(resourceId);
        if (record == nullptr) {
            return ResourceHandle<Raw>(
                nullptr, nullptr, makeDiagnostic("resource not found: ", resourceId), 0u);
        }
        if (record->state != ResourceState::Ready) {
            return ResourceHandle<Raw>(record, nullptr, record->diagnostics, record->generation);
        }
        if (record->ownedResource) {
            const Raw* typed = dynamic_cast<const Raw*>(record->ownedResource.get());
            if (typed != nullptr) {
                return ResourceHandle<Raw>(record,
                    typed,
                    core::pmr::String(&core::defaultCpuMemoryResource()),
                    record->generation);
            }
        }
        return ResourceHandle<Raw>(record,
            nullptr,
            makeDiagnostic("type mismatch for resource: ", resourceId),
            record->generation);
    }

    template <typename T> ResourceHandle<std::remove_cv_t<T>> findByAlias(NameView alias) const
    {
        using Raw = std::remove_cv_t<T>;
        const auto resolved = resolveAlias(alias);
        if (!resolved.success) {
            return ResourceHandle<Raw>(nullptr, nullptr, resolved.diagnostics, 0u);
        }
        return find<Raw>(resolved.resourceId);
    }

    /// Mutable access for resource-owned incremental streaming operations.
    /// The caller remains on the ResourceManager owner thread and must not
    /// replace the ResourceBase or mutate metadata. This is intentionally
    /// narrower than exposing ResourceRecord ownership: typed resources may
    /// append an already-admitted payload (for example a distance LOD) while
    /// keeping the same resource identity and generation.
    template <typename T> std::remove_cv_t<T>* findMutable(NameView resourceId)
    {
        using Raw = std::remove_cv_t<T>;
        ResourceRecord* record = findRecord(resourceId);
        if (record == nullptr || record->state != ResourceState::Ready || !record->ownedResource) {
            return nullptr;
        }
        return dynamic_cast<Raw*>(record->ownedResource.get());
    }

    template <typename T> std::remove_cv_t<T>* findMutableByAlias(NameView alias)
    {
        const auto resolved = resolveAlias(alias);
        return resolved.success ? findMutable<std::remove_cv_t<T>>(resolved.resourceId) : nullptr;
    }

private:
    friend class ResourceOperation;
    using ResourceRecordMap =
        core::pmr::UnorderedMap<core::pmr::String, core::UniquePtr<ResourceRecord>>;
    using ResourceAliasMap = core::pmr::UnorderedMap<core::pmr::String, core::pmr::String>;

    ResourceRecord* findRecord(NameView resourceId);
    const ResourceRecord* findRecord(NameView resourceId) const;
    ResourceRecord& upsertRecord(const ResourceEnvelope& envelope);
    ResourceLoadResult admitParsedEnvelope(const ResourceEnvelope& envelope,
        ResourceLoadPolicy policy,
        reflection::object value,
        core::pmr::Vector<reflection::object>&& ownedObjects);

    void collectDependentsTopological(NameView resourceId,
        core::pmr::Vector<core::pmr::String>& outDependents,
        core::pmr::Vector<core::pmr::String>& visited);

    void applyBudgetConfig(ResourceLoadContext& context) const noexcept;
    ResourceLoadResult applyDiagnosticsBudget(ResourceLoadResult result);
    bool retainedRecoveryBudgetAllows(const ResourceBase* resource) const noexcept;
    std::size_t retainedRecoveryBytes() const noexcept;

    std::uint32_t computeContentHash(const ResourceBase* resource);

    ResourceOperation* createOperation();
    bool destroyOperation(ResourceOperation* operation);

    static core::pmr::String makeString(NameView text);
    static core::pmr::String makeDiagnostic(const char* text);
    static core::pmr::String makeDiagnostic(const char* prefix, NameView value);

    ResourceRecordMap records_{&core::defaultCpuMemoryResource()};
    ResourceAliasMap aliases_{&core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<ResourceBase>> retainedResources_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<ResourceLifecycleObserver*> lifecycleObservers_{
        &core::defaultCpuMemoryResource()};
    core::pmr::Vector<core::UniquePtr<ResourceOperation>> operations_{
        &core::defaultCpuMemoryResource()};
    core::runtime::AsyncRuntime* scriptAsyncRuntime_ = nullptr;
    ResourceBudgetConfig budgetConfig_{};
    std::uint64_t budgetExhaustionEvents_ = 0u;
    arp::reflection::object reflectionObject_;
};

} // namespace arp::resource
