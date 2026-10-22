#pragma once
#include "3d/vegetation/template.h"
#include "cpu_quota_factory_fixture.h"
#include "original_bindless_graphics_stream_fixture.h"
#include "original_graphics_push_data_fixture.h"
#include "original_graphics_runtime_memory_fixture.h"
#include "original_initialized_geometry_data_fixture.h"
#include "rhi/backends/webgpu/webgpu_original_geometry_content.h"
#include "webgpu_async_wait_fixture.h"
#include "webgpu_native_submission_probe_fixture.h"
#include "webgpu_original_bindless_root_preparation_fixture.h"
#include "webgpu_original_completed_content_fixture.h"

#include <cstring>

namespace arp::test {
struct WebGpuInitializedGeometryBaseRequest : WebGpuOriginalCompletedContentRequest {
    std::uint64_t nativeStagingCeiling = rhi_webgpu::webGpuNativeDeviceLimits().maxBufferSize;
};

using WebGpuInitializedGeometryExecutionRequest =
    OriginalInitializedGeometryDataFixture<rhi_webgpu::WebGpuOriginalGeometryContent,
        WebGpuInitializedGeometryBaseRequest>;

template <class T, class = void> struct OriginalBindlessOrdinaryResultFixture {
    static constexpr std::uint32_t binding = UINT32_MAX;
};

template <class T>
struct OriginalBindlessOrdinaryResultFixture<T, std::void_t<decltype(T::ordinaryResultBinding)>> {
    static constexpr std::uint32_t binding = T::ordinaryResultBinding;
};

template <bool Graphics = false,
    bool Sampled = false,
    bool Array = false,
    bool Depth = false,
    bool Composed = false,
    class GraphicsData = void,
    bool OrdinaryColor = false,
    bool OrdinaryDepth = false,
    bool InitializedGeometry = false>
class WebGpuOriginalBindlessRootExecutionFixtureImpl final
    : public std::conditional_t<InitializedGeometry,
          WebGpuInitializedGeometryExecutionRequest,
          WebGpuOriginalCompletedContentRequest> {
    static void require(const char* id)
    {
        if (id)
            std::fprintf(stderr, "Root execution: %s\n", id);
        assert(!id);
    }

public:
    static_assert(!InitializedGeometry
        || (Graphics && Sampled && !Array && !Depth && !Composed
            && std::is_void_v<GraphicsData> && !OrdinaryColor && !OrdinaryDepth));
    static constexpr bool enabled = true;
    static constexpr bool canonicalCompletion = true;
    static constexpr bool completedTextures = Sampled;
    static constexpr bool tableBufferData = OrdinaryColor;
    static constexpr auto originalRootCpuBytes =
        OriginalBindlessFixtureCpuBudget<GraphicsData>::bytes;

    static constexpr std::uint32_t completedTexturePublicationVersion() noexcept
    {
        return OrdinaryColor ? 6u : 2u;
    }

    static constexpr bool graphicsPush = OriginalBindlessPushFixtureTraits<GraphicsData>::value;
    static constexpr std::uint32_t pushUpdates =
        OriginalBindlessPushUpdateFixtureTraits<GraphicsData>::value;

    template <class Root, class Sources>
    void inspectOriginalRoot(const Root& root, const Sources& sources, rhi::GpuDevice* device) const
    {
        if constexpr (!std::is_void_v<GraphicsData>)
            GraphicsData::inspectOriginalRoot(root, sources, device, fault);
        if constexpr (Composed && Graphics && std::is_void_v<GraphicsData>) {
            OriginalGraphicsRuntimeMemoryFixture::inspect(root, sources, device, fault);
            if (!device)
                return;
            using Source = rhi::OriginalCommandBindlessRootSource;
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{root->originalMetadataQuota()}, [&]() noexcept -> const char* {
                    for (const auto binding : {3u, 4u}) {
                        rhi::ShaderBufferMemoryRequirement requirement;
                        rhi::BindlessResourceAccess access;
                        std::size_t source = 0u;
                        const auto entry = fault ? "faultFragment" : "cleanFragment";
                        assert(Source::originalOrdinaryBufferRequirement(
                                   root, entry, 2u, binding, requirement, access, source, 1u)
                            && !requirement.minimumBytes && !access.resource.isValid()
                            && source == SIZE_MAX);
                        require(Source::originalOrdinaryBufferRequirement(
                            root, entry, 2u, binding, requirement, access, source, 2u));
                        assert(requirement.hasRuntimeArray && requirement.sourceMinimumBytes == 4u
                            && requirement.sourceRuntimeArrayStrideBytes == 4u
                            && requirement.targetRuntimeArrayStrideBytes == 4u
                            && requirement.sourceElementCapacity == 4u && access.bufferSize == 16u
                            && source < sources->sources().size());
                        assert(Source::originalOrdinaryBufferRequirement(
                                   root, entry, 2u, binding, requirement, access, source, 3u)
                            && !requirement.minimumBytes && !access.resource.isValid()
                            && source == SIZE_MAX);
                    }
                    return nullptr;
                }));
            std::puts(
                "BINDLESS_GRAPHICS_RUNTIME_RANGE2_OK elements4 legacy1-refused unknown3-refused hot0");
        }
    }

    static constexpr std::uint32_t executionVersion = OrdinaryDepth ? 14u
        : OrdinaryColor                                             ? 13u
        : Depth                                                     ? 5u
        : Array                                                     ? 4u
        : Sampled                                                   ? 3u
        : Graphics                                                  ? 2u
                                                                    : 1u;
    enum class Scenario {
        Success,
        ColdCancel,
        RecordedCancel,
        RecordingError,
        SubmissionError,
        Reverse,
        PendingCancel,
        CallerDrop,
        ThrowAfter,
        ThrowBefore,
        RetiredLifetime
    };
    bool fault = false;
    bool rejectAlias = false;
    bool records = false;
    bool finiteRanges = false;
    Scenario scenario = Scenario::Success;
    std::shared_ptr<OriginalBindlessPreparedBindingRefusalReceipt> preparedRefusal;

    void observePreparedBindingRefusal(core::StringView diagnostic,
        const std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u>& quotas) const
    {
        assert(Composed && !Graphics && rejectAlias && preparedRefusal);
        assert(diagnostic == "RHI-WEBGPU-BINDLESS-PIPELINE-WRITABLE-BUFFER-ALIAS");
        assert(!preparedRefusal->observed);
        preparedRefusal->quotas = quotas;
        preparedRefusal->observed = true;
    }

    bool originalDeviceLifetimeRetired() const noexcept
    {
        return scenario == Scenario::RetiredLifetime;
    }

    mutable std::array<std::weak_ptr<rhi_webgpu::WebGpuNativeBufferPayload>, 2u> counters;

    void configureRequest(rhi::BindlessRequirements& request) const
    {
        request.originalExecution = {executionVersion, true};
        if constexpr (Composed) {
            assert(request.tables.size() == 1u);
            request.tables[0].resourceCount = rejectAlias ? 1u : 2u;
        }
        if constexpr (!std::is_void_v<GraphicsData>)
            GraphicsData::configureRequest(request);
    }

    void configureDispatch(rhi::CommandScalarArguments::Dispatch& dispatch) const
    {
        if constexpr (Composed)
            dispatch.x = 2u;
    }

    template <bool Enabled = OrdinaryColor, std::enable_if_t<Enabled, int> = 0>
    std::shared_ptr<const rhi::OriginalCommandTextureLease> createCompletedOrdinaryTexture(
        rhi::GpuDevice& device,
        rhi::TextureDesc& desc,
        const std::shared_ptr<core::CpuMemoryQuota>& quota,
        std::uint32_t set,
        const rhi::DescriptorBindingDesc& binding) const
    {
        return GraphicsData::createCompletedOrdinaryTexture(device, desc, quota, set, binding);
    }

    template <bool Enabled = Composed || Sampled, std::enable_if_t<Enabled, int> = 0>
    void configureOrdinaryBuffer(
        rhi::BufferDesc& desc, std::uint32_t set, const rhi::DescriptorBindingDesc& binding) const
    {
        desc.usage = desc.usage | rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
        if constexpr (!std::is_void_v<GraphicsData>)
            GraphicsData::configureOrdinaryBuffer(desc, set, binding);
    }

    template <bool Enabled = Composed || Sampled, std::enable_if_t<Enabled, int> = 0>
    void initializeOrdinaryBuffer(
        rhi::GpuBuffer& buffer, std::uint32_t set, const rhi::DescriptorBindingDesc& binding) const
    {
        if constexpr (Composed || Sampled) {
            if constexpr (!std::is_void_v<GraphicsData>) {
                GraphicsData::initializeOrdinaryBuffer(buffer, set, binding);
                return;
            }
            const auto native = rhi_webgpu::webGpuNativePayloadFor(buffer);
            assert(native && native->buffer);
            std::array<std::uint32_t, 4u> words{};
            if (set == 0u && binding.binding == 3u) {
                const float value = 32.0f;
                std::memcpy(words.data(), &value, sizeof(value));
            } else if (set == 2u && binding.binding == 3u)
                words[0] = 17u;
            else {
                assert(set == 2u && binding.binding == 4u);
                if constexpr (!Graphics)
                    pass_ = native;
            }
            const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
            assert(queue);
            wgpuQueueWriteBuffer(queue, native->buffer, 0u, words.data(), sizeof(words));
            wgpuQueueRelease(queue);
        }
    }

    void configureAdmission(rhi::OriginalCommandAdmissionBudget& budget) const
    {
        WebGpuOriginalBindlessPreparationRequest{}.configureAdmission(budget);
        budget.maxCpuBytes[static_cast<std::size_t>(
            rhi::OriginalCommandAdmissionBudget::CpuDomain::Callback)] = 65536u;
        if constexpr (OrdinaryDepth)
            budget.maxUnpublishedResultBytes = sizeof(std::uint32_t); // One fault result.
        if constexpr (Graphics) {
            budget.maxCommands = budget.maxReferences = 32u;
            budget.maxInputDescriptors = 16u;
            budget.maxStateObservations = 64u;
        }
        if constexpr (Sampled)
            WebGpuOriginalCompletedContentRequest::configureAdmission(budget);
        if constexpr (InitializedGeometry) {
            using Actor = rhi_webgpu::WebGpuOriginalGeometryContent;
            budget.maxGraphOwners += 2u * (Actor::exclusiveCpuQuotaSlots + 1u);
            budget.maxNativePayloadObjects += 2u;
        }
        if constexpr (Sampled && !std::is_void_v<GraphicsData>) {
            // Command descriptors include one transition per sampled input,
            // the attachment transition/pass, four fault barriers and a copy.
            // These are independent from shader physical binding counts.
            budget.maxInputDescriptors = [] {
                if constexpr (OrdinaryColor)
                    return 3u;
                else
                    return GraphicsData::DataModel::sampledTextureCount;
            }() + 7u;
            budget.maxTextureRanges = 2u
                * budget.maxSourceOwners[static_cast<std::size_t>(
                    rhi::CommandReferenceDomain::TextureView)];
            const auto sourceCount = [&](rhi::CommandReferenceDomain domain) {
                return budget.maxSourceOwners[static_cast<std::size_t>(domain)];
            };
            // Complete caller-owned custody ceiling, derived from the typed
            // sources and the existing callback/command contracts. Graph-only
            // owners cannot cover the independently retained native closure,
            // seal, result storage, callback slots and issuer facades.
            const auto views = sourceCount(rhi::CommandReferenceDomain::TextureView);
            // Complete source-side identities are distinct from native handles:
            // each image retains its parent/view and the bounded registration
            // projections visited by the canonical owner union. Keep the
            // original non-image ceiling, as in the native material fixtures.
            constexpr auto imageOwners =
                rhi::OriginalCommandTextureViewSource::completeRetainedOwnerCount
                + 2u * rhi::OriginalCommandGraphicsPipelineSource::cpuOwnerSlots;
            assert(views <= (UINT32_MAX - budget.maxGraphOwners) / imageOwners);
            budget.maxGraphOwners += views * imageOwners;
            budget.maxCommands = budget.maxReferences = std::max(budget.maxCommands, views + 22u);
            const auto native = sourceCount(rhi::CommandReferenceDomain::Buffer)
                + sourceCount(rhi::CommandReferenceDomain::Sampler) + 2u * views + 3u
                + 2u * sourceCount(rhi::CommandReferenceDomain::Descriptor);
            const rhi::OriginalBindlessRootCallbackCustodyRequest custody{1u, true, 1u, 12u};
            constexpr auto coldFacades =
                2u + 1u + 2u + 1u; // closure/quota, admission, stream/quota, result quota
            budget.maxGraphOwners += native
                + rhi::OriginalCommandRecordingSeal::additionalOwnerCount
                + rhi::OriginalCommandLifecycleReservation::additionalOwnerCount + coldFacades
                + rhi::OriginalCommandBufferReadbackBinding::retainedOwnerCount
                + rhi::OriginalCommandReadbackResultStorageContract::fixedAdditionalOwners + 2u
                + rhi::OriginalBindlessRootColdCommandContract::additionalOwners
                + rhi::OriginalBindlessRootCallbackCustodyContract::additionalOwners(custody)
                + rhi::OriginalBindlessRootIssuerContract::additionalOwners;
        }
        if constexpr (graphicsPush) {
            budget.maxPushBytes = pushUpdates * sizeof(OriginalGraphicsPushDataFixture::Block);
            std::uint32_t sourceCeiling = 0u;
            for (const auto ceiling : budget.maxSourceOwners)
                sourceCeiling += ceiling;
            // Fixture ceiling derived from typed source owners, two command
            // objects and the three explicitly owned push native objects.
            budget.maxNativePayloadObjects = 2u * sourceCeiling + 2u
                + rhi_webgpu::WebGpuBindlessRootNativeProgram::pushNativeObjects;
        }
    }

    template <class Table> void configureBuffer(rhi::BufferDesc& desc, const Table& table) const
    {
        desc.sizeBytes = table.bufferLayout.minBindingSizeBytes;
        if constexpr (Graphics) {
            if (records)
                desc.sizeBytes *= table.tableId == 1u ? 1u : 2u;
            else if (table.tableId == 3u)
                desc.sizeBytes *= 3u;
        }
        desc.usage = desc.usage | rhi::ResourceUsage::CopySrc | rhi::ResourceUsage::CopyDst;
        if constexpr (!std::is_void_v<GraphicsData>)
            GraphicsData::configureBuffer(desc, table);
    }

    template <class Table>
    rhi::DescriptorBufferBindingRange originalBufferRange(
        rhi::GpuDevice& device, const Table& table, rhi::BufferDesc& desc) const
    {
        if (!finiteRanges)
            return {};
        const auto range =
            OriginalBindlessBufferRangeFixture{}.originalBufferRange(device, table, desc);
        assert(table.tableId >= 1u && table.tableId <= ranges_.size());
        ranges_[table.tableId - 1u] = range;
        return range;
    }

    template <class Table>
    void initializeBuffer(rhi::GpuBuffer& buffer, const Table& table, std::uint32_t index) const
    {
        if constexpr (!std::is_void_v<GraphicsData>) {
            GraphicsData::initializeBuffer(buffer, table, index);
            return;
        }
        const auto native = rhi_webgpu::webGpuNativePayloadFor(buffer);
        assert(native && native->buffer);
        core::pmr::Vector<std::uint8_t> bytes(buffer.desc().sizeBytes, finiteRanges ? 0xCDu : 0u);
        auto* data = bytes.data();
        if (finiteRanges) {
            const auto range = ranges_[table.tableId - 1u];
            assert(range.contractVersion == 2u);
            data += range.offsetBytes;
            std::memset(data, 0, range.sizeBytes);
        }
        if constexpr (Composed) {
            assert(table.tableId == 1u);
            if constexpr (!Graphics)
                counters[index] = native;
            if constexpr (Graphics) {
                const std::uint32_t value = 777u + index;
                std::memcpy(data, &value, sizeof(value));
            }
        } else if constexpr (Graphics) {
            initializeGraphicsBytes(data, table.tableId, index);
        } else if (table.tableId == 1u) {
            const float value = float(index + 1u);
            std::memcpy(data, &value, sizeof(value));
        } else if (table.tableId == 2u) {
            const std::uint32_t value = 10u + index;
            std::memcpy(data, &value, sizeof(value));
        } else
            counters[index] = native;
        const auto queue = wgpuDeviceGetQueue(native->ownerDevice);
        assert(queue);
        wgpuQueueWriteBuffer(queue, native->buffer, 0u, bytes.data(), buffer.desc().sizeBytes);
        wgpuQueueRelease(queue);
    }

    void operator()(const std::shared_ptr<const rhi::OriginalCommandBindlessRootSource>& root,
        const std::shared_ptr<const rhi::OriginalCommandResourceSources>& inventory,
        rhi::GpuDevice* device) const
    {
        static_assert(Graphics);
        if (!device) {
            assert(!root && !inventory);
            return;
        }
        OriginalBindlessGraphicsStreamFixture::execute(
            root, inventory, *device, *this, nullptr, OrdinaryColor ? 3u : 1u);
    }

    template <class Result, class Request>
    void verifyRetiredResult(const Result& result, const Request& request) const
    {
        assert(core::SharedOwnerProjection::sameOwner(result->originalRequest(), request));
        const bool published =
            !rejectAlias && (scenario == Scenario::Success || scenario == Scenario::Reverse);
        assert(result->nativeExecutionProven() == published);
        if (published) {
            std::uint32_t bits = 0u;
            std::memcpy(&bits, result->publishedBytes(), sizeof(bits));
            assert(bits == (fault ? 1u : 0u));
        } else
            assert(!result->publishedBytes());
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root& root,
        const Inventory& inventory,
        const Binding& binding,
        const Transaction& tx,
        rhi::GpuDevice& device) const
    {
        using namespace rhi_webgpu;
        using Issuer = rhi::OriginalBindlessRootExecution;
        using Probe = WebGpuNativeSubmissionProbeFixture;
        const auto cpu = tx->originalMetadataQuota()->limitBytes();
        using CpuDomain = rhi::OriginalCommandAdmissionBudget::CpuDomain;
        const auto domainBytes = [&](CpuDomain domain) {
            return inventory->originalBudget().maxCpuBytes[static_cast<std::size_t>(domain)];
        };
        const auto recordingCpu = domainBytes(CpuDomain::Recording);
        const auto submissionCpu = domainBytes(CpuDomain::Submission);
        const auto callbackCpu = domainBytes(CpuDomain::Callback);
        auto nativeQuota = core::makeShared<core::CpuMemoryQuota>(cpu);
        auto submission = core::makeShared<core::CpuMemoryQuota>(submissionCpu),
             callback = core::makeShared<core::CpuMemoryQuota>(callbackCpu);
        auto count =
            static_cast<std::uint32_t>(root->elements().size() + 1u + (Graphics ? 1u : 0u));
        for (const auto& domain : root->ordinaryDomains())
            count += domain.source->frozenElements().size();
        if constexpr (InitializedGeometry)
            count += this->initialized->actors.size();
        const auto operations = binding->originalProgram()->operations().size();
        std::uint64_t visits = count + operations;
        for (const auto& op : binding->originalProgram()->operations())
            visits += op.referenceCount + 2u * count;
        rhi::OriginalBindlessRootExecutionBudget budget{{executionVersion, true},
            {OrdinaryDepth          ? 20u
                    : OrdinaryColor ? 18u
                    : Depth         ? 8u
                    : Array         ? 6u
                    : Sampled       ? 5u
                    : Graphics      ? 4u
                                    : 1u,
                true,
                count,
                count + 3u + (Graphics ? 1u : 0u) + (Sampled ? 2u : 0u),
                128u,
                cpu,
                0u,
                0u,
                Graphics ? 1u : 0u},
            {OrdinaryDepth     ? 6u
                    : Depth    ? 3u
                    : Graphics ? 2u
                               : 1u,
                true,
                operations,
                operations + 3u,
                128u,
                cpu,
                {1u, true, 1u, 1u, 1u, 4u, 4u, cpu},
                {1u, true, 1u, 1u, 3u, 12u, 12u}},
            {OrdinaryDepth     ? 20u
                    : Depth    ? 13u
                    : Graphics ? 12u
                               : 11u,
                true,
                1u,
                12u,
                12u,
                128u,
                128u,
                128u,
                recordingCpu,
                submissionCpu,
                callbackCpu},
            {3u, true, 1u, 4u, cpu, 128u},
            {2u,
                true,
                128u,
                cpu,
                {OrdinaryDepth     ? 6u
                        : Depth    ? 3u
                        : Graphics ? 2u
                                   : 1u,
                    true,
                    operations,
                    visits}},
            {1u, true, 1u, 12u, 128u, submissionCpu, callbackCpu},
            {1u, true, 1u, 1u, 128u, recordingCpu, submissionCpu, callbackCpu}};
        budget.requireWorkloadRetention = true;
        if constexpr (InitializedGeometry) {
            budget.closure.initializedGeometryContractVersion = 1u;
            budget.closure.initializedGeometryOptIn = true;
            budget.closure.maxInitializedGeometryActors = this->initialized->actors.size();
            for (const auto& actor : this->initialized->actors)
                budget.closure.maxInitializedGeometryBytes += actor->initializedBytes();
            budget.admission.initializedGeometry = {2u, true, operations, 1u, 1u, 64u, 3u};
            budget.command.program.initializedGeometryContractVersion = 1u;
            budget.command.program.initializedGeometryOptIn = true;
        }
        std::uint64_t pushResidentBytes = 0u;
        if constexpr (graphicsPush) {
            const auto& prepared =
                std::get<std::shared_ptr<const rhi::BindlessPreparedGraphicsBinding>>(
                    root->originalPrepared());
            const auto native = webGpuNativePayloadFor(*prepared->owners()->desc().pipeline);
            assert(native && native->issuedContract());
            auto limits = WGPU_LIMITS_INIT;
            assert(wgpuDeviceGetLimits(
                       static_cast<WGPUDevice>(native->issuedContract()->nativeDevice()), &limits)
                == WGPUStatus_Success);
            std::uint64_t stride = 0u;
            constexpr auto blockBytes = sizeof(OriginalGraphicsPushDataFixture::Block);
            assert(core::roundUpUnsigned(std::uint64_t(blockBytes),
                std::uint64_t(limits.minUniformBufferOffsetAlignment),
                stride));
            pushResidentBytes = (pushUpdates - 1u) * stride + blockBytes;
            assert(stride && stride % limits.minUniformBufferOffsetAlignment == 0u
                && pushResidentBytes <= limits.maxBufferSize
                && blockBytes <= limits.maxUniformBufferBindingSize);
            budget.admission.pushUpload = budget.command.program.pushUpload = {1u,
                true,
                pushUpdates,
                pushUpdates * blockBytes,
                pushResidentBytes,
                rhi_webgpu::WebGpuBindlessRootNativeProgram::pushNativeObjects,
                rhi_webgpu::WebGpuBindlessRootNativeProgram::pushRetainedOwners};
            if constexpr (pushUpdates == 2u)
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PUSH_SLICES_OK updates2 secondOffset%llu resident%llu alignment%u\n",
                    static_cast<unsigned long long>(stride),
                    static_cast<unsigned long long>(pushResidentBytes),
                    limits.minUniformBufferOffsetAlignment);
        }
        if constexpr (Depth || OrdinaryDepth)
            budget.closure.completedDepthInputs = {1u,
                true,
                2u,
                20u,
                rhi::OriginalCompletedDepthTextureSource::completeRetainedOwnerCount};
        if constexpr (Composed || Sampled) {
            budget.closure.maxNativeOwners += 2u * root->ordinaryDomains().size();
            if constexpr (Sampled && !std::is_void_v<GraphicsData>) {
                std::uint32_t sampledParents = 0u;
                for (const auto& element : root->elements())
                    sampledParents +=
                        std::holds_alternative<
                            std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                            element.owner)
                        ? 1u
                        : 0u;
                if constexpr (OrdinaryColor)
                    for (const auto& domain : root->ordinaryDomains())
                        for (const auto& element : domain.source->frozenElements())
                            sampledParents +=
                                std::holds_alternative<
                                    std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                                    element.resourceOwner)
                                ? 1u
                                : 0u;
                // The canonical closure projects a view and parent for every
                // sampled image, two attachment owners, three pipeline/group
                // owners and two owners for each ordinary descriptor group.
                budget.closure.maxNativeOwners =
                    count + sampledParents + 2u + 3u + 2u * root->ordinaryDomains().size();
            }
            const auto owners = inventory->originalBudget().maxGraphOwners;
            budget.closure.maxRetainedOwners = budget.admission.maxRetainedOwners =
                budget.command.maxRetainedOwners = budget.custody.maxRetainedOwners =
                    budget.issuers.maxRetainedOwners = budget.result.maxRetainedOwners = owners;
            budget.lifecycle.maxRecordingOwners = budget.lifecycle.maxSubmissionOwners =
                budget.lifecycle.maxCallbackOwners = owners;
        }
        rhi::OriginalBindlessRootExecutionInputs inputs{
            root, inventory, tx, binding, nativeQuota, submission, callback};
        std::array<std::shared_ptr<const rhi::OriginalInitializedGeometrySource>, 2u> geometry;
        if constexpr (InitializedGeometry) {
            for (std::size_t i = 0u; i < geometry.size(); ++i)
                geometry[i] = this->initialized->actors[i];
            inputs.geometry = geometry.data();
            inputs.geometryCount = geometry.size();
        }
        auto* extension = static_cast<rhi::BindlessCrossExtension*>(
            device.queryInterface(rhi::kExtensionBindlessCross));
        assert(extension);
        core::pmr::String diagnostic;
        auto invalid = budget;
        invalid.intent.prototypeOptIn = false;
        assert(!extension->prepareOriginalRootExecution(device, inputs, invalid, diagnostic)
            && diagnostic == "RHI-ORIGINAL-ROOT-EXECUTION-OPT-IN");
        invalid = budget;
        invalid.lifecycle.contractVersion = 10u;
        assert(!extension->prepareOriginalRootExecution(device, inputs, invalid, diagnostic)
            && diagnostic == "RHI-ORIGINAL-ROOT-EXECUTION-LIFECYCLE");
        rhi::DiagnosticBindlessCrossExtension foreign;
        assert(!foreign.prepareOriginalRootExecution(device, inputs, budget, diagnostic)
            && diagnostic == "RHI-BINDLESS-ROOT-EXECUTION-DEVICE-ISSUER");
        invalid = budget;
        --invalid.closure.maxResourceEntries;
        assert(!extension->prepareOriginalRootExecution(device, inputs, invalid, diagnostic)
            && !diagnostic.empty());
        assert(!nativeQuota->liveBytes() && !submission->liveBytes() && !callback->liveBytes()
            && tx->journalEmpty() && !inventory->originalReservedSubmissionCount());
        if constexpr (InitializedGeometry) {
            const auto rejected = [&](const auto& selectedInputs, const auto& selectedBudget) {
                assert(!extension->prepareOriginalRootExecution(
                           device, selectedInputs, selectedBudget, diagnostic)
                    && !diagnostic.empty());
                assert(!nativeQuota->liveBytes() && !submission->liveBytes()
                    && !callback->liveBytes() && tx->journalEmpty()
                    && !inventory->originalReservedSubmissionCount()
                    && !binding->originalResult()->publishedBytes());
            };
            invalid = budget;
            invalid.closure.initializedGeometryOptIn = false;
            rejected(inputs, invalid);
            invalid = budget;
            --invalid.closure.maxInitializedGeometryBytes;
            rejected(inputs, invalid);
            invalid = budget;
            --invalid.admission.initializedGeometry.maxAnalyzedIndices;
            rejected(inputs, invalid);
            invalid = budget;
            invalid.command.program.initializedGeometryOptIn = false;
            rejected(inputs, invalid);
            auto missing = inputs;
            missing.geometryCount = 0u;
            rejected(missing, budget);
            auto forgedActors = geometry;
            forgedActors[0] = std::shared_ptr<const rhi::OriginalInitializedGeometrySource>(
                nativeQuota, geometry[0].get());
            missing = inputs;
            missing.geometry = forgedActors.data();
            rejected(missing, budget);
            forgedActors[0].reset();
            const char* id = nullptr;
            auto closure = WebGpuBindlessRootNativeClosure::prepareOriginal(root,
                inventory,
                device,
                budget.closure,
                nativeQuota,
                id,
                {this->initialized->actors.data(), this->initialized->actors.size()});
            require(id);
            assert(closure);
            auto exact = budget.closure;
            exact.maxNativeOwners = closure->nativeOwnerCount();
            exact.maxRetainedOwners = closure->retainedOwnerCount();
            closure.reset();
            for (const bool native : {false, true}) {
                invalid = budget;
                invalid.closure = exact;
                if (native)
                    --invalid.closure.maxNativeOwners;
                else
                    --invalid.closure.maxRetainedOwners;
                rejected(inputs, invalid);
            }
        }
        if constexpr (Composed && !graphicsPush) {
            if (rejectAlias) {
                assert(!extension->prepareOriginalRootExecution(device, inputs, budget, diagnostic)
                    && diagnostic == "RHI-WEBGPU-BINDLESS-PIPELINE-WRITABLE-BUFFER-ALIAS");
                assert(!nativeQuota->liveBytes() && !submission->liveBytes()
                    && !callback->liveBytes() && tx->journalEmpty()
                    && !binding->originalResult()->publishedBytes()
                    && !inventory->originalReservedSubmissionCount());
                pass_.reset();
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPOSED_ALIAS_REFUSED_OK before-record no-publication quotas-zero");
                return;
            }
            const char* id = nullptr;
            auto closure = WebGpuBindlessRootNativeClosure::prepareOriginal(
                root, inventory, device, budget.closure, nativeQuota, id);
            assert(closure && !id && closure->resourceEntryCount() == count);
            auto exact = budget.closure;
            exact.maxNativeOwners = closure->nativeOwnerCount();
            exact.maxRetainedOwners = closure->retainedOwnerCount();
            const auto payload =
                webGpuNativePayloadFor(*root->ordinaryDomains().front().source->originalObject());
            auto retained = payload->bindings.front().buffer;
            assert(retained);
            const auto replacement =
                webGpuNativePayloadFor(*root->originalSnapshot()->faultLatchBuffer());
            assert(replacement && replacement.get() != retained.get());
            payload->bindings.front().buffer = replacement;
            assert(closure->validate());
            payload->bindings.front().buffer = retained;
            require(closure->validate());
            closure.reset();
            assert(!nativeQuota->liveBytes());
            closure = WebGpuBindlessRootNativeClosure::prepareOriginal(
                root, inventory, device, exact, nativeQuota, id);
            assert(closure && !id);
            closure.reset();
            auto less = exact;
            --less.maxNativeOwners;
            assert(!WebGpuBindlessRootNativeClosure::prepareOriginal(
                       root, inventory, device, less, nativeQuota, id)
                && core::StringView(id) == "RHI-WEBGPU-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY");
            less = exact;
            --less.maxRetainedOwners;
            assert(!WebGpuBindlessRootNativeClosure::prepareOriginal(
                       root, inventory, device, less, nativeQuota, id)
                && core::StringView(id) == "RHI-WEBGPU-BINDLESS-ROOT-OWNER-CAPACITY");
            assert(!nativeQuota->liveBytes() && tx->journalEmpty());
        }
        const auto allocatedBefore = device.memoryStats().allocatedBytes;
        if constexpr (graphicsPush) {
            const auto refuseUpload = [&](const auto& rejected) {
                assert(
                    !extension->prepareOriginalRootExecution(device, inputs, rejected, diagnostic)
                    && !diagnostic.empty());
                assert(device.memoryStats().allocatedBytes == allocatedBefore
                    && !nativeQuota->liveBytes() && !submission->liveBytes()
                    && !callback->liveBytes() && tx->journalEmpty()
                    && !binding->originalResult()->publishedBytes()
                    && !inventory->originalReservedSubmissionCount());
            };
            for (unsigned field = 0u; field != 6u; ++field) {
                auto rejected = budget;
                auto& upload = rejected.admission.pushUpload;
                switch (field) {
                case 0u:
                    --upload.maxResidentBytes;
                    break;
                case 1u:
                    --upload.maxNativePayloadObjects;
                    break;
                case 2u:
                    --upload.maxRetainedOwners;
                    break;
                case 3u:
                    --upload.maxPayloadBytes;
                    break;
                case 4u:
                    upload.prototypeOptIn = false;
                    break;
                default:
                    --upload.maxUpdates;
                    break;
                }
                rejected.command.program.pushUpload = upload;
                refuseUpload(rejected);
            }
            auto rejected = budget;
            rejected.command.program.pushUpload = {};
            refuseUpload(rejected);
            rejected = budget;
            --rejected.command.program.pushUpload.maxResidentBytes;
            refuseUpload(rejected);
        }
        if constexpr (OrdinaryDepth) {
            require(budget.validate());
            const char* id = nullptr;
            const auto closure = WebGpuBindlessRootNativeClosure::prepareOriginal(
                root, inventory, device, budget.closure, nativeQuota, id);
            require(id);
            assert(closure);
            WebGpuBindlessRootColdAdmission::Requirements required;
            require(WebGpuBindlessRootColdAdmission::deriveRequirements(
                closure, tx, binding, budget.admission, required));
            auto oneLess = inventory->originalBudget();
            assert(oneLess.maxUnpublishedResultBytes == sizeof(std::uint32_t));
            --oneLess.maxUnpublishedResultBytes;
            rhi::OriginalCommandReadbackMapContract::Requirements refused;
            assert(rhi::OriginalCommandReadbackMapContract::deriveRequirements(required.readback,
                oneLess,
                WebGpuBindlessRootColdAdmission::baseCallbackTickets,
                WebGpuBindlessRootColdAdmission::baseCallbackRegistrations,
                budget.admission.readbackMap,
                refused));
            assert(!refused.maps && !refused.totalCallbackTickets
                && !refused.totalCallbackRegistrations);
            std::printf(
                "BINDLESS_WEBGPU_ORDINARY_DEPTH_PREFLIGHT_OK readbacks%llu bytes%llu source-result-cpu%llu\n",
                static_cast<unsigned long long>(required.readback.resultDeclarations),
                static_cast<unsigned long long>(required.readback.uniqueResultBytes),
                static_cast<unsigned long long>(
                    inventory->originalBudget().maxCpuBytes[static_cast<std::size_t>(
                        rhi::OriginalCommandAdmissionBudget::CpuDomain::Result)]));
        }
        if (callbackCpu != cpu) {
            auto mismatched = budget;
            mismatched.lifecycle.maxCallbackCpuBytes = cpu;
            mismatched.custody.maxCpuCallbackBytes = cpu;
            mismatched.issuers.maxCallbackCpuBytes = cpu;
            assert(!extension->prepareOriginalRootExecution(device, inputs, mismatched, diagnostic)
                && diagnostic == "RHI-ORIGINAL-NATIVE-LIFECYCLE-SOURCE-CPU-BUDGET");
            assert(!nativeQuota->liveBytes() && !submission->liveBytes() && !callback->liveBytes()
                && tx->journalEmpty() && !inventory->originalReservedSubmissionCount()
                && !binding->originalResult()->publishedBytes());
            std::printf(
                "BINDLESS_WEBGPU_ROOT_CPU_DOMAINS_OK graph%llu recording%llu submission%llu callback%llu mismatch-refused no-publication\n",
                static_cast<unsigned long long>(cpu),
                static_cast<unsigned long long>(recordingCpu),
                static_cast<unsigned long long>(submissionCpu),
                static_cast<unsigned long long>(callbackCpu));
        }
        auto issuer = extension->prepareOriginalRootExecution(device, inputs, budget, diagnostic);
        if (!issuer)
            std::fprintf(stderr, "Root execution preparation: %s\n", diagnostic.c_str());
        assert(issuer && diagnostic.empty());
        if constexpr (graphicsPush)
            assert(device.memoryStats().allocatedBytes == allocatedBefore + pushResidentBytes);
        auto forged = std::shared_ptr<const Issuer>(callback, issuer.get());
        assert(core::StringView(Issuer::record(forged, {1u, true}))
            == "RHI-BINDLESS-ROOT-EXECUTION-OWNER");
        assert(core::StringView(Issuer::submit(forged, {1u, true}))
            == "RHI-BINDLESS-ROOT-EXECUTION-OWNER");
        forged.reset();
        const auto& result = binding->originalResult();
        const auto snapshot = root->originalSnapshot();
        const auto pipeline = std::visit(
            [](const auto& prepared) -> std::shared_ptr<WebGpuNativePipelineOwnership> {
                return webGpuNativePayloadFor(*prepared->owners()->desc().pipeline);
            },
            root->originalPrepared());
        const auto nativeDevice =
            static_cast<WGPUDevice>(pipeline->issuedContract()->nativeDevice());
        const auto queue = wgpuDeviceGetQueue(nativeDevice);
        assert(queue);
        assert(core::StringView(Issuer::record(issuer, {1u, false}))
            == "RHI-ORIGINAL-ROOT-EXECUTION-OPT-IN");
        assert(Issuer::submit(issuer, {executionVersion, true}) && !result->publishedBytes()
            && tx->journalEmpty());
        const auto quotas =
            std::array{tx->originalMetadataQuota(), nativeQuota, submission, callback};
        if (scenario == Scenario::ColdCancel)
            require(issuer->cancelExecution());
        else {
            if (scenario == Scenario::RecordingError || scenario == Scenario::Reverse)
                Probe::instrumentRecording(nativeDevice, scenario == Scenario::Reverse);
            require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                quotas, [&]() noexcept -> const char* {
                    require(Issuer::record(issuer, {executionVersion, true}));
                    assert(snapshot->originalSubmissionBudgetOwner()->used() == 1u
                        && !result->publishedBytes());
                    assert(WebGpuAsyncWaitFixture::until([&] {
                        return issuer->recordingProven() || issuer->executionDiagnostic();
                    }));
                    return nullptr;
                }));
            if (scenario == Scenario::RecordingError || scenario == Scenario::Reverse)
                Probe::restoreRecording();
            if (scenario == Scenario::RecordingError)
                assert(issuer->executionDiagnostic() && !issuer->pendingRecords());
            else if (scenario == Scenario::RecordedCancel)
                require(issuer->cancelExecution());
            else {
                if (issuer->executionDiagnostic())
                    std::fprintf(stderr, "Root recording: %s\n", issuer->executionDiagnostic());
                assert(issuer->recordingProven()
                    && tx->status() == rhi::OriginalCommandStateTransaction::Status::Pending);
                assert(Issuer::record(issuer, {executionVersion, true}));
                const bool instrumented = scenario == Scenario::SubmissionError
                    || scenario == Scenario::Reverse || scenario == Scenario::ThrowAfter
                    || scenario == Scenario::ThrowBefore;
                if (instrumented)
                    Probe::instrument(nativeDevice,
                        queue,
                        scenario == Scenario::SubmissionError   ? Probe::Probe::NativeError
                            : scenario == Scenario::Reverse     ? Probe::Probe::Reverse
                            : scenario == Scenario::ThrowBefore ? Probe::Probe::ThrowBefore
                                                                : Probe::Probe::ThrowAfter,
                        nullptr,
                        [](void*) noexcept {});
                require(CpuQuotaFactoryFixture::verifyHotNoAllocation(
                    quotas, [&]() noexcept -> const char* {
                        const auto* submitId = Issuer::submit(issuer, {executionVersion, true});
                        if (scenario == Scenario::ThrowAfter || scenario == Scenario::ThrowBefore)
                            assert(submitId);
                        else
                            require(submitId);
                        assert(!result->publishedBytes()
                            && snapshot->originalSubmissionBudgetOwner()->used() == 1u);
                        if (scenario == Scenario::PendingCancel)
                            assert(issuer->cancelExecution());
                        if (scenario == Scenario::RetiredLifetime)
                            device.retireOriginalDeviceLifetime();
                        if constexpr (graphicsPush)
                            assert(device.memoryStats().allocatedBytes
                                == allocatedBefore + pushResidentBytes);
                        if (scenario == Scenario::CallerDrop) {
                            issuer.reset();
                            if constexpr (graphicsPush)
                                assert(device.memoryStats().allocatedBytes
                                    == allocatedBefore + pushResidentBytes);
                        }
                        assert(WebGpuAsyncWaitFixture::until([&] {
                            return snapshot->originalSubmissionBudgetOwner()->used() == 0u;
                        }));
                        return nullptr;
                    }));
                if (instrumented)
                    Probe::restore();
                const bool success = scenario == Scenario::Success || scenario == Scenario::Reverse;
                assert(result->nativeExecutionProven() == success);
                if (success) {
                    assert(issuer->submissionCompleted() && !issuer->executionDiagnostic()
                        && tx->status() == rhi::OriginalCommandStateTransaction::Status::Committed
                        && tx->journalEmpty());
                    if constexpr (Depth)
                        verifyDepth(root, inventory, nativeDevice, queue);
                    else if constexpr (Graphics && !std::is_void_v<GraphicsData>)
                        verifyComposedGraphics(root, inventory, nativeDevice, queue);
                    else if constexpr (Sampled)
                        verifySampledGraphics(root, inventory, nativeDevice, queue);
                    else if constexpr (Graphics)
                        verifyGraphics(root, inventory, nativeDevice, queue);
                    else
                        verifyCounters(nativeDevice, queue);
                } else
                    assert(!result->publishedBytes()
                        && tx->status()
                            == rhi::OriginalCommandStateTransaction::Status::Invalidated);
            }
        }
        assert(snapshot->originalSubmissionBudgetOwner()->used() == 0u
            && bool(result->publishedBytes())
                == (scenario == Scenario::Success || scenario == Scenario::Reverse));
        wgpuQueueRelease(queue);
        issuer.reset();
        if constexpr (graphicsPush)
            assert(device.memoryStats().allocatedBytes == allocatedBefore);
        assert(!submission->liveBytes() && !callback->liveBytes() && !nativeQuota->liveBytes()
            && !inventory->originalReservedSubmissionCount()
            && !inventory->originalReservedCallbackTicketCount()
            && !inventory->originalReservedCallbackRegistrationCount());
        // The ordinary native payload control block was allocated in the
        // original Source quota. The test observer must retire its weak charge
        // as well, before the shared fixture checks that quota returns to zero.
        if constexpr (Composed)
            pass_.reset();
        if constexpr (OrdinaryDepth) {
            std::printf(
                "BINDLESS_WEBGPU_ORDINARY_DEPTH_EXECUTION14_OK scenario%u fault%u Content19 Closure20 Lifecycle20 Pending4 Submission14 same-root-journal genuine-queue-map hot0 publication-gated budgets-returned\n",
                static_cast<unsigned>(scenario),
                fault ? 1u : 0u);
        } else if constexpr (Depth) {
            std::printf(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_DEPTH_EXECUTION_OK scenario%u fault%u public-bindless-cross Content11 lifecycle13 same-root-journal genuine-queue-map hot0 publication-gated budgets-returned\n",
                static_cast<unsigned>(scenario),
                fault ? 1u : 0u);
        } else if constexpr (Sampled) {
            std::printf(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_%s_EXECUTION_OK scenario%u fault%u execution%u Content9 read%u control1 lifecycle12 same-root-journal genuine-draw-queue-map hot0 publication-gated budgets-returned\n",
                []() {
                    if constexpr (!std::is_void_v<GraphicsData>)
                        return GraphicsData::sampledReceipt;
                    else
                        return Array ? "ARRAY" : "SAMPLED";
                }(),
                static_cast<unsigned>(scenario),
                fault ? 1u : 0u,
                executionVersion,
                OrdinaryColor ? 18u : 10u);
        } else if constexpr (Graphics) {
            if constexpr (graphicsPush)
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_%s_EXECUTION_OK scenario%u fault%u Root2 Content9 lifecycle12 hot0 publication-gated budgets-returned\n",
                    pushUpdates == 2u ? "PUSH_MULTI" : "PUSH",
                    static_cast<unsigned>(scenario),
                    fault ? 1u : 0u);
            else if constexpr (!std::is_void_v<GraphicsData>)
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_MATRIX_EXECUTION_OK scenario%u fault%u Root2 Content9 lifecycle12 hot0 publication-gated budgets-returned\n",
                    static_cast<unsigned>(scenario),
                    fault ? 1u : 0u);
            else if constexpr (Composed)
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPOSED_GRAPHICS_EXECUTION_OK scenario%u fault%u Root2 groups3 Content9 lifecycle12 hot0 publication-gated budgets-returned\n",
                    static_cast<unsigned>(scenario),
                    fault ? 1u : 0u);
            else
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_GRAPHICS_EXECUTION_OK records%u scenario%u fault%u public-bindless-cross Content9 lifecycle12 genuine-draw-queue-map hot0 publication-gated budgets-returned\n",
                    records ? 1u : 0u,
                    static_cast<unsigned>(scenario),
                    fault ? 1u : 0u);
            if (finiteRanges)
                std::printf(
                    "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_RANGE_EXECUTION_OK records%u scenario%u fault%u queried-offsets original-range1 same-root-journal fault-publication retirement\n",
                    records ? 1u : 0u,
                    static_cast<unsigned>(scenario),
                    fault ? 1u : 0u);
        } else
            std::printf(Composed
                    ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPOSED_EXECUTION_OK scenario%u fault%u public-bindless-cross Root2 groups3 lifecycle11 hot0 publication-gated budgets-returned\n"
                    : "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_EXECUTION_OK scenario%u fault%u public-bindless-cross original-journal-seal callbacks12 genuine-queue-map hot0 publication-gated budgets-returned\n",
                static_cast<unsigned>(scenario),
                fault ? 1u : 0u);
    }

