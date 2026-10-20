#pragma once

#include "cpu_quota_factory_fixture.h"
#include "creation_receipt_lifecycle_fixture.h"
#include "graphics_shader_interface_merge_fixture.h"
#include "graphics_shader_module_artifact_fixture.h"
#include "original_accepted_compute_pipeline_factory_fixture.h"
#include "original_accepted_graphics_pipeline_factory_fixture.h"
#include "original_artifact_compute_consumption_fixture.h"
#include "original_artifact_compute_creation_fixture.h"
#include "original_artifact_graphics_consumption_fixture.h"
#include "original_artifact_graphics_creation_fixture.h"
#include "original_descriptor_factory_content_fixture.h"
#include "original_descriptor_source_freeze_fixture.h"
#include "original_graphics_pipeline_input_fixture.h"
#include "original_pipeline_factory_registration_fixture.h"
#include "original_pipeline_full_definition_fixture.h"
#include "original_pipeline_metadata_copy_fixture.h"
#include "original_single_entry_pipeline_input_fixture.h"
#include "original_typed_source_assembly_fixture.h"
#include "pipeline_definition_identity_fixture.h"
#include "rhi/frontend/command_usage.h"
#include "rhi/semantics/command_lowering.h"

#include <cassert>
#include <cstdio>
#include <string_view>

namespace arp::test {
class OriginalCommandTypedSourceContractFixture final {
    using Contract = rhi::OriginalCommandTypedSourceContract;
    using Inputs = rhi::OriginalCommandTypedInputContract;
    using Admission = rhi::OriginalCommandAdmissionBudget;
    static constexpr std::uint64_t bytes = 65536u;

    static auto quota() { return core::makeShared<core::CpuMemoryQuota>(bytes); }

    static void is(const char* actual, const char* expected)
    {
        if (!actual || std::string_view(actual) != expected)
            std::fprintf(stderr,
                "typed source expected %s, got %s\n",
                expected,
                actual ? actual : "success");
        assert(actual && std::string_view(actual) == expected);
    }

    static Admission authored()
    {
        Admission result;
        result.contractVersion = 1u;
        result.prototypeOptIn = true;
        result.maxCommands = 16u;
        result.maxReferences = 16u;
        result.maxGraphOwners = 24u;
        result.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Buffer)] = 1u;
        result.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::Texture)] = 1u;
        result.maxSourceOwners[static_cast<std::size_t>(rhi::CommandReferenceDomain::TextureView)] =
            1u;
        for (const auto domain : {Admission::CpuDomain::Source,
                 Admission::CpuDomain::Graph,
                 Admission::CpuDomain::Recording,
                 Admission::CpuDomain::Callback})
            result.maxCpuBytes[static_cast<std::size_t>(domain)] = bytes;
        result.maxTypedNativeCommandCalls = 18u;
        result.maxNativePayloadObjects = 7u;
        result.maxCallbackTickets = 8u;
        result.maxCallbackRegistrations = 8u;
        return result;
    }

    static Contract::Budget budget()
    {
        return {1u, true, 2u, 4u, 3u, 4096u, 4u, 18u, 7u, 8u, 8u, bytes, bytes, bytes, bytes};
    }

