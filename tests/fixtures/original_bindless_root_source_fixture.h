#pragma once
#include "bindless_package_request_fixture.h"
#include "cpu_quota_factory_fixture.h"
#include "original_bindless_buffer_range_fixture.h"
#include "original_bindless_graphics_content_contract_fixture.h"
#include "original_workload_resource_inspection_fixture.h"
#include "rhi/backends/null/null_backend.h"
#include "rhi/frontend/command_buffer_readback.h"
#include "rhi/frontend/command_state.h"
#include "rhi/frontend/command_stream_program.h"
#include "rhi/frontend/command_transfer_validation.h"

namespace arp::test {
template <class T, class = void> struct OriginalBindlessFixtureCpuBudget {
    static constexpr std::uint64_t bytes = 65536u;
};

template <class T>
struct OriginalBindlessFixtureCpuBudget<T, std::void_t<decltype(T::originalRootCpuBytes)>> {
    static constexpr std::uint64_t bytes = T::originalRootCpuBytes;
};

template <class Binding> class ForgedRootPreparedBinding final : public Binding {
public:
    template <class Owners>
    explicit ForgedRootPreparedBinding(std::shared_ptr<const Owners> owners)
        : Binding(std::move(owners))
    {}

    rhi::ValidationResult validatePayload() const override { return {true, {}}; }

    std::uint32_t argumentBufferCount() const noexcept override { return 0u; }

    std::uint64_t argumentBufferBytes() const noexcept override { return 0u; }
};

struct OriginalBindlessRootFixtureObserver {
    template <class Root, class Sources>
    void operator()(const Root&, const Sources&, rhi::GpuDevice*) const
    {}
};

struct OriginalBindlessRootFixtureExecution {
    static constexpr bool enabled = false;
};

struct OriginalBindlessPreparedBindingRefusalReceipt {
    std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> quotas;
    bool observed = false;