private:
    mutable std::weak_ptr<rhi_webgpu::WebGpuNativeBufferPayload> pass_;

    template <class Root>
    static std::shared_ptr<rhi_webgpu::WebGpuNativeBufferPayload> ordinaryBuffer(
        const Root& root, std::uint32_t set, std::uint32_t binding)
    {
        std::shared_ptr<rhi_webgpu::WebGpuNativeBufferPayload> result;
        for (const auto& domain : root->ordinaryDomains())
            if (domain.set == set)
                for (const auto& element : domain.source->frozenElements())
                    if (element.declaration.write.binding == binding) {
                        assert(!result);
                        const auto& lease =
                            std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                                element.resourceOwner);
                        result = rhi_webgpu::webGpuNativePayloadFor(*lease->originalObject());
                    }
        return result;
    }

    template <class Root, class Inventory>
    void verifyComposedGraphics(
        const Root& root, const Inventory& inventory, WGPUDevice device, WGPUQueue queue) const
    {
        constexpr std::size_t payloadBytes = [] {
            if constexpr (std::is_void_v<GraphicsData>)
                return std::size_t{24u};
            else
                return GraphicsData::readbackBytes;
        }();
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 512u + payloadBytes;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        const auto readback = wgpuDeviceCreateBuffer(device, &desc);
        assert(readback);
        const auto encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        assert(encoder);
        const auto& tables = root->authorization()->package().artifact().tables;
        const auto& table = [&]() -> const auto& {
            if constexpr (Sampled && !std::is_void_v<GraphicsData>) {
                const auto found =
                    std::find_if(tables.begin(), tables.end(), [](const auto& candidate) {
                        return candidate.firstBinding == GraphicsData::resultBinding;
                    });
                assert(found != tables.end());
                return *found;
            } else
                return tables.front();
        }();
        if constexpr (std::is_void_v<GraphicsData>)
            assert(table.capacity == 2u);
        std::uint32_t copied = 0u;
        std::uint64_t offset = 512u;
        if constexpr (OriginalBindlessOrdinaryResultFixture<GraphicsData>::binding != UINT32_MAX) {
            const auto result = ordinaryBuffer(
                root, 0u, OriginalBindlessOrdinaryResultFixture<GraphicsData>::binding);
            assert(result && result->sizeBytes == payloadBytes);
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, result->buffer, 0u, readback, offset, payloadBytes);
            offset += payloadBytes;
        } else
            for (const auto& element : root->elements()) {
                const auto binding = element.declaration.write.binding;
                if (binding < table.firstBinding || binding - table.firstBinding >= table.capacity)
                    continue;
                ++copied;
                const auto& lease =
                    std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(element.owner);
                const auto native = rhi_webgpu::webGpuNativePayloadFor(*lease->originalObject());
                assert(native);
                const auto bytes = std::is_void_v<GraphicsData> ? 4u : lease->layout().sizeBytes;
                assert(offset + bytes <= desc.size);
                wgpuCommandEncoderCopyBufferToBuffer(
                    encoder, native->buffer, 0u, readback, offset, bytes);
                offset += bytes;
            }
        if constexpr (OriginalBindlessOrdinaryResultFixture<GraphicsData>::binding == UINT32_MAX)
            assert(copied == table.capacity);
        if constexpr (std::is_void_v<GraphicsData>) {
            const auto pass = ordinaryBuffer(root, 2u, 4u);
            assert(pass && pass->sizeBytes == 16u);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, pass->buffer, 0u, readback, offset, 16u);
            offset += 16u;
        }
        assert(offset == desc.size);
        std::shared_ptr<const rhi::OriginalCommandTextureViewSource> target;
        for (const auto& source : inventory->sources())
            if (const auto* view =
                    std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                        &source))
                if ((*view)->originalObject()->desc().usage
                    == rhi::TextureUsage::RenderAttachment) {
                    assert(!target);
                    target = *view;
                }
        assert(target);
        const auto& object = *target->originalParentLease()->originalObject();
        const auto content =
            object.observeOriginalContent(rhi::defaultTextureSubresourceRange(object.desc()));
        assert(content && content->controlVersion() == 5u && content->contentGeneration() == 1u);
        const auto native = rhi_webgpu::webGpuNativePayloadFor(object);
        assert(native && native->texture);
        auto src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        src.texture = native->texture;
        auto dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dst.buffer = readback;
        dst.layout.bytesPerRow = 256u;
        dst.layout.rowsPerImage = 2u;
        WGPUExtent3D extent{2u, 2u, 1u};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
        const auto command = wgpuCommandEncoderFinish(encoder, nullptr);
        assert(command);
        wgpuQueueSubmit(queue, 1u, &command);
        rhi_webgpu::WebGpuNativeBufferMapReceipt receipt;
        bool delivered = false;
        require(receipt.prepareCold(readback,
            desc.size,
            &delivered,
            [](void* p, rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome result) noexcept {
                assert(result == rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome::Success);
                *static_cast<bool*>(p) = true;
            }));
        require(receipt.start());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return delivered;
        }));
        std::array<std::uint8_t, 512u + payloadBytes> actual{};
        require(receipt.readAndUnmap(actual.data(), actual.size()));
        if constexpr (!std::is_void_v<GraphicsData>) {
            GraphicsData::verify(actual, fault);
        } else {
            for (std::uint32_t y = 0u; y < 2u; ++y)
                for (std::uint32_t x = 0u; x < 2u; ++x) {
                    const auto* pixel = actual.data() + y * 256u + x * 4u;
                    assert(pixel[0] == (x ? 0u : 255u) && pixel[1] == (x ? 255u : 0u) && !pixel[2]
                        && pixel[3] == 255u);
                }
            std::array<std::uint32_t, 6u> words{};
            std::memcpy(words.data(), actual.data() + 512u, 24u);
            assert((words == std::array<std::uint32_t, 6u>{777u, 778u, 875u, 876u, 875u, 876u}));
            std::printf("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPOSED_GRAPHICS_PIXELS fault%u rgba",
                fault ? 1u : 0u);
            for (std::uint32_t y = 0u; y < 2u; ++y)
                for (std::uint32_t b = 0u; b < 8u; ++b)
                    std::printf(" %u", static_cast<unsigned>(actual[y * 256u + b]));
            std::printf(" words");
            for (const auto word : words)
                std::printf(" %u", word);
            std::puts("");
        }
        wgpuBufferRelease(readback);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        if constexpr (std::is_void_v<GraphicsData>)
            std::printf(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPOSED_GRAPHICS_NUMERIC_OK fault%u rgba-red-green indexed777-778 pass875-876 vertex-fragment-ordinary original-content5\n",
                fault ? 1u : 0u);
    }

    template <class Root, class Inventory>
    void verifyDepth(
        const Root& root, const Inventory& inventory, WGPUDevice device, WGPUQueue queue) const
    {
        std::shared_ptr<const rhi::OriginalCommandBufferLease> output;
        for (const auto& element : root->elements())
            if (element.declaration.write.binding == 4u)
                output = std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(
                    inventory->sources()[element.declaration.sourceIndex]);
        assert(output && output->layout().sizeBytes == 16u);
        const auto native = rhi_webgpu::webGpuNativePayloadFor(*output->originalObject());
        assert(native);
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 16u;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        const auto readback = wgpuDeviceCreateBuffer(device, &desc);
        assert(readback);
        const auto encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        assert(encoder);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, native->buffer, 0u, readback, 0u, 16u);
        const auto command = wgpuCommandEncoderFinish(encoder, nullptr);
        assert(command);
        wgpuQueueSubmit(queue, 1u, &command);
        rhi_webgpu::WebGpuNativeBufferMapReceipt receipt;
        bool delivered = false;
        require(receipt.prepareCold(readback,
            16u,
            &delivered,
            [](void* p, rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome outcome) noexcept {
                assert(outcome == rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome::Success);
                *static_cast<bool*>(p) = true;
            }));
        require(receipt.start());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return delivered;
        }));
        std::array<std::uint32_t, 4u> actual{};
        require(receipt.readAndUnmap(actual.data(), sizeof(actual)));
        if (fault)
            assert(actual[0] == 0u);
        else {
            const auto& texture = std::get<std::shared_ptr<const rhi::OriginalCommandTextureLease>>(
                inventory->sources()[0]);
            const auto expected = std::array<std::uint32_t, 4u>{0x3e000000u,
                0x3f000000u,
                texture->originalObject()->desc().extent.width,
                texture->originalObject()->desc().extent.height};
            assert(actual == expected);
        }
        wgpuBufferRelease(readback);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_DEPTH_NUMERIC_OK depth-load integer-mip query bounds fault same-content-generations");
    }

    mutable std::array<rhi::DescriptorBufferBindingRange, 3u> ranges_;

    struct Material {
        std::array<float, 4u> color;
        std::array<std::uint32_t, 4u> resources;
    };

    struct Output {
        std::array<float, 4u> color;
        std::array<std::uint32_t, 4u> metadata;
        std::array<std::int32_t, 4u> signedValues;
    };

    static_assert(sizeof(Material) == 32u && offsetof(Material, resources) == 16u);
    static_assert(sizeof(Output) == 48u && offsetof(Output, metadata) == 16u
        && offsetof(Output, signedValues) == 32u);
    static_assert(sizeof(vegetation::InstanceDataGpu) == 64u
        && offsetof(vegetation::InstanceDataGpu, speciesLodState) == 48u);

    static std::array<float, 4u> color(std::uint32_t index)
    {
        return index ? std::array<float, 4u>{.75f, .5f, .25f, 1.f}
                     : std::array<float, 4u>{.25f, .5f, .75f, 1.f};
    }

    void initializeGraphicsBytes(std::uint8_t* data, std::uint32_t table, std::uint32_t index) const
    {
        if (!records) {
            if (table == 1u) {
                const auto value = color(index);
                std::memcpy(data, value.data(), sizeof(value));
            } else {
                const auto value = (table == 2u ? 10u : 100u) * (index + 1u);
                std::memcpy(data, &value, sizeof(value));
            }
        } else if (table == 1u) {
            const Material value{color(index), {index, 5u, 7u, 9u}};
            std::memcpy(data, &value, sizeof(value));
        } else if (table == 2u) {
            for (std::uint32_t element = 0u; element < 2u; ++element) {
                vegetation::InstanceDataGpu value{
                    {float(index + 1u), float(element + 2u), 3.f, 1.f},
                    {.125f, .25f, float(index), float(element)},
                    {5.f, 6.f, 7.f, 8.f},
                    {}};
                vegetation::packInstanceMetadataFloat(
                    value.speciesLodState[0], 0x00040302u + index + element);
                vegetation::packInstanceMetadataFloat(value.speciesLodState[1], 0x87654321u);
                std::memcpy(data + element * sizeof(value), &value, sizeof(value));
            }
        }
    }

    template <class Root, class Inventory>
    void verifySampledGraphics(
        const Root& root, const Inventory& inventory, WGPUDevice device, WGPUQueue queue) const
    {
        assert(root->sourceContractVersion() == 2u && root->ordinaryDomains().size() == 2u);
        std::shared_ptr<const rhi::OriginalCommandTextureViewSource> target;
        for (const auto& source : inventory->sources())
            if (const auto* view =
                    std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                        &source))
                if ((*view)->originalObject()->desc().usage == rhi::TextureUsage::RenderAttachment)
                    target = *view;
        assert(target);
        for (const auto& entry : root->elements())
            if (const auto* view =
                    std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                        &entry.owner)) {
                const auto* texture = (*view)->originalParentLease()->originalObject();
                const auto content = texture->observeOriginalContent();
                assert(content && content->controlVersion() == 1u
                    && content->contentGeneration() == 2u
                    && texture->state() == rhi::ResourceState::ShaderRead);
            }
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = InitializedGeometry ? 512u : 528u;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        const auto readback = wgpuDeviceCreateBuffer(device, &desc);
        assert(readback);
        const auto encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        assert(encoder);
        const auto texture =
            rhi_webgpu::webGpuNativePayloadFor(*target->originalParentLease()->originalObject());
        assert(texture);
        auto src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        src.texture = texture->texture;
        auto dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dst.buffer = readback;
        dst.layout.bytesPerRow = 256u;
        dst.layout.rowsPerImage = 2u;
        WGPUExtent3D extent{2u, 2u, 1u};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
        if constexpr (!InitializedGeometry) {
            const auto pass = ordinaryBuffer(root, 2u, 4u);
            assert(pass && pass->sizeBytes == 16u);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, pass->buffer, 0u, readback, 512u, 16u);
        }
        const auto command = wgpuCommandEncoderFinish(encoder, nullptr);
        assert(command);
        wgpuQueueSubmit(queue, 1u, &command);
        rhi_webgpu::WebGpuNativeBufferMapReceipt receipt;
        bool delivered = false;
        require(receipt.prepareCold(readback,
            desc.size,
            &delivered,
            [](void* p, rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome result) noexcept {
                assert(result == rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome::Success);
                *static_cast<bool*>(p) = true;
            }));
        require(receipt.start());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return delivered;
        }));
        std::array<std::uint8_t, InitializedGeometry ? 512u : 528u> actual{};
        require(receipt.readAndUnmap(actual.data(), actual.size()));
        if constexpr (!InitializedGeometry) {
            std::array<std::uint32_t, 4u> words{};
            std::memcpy(words.data(), actual.data() + 512u, sizeof(words));
            assert((words == std::array<std::uint32_t, 4u>{98u, Array ? 0u : 98u, 98u, 98u}));
            std::printf("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_%s_ORDINARY_WORDS fault%u words",
                Array ? "ARRAY" : "SAMPLED",
                fault ? 1u : 0u);
            for (const auto word : words)
                std::printf(" %u", word);
            std::puts(" Root2 groups3 Vertex-Fragment same-journal");
        }
        std::printf("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_%s_PIXELS fault%u rgba",
            Array ? "ARRAY" : "SAMPLED",
            fault ? 1u : 0u);
        for (std::uint32_t y = 0u; y < 2u; ++y)
            for (std::uint32_t x = 0u; x < 2u; ++x)
                for (std::uint32_t c = 0u; c < 4u; ++c)
                    std::printf(" %u", unsigned(actual[y * 256u + x * 4u + c]));
        std::puts("");
        for (std::uint32_t y = 0u; y < 2u; ++y)
            for (std::uint32_t x = 0u; x < 2u; ++x) {
                const auto* pixel = actual.data() + y * 256u + x * 4u;
                // Discard preserves the explicitly authored opaque-black pass clear.
                const std::array<std::array<std::uint8_t, 4u>, 4u> arrayExpected{
                    {{33u, 45u, 65u, 255u},
                        {0u, 0u, 0u, 255u},
                        {49u, 61u, 69u, 255u},
                        {17u, 23u, 37u, 255u}}};
                const std::array<std::uint8_t, 4u> expected = InitializedGeometry && fault
                    ? std::array<std::uint8_t, 4u>{0u, 0u, 0u, 0u}
                    : Array   ? arrayExpected[y * 2u + x]
                    : x == 0u ? std::array<std::uint8_t, 4u>{33u, 45u, 49u, 255u}
                    : fault   ? std::array<std::uint8_t, 4u>{0u, 0u, 0u, 0u}
                              : std::array<std::uint8_t, 4u>{1u, 7u, 17u, 255u};
                for (std::size_t channel = 0u; channel < 4u; ++channel)
                    assert(pixel[channel] == expected[channel]);
            }
        wgpuBufferRelease(readback);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_%s_NUMERIC_OK fault%u textures2 samplers2 nonuniform load-query-sampleLevel-sampleGrad mip1 read10 same-generation%s\n",
            Array ? "ARRAY" : "SAMPLED",
            fault ? 1u : 0u,
            Array ? " layers2 discard invalid-layer-mip-texture-sampler" : "");
        if constexpr (InitializedGeometry)
            std::printf(
                "BINDLESS_WEBGPU_INITIALIZED_GEOMETRY_NUMERIC_OK rate%s fault%u indexed3 vertexOffset16 indexOffset2 firstInstance1 same-root-journal\n",
                this->instanceRate ? "Instance" : "Vertex",
                fault ? 1u : 0u);
    }

    template <class Root, class Inventory>
    void verifyGraphics(
        const Root& root, const Inventory& inventory, WGPUDevice device, WGPUQueue queue) const
    {
        if constexpr (Composed) {
            verifyComposedGraphics(root, inventory, device, queue);
            return;
        }
        // Independent numerical receipt, separate from the original fault Result.
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 512u;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        for (const auto& entry : root->elements()) {
            if (entry.declaration.write.binding >= 6u)
                continue;
            desc.size +=
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(entry.owner)
                    ->layout()
                    .sizeBytes;
        }
        const auto readback = wgpuDeviceCreateBuffer(device, &desc);
        assert(readback);
        const auto encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        assert(encoder);
        std::array<std::shared_ptr<rhi_webgpu::WebGpuNativeBufferPayload>, 6u> buffers;
        std::size_t count = 0u;
        std::uint64_t offset = 512u;
        for (const auto& entry : root->elements()) {
            if (entry.declaration.write.binding >= 6u)
                continue;
            const auto& lease =
                std::get<std::shared_ptr<const rhi::OriginalCommandBufferLease>>(entry.owner);
            const auto native = rhi_webgpu::webGpuNativePayloadFor(*lease->originalObject());
            assert(native);
            assert(count < buffers.size() && offset + native->sizeBytes <= desc.size);
            buffers[count++] = native;
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, native->buffer, 0u, readback, offset, native->sizeBytes);
            offset += native->sizeBytes;
        }
        assert(count == 6u);
        std::shared_ptr<const rhi::OriginalCommandTextureViewSource> target;
        for (const auto& source : inventory->sources())
            if (const auto* view =
                    std::get_if<std::shared_ptr<const rhi::OriginalCommandTextureViewSource>>(
                        &source))
                if ((*view)->originalObject()->desc().usage == rhi::TextureUsage::RenderAttachment)
                    target = *view;
        assert(target);
        const auto& object = *target->originalParentLease()->originalObject();
        const auto content =
            object.observeOriginalContent(rhi::defaultTextureSubresourceRange(object.desc()));
        assert(content && content->controlVersion() == 5u && content->contentGeneration() == 1u);
        const auto native = rhi_webgpu::webGpuNativePayloadFor(object);
        assert(native && native->texture);
        auto src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        src.texture = native->texture;
        auto dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dst.buffer = readback;
        dst.layout.bytesPerRow = 256u;
        dst.layout.rowsPerImage = 2u;
        WGPUExtent3D extent{2u, 2u, 1u};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &extent);
        const auto command = wgpuCommandEncoderFinish(encoder, nullptr);
        assert(command);
        wgpuQueueSubmit(queue, 1u, &command);
        rhi_webgpu::WebGpuNativeBufferMapReceipt receipt;
        bool delivered = false;
        require(receipt.prepareCold(readback,
            desc.size,
            &delivered,
            [](void* p, rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome result) noexcept {
                assert(result == rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome::Success);
                *static_cast<bool*>(p) = true;
            }));
        require(receipt.start());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return delivered;
        }));
        core::pmr::Vector<std::uint8_t> actual(desc.size);
        require(receipt.readAndUnmap(actual.data(), actual.size()));
        for (std::uint32_t y = 0u; y < 2u; ++y)
            for (std::uint32_t x = 0u; x < 2u; ++x) {
                const auto* pixel = actual.data() + y * 256u + x * 4u;
                assert(pixel[0] == (x ? 191u : 64u) && (pixel[1] == 127u || pixel[1] == 128u)
                    && pixel[2] == (x ? 64u : 191u) && pixel[3] == 255u);
            }
        offset = 512u;
        count = 0u;
        for (const auto& entry : root->elements()) {
            const auto binding = entry.declaration.write.binding;
            if (binding >= 6u)
                continue;
            assert(entry.declaration.write.arrayElement == 0u);
            const auto index = binding % 2u;
            const auto size = buffers[count++]->sizeBytes;
            core::pmr::Vector<std::uint8_t> expected(size, finiteRanges ? 0xCDu : 0u);
            auto* data = expected.data();
            if (finiteRanges) {
                const auto range = entry.declaration.write.bufferBindingRange;
                // Observer and initializer are separate fixture values. Derive
                // this receipt from actual limits and the authorized ABI,
                // rather than borrowing mutable initializer-side state.
                const auto& tables = root->authorization()->package().artifact().tables;
                const auto table =
                    std::find_if(tables.begin(), tables.end(), [&](const auto& value) {
                        return value.tableId == binding / 2u + 1u;
                    });
                assert(table != tables.end());
                const auto limits = rhi_webgpu::webGpuNativeDeviceLimits();
                const auto alignment = binding < 2u ? limits.uniformBufferOffsetAlignment
                                                    : limits.storageBufferOffsetAlignment;
                const auto elements =
                    records ? (binding < 2u ? 1u : 2u) : (binding >= 4u ? 3u : 1u);
                assert(range.contractVersion == 2u
                    && range.offsetBytes
                        == std::max<std::uint64_t>(
                            alignment, table->bufferLayout.elementAlignmentBytes)
                    && range.sizeBytes == table->bufferLayout.minBindingSizeBytes * elements
                    && size
                        == range.offsetBytes + range.sizeBytes
                            + table->bufferLayout.elementAlignmentBytes);
                data += range.offsetBytes;
                std::memset(data, 0, range.sizeBytes);
                assert(snapshotRangeCount(root, binding, index)
                    == range.sizeBytes
                        / (records ? (binding < 2u          ? sizeof(Material)
                                             : binding < 4u ? sizeof(vegetation::InstanceDataGpu)
                                                            : sizeof(Output))
                                   : (binding < 2u ? 16u : 4u)));
            }
            initializeGraphicsBytes(data, binding / 2u + 1u, index);
            if (binding >= 4u) {
                if (!records) {
                    const std::array<std::uint32_t, 3u> value{
                        100u * (index + 1u) + 2u, 10u * (index + 1u), 10u * (index + 1u)};
                    std::memcpy(data, value.data(), sizeof(value));
                } else
                    for (std::uint32_t element = 0u; element < 2u; ++element) {
                        const Output value{color(index),
                            {0x00040302u + index + element, 0x87654321u, 0u, 0u},
                            {-1 - static_cast<std::int32_t>(index),
                                -2 - static_cast<std::int32_t>(element),
                                7,
                                -9}};
                        std::memcpy(data + element * sizeof(value), &value, sizeof(value));
                    }
            }
            assert(!std::memcmp(actual.data() + offset, expected.data(), size));
            offset += size;
        }
        std::printf("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_GRAPHICS_PIXELS records%u fault%u rgba",
            records ? 1u : 0u,
            fault ? 1u : 0u);
        for (std::uint32_t y = 0u; y < 2u; ++y)
            for (std::uint32_t byte = 0u; byte < 8u; ++byte)
                std::printf(" %u", static_cast<unsigned>(actual[y * 256u + byte]));
        std::puts("");
        wgpuBufferRelease(readback);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        std::printf(
            "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_GRAPHICS_NUMERIC_OK records%u fault%u rgba8-2x2 exact-buffers nonuniform fragment-bounds Content5-generation1\n",
            records ? 1u : 0u,
            fault ? 1u : 0u);
        if (finiteRanges)
            std::printf(
                "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_RANGE_NUMERIC_OK records%u fault%u finite-runtime-length exact-view-values outside-canaries-unchanged\n",
                records ? 1u : 0u,
                fault ? 1u : 0u);
    }

    template <class Root>
    static std::uint32_t snapshotRangeCount(
        const Root& root, std::uint32_t binding, std::uint32_t index)
    {
        return root->originalSnapshot()->bufferElementCount(
            root->originalSnapshot()->handle(binding / 2u + 1u, index));
    }

    void verifyCounters(WGPUDevice device, WGPUQueue queue) const
    {
        const auto bytes = Composed ? 24u : 8u;
        auto desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = bytes;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        const auto readback = wgpuDeviceCreateBuffer(device, &desc);
        assert(readback);
        const auto encoder = wgpuDeviceCreateCommandEncoder(device, nullptr);
        assert(encoder);
        for (std::size_t i = 0u; i < counters.size(); ++i) {
            const auto native = counters[Composed && rejectAlias ? 0u : i].lock();
            assert(native);
            if constexpr (!Composed)
                assert(native->sizeBytes == 4u);
            const auto offset = finiteRanges ? ranges_[Composed ? 0u : 2u].offsetBytes : 0u;
            assert(native->sizeBytes >= offset + 4u);
            wgpuCommandEncoderCopyBufferToBuffer(
                encoder, native->buffer, offset, readback, i * 4u, 4u);
        }
        if constexpr (Composed) {
            const auto pass = pass_.lock();
            assert(pass && pass->sizeBytes == 16u);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, pass->buffer, 0u, readback, 8u, 16u);
        }
        const auto command = wgpuCommandEncoderFinish(encoder, nullptr);
        assert(command);
        wgpuQueueSubmit(queue, 1u, &command);
        rhi_webgpu::WebGpuNativeBufferMapReceipt receipt;
        bool delivered = false;
        require(receipt.prepareCold(readback,
            bytes,
            &delivered,
            [](void* p, rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome outcome) noexcept {
                assert(outcome == rhi_webgpu::WebGpuNativeBufferMapReceipt::Outcome::Success);
                *static_cast<bool*>(p) = true;
            }));
        require(receipt.start());
        assert(WebGpuAsyncWaitFixture::until([&] {
            return delivered;
        }));
        std::array<std::uint32_t, 6u> actual{};
        require(receipt.readAndUnmap(actual.data(), bytes));
        if constexpr (Composed)
            assert(actual[0] == 49u && actual[1] == (fault ? 0u : 49u) && actual[2] == 49u
                && actual[3] == (fault ? 50u : 49u) && !actual[4] && !actual[5]);
        else
            assert(actual[0] == 14u && actual[1] == 16u);
        wgpuBufferRelease(readback);
        wgpuCommandBufferRelease(command);
        wgpuCommandEncoderRelease(encoder);
        std::puts(Composed
                ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPOSED_NUMERIC_OK indexed49 pass49-or50 sets0-1-2 repeated-binding3 original-callback-readback"
                : "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_NUMERIC_OK counters14-16 nonuniform-input uniform storage-load-store-atomic bounds fault-readback");
    }
};