public:
    static void verifyContract()
    {
        CreationReceiptLifecycleFixture::verify();
        GraphicsShaderInterfaceMergeFixture::verify();
        OriginalArtifactComputeConsumptionFixture::verifyContract();
        OriginalArtifactGraphicsConsumptionFixture::verifyContract();
        OriginalArtifactComputeCreationFixture::verifyContract();
        OriginalArtifactGraphicsCreationFixture::verifyContract();
        OriginalAcceptedComputePipelineFactoryFixture::verifyContract();
        OriginalAcceptedGraphicsPipelineFactoryFixture::verifyContract();
        OriginalCommandPipelineSourceFixture::verifyContract();
        PipelineDefinitionIdentityFixture::verify();
        auto source = authored();
        auto limits = budget();
        const Contract::Requirements required{2u, 4u, 3u, 4096u, 4u, 18u, 7u, 8u, 8u};
        assert(!Contract::validateRequirements(required, limits, source));
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        const auto reject = [&](auto change, const char* expected) {
            auto b = limits;
            change(b);
            is(Contract::validateRequirements(required, b, source), expected);
        };
        reject(
            [](auto& b) {
                b.contractVersion = 0u;
            },
            "RHI-ORIGINAL-TYPED-SOURCE-VERSION");
        reject(
            [](auto& b) {
                b.prototypeOptIn = false;
            },
            "RHI-ORIGINAL-TYPED-SOURCE-OPT-IN");
        reject(
            [](auto& b) {
                --b.sourceCpuBytes;
            },
            "RHI-ORIGINAL-TYPED-CPU-BUDGET");
        reject(
            [](auto& b) {
                --b.graphCpuBytes;
            },
            "RHI-ORIGINAL-TYPED-CPU-BUDGET");
        reject(
            [](auto& b) {
                --b.recordingCpuBytes;
            },
            "RHI-ORIGINAL-TYPED-CPU-BUDGET");
        reject(
            [](auto& b) {
                --b.callbackCpuBytes;
            },
            "RHI-ORIGINAL-TYPED-CPU-BUDGET");
        reject(
            [](auto& b) {
                --b.maxNativeCommandCalls;
            },
            "RHI-ORIGINAL-TYPED-SOURCE-AUTHORED-NATIVE-BUDGET");
        reject(
            [](auto& b) {
                --b.maxCallbackRegistrations;
            },
            "RHI-ORIGINAL-TYPED-SOURCE-AUTHORED-NATIVE-BUDGET");
        for (auto member : {&Contract::Requirements::pipelineDefinitions,
                 &Contract::Requirements::descriptorElements,
                 &Contract::Requirements::querySlots,
                 &Contract::Requirements::shaderArtifactBytes,
                 &Contract::Requirements::accessDeclarations,
                 &Contract::Requirements::nativeCommandCalls}) {
            auto excess = required;
            ++(excess.*member);
            is(Contract::validateRequirements(excess, limits, source),
                "RHI-ORIGINAL-TYPED-REQUIREMENTS-CAPACITY");
        }
        for (auto member : {&Contract::Requirements::nativePayloadObjects,
                 &Contract::Requirements::callbackTickets,
                 &Contract::Requirements::callbackRegistrations}) {
            auto excess = required;
            ++(excess.*member);
            is(Contract::validateRequirements(excess, limits, source),
                "RHI-ORIGINAL-TYPED-REQUIREMENTS-CAPACITY");
        }
        auto noNative = source;
        noNative.maxTypedNativeCommandCalls = 0u;
        auto cpuOnly = limits;
        cpuOnly.maxNativeCommandCalls = 0u;
        is(Contract::validateRequirements(required, cpuOnly, noNative),
            "RHI-ORIGINAL-TYPED-REQUIREMENTS-CAPACITY");
        assert(core::defaultCpuAllocator().stats().allocationCount == allocations);
        std::array<std::shared_ptr<core::CpuMemoryQuota>, 4u> quotas{
            quota(), quota(), quota(), quota()};
        assert(!Contract::validateQuotas(limits, source, quotas));
        for (std::size_t i = 0u; i < quotas.size(); ++i) {
            const auto original = quotas[i];
            quotas[i].reset();
            is(Contract::validateQuotas(limits, source, quotas), "RHI-ORIGINAL-TYPED-QUOTA");
            quotas[i] = original;
            for (std::size_t j = 0u; j < i; ++j) {
                quotas[i] = quotas[j];
                is(Contract::validateQuotas(limits, source, quotas),
                    "RHI-ORIGINAL-TYPED-QUOTA-DOMAIN");
                quotas[i] = original;
            }
        }
        {
            const auto original = quotas[3];
            quotas[3] = std::shared_ptr<core::CpuMemoryQuota>(original, quotas[0].get());
            is(Contract::validateQuotas(limits, source, quotas), "RHI-ORIGINAL-TYPED-QUOTA-DOMAIN");
            quotas[3] = original;
        }
        for (const auto backend :
            {rhi::Backend::Null, rhi::Backend::Vulkan, rhi::Backend::Metal, rhi::Backend::WebGPU}) {
            const auto decision =
                arp::rhi::semantics::OriginalCommandTypedSourceLowering::lowering(backend);
            assert(decision.mode == rhi::CapabilityMode::DiagnosticFailure
                && !decision.sourceAdapterAvailable && !decision.executionAvailable
                && decision.diagnostic
                && decision.validatorOnly == (backend == rhi::Backend::Null));
        }
        assert(arp::rhi::semantics::OriginalCommandTypedSourceLowering::lowering(
            static_cast<rhi::Backend>(UINT32_MAX))
                .diagnostic);
        assert(Contract::ownership(Contract::Kind::ComputePipeline)
            == rhi::OriginalCommandAdmissionContract::Ownership::SharedOriginal);
        assert(Contract::ownership(Contract::Kind::Descriptor)
            == rhi::OriginalCommandAdmissionContract::Ownership::UniqueObject);
        assert(Contract::ownership(Contract::Kind::DescriptorTable)
            == rhi::OriginalCommandAdmissionContract::Ownership::UniqueObject);
        assert(Contract::ownership(Contract::Kind::Query)
            == rhi::OriginalCommandAdmissionContract::Ownership::PreparedOriginal);
        verifyPipeline();
        verifyFieldsAndQuery();
        verifyGeometryFields();
        OriginalPipelineMetadataCopyFixture::verify();
        OriginalPipelineFactoryRegistrationFixture::verifyLayout();
        OriginalPipelineFullDefinitionFixture::verifyContractAndCopies();
#if defined(ARP_ORIGINAL_ARTIFACT_OWNER_SMOKE_PUBLICATION)
        GraphicsShaderModuleArtifactFixture::verify(
            ARP_ORIGINAL_ARTIFACT_OWNER_SMOKE_PUBLICATION, [](const auto& module) {
                auto vertex = OriginalSingleEntryPipelineInputFixture::verify(module->vertex());
                auto fragment = OriginalSingleEntryPipelineInputFixture::verify(module->fragment());
                auto graphics = OriginalGraphicsPipelineInputFixture::verify(module);
            });
#endif
        OriginalDescriptorFactoryContentFixture::verify([] {
            return rhi::createNullDevice();
        });
        verifyOriginalResources(
            [] {
                return rhi::createNullDevice();
            },
            true);
        verifyOriginalResources(
            [] {
                return rhi::createNullDevice();
            },
            true,
            false,
            true);
        std::puts(
            "BINDLESS_RHI_ORIGINAL_TYPED_SOURCE_CONTRACT_OK version-budget exact-source-native-demand four-independent-quotas shader-entry-reflection descriptor-resource-access query-fields four-backends-closed declaration-not-authority");
    }

    template <class Factory>
    static void verifyOriginalResources(Factory&& create,
        bool freezeDescriptors = false,
        bool scalarDescriptorSources = false,
        bool assembleSources = false)
    {
        auto device = create(), foreign = create();
        assert(device && foreign);
        auto lifetime = quota(), registry = quota(), sourceQuota = quota(),
             foreignLifetime = quota(), foreignRegistry = quota();
        assert(!device->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, lifetime));
        rhi::OriginalResourceRegistrationBudget registration{1u, true, 1u, 1u, 1u, 9u, bytes};
        if (freezeDescriptors) {
            registration.contractVersion = 3u;
            registration.maxDescriptorArenas = 2u;
            registration.maxDescriptorSets = 1u;
            registration.maxDescriptorTables = 1u;
            registration.maxRetainedOwners = 19u;
            if (assembleSources)
                registration.contractVersion = 4u;
        }
        assert(!device->prepareOriginalResourceRegistration(registration, registry));
        assert(!foreign->prepareOriginalDeviceLifetime({1u, true, 1u, 2u, bytes}, foreignLifetime));
        assert(!foreign->prepareOriginalResourceRegistration(
            {1u, true, 1u, 1u, 1u, 9u, bytes}, foreignRegistry));
        rhi::BufferDesc bufferDesc;
        bufferDesc.sizeBytes = 64u;
        bufferDesc.usage = rhi::ResourceUsage::StorageBuffer;
        bufferDesc.initialState = rhi::ResourceState::ShaderWrite;
        auto buffer = device->createBuffer(bufferDesc);
        assert(buffer);
        rhi::TextureDesc textureDesc;
        textureDesc.extent = {2u, 2u, 1u};
        textureDesc.format = rhi::TextureFormat::Rgba8Unorm;
        textureDesc.usage = rhi::TextureUsage::Sampled;
        textureDesc.initialState = rhi::ResourceState::ShaderRead;
        auto texture = device->createTexture(textureDesc);
        assert(texture);
        rhi::TextureViewDesc viewDesc;
        viewDesc.format = textureDesc.format;
        viewDesc.usage = rhi::TextureUsage::Sampled;
        auto view = device->createTextureView(*texture, viewDesc);
        assert(view);
        const char* diagnostic = nullptr;
        const rhi::OriginalCommandResourceLeaseBudget leaseBudget{1u, true, 5u, bytes};
        auto bufferLease = rhi::OriginalCommandBufferLease::prepareOriginal(
            buffer, leaseBudget, sourceQuota, diagnostic);
        auto textureLease = rhi::OriginalCommandTextureLease::prepareOriginal(
            texture, leaseBudget, sourceQuota, diagnostic);
        auto viewLease = rhi::OriginalCommandTextureViewLease::prepareOriginal(
            view, leaseBudget, sourceQuota, diagnostic);
        auto viewSource = rhi::OriginalCommandTextureViewSource::prepareOriginal(
            viewLease, textureLease, leaseBudget, sourceQuota, diagnostic);
        assert(bufferLease && textureLease && viewSource);
        using Sources = rhi::OriginalCommandResourceSources;
        const std::array<Sources::Source, 3u> entries{bufferLease, textureLease, viewSource};
        const auto sourceBudget = authored();
        auto sources = Sources::prepareOriginal(
            entries.data(), entries.size(), sourceBudget, sourceQuota, diagnostic);
        assert(sources && !diagnostic);
        rhi::DescriptorLayoutDesc layout{
            core::pmr::String("typed", &core::defaultCpuMemoryResource()),
            core::pmr::Vector<rhi::DescriptorBindingDesc>(&core::defaultCpuMemoryResource())};
        layout.bindings.push_back(
            {0u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute, 2u});
        Contract::Element original;
        original.write = {0u,
            rhi::DescriptorType::StorageBuffer,
            bufferLease->resource(),
            rhi::DescriptorResourceKind::Buffer,
            rhi::ResourceState::ShaderWrite};
        original.sourceIndex = 0u;
        original.stages = rhi::PipelineStage::ComputeShader;
        original.access = rhi::ResourceAccess::ShaderWrite;
        original.registrationGeneration =
            bufferLease->originalObject()->originalResourceRegistration().generation();
        original.bufferAccessOffset = 4u;
        original.bufferAccessBytes = 16u;
        std::array<Contract::Element, 2u> elements{original, original};
        elements[1].write.arrayElement = 1u;
        assert(!Contract::validateSetElements(
            layout, elements.data(), elements.size(), sources, *device));
        rhi::DescriptorTableDesc table{
            core::pmr::String("images", &core::defaultCpuMemoryResource())};
        table.type = rhi::DescriptorType::SampledTexture;
        table.capacity = 1u;
        table.stages = rhi::ShaderStage::Compute;
        rhi::DescriptorTableWrite write{
            0u, table.type, viewSource->resource(), rhi::DescriptorResourceKind::TextureView};
        const std::size_t index = 2u;
        const auto generation =
            viewSource->originalObject()->originalResourceRegistration().generation();
        if (!freezeDescriptors)
            assert(!CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array{sourceQuota}, [&]() noexcept {
                    return Contract::validateSetElements(
                        layout, elements.data(), elements.size(), sources, *device);
                }));
        const auto rejected = [&](auto change, const char* expected) {
            auto changed = elements;
            change(changed);
            is(Contract::validateSetElements(
                   layout, changed.data(), changed.size(), sources, *device),
                expected);
        };
        rejected(
            [](auto& e) {
                e[1].write.arrayElement = 0u;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENT-DUPLICATE");
        rejected(
            [](auto& e) {
                e[1].write.arrayElement = 2u;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-ELEMENT-SHAPE");
        rejected(
            [](auto& e) {
                e[0].sourceIndex = SIZE_MAX;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-SOURCE-INDEX");
        rejected(
            [](auto& e) {
                ++e[0].write.resource.value;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE");
        rejected(
            [](auto& e) {
                ++e[0].registrationGeneration;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-GENERATION");
        rejected(
            [](auto& e) {
                e[0].bufferAccessOffset = UINT64_MAX;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-BUFFER-RANGE");
        rejected(
            [](auto& e) {
                e[0].bufferAccessBytes = 0u;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-BUFFER-RANGE");
        rejected(
            [](auto& e) {
                e[0].write.resourceKind = rhi::DescriptorResourceKind::TextureView;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE-DOMAIN");
        rejected(
            [](auto& e) {
                e[0].write.physicalLowering.enabled = true;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-LOWERING-UNBOUND");
        rejected(
            [](auto& e) {
                e[0].stages = rhi::PipelineStage::FragmentShader;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-ACCESS-STAGES");
        rejected(
            [](auto& e) {
                e[0].access = rhi::ResourceAccess::TransferWrite;
            },
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-ACCESS-MODE");
        rejected(
            [](auto& e) {
                e[0].write.resourceState = rhi::ResourceState::ShaderRead;
            },
            "RHI-ACCESS-STATE-MISMATCH");
        {
            auto uniformLayout = layout;
            uniformLayout.bindings[0].type = rhi::DescriptorType::UniformBuffer;
            auto uniform = original;
            uniform.write.type = rhi::DescriptorType::UniformBuffer;
            uniform.access = rhi::ResourceAccess::ShaderRead;
            is(Contract::validateSetElements(uniformLayout, &uniform, 1u, sources, *device),
                "RHI-ORIGINAL-TYPED-DESCRIPTOR-RESOURCE-USAGE");
        }
        is(Contract::validateSetElements(
               layout, elements.data(), elements.size(), sources, *foreign),
            "RHI-ORIGINAL-REGISTRATION-FOREIGN-DEVICE");
        auto forged = std::shared_ptr<const Sources>(sourceQuota, sources.get());
        is(Contract::validateSetElements(layout, elements.data(), elements.size(), forged, *device),
            "RHI-ORIGINAL-SOURCES-OWNER");
        assert(!Contract::validateTableElements(
            table, &write, &index, &generation, 1u, sources, *device));
        table.textureViewDimensionConstrained = true;
        table.textureViewDimension = rhi::TextureViewDimension::TextureCube;
        is(Contract::validateTableElements(
               table, &write, &index, &generation, 1u, sources, *device),
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-TEXTURE-DIMENSION");
        table.textureViewDimension = viewDesc.dimension;
        table.depthTexture = true;
        is(Contract::validateTableElements(
               table, &write, &index, &generation, 1u, sources, *device),
            "RHI-ORIGINAL-TYPED-DESCRIPTOR-TEXTURE-DEPTH");
        table.depthTexture = false;
        table.textureViewDimensionConstrained = false;
        table.type = rhi::DescriptorType::StorageBuffer;
        is(Contract::validateTableElements(
               table, &write, &index, &generation, 1u, sources, *device),
            "RHI-ORIGINAL-TYPED-TABLE-ACCESS-UNDECLARED");
        verifySetBinding(*device, layout, bufferLease->resource());
        if (freezeDescriptors) {
            table.type = rhi::DescriptorType::SampledTexture;
            table.capacity = 2u;
            auto freezeLayout = rhi::DescriptorStateCopy::copy(layout);
            auto freezeElements = elements;
            if (scalarDescriptorSources) {
                // Explicit browser fixture: the C ABI has no array payload.
                // Keep its required native failure/revision atomicity observable.
                rhi::DescriptorArenaDesc ad;
                ad.label = "ArrayPayloadClosed";
                ad.maxSets = 1u;
                ad.maxDescriptors = 2u;
                auto arena = core::shareUnique(device->createOriginalDescriptorArena(ad));
                assert(arena);
                auto array = device->allocateOriginalDescriptorSet(
                    arena, {"ArrayPayloadClosed", rhi::DescriptorStateCopy::copy(layout)});
                assert(array);
                assert(array->write(elements[0].write).success);
                const auto revision = array->contentRevision();
                auto failed = array->write(elements[1].write);
                assert(!failed.success);
                assert(failed.diagnostics.find("RHI-DESCRIPTOR-PAYLOAD-CREATION-FAILED")
                    != core::pmr::String::npos);
                assert(array->contentRevision() == revision
                    && !array->hasWrittenBindingElement(
                        elements[1].write.binding, elements[1].write.arrayElement));
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_DESCRIPTOR_ARRAY_PAYLOAD_CLOSED actual-required-payload-failure prior-write-revision-preserved no-array-promotion");
                freezeLayout.bindings[0].arrayCount = 1u;
                auto second = freezeLayout.bindings[0];
                second.binding = 1u;
                freezeLayout.bindings.push_back(second);
                freezeElements[1].write.binding = 1u;
                freezeElements[1].write.arrayElement = 0u;
            }
            if (assembleSources)
                OriginalTypedSourceAssemblyFixture::verify(*device,
                    sourceQuota,
                    entries,
                    sourceBudget,
                    freezeLayout,
                    freezeElements,
                    table,
                    {write, index, generation});
            else
                OriginalDescriptorSourceFreezeFixture::verify(*device,
                    *foreign,
                    sourceQuota,
                    sources,
                    freezeLayout,
                    freezeElements,
                    table,
                    {write, index, generation});
            table.capacity = 1u;
        }
        device->retireOriginalDeviceLifetime();
        is(Contract::validateSetElements(
               layout, elements.data(), elements.size(), sources, *device),
            "RHI-ORIGINAL-DEVICE-LIFETIME-RETIRED");
        assert(!sources->nativeExecutionProven());
        std::puts(
            "BINDLESS_RHI_ORIGINAL_TYPED_RESOURCE_DECLARATION_OK genuine-Null-registration exact-original-resource element-array access-state foreign-forged retired lowering-closed");
    }

private:
    static void verifyGeometryFields()
    {
        using Scalar = rhi::CommandScalarArguments;
        using K = rhi::CommandUsageKind;
        using Input = rhi::CommandVariableArguments::Input;
        core::pmr::Vector<rhi::VertexBufferLayoutDesc> streams;
        rhi::VertexBufferLayoutDesc stream;
        stream.slot = 3u;
        stream.strideBytes = 16u;
        stream.attributes.push_back({0u, 4u, rhi::VertexFormat::Float32x3});
        streams.push_back(stream);
        rhi::VertexInputConsumptionContract vertex(streams);
        assert(!vertex.diagnostic());
        rhi::BufferResourceLayout buffer{32u,
            rhi::ResourceUsage::VertexBuffer | rhi::ResourceUsage::IndexBuffer,
            rhi::MemoryUsage::GpuOnly,
            rhi::ResourceState::Undefined};
        const auto vertexInput = [](std::uint32_t slot, std::uint64_t offset) {
            return Input{
                *Scalar::capture(K::BindVertexBuffer, Scalar::VertexBinding{slot, offset})};
        };
        const auto indexInput = [](rhi::IndexFormat format, std::uint64_t offset) {
            return Input{
                *Scalar::capture(K::BindIndexBuffer, Scalar::IndexBinding{format, offset})};
        };
        const auto allocations = core::defaultCpuAllocator().stats().allocationCount;
        assert(!Inputs::validateVertexBindingShape(vertexInput(3u, 16u), buffer, vertex));
        assert(!Inputs::validateVertexBindingShape(vertexInput(3u, 16u), buffer, streams));
        is(Inputs::validateVertexBindingShape(vertexInput(3u, 17u), buffer, streams),
            "RHI-VERTEX-INPUT-BUFFER-RANGE");
        is(Inputs::validateVertexBindingShape(vertexInput(0u, 0u), buffer, streams),
            "RHI-VERTEX-INPUT-SLOT-UNDECLARED");
        is(Inputs::validateVertexBindingShape(vertexInput(0u, 0u), buffer, vertex),
            "RHI-VERTEX-INPUT-SLOT-UNDECLARED");
        is(Inputs::validateVertexBindingShape(vertexInput(3u, 17u), buffer, vertex),
            "RHI-VERTEX-INPUT-BUFFER-RANGE");
        is(Inputs::validateVertexBindingShape(vertexInput(3u, UINT64_MAX), buffer, vertex),
            "RHI-VERTEX-INPUT-BUFFER-RANGE");
        auto wrongUsage = buffer;
        wrongUsage.usage = rhi::ResourceUsage::IndexBuffer;
        is(Inputs::validateVertexBindingShape(vertexInput(3u, 0u), wrongUsage, vertex),
            "RHI-ORIGINAL-GEOMETRY-VERTEX-USAGE");
        is(Inputs::validateVertexBindingShape(
               indexInput(rhi::IndexFormat::Uint16, 0u), buffer, vertex),
            "RHI-ORIGINAL-GEOMETRY-BINDING-KIND");
        for (const auto format : {rhi::IndexFormat::Uint16, rhi::IndexFormat::Uint32}) {
            const auto width = rhi::IndexInputConsumptionContract::elementBytes(format);
            assert(!Inputs::validateIndexBindingShape(indexInput(format, 32u - width), buffer));
            is(Inputs::validateIndexBindingShape(indexInput(format, 32u), buffer),
                "RHI-ORIGINAL-GEOMETRY-INDEX-RANGE");
            is(Inputs::validateIndexBindingShape(indexInput(format, 1u), buffer),
                "RHI-ORIGINAL-GEOMETRY-INDEX-ALIGNMENT");
            is(Inputs::validateIndexBindingShape(
                   indexInput(format, UINT64_MAX - width + 1u), buffer),
                "RHI-ORIGINAL-GEOMETRY-INDEX-RANGE");
            auto odd = buffer;
            odd.sizeBytes = 31u;
            is(Inputs::validateIndexBindingShape(indexInput(format, 0u), odd),
                "RHI-ORIGINAL-GEOMETRY-INDEX-ALIGNMENT");
        }
        is(Inputs::validateIndexBindingShape(
               indexInput(static_cast<rhi::IndexFormat>(255u), 0u), buffer),
            "RHI-INDEX-INPUT-FORMAT-UNKNOWN");
        wrongUsage.usage = rhi::ResourceUsage::VertexBuffer;
        is(Inputs::validateIndexBindingShape(indexInput(rhi::IndexFormat::Uint16, 0u), wrongUsage),
            "RHI-ORIGINAL-GEOMETRY-INDEX-USAGE");
        is(Inputs::validateIndexBindingShape(vertexInput(3u, 0u), buffer),
            "RHI-ORIGINAL-GEOMETRY-BINDING-KIND");
        auto payload = vertexInput(3u, 0u);
        payload.text = "not-a-binding-field";
        is(Inputs::validateVertexBindingShape(payload, buffer, vertex),
            "RHI-COMMAND-ARGUMENTS-PAYLOAD");
        const std::uint32_t dynamic = 0u;
        payload = indexInput(rhi::IndexFormat::Uint16, 0u);
        payload.offsets = &dynamic;
        payload.offsetCount = 1u;
        is(Inputs::validateIndexBindingShape(payload, buffer), "RHI-COMMAND-ARGUMENTS-PAYLOAD");
        assert(allocations == core::defaultCpuAllocator().stats().allocationCount);
        auto changedStreams = streams;
        changedStreams.push_back(stream);
        is(Inputs::validateVertexBindingShape(vertexInput(3u, 0u), buffer, changedStreams),
            "RHI-VERTEX-INPUT-SLOT-DUPLICATE");
        changedStreams = streams;
        changedStreams[0].strideBytes = 0u;
        is(Inputs::validateVertexBindingShape(vertexInput(3u, 0u), buffer, changedStreams),
            "RHI-VERTEX-INPUT-LAYOUT-INVALID");
        changedStreams = streams;
        changedStreams[0].inputRate = static_cast<rhi::VertexInputRate>(255u);
        is(Inputs::validateVertexBindingShape(vertexInput(3u, 0u), buffer, changedStreams),
            "RHI-VERTEX-INPUT-RATE-UNKNOWN");
        changedStreams = streams;
        changedStreams[0].attributes[0].format = static_cast<rhi::VertexFormat>(255u);
        is(Inputs::validateVertexBindingShape(vertexInput(3u, 0u), buffer, changedStreams),
            "RHI-VERTEX-INPUT-FORMAT-UNKNOWN");
        std::puts(
            "BINDLESS_RHI_GEOMETRY_BINDING_SHAPE_OK vertex-original-slot-fetch index16-32-offset-span overflow-payload-usage-negatives hot0 shape-only state-content-position-native-closed");
    }

    static void verifyPipeline()
    {
        // Authored CPU declarations only, never artifact hydration/native proof.
        rhi::ShaderPackageDesc package;
        package.slangModulePath = "typed.slang";
        package.artifacts = {"typed.spv", "typed.metal", "typed.wgsl", "typed.reflection.yaml"};
        rhi::ShaderEntrypoint entry;
        entry.modulePath = package.slangModulePath;
        entry.entryPoint = "computeMain";
        entry.stage = rhi::ShaderStage::Compute;
        entry.computeWorkgroupSizeX = entry.computeWorkgroupSizeY = entry.computeWorkgroupSizeZ =
            1u;
        package.entrypoints.push_back(entry);
        auto& reflection = package.reflection;
        reflection.computeWorkgroupSizeX = reflection.computeWorkgroupSizeY =
            reflection.computeWorkgroupSizeZ = 1u;
        rhi::PipelineLayoutDesc layout;
        assert(!Contract::validatePipelineDeclaration(
            layout, package, &entry, 1u, Contract::Kind::ComputePipeline));
        package.generatedReflectionHydrated = true; // Public label never opens lowering.
        assert(
            !arp::rhi::semantics::OriginalCommandTypedSourceLowering::lowering(rhi::Backend::WebGPU)
                .sourceAdapterAvailable);
        --reflection.computeWorkgroupSizeX;
        is(Contract::validatePipelineDeclaration(
               layout, package, &entry, 1u, Contract::Kind::ComputePipeline),
            "RHI-ORIGINAL-TYPED-WORKGROUP-REFLECTION");
        ++reflection.computeWorkgroupSizeX;
        entry.entryPoint = "other";
        is(Contract::validatePipelineDeclaration(
               layout, package, &entry, 1u, Contract::Kind::ComputePipeline),
            "RHI-ORIGINAL-TYPED-PIPELINE-ENTRY");
        entry = package.entrypoints[0];
        reflection.descriptorBindings.push_back(
            {0u, 0u, rhi::DescriptorType::StorageBuffer, rhi::ShaderStage::Compute, 1u});
        is(Contract::validatePipelineDeclaration(
               layout, package, &entry, 1u, Contract::Kind::ComputePipeline),
            "RHI-ORIGINAL-TYPED-PIPELINE-REFLECTION");
        reflection.descriptorBindings.clear();
        is(Contract::validatePipelineDeclaration(
               layout, package, &entry, 1u, Contract::Kind::GraphicsPipeline),
            "RHI-ORIGINAL-TYPED-PIPELINE-STAGES");
    }

    static void verifyFieldsAndQuery()
    {
        using K = rhi::CommandUsageKind;
        using S = rhi::CommandScalarArguments;
        using D = rhi::CommandReferenceDomain;
        const D pipeline = D::Pipeline, descriptor = D::Descriptor, query = D::Query;
        const rhi::CommandVariableArguments::Input bind{
            *S::capture(K::BindComputePipeline, S::None{}), {}};
        assert(!Inputs::validateDeclaredFields(bind, &pipeline, 1u));
        is(Inputs::validateDeclaredFields(bind, &descriptor, 1u),
            "RHI-ORIGINAL-TYPED-INPUT-REFERENCES");
        is(Inputs::validateDeclaredFields(bind, nullptr, 1u),
            "RHI-ORIGINAL-TYPED-INPUT-REFERENCES");
        is(Inputs::validatePipelineKind(K::BindComputePipeline, Contract::Kind::GraphicsPipeline),
            "RHI-ORIGINAL-TYPED-INPUT-PIPELINE-KIND");
        assert(!Inputs::validatePipelineKind(
            K::BindGraphicsPipeline, Contract::Kind::GraphicsPipeline));
        for (const auto& scalar : {*S::capture(K::Dispatch, S::Dispatch{2u, 3u, 4u}),
                 *S::capture(K::Draw, S::Draw{3u, 2u, 1u, 7u}),
                 *S::capture(K::DrawIndexed, S::DrawIndexed{3u, 2u, 1u, -5, 7u})}) {
            const rhi::CommandVariableArguments::Input input{scalar, {}};
            assert(!Inputs::validateDeclaredFields(input, nullptr, 0u));
            is(Inputs::validateDeclaredFields(input, &pipeline, 1u),
                "RHI-ORIGINAL-TYPED-INPUT-REFERENCES");
        }
        rhi::QueryPoolDesc pool;
        pool.type = rhi::QueryType::Timestamp;
        pool.count = 3u;
        auto meta = quota(), label = quota(), scopes = quota();
        const char* diagnostic = nullptr;
        auto snapshot = rhi::QueryPoolInputSnapshot::prepareOriginal(
            pool, {1u, true, 0u, 0u, bytes, bytes, bytes}, meta, label, scopes, diagnostic);
        assert(snapshot && !diagnostic);
        const auto declaration =
            rhi::QueryPoolOriginalInputArguments::captureOriginal(K::Timestamp, 2u, snapshot);
        assert(declaration);
        const rhi::CommandVariableArguments::Input input{declaration->scalar(), {}};
        assert(!Inputs::validateDeclaredFields(input, &query, 1u)
            && !Inputs::validateQueryBinding(input, *declaration));
        const rhi::CommandVariableArguments::Input changed{
            *S::capture(K::Timestamp, S::Query{1u}), {}};
        is(Inputs::validateQueryBinding(changed, *declaration),
            "RHI-ORIGINAL-TYPED-INPUT-QUERY-FIELDS");
        const auto outside =
            rhi::QueryPoolOriginalInputArguments::captureOriginal(K::Timestamp, 3u, snapshot);
        assert(outside);
        is(Contract::validateQueryDeclaration(*outside), "RHI-QUERY-COMMAND-INDEX");
    }

    static void verifySetBinding(
        rhi::GpuDevice& device, rhi::DescriptorLayoutDesc layout, rhi::GpuResourceId resource)
    {
        using K = rhi::CommandUsageKind;
        using S = rhi::CommandScalarArguments;
        layout.bindings[0].arrayCount = 1u;
        layout.bindings[0].dynamicOffset = true;
        rhi::DescriptorArenaDesc arenaDesc;
        arenaDesc.maxSets = 1u;
        arenaDesc.maxDescriptors = 1u;
        auto arena = device.createDescriptorArena(arenaDesc);
        assert(arena);
        rhi::DescriptorSetDesc setDesc;
        setDesc.layout = layout;
        auto set = arena->allocateDescriptorSet(setDesc);
        assert(set);
        assert(
            set->write({0u,
                           rhi::DescriptorType::StorageBuffer,
                           resource,
                           rhi::DescriptorResourceKind::Buffer,
                           rhi::ResourceState::ShaderWrite})
                .success);
        rhi::PipelineLayoutDesc pipeline;
        pipeline.descriptorLayouts.push_back(layout);
        pipeline.descriptorSets.push_back({0u, layout.name});
        const std::uint32_t offset = 0u;
        rhi::CommandVariableArguments::Input input{
            *S::capture(K::BindComputeDescriptor, S::DescriptorBinding{0u}), {}, &offset, 1u};
        assert(!Inputs::validateSetBinding(input, pipeline, *set));
        input.offsetCount = 0u;
        is(Inputs::validateSetBinding(input, pipeline, *set),
            "RHI-ORIGINAL-TYPED-INPUT-DYNAMIC-OFFSETS");
        input.offsetCount = 1u;
        arena->reset();
        is(Inputs::validateSetBinding(input, pipeline, *set),
            "RHI-ORIGINAL-TYPED-INPUT-DESCRIPTOR-LAYOUT");
    }
};
} // namespace arp::test