    void verifyRetired() const
    {
        assert(observed);
        for (const auto& quota : quotas)
            assert(quota && !quota->liveBytes());
    }
};

template <class T, class = void>
struct OriginalBindlessPreparedBindingRefusalFixture : std::false_type {};

template <class T>
struct OriginalBindlessPreparedBindingRefusalFixture<T,
    std::void_t<decltype(std::declval<const T&>().observePreparedBindingRefusal(core::StringView{},
        std::declval<const std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u>&>()))>>
    : std::true_type {};

template <class T, class = void> struct OriginalBindlessTableBufferDataFixture : std::false_type {};

template <class T>
struct OriginalBindlessTableBufferDataFixture<T, std::void_t<decltype(T::tableBufferData)>>
    : std::bool_constant<T::tableBufferData> {};
enum class OrdinaryRootMutation { None, DescriptorWrite, ArenaReset };

template <class T, class = void>
struct OriginalBindlessCompletedTextureFixture : std::false_type {};

template <class T>
struct OriginalBindlessCompletedTextureFixture<T, std::void_t<decltype(T::completedTextures)>>
    : std::bool_constant<T::completedTextures> {};

template <class T, class = void> struct OriginalBindlessRegistrationFixture : std::false_type {};

template <class T>
struct OriginalBindlessRegistrationFixture<T,
    std::void_t<decltype(std::declval<const T&>().configureAdditionalRegistration(
        std::declval<rhi::OriginalResourceRegistrationBudget&>()))>> : std::true_type {};

template <class T, class = void> struct OriginalBindlessAdmissionFixture : std::false_type {};

template <class T, class = void>
struct OriginalBindlessCanonicalCompletionFixture : std::false_type {};

template <class T>
struct OriginalBindlessCanonicalCompletionFixture<T, std::void_t<decltype(T::canonicalCompletion)>>
    : std::bool_constant<T::canonicalCompletion> {};

template <class T>
struct OriginalBindlessAdmissionFixture<T,
    std::void_t<decltype(std::declval<const T&>().configureAdmission(
        std::declval<rhi::OriginalCommandAdmissionBudget&>()))>> : std::true_type {};

template <class T, class = void> struct OriginalBindlessBufferRangeDataFixture : std::false_type {};

template <class T>
struct OriginalBindlessBufferRangeDataFixture<T,
    std::void_t<decltype(std::declval<const T&>().originalBufferRange(
        std::declval<rhi::GpuDevice&>(),
        std::declval<const rhi::BindlessTableMapping&>(),
        std::declval<rhi::BufferDesc&>()))>> : std::true_type {};

template <class T, class = void> struct OriginalBindlessRequestFixture : std::false_type {};

// Optional factories return authentic original leases, including leases issued
// by the public immutable initializer. Table factories see the real unsealed
// snapshot so material bytes carry its generation before publication. Ordinary
// factories receive that same snapshot, possibly already sealed. No replacement
// buffer or second registration is created around an initialized actor.
template <class T, class = void> struct OriginalBindlessBufferFactoryFixture : std::false_type {};

template <class T>
struct OriginalBindlessBufferFactoryFixture<T, std::void_t<decltype(T::originalBufferFactories)>>
    : std::bool_constant<T::originalBufferFactories> {};

template <class T, class = void> struct OriginalBindlessOrdinaryBufferFixture : std::false_type {};

template <class T>
struct OriginalBindlessOrdinaryBufferFixture<T,
    std::void_t<decltype(std::declval<const T&>().configureOrdinaryBuffer(
        std::declval<rhi::BufferDesc&>(),
        std::uint32_t{},
        std::declval<const rhi::DescriptorBindingDesc&>()))>> : std::true_type {};

template <class T, class = void> struct OriginalBindlessDispatchFixture : std::false_type {};

template <class T, class = void> struct OriginalBindlessOrdinaryTextureFixture : std::false_type {};

template <class T>
struct OriginalBindlessOrdinaryTextureFixture<T,
    std::void_t<decltype(std::declval<const T&>().createCompletedOrdinaryTexture(
        std::declval<rhi::GpuDevice&>(),
        std::declval<rhi::TextureDesc&>(),
        std::declval<const std::shared_ptr<core::CpuMemoryQuota>&>(),
        std::uint32_t{},
        std::declval<const rhi::DescriptorBindingDesc&>()))>> : std::true_type {};

template <class T>
struct OriginalBindlessDispatchFixture<T,
    std::void_t<decltype(std::declval<const T&>().configureDispatch(
        std::declval<rhi::CommandScalarArguments::Dispatch&>()))>> : std::true_type {};

template <class T>
struct OriginalBindlessRequestFixture<T,
    std::void_t<decltype(std::declval<const T&>().configureRequest(
        std::declval<rhi::BindlessRequirements&>()))>> : std::true_type {};

template <class T, class = void> struct OriginalBindlessReadbackObserver : std::false_type {};

template <class T, class = void> struct OriginalBindlessRootInspectionFixture : std::false_type {};

template <class T>
struct OriginalBindlessRootInspectionFixture<T,
    std::void_t<decltype(std::declval<const T&>().inspectOriginalRoot(
        std::declval<const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>&>(),
        std::declval<const std::shared_ptr<const rhi::OriginalCommandResourceSources>&>(),
        static_cast<rhi::GpuDevice*>(nullptr)))>> : std::true_type {};

template <class T>
struct OriginalBindlessReadbackObserver<T,
    std::void_t<decltype(std::declval<const T&>().observeOriginalReadback(
        std::declval<const std::shared_ptr<const rhi::OriginalCommandBufferReadbackBinding>&>(),
        std::declval<const std::shared_ptr<rhi::OriginalCommandStateTransaction>&>(),
        std::declval<rhi::GpuDevice&>()))>> : std::true_type {};

template <class T, class = void>
struct OriginalBindlessLifetimeRetirementFixture : std::false_type {};

template <class T>
struct OriginalBindlessLifetimeRetirementFixture<T,
    std::void_t<decltype(std::declval<const T&>().originalDeviceLifetimeRetired())>>
    : std::true_type {};

template <class T, class = void> struct OriginalBindlessGeometryFixtureTraits : std::false_type {};

template <class T, class = void> struct OriginalBindlessPushFixtureTraits : std::false_type {};

template <class T>
struct OriginalBindlessPushFixtureTraits<T, std::void_t<decltype(T::graphicsPush)>>
    : std::bool_constant<T::graphicsPush> {};

template <class T, class = void>
struct OriginalBindlessPushUpdateFixtureTraits : std::integral_constant<std::uint32_t, 1u> {};

template <class T>
struct OriginalBindlessPushUpdateFixtureTraits<T, std::void_t<decltype(T::pushUpdates)>>
    : std::integral_constant<std::uint32_t, T::pushUpdates> {};

template <class T>
struct OriginalBindlessGeometryFixtureTraits<T, std::void_t<decltype(T::geometryBufferCount)>>
    : std::true_type {};

template <class Observe = OriginalBindlessRootFixtureObserver,
    class Execute = OriginalBindlessRootFixtureExecution>
inline void validateOriginalBindlessRootSource(
    std::shared_ptr<const rhi::BindlessShaderPackage> package,
    core::UniquePtr<rhi::GpuDevice> device = {},
    std::string_view computeEntry = "cleanMain",
    bool exerciseCpuState = true,
    Observe observe = {},
    Execute execute = {},
    bool originalGraphicsAttachment = false,
    std::string_view graphicsEntry = {},
    std::string_view vertexEntry = "vertexMain",
    bool originalDepthAttachment = false,
    OrdinaryRootMutation ordinaryMutation = OrdinaryRootMutation::None,
    rhi::TextureFormat graphicsColorFormat = rhi::TextureFormat::Rgba8Unorm,
    bool reserveWorkloadRetention = false)
{
    using namespace rhi;
    using namespace core;
    using Sources = OriginalCommandResourceSources;
    using Root = OriginalCommandBindlessRootSource;
    {
        OriginalCommandBindlessRootSourceBudget composed{
            2u, true, 1u, Root::retainedOwnerCount + Root::cpuOwnerSlots, 65536u, 2u, 8u};
        assert(!Root::validateBudget(composed));
        composed.maxOrdinaryDomains = 0u;
        assert(StringView(Root::validateBudget(composed))
            == "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-BUDGET");
        composed = {1u, true, 1u, Root::retainedOwnerCount + Root::cpuOwnerSlots, 65536u, 1u, 1u};
        assert(StringView(Root::validateBudget(composed))
            == "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-SCHEMA");
        composed.contractVersion = 3u;
        assert(StringView(Root::validateBudget(composed)) == "RHI-ORIGINAL-BINDLESS-ROOT-VERSION");
        for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto decision =
                semantics::OriginalBindlessRootSourceLowering::lowering(backend, 2u);
            assert(
                decision.sourceAvailable && !decision.executionAvailable && !decision.diagnostic);
            assert(StringView(
                       semantics::OriginalBindlessRootSourceLowering::nativeClosureDiagnostic(2u))
                == "RHI-BINDLESS-ROOT-ORDINARY-NATIVE-CLOSURE-UNAVAILABLE");
            assert(bool(semantics::OriginalBindlessRootSourceLowering::nativeClosureDiagnostic(
                       backend, 2u))
                == (backend == Backend::Null));
        }
    }
    OriginalBindlessGraphicsContentContractFixture::verify();
    constexpr std::uint64_t cpuBytes = OriginalBindlessFixtureCpuBudget<Execute>::bytes;
    const auto quota = [&] {
        return makeShared<CpuMemoryQuota>(cpuBytes);
    };
    OriginalBindlessRootNativeClosureBudget nativeBudget{1u, true, 1u, 1u, 1u, cpuBytes};
    assert(!OriginalBindlessRootNativeClosureContract::validateBudget(nativeBudget));
    auto refusedNative = nativeBudget;
    refusedNative.contractVersion = 22u;
    assert(StringView(OriginalBindlessRootNativeClosureContract::validateBudget(refusedNative))
        == "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-VERSION");
    refusedNative = nativeBudget;
    refusedNative.contractVersion = 4u;
    assert(StringView(OriginalBindlessRootNativeClosureContract::validateBudget(refusedNative))
        == "RHI-BINDLESS-ROOT-ATTACHMENT-BUDGET");
    refusedNative = nativeBudget;
    refusedNative.prototypeOptIn = false;
    assert(StringView(OriginalBindlessRootNativeClosureContract::validateBudget(refusedNative))
        == "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-OPT-IN");
    for (auto field : {&OriginalBindlessRootNativeClosureBudget::maxResourceEntries,
             &OriginalBindlessRootNativeClosureBudget::maxNativeOwners,
             &OriginalBindlessRootNativeClosureBudget::maxRetainedOwners}) {
        refusedNative = nativeBudget;
        refusedNative.*field = 0u;
        assert(StringView(OriginalBindlessRootNativeClosureContract::validateBudget(refusedNative))
            == "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-BUDGET");
    }
    refusedNative = nativeBudget;
    refusedNative.maxCpuMetadataBytes = 0u;
    assert(OriginalBindlessRootNativeClosureContract::validateBudget(refusedNative));
    auto lifetimeQuota = quota(), registryQuota = quota(), sourceQuota = quota(),
         graphQuota = quota();
    std::shared_ptr<const BufferReadbackRequestDeclaration> retiredBufferRequest;
    std::shared_ptr<const OriginalCommandBufferReadbackResultSnapshot> retiredBufferResult;
    if (!device)
        device = createNullDevice();
    assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, cpuBytes}, lifetimeQuota));
    if constexpr (OriginalBindlessCanonicalCompletionFixture<Execute>::value)
        if (originalGraphicsAttachment)
            assert(!device->prepareOriginalDeviceNativeLoss(
                {{1u, true, 1u, 1u, cpuBytes}, 1u}, quota()));
    if (originalGraphicsAttachment && !OriginalBindlessCompletedTextureFixture<Execute>::value
        && !OriginalBindlessCanonicalCompletionFixture<Execute>::value) {
        const auto* lossDiagnostic =
            device->prepareOriginalDeviceNativeResultLoss({1u, true, 1u, 2u, cpuBytes}, quota());
        if (device->backend() == Backend::Null)
            assert(lossDiagnostic
                && StringView(lossDiagnostic) == "RHI-ORIGINAL-RESULT-LOSS-NULL-VALIDATOR-ONLY");
        else
            assert(!lossDiagnostic);
    }
    if constexpr (OriginalBindlessCompletedTextureFixture<Execute>::value)
        execute.prepareOriginalDevice(*device);
    auto* extension =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    assert(extension);
    const auto& artifact = package->artifact();
    const bool graphics =
        std::any_of(package->modules().begin(), package->modules().end(), [&](const auto& module) {
            return module.entry == vertexEntry;
        });
    const auto ordinary = [&](const DescriptorBindingDesc& binding) {
        if (binding.binding == artifact.snapshotMetadata.binding
            || binding.binding == artifact.faultLatch.binding)
            return false;
        for (const auto& table : artifact.tables) {
            const auto physicalCount =
                artifact.execution == BindlessExecution::Emulated ? table.capacity : 1u;
            if (binding.binding >= table.firstBinding
                && binding.binding - table.firstBinding < physicalCount)
                return false;
        }
        return true;
    };
    std::uint32_t tableBuffers = 0u, tableImages = 0u, tableSamplers = 0u;
    for (const auto& table : artifact.tables) {
        const auto* binding = findDescriptorSetBinding(artifact.physicalLayout, table.firstBinding);
        assert(binding);
        if (binding->type == DescriptorType::Sampler)
            tableSamplers += table.capacity;
        else if (binding->type == DescriptorType::SampledTexture
            || binding->type == DescriptorType::StorageTexture)
            tableImages += table.capacity;
        else
            tableBuffers += table.capacity;
    }
    for (const auto& binding : artifact.physicalLayout.bindings)
        if (ordinary(binding))
            tableBuffers += binding.arrayCount;
    std::uint32_t ordinaryElements = 0u;
    for (const auto& domain : artifact.additionalDomains)
        for (const auto& binding : domain.layout.bindings) {
            assert(binding.arrayCount == 1u);
            if (binding.type == DescriptorType::Sampler)
                ++tableSamplers;
            else if (binding.type == DescriptorType::SampledTexture
                || binding.type == DescriptorType::StorageTexture)
                ++tableImages;
            else
                ++tableBuffers;
            ++ordinaryElements;
        }
    const bool faultCopy = artifact.faultLatch.contractVersion;
    const bool retainFaultReadback = faultCopy
        || (artifact.faultLatch.contractVersion
            && extension->capabilities().faultReadbackContractVersion == 1u);
    const std::uint32_t internalBuffers = (artifact.snapshotMetadata.contractVersion ? 1u : 0u)
        + (artifact.faultLatch.contractVersion ? 1u : 0u) + (retainFaultReadback ? 1u : 0u);
    const auto registeredImages = tableImages + (graphics ? 1u : 0u);
    OriginalResourceRegistrationBudget registration{3u,
        true,
        tableBuffers + internalBuffers,
        registeredImages,
        registeredImages,
        3u + 2u * (tableBuffers + internalBuffers + 2u * registeredImages + tableSamplers) + 2u
            + 3u,
        cpuBytes};
    if constexpr (OriginalBindlessGeometryFixtureTraits<Execute>::value) {
        registration.maxBuffers += Execute::geometryBufferCount;
        registration.maxRetainedOwners += Execute::geometryBufferCount
            * OriginalResourceRegistrationContract::perResourceOwnerCount;
    }
    registration.maxDescriptorArenas = registration.maxDescriptorSets = 1u;
    registration.maxDescriptorArenas += artifact.additionalDomains.size();
    registration.maxDescriptorSets += artifact.additionalDomains.size();
    registration.maxRetainedOwners += artifact.additionalDomains.size()
        * (OriginalResourceRegistrationContract::perResourceOwnerCount
            + OriginalResourceRegistrationContract::perDescriptorOwnerCount);
    if (originalDepthAttachment)
        assert(graphics);
    if (originalDepthAttachment && originalGraphicsAttachment && !tableImages) {
        ++registration.maxTextureViews;
        ++registration.maxDescriptorArenas;
        ++registration.maxDescriptorSets;
        registration.maxRetainedOwners +=
            2u * OriginalResourceRegistrationContract::perResourceOwnerCount
            + OriginalResourceRegistrationContract::perDescriptorOwnerCount;
        // Native cold depth consumer: one original snapshot (three internal
        // buffers), two disjoint Results buffers and independent samplers.
        registration.maxBuffers += 5u;
        registration.samplers = {1u, true, 2u};
        ++registration.maxDescriptorArenas;
        ++registration.maxDescriptorSets;
        // Five buffers, two samplers and the snapshot descriptor arena each
        // hold a resource registration; the descriptor set has its own charge.
        registration.maxRetainedOwners +=
            (5u + 2u + 1u) * OriginalResourceRegistrationContract::perResourceOwnerCount
            + OriginalResourceRegistrationContract::perDescriptorOwnerCount;
    }
    if (tableSamplers)
        registration.samplers = {1u, true, tableSamplers};
    if constexpr (OriginalBindlessCompletedTextureFixture<Execute>::value)
        execute.configureRegistration(registration);
    if constexpr (OriginalBindlessRegistrationFixture<Execute>::value)
        execute.configureAdditionalRegistration(registration);
    if constexpr (OriginalBindlessCompletedTextureFixture<Execute>::value)
        execute.prepareRegistration(*device, registration, registryQuota);
    else if (originalGraphicsAttachment && device->backend() != Backend::Null) {
        registration.contractVersion = 9u;
        const char* lossDiagnostic = nullptr;
        const auto loss = device->observeOriginalDeviceNativeLoss(lossDiagnostic);
        assert(loss && !lossDiagnostic);
        assert(!device->prepareOriginalDescriptorNativeLossRegistration(
            {OriginalBindlessCanonicalCompletionFixture<Execute>::value ? 3u
                    : registration.samplers.requested()                 ? 2u
                                                                        : 1u,
                true},
            registration,
            registryQuota,
            *loss));
    } else
        assert(!device->prepareOriginalResourceRegistration(registration, registryQuota));
    auto request =
        BindlessPackageRequestFixture::make(artifact, extension->capabilities().deviceIdentity);
    if constexpr (OriginalBindlessRequestFixture<Execute>::value)
        execute.configureRequest(request);
    constexpr bool partialRanges =
        OriginalBindlessBufferRangeDataFixture<Execute>::value && !Execute::enabled;
    if constexpr (partialRanges)
        for (auto& table : request.tables)
            table.resourceCount = 1u;
    BindlessSnapshotPool snapshots(*device, request.budget);
    pmr::String diagnostic;
    assert(!snapshots.prepareOriginalDescriptors(request, artifact, {0u, true}, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-ORIGINAL-DESCRIPTORS-VERSION");
    assert(!snapshots.prepareOriginalDescriptors(request, artifact, {1u, false}, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-ORIGINAL-DESCRIPTORS-OPT-IN");
    assert(!snapshots.prepareOriginalDescriptors(request, artifact, {1u, true, true}, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-ORIGINAL-DESCRIPTORS-LEGACY-SCHEMA");
    assert(!snapshots.prepareOriginalDescriptors(request, artifact, {2u, true, false}, diagnostic));
    assert(diagnostic == "RHI-BINDLESS-ORIGINAL-DESCRIPTORS-FAULT-OPT-IN");
    auto snapshot = shareUnique(snapshots.prepareOriginalDescriptors(
        request, artifact, {retainFaultReadback ? 2u : 1u, true, retainFaultReadback}, diagnostic));
    if (!snapshot)
        std::fprintf(stderr, "root snapshot: %s\n", diagnostic.c_str());
    assert(snapshot);
    const char* stable = nullptr;
    const auto is = [](const char* actual, const char* expected) {
        if (!actual || StringView(actual) != expected)
            std::fprintf(stderr,
                "bindless root expected %s got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && StringView(actual) == expected);
    };
    const OriginalCommandResourceLeaseBudget leaseBudget{1u, true, 2u, cpuBytes};
    pmr::Vector<Sources::Source> inputs;
    std::size_t attachmentSourceIndex = SIZE_MAX;
    GpuResourceId attachmentId;
    std::uint32_t createdTableBuffers = 0u;
    for (const auto& table : artifact.tables) {
        const auto* binding = findDescriptorSetBinding(artifact.physicalLayout, table.firstBinding);
        assert(binding);
        for (std::uint32_t i = 0u; i < snapshot->activeCount(table.tableId); ++i) {
            if (binding->type == DescriptorType::Sampler) {
                SamplerDesc samplerDesc;
                if constexpr (OriginalBindlessCompletedTextureFixture<Execute>::value)
                    execute.configureSampler(samplerDesc, i);
                auto sampler = device->createSampler(samplerDesc);
                assert(sampler
                    && snapshot->writeSampler(snapshot->handle(table.tableId, i), *sampler)
                        .success);
                auto lease = OriginalCommandSamplerLease::prepareOriginal(
                    sampler, leaseBudget, sourceQuota, stable);
                assert(lease && !stable);
                inputs.push_back(std::move(lease));
                continue;
            }
            if (binding->type == DescriptorType::SampledTexture
                || binding->type == DescriptorType::StorageTexture) {
                const bool storage = binding->type == DescriptorType::StorageTexture;
                TextureDesc desc;
                desc.label = "root-texture";
                desc.extent = {2u, 2u, 1u};
                desc.format = storage ? binding->storageTextureFormat : TextureFormat::Rgba8Unorm;
                desc.usage = storage ? TextureUsage::Storage : TextureUsage::Sampled;
                desc.initialState =
                    storage ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
                if (binding->textureViewDimension == TextureViewDimension::Texture2DArray)
                    desc.arrayLayerCount = 2u;
                std::shared_ptr<const OriginalCommandTextureLease> parent;
                core::UniquePtr<GpuTexture> texture;
                if constexpr (OriginalBindlessCompletedTextureFixture<Execute>::value) {
                    assert(!storage);
                    parent = execute.createCompletedTexture(*device, desc, sourceQuota, i);
                } else
                    texture = device->createTexture(desc);
                const auto* object = parent ? parent->originalObject() : texture.get();
                assert(object);
                TextureViewDesc viewDesc;
                // Exercise immutable native metadata borrowing beyond SSO.
                viewDesc.label = "root-original-texture-view-with-long-label";
                viewDesc.format = desc.format;
                viewDesc.usage = storage ? TextureUsage::Storage : TextureUsage::Sampled;
                viewDesc.mipLevelCount = object->desc().mipLevelCount;
                viewDesc.dimension = binding->textureViewDimension;
                viewDesc.arrayLayerCount = object->desc().arrayLayerCount;
                auto view = device->createTextureView(*object, viewDesc);
                assert(view);
                if (!parent)
                    parent = OriginalCommandTextureLease::prepareOriginal(
                        texture, leaseBudget, sourceQuota, stable);
                auto viewLease = OriginalCommandTextureViewLease::prepareOriginal(
                    view, leaseBudget, sourceQuota, stable);
                assert(parent && viewLease && !stable);
                auto source = OriginalCommandTextureViewSource::prepareOriginal(
                    viewLease, parent, {1u, true, 5u, cpuBytes}, sourceQuota, stable);
                if constexpr (OriginalBindlessCompletedTextureFixture<Execute>::value) {
                    const auto completed = object->observeOriginalContent();
                    assert(completed);
                    assert(snapshot
                            ->writeOriginalCompletedTexture(snapshot->handle(table.tableId, i),
                                *device,
                                source,
                                *completed,
                                {execute.completedTexturePublicationVersion(),
                                    true,
                                    1u,
                                    1u,
                                    OriginalCommandTextureViewSource::completeRetainedOwnerCount})
                            .success);
                } else
                    assert(snapshot
                            ->writeTexture(snapshot->handle(table.tableId, i),
                                *viewLease->originalObject(),
                                desc.initialState)
                            .success);
                assert(source && !stable);
                inputs.push_back(std::move(parent));
                inputs.push_back(std::move(source));
                continue;
            }
            BufferDesc bufferDesc{"root-table",
                table.bufferLayout.elementStrideBytes * 4u,
                binding->type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                               : ResourceUsage::StorageBuffer,
                MemoryUsage::GpuOnly,
                (static_cast<std::uint32_t>(table.operations)
                    & static_cast<std::uint32_t>(
                        BindlessOperation::Store | BindlessOperation::Atomic))
                    ? ResourceState::ShaderWrite
                    : ResourceState::ShaderRead};
            if constexpr (Execute::enabled
                || OriginalBindlessTableBufferDataFixture<Execute>::value)
                execute.configureBuffer(bufferDesc, table);
            DescriptorBufferBindingRange bufferRange;
            if constexpr (OriginalBindlessBufferRangeDataFixture<Execute>::value)
                bufferRange = execute.originalBufferRange(*device, table, bufferDesc);
            std::shared_ptr<const OriginalCommandBufferLease> lease;
            if constexpr (OriginalBindlessBufferFactoryFixture<Execute>::value) {
                lease = execute.createOriginalTableBuffer(
                    *device, bufferDesc, table, i, *snapshot, leaseBudget, sourceQuota);
            } else {
                auto buffer = device->createBuffer(bufferDesc);
                assert(buffer);
                if constexpr (Execute::enabled
                    || OriginalBindlessTableBufferDataFixture<Execute>::value)
                    execute.initializeBuffer(*buffer, table, i);
                lease = OriginalCommandBufferLease::prepareOriginal(
                    buffer, leaseBudget, sourceQuota, stable);
                assert(!buffer);
            }
            ++createdTableBuffers;
            assert(lease && !stable);
            if (bufferRange.contractVersion == 1u)
                assert(snapshot
                        ->writeBuffer(snapshot->handle(table.tableId, i), *lease->originalObject())
                        .success);
            if (bufferRange.contractVersion == 2u) {
                OriginalBindlessBufferRangeFixture::verifyFailures(*binding,
                    table,
                    *snapshot,
                    snapshot->handle(table.tableId, i),
                    *device,
                    lease,
                    bufferRange);
                const auto written =
                    snapshot->writeOriginalBufferRange(snapshot->handle(table.tableId, i),
                        *device,
                        lease,
                        bufferRange,
                        {1u, true, 1u});
                if (!written.success)
                    std::fprintf(stderr, "Root buffer range: %s\n", written.diagnostics.c_str());
                assert(written.success
                    && snapshot->bufferElementCount(snapshot->handle(table.tableId, i))
                        == bufferRange.sizeBytes / table.bufferLayout.elementStrideBytes);
            }
            inputs.push_back(std::move(lease));
        }
        if (snapshot->activeCount(table.tableId) < table.capacity) {
            const auto handle = snapshot->handle(table.tableId, 0u);
            if constexpr (partialRanges) {
                assert(!snapshot->initializeInactiveSlots(handle).success);
                assert(!snapshot
                        ->initializeInactiveSlots(handle, static_cast<const GpuDevice&>(*device))
                        .success);
            }
            assert(snapshot->initializeInactiveSlots(handle, *device).success);
            // Physical padding aliases the authentic finite view and owner;
            // the logical table still exposes exactly one resource.
            assert(!snapshot->handle(table.tableId, 1u).generation);
        }
    }
    for (const auto& binding : artifact.physicalLayout.bindings) {
        if (!ordinary(binding))
            continue;
        BufferDesc desc{"root-ordinary",
            16u,
            binding.type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                          : ResourceUsage::StorageBuffer,
            MemoryUsage::GpuOnly,
            binding.type == DescriptorType::StorageBuffer ? ResourceState::ShaderWrite
                                                          : ResourceState::ShaderRead};
        if constexpr (OriginalBindlessOrdinaryBufferFixture<Execute>::value)
            execute.configureOrdinaryBuffer(desc, artifact.set, binding);
        std::shared_ptr<const OriginalCommandBufferLease> lease;
        if constexpr (OriginalBindlessBufferFactoryFixture<Execute>::value) {
            lease = execute.createOriginalOrdinaryBuffer(
                *device, desc, artifact.set, binding, *snapshot, leaseBudget, sourceQuota);
        } else {
            auto buffer = device->createBuffer(desc);
            assert(buffer);
            if constexpr (OriginalBindlessOrdinaryBufferFixture<Execute>::value)
                execute.initializeOrdinaryBuffer(*buffer, artifact.set, binding);
            lease = OriginalCommandBufferLease::prepareOriginal(
                buffer, leaseBudget, sourceQuota, stable);
        }
        assert(lease && !stable
            && snapshot->writeOrdinaryBuffer(binding.binding, *lease->originalObject()).success);
        ++createdTableBuffers;
        inputs.push_back(std::move(lease));
    }
    const auto sealed = snapshot->seal(*device);
    if (!sealed.success)
        std::fprintf(stderr, "root seal: %s\n", sealed.diagnostics.c_str());
    assert(sealed.success);
    assert(!BindlessSnapshot::prepareOriginalBufferSources(
        snapshot, *device, leaseBudget, sourceQuota));
    for (const auto role : {BindlessSnapshot::OriginalBufferRole::Metadata,
             BindlessSnapshot::OriginalBufferRole::FaultLatch,
             BindlessSnapshot::OriginalBufferRole::FaultReadback})
        if (const auto& source = snapshot->originalBufferSource(role))
            inputs.push_back(source);
    if (graphics) {
        assert(!originalGraphicsAttachment || !exerciseCpuState);
        TextureDesc desc;
        desc.label = "root-render-target";
        desc.extent = {2u, 2u, 1u};
        desc.format = graphicsColorFormat;
        if (originalDepthAttachment)
            desc.format = TextureFormat::D32Float;
        desc.usage = TextureUsage::RenderAttachment;
        desc.initialState = originalDepthAttachment ? ResourceState::DepthAttachment
                                                    : ResourceState::ColorAttachment;
        core::UniquePtr<GpuTexture> texture;
        if (originalGraphicsAttachment) {
            desc.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
            if (originalDepthAttachment)
                desc.usage = desc.usage | TextureUsage::Sampled;
            desc.initialState = ResourceState::Undefined;
            const bool logicalAttachment =
                OriginalBindlessCanonicalCompletionFixture<Execute>::value
                || device->backend() == Backend::Null;
            const auto required = logicalAttachment
                ? 0u
                : device->queryOriginalContentTextureAllocation(desc, stable, 4u);
            assert((logicalAttachment || required) && !stable);
            std::uint64_t texelBytes = 0u;
            assert(
                !OriginalTextureContentCreationContract::inspectCellLayout(desc, 1u, texelBytes));
            core::CpuMemoryQuotaScope resource(sourceQuota);
            texture = device->createOriginalContentTexture(desc,
                {logicalAttachment ? 5u : 4u, true, 1u, 1u, texelBytes, 8u, cpuBytes, 1u, required},
                quota(),
                stable);
            if (device->backend() == Backend::Null) {
                assert(!texture && stable
                    && StringView(stable) == "RHI-CANONICAL-CONTENT-CREATION-NULL-VALIDATOR-ONLY");
                // Null validates the refusal. Its ordinary textures cannot
                // impersonate an exclusive content domain or native completion.
                std::puts("BINDLESS_ROOT_NULL_ATTACHMENT_REFUSED");
                return;
            }
        } else
            texture = device->createTexture(desc);
        assert(texture && !stable);
        TextureViewDesc viewDesc;
        viewDesc.format = desc.format;
        viewDesc.usage = TextureUsage::RenderAttachment;
        if (originalDepthAttachment)
            viewDesc.aspect = TextureAspect::Depth;
        auto view = device->createTextureView(*texture, viewDesc);
        assert(view);
        attachmentId = view->id();
        auto parent =
            OriginalCommandTextureLease::prepareOriginal(texture, leaseBudget, sourceQuota, stable);
        auto child = OriginalCommandTextureViewLease::prepareOriginal(
            view, leaseBudget, sourceQuota, stable);
        auto source = OriginalCommandTextureViewSource::prepareOriginal(
            child, parent, {1u, true, 5u, cpuBytes}, sourceQuota, stable);
        assert(source && !stable);
        inputs.push_back(std::move(parent));
        attachmentSourceIndex = inputs.size();
        inputs.push_back(std::move(source));
    }
    if constexpr (OriginalBindlessGeometryFixtureTraits<Execute>::value)
        execute.appendGeometrySources(*device, inputs, leaseBudget, sourceQuota);

    struct PendingDomain {
        std::uint32_t set;
        std::shared_ptr<DescriptorArena> arena;
        core::UniquePtr<DescriptorSet> object;
        pmr::Vector<OriginalCommandDescriptorSetElement> elements;
    };

    pmr::Vector<PendingDomain> pendingDomains;
    pmr::Vector<DescriptorSet*> mutableOrdinarySets;
    pmr::Vector<DescriptorArena*> mutableOrdinaryArenas;
    for (const auto& domain : artifact.additionalDomains) {
        CpuMemoryQuotaScope scope(sourceQuota);
        auto arena = shareUnique(device->createOriginalDescriptorArena(
            {domain.layout.name, 1u, static_cast<std::uint32_t>(domain.layout.bindings.size())}));
        assert(arena);
        auto object =
            device->allocateOriginalDescriptorSet(arena, {domain.layout.name, domain.layout});
        assert(object);
        mutableOrdinarySets.push_back(object.get());
        mutableOrdinaryArenas.push_back(arena.get());
        PendingDomain pending{domain.set, arena, std::move(object), {}};
        for (const auto& binding : domain.layout.bindings) {
            OriginalCommandDescriptorSetElement element;
            element.write.binding = binding.binding;
            element.write.type = binding.type;
            element.stages = bindlessPipelineStagesForShader(binding.stages);
            if (binding.type == DescriptorType::Sampler) {
                SamplerDesc desc;
                desc.compareOp =
                    binding.comparisonSampler ? CompareOp::LessEqual : CompareOp::Never;
                if (!binding.sampledTextureFilterable) {
                    desc.minFilter = desc.magFilter = SamplerFilter::Nearest;
                    desc.mipFilter = SamplerMipFilter::Nearest;
                }
                auto sampler = device->createSampler(desc);
                assert(sampler);
                auto lease = OriginalCommandSamplerLease::prepareOriginal(
                    sampler, leaseBudget, sourceQuota, stable);
                assert(lease && !stable);
                element.sourceIndex = inputs.size();
                element.write.resource = lease->resource();
                element.registrationGeneration =
                    lease->originalObject()->originalResourceRegistration().generation();
                element.write.resourceKind = DescriptorResourceKind::Sampler;
                element.write.resourceState = ResourceState::Undefined;
                element.access = ResourceAccess::None;
                assert(pending.object->write(element.write).success);
                pending.elements.push_back(element);
                inputs.push_back(std::move(lease));
                continue;
            }
            if (binding.type == DescriptorType::SampledTexture
                || binding.type == DescriptorType::StorageTexture) {
                const bool storage = binding.type == DescriptorType::StorageTexture;
                TextureDesc desc;
                desc.label = "root-additional-image";
                desc.extent = {2u, 2u, 1u};
                desc.format = binding.depthTexture ? TextureFormat::D32Float
                    : storage                      ? binding.storageTextureFormat
                                                   : TextureFormat::Rgba8Unorm;
                desc.usage = storage ? TextureUsage::Storage : TextureUsage::Sampled;
                desc.initialState =
                    storage ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
                if (binding.textureViewDimension == TextureViewDimension::TextureCube) {
                    desc.cubeCompatible = true;
                    desc.arrayLayerCount = 6u;
                } else if (binding.textureViewDimension == TextureViewDimension::Texture2DArray)
                    desc.arrayLayerCount = 2u;
                else if (binding.textureViewDimension == TextureViewDimension::Texture3D)
                    desc.extent.depth = 2u;
                core::UniquePtr<GpuTexture> texture;
                std::shared_ptr<const OriginalCommandTextureLease> parent;
                if constexpr (OriginalBindlessOrdinaryTextureFixture<Execute>::value)
                    parent = execute.createCompletedOrdinaryTexture(
                        *device, desc, sourceQuota, domain.set, binding);
                else
                    texture = device->createTexture(desc);
                const auto* object = parent ? parent->originalObject() : texture.get();
                assert(object);
                TextureViewDesc viewDesc;
                viewDesc.format = desc.format;
                viewDesc.usage = storage ? TextureUsage::Storage : TextureUsage::Sampled;
                viewDesc.dimension = binding.textureViewDimension;
                viewDesc.mipLevelCount = desc.mipLevelCount;
                viewDesc.arrayLayerCount = desc.arrayLayerCount;
                viewDesc.aspect =
                    binding.depthTexture ? TextureAspect::Depth : TextureAspect::Color;
                auto view = device->createTextureView(*object, viewDesc);
                assert(view);
                if (!parent)
                    parent = OriginalCommandTextureLease::prepareOriginal(
                        texture, leaseBudget, sourceQuota, stable);
                auto child = OriginalCommandTextureViewLease::prepareOriginal(
                    view, leaseBudget, sourceQuota, stable);
                auto source = OriginalCommandTextureViewSource::prepareOriginal(
                    child, parent, {1u, true, 5u, cpuBytes}, sourceQuota, stable);
                assert(source && !stable);
                inputs.push_back(std::move(parent));
                element.sourceIndex = inputs.size();
                element.write.resource = source->resource();
                element.write.resourceKind = DescriptorResourceKind::TextureView;
                element.registrationGeneration =
                    source->originalObject()->originalResourceRegistration().generation();
                element.write.resourceState = ResourceState::ShaderRead;
                if (storage)
                    element.write.resourceState = desc.initialState;
                element.access =
                    !storage || binding.storageTextureAccess == StorageTextureAccess::ReadOnly
                    ? ResourceAccess::ShaderRead
                    : binding.storageTextureAccess == StorageTextureAccess::WriteOnly
                    ? ResourceAccess::ShaderWrite
                    : ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
                if constexpr (OriginalBindlessOrdinaryTextureFixture<Execute>::value) {
                    assert(!storage);
                    const auto completed = object->observeOriginalContent();
                    assert(completed);
                    const auto result = pending.object->writeOriginalCompletedTexture(*device,
                        element.write,
                        source,
                        *completed,
                        {execute.completedTexturePublicationVersion(),
                            true,
                            1u,
                            1u,
                            OriginalCommandTextureViewSource::completeRetainedOwnerCount});
                    if (!result.success)
                        std::fprintf(
                            stderr, "ordinary completed image: %s\n", result.diagnostics.c_str());
                    assert(result.success);
                } else
                    assert(pending.object->write(element.write).success);
                pending.elements.push_back(element);
                inputs.push_back(std::move(source));
                continue;
            }
            const auto writable = binding.type == DescriptorType::StorageBuffer;
            BufferDesc desc{"root-additional-buffer",
                16u,
                binding.type == DescriptorType::UniformBuffer ? ResourceUsage::UniformBuffer
                                                              : ResourceUsage::StorageBuffer,
                MemoryUsage::GpuOnly,
                writable ? ResourceState::ShaderWrite : ResourceState::ShaderRead};
            if constexpr (OriginalBindlessOrdinaryBufferFixture<Execute>::value)
                execute.configureOrdinaryBuffer(desc, domain.set, binding);
            std::shared_ptr<const OriginalCommandBufferLease> lease;
            if constexpr (OriginalBindlessBufferFactoryFixture<Execute>::value) {
                lease = execute.createOriginalOrdinaryBuffer(
                    *device, desc, domain.set, binding, *snapshot, leaseBudget, sourceQuota);
            } else {
                auto buffer = device->createBuffer(desc);
                assert(buffer);
                if constexpr (OriginalBindlessOrdinaryBufferFixture<Execute>::value)
                    execute.initializeOrdinaryBuffer(*buffer, domain.set, binding);
                lease = OriginalCommandBufferLease::prepareOriginal(
                    buffer, leaseBudget, sourceQuota, stable);
            }
            ++createdTableBuffers;
            assert(lease && !stable);
            element.sourceIndex = inputs.size();
            element.registrationGeneration =
                lease->originalObject()->originalResourceRegistration().generation();
            element.write.resourceKind = DescriptorResourceKind::Buffer;
            element.write.resource = lease->resource();
            element.write.resourceState =
                writable ? ResourceState::ShaderWrite : ResourceState::ShaderRead;
            element.bufferAccessBytes = lease->layout().sizeBytes;
            element.access = writable ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
                                      : ResourceAccess::ShaderRead;
            assert(pending.object->write(element.write).success);
            pending.elements.push_back(element);
            inputs.push_back(std::move(lease));
        }
        pendingDomains.push_back(std::move(pending));
    }
    const auto baseSourceCount = inputs.size();
    OriginalCommandAdmissionBudget admission;
    admission.contractVersion = 1u;
    admission.prototypeOptIn = true;
    admission.maxCommands = 18u;
    admission.maxReferences = 11u;
    admission.maxGraphOwners = registration.maxRetainedOwners + 128u;
    if (faultCopy)
        admission.maxInputDescriptors = graphics ? 7u : 6u;
    for (const auto& input : inputs)
        ++admission.maxSourceOwners[static_cast<std::size_t>(Sources::domain(input))];
    admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::BindlessRoot)] = 1u;
    admission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Descriptor)] =
        pendingDomains.size();
    admission.maxGraphOwners += pendingDomains.size()
        * OriginalCommandDescriptorSetSource::canonicalExclusiveRetainedOwnerCount;
    // Ordinary buffers contribute their own typed leases/wrappers and at most
    // the declared capacity-lease/budget slots; inventory still deduplicates
    // shared capacity owners. Descriptor custody alone does not retain these.
    for (const auto& domain : pendingDomains)
        admission.maxGraphOwners += domain.elements.size()
            * (OriginalCommandBufferLease::retainedOwnerCount
                + OriginalCommandBufferLease::capacityOwnerSlots);
    if constexpr (OriginalBindlessCompletedTextureFixture<Execute>::value) {
        // Content9 retains one control per registered image (including the
        // attachment) plus the original stream, beyond the state-owner union.
        if (graphics)
            admission.maxGraphOwners += registeredImages + 1u;
    }
    admission
        .maxCpuBytes[static_cast<std::size_t>(OriginalCommandAdmissionBudget::CpuDomain::Source)] =
        cpuBytes;
    admission
        .maxCpuBytes[static_cast<std::size_t>(OriginalCommandAdmissionBudget::CpuDomain::Graph)] =
        cpuBytes;
    admission.maxStateObservations = tableBuffers + internalBuffers + registeredImages;
    if constexpr (OriginalBindlessGeometryFixtureTraits<Execute>::value) {
        admission.maxStateObservations += Execute::geometryBufferCount;
        admission.maxCommands = admission.maxReferences = 32u;
        admission.maxInputDescriptors = 16u;
    }
    admission.maxTextureRanges = registeredImages;
    if (originalGraphicsAttachment) {
        // The original Undefined -> attachment barrier and pass each have
        // their own captured range, sharing the same content/state identity.
        ++admission.maxStateObservations;
        ++admission.maxTextureRanges;
    }
    admission.maxStateTransactions = 1u;
    if (graphics) {
        admission.maxPassColorAttachments = admission.maxPassAttachmentReferences = 1u;
        if (!faultCopy)
            admission.maxInputDescriptors = 1u;
    }
    admission.maxCpuBytes[static_cast<std::size_t>(
        OriginalCommandAdmissionBudget::CpuDomain::Recording)] = cpuBytes;
    if (faultCopy) {
        // Authored cold lifetime ceilings: original submission has one queue,
        // three error-scope and one loss participant; one map adds four.
        admission.maxPendingSubmissions = 1u;
        admission.maxCallbackTickets = admission.maxCallbackRegistrations =
            Execute::enabled ? 0u : 9u;
        admission.maxUnpublishedResultBytes = sizeof(std::uint32_t);
        for (const auto domain : {OriginalCommandAdmissionBudget::CpuDomain::Submission,
                 OriginalCommandAdmissionBudget::CpuDomain::Callback,
                 OriginalCommandAdmissionBudget::CpuDomain::Result})
            admission.maxCpuBytes[static_cast<std::size_t>(domain)] = cpuBytes;
        if constexpr (Execute::enabled)
            admission.maxCpuBytes[static_cast<std::size_t>(
                OriginalCommandAdmissionBudget::CpuDomain::Callback)] = 0u;
        if constexpr (Execute::enabled)
            admission.maxNativePayloadObjects = 1u;
    }
    if constexpr (OriginalBindlessAdmissionFixture<Execute>::value)
        execute.configureAdmission(admission);
    // One genuine Graphics pass: renderpass, framebuffer and its one view.
    if (originalGraphicsAttachment)
        admission.maxNativePayloadObjects += 3u;
    auto inventory = Sources::prepareColdOriginalTyped(inputs.data(),
        inputs.size(),
        admission,
        {6u, true, static_cast<std::uint32_t>(pendingDomains.size() + 1u)},
        sourceQuota,
        stable);
    if (!inventory)
        std::fprintf(stderr, "root inventory: %s\n", stable ? stable : "missing");
    assert(inventory && !stable);
    pmr::Vector<Root::OrdinaryDomain> ordinaryDomains;
    for (auto& pending : pendingDomains) {
        const auto count = static_cast<std::uint32_t>(pending.elements.size());
        auto source = OriginalCommandDescriptorSetSource::prepareOriginal(pending.object,
            pending.arena,
            inventory,
            pending.elements.data(),
            count,
            {2u,
                true,
                count,
                count,
                OriginalCommandDescriptorSetSource::canonicalExclusiveRetainedOwnerCount,
                cpuBytes},
            sourceQuota,
            *device,
            stable);
        if (!source)
            std::fprintf(stderr, "root ordinary source: %s\n", stable ? stable : "missing");
        assert(source && !stable && !pending.object);
        ordinaryDomains.push_back({pending.set, inputs.size(), source});
        assert(!Sources::appendOriginalTyped(inventory, source, *device));
        inputs.push_back(std::move(source));
    }
    pendingDomains.clear();
    pmr::Vector<PendingDomain>().swap(pendingDomains);
    pmr::Vector<OriginalCommandDescriptorSetElement> elements;
    for (const auto& binding : artifact.physicalLayout.bindings) {
        for (std::uint32_t i = 0u; i < binding.arrayCount; ++i) {
            const auto observation = snapshot->descriptorSet()->observeSuccessfulBindingWrite(
                binding.binding, i, stable);
            assert(observation && !stable);
            OriginalCommandDescriptorSetElement element;
            element.write = observation->write();
            bool found = false;
            for (std::size_t index = 0u; index < inputs.size(); ++index) {
                if (Sources::resource(inputs[index]) != element.write.resource)
                    continue;
                found = std::visit(
                    [&](const auto& lease) {
                        using T = std::remove_const_t<
                            typename std::decay_t<decltype(lease)>::element_type>;
                        if constexpr (T::domain == CommandReferenceDomain::Buffer
                            || T::domain == CommandReferenceDomain::TextureView
                            || T::domain == CommandReferenceDomain::Sampler) {
                            element.sourceIndex = index;
                            element.registrationGeneration = lease->originalObject()
                                                                 ->originalResourceRegistration()
                                                                 .generation();
                            if constexpr (T::domain == CommandReferenceDomain::Buffer) {
                                DescriptorBufferBindingRangeContract::Resolved view;
                                assert(!DescriptorBufferBindingRangeContract::resolve(
                                    element.write.bufferBindingRange,
                                    lease->layout().sizeBytes,
                                    0u,
                                    view));
                                element.bufferAccessOffset = view.offsetBytes;
                                element.bufferAccessBytes = view.sizeBytes;
                            }
                            return true;
                        } else
                            return false;
                    },
                    inputs[index]);
                if (found)
                    break;
            }
            assert(found);
            element.stages = bindlessPipelineStagesForShader(binding.stages);
            element.access = binding.type == DescriptorType::StorageBuffer
                ? ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite
                : ResourceAccess::ShaderRead;
            if (binding.type == DescriptorType::Sampler)
                element.access = ResourceAccess::None;
            if (binding.type == DescriptorType::StorageTexture) {
                element.access = binding.storageTextureAccess == StorageTextureAccess::WriteOnly
                    ? ResourceAccess::ShaderWrite
                    : binding.storageTextureAccess == StorageTextureAccess::ReadOnly
                    ? ResourceAccess::ShaderRead
                    : ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
            }
            if (binding.type == DescriptorType::StorageBuffer) {
                for (const auto& table : artifact.tables) {
                    const auto physicalCount =
                        artifact.execution == BindlessExecution::Emulated ? table.capacity : 1u;
                    if (binding.binding < table.firstBinding
                        || binding.binding - table.firstBinding >= physicalCount)
                        continue;
                    const auto operations = static_cast<std::uint32_t>(table.operations);
                    element.access = ResourceAccess::None;
                    if (operations & ~static_cast<std::uint32_t>(BindlessOperation::Store))
                        element.access = element.access | ResourceAccess::ShaderRead;
                    if (operations
                        & static_cast<std::uint32_t>(
                            BindlessOperation::Store | BindlessOperation::Atomic))
                        element.access = element.access | ResourceAccess::ShaderWrite;
                }
            }
            elements.push_back(element);
        }
    }
    auto authorization = extension->authorize(package, *snapshot, diagnostic);
    if (!authorization)
        std::fprintf(stderr, "Root authorization: %s\n", diagnostic.c_str());
    assert(authorization);
    RenderPipelineDesc state;
    state.colorFormat = graphicsColorFormat;
    state.dynamicViewportScissor = true;
    if (originalDepthAttachment) {
        state.colorFormat = TextureFormat::Unknown;
        state.depthFormat = TextureFormat::D32Float;
        state.depthStencil.depthTestEnabled = true;
        state.depthStencil.depthWriteEnabled = true;
        state.depthStencil.depthCompare = CompareOp::LessEqual;
    }
    if constexpr (OriginalBindlessGeometryFixtureTraits<Execute>::value)
        execute.configureGeometryPipeline(state);
    std::shared_ptr<RenderPipeline> pipeline;
    std::shared_ptr<ComputePipeline> compute;
    const auto retained = snapshot->plan().snapshotBytes + package->sourceBytes();
    BindlessGraphicsBindingPool bindings(
        *extension, {1u, retained + 4096u, retained + 4096u, 8u, 4096u});
    BindlessComputeBindingPool computeBindings(
        *extension, {1u, retained + 4096u, retained + 4096u, 8u, 4096u});
    Root::Prepared prepared;
    if (graphics) {
        const auto fragment = graphicsEntry.empty()
            ? (tableImages ? "fragmentMain" : "cleanFragment")
            : graphicsEntry;
        if constexpr (OriginalBindlessPushFixtureTraits<Execute>::value) {
            std::uint32_t pushBytes = 0u;
            for (const auto& module : package->modules())
                if (module.entry == vertexEntry)
                    pushBytes = module.push.sizeBytes;
            assert(pushBytes >= 4u);
            assert(!extension->createRenderPipeline(
                       authorization, state, vertexEntry, fragment, diagnostic)
                && diagnostic == "RHI-BINDLESS-PIPELINE-PUSH-EXECUTION-UNAVAILABLE");
            assert(!extension->createRenderPipeline(
                authorization, state, vertexEntry, fragment, {1u, false, pushBytes}, diagnostic));
            assert(!extension->createRenderPipeline(authorization,
                state,
                vertexEntry,
                fragment,
                {1u, true, pushBytes - 4u},
                diagnostic));
            pipeline = shareUnique(extension->createRenderPipeline(
                authorization, state, vertexEntry, fragment, {1u, true, pushBytes}, diagnostic));
        } else {
            pipeline = shareUnique(extension->createRenderPipeline(
                authorization, state, vertexEntry, fragment, diagnostic));
        }
        if (!pipeline)
            std::fprintf(stderr, "root graphics pipeline: %s\n", diagnostic.c_str());
        assert(pipeline);
        if constexpr (OriginalBindlessGeometryFixtureTraits<Execute>::value) {
            state.vertexBuffers.clear();
            assert(pipeline->bindlessVertexBuffers(1u)
                && pipeline->bindlessVertexBuffers(1u)->size() == 1u
                && !pipeline->bindlessVertexBuffers(0u) && !pipeline->bindlessVertexBuffers(2u));
            assert(pipeline->bindlessVertexBuffers(1u)->get_allocator().resource()
                    == &core::defaultCpuMemoryResource()
                && pipeline->bindlessVertexBuffers(1u)
                        ->front()
                        .attributes.get_allocator()
                        .resource()
                    == &core::defaultCpuMemoryResource());
        }
        if (originalGraphicsAttachment) {
            // Caller metadata is not a compatibility proof for the native
            // pipeline; cold pass preparation must use factory-frozen facts.
            state.colorFormat = TextureFormat::Rgba16Float;
            state.multisample.sampleCount = 4u;
        }
        prepared = bindings.prepare(
            {true, snapshot, pipeline, snapshot->plan(), snapshot->generation()}, diagnostic);
    } else {
        compute =
            shareUnique(extension->createComputePipeline(authorization, computeEntry, diagnostic));
        if (!compute)
            std::fprintf(stderr, "root compute pipeline: %s\n", diagnostic.c_str());
        assert(compute);
        prepared = computeBindings.prepare(
            {true, snapshot, compute, snapshot->plan(), snapshot->generation()}, diagnostic);
    }
    if (!std::visit(
            [](const auto& binding) {
                return bool(binding);
            },
            prepared)) {
        if constexpr (OriginalBindlessPreparedBindingRefusalFixture<Execute>::value) {
            execute.observePreparedBindingRefusal(
                diagnostic, std::array{lifetimeQuota, registryQuota, sourceQuota, graphQuota});
            return;
        }
        std::fprintf(stderr, "root prepared binding: %s\n", diagnostic.c_str());
    }
    assert(std::visit(
        [](const auto& binding) {
            return bool(binding);
        },
        prepared));
    const OriginalCommandBindlessRootSourceBudget budget{ordinaryDomains.empty() ? 1u : 2u,
        true,
        static_cast<std::uint32_t>(elements.size()),
        Root::retainedOwnerCount + Root::cpuOwnerSlots,
        cpuBytes,
        static_cast<std::uint32_t>(ordinaryDomains.size()),
        ordinaryElements};
    const auto prepare = [&](const auto& owner, const auto& values, const auto& limits) {
        return Root::prepareOriginal(owner,
            prepared,
            inventory,
            values.data(),
            values.size(),
            ordinaryDomains.empty() ? nullptr : ordinaryDomains.data(),
            ordinaryDomains.size(),
            limits,
            sourceQuota,
            *device,
            stable);
    };
    if (!ordinaryDomains.empty()) {
        const auto reject = [&](const auto& domains, const auto& limits, const char* expected) {
            const auto before = sourceQuota->liveBytes();
            assert(!Root::prepareOriginal(snapshot,
                prepared,
                inventory,
                elements.data(),
                elements.size(),
                domains.data(),
                domains.size(),
                limits,
                sourceQuota,
                *device,
                stable));
            is(stable, expected);
            assert(sourceQuota->liveBytes() == before);
        };
        auto limits = budget;
        limits.contractVersion = 1u;
        limits.maxOrdinaryDomains = limits.maxOrdinaryElements = 0u;
        assert(!Root::prepareOriginal(snapshot,
            prepared,
            inventory,
            elements.data(),
            elements.size(),
            limits,
            sourceQuota,
            *device,
            stable));
        is(stable, "RHI-ORIGINAL-BINDLESS-ROOT-DOMAINS-REQUIRED");
        auto changed = ordinaryDomains;
        changed.pop_back();
        reject(changed, budget, "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-COVERAGE");
        changed = ordinaryDomains;
        changed[0].set = artifact.set;
        reject(changed, budget, "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-SET");
        if (ordinaryDomains.size() > 1u) {
            changed = ordinaryDomains;
            changed.back().set = changed.front().set;
            reject(changed, budget, "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-DUPLICATE");
        }
        changed = ordinaryDomains;
        changed[0].sourceIndex = SIZE_MAX;
        reject(changed, budget, "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-POSITION");
        changed = ordinaryDomains;
        changed[0].sourceIndex = 0u;
        reject(changed, budget, "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-INVENTORY");
        if (ordinaryDomains.size() > 1u) {
            changed = ordinaryDomains;
            std::swap(changed[0].source, changed[1].source);
            std::swap(changed[0].sourceIndex, changed[1].sourceIndex);
            reject(changed, budget, "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-LAYOUT");
        }
        changed = ordinaryDomains;
        changed[0].source = std::shared_ptr<const OriginalCommandDescriptorSetSource>(
            sourceQuota, changed[0].source.get());
        reject(changed, budget, "RHI-ORIGINAL-DESCRIPTOR-SOURCE-OWNER");
        limits = budget;
        --limits.maxOrdinaryDomains;
        reject(ordinaryDomains,
            limits,
            limits.maxOrdinaryDomains ? "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-DOMAIN-CAPACITY"
                                      : "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-BUDGET");
        limits = budget;
        --limits.maxOrdinaryElements;
        reject(ordinaryDomains,
            limits,
            limits.maxOrdinaryElements ? "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-ELEMENT-CAPACITY"
                                       : "RHI-ORIGINAL-BINDLESS-ROOT-ORDINARY-BUDGET");
    }
    auto disabled = budget;
    disabled.prototypeOptIn = false;
    assert(!prepare(snapshot, elements, disabled));
    is(stable, "RHI-ORIGINAL-BINDLESS-ROOT-OPT-IN");
    disabled = budget;
    --disabled.maxExclusiveRetainedOwners;
    assert(!prepare(snapshot, elements, disabled));
    is(stable, "RHI-ORIGINAL-BINDLESS-ROOT-OWNER-CAPACITY");
    std::visit(
        [&](const auto& original) {
            using Binding =
                std::remove_const_t<typename std::decay_t<decltype(original)>::element_type>;
            auto forgedBinding = makeShared<ForgedRootPreparedBinding<Binding>>(original->owners());
            assert(!forgedBinding->originalFactoryOwnerMatches(forgedBinding));
            assert(!Root::prepareOriginal(snapshot,
                forgedBinding,
                inventory,
                elements.data(),
                elements.size(),
                budget,
                sourceQuota,
                *device,
                stable));
            is(stable, "RHI-ORIGINAL-BINDLESS-ROOT-PREPARED");
        },
        prepared);
    auto forged = std::shared_ptr<const BindlessSnapshot>(sourceQuota, snapshot.get());
    assert(!prepare(forged, elements, budget));
    is(stable, "RHI-ORIGINAL-BINDLESS-ROOT-SNAPSHOT");
    forged.reset();
    auto missing = elements;
    missing.pop_back();
    assert(!prepare(snapshot, missing, budget));
    is(stable, "RHI-ORIGINAL-BINDLESS-ROOT-COMPLETE-CLOSURE");
    auto wrong = elements;
    wrong[0].access = ResourceAccess::None;
    assert(!prepare(snapshot, wrong, budget));
    wrong = elements;
    ++wrong[0].registrationGeneration;
    assert(!prepare(snapshot, wrong, budget));
    const auto baseline = sourceQuota->liveBytes();
    {
        CpuMemoryQuotaLease pressure(sourceQuota, cpuBytes - baseline);
        assert(pressure.charged() && !prepare(snapshot, elements, budget));
        is(stable, "RHI-ORIGINAL-BINDLESS-ROOT-ALLOCATION");
    }
    assert(sourceQuota->liveBytes() == baseline);
    auto root = prepare(snapshot, elements, budget);
    if (!root)
        std::fprintf(stderr, "root preparation: %s\n", stable ? stable : "missing");
    assert(root && !stable && !root->nativeExecutionProven());
    assert(root->valid()
        && root->size()
            == elements.size() + ordinaryElements - tableSamplers
                + (retainFaultReadback ? 1u : 0u));
    assert(root->ordinaryDomains().size() == ordinaryDomains.size());
    std::uint64_t rootTextureCells = 0u;
    for (std::size_t i = 0u; i < root->size(); ++i) {
        BindlessResourceAccess access;
        std::size_t declarationIndex = SIZE_MAX;
        assert(root->readOriginalDeclaration(i, access, declarationIndex)
            && access.resource.isValid() && access.access != ResourceAccess::None);
        if (access.kind == BindlessAccessKind::Texture) {
            rootTextureCells += std::uint64_t{access.textureRange.mipLevelCount}
                * access.textureRange.arrayLayerCount;
            assert(declarationIndex < inventory->sources().size());
            const auto* view = std::get_if<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                &inventory->sources()[declarationIndex]);
            assert(view);
            assert((*view)->originalParentLease()->resource() == access.resource);
            const auto& layout = (*view)->originalViewLease()->layout();
            assert((TextureSubresourceRange{layout.baseMipLevel,
                       layout.mipLevelCount,
                       layout.baseArrayLayer,
                       layout.arrayLayerCount,
                       layout.aspect})
                == access.textureRange);
            bool originalParent = false;
            for (const auto& source : inventory->sources())
                if (const auto* parent =
                        std::get_if<std::shared_ptr<const OriginalCommandTextureLease>>(&source))
                    originalParent = originalParent
                        || core::SharedOwnerProjection::sameOwner(
                            (*view)->originalParentLease(), *parent);
            assert(originalParent);
        }
        bool canonical = false;
        for (const auto& source : inputs)
            if (Sources::resource(source) == access.resource)
                canonical = true;
        assert(canonical);
    }
    if (tableImages)
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ROOT_TEXTURE_VIEW_PROVENANCE 1 exact-declaration-index same-parent-lease canonical-inventory cold-correspondence-only");
    BindlessResourceAccess invalid;
    invalid.resource = snapshot->metadataBuffer()->id();
    assert(!root->read(root->size(), invalid) && !invalid.resource.isValid());
    assert(!root->read(SIZE_MAX, invalid) && !invalid.resource.isValid());
    std::visit(
        [&](const auto& binding) {
            assert(binding->validateForRecord(*binding->owners()->desc().pipeline, *root).success);
        },
        prepared);
    auto foreign = Sources::prepareColdOriginalTyped(inputs.data(),
        baseSourceCount,
        admission,
        {6u, true, static_cast<std::uint32_t>(ordinaryDomains.size() + 1u)},
        sourceQuota,
        stable);
    assert(foreign);
    is(Sources::appendOriginalTyped(foreign, root, *device),
        "RHI-ORIGINAL-SOURCES-ASSEMBLY-FOREIGN-INVENTORY");
    auto oldAdmission = admission;
    oldAdmission.maxSourceOwners[static_cast<std::size_t>(CommandReferenceDomain::Descriptor)] = 1u;
    auto legacy = Sources::prepareColdOriginalTyped(
        inputs.data(), baseSourceCount, oldAdmission, {5u, true, 1u}, sourceQuota, stable);
    assert(legacy);
    is(Sources::appendOriginalTyped(legacy, root, *device),
        "RHI-ORIGINAL-SOURCES-ASSEMBLY-BINDLESS-VERSION");
    legacy.reset();
    for (const auto& domain : ordinaryDomains)
        assert(!inventory->isBindlessOrdinaryDescriptor(domain.sourceIndex));
    assert(!Sources::appendOriginalTyped(inventory, root, *device));
    for (const auto& domain : ordinaryDomains)
        assert(inventory->isBindlessOrdinaryDescriptor(domain.sourceIndex));
    assert(!inventory->isBindlessOrdinaryDescriptor(0u)
        && !inventory->isBindlessOrdinaryDescriptor(inventory->sources().size() - 1u));
    is(inventory->typedStateJoinDiagnostic(), "RHI-ORIGINAL-BINDLESS-ROOT-STATE-JOIN-MISSING");
    OriginalWorkloadResourceInspectionFixture::inspectUnsealed(inventory, *device);
    std::shared_ptr<BindlessWorkloadCapacityPool> workloadCapacity;
    if ((device->backend() != Backend::Null || originalGraphicsAttachment
            || reserveWorkloadRetention)
        && snapshot->faultReadbackBuffer())
        workloadCapacity =
            OriginalWorkloadResourceInspectionFixture::prepareRetention(inventory, *device);
    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array<std::shared_ptr<CpuMemoryQuota>, 1u>{sourceQuota}, [&]() noexcept {
            if (const auto* id = Sources::validateRegisteredLifetime(inventory, *device))
                return id;
            for (std::size_t i = 0u; i < root->size(); ++i) {
                BindlessResourceAccess access;
                if (!root->read(i, access))
                    return "ROOT-READ-FAILED";
            }
            return static_cast<const char*>(nullptr);
        }));
    std::uint32_t owners = 0u;
    const auto* ownerDiagnostic = inventory->cpuOwnerCount(1u, admission.maxGraphOwners, owners);
    if (ownerDiagnostic)
        std::fprintf(stderr, "Root inventory CPU owners: %s\n", ownerDiagnostic);
    assert(!ownerDiagnostic && owners > inputs.size());
    const std::array<CommandUsage, 2u> commands{
        {{CommandUsageKind::RetainIndexedSnapshot, 0u, 1u}, {CommandUsageKind::Finish, 1u, 0u}}};
    const std::array<OriginalCommandUsage::Reference, 1u> references{
        {{static_cast<std::uint32_t>(inputs.size())}}};
    auto usage = OriginalCommandUsage::prepareOriginal(inventory,
        commands.data(),
        commands.size(),
        references.data(),
        references.size(),
        graphQuota,
        stable);
    assert(usage && !stable && inventory->inventorySealed() && !usage->nativeExecutionProven());
    OriginalWorkloadResourceInspectionFixture::inspect(inventory, *device);
    // Create the retained lifetime probe before an observer may genuinely retire
    // the device. Post-retirement validation must reject that already-held probe.
    if constexpr (OriginalBindlessRootInspectionFixture<Execute>::value)
        execute.inspectOriginalRoot(root, inventory, device.get());
    observe(root, inventory, device.get());
    // The ownership builder is reusable with real devices. CPU state fixtures
    // assume initialized Null texture cells; native images start Undefined and
    // require genuine initialization/barriers before any shader consumption.
    if (exerciseCpuState) {
        std::array<CommandVariableArguments::Input, 2u> scalarFields{
            {{*CommandScalarArguments::capture(
                 CommandUsageKind::RetainIndexedSnapshot, CommandScalarArguments::None{})},
                {*CommandScalarArguments::capture(
                    CommandUsageKind::Finish, CommandScalarArguments::None{})}}};
        auto arguments = CommandVariableArguments::prepareOriginal(scalarFields.data(),
            scalarFields.size(),
            {1u, true, 2u, 0u, 0u, 0u, cpuBytes},
            graphQuota,
            stable);
        assert(arguments && !stable);
        auto scalars =
            OriginalCommandScalars::prepareOriginal(usage, arguments, graphQuota, stable);
        assert(scalars && !stable);
        auto commandInputs =
            OriginalCommandInputs::prepareOriginal(scalars, nullptr, 0u, graphQuota, stable);
        assert(commandInputs && !stable);
        auto stateQuota = quota();
        OriginalCommandStateAdmissionBudget stateBudget{1u,
            true,
            1u,
            registration.maxRetainedOwners,
            createdTableBuffers + internalBuffers + tableImages,
            tableImages,
            256u,
            cpuBytes};
        const auto stateBaseline = stateQuota->liveBytes();
        assert(!OriginalCommandResourceStates::prepareOriginal(
            commandInputs, *device, stateBudget, stateQuota, stable));
        is(stable, "RHI-ORIGINAL-BINDLESS-ROOT-STATE-JOIN-MISSING");
        stateBudget.bindlessRoots = {
            1u, true, 1u, root->size(), root->size() * inventory->sources().size()};
        const auto exactRoots = common::OriginalCommandBindlessRootAccesses::visit(
            commandInputs, stateBudget.bindlessRoots, [](const auto&) noexcept -> const char* {
                return nullptr;
            });
        assert(!exactRoots.diagnostic && exactRoots.roots == 1u
            && exactRoots.accesses == root->size());
        stateBudget.bindlessRoots.maxSourceVisits = exactRoots.sourceVisits;
        auto badState = stateBudget;
        badState.bindlessRoots.prototypeOptIn = false;
        assert(!OriginalCommandResourceStates::prepareOriginal(
            commandInputs, *device, badState, stateQuota, stable));
        is(stable, "RHI-ORIGINAL-BINDLESS-STATE-OPT-IN");
        badState = stateBudget;
        badState.bindlessRoots.maxAccesses = root->size() - 1u;
        assert(!OriginalCommandResourceStates::prepareOriginal(
            commandInputs, *device, badState, stateQuota, stable));
        is(stable, "RHI-ORIGINAL-BINDLESS-STATE-ACCESS-CAPACITY");
        badState = stateBudget;
        --badState.bindlessRoots.maxSourceVisits;
        assert(!OriginalCommandResourceStates::prepareOriginal(
            commandInputs, *device, badState, stateQuota, stable));
        is(stable, "RHI-ORIGINAL-BINDLESS-STATE-VISIT-CAPACITY");
        badState = stateBudget;
        --badState.maxStateObservations;
        assert(!OriginalCommandResourceStates::prepareOriginal(
            commandInputs, *device, badState, stateQuota, stable));
        is(stable, "RHI-ORIGINAL-RESOURCE-STATES-OBSERVATION-CAPACITY");
        badState = stateBudget;
        badState.contractVersion = 2u;
        assert(!OriginalCommandResourceStates::prepareOriginal(
            commandInputs, *device, badState, stateQuota, stable));
        is(stable, "RHI-ORIGINAL-BINDLESS-STATE-PROFILE");
        {
            CpuMemoryQuotaLease pressure(stateQuota, cpuBytes);
            assert(pressure.charged());
            assert(!OriginalCommandResourceStates::prepareOriginal(
                commandInputs, *device, stateBudget, stateQuota, stable));
            is(stable, "RHI-ORIGINAL-RESOURCE-STATES-ALLOCATION");
        }
        assert(stateQuota->liveBytes() == stateBaseline);
        auto stateGraph = OriginalCommandResourceStates::prepareOriginal(
            commandInputs, *device, stateBudget, stateQuota, stable);
        if (!stateGraph)
            std::fprintf(stderr, "root state admission: %s\n", stable ? stable : "missing");
        assert(stateGraph && !stable && !stateGraph->nativeExecutionProven());
        assert(stateGraph->bufferEntries().size() == createdTableBuffers + internalBuffers);
        assert(stateGraph->textureEntries().size() == tableImages);
        for (const auto& entry : stateGraph->textureEntries()) {
            assert(entry.point.origin
                == common::OriginalCommandTextureRanges::Point::Origin::BindlessRoot);
            assert(entry.point.descriptorSourceIndex == inputs.size());
            assert(entry.point.viewSourceIndex < inventory->sources().size());
            const auto& view = std::get<std::shared_ptr<const OriginalCommandTextureViewSource>>(
                inventory->sources()[entry.point.viewSourceIndex]);
            const auto& parent = std::get<std::shared_ptr<const OriginalCommandTextureLease>>(
                inventory->sources()[entry.point.sourceIndex]);
            assert(core::SharedOwnerProjection::sameOwner(view->originalParentLease(), parent));
            assert(defaultTextureSubresourceRange(parent->originalObject()->desc())
                == entry.point.range);
            assert(entry.point.state == ResourceState::ShaderRead
                || entry.point.state == ResourceState::ShaderWrite);
        }
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array<std::shared_ptr<CpuMemoryQuota>, 1u>{stateQuota}, [&]() noexcept {
                return stateGraph->validate();
            }));
        OriginalCommandStateSequenceBudget sequence{1u,
            true,
            2u,
            0u,
            createdTableBuffers + internalBuffers + rootTextureCells,
            root->size() + rootTextureCells - tableImages,
            root->size(),
            1u,
            true};
        auto accessResult = common::OriginalCommandAccessValidation::inspect(stateGraph, sequence);
        if (accessResult.diagnostic)
            std::fprintf(stderr, "root state sequence: %s\n", accessResult.diagnostic);
        assert(!accessResult.diagnostic && accessResult.steps == sequence.maxSteps);
        auto oldSequence = sequence;
        oldSequence.bindlessRootStateVersion = 0u;
        oldSequence.bindlessRootStateOptIn = false;
        oldSequence.maxImplicitAccesses = 0u;
        is(common::OriginalCommandAccessValidation::inspect(stateGraph, oldSequence).diagnostic,
            "RHI-ORIGINAL-BINDLESS-SEQUENCE-STATE-PROFILE");
        auto limitedSequence = sequence;
        --limitedSequence.maxImplicitAccesses;
        is(common::OriginalCommandAccessValidation::inspect(stateGraph, limitedSequence).diagnostic,
            "RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY");
        assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
            std::array<std::shared_ptr<CpuMemoryQuota>, 1u>{stateQuota}, [&]() noexcept {
                return common::OriginalCommandAccessValidation::inspect(stateGraph, sequence)
                    .diagnostic;
            }));
        auto transactionQuota = quota();
        auto transactionBudget = stateBudget;
        auto wrongTransactionBudget = transactionBudget;
        wrongTransactionBudget.bindlessRoots = {};
        assert(!OriginalCommandStateTransaction::prepareOriginalOwnership(
            stateGraph, *device, wrongTransactionBudget, transactionQuota, stable));
        is(stable, "RHI-ORIGINAL-BINDLESS-TRANSACTION-FOOTPRINT-MISMATCH");
        auto transaction = OriginalCommandStateTransaction::prepareOriginalOwnership(
            stateGraph, *device, transactionBudget, transactionQuota, stable);
        if (!transaction)
            std::fprintf(stderr, "root journal admission: %s\n", stable ? stable : "missing");
        assert(transaction && !stable && !transaction->nativeExecutionProven());
        assert(!transaction->materializeOriginalPrototypePlan(sequence));
        assert(transaction->plannedAccessStepCount() == sequence.maxSteps);
        assert(!transaction->applyOriginalPrototypePlan() && !transaction->journalEmpty());
        assert(stateGraph->validate() && !transaction->validate());
        assert(!transaction->abandonOriginalPendingOwnership() && transaction->journalEmpty());
        // Cancellation retires captured observations. Reusing the old ticket must
        // fail even when rollback restores the same logical resource state.
        assert(stateGraph->validate());
        transaction.reset();
        assert(transactionQuota->liveBytes() == 0u
            && inventory->originalStateTransactionCount() == 0u);
        stateGraph.reset();
        stateGraph = OriginalCommandResourceStates::prepareOriginal(
            commandInputs, *device, stateBudget, stateQuota, stable);
        assert(stateGraph && !stable && !stateGraph->validate());
        const std::array<CommandUsage, 2u> faultCommands{
            {{CommandUsageKind::ClearSnapshotFault, 0u, 1u}, {CommandUsageKind::Finish, 1u, 0u}}};
        auto faultUsage = OriginalCommandUsage::prepareOriginal(inventory,
            faultCommands.data(),
            faultCommands.size(),
            references.data(),
            references.size(),
            graphQuota,
            stable);
        auto faultFields = scalarFields;
        faultFields[0].scalar = *CommandScalarArguments::capture(
            CommandUsageKind::ClearSnapshotFault, CommandScalarArguments::None{});
        auto faultArguments = CommandVariableArguments::prepareOriginal(faultFields.data(),
            faultFields.size(),
            {1u, true, 2u, 0u, 0u, 0u, cpuBytes},
            graphQuota,
            stable);
        auto faultScalars =
            OriginalCommandScalars::prepareOriginal(faultUsage, faultArguments, graphQuota, stable);
        auto faultInputs =
            OriginalCommandInputs::prepareOriginal(faultScalars, nullptr, 0u, graphQuota, stable);
        assert(faultInputs && !stable);
        assert(!OriginalCommandResourceStates::prepareOriginal(
            faultInputs, *device, stateBudget, stateQuota, stable));
        is(stable, "RHI-ORIGINAL-BINDLESS-STATE-COMMAND-CLOSED");
        faultInputs.reset();
        faultScalars.reset();
        faultArguments.reset();
        faultUsage.reset();
        stateGraph.reset();
        commandInputs.reset();
        scalars.reset();
        arguments.reset();
        assert(stateQuota->liveBytes() == 0u);
        // RootState /2 consumes the exact prepared owner at dispatch/draw,
        // rather than turning a retention/binding command into shader access.
        {
            using K = CommandUsageKind;
            pmr::Vector<CommandUsage> preparedCommands{{K::RetainIndexedSnapshot, 0u, 1u},
                {K::BeginComputePass, 1u, 0u},
                {K::BindPreparedComputeBinding, 1u, 1u},
                {K::Dispatch, 2u, 0u},
                {K::EndComputePass, 2u, 0u},
                {K::Finish, 2u, 0u}};
            pmr::Vector<OriginalCommandUsage::Reference> preparedReferences{
                references[0], references[0]};
            std::shared_ptr<const RenderPassArguments> pass;
            if (graphics) {
                preparedCommands = {{K::RetainIndexedSnapshot, 0u, 1u},
                    {K::BeginRenderPass, 1u, 1u},
                    {K::BindPreparedGraphicsBinding, 2u, 1u},
                    {K::SetViewport, 3u, 0u},
                    {K::SetScissor, 3u, 0u},
                    {K::Draw, 3u, 0u},
                    {K::EndRenderPass, 3u, 0u},
                    {K::Finish, 3u, 0u}};
                preparedReferences = {references[0],
                    {static_cast<std::uint32_t>(attachmentSourceIndex)},
                    references[0]};
                RenderPassDesc desc;
                RenderPassColorAttachmentDesc color;
                color.target = attachmentId;
                color.format = TextureFormat::Rgba8Unorm;
                color.loadOp = LoadOp::Clear;
                color.storeOp = StoreOp::Store;
                desc.colorAttachments.push_back(color);
                pass = RenderPassArguments::prepareOriginal(
                    desc, {1u, true, 1u, 0u, 0u, 0u, 0u, cpuBytes}, graphQuota, stable);
                assert(pass && !stable);
            }
            const std::size_t invocation = graphics ? 5u : 3u;
            const auto activeStages = graphics
                ? std::get<std::shared_ptr<const BindlessPreparedGraphicsBinding>>(prepared)
                      ->owners()
                      ->stages()
                : ShaderStage::Compute;
            const auto makeInputs =
                [&](const auto& commandList,
                    const pmr::Vector<OriginalCommandUsage::Reference>* explicitReferences =
                        nullptr,
                    const pmr::Vector<OriginalCommandInputs::OriginalDescriptor>*
                        explicitDescriptors = nullptr,
                    bool graphicsFault = false) {
                    // Reuse every clear/copy/custody negative case for Graphics. Expand
                    // the authored Compute scope before constructing original owners;
                    // subsequent graph/stream/journal APIs see only the actual Draw.
                    auto commands = commandList;
                    auto refs = explicitReferences ? *explicitReferences : preparedReferences;
                    pmr::Vector<OriginalCommandInputs::OriginalDescriptor> descriptors;
                    if (explicitDescriptors)
                        descriptors = *explicitDescriptors;
                    else if (commandList[1].kind == K::BeginRenderPass)
                        descriptors.push_back({1u, pass});
                    if (graphicsFault) {
                        for (std::size_t i = 0u; i < commands.size(); ++i) {
                            auto& command = commands[i];
                            if (command.kind == K::BeginComputePass) {
                                command.kind = K::BeginRenderPass;
                                command.referenceCount = 1u;
                                refs.insert(refs.begin() + command.firstReference,
                                    {static_cast<std::uint32_t>(attachmentSourceIndex)});
                                for (std::size_t j = i + 1u; j < commands.size(); ++j)
                                    ++commands[j].firstReference;
                                descriptors.push_back({i, pass});
                            } else if (command.kind == K::BindPreparedComputeBinding) {
                                command.kind = K::BindPreparedGraphicsBinding;
                            } else if (command.kind == K::Dispatch) {
                                command.kind = K::Draw;
                                const auto reference = command.firstReference;
                                commands.insert(commands.begin() + i,
                                    {{K::SetViewport, reference, 0u},
                                        {K::SetScissor, reference, 0u}});
                                for (auto& descriptor : descriptors)
                                    if (descriptor.commandIndex >= i)
                                        descriptor.commandIndex += 2u;
                                i += 2u;
                            } else if (command.kind == K::EndComputePass)
                                command.kind = K::EndRenderPass;
                        }
                        std::sort(descriptors.begin(),
                            descriptors.end(),
                            [](const auto& a, const auto& b) {
                                return a.commandIndex < b.commandIndex;
                            });
                    }
                    auto used = OriginalCommandUsage::prepareOriginal(inventory,
                        commands.data(),
                        commands.size(),
                        refs.data(),
                        refs.size(),
                        graphQuota,
                        stable);
                    if (!used)
                        return std::shared_ptr<const OriginalCommandInputs>{};
                    pmr::Vector<CommandVariableArguments::Input> fields;
                    fields.reserve(commands.size());
                    for (std::size_t i = 0u; i < commands.size(); ++i) {
                        CommandScalarArguments::Dispatch dispatch{1u, 1u, 1u};
                        if constexpr (OriginalBindlessDispatchFixture<Execute>::value)
                            execute.configureDispatch(dispatch);
                        auto scalar = commands[i].kind == K::Dispatch
                            ? CommandScalarArguments::capture(K::Dispatch, dispatch)
                            : commands[i].kind == K::Draw
                            ? CommandScalarArguments::capture(
                                  K::Draw, CommandScalarArguments::Draw{3u, 1u, 0u, 0u})
                            : commands[i].kind == K::SetViewport
                            ? CommandScalarArguments::capture(
                                  K::SetViewport, ViewportState{0.0f, 0.0f, 2.0f, 2.0f, 0.0f, 1.0f})
                            : commands[i].kind == K::SetScissor
                            ? CommandScalarArguments::capture(
                                  K::SetScissor, ScissorState{0, 0, 2u, 2u})
                            : CommandScalarArguments::capture(
                                  commands[i].kind, CommandScalarArguments::None{});
                        assert(scalar);
                        fields.push_back({*scalar});
                    }
                    auto args = CommandVariableArguments::prepareOriginal(fields.data(),
                        fields.size(),
                        {1u, true, 18u, 0u, 0u, 0u, cpuBytes},
                        graphQuota,
                        stable);
                    assert(args && !stable);
                    auto values =
                        OriginalCommandScalars::prepareOriginal(used, args, graphQuota, stable);
                    auto result = OriginalCommandInputs::prepareOriginal(
                        values, descriptors.data(), descriptors.size(), graphQuota, stable);
                    if (!result)
                        std::fprintf(stderr, "prepared inputs: %s\n", stable ? stable : "missing");
                    return result;
                };
            auto preparedInputs = makeInputs(preparedCommands);
            assert(preparedInputs && !stable);
            if (!graphics) {
                // Inputs8 admits the same authentic prepared compute binding;
                // Inputs7 must keep its previous graphics-only refusal.
                OriginalCommandInputsBudget mixedInputs{8u, true, 1u};
                mixedInputs.maxIndexBindings = 1u;
                auto mixed =
                    OriginalCommandInputs::prepareOriginal(preparedInputs->originalScalars(),
                        nullptr,
                        0u,
                        mixedInputs,
                        graphQuota,
                        stable);
                assert(mixed && !stable);
                mixedInputs.contractVersion = 7u;
                assert(!OriginalCommandInputs::prepareOriginal(preparedInputs->originalScalars(),
                    nullptr,
                    0u,
                    mixedInputs,
                    graphQuota,
                    stable));
            }
            auto preparedState = stateBudget;
            if (graphics) {
                ++preparedState.maxStateObservations;
                ++preparedState.maxTextureRanges;
            }
            preparedState.bindlessRoots.contractVersion = 2u;
            std::uint64_t expectedAccesses = 0u;
            std::uint64_t expectedSteps = 0u;
            for (std::size_t i = 0u; i < root->size(); ++i) {
                BindlessResourceAccess access;
                assert(root->read(i, access));
                if ((access.stages & activeStages) != ShaderStage::None) {
                    ++expectedAccesses;
                    expectedSteps += access.kind == BindlessAccessKind::Texture
                        ? std::uint64_t{access.textureRange.mipLevelCount}
                            * access.textureRange.arrayLayerCount
                        : 1u;
                }
            }
            preparedState.bindlessRoots.maxAccesses = expectedAccesses;
            const auto projected = common::OriginalCommandBindlessRootAccesses::visit(
                preparedInputs,
                preparedState.bindlessRoots,
                [&](const auto& access) noexcept -> const char* {
                    assert(access.commandIndex == invocation
                        && access.rootSourceIndex == inputs.size());
                    assert((access.declaration.stages & activeStages) == access.declaration.stages);
                    assert(access.declaration.pipelineStages
                        == bindlessPipelineStagesForShader(access.declaration.stages));
                    return nullptr;
                });
            if (projected.diagnostic)
                std::fprintf(stderr, "prepared projection: %s\n", projected.diagnostic);
            assert(!projected.diagnostic && projected.roots == 1u
                && projected.accesses == expectedAccesses);
            preparedState.bindlessRoots.maxSourceVisits = projected.sourceVisits;
            auto badCommands = preparedCommands;
            badCommands[2].kind =
                graphics ? K::BindPreparedComputeBinding : K::BindPreparedGraphicsBinding;
            auto badInputs = makeInputs(badCommands);
            assert(!badInputs);
            is(stable, "RHI-COMMAND-ORDER-SCOPE");
            badCommands = preparedCommands;
            badCommands[2].kind = K::RetainIndexedSnapshot;
            badInputs = makeInputs(badCommands);
            assert(!badInputs);
            is(stable, "RHI-COMMAND-ORDER-PIPELINE");
            badInputs.reset();
            const pmr::Vector<CommandUsage> computeCommands{{K::RetainIndexedSnapshot, 0u, 1u},
                {K::BeginComputePass, 1u, 0u},
                {K::BindPreparedComputeBinding, 1u, 1u},
                {K::Dispatch, 2u, 0u},
                {K::EndComputePass, 2u, 0u},
                {K::Finish, 2u, 0u}};
            const pmr::Vector<OriginalCommandUsage::Reference> twoRoots{
                references[0], references[0]};
            if (graphics) {
                // Structurally valid Compute scope, but the authentic Root owns a
                // Graphics prepared binding; order alone cannot authorize it.
                badInputs = makeInputs(computeCommands, &twoRoots);
                assert(badInputs && !stable);
                is(common::OriginalCommandBindlessRootAccesses::visit(badInputs,
                       preparedState.bindlessRoots,
                       [](const auto&) noexcept -> const char* {
                           return nullptr;
                       })
                        .diagnostic,
                    "RHI-ORIGINAL-BINDLESS-STATE-PREPARED-KIND");
                badInputs.reset();
            } else {
                const pmr::Vector<CommandUsage> crossPass{{K::RetainIndexedSnapshot, 0u, 1u},
                    {K::BeginComputePass, 1u, 0u},
                    {K::BindPreparedComputeBinding, 1u, 1u},
                    {K::EndComputePass, 2u, 0u},
                    {K::BeginComputePass, 2u, 0u},
                    {K::RetainIndexedSnapshot, 2u, 1u},
                    {K::Dispatch, 3u, 0u},
                    {K::EndComputePass, 3u, 0u},
                    {K::Finish, 3u, 0u}};
                const pmr::Vector<OriginalCommandUsage::Reference> threeRoots{
                    references[0], references[0], references[0]};
                badInputs = makeInputs(crossPass, &threeRoots);
                assert(!badInputs);
                is(stable, "RHI-COMMAND-ORDER-PIPELINE");
            }
            auto graph = OriginalCommandResourceStates::prepareOriginal(
                preparedInputs, *device, preparedState, stateQuota, stable);
            if (!graph)
                std::fprintf(stderr, "prepared state: %s\n", stable ? stable : "missing");
            assert(graph && !stable && !graph->nativeExecutionProven());
            for (const auto& entry : graph->textureEntries())
                assert(entry.point.commandIndex
                    == (entry.point.origin
                                == common::OriginalCommandTextureRanges::Point::Origin::BindlessRoot
                            ? invocation
                            : 1u));
            auto preparedSequence = sequence;
            preparedSequence.maxCommands = preparedCommands.size();
            preparedSequence.bindlessRootStateVersion = 2u;
            preparedSequence.maxImplicitAccesses = expectedAccesses;
            preparedSequence.maxSteps = expectedSteps + (graphics ? 1u : 0u);
            if (graphics)
                ++preparedSequence.maxStateCells;
            const auto inspected =
                common::OriginalCommandAccessValidation::inspect(graph, preparedSequence);
            if (inspected.diagnostic)
                std::fprintf(stderr, "prepared sequence: %s\n", inspected.diagnostic);
            assert(!inspected.diagnostic && inspected.steps == preparedSequence.maxSteps);
            OriginalCommandStreamBudget streamBudget;
            streamBudget.contractVersion = 1u;
            streamBudget.prototypeOptIn = true;
            streamBudget.maxCommands = preparedCommands.size();
            streamBudget.maxReferences = preparedReferences.size();
            if (graphics) {
                streamBudget.maxDescriptors = streamBudget.maxRenderPasses =
                    streamBudget.maxAttachmentReferences = 1u;
            }
            streamBudget.maxRetainedOwners = graph->retainedOwnerCount() + 1u;
            streamBudget.maxCpuMetadataBytes = cpuBytes;
            streamBudget.maxPipelineBinds = streamBudget.maxConsumptions = 1u;
            streamBudget.maxImplicitAccesses = expectedAccesses;
            streamBudget.stateSequence = preparedSequence;
            streamBudget.bindlessRoots = preparedState.bindlessRoots;
            auto streamQuota = quota();
            auto legacyStream = streamBudget;
            legacyStream.bindlessRoots = {};
            legacyStream.stateSequence = {};
            legacyStream.maxPipelineBinds = legacyStream.maxConsumptions =
                legacyStream.maxImplicitAccesses = 0u;
            is(OriginalCommandStreamProgram::inspect(preparedInputs, legacyStream).diagnostic,
                "RHI-ORIGINAL-BINDLESS-STREAM-STATE-REQUIRED");
            is(OriginalCommandStreamProgram::inspect(preparedInputs, streamBudget).diagnostic,
                "RHI-ORIGINAL-STREAM-TYPED-STATE-REQUIRED");
            auto stream = OriginalCommandStreamProgram::prepareOriginalTyped(
                graph, streamBudget, streamQuota, stable);
            if (!stream)
                std::fprintf(stderr, "prepared stream: %s\n", stable ? stable : "missing");
            assert(stream && !stable && !stream->nativeExecutionProven());
            assert(stream->counts().pipelineBinds == 1u && stream->counts().consumptions == 1u
                && stream->counts().implicitAccesses == expectedAccesses);
            assert(stream->operations()[2].implicitAccessCount == 0u
                && stream->operations()[invocation].implicitAccessCount == expectedAccesses);
            auto limitedStream = streamBudget;
            --limitedStream.maxImplicitAccesses;
            is(OriginalCommandStreamProgram::inspectTyped(graph, limitedStream).diagnostic,
                "RHI-ORIGINAL-STREAM-IMPLICIT-CAPACITY");
            limitedStream = streamBudget;
            ++limitedStream.bindlessRoots.maxSourceVisits;
            is(OriginalCommandStreamProgram::inspectTyped(graph, limitedStream).diagnostic,
                "RHI-ORIGINAL-BINDLESS-STREAM-FOOTPRINT-MISMATCH");
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array<std::shared_ptr<CpuMemoryQuota>, 2u>{stateQuota, streamQuota},
                [&]() noexcept {
                    return stream->validate();
                }));
            auto tx = OriginalCommandStateTransaction::prepareOriginalOwnership(
                graph, *device, preparedState, transactionQuota, stable);
            assert(tx && !stable && !tx->materializeOriginalPrototypePlan(preparedSequence));
            assert(tx->plannedAccessStepCount() == preparedSequence.maxSteps);
            assert(!tx->applyOriginalPrototypePlan() && !tx->journalEmpty());
            assert(!tx->abandonOriginalPendingOwnership() && tx->journalEmpty());
            assert(graph->validate() && stream->validate());
            tx.reset();
            stream.reset();
            graph.reset();
            preparedInputs.reset();
            if (artifact.faultLatch.contractVersion) {
                const auto makeFaultInputs =
                    [&](const auto& commands, const auto* refs, const auto* descriptors) {
                        return makeInputs(commands, refs, descriptors, graphics);
                    };
                const auto faultOrdinal = [graphics](std::size_t computeOrdinal) {
                    return computeOrdinal + (graphics && computeOrdinal >= 5u ? 2u : 0u);
                };
                const auto faultCommandCount = [graphics](std::size_t count) {
                    return count + (graphics ? 2u : 0u);
                };
                // RootState /3 clears only the snapshot's authentic canonical latch.
                // The same timeline must transition it to CopyDst and back before
                // the shader invocation. No alternate state/journal is introduced.
                const auto& latch = snapshot->originalBufferSource(
                    BindlessSnapshot::OriginalBufferRole::FaultLatch);
                assert(latch);
                std::size_t latchIndex = SIZE_MAX;
                for (std::size_t i = 0u; i < inputs.size(); ++i)
                    if (Sources::resource(inputs[i]) == latch->resource())
                        latchIndex = i;
                assert(latchIndex != SIZE_MAX);
                const pmr::Vector<CommandUsage> clearedCommands{{K::BufferBarrier, 0u, 1u},
                    {K::ClearSnapshotFault, 1u, 1u},
                    {K::BufferBarrier, 2u, 1u},
                    {K::BeginComputePass, 3u, 0u},
                    {K::BindPreparedComputeBinding, 3u, 1u},
                    {K::Dispatch, 4u, 0u},
                    {K::EndComputePass, 4u, 0u},
                    {K::Finish, 4u, 0u}};
                const pmr::Vector<OriginalCommandUsage::Reference> clearedReferences{
                    {static_cast<std::uint32_t>(latchIndex)},
                    references[0],
                    {static_cast<std::uint32_t>(latchIndex)},
                    references[0]};
                ResourceBarrierDesc enter{
                    latch->resource(), {}, ResourceState::ShaderWrite, ResourceState::CopyDst};
                enter.sourceStages = bindlessPipelineStagesForShader(activeStages);
                enter.sourceAccess = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
                enter.destinationStages = PipelineStage::Transfer;
                enter.destinationAccess = ResourceAccess::TransferWrite;
                ResourceBarrierDesc leave{
                    latch->resource(), {}, ResourceState::CopyDst, ResourceState::ShaderWrite};
                leave.sourceStages = PipelineStage::Transfer;
                leave.sourceAccess = ResourceAccess::TransferWrite;
                leave.destinationStages = bindlessPipelineStagesForShader(activeStages);
                leave.destinationAccess = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
                pmr::Vector<OriginalCommandInputs::OriginalDescriptor> clearDescriptors{
                    {0u, *ResourceDependencyArguments::capture(K::BufferBarrier, enter)},
                    {2u, *ResourceDependencyArguments::capture(K::BufferBarrier, leave)}};
                auto clearedInputs =
                    makeFaultInputs(clearedCommands, &clearedReferences, &clearDescriptors);
                assert(clearedInputs && !stable);
                auto clearState = preparedState;
                clearState.bindlessRoots.contractVersion = 3u;
                clearState.bindlessRoots.maxFaultClears = 1u;
                clearState.bindlessRoots.maxRoots = 2u;
                ++clearState.bindlessRoots.maxAccesses;
                clearState.bindlessRoots.maxSourceVisits += inventory->sources().size();
                const auto projection =
                    common::OriginalCommandBindlessRootAccesses::visit(clearedInputs,
                        clearState.bindlessRoots,
                        [&](const auto& access) noexcept -> const char* {
                            if (access.commandIndex == 1u) {
                                assert(access.sourceIndex == latchIndex
                                    && access.declaration.resource == latch->resource());
                                assert(access.declaration.state == ResourceState::CopyDst
                                    && access.declaration.access == ResourceAccess::TransferWrite
                                    && access.declaration.pipelineStages == PipelineStage::Transfer
                                    && access.declaration.stages == ShaderStage::None
                                    && access.declaration.bufferSize == sizeof(std::uint32_t));
                            } else
                                assert(access.commandIndex == faultOrdinal(5u));
                            return nullptr;
                        });
                assert(!projection.diagnostic && projection.faultClears == 1u
                    && projection.roots == 2u);
                clearState.bindlessRoots.maxSourceVisits = projection.sourceVisits;
                is(common::OriginalCommandBindlessRootAccesses::visit(clearedInputs,
                       preparedState.bindlessRoots,
                       [](const auto&) noexcept -> const char* {
                           return nullptr;
                       })
                        .diagnostic,
                    "RHI-ORIGINAL-BINDLESS-STATE-COMMAND-CLOSED");
                auto limited = clearState;
                limited.bindlessRoots.maxFaultClears = 0u;
                assert(!OriginalCommandResourceStates::prepareOriginal(
                    clearedInputs, *device, limited, stateQuota, stable));
                is(stable, "RHI-ORIGINAL-BINDLESS-STATE-FAULT-BUDGET");
                limited = preparedState;
                limited.bindlessRoots.maxFaultClears = 1u;
                assert(!OriginalCommandResourceStates::prepareOriginal(
                    clearedInputs, *device, limited, stateQuota, stable));
                is(stable, "RHI-ORIGINAL-BINDLESS-STATE-FAULT-LEGACY-SCHEMA");
                auto doubleCommands = clearedCommands;
                doubleCommands.insert(doubleCommands.begin() + 2u, {K::ClearSnapshotFault, 2u, 1u});
                for (std::size_t i = 3u; i < doubleCommands.size(); ++i)
                    ++doubleCommands[i].firstReference;
                auto doubleReferences = clearedReferences;
                doubleReferences.insert(doubleReferences.begin() + 2u, references[0]);
                auto doubleDescriptors = clearDescriptors;
                doubleDescriptors[1].commandIndex = 3u;
                auto doubleInputs =
                    makeFaultInputs(doubleCommands, &doubleReferences, &doubleDescriptors);
                assert(doubleInputs && !stable);
                assert(!OriginalCommandResourceStates::prepareOriginal(
                    doubleInputs, *device, clearState, stateQuota, stable));
                is(stable, "RHI-ORIGINAL-BINDLESS-STATE-FAULT-CAPACITY");
                doubleInputs.reset();
                graph = OriginalCommandResourceStates::prepareOriginal(
                    clearedInputs, *device, clearState, stateQuota, stable);
                assert(graph && !stable);
                auto clearSequence = preparedSequence;
                clearSequence.bindlessRootStateVersion = 3u;
                clearSequence.maxCommands = faultCommandCount(clearedCommands.size());
                ++clearSequence.maxImplicitAccesses;
                clearSequence.maxSteps += 3u;
                const auto inspectedClear =
                    common::OriginalCommandAccessValidation::inspect(graph, clearSequence);
                if (inspectedClear.diagnostic)
                    std::fprintf(stderr, "fault clear sequence: %s\n", inspectedClear.diagnostic);
                assert(
                    !inspectedClear.diagnostic && inspectedClear.steps == clearSequence.maxSteps);
                const pmr::Vector<CommandUsage> untransitionedCommands{
                    {K::ClearSnapshotFault, 0u, 1u},
                    {K::BeginComputePass, 1u, 0u},
                    {K::BindPreparedComputeBinding, 1u, 1u},
                    {K::Dispatch, 2u, 0u},
                    {K::EndComputePass, 2u, 0u},
                    {K::Finish, 2u, 0u}};
                auto untransitionedInputs = makeFaultInputs(untransitionedCommands,
                    &twoRoots,
                    static_cast<const pmr::Vector<OriginalCommandInputs::OriginalDescriptor>*>(
                        nullptr));
                auto untransitionedGraph = OriginalCommandResourceStates::prepareOriginal(
                    untransitionedInputs, *device, clearState, stateQuota, stable);
                assert(untransitionedGraph && !stable);
                is(common::OriginalCommandAccessValidation::inspect(
                       untransitionedGraph, clearSequence)
                        .diagnostic,
                    "RHI-ORIGINAL-BINDLESS-SEQUENCE-BUFFER-STATE");
                untransitionedGraph.reset();
                untransitionedInputs.reset();
                auto clearStreamBudget = streamBudget;
                clearStreamBudget.bindlessRoots = clearState.bindlessRoots;
                clearStreamBudget.stateSequence = clearSequence;
                clearStreamBudget.maxCommands = faultCommandCount(clearedCommands.size());
                clearStreamBudget.maxReferences = clearedReferences.size() + (graphics ? 1u : 0u);
                clearStreamBudget.maxDescriptors = clearDescriptors.size() + (graphics ? 1u : 0u);
                ++clearStreamBudget.maxImplicitAccesses;
                stream = OriginalCommandStreamProgram::prepareOriginalTyped(
                    graph, clearStreamBudget, streamQuota, stable);
                assert(stream && !stable && stream->counts().consumptions == 1u
                    && stream->counts().implicitAccesses == expectedAccesses + 1u
                    && stream->operations()[1].implicitAccessCount == 1u);
                tx = OriginalCommandStateTransaction::prepareOriginalOwnership(
                    graph, *device, clearState, transactionQuota, stable);
                assert(tx && !stable && !tx->materializeOriginalPrototypePlan(clearSequence));
                assert(tx->plannedAccessStepCount() == clearSequence.maxSteps);
                assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    std::array<std::shared_ptr<CpuMemoryQuota>, 2u>{stateQuota, streamQuota},
                    [&]() noexcept {
                        return stream->validate();
                    }));
                assert(!tx->applyOriginalPrototypePlan() && !tx->abandonOriginalPendingOwnership());
                assert(graph->validate() && stream->validate());
                tx.reset();
                stream.reset();
                graph.reset();
                clearedInputs.reset();
                // RootState /4 authenticates the existing explicit BufferCopy;
                // transfer accesses stay in their original ordinal/journal domain.
                const auto& readback = snapshot->originalBufferSource(
                    BindlessSnapshot::OriginalBufferRole::FaultReadback);
                assert(readback && readback->resource() != latch->resource());
                std::size_t readbackIndex = SIZE_MAX;
                for (std::size_t i = 0u; i < inputs.size(); ++i)
                    if (Sources::resource(inputs[i]) == readback->resource())
                        readbackIndex = i;
                assert(readbackIndex != SIZE_MAX);
                auto copyCommands = clearedCommands;
                copyCommands.pop_back();
                copyCommands.insert(copyCommands.end(),
                    {{K::BufferBarrier, 4u, 1u},
                        {K::BeginTransferPass, 5u, 0u},
                        {K::BufferCopy, 5u, 2u},
                        {K::EndTransferPass, 7u, 0u},
                        {K::BufferBarrier, 7u, 1u},
                        {K::Finish, 8u, 0u}});
                auto copyReferences = clearedReferences;
                copyReferences.insert(copyReferences.end(),
                    {{static_cast<std::uint32_t>(latchIndex)},
                        {static_cast<std::uint32_t>(latchIndex)},
                        {static_cast<std::uint32_t>(readbackIndex)},
                        {static_cast<std::uint32_t>(latchIndex)}});
                ResourceBarrierDesc toCopy{
                    latch->resource(), {}, ResourceState::ShaderWrite, ResourceState::CopySrc};
                toCopy.sourceStages = bindlessPipelineStagesForShader(activeStages);
                toCopy.sourceAccess = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
                toCopy.destinationStages = PipelineStage::Transfer;
                toCopy.destinationAccess = ResourceAccess::TransferRead;
                ResourceBarrierDesc fromCopy{
                    latch->resource(), {}, ResourceState::CopySrc, ResourceState::ShaderWrite};
                fromCopy.sourceStages = PipelineStage::Transfer;
                fromCopy.sourceAccess = ResourceAccess::TransferRead;
                fromCopy.destinationStages = bindlessPipelineStagesForShader(activeStages);
                fromCopy.destinationAccess =
                    ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
                const BufferCopyDesc faultCopyDesc{
                    latch->resource(), readback->resource(), 0u, 0u, sizeof(std::uint32_t)};
                auto copyDescriptors = clearDescriptors;
                copyDescriptors.insert(copyDescriptors.end(),
                    {{7u, *ResourceDependencyArguments::capture(K::BufferBarrier, toCopy)},
                        {9u, *TransferCopyArguments::capture(faultCopyDesc)},
                        {11u, *ResourceDependencyArguments::capture(K::BufferBarrier, fromCopy)}});
                auto copiedInputs =
                    makeFaultInputs(copyCommands, &copyReferences, &copyDescriptors);
                assert(copiedInputs && !stable);
                auto copyState = clearState;
                copyState.bindlessRoots.contractVersion = 4u;
                copyState.bindlessRoots.maxFaultCopies = 1u;
                copyState.bindlessRoots.maxRoots = 3u;
                copyState.bindlessRoots.maxSourceVisits += inventory->sources().size();
                const auto copied = common::OriginalCommandBindlessRootAccesses::visit(
                    copiedInputs, copyState.bindlessRoots, [](const auto&) noexcept -> const char* {
                        return nullptr;
                    });
                assert(!copied.diagnostic && copied.faultCopies == 1u && copied.faultClears == 1u
                    && copied.roots == 3u && copied.accesses == projection.accesses);
                copyState.bindlessRoots.maxSourceVisits = copied.sourceVisits;
                auto rejectCopyBudget = [&](const auto& limits, const char* id) {
                    const auto baselineBytes = stateQuota->liveBytes();
                    assert(!OriginalCommandResourceStates::prepareOriginal(
                        copiedInputs, *device, limits, stateQuota, stable));
                    is(stable, id);
                    assert(stateQuota->liveBytes() == baselineBytes);
                };
                auto copyLimited = copyState;
                copyLimited.bindlessRoots.maxFaultCopies = 0u;
                rejectCopyBudget(copyLimited, "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-BUDGET");
                copyLimited = copyState;
                copyLimited.bindlessRoots.contractVersion = 3u;
                rejectCopyBudget(
                    copyLimited, "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-LEGACY-SCHEMA");
                copyLimited = copyState;
                --copyLimited.bindlessRoots.maxRoots;
                rejectCopyBudget(copyLimited, "RHI-ORIGINAL-BINDLESS-STATE-ROOT-CAPACITY");
                copyLimited = copyState;
                --copyLimited.bindlessRoots.maxSourceVisits;
                rejectCopyBudget(copyLimited, "RHI-ORIGINAL-BINDLESS-STATE-VISIT-CAPACITY");
                for (unsigned mutation = 0u; mutation < 3u; ++mutation) {
                    auto changed = faultCopyDesc;
                    if (mutation == 0u)
                        changed.sourceOffsetBytes = 1u;
                    if (mutation == 1u)
                        changed.destinationOffsetBytes = 1u;
                    if (mutation == 2u)
                        changed.sizeBytes = 0u;
                    auto changedDescriptors = copyDescriptors;
                    changedDescriptors[3].arguments = *TransferCopyArguments::capture(changed);
                    auto changedInputs =
                        makeFaultInputs(copyCommands, &copyReferences, &changedDescriptors);
                    assert(changedInputs && !stable);
                    is(common::OriginalCommandBindlessRootAccesses::visit(changedInputs,
                           copyState.bindlessRoots,
                           [](const auto&) noexcept -> const char* {
                               return nullptr;
                           })
                            .diagnostic,
                        "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-RANGE");
                }
                auto reversedDesc = faultCopyDesc;
                std::swap(reversedDesc.sourceBuffer, reversedDesc.destinationBuffer);
                auto reversedReferences = copyReferences;
                std::swap(reversedReferences[5], reversedReferences[6]);
                auto reversedDescriptors = copyDescriptors;
                reversedDescriptors[3].arguments = *TransferCopyArguments::capture(reversedDesc);
                auto reversedInputs =
                    makeFaultInputs(copyCommands, &reversedReferences, &reversedDescriptors);
                assert(reversedInputs && !stable);
                is(common::OriginalCommandBindlessRootAccesses::visit(reversedInputs,
                       copyState.bindlessRoots,
                       [](const auto&) noexcept -> const char* {
                           return nullptr;
                       })
                        .diagnostic,
                    "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-SNAPSHOT");
                reversedInputs.reset();
                auto duplicateCommands = copyCommands;
                duplicateCommands.insert(duplicateCommands.begin() + 10u, {K::BufferCopy, 7u, 2u});
                for (std::size_t i = 11u; i < duplicateCommands.size(); ++i)
                    duplicateCommands[i].firstReference += 2u;
                auto duplicateReferences = copyReferences;
                duplicateReferences.insert(duplicateReferences.begin() + 7u,
                    {{static_cast<std::uint32_t>(latchIndex)},
                        {static_cast<std::uint32_t>(readbackIndex)}});
                auto duplicateDescriptors = copyDescriptors;
                duplicateDescriptors.back().commandIndex = 12u;
                duplicateDescriptors.insert(duplicateDescriptors.begin() + 4u,
                    {10u, *TransferCopyArguments::capture(faultCopyDesc)});
                auto duplicateInputs =
                    makeFaultInputs(duplicateCommands, &duplicateReferences, &duplicateDescriptors);
                assert(duplicateInputs && !stable);
                assert(!OriginalCommandResourceStates::prepareOriginal(
                    duplicateInputs, *device, copyState, stateQuota, stable));
                is(stable, "RHI-ORIGINAL-BINDLESS-STATE-FAULT-COPY-CAPACITY");
                duplicateInputs.reset();
                graph = OriginalCommandResourceStates::prepareOriginal(
                    copiedInputs, *device, copyState, stateQuota, stable);
                assert(graph && !stable);
                auto copySequence = clearSequence;
                copySequence.bindlessRootStateVersion = 4u;
                copySequence.maxCommands = faultCommandCount(copyCommands.size());
                copySequence.maxSteps += 4u;
                const auto copyInspection =
                    common::OriginalCommandAccessValidation::inspect(graph, copySequence);
                if (copyInspection.diagnostic)
                    std::fprintf(stderr, "fault copy sequence: %s\n", copyInspection.diagnostic);
                assert(!copyInspection.diagnostic && copyInspection.steps == copySequence.maxSteps);
                auto missingCommands = copyCommands;
                missingCommands.erase(missingCommands.begin() + 7u);
                for (std::size_t i = 7u; i < missingCommands.size(); ++i)
                    --missingCommands[i].firstReference;
                auto missingReferences = copyReferences;
                missingReferences.erase(missingReferences.begin() + 4u);
                auto missingDescriptors = copyDescriptors;
                missingDescriptors.erase(missingDescriptors.begin() + 2u);
                --missingDescriptors[2].commandIndex;
                --missingDescriptors[3].commandIndex;
                auto missingInputs =
                    makeFaultInputs(missingCommands, &missingReferences, &missingDescriptors);
                assert(missingInputs && !stable);
                auto missingGraph = OriginalCommandResourceStates::prepareOriginal(
                    missingInputs, *device, copyState, stateQuota, stable);
                assert(missingGraph && !stable);
                is(common::OriginalCommandAccessValidation::inspect(missingGraph, copySequence)
                        .diagnostic,
                    "RHI-ORIGINAL-SEQUENCE-COPY-STATE");
                missingGraph.reset();
                missingInputs.reset();
                auto copyStreamBudget = clearStreamBudget;
                copyStreamBudget.bindlessRoots = copyState.bindlessRoots;
                copyStreamBudget.stateSequence = copySequence;
                copyStreamBudget.maxCommands = faultCommandCount(copyCommands.size());
                copyStreamBudget.maxReferences = copyReferences.size() + (graphics ? 1u : 0u);
                copyStreamBudget.maxDescriptors = copyDescriptors.size() + (graphics ? 1u : 0u);
                copyStreamBudget.maxCopyElements = 1u;
                stream = OriginalCommandStreamProgram::prepareOriginalTyped(
                    graph, copyStreamBudget, streamQuota, stable);
                assert(stream && !stable && stream->counts().copyElements == 1u
                    && stream->counts().consumptions == 1u
                    && stream->counts().implicitAccesses == expectedAccesses + 1u
                    && stream->operations()[faultOrdinal(9u)].implicitAccessCount == 0u);
                // A genuine buffer request captures the original registration,
                // then joins the existing /4 copy/graph/program. Its independent
                // unpublished result deliberately retains no GPU resource.
                using BufferRequest = BufferReadbackRequestDeclaration;
                using BufferResult = OriginalCommandBufferReadbackResultSnapshot;
                using BufferBinding = OriginalCommandBufferReadbackBinding;
                const BufferReadbackIdentity identity{1u, 1u, snapshot->generation()};
                const BufferReadbackRequestBudget requestBudget{
                    1u, true, sizeof(std::uint32_t), cpuBytes};
                const auto requestBaseline = graphQuota->liveBytes();
                const auto prepareRequest = [&](auto id,
                                                auto offset,
                                                auto bytes,
                                                const auto& limits,
                                                const auto& owner,
                                                const auto& metadata) {
                    return BufferRequest::prepareOriginal(
                        owner, *device, id, offset, bytes, limits, metadata, stable);
                };
                auto rejectedBudget = requestBudget;
                rejectedBudget.contractVersion = 0u;
                assert(!prepareRequest(identity, 0u, 4u, rejectedBudget, readback, graphQuota));
                is(stable, "RHI-BUFFER-READBACK-REQUEST-VERSION");
                rejectedBudget = requestBudget;
                rejectedBudget.prototypeOptIn = false;
                assert(!prepareRequest(identity, 0u, 4u, rejectedBudget, readback, graphQuota));
                is(stable, "RHI-BUFFER-READBACK-REQUEST-OPT-IN");
                rejectedBudget = requestBudget;
                rejectedBudget.maxReadbackBytes = 3u;
                assert(!prepareRequest(identity, 0u, 4u, rejectedBudget, readback, graphQuota));
                is(stable, "RHI-BUFFER-READBACK-REQUEST-RANGE");
                for (unsigned field = 0u; field < 3u; ++field) {
                    auto invalidIdentity = identity;
                    if (field == 0u)
                        invalidIdentity.requestId = 0u;
                    if (field == 1u)
                        invalidIdentity.frameSerial = 0u;
                    if (field == 2u)
                        invalidIdentity.resourceGeneration = 0u;
                    assert(!prepareRequest(
                        invalidIdentity, 0u, 4u, requestBudget, readback, graphQuota));
                    is(stable, "RHI-BUFFER-READBACK-REQUEST-IDENTITY");
                }
                for (const auto range :
                    {std::array<std::uint64_t, 2>{4u, 4u}, {0u, 0u}, {0u, 3u}}) {
                    assert(!prepareRequest(
                        identity, range[0], range[1], requestBudget, readback, graphQuota));
                    is(stable, "RHI-BUFFER-READBACK-REQUEST-ALIGNMENT");
                }
                assert(!prepareRequest(
                    identity, UINT64_MAX - 7u, 4u, requestBudget, readback, graphQuota));
                is(stable, "RHI-BUFFER-READBACK-REQUEST-RANGE");
                assert(!prepareRequest(identity, 0u, 4u, requestBudget, latch, graphQuota));
                is(stable, "RHI-BUFFER-READBACK-REQUEST-MEMORY");
                assert(!prepareRequest(identity, 0u, 4u, requestBudget, readback, sourceQuota));
                is(stable, "RHI-BUFFER-READBACK-REQUEST-QUOTA-DOMAIN");
                {
                    CpuMemoryQuotaLease pressure(graphQuota, cpuBytes - requestBaseline);
                    assert(pressure.charged()
                        && !prepareRequest(identity, 0u, 4u, requestBudget, readback, graphQuota));
                    is(stable, "RHI-BUFFER-READBACK-REQUEST-ALLOCATION");
                }
                assert(graphQuota->liveBytes() == requestBaseline);
                auto bufferRequest =
                    prepareRequest(identity, 0u, 4u, requestBudget, readback, graphQuota);
                assert(bufferRequest && !stable && bufferRequest->identity() == identity
                    && bufferRequest->source() == readback->resource()
                    && bufferRequest->offsetBytes() == 0u
                    && bufferRequest->registrationGeneration()
                        == readback->originalObject()->originalResourceRegistration().generation()
                    && !bufferRequest->nativeExecutionProven());
                assert(!bufferRequest->matchesOriginalSource(readback, *device));
                is(bufferRequest->matchesOriginalSource(latch, *device),
                    "RHI-BUFFER-READBACK-REQUEST-SOURCE");
                auto forgedRequest =
                    std::shared_ptr<const BufferRequest>(graphQuota, bufferRequest.get());
                assert(!BufferResult::prepareOriginalCold(forgedRequest, quota(), stable));
                is(stable, "RHI-ORIGINAL-READBACK-SNAPSHOT-REQUEST-OWNER");
                forgedRequest.reset();
                assert(!BufferResult::prepareOriginalCold(bufferRequest, graphQuota, stable));
                is(stable, "RHI-ORIGINAL-READBACK-SNAPSHOT-QUOTA-DOMAIN");
                auto resultQuota = quota();
                {
                    CpuMemoryQuotaLease pressure(resultQuota, cpuBytes);
                    assert(pressure.charged()
                        && !BufferResult::prepareOriginalCold(bufferRequest, resultQuota, stable));
                    is(stable, "RHI-ORIGINAL-READBACK-SNAPSHOT-ALLOCATION");
                }
                assert(resultQuota->liveBytes() == 0u);
                auto bufferResult =
                    BufferResult::prepareOriginalCold(bufferRequest, resultQuota, stable);
                assert(bufferResult && !stable
                    && bufferResult->byteCapacity() == sizeof(std::uint32_t)
                    && !bufferResult->publishedBytes() && !bufferResult->nativeExecutionProven());
                const OriginalCommandBufferReadbackBindingBudget bindingBudget{1u,
                    true,
                    BufferBinding::retainedOwnerCount,
                    faultCommandCount(copyCommands.size()),
                    cpuBytes};
                auto bound = BufferBinding::prepareOriginal(graph,
                    stream,
                    bufferRequest,
                    bufferResult,
                    faultOrdinal(9u),
                    references[0].sourceIndex,
                    *device,
                    bindingBudget,
                    graphQuota,
                    stable);
                assert(bound && !stable && !bound->nativeExecutionProven()
                    && core::SharedOwnerProjection::sameOwner(bound->originalGraph(), graph)
                    && core::SharedOwnerProjection::sameOwner(bound->originalProgram(), stream)
                    && core::SharedOwnerProjection::sameOwner(
                        bound->originalRequest(), bufferRequest)
                    && core::SharedOwnerProjection::sameOwner(
                        bound->originalResult(), bufferResult));
                auto pendingBudget = bindingBudget;
                pendingBudget.contractVersion = 2u;
                auto retained = BufferBinding::prepareOriginal(graph,
                    stream,
                    bufferRequest,
                    bufferResult,
                    faultOrdinal(9u),
                    references[0].sourceIndex,
                    *device,
                    pendingBudget,
                    graphQuota,
                    stable);
                assert(retained && !stable);
                if constexpr (OriginalBindlessReadbackObserver<Observe>::value) {
                    auto observed = OriginalCommandStateTransaction::prepareOriginalOwnership(
                        graph, *device, copyState, transactionQuota, stable);
                    assert(observed && !stable
                        && !observed->materializeOriginalPrototypePlan(copySequence));
                    observe.observeOriginalReadback(retained, observed, *device);
                }
                if constexpr (Execute::enabled) {
                    tx = OriginalCommandStateTransaction::prepareOriginalOwnership(
                        graph, *device, copyState, transactionQuota, stable);
                    assert(tx && !stable && !tx->materializeOriginalPrototypePlan(copySequence));
                    execute(root, inventory, retained, tx, *device);
                    retiredBufferRequest = bufferRequest;
                    retiredBufferResult = bufferResult;
                    retained.reset();
                    bound.reset();
                    bufferRequest.reset();
                    bufferResult.reset();
                    tx.reset();
                    stream.reset();
                    graph.reset();
                    copiedInputs.reset();
                } else {
                    auto differentStreamBudget = copyStreamBudget;
                    ++differentStreamBudget.stateSequence.maxCommands;
                    auto differentStream = OriginalCommandStreamProgram::prepareOriginalTyped(
                        graph, differentStreamBudget, streamQuota, stable);
                    assert(differentStream && !stable);
                    auto differentBinding = BufferBinding::prepareOriginal(graph,
                        differentStream,
                        bufferRequest,
                        bufferResult,
                        faultOrdinal(9u),
                        references[0].sourceIndex,
                        *device,
                        pendingBudget,
                        graphQuota,
                        stable);
                    assert(differentBinding && !stable);
                    auto legacyFault = BindlessFaultSubmissionLease::acquire(snapshot, diagnostic);
                    assert(legacyFault.valid());
                    assert(
                        !BufferBinding::acquireOriginalFaultCustody(retained, diagnostic).valid());
                    assert(diagnostic == "RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT"
                        && !snapshot->originalJournalMode());
                    legacyFault.release();
                    assert(!BufferBinding::acquireOriginalFaultCustody(bound, diagnostic).valid());
                    assert(diagnostic == "RHI-ORIGINAL-BUFFER-READBACK-PENDING-VERSION");
                    auto faultCustody =
                        BufferBinding::acquireOriginalFaultCustody(retained, diagnostic);
                    assert(faultCustody.originalJournalOwned() && snapshot->originalJournalMode()
                        && snapshot->originalSubmissionBudgetOwner()->used() == 1u
                        && core::SharedOwnerProjection::sameOwner(
                            faultCustody.originalDomain(), snapshot));
                    assert(
                        !BufferBinding::acquireOriginalFaultCustody(retained, diagnostic).valid());
                    assert(diagnostic == "RHI-BINDLESS-FAULT-SUBMISSION-IN-FLIGHT");
                    auto bindingLimited = bindingBudget;
                    --bindingLimited.maxOperationVisits;
                    assert(!BufferBinding::prepareOriginal(graph,
                        stream,
                        bufferRequest,
                        bufferResult,
                        faultOrdinal(9u),
                        references[0].sourceIndex,
                        *device,
                        bindingLimited,
                        graphQuota,
                        stable));
                    is(stable, "RHI-ORIGINAL-BUFFER-READBACK-BINDING-VISIT-CAPACITY");
                    assert(!BufferBinding::prepareOriginal(graph,
                        stream,
                        bufferRequest,
                        bufferResult,
                        faultOrdinal(5u),
                        references[0].sourceIndex,
                        *device,
                        bindingBudget,
                        graphQuota,
                        stable));
                    is(stable, "RHI-ORIGINAL-BUFFER-READBACK-BINDING-COMMAND");
                    assert(!BufferBinding::prepareOriginal(graph,
                        stream,
                        bufferRequest,
                        bufferResult,
                        faultOrdinal(9u),
                        latchIndex,
                        *device,
                        bindingBudget,
                        graphQuota,
                        stable));
                    is(stable, "RHI-ORIGINAL-BUFFER-READBACK-BINDING-ROOT");
                    auto wrongIdentity = identity;
                    ++wrongIdentity.resourceGeneration;
                    auto wrongRequest =
                        prepareRequest(wrongIdentity, 0u, 4u, requestBudget, readback, graphQuota);
                    assert(wrongRequest && !stable);
                    assert(!BufferBinding::prepareOriginal(graph,
                        stream,
                        wrongRequest,
                        bufferResult,
                        faultOrdinal(9u),
                        references[0].sourceIndex,
                        *device,
                        bindingBudget,
                        graphQuota,
                        stable));
                    is(stable, "RHI-ORIGINAL-READBACK-SNAPSHOT-REQUEST");
                    auto wrongResult =
                        BufferResult::prepareOriginalCold(wrongRequest, quota(), stable);
                    assert(wrongResult && !stable);
                    assert(!BufferBinding::prepareOriginal(graph,
                        stream,
                        wrongRequest,
                        wrongResult,
                        faultOrdinal(9u),
                        references[0].sourceIndex,
                        *device,
                        bindingBudget,
                        graphQuota,
                        stable));
                    is(stable, "RHI-ORIGINAL-BUFFER-READBACK-BINDING-GENERATION");
                    wrongResult.reset();
                    wrongRequest.reset();
                    const std::array<std::shared_ptr<const BufferBinding>, 1u> readbackBindings{
                        bound};
                    const OriginalCommandReadbackResultContract::Budget resultBudget{1u,
                        true,
                        1u,
                        1u,
                        1u,
                        sizeof(std::uint32_t),
                        sizeof(std::uint32_t),
                        cpuBytes};
                    const auto resultVisits = faultCommandCount(copyCommands.size()) + 3u;
                    const auto projectedResults =
                        OriginalCommandBufferReadbackResultContract::inspect(
                            readbackBindings.data(),
                            readbackBindings.size(),
                            resultVisits,
                            resultBudget);
                    assert(!projectedResults.diagnostic
                        && projectedResults.required.readbackCommands == 1u
                        && projectedResults.required.nativeCopyCommands == 1u
                        && projectedResults.required.resultDeclarations == 1u
                        && projectedResults.required.declaredReadbackBytes == sizeof(std::uint32_t)
                        && projectedResults.required.uniqueResultBytes == sizeof(std::uint32_t)
                        && projectedResults.required.cpuResultBytes == resultQuota->limitBytes());
                    const auto shortResults = OriginalCommandBufferReadbackResultContract::inspect(
                        readbackBindings.data(),
                        readbackBindings.size(),
                        resultVisits - 1u,
                        resultBudget);
                    is(shortResults.diagnostic,
                        "RHI-ORIGINAL-BUFFER-READBACK-RESULT-VISIT-CAPACITY");
                    assert(shortResults.required.resultDeclarations == 0u
                        && shortResults.required.uniqueResultBytes == 0u);
                    auto shortBytes = resultBudget;
                    shortBytes.maxUniqueResultBytes = 3u;
                    is(OriginalCommandBufferReadbackResultContract::inspect(
                           readbackBindings.data(), 1u, resultVisits, shortBytes)
                            .diagnostic,
                        "RHI-ORIGINAL-BUFFER-READBACK-RESULT-BYTE-CAPACITY");
                    const std::array<std::shared_ptr<const BufferBinding>, 2u> repeatedBindings{
                        bound, bound};
                    auto repeatedBudget = resultBudget;
                    repeatedBudget.maxReadbackCommands = repeatedBudget.maxNativeCopyCommands =
                        repeatedBudget.maxResultDeclarations = 2u;
                    is(OriginalCommandBufferReadbackResultContract::inspect(
                           repeatedBindings.data(), 2u, resultVisits, repeatedBudget)
                            .diagnostic,
                        "RHI-ORIGINAL-BUFFER-READBACK-RESULT-COPY-COVERAGE");
                    OriginalCommandReadbackMapContract::Requirements maps;
                    const OriginalCommandReadbackMapContract::Budget mapBudget{
                        1u, true, 1u, 1u, 3u, 9u, 9u};
                    assert(!OriginalCommandReadbackMapContract::deriveRequirements(
                        projectedResults.required,
                        inventory->originalBudget(),
                        5u,
                        5u,
                        mapBudget,
                        maps));
                    assert(maps.maps == 1u && maps.mapCallbacks == 1u && maps.scopeCallbacks == 3u
                        && maps.totalCallbackTickets == 9u
                        && maps.totalCallbackRegistrations == 9u);
                    auto insufficientMaps = mapBudget;
                    --insufficientMaps.maxTotalCallbackRegistrations;
                    OriginalCommandReadbackMapContract::Requirements rejectedMaps;
                    is(OriginalCommandReadbackMapContract::deriveRequirements(
                           projectedResults.required,
                           inventory->originalBudget(),
                           5u,
                           5u,
                           insufficientMaps,
                           rejectedMaps),
                        "RHI-ORIGINAL-READBACK-MAP-SOURCE-CALLBACK-CAPACITY");
                    assert(!OriginalCommandReadbackMapContract::hasRequirements(rejectedMaps));
                    OriginalCommandReadbackMapDrainContract::Facts drain{1u, true};
                    const auto pendingDrain =
                        OriginalCommandReadbackMapDrainContract::inspect(maps, drain);
                    assert(pendingDrain.action
                            == OriginalCommandReadbackMapDrainContract::Action::RetainOriginal
                        && !pendingDrain.mayPublish);
                    drain.nativeError = true;
                    assert(OriginalCommandReadbackMapDrainContract::inspect(maps, drain).action
                        == OriginalCommandReadbackMapDrainContract::Action::InvalidateAndRetain);
                    drain.originalForeignCallbacksDrained = true;
                    assert(OriginalCommandReadbackMapDrainContract::inspect(maps, drain).action
                        == OriginalCommandReadbackMapDrainContract::Action::RetireTerminal);
                    assert(
                        !bufferResult->publishedBytes() && !bufferResult->nativeExecutionProven());
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array<std::shared_ptr<CpuMemoryQuota>, 2u>{graphQuota, resultQuota},
                        [&]() noexcept -> const char* {
                            if (const auto* id = BufferRequest::validateOriginal(bufferRequest))
                                return id;
                            if (const auto* id =
                                    BufferResult::validateOriginalCold(bufferResult, bufferRequest))
                                return id;
                            if (const auto* id = BufferBinding::validateOriginal(bound))
                                return id;
                            return OriginalCommandBufferReadbackResultContract::inspect(
                                readbackBindings.data(),
                                readbackBindings.size(),
                                resultVisits,
                                resultBudget)
                                .diagnostic;
                        }));
                    retiredBufferRequest = bufferRequest;
                    retiredBufferResult = bufferResult;
                    bound.reset();
                    bufferRequest.reset();
                    bufferResult.reset();
                    tx = OriginalCommandStateTransaction::prepareOriginalOwnership(
                        graph, *device, copyState, transactionQuota, stable);
                    assert(tx && !stable && !tx->materializeOriginalPrototypePlan(copySequence));
                    is(BufferBinding::validateOriginalPending(retained, tx, faultCustody),
                        "RHI-ORIGINAL-BUFFER-READBACK-PENDING-STATUS");
                    assert(tx->plannedAccessStepCount() == copySequence.maxSteps);
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array<std::shared_ptr<CpuMemoryQuota>, 2u>{stateQuota, streamQuota},
                        [&]() noexcept {
                            return stream->validate();
                        }));
                    assert(!tx->applyOriginalPrototypePlan());
                    assert(graph->validate() && stream->validate()
                        && BufferBinding::validateOriginal(retained));
                    assert(!BufferBinding::validateOriginalPending(retained, tx, faultCustody));
                    is(BufferBinding::validateOriginalPending(
                           readbackBindings[0], tx, faultCustody),
                        "RHI-ORIGINAL-BUFFER-READBACK-PENDING-VERSION");
                    is(BufferBinding::validateOriginalPending(differentBinding, tx, faultCustody),
                        "RHI-ORIGINAL-BUFFER-READBACK-PENDING-SEQUENCE");
                    auto forgedTransaction = std::shared_ptr<const OriginalCommandStateTransaction>(
                        graphQuota, tx.get());
                    is(BufferBinding::validateOriginalPending(
                           retained, forgedTransaction, faultCustody),
                        "RHI-ORIGINAL-TRANSACTION-OWNER");
                    forgedTransaction.reset();
                    BindlessFaultSubmissionLease absentFault;
                    is(BufferBinding::validateOriginalPending(retained, tx, absentFault),
                        "RHI-ORIGINAL-BUFFER-READBACK-PENDING-FAULT-CUSTODY");
                    assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                        std::array<std::shared_ptr<CpuMemoryQuota>, 3u>{
                            graphQuota, resultQuota, transactionQuota},
                        [&]() noexcept {
                            return BufferBinding::validateOriginalPending(
                                retained, tx, faultCustody);
                        }));
                    const auto priorState = snapshot->faultLatchPriorState();
                    faultCustody
                        .markFaultReadbackCompleted(); // CPU call grants no original state authority.
                    assert(snapshot->faultLatchPriorState() == priorState
                        && tx->status() == OriginalCommandStateTransaction::Status::Pending
                        && !retained->originalResult()->publishedBytes());
                    assert(!tx->abandonOriginalPendingOwnership());
                    is(BufferBinding::validateOriginalPending(retained, tx, faultCustody),
                        "RHI-ORIGINAL-TRANSACTION-ABANDONED");
                    assert(snapshot->originalSubmissionBudgetOwner()->used() == 1u);
                    faultCustody.release();
                    faultCustody.release();
                    assert(snapshot->originalSubmissionBudgetOwner()->used() == 0u);
                    assert(!BindlessFaultSubmissionLease::acquire(snapshot, diagnostic).valid()
                        && diagnostic == "RHI-BINDLESS-FAULT-SUBMISSION-ORIGINAL-JOURNAL"
                        && snapshot->originalSubmissionBudgetOwner()->used() == 0u);
                    faultCustody =
                        BindlessFaultSubmissionLease::acquireOriginalJournal(snapshot, diagnostic);
                    assert(faultCustody.originalJournalOwned());
                    faultCustody.release();
                    assert(graph->validate() && stream->validate());
                    retained.reset();
                    differentBinding.reset();
                    differentStream.reset();
                    tx.reset();
                    stream.reset();
                    graph.reset();
                    copiedInputs.reset();
                }
            }
            pass.reset();
            assert(transactionQuota->liveBytes() == 0u && streamQuota->liveBytes() == 0u
                && stateQuota->liveBytes() == 0u);
        }
    }
    std::weak_ptr<const Root> weakRoot = root;
    std::weak_ptr<const BindlessSnapshot> weakSnapshot = snapshot;
    pmr::Vector<std::weak_ptr<const OriginalCommandDescriptorSetSource>> weakOrdinary;
    for (const auto& domain : ordinaryDomains)
        weakOrdinary.emplace_back(domain.source);
    if (ordinaryMutation != OrdinaryRootMutation::None) {
        assert(!ordinaryDomains.empty());
        const auto expected = ordinaryMutation == OrdinaryRootMutation::DescriptorWrite
            ? "RHI-DESCRIPTOR-CONTENT-CHANGED"
            : "RHI-ORIGINAL-REGISTRATION-ALLOCATION-STALE";
        if (ordinaryMutation == OrdinaryRootMutation::DescriptorWrite)
            assert(mutableOrdinarySets.front()
                    ->write(
                        ordinaryDomains.front().source->frozenElements().front().declaration.write)
                    .success);
        else
            mutableOrdinaryArenas.front()->reset();
        is(CpuQuotaFactoryFixture::verifyHotNoAllocation(std::array{sourceQuota},
               [&]() noexcept {
                   return Root::validateOriginal(root, *device);
               }),
            expected);
        const auto before = sourceQuota->liveBytes();
        assert(!prepare(snapshot, elements, budget));
        is(stable, expected);
        assert(sourceQuota->liveBytes() == before);
    }
    // These fixture vectors were filled while the object quota was active.
    pmr::Vector<DescriptorSet*>().swap(mutableOrdinarySets);
    pmr::Vector<DescriptorArena*>().swap(mutableOrdinaryArenas);
    snapshot.reset();
    prepared = {};
    pipeline.reset();
    compute.reset();
    authorization.reset();
    inputs.clear();
    ordinaryDomains.clear();
    pmr::Vector<Sources::Source>().swap(inputs);
    root.reset();
    foreign.reset();
    assert(!weakRoot.expired() && !weakSnapshot.expired() && snapshots.liveSnapshots() == 1u);
    inventory.reset();
    assert(!weakRoot.expired() && !weakSnapshot.expired());
    for (const auto& owner : weakOrdinary)
        assert(!owner.expired());
    if constexpr (OriginalBindlessLifetimeRetirementFixture<Execute>::value)
        assert(bool(usage->validate()) == execute.originalDeviceLifetimeRetired());
    else
        assert(bool(usage->validate()) == (ordinaryMutation != OrdinaryRootMutation::None));
    device->retireOriginalDeviceLifetime();
    OriginalWorkloadResourceInspectionFixture::inspectRetired(usage->originalSources(), *device);
    const auto* retired = CpuQuotaFactoryFixture::verifyHotNoAllocation(
        std::array{sourceQuota, graphQuota}, [&]() noexcept {
            return usage->validate();
        });
    is(retired, "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
    device.reset();
    assert(usage->validate());
    if constexpr (OriginalBindlessRootInspectionFixture<Execute>::value)
        execute.inspectOriginalRoot(root, inventory, nullptr);
    observe(root, inventory, nullptr);
    usage.reset();
    if (workloadCapacity)
        for (std::size_t i = 0u; i < BindlessWorkloadCapacityPool::domainCount; ++i)
            assert(
                !workloadCapacity->observation(static_cast<BindlessWorkloadCapacityPool::Domain>(i))
                    .used());
    assert(weakRoot.expired() && weakSnapshot.expired());
    for (const auto& owner : weakOrdinary)
        assert(owner.expired());
    weakOrdinary.clear();
    if (retiredBufferRequest) {
        // Request/result metadata survives GPU/device retirement without keeping
        // the original snapshot or descriptor/resource budgets alive.
        assert(!BufferReadbackRequestDeclaration::validateOriginal(retiredBufferRequest));
        if constexpr (Execute::enabled)
            execute.verifyRetiredResult(retiredBufferResult, retiredBufferRequest);
        else {
            assert(!OriginalCommandBufferReadbackResultSnapshot::validateOriginalCold(
                retiredBufferResult, retiredBufferRequest));
            assert(!retiredBufferResult->publishedBytes());
        }
        retiredBufferResult.reset();
        retiredBufferRequest.reset();
    }
    weakRoot.reset();
    weakSnapshot.reset();
    assert(snapshots.liveSnapshots() == 0u && snapshots.chargedBytes() == 0u
        && bindings.liveBindings() == 0u && computeBindings.liveBindings() == 0u
        && sourceQuota->liveBytes() == 0u && graphQuota->liveBytes() == 0u);
    for (const auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
        for (const auto version : {1u, 2u}) {
            const auto lowering =
                semantics::OriginalBufferReadbackBindingLowering::lowering(backend, version);
            assert(lowering.cpuMetadataImplemented
                && lowering.pendingAssociationImplemented == (version == 2u)
                && lowering.validatorOnly == (backend == Backend::Null)
                && !lowering.nativeExecutionImplemented && !lowering.diagnostic);
        }
        const auto geometryBinding =
            semantics::OriginalBufferReadbackBindingLowering::lowering(backend, 3u);
        assert(geometryBinding.cpuMetadataImplemented == (backend != Backend::WebGPU)
            && geometryBinding.pendingAssociationImplemented
                == geometryBinding.cpuMetadataImplemented
            && geometryBinding.validatorOnly == (backend == Backend::Null)
            && !geometryBinding.nativeExecutionImplemented
            && bool(geometryBinding.diagnostic) == (backend == Backend::WebGPU));
        const auto producedBinding =
            semantics::OriginalBufferReadbackBindingLowering::lowering(backend, 4u);
        assert(producedBinding.cpuMetadataImplemented
            && producedBinding.pendingAssociationImplemented
            && !producedBinding.nativeExecutionImplemented
            && producedBinding.validatorOnly == (backend == Backend::Null)
            && !producedBinding.diagnostic);
        const auto controlBinding =
            semantics::OriginalBufferReadbackBindingLowering::lowering(backend, 5u);
        assert(controlBinding.cpuMetadataImplemented && controlBinding.pendingAssociationImplemented
            && !controlBinding.nativeExecutionImplemented && !controlBinding.diagnostic);
        const auto directBinding =
            semantics::OriginalBufferReadbackBindingLowering::lowering(backend, 6u);
        assert(directBinding.cpuMetadataImplemented && directBinding.pendingAssociationImplemented
            && !directBinding.nativeExecutionImplemented && !directBinding.diagnostic);
        assert(!semantics::OriginalBufferReadbackBindingLowering::lowering(backend, 7u)
                .cpuMetadataImplemented);
        const auto bufferReadback = BufferReadbackRequestDeclaration::lowering(backend);
        assert(bufferReadback.cpuMetadataImplemented && !bufferReadback.nativeExecutionImplemented
            && bufferReadback.validatorOnly == (backend == Backend::Null)
            && !bufferReadback.diagnostic);
        assert(!BufferReadbackRequestDeclaration::lowering(backend, 2u).cpuMetadataImplemented);
        const auto decision = semantics::OriginalBindlessRootSourceLowering::lowering(backend);
        assert(decision.sourceAvailable && !decision.executionAvailable
            && decision.mode == CapabilityMode::Equivalent);
        const auto closure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend);
        assert(closure.nativeCorrespondenceAvailable == (backend != Backend::Null)
            && closure.validatorOnly == (backend == Backend::Null) && !closure.executionAvailable
            && bool(closure.diagnostic) == (backend == Backend::Null));
        assert(semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 22u)
                .diagnostic);
        const auto producedClosure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 21u);
        assert(producedClosure.nativeCorrespondenceAvailable == (backend != Backend::Null)
            && !producedClosure.executionAvailable
            && producedClosure.validatorOnly == (backend == Backend::Null)
            && bool(producedClosure.diagnostic) == (backend == Backend::Null));
        const auto storageClosure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 10u);
        assert(storageClosure.nativeCorrespondenceAvailable == (backend != Backend::Null)
            && !storageClosure.executionAvailable
            && storageClosure.validatorOnly == (backend == Backend::Null));
        const auto nativeDepthClosure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 9u);
        assert(nativeDepthClosure.nativeCorrespondenceAvailable
                == (backend == Backend::Vulkan || backend == Backend::Metal)
            && nativeDepthClosure.validatorOnly == (backend == Backend::Null)
            && !nativeDepthClosure.executionAvailable
            && bool(nativeDepthClosure.diagnostic)
                == (backend != Backend::Vulkan && backend != Backend::Metal));
        const auto depthExecutionClosure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 8u);
        assert(depthExecutionClosure.nativeCorrespondenceAvailable == (backend == Backend::WebGPU)
            && depthExecutionClosure.validatorOnly == (backend == Backend::Null)
            && !depthExecutionClosure.executionAvailable
            && bool(depthExecutionClosure.diagnostic) == (backend != Backend::WebGPU));
        const auto arrayClosure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 6u);
        assert(arrayClosure.nativeCorrespondenceAvailable == (backend == Backend::WebGPU)
            && arrayClosure.validatorOnly == (backend == Backend::Null)
            && !arrayClosure.executionAvailable
            && bool(arrayClosure.diagnostic) == (backend != Backend::WebGPU));
        const auto sampledClosure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 5u);
        assert(sampledClosure.nativeCorrespondenceAvailable == (backend == Backend::WebGPU)
            && sampledClosure.validatorOnly == (backend == Backend::Null)
            && !sampledClosure.executionAvailable
            && bool(sampledClosure.diagnostic) == (backend != Backend::WebGPU));
        const auto attachmentClosure =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 4u);
        assert(attachmentClosure.nativeCorrespondenceAvailable == (backend == Backend::WebGPU)
            && attachmentClosure.validatorOnly == (backend == Backend::Null)
            && !attachmentClosure.executionAvailable
            && bool(attachmentClosure.diagnostic) == (backend != Backend::WebGPU));
        const auto initializedGeometry =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 3u);
        assert(initializedGeometry.nativeCorrespondenceAvailable == (backend != Backend::Null)
            && initializedGeometry.validatorOnly == (backend == Backend::Null)
            && !initializedGeometry.executionAvailable
            && bool(initializedGeometry.diagnostic) == (backend == Backend::Null));
        const auto geometry =
            semantics::OriginalBindlessRootNativeClosureLowering::lowering(backend, 2u);
        assert(geometry.nativeCorrespondenceAvailable == (backend == Backend::Vulkan)
            && !geometry.executionAvailable
            && bool(geometry.diagnostic) == (backend != Backend::Vulkan));
        const auto stateDecision =
            semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend);
        assert(stateDecision.projectionAvailable && !stateDecision.executionAvailable
            && stateDecision.validatorOnly == (backend == Backend::Null));
        const auto preparedStateDecision =
            semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend, 2u);
        assert(preparedStateDecision.projectionAvailable
            && !preparedStateDecision.executionAvailable
            && preparedStateDecision.validatorOnly == (backend == Backend::Null));
        const auto faultStateDecision =
            semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend, 3u);
        assert(faultStateDecision.projectionAvailable && !faultStateDecision.executionAvailable);
        const auto copyDecision =
            semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend, 4u);
        assert(copyDecision.projectionAvailable && !copyDecision.executionAvailable);
        const auto geometryState =
            semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend, 5u);
        assert(geometryState.projectionAvailable && !geometryState.executionAvailable
            && geometryState.validatorOnly == (backend == Backend::Null)
            && !geometryState.diagnostic);
        const auto indirectState =
            semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend, 6u);
        assert(indirectState.projectionAvailable && !indirectState.executionAvailable
            && !indirectState.diagnostic);
        assert(!semantics::OriginalCommandStateAdmissionLowering::bindlessRootLowering(backend, 7u)
                .projectionAvailable);
        const auto streamDecision =
            semantics::OriginalCommandStreamLowering::bindlessRootLowering(backend);
        assert(!streamDecision.nativeExecutionAvailable && streamDecision.diagnostic);
        const auto faultStreamDecision =
            semantics::OriginalCommandStreamLowering::bindlessRootLowering(backend, 3u);
        assert(faultStreamDecision.contractVersion == 3u
            && !faultStreamDecision.nativeExecutionAvailable && faultStreamDecision.diagnostic);
        const auto copyStreamDecision =
            semantics::OriginalCommandStreamLowering::bindlessRootLowering(backend, 4u);
        assert(copyStreamDecision.contractVersion == 4u
            && !copyStreamDecision.nativeExecutionAvailable && copyStreamDecision.diagnostic);
    }
    assert(
        !semantics::OriginalBindlessRootSourceLowering::lowering(static_cast<Backend>(UINT32_MAX))
            .sourceAvailable);
    if constexpr (OriginalBindlessCompletedTextureFixture<Execute>::value)
        std::puts(
            "originalBindlessRootSource: authentic completed upload and retained Root; shader execution requires its independent execution observer");
    else
        std::puts(exerciseCpuState
                ? "originalBindlessRootSource: authentic owners, canonical state cells/timeline/journal, exact capacity rollback, cancellation invalidation, hot allocation0, cycle-free retirement; execution evidence belongs to the independent observer"
                : "originalBindlessRootSource: native ownership/declarations retired; execution evidence belongs to the independent observer");
}
} // namespace arp::test