using WebGpuOriginalBindlessRootExecutionFixture = WebGpuOriginalBindlessRootExecutionFixtureImpl<>;
using WebGpuOriginalBindlessComposedExecutionFixture =
    WebGpuOriginalBindlessRootExecutionFixtureImpl<false, false, false, false, true>;
using WebGpuOriginalBindlessComposedGraphicsExecutionFixture =
    WebGpuOriginalBindlessRootExecutionFixtureImpl<true, false, false, false, true>;
using WebGpuOriginalBindlessGraphicsExecutionFixture =
    WebGpuOriginalBindlessRootExecutionFixtureImpl<true>;
using WebGpuOriginalBindlessSampledExecutionFixture =
    WebGpuOriginalBindlessRootExecutionFixtureImpl<true, true>;
using WebGpuOriginalBindlessArrayExecutionFixture =
    WebGpuOriginalBindlessRootExecutionFixtureImpl<true, true, true>;
using WebGpuOriginalBindlessInitializedGeometryExecutionFixture =
    WebGpuOriginalBindlessRootExecutionFixtureImpl<true,
        true,
        false,
        false,
        false,
        void,
        false,
        false,
        true>;
using WebGpuOriginalBindlessDepthExecutionFixture =
    WebGpuOriginalBindlessRootExecutionFixtureImpl<false, false, false, true>;
} // namespace arp::test
