#include "rhi/backends/vulkan/vulkan_backend_objects.h"
#include "rhi/backends/vulkan/vulkan_bindless.h"
#include "rhi/backends/vulkan/vulkan_bindless_root.h"
#include "rhi/backends/vulkan/vulkan_native_commands.h"
#include "rhi/backends/vulkan/vulkan_original_bindless_command.h"
#include "rhi/frontend/bindless_compute_binding.h"
#include "rhi/frontend/bindless_graphics_binding.h"
#include "rhi/frontend/bindless_shader_package.h"
#include "rhi/frontend/bindless_snapshot.h"
#include "rhi/frontend/command_indirect_commands.h"
#include "rhi/gpu_bindless.h"
#include "rhi/gpu_extensions.h"
#include "tests/fixtures/original_bindless_root_source_fixture.h"
#include "tests/fixtures/original_graphics_runtime_memory_fixture.h"
#include "tests/fixtures/original_storage_root_closure_fixture.h"
#include "tests/fixtures/original_storage_texture_creation_fixture.h"
#include "tests/fixtures/renderdoc_capture.h"
#include "tests/fixtures/vulkan_completed_texture_root_fixture.h"
#include "tests/fixtures/vulkan_headless_runtime.h"
#include "tests/fixtures/vulkan_native_result_loss_fixture.h"
#include "tests/fixtures/vulkan_original_bindless_geometry_fixture.h"
#include "tests/fixtures/vulkan_original_bindless_graphics_content_fixture.h"
#include "tests/fixtures/vulkan_original_completed_depth_publication_fixture.h"
#include "tests/fixtures/vulkan_original_storage_execution_fixture.h"
#include "tests/fixtures/vulkan_original_texture_creation_fixture.h"
#include "tests/fixtures/vulkan_original_texture_upload_fixture.h"
#include "tests/fixtures/vulkan_public_completed_texture_root_fixture.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

using RootFenceNegativeObservation = arp::test::RootFenceNegativeObservation;

struct VulkanRootFenceFixture {
    static constexpr bool enabled = true;
    bool submit = true;
    std::uint32_t expectedFault = 0u;
    bool uncertainSubmit = false;
    mutable std::array<std::shared_ptr<arp::rhi_vulkan::VulkanRhiNativeResourcePayload>, 2u>
        counters;
    bool deviceIdleDrain = false;
    bool rejectInvocation = false;
    std::array<std::uint32_t, 2u> expectedCounters{14u, 16u};

    template <class Result, class Request>
    void verifyRetiredResult(const Result& result, const Request& request) const
    {
        assert(result->nativeExecutionProven() == (submit && !uncertainSubmit));
        assert(arp::core::SharedOwnerProjection::sameOwner(result->originalRequest(), request));
        if (submit && !uncertainSubmit) {
            std::uint32_t bits = 0u;
            std::memcpy(&bits, result->publishedBytes(), sizeof(bits));
            assert(bits == expectedFault);
        } else
            assert(!arp::rhi::OriginalCommandBufferReadbackResultSnapshot::validateOriginalCold(
                result, request));
    }

    template <class Table>
    void configureBuffer(arp::rhi::BufferDesc& desc, const Table& table) const
    {
        desc.memory = arp::rhi::MemoryUsage::CpuToGpu;
        // faultMain authors element 1 as out of range. Admit exactly one ABI
        // element per buffer so that the intended negative operand stays invalid.
        desc.sizeBytes = table.bufferLayout.minBindingSizeBytes;
    }

    template <class Table>
    void initializeBuffer(
        arp::rhi::GpuBuffer& buffer, const Table& table, std::uint32_t index) const
    {
        auto native = arp::rhi_vulkan::vulkanRhiNativePayload(buffer);
        assert(native && native->backendMappedData());
        std::memset(
            native->backendMappedData(), 0, static_cast<std::size_t>(buffer.desc().sizeBytes));
        // IDs are authored by bindless_metal_compute.yaml, not backend heuristics.
        if (table.tableId == 1u) {
            const float value = float(index + 1u);
            std::memcpy(native->backendMappedData(), &value, sizeof(value));
        } else if (table.tableId == 2u) {
            const std::uint32_t value = 10u + index;
            std::memcpy(native->backendMappedData(), &value, sizeof(value));
        } else
            counters[index] = native;
        arp::core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, buffer.desc().sizeBytes, diagnostic));
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root& root,
        const Inventory& inventory,
        const Binding& binding,
        const Transaction& tx,
        arp::rhi::GpuDevice& device) const
    {
        using namespace arp;
        using namespace arp::rhi;
        using namespace arp::rhi_vulkan;
        constexpr std::uint64_t cpuBytes = 65536u;
        auto nativeQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        auto submissionQuota = core::makeShared<core::CpuMemoryQuota>(cpuBytes);
        const char* id = nullptr;
        auto count = static_cast<std::uint32_t>(root->elements().size() + 1u);
        for (const auto& domain : root->ordinaryDomains())
            count += domain.source->frozenElements().size();
        const OriginalBindlessRootNativeClosureBudget closureBudget{
            1u, true, count, count * 2u + 2u, 512u, cpuBytes};
        if (!root->ordinaryDomains().empty()) {
            auto shortBudget = closureBudget;
            shortBudget.maxResourceEntries = count - 1u;
            assert(!VulkanBindlessRootNativeClosure::prepareOriginal(
                root, inventory, device, shortBudget, nativeQuota, id));
            assert(std::string_view(id) == "RHI-VULKAN-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY"
                && !nativeQuota->liveBytes());
            core::CpuMemoryQuotaLease pressure(nativeQuota, cpuBytes);
            assert(pressure.charged());
            assert(!VulkanBindlessRootNativeClosure::prepareOriginal(
                root, inventory, device, closureBudget, nativeQuota, id));
            assert(nativeQuota->liveBytes() == cpuBytes
                && tx->status() == OriginalCommandStateTransaction::Status::Prepared);
        }
        auto closure = VulkanBindlessRootNativeClosure::prepareOriginal(
            root, inventory, device, closureBudget, nativeQuota, id);
        if (!closure)
            std::cerr << "root fence closure: " << id << '\n';
        assert(closure && !id);
        if (!root->ordinaryDomains().empty()) {
            assert(closure->resourceEntryCount() == count);
            const auto charged = nativeQuota->liveBytes();
            for (const auto nativeOwners : {true, false}) {
                auto shortBudget = closureBudget;
                if (nativeOwners)
                    shortBudget.maxNativeOwners = closure->nativeOwnerCount() - 1u;
                else
                    shortBudget.maxRetainedOwners = closure->retainedOwnerCount() - 1u;
                assert(!VulkanBindlessRootNativeClosure::prepareOriginal(
                    root, inventory, device, shortBudget, nativeQuota, id));
                assert(std::string_view(id)
                    == (nativeOwners ? "RHI-VULKAN-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY"
                                     : "RHI-VULKAN-BINDLESS-ROOT-OWNER-CAPACITY"));
                assert(nativeQuota->liveBytes() == charged
                    && tx->status() == OriginalCommandStateTransaction::Status::Prepared);
            }
        }
        OriginalCommandNativeLifecycleBudget budget{
            6u, true, 1u, 0u, 0u, tx->retainedOwnerCount() + 2u, 512u, 0u, cpuBytes, cpuBytes, 0u};
        auto seal = OriginalCommandRecordingSeal::prepareOriginal(
            tx, device, budget, submissionQuota, {}, id);
        if (!seal)
            std::cerr << "root fence seal: " << id << '\n';
        assert(seal && !id && seal->retainedOwnerCount() == tx->retainedOwnerCount() + 2u);
        auto reservation =
            OriginalCommandLifecycleReservation::prepareOriginal(seal, {2u, true, 1u, 0u, 0u}, id);
        if (!reservation)
            std::cerr << "root fence reservation: " << id << '\n';
        assert(reservation && !id);
        core::pmr::String diagnostic;
        diagnostic.reserve(192u); // Failure diagnostic storage is cold too.
        auto forged = std::shared_ptr<const OriginalCommandLifecycleReservation>(
            submissionQuota, reservation.get());
        assert(!VulkanOriginalBindlessCommand::prepareOriginal(
            closure, forged, binding, device, diagnostic));
        forged.reset();
        {
            core::CpuMemoryQuotaLease pressure(
                submissionQuota, cpuBytes - submissionQuota->liveBytes());
            assert(pressure.charged());
            assert(!VulkanOriginalBindlessCommand::prepareOriginal(
                closure, reservation, binding, device, diagnostic));
            assert(tx->status() == OriginalCommandStateTransaction::Status::Prepared
                && root->originalSnapshot()->originalSubmissionBudgetOwner()->used() == 0u);
        }
        const auto beforeRecordingBytes = submissionQuota->liveBytes();
        auto command = VulkanOriginalBindlessCommand::prepareOriginal(
            closure, reservation, binding, device, diagnostic);
        if (!command)
            std::cerr << "root fence command: " << diagnostic << '\n';
        assert(command && tx->status() == OriginalCommandStateTransaction::Status::Pending);
        assert(!binding->originalResult()->publishedBytes());
        assert(tx->applyOriginalPrototypePlan()); // Caller cannot borrow seal authority.
        assert(!VulkanOriginalBindlessCommand::prepareOriginal(
            closure, reservation, binding, device, diagnostic));
        assert(root->originalSnapshot()->originalSubmissionBudgetOwner()->used() == 1u);
        std::weak_ptr<const OriginalCommandRecordingSeal> weakSeal = seal;
        if (submit) {
            const std::array hotQuotas{submissionQuota,
                nativeQuota,
                tx->originalMetadataQuota(),
                binding->originalMetadataQuota(),
                binding->originalResult()->originalMetadataQuota()};
            arp::test::VulkanOriginalRootTerminalFixture::verify(
                {uncertainSubmit, deviceIdleDrain, rejectInvocation, false},
                command,
                closure,
                seal,
                reservation,
                binding,
                tx,
                root,
                device,
                hotQuotas,
                diagnostic,
                [] {});
            if (uncertainSubmit) {
                assert(tx->validate() && !binding->originalResult()->publishedBytes());
            } else {
                assert(tx->status() == OriginalCommandStateTransaction::Status::Committed);
                const auto* result = binding->originalResult()->publishedBytes();
                assert(result && binding->originalResult()->nativeExecutionProven());
                std::uint32_t bits = 0u;
                std::memcpy(&bits, result, sizeof(bits));
                assert(bits == expectedFault);
                for (std::uint32_t i = 0u; i < counters.size(); ++i) {
                    if (!counters[i])
                        continue;
                    assert(counters[i]->backendInvalidateMappedRead(
                        0u, sizeof(std::uint32_t), diagnostic));
                    std::uint32_t actual = 0u;
                    std::memcpy(&actual, counters[i]->backendMappedData(), sizeof(actual));
                    std::cout << "canonical buffer " << i << ": " << actual << std::endl;
                    assert(actual == expectedCounters[i]);
                }
            }
            assert(!command->submit(diagnostic));
            retireCompletedVulkanRhiSubmittedCommands();
        }
        command.reset();
        assert(root->originalSnapshot()->originalSubmissionBudgetOwner()->used() == 0u);
        if (!submit) {
            assert(tx->status() == OriginalCommandStateTransaction::Status::Abandoned);
            assert(!binding->originalResult()->publishedBytes());
        }
        assert(submissionQuota->liveBytes() == (submit ? 0u : beforeRecordingBytes));
        reservation.reset();
        seal.reset();
        closure.reset();
        assert(weakSeal.expired() && submissionQuota->liveBytes() == 0u
            && nativeQuota->liveBytes() == 0u);
        for (auto& counter : counters)
            counter.reset();
    }
};

struct VulkanComposedRootFenceFixture final : VulkanRootFenceFixture {
    bool indexedFault = false;
    mutable std::shared_ptr<arp::rhi_vulkan::VulkanRhiNativeResourcePayload> ordinaryOutput;

    void configureRequest(arp::rhi::BindlessRequirements& request) const
    {
        if (indexedFault)
            request.tables[0].resourceCount = 1u;
    }

    void configureDispatch(arp::rhi::CommandScalarArguments::Dispatch& dispatch) const
    {
        dispatch.x = 2u;
    }

    template <class Table>
    void initializeBuffer(arp::rhi::GpuBuffer& buffer, const Table&, std::uint32_t index) const
    {
        using namespace arp::rhi_vulkan;
        auto native = vulkanRhiNativePayload(buffer);
        assert(native && native->backendMappedData());
        const std::uint32_t value = 777u;
        std::memcpy(native->backendMappedData(), &value, sizeof(value));
        arp::core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, buffer.desc().sizeBytes, diagnostic));
        counters[index] = native;
    }

    void configureOrdinaryBuffer(
        arp::rhi::BufferDesc& desc, std::uint32_t, const arp::rhi::DescriptorBindingDesc&) const
    {
        desc.memory = arp::rhi::MemoryUsage::CpuToGpu;
    }

    void initializeOrdinaryBuffer(arp::rhi::GpuBuffer& buffer,
        std::uint32_t set,
        const arp::rhi::DescriptorBindingDesc& binding) const
    {
        auto native = arp::rhi_vulkan::vulkanRhiNativePayload(buffer);
        assert(native && native->backendMappedData());
        std::memset(
            native->backendMappedData(), 0, static_cast<std::size_t>(buffer.desc().sizeBytes));
        if (set == 0u && binding.binding == 3u) {
            const float frame = 17.0f;
            std::memcpy(native->backendMappedData(), &frame, sizeof(frame));
        } else if (set == 2u && binding.binding == 3u) {
            const std::uint32_t shared = 32u;
            std::memcpy(native->backendMappedData(), &shared, sizeof(shared));
        } else {
            assert(set == 2u && binding.binding == 4u);
            ordinaryOutput = native;
        }
        arp::core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, buffer.desc().sizeBytes, diagnostic));
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root& root,
        const Inventory& inventory,
        const Binding& binding,
        const Transaction& tx,
        arp::rhi::GpuDevice& device) const
    {
        assert(root->sourceContractVersion() == 2u && root->ordinaryDomains().size() == 2u);
        assert(root->originalSnapshot()->setIndex() == 1u && ordinaryOutput);
        {
            const auto prepared =
                std::get<std::shared_ptr<const arp::rhi::BindlessPreparedComputeBinding>>(
                    root->originalPrepared());
            auto encoder =
                device.createCommandEncoder({"Root/2 standalone snapshot refusal", 1u, 1u});
            assert(encoder);
            auto pass = encoder->beginComputePass({"Root/2 standalone snapshot refusal"});
            assert(pass && pass->setPipeline(*prepared->owners()->desc().pipeline).success);
            auto* extension = static_cast<arp::rhi::BindlessPassExtension*>(
                pass->queryInterface(arp::rhi::kExtensionBindlessCross));
            assert(extension);
            const auto& snapshot = root->originalSnapshot();
            const auto refused = extension->bindSnapshot(snapshot,
                snapshot->plan(),
                snapshot->generation(),
                arp::rhi::BindlessAccessArray(nullptr, 0u));
            assert(!refused.success
                && refused.diagnostics == "RHI-BINDLESS-PIPELINE-DOMAINS-ORIGINAL-ROOT-REQUIRED");
        }
        VulkanRootFenceFixture::operator()(root, inventory, binding, tx, device);
        if (submit && !uncertainSubmit) {
            arp::core::pmr::String diagnostic;
            assert(ordinaryOutput->backendInvalidateMappedRead(0u, 8u, diagnostic));
            std::array<std::uint32_t, 2u> words;
            std::memcpy(words.data(), ordinaryOutput->backendMappedData(), sizeof(words));
            assert(words[0] == 49u && words[1] == 49u + std::uint32_t(indexedFault));
            std::cout << "Root/2 sets0/1/2: indexed49, ordinary49/" << words[1] << ", fault"
                      << expectedFault << "; original journal/completion retired\n";
        }
        ordinaryOutput.reset();
    }
};

struct VulkanComposedGraphicsDataFixture final {
    static constexpr bool enabled = true;

    template <class Root, class Sources>
    void inspectOriginalRoot(
        const Root& root, const Sources& sources, arp::rhi::GpuDevice* device) const
    {
        arp::test::OriginalGraphicsRuntimeMemoryFixture::inspect(root, sources, device, fault);
    }

    bool fault = false;

    template <class Result, class Request>
    void verifyRetiredResult(const Result& result, const Request& request) const
    {
        assert(!result && !request);
    }

    template <class Root, class Inventory, class Binding, class Transaction>
    void operator()(const Root&,
        const Inventory&,
        const Binding&,
        const Transaction&,
        arp::rhi::GpuDevice&) const
    {
        assert(false && "Graphics uses the original public execution observer");
    }

    void configureAdmission(arp::rhi::OriginalCommandAdmissionBudget& budget) const
    {
        budget.maxCommands = 32u;
        budget.maxReferences = 32u;
        budget.maxInputDescriptors = 16u;
        budget.maxStateObservations = 64u;
    }

    template <class Table>
    void configureBuffer(arp::rhi::BufferDesc& desc, const Table& table) const
    {
        desc.memory = arp::rhi::MemoryUsage::CpuToGpu;
        desc.sizeBytes = table.bufferLayout.minBindingSizeBytes;
    }

    template <class Table>
    void initializeBuffer(arp::rhi::GpuBuffer& buffer, const Table&, std::uint32_t index) const
    {
        const auto native = arp::rhi_vulkan::vulkanRhiNativePayload(buffer);
        assert(native && native->backendMappedData());
        const std::uint32_t value = 777u + index;
        std::memcpy(native->backendMappedData(), &value, sizeof(value));
        arp::core::pmr::String diagnostic;
        assert(native->backendFlushMappedWrite(0u, buffer.desc().sizeBytes, diagnostic));
    }

    void configureOrdinaryBuffer(arp::rhi::BufferDesc& desc,
        std::uint32_t set,
        const arp::rhi::DescriptorBindingDesc& binding) const
    {
        VulkanComposedRootFenceFixture{}.configureOrdinaryBuffer(desc, set, binding);
    }

    void initializeOrdinaryBuffer(arp::rhi::GpuBuffer& buffer,
        std::uint32_t set,
        const arp::rhi::DescriptorBindingDesc& binding) const
    {
        VulkanComposedRootFenceFixture{}.initializeOrdinaryBuffer(buffer, set, binding);
    }

    static void verify(const arp::rhi::OriginalCommandBindlessRootSource& root)
    {
        using namespace arp::rhi;
        assert(root.sourceContractVersion() == 2u && root.ordinaryDomains().size() == 2u);
        std::uint32_t ordinary = 0u, indexed = 0u;
        const auto read = [](const auto& lease) {
            const auto native = arp::rhi_vulkan::vulkanRhiNativePayload(*lease->originalObject());
            assert(native);
            arp::core::pmr::String diagnostic;
            assert(native->backendInvalidateMappedRead(
                0u, lease->originalObject()->desc().sizeBytes, diagnostic));
            return native->backendMappedData();
        };
        for (const auto& element : root.elements())
            if (element.declaration.write.binding == 3u) {
                const auto& lease =
                    std::get<std::shared_ptr<const OriginalCommandBufferLease>>(element.owner);
                std::uint32_t value = 0u;
                std::memcpy(&value, read(lease), sizeof(value));
                assert(value == 777u + element.declaration.write.arrayElement);
                ++indexed;
            }
        for (const auto& domain : root.ordinaryDomains())
            for (const auto& element : domain.source->frozenElements()) {
                const auto& lease = std::get<std::shared_ptr<const OriginalCommandBufferLease>>(
                    element.resourceOwner);
                const auto data = read(lease);
                if (domain.set == 0u) {
                    float frame = 0.f;
                    std::memcpy(&frame, data, sizeof(frame));
                    assert(frame == 17.f);
                } else if (element.declaration.write.binding == 3u) {
                    std::uint32_t shared = 0u;
                    std::memcpy(&shared, data, sizeof(shared));
                    assert(shared == 32u);
                } else {
                    std::array<std::uint32_t, 4u> actual{};
                    std::memcpy(actual.data(), data, sizeof(actual));
                    assert((actual == std::array<std::uint32_t, 4u>{875u, 876u, 875u, 876u}));
                }
                ++ordinary;
            }
        assert(ordinary == 3u && indexed == 2u);
        std::puts(
            "BINDLESS_VULKAN_COMPOSED_GRAPHICS_BUFFERS exact-sets0/1/2 Vertex49 Fragment49 indexed777/778 ordinary875/876/875/876");
    }
};

int validateComposedRoot(const char* publication, bool graphics = false)
{
    using namespace arp;
    using namespace arp::rhi;
    core::pmr::String diagnostic;
    auto package = BindlessShaderPackage::load(
        publication, {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
    assert(package);
    tests::VulkanHeadlessRuntime native(true, RootFenceNegativeObservation::resolve);
    if (!native.runtime())
        return 77;
    const auto& queried = native.runtime()->descriptorIndexing();
    auto capped = queried;
    capped.limits.maxDescriptorSetStorageBuffers = 5u;
    auto rejected =
        rhi_vulkan::validateVulkanBindlessDescriptorRequirements(capped, package->artifact());
    assert(!rejected.success
        && rejected.diagnostics == "RHI-VULKAN-BINDLESS-PIPELINE-DESCRIPTOR-LIMIT");
    capped = queried;
    capped.limits.maxPerStageDescriptorStorageBuffers = 5u;
    rejected =
        rhi_vulkan::validateVulkanBindlessDescriptorRequirements(capped, package->artifact());
    assert(
        !rejected.success && rejected.diagnostics == "RHI-VULKAN-BINDLESS-STAGE-DESCRIPTOR-LIMIT");
    capped = queried;
    capped.limits.maxPerStageResources = 6u;
    rejected =
        rhi_vulkan::validateVulkanBindlessDescriptorRequirements(capped, package->artifact());
    assert(!rejected.success && rejected.diagnostics == "RHI-VULKAN-BINDLESS-STAGE-RESOURCE-LIMIT");
    tests::RenderDocCaptureScope capture;
    for (unsigned mode = 0u; mode < (graphics ? 2u : 7u); ++mode) {
        rhi_vulkan::VulkanRhiDeviceDesc desc;
        desc.runtime = native.runtime();
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
        desc.requireNativeResourceAllocation = true;
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        VulkanComposedRootFenceFixture execute;
        execute.expectedCounters = {49u, 49u};
        execute.indexedFault = mode == 1u;
        execute.expectedFault = execute.indexedFault ? 1u : 0u;
        execute.submit = mode != 2u;
        execute.deviceIdleDrain = mode >= 3u;
        execute.uncertainSubmit = mode >= 4u;
        execute.rejectInvocation = mode == 5u;
        if (mode == 6u) {
            execute.deviceIdleDrain = false;
            execute.rejectInvocation = false;
        }
        auto device = rhi_vulkan::createVulkanRhiDevice(desc);
        assert(device);
        if (graphics) {
            test::VulkanOriginalBindlessGraphicsContentFixture observer;
            observer.execute = true;
            observer.expectedFaultBits = mode;
            observer.verifyOrdinaryBuffers = VulkanComposedGraphicsDataFixture::verify;
            test::validateOriginalBindlessRootSource(package,
                std::move(device),
                "unused",
                false,
                observer,
                VulkanComposedGraphicsDataFixture{mode == 1u},
                true,
                mode ? "faultFragment" : "cleanFragment");
            continue;
        }
        test::validateOriginalBindlessRootSource(package,
            std::move(device),
            "computeMain",
            true,
            test::OriginalBindlessRootFixtureObserver{},
            execute);
    }
    if (graphics)
        std::cout
            << "BINDLESS_VULKAN_COMPOSED_GRAPHICS2 public prepare record submit poll fault0/1 cancellation quota-return allocation0\n";
    else
        std::cout
            << "BINDLESS_VULKAN_COMPOSED_ROOT2 1 three-sets exact-buffer-output fault cancellation fence idle shutdown uncertain-submit no-invocation quota-return allocation0\n";
    return 0;
}

using namespace arp;
using namespace arp::rhi;
using namespace arp::rhi_vulkan;

// Hide every executable after the sealed package has retained its bytes. Restore
// before evaluating creation results so a failed assertion cannot damage input.
template <class Create>
auto withoutSpirvFiles(
    const BindlessShaderPackage& package, const std::filesystem::path& directory, Create create)
{
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> renamed;
    renamed.reserve(package.modules().size());
    auto restore = [&] {
        for (auto it = renamed.rbegin(); it != renamed.rend(); ++it)
            std::filesystem::rename(it->second, it->first);
    };
    try {
        for (const auto& module : package.modules()) {
            const auto source =
                directory / std::filesystem::path(package.files()[module.spirvFile].name);
            auto hidden = source;
            hidden += ".retained-test";
            std::filesystem::rename(source, hidden);
            renamed.emplace_back(source, hidden);
        }
        auto result = create();
        restore();
        return result;
    } catch (...) {
        restore();
        throw;
    }
}

void validateLimits(
    const VulkanDescriptorIndexingInspection& device, const BindlessShaderArtifact& artifact)
{
    auto expect = [&](const VulkanDescriptorIndexingInspection& caps,
                      const BindlessShaderArtifact& input,
                      const char* diagnostic) {
        const auto result = validateVulkanBindlessComputeRequirements(caps, input, {{8, 1, 1}});
        assert(!result.success && result.diagnostics == diagnostic);
    };
    auto caps = device;
    caps.enabled.nonUniform.fill(false);
    expect(caps, artifact, "RHI-VULKAN-BINDLESS-INDEXING-NOT-ENABLED");
    caps = device;
    caps.limits.maxDescriptorSetStorageBuffers = 4;
    // RO/RW tables and both ordinary fault buffers count with metadata.
    expect(caps, artifact, "RHI-VULKAN-BINDLESS-PIPELINE-DESCRIPTOR-LIMIT");
    caps = device;
    caps.limits.maxPerStageDescriptorStorageBuffers = 4;
    expect(caps, artifact, "RHI-VULKAN-BINDLESS-STAGE-DESCRIPTOR-LIMIT");
    caps = device;
    caps.limits.maxPerStageResources = 8;
    expect(caps, artifact, "RHI-VULKAN-BINDLESS-STAGE-RESOURCE-LIMIT");
    caps = device;
    caps.limits.maxComputeWorkGroupInvocations = 4;
    expect(caps, artifact, "RHI-VULKAN-BINDLESS-WORKGROUP-LIMIT");
    caps = device;
    caps.limits.maxUniformBufferRange = 15;
    expect(caps, artifact, "RHI-VULKAN-BINDLESS-BUFFER-RANGE-LIMIT");
    caps = device;
    caps.limits.maxStorageBufferRange = 159;
    expect(caps, artifact, "RHI-VULKAN-BINDLESS-BUFFER-RANGE-LIMIT");
    auto changed = artifact;
    changed.otherBindGroups = 1;
    expect(device, changed, "RHI-VULKAN-BINDLESS-DOMAINS-INCOMPLETE");
    changed = artifact;
    changed.set = device.limits.maxBoundDescriptorSets;
    expect(device, changed, "RHI-VULKAN-BINDLESS-SET-LIMIT");
    changed = artifact;
    changed.execution = BindlessExecution::Emulated;
    expect(device, changed, "RHI-VULKAN-BINDLESS-PACKAGE-MODE");
}

int main(int argc, char** argv)
{
    if (argc == 2
        && (std::string_view(argv[1]) == "--storage-execution"
            || std::string_view(argv[1]) == "--capture-storage-execution")) {
        const bool captureOnly = std::string_view(argv[1]) == "--capture-storage-execution";
        tests::VulkanHeadlessRuntime native(true, RootFenceNegativeObservation::resolve);
        if (!native.runtime())
            return 77;
        test::VulkanStorageExecutionBackendFixture::runtime = native.runtime();
        tests::RenderDocCaptureScope capture;
        for (unsigned scenario = 0u; scenario != (captureOnly ? 1u : 8u); ++scenario)
            for (bool fault : {false, true}) {
                const auto terminal = scenario < 5u ? scenario : 2u;
                test::VulkanStorageExecutionBackendFixture::failure =
                    scenario < 5u ? 0u : scenario - 4u;
                VulkanRhiDeviceDesc desc;
                desc.label = "Public storage execution9";
                desc.capabilityContract =
                    semantics::nativeBackendCapabilityContract(Backend::Vulkan);
                desc.runtime = native.runtime();
                desc.memoryBudgetBytes = 64u * 1024u * 1024u;
                desc.requireNativeResourceAllocation = true;
                test::VulkanOriginalStorageExecutionFixture::terminal = terminal;
                test::VulkanOriginalStorageExecutionFixture::fault = fault;
                test::OriginalStorageTextureCreationFixture::verifyDevice(
                    createVulkanRhiDevice(desc),
                    nullptr,
                    nullptr,
                    test::VulkanOriginalStorageExecutionFixture::execute);
            }
        capture.end();
        test::VulkanStorageExecutionBackendFixture::failure = 0u;
        test::VulkanStorageExecutionBackendFixture::runtime.reset();
        return 0;
    }
#if defined(ARP_VULKAN_ROOT_GRAPHICS_PUBLICATION) && defined(ARP_VULKAN_SAMPLED_DEPTH_PUBLICATION)
    if (argc == 2
        && (std::string_view(argv[1]) == "--mixed-content-plan"
            || std::string_view(argv[1]) == "--mixed-execution"
            || std::string_view(argv[1]) == "--mixed-lifecycle")) {
        const bool lifecycle = std::string_view(argv[1]) == "--mixed-lifecycle";
        const bool mixedExecution = lifecycle || std::string_view(argv[1]) == "--mixed-execution";
        core::pmr::String diagnostic;
        const auto load = [&](const char* path) {
            auto package = BindlessShaderPackage::load(
                path, {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
            assert(package);
            return package;
        };
        tests::VulkanHeadlessRuntime native(true, RootFenceNegativeObservation::resolve);
        if (!native.runtime())
            return 77;
        test::VulkanStorageExecutionBackendFixture::runtime = native.runtime();
        test::OriginalMixedDepthStoragePlanFixture::executionCheck =
            mixedExecution ? &test::VulkanOriginalStorageExecutionFixture::executeMixed : nullptr;
        // vertexMain's x-only interpolated depths over the 2x2 target.
        test::VulkanOriginalStorageExecutionFixture::mixedDepthValues = {
            .01125f, .01375f, .01125f, .01375f};
        tests::RenderDocCaptureScope capture;
        // The producer stays genuinely completed. Termination applies only to
        // the consuming three-entry stream, not to its depth prerequisite.
        for (unsigned scenario = 0u; scenario != (lifecycle ? 8u : 1u); ++scenario)
            for (const bool fault : {false, true}) {
                if (!mixedExecution && fault)
                    continue;
                test::VulkanOriginalStorageExecutionFixture::terminal =
                    scenario < 5u ? scenario : 2u;
                test::VulkanStorageExecutionBackendFixture::failure =
                    scenario < 5u ? 0u : scenario - 4u;
                test::VulkanOriginalStorageExecutionFixture::fault = fault;
                std::printf(
                    "BINDLESS_VULKAN_PUBLIC_MIXED_BEGIN scenario=%u fault=%d\n", scenario, fault);
                std::fflush(stdout);
                VulkanRhiDeviceDesc desc;
                desc.label = "Mixed Content15 planning";
                desc.capabilityContract =
                    semantics::nativeBackendCapabilityContract(Backend::Vulkan);
                desc.runtime = native.runtime();
                desc.memoryBudgetBytes = 64u * 1024u * 1024u;
                desc.requireNativeResourceAllocation = true;
                test::VulkanOriginalCompletedDepthPublicationFixture observer;
                observer.depthPackage = load(ARP_VULKAN_SAMPLED_DEPTH_PUBLICATION);
                observer.mixedContent = true;
                test::VulkanOriginalGraphicsBufferDataFixture data;
                data.mixedContent = true;
                data.retireDevice = (scenario == 2u || scenario == 4u);
                test::validateOriginalBindlessRootSource(load(ARP_VULKAN_ROOT_GRAPHICS_PUBLICATION),
                    createVulkanRhiDevice(desc),
                    "cleanMain",
                    false,
                    observer,
                    data,
                    true,
                    {},
                    "vertexMain",
                    true);
            }
        capture.end();
        test::VulkanOriginalStorageExecutionFixture::terminal = 0u;
        test::VulkanStorageExecutionBackendFixture::failure = 0u;
        test::OriginalMixedDepthStoragePlanFixture::executionCheck = nullptr;
        test::VulkanStorageExecutionBackendFixture::runtime.reset();
        return 0;
    }
#endif
    if (argc == 2 && std::string_view(argv[1]) == "--storage-content-creation") {
        tests::VulkanHeadlessRuntime native(true, RootFenceNegativeObservation::resolve);
        if (!native.runtime())
            return 77;
        VulkanRhiDeviceDesc desc;
        desc.label = "Original storage content creation6";
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
        desc.runtime = native.runtime();
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        desc.requireNativeResourceAllocation = true;
        arp::test::OriginalStorageTextureCreationFixture::verifyDevice(
            createVulkanRhiDevice(desc),
            [](const DescriptorSet& set) {
                assert(!vulkanRhiNativePayload(set));
            },
            [](auto& device, const auto& root, const auto& inventory) {
                arp::test::OriginalStorageRootClosureFixture::verify<
                    VulkanBindlessRootNativeClosure>(device, root, inventory);
            });
        return 0;
    }
    if (argc == 3
        && (std::string_view(argv[1]) == "--public-completed-color"
            || std::string_view(argv[1]) == "--capture-public-completed-color")) {
        const bool captureOnly = std::string_view(argv[1]) == "--capture-public-completed-color";
        core::pmr::String diagnostic;
        auto package = BindlessShaderPackage::load(
            argv[2], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
        assert(package);
        tests::VulkanHeadlessRuntime native(true, RootFenceNegativeObservation::resolve);
        if (!native.runtime())
            return 77;
        tests::RenderDocCaptureScope capture;
        VulkanRhiDeviceDesc desc;
        desc.label = "Public completed color8";
        desc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
        desc.runtime = native.runtime();
        desc.memoryBudgetBytes = 64u * 1024u * 1024u;
        desc.requireNativeResourceAllocation = true;
        const bool array = std::any_of(package->artifact().physicalLayout.bindings.begin(),
            package->artifact().physicalLayout.bindings.end(),
            [](const auto& b) {
                return b.type == DescriptorType::SampledTexture
                    && b.textureViewDimension == TextureViewDimension::Texture2DArray;
            });
        for (unsigned terminal = 0u; terminal < (captureOnly ? 1u : 7u); ++terminal)
            for (bool fault : {false, true}) {
                arp::test::VulkanPublicCompletedTextureRootFixture fixture;
                fixture.arrayTextures = array;
                fixture.fault = fault;
                fixture.publicTerminal = terminal;
                fixture.nativeStagingCeiling = desc.memoryBudgetBytes;
                fixture.repeatAfterCompletion = !captureOnly;
                arp::test::validateOriginalBindlessRootSource(package,
                    createVulkanRhiDevice(desc),
                    fault ? "faultMain" : "cleanMain",
                    false,
                    fixture,
                    fixture);
            }
        capture.end();
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--composed-root")
        return validateComposedRoot(argv[2]);
    if (argc == 3 && std::string_view(argv[1]) == "--composed-graphics")
        return validateComposedRoot(argv[2], true);
    assert(argc == 4 || (argc == 5 && std::string_view(argv[4]) == "--partial"));
    const bool partial = argc == 5;
    core::pmr::String diagnostic;
    auto package = BindlessShaderPackage::load(
        argv[1], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64, 16, 256, 64}, diagnostic);
    if (!package)
        std::cerr << diagnostic << '\n';
    assert(package);
    auto publicPackage = BindlessShaderPackage::load(
        argv[2], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64, 16, 256, 64}, diagnostic);
    if (!publicPackage)
        std::cerr << diagnostic << '\n';
    assert(publicPackage);
    auto publicGraphicsPackage = BindlessShaderPackage::load(
        argv[3], {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64, 16, 256, 64}, diagnostic);
    if (!publicGraphicsPackage)
        std::cerr << diagnostic << '\n';
    assert(publicGraphicsPackage);
    {
        tests::VulkanHeadlessRuntime disabled;
        if (!disabled.runtime())
            return 77;
        VulkanRhiNativeResourceAllocator resources(disabled.runtime());
        const auto denied = resources.createBindlessComputePayload(*package, "computeMain");
        assert(!denied.success
            && denied.diagnostics == "RHI-VULKAN-BINDLESS-STAGE-WRITES-NOT-ENABLED");
        assert(resources.stats().nativeComputePipelineCreateCount == 0);
    }
    tests::VulkanHeadlessRuntime native(true, RootFenceNegativeObservation::resolve);
    if (!native.runtime())
        return 77;
    tests::RenderDocCaptureScope capture;
    const auto validation = validateVulkanBindlessComputeRequirements(
        native.runtime()->descriptorIndexing(), package->artifact(), {{8, 1, 1}});
    if (!validation.success) {
        std::cout << "SKIP: " << validation.diagnostics << '\n';
        return 77;
    }
    validateLimits(native.runtime()->descriptorIndexing(), package->artifact());
    VulkanRhiNativeResourceAllocator resources(native.runtime());
    assert(!resources.createBindlessComputePayload(*package, "fragmentMain").success);
    const auto found =
        std::find_if(package->modules().begin(), package->modules().end(), [](const auto& m) {
            return m.entry == "computeMain";
        });
    assert(found != package->modules().end());
    assert(found->reflection.reflection.entryPoints[0].computeWorkgroupSizeX == 8);
    const auto directory = std::filesystem::path(argv[1]).parent_path();
    auto pipeline = withoutSpirvFiles(*package, directory, [&] {
        return resources.createBindlessComputePayload(*package, "computeMain");
    });
    if (!pipeline.success)
        std::cerr << pipeline.diagnostics << '\n';
    assert(pipeline.success && pipeline.payload
        && pipeline.payload->backendVulkanComputePipelineHandle());
    RenderPipelineDesc state;
    state.label = "indexed sampler readback";
    state.colorFormat = TextureFormat::Rgba8Unorm;
    state.hasViewport = true;
    state.viewport = {0, 0, 2, 2, 0, 1};
    state.hasScissor = true;
    state.scissor = {0, 0, 2, 2};
    auto rejectedState = state;
    rejectedState.multisample.sampleCount = 2;
    assert(
        !resources.createBindlessRenderPayload(package, rejectedState, "vertexMain", "fragmentMain")
            .success);
    assert(!resources.createBindlessRenderPayload(package, state, "computeMain", "fragmentMain")
            .success);
    auto graphics = withoutSpirvFiles(*package, directory, [&] {
        return resources.createBindlessRenderPayload(package, state, "vertexMain", "fragmentMain");
    });
    if (!graphics.success)
        std::cerr << graphics.diagnostics << '\n';
    assert(graphics.success && graphics.payload->backendVulkanRenderPipelineHandle());
    auto depthState = state;
    depthState.colorFormat = TextureFormat::Unknown;
    depthState.depthStencil.format = TextureFormat::D32Float;
    depthState.depthStencil.depthTestEnabled = true;
    depthState.depthStencil.depthWriteEnabled = true;
    depthState.depthStencil.depthCompare = CompareOp::LessEqual;
    assert(!resources.createBindlessRenderPayload(package, state, "vertexMain", "").success);
    const auto shaderModulesBeforeDepth = resources.stats().nativeShaderModuleCreateCount;
    auto depthOnly = withoutSpirvFiles(*package, directory, [&] {
        return resources.createBindlessRenderPayload(package, depthState, "vertexMain", "");
    });
    if (!depthOnly.success)
        std::cerr << depthOnly.diagnostics << '\n';
    assert(depthOnly.success && depthOnly.payload->backendVulkanRenderPipelineHandle());
    assert(resources.stats().nativeShaderModuleCreateCount == shaderModulesBeforeDepth + 1u);
    VulkanRhiDeviceDesc deviceDesc;
    deviceDesc.label = "native bindless compute fixture";
    deviceDesc.capabilityContract = semantics::nativeBackendCapabilityContract(Backend::Vulkan);
    deviceDesc.runtime = native.runtime();
    deviceDesc.memoryBudgetBytes = 64u * 1024u * 1024u;
    deviceDesc.requireNativeResourceAllocation = true;
#ifdef ARP_VULKAN_ROOT_GRAPHICS_PUBLICATION
    const auto publicDepthPublication = [&] {
        auto graphicsRoot = BindlessShaderPackage::load(ARP_VULKAN_ROOT_GRAPHICS_PUBLICATION,
            {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
            diagnostic);
        assert(graphicsRoot);
        auto depthPackage = BindlessShaderPackage::load(ARP_VULKAN_SAMPLED_DEPTH_PUBLICATION,
            {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
            diagnostic);
        assert(depthPackage);
        arp::test::VulkanOriginalCompletedDepthPublicationFixture publication;
        publication.depthPackage = depthPackage;
#ifdef ARP_VULKAN_ORDINARY_DEPTH_GRAPHICS_PACKAGE
        publication.ordinaryGraphicsPackage =
            BindlessShaderPackage::load(ARP_VULKAN_ORDINARY_DEPTH_GRAPHICS_PACKAGE,
                {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                diagnostic);
        assert(publication.ordinaryGraphicsPackage);
#endif
        arp::test::VulkanOriginalGraphicsBufferDataFixture publicationResources;
        publicationResources.ordinaryDepthRoot = bool(publication.ordinaryGraphicsPackage);
        arp::test::validateOriginalBindlessRootSource(graphicsRoot,
            createVulkanRhiDevice(deviceDesc),
            "cleanMain",
            false,
            publication,
            publicationResources,
            true,
            {},
            "vertexMain",
            true);
    };
    if (std::getenv("ARP_VULKAN_PUBLIC_DEPTH_ONLY")) {
        publicDepthPublication();
        capture.end();
        return 0;
    }
#endif
    auto device = createVulkanRhiDevice(deviceDesc);
    assert(device);
    arp::test::OriginalStorageTextureCreationFixture::verifyDevice(
        createVulkanRhiDevice(deviceDesc),
        [](const DescriptorSet& set) {
            assert(!vulkanRhiNativePayload(set));
        },
        [](auto& device, const auto& root, const auto& inventory) {
            arp::test::OriginalStorageRootClosureFixture::verify<VulkanBindlessRootNativeClosure>(
                device, root, inventory);
        });
    arp::test::VulkanOriginalTextureCreationFixture::verify(deviceDesc);
    arp::test::VulkanOriginalTextureCreationFixture::verifyAttachments(deviceDesc);
    arp::test::VulkanOriginalTextureUploadFixture::verify<RootFenceNegativeObservation>(
        deviceDesc, package->artifact());
    arp::test::VulkanNativeResultLossFixture::verify(
        deviceDesc,
        [](VkDevice device) {
            RootFenceNegativeObservation::injectedLostDevice = device;
        },
        [] {
            RootFenceNegativeObservation::injectedLostDevice = VK_NULL_HANDLE;
        });
    for (const auto* variable :
        {"ARP_VULKAN_TEXTURE_ROOT_PACKAGE", "ARP_VULKAN_ARRAY_ROOT_PACKAGE"}) {
        if (const auto* texturePackagePath = std::getenv(variable)) {
            auto texturePackage = BindlessShaderPackage::load(texturePackagePath,
                {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64, 16, 256, 64},
                diagnostic);
            assert(texturePackage);
            for (const auto scenario : {0u, 1u, 2u}) {
                arp::test::VulkanCompletedTextureRootFixture textureRoot;
                textureRoot.arrayTextures =
                    std::strcmp(variable, "ARP_VULKAN_ARRAY_ROOT_PACKAGE") == 0;
                textureRoot.fault = scenario == 1u;
                textureRoot.abandon = scenario == 2u;
                textureRoot.nativeStagingCeiling = deviceDesc.memoryBudgetBytes;
                arp::test::validateOriginalBindlessRootSource(texturePackage,
                    createVulkanRhiDevice(deviceDesc),
                    textureRoot.fault ? "faultMain" : "cleanMain",
                    false,
                    textureRoot,
                    textureRoot);
            }
            if (std::getenv("ARP_VULKAN_TEXTURE_ROOT_TERMINAL_CASES")) {
                // Hooks start only after each texture's genuine /7 upload. The
                // same /8 factory covers fence and idle retirement, unknown queue
                // results, no invocation, and an unknown fence observation.
                for (const auto& scenario :
                    std::array<arp::test::VulkanOriginalRootTerminalScenario, 5u>{
                        {{false, false, false, false},
                            {false, true, false, false},
                            {true, false, false, false},
                            {true, true, true, false},
                            {false, true, false, true}}}) {
                    for (const auto fault : {false, true}) {
                        arp::test::VulkanCompletedTextureRootFixture textureRoot;
                        textureRoot.arrayTextures =
                            std::strcmp(variable, "ARP_VULKAN_ARRAY_ROOT_PACKAGE") == 0;
                        textureRoot.fault = fault;
                        textureRoot.terminalCases = true;
                        textureRoot.terminal = scenario;
                        textureRoot.repeatAfterCompletion = false;
                        textureRoot.nativeStagingCeiling = deviceDesc.memoryBudgetBytes;
                        arp::test::validateOriginalBindlessRootSource(texturePackage,
                            createVulkanRhiDevice(deviceDesc),
                            fault ? "faultMain" : "cleanMain",
                            false,
                            textureRoot,
                            textureRoot);
                    }
                }
                std::cout << (std::strcmp(variable, "ARP_VULKAN_ARRAY_ROOT_PACKAGE") == 0
                        ? "BINDLESS_VULKAN_COMPLETED_ARRAY_ROOT8_TERMINAL 1\n"
                        : "BINDLESS_VULKAN_COMPLETED_TEXTURE_ROOT8_TERMINAL 1 texture-view-sampler-custody withheld-fence shutdown same-runtime-idle unknown-queue unknown-fence unpublished-quarantine allocation0\n");
            }
        }
    }
    if (const auto* rootPackagePath = std::getenv("ARP_VULKAN_ROOT_FENCE_PACKAGE")) {
        auto rootPackage = BindlessShaderPackage::load(rootPackagePath,
            {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64, 16, 256, 64},
            diagnostic);
        assert(rootPackage);
        for (const auto& scenario : std::array<VulkanRootFenceFixture, 3u>{
                 {{false, 0u, false, {}}, {true, 0u, false, {}}, {true, 1u, false, {}}}})
            arp::test::validateOriginalBindlessRootSource(rootPackage,
                createVulkanRhiDevice(deviceDesc),
                scenario.expectedFault ? "faultMain" : "cleanMain",
                true,
                arp::test::OriginalBindlessRootFixtureObserver{},
                scenario);
        std::cout << "BINDLESS_VULKAN_CANONICAL_ROOT_BUFFER_EXECUTION 1\n";
        if (std::getenv("ARP_VULKAN_ROOT_UNCERTAIN_SUBMIT")) {
            arp::test::validateOriginalBindlessRootSource(rootPackage,
                createVulkanRhiDevice(deviceDesc),
                "cleanMain",
                true,
                arp::test::OriginalBindlessRootFixtureObserver{},
                VulkanRootFenceFixture{true, 0u, true, {}});
            std::cout << "BINDLESS_VULKAN_ROOT_UNCERTAIN_SUBMIT_RETIRED_UNPUBLISHED 1\n";
        }
        if (std::getenv("ARP_VULKAN_ROOT_IDLE_DRAIN")) {
            for (const auto& scenario :
                std::array<VulkanRootFenceFixture, 4u>{{{true, 0u, false, {}, true},
                    {true, 1u, false, {}, true},
                    {true, 0u, true, {}, true},
                    {true, 0u, true, {}, true, true}}})
                arp::test::validateOriginalBindlessRootSource(rootPackage,
                    createVulkanRhiDevice(deviceDesc),
                    scenario.expectedFault ? "faultMain" : "cleanMain",
                    true,
                    arp::test::OriginalBindlessRootFixtureObserver{},
                    scenario);
            std::cout << "BINDLESS_VULKAN_ROOT_DEVICE_IDLE_RETIRED 1\n";
        }
    }
#ifdef ARP_VULKAN_ROOT_GRAPHICS_PUBLICATION
    {
        auto graphicsRoot = BindlessShaderPackage::load(ARP_VULKAN_ROOT_GRAPHICS_PUBLICATION,
            {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
            diagnostic);
        assert(graphicsRoot);
        arp::test::validateOriginalBindlessRootSource(graphicsRoot,
            createVulkanRhiDevice(deviceDesc),
            "cleanMain",
            false,
            arp::test::VulkanOriginalBindlessGraphicsContentFixture{},
            arp::test::OriginalBindlessRootFixtureExecution{},
            true);
        publicDepthPublication();
    }
#endif
#ifdef ARP_VULKAN_ROOT_COMPLETED_GRAPHICS_PUBLICATION
    {
        auto graphicsRoot =
            BindlessShaderPackage::load(ARP_VULKAN_ROOT_COMPLETED_GRAPHICS_PUBLICATION,
                {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                diagnostic);
        assert(graphicsRoot);
        arp::test::VulkanCompletedTextureRootFixture uploaded;
        uploaded.nativeStagingCeiling = deviceDesc.memoryBudgetBytes;
        arp::test::validateOriginalBindlessRootSource(graphicsRoot,
            createVulkanRhiDevice(deviceDesc),
            "cleanMain",
            false,
            arp::test::VulkanOriginalBindlessGraphicsContentFixture{},
            uploaded,
            true);
        arp::test::VulkanOriginalBindlessGraphicsContentFixture sampledGraphics;
        sampledGraphics.execute = true;
        sampledGraphics.verifyOrdinaryBuffers =
            arp::test::VulkanCompletedTextureRootFixture::verifyGraphicsOrdinaryBuffers<false>;
        arp::test::validateOriginalBindlessRootSource(graphicsRoot,
            createVulkanRhiDevice(deviceDesc),
            "cleanMain",
            false,
            sampledGraphics,
            uploaded,
            true);
        arp::test::VulkanOriginalGeometryDataFixture geometryData;
        geometryData.nativeStagingCeiling = deviceDesc.memoryBudgetBytes;
        arp::test::VulkanOriginalBindlessGeometryFixture geometryObserver;
        geometryObserver.initialized = geometryData.initialized;
        // The cold Geometry2/3 contracts deliberately exclude ordinary groups.
        // Keep their existing initialized vertex/instance shaders as evidence;
        // the scene sampling composition above uses the public Root2 contract.
        auto geometryRoot = BindlessShaderPackage::load(ARP_VULKAN_ROOT_GEOMETRY_VERTEX_PUBLICATION,
            {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
            diagnostic);
        assert(geometryRoot);
        arp::test::validateOriginalBindlessRootSource(geometryRoot,
            createVulkanRhiDevice(deviceDesc),
            "cleanMain",
            false,
            geometryObserver,
            geometryData,
            true);
        for (const auto& q : geometryData.initialized->quotas)
            assert(!q.metadata->liveBytes() && !q.payload->liveBytes() && !q.resource->liveBytes()
                && !q.lease->liveBytes());
        geometryData.instanceRate = true;
        geometryRoot = BindlessShaderPackage::load(ARP_VULKAN_ROOT_GEOMETRY_INSTANCE_PUBLICATION,
            {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
            diagnostic);
        assert(geometryRoot);
        arp::test::validateOriginalBindlessRootSource(geometryRoot,
            createVulkanRhiDevice(deviceDesc),
            "cleanMain",
            false,
            geometryObserver,
            geometryData,
            true);
        for (const auto& q : geometryData.initialized->quotas)
            assert(!q.metadata->liveBytes() && !q.payload->liveBytes() && !q.resource->liveBytes()
                && !q.lease->liveBytes());
        if (std::getenv("ARP_VULKAN_TEXTURE_ROOT_TERMINAL_CASES")) {
            for (const auto& scenario :
                std::array<arp::test::VulkanOriginalRootTerminalScenario, 5u>{
                    {{false, false, false, false},
                        {false, true, false, false},
                        {true, false, false, false},
                        {true, true, true, false},
                        {false, true, false, true}}}) {
                arp::test::VulkanOriginalBindlessGraphicsContentFixture graphics;
                graphics.execute = true;
                graphics.terminalCases = true;
                graphics.terminal = scenario;
                graphics.verifyOrdinaryBuffers =
                    arp::test::VulkanCompletedTextureRootFixture::verifyGraphicsOrdinaryBuffers<
                        false>;
                arp::test::validateOriginalBindlessRootSource(graphicsRoot,
                    createVulkanRhiDevice(deviceDesc),
                    "cleanMain",
                    false,
                    graphics,
                    uploaded,
                    true);
            }
            std::cout
                << "BINDLESS_VULKAN_ROOT_GRAPHICS_EXECUTION9_TERMINAL 1 original-attachment cold-pass retained-shutdown genuine-fence-idle uncertain-queue-fence unpublished-retirement\n";
        }
    }
#endif
#ifdef ARP_VULKAN_ROOT_GEOMETRY_VERTEX_PUBLICATION
    for (const bool instanceRate : {false, true})
        for (const bool fault : {false, true}) {
            auto package = BindlessShaderPackage::load(instanceRate
                    ? ARP_VULKAN_ROOT_GEOMETRY_INSTANCE_PUBLICATION
                    : ARP_VULKAN_ROOT_GEOMETRY_VERTEX_PUBLICATION,
                {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                diagnostic);
            assert(package);
            arp::test::VulkanOriginalGeometryDataFixture data;
            data.nativeStagingCeiling = deviceDesc.memoryBudgetBytes;
            data.instanceRate = instanceRate;
            data.executionGeometry = true;
            arp::test::VulkanOriginalBindlessGraphicsContentFixture observer;
            observer.execute = true;
            observer.geometry = data.initialized;
            observer.repeatCompletedAttachment = true;
            observer.attachmentClearOptIn = true;
            observer.expectedFaultBits = fault ? 1u : 0u;
            arp::test::validateOriginalBindlessRootSource(package,
                createVulkanRhiDevice(deviceDesc),
                "cleanMain",
                false,
                observer,
                data,
                true,
                fault ? "faultFragment" : "fragmentMain");
            data.initialized->actors = {};
            for (const auto& q : data.initialized->quotas)
                assert(!q.metadata->liveBytes() && !q.payload->liveBytes()
                    && !q.resource->liveBytes() && !q.lease->liveBytes());
        }
#endif
#ifdef ARP_VULKAN_ROOT_GRAPHICS_BUFFERS_PUBLICATION
    {
        auto graphicsRoot =
            BindlessShaderPackage::load(ARP_VULKAN_ROOT_GRAPHICS_BUFFERS_PUBLICATION,
                {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                diagnostic);
        assert(graphicsRoot);
        auto disabledFragment = native.runtime()->descriptorIndexing();
        disabledFragment.enabled.fragmentStoresAndAtomics = false;
        const auto denied = validateVulkanBindlessDescriptorRequirements(
            disabledFragment, graphicsRoot->artifact());
        assert(!denied.success
            && denied.diagnostics == "RHI-VULKAN-BINDLESS-STAGE-WRITES-NOT-ENABLED");
        for (const auto fault : {false, true}) {
            arp::test::VulkanOriginalBindlessGraphicsContentFixture graphics;
            graphics.execute = true;
            graphics.bufferSideEffects = true;
            graphics.expectedFaultBits = fault ? 1u : 0u;
            arp::test::validateOriginalBindlessRootSource(graphicsRoot,
                createVulkanRhiDevice(deviceDesc),
                "cleanMain",
                false,
                graphics,
                arp::test::VulkanOriginalGraphicsBufferDataFixture{},
                true,
                fault ? "faultFragment" : "cleanFragment");
        }
    }
#endif
#ifdef ARP_VULKAN_ROOT_GRAPHICS_ARRAY_PUBLICATION
    {
        auto graphicsRoot = BindlessShaderPackage::load(ARP_VULKAN_ROOT_GRAPHICS_ARRAY_PUBLICATION,
            {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
            diagnostic);
        assert(graphicsRoot);
        arp::test::VulkanCompletedTextureRootFixture uploaded;
        uploaded.arrayTextures = true;
        uploaded.nativeStagingCeiling = deviceDesc.memoryBudgetBytes;
        for (const auto fault : {false, true}) {
            arp::test::VulkanOriginalBindlessGraphicsContentFixture graphics;
            graphics.execute = true;
            graphics.arraySampling = true;
            graphics.expectedFaultBits = fault ? 1u : 0u;
            graphics.verifyOrdinaryBuffers =
                arp::test::VulkanCompletedTextureRootFixture::verifyGraphicsOrdinaryBuffers<true>;
            arp::test::validateOriginalBindlessRootSource(graphicsRoot,
                createVulkanRhiDevice(deviceDesc),
                "cleanMain",
                false,
                graphics,
                uploaded,
                true,
                fault ? "faultFragment" : "cleanFragment");
        }
    }
#endif
#ifdef ARP_VULKAN_ROOT_GRAPHICS_RECORDS_PUBLICATION
    {
        auto graphicsRoot =
            BindlessShaderPackage::load(ARP_VULKAN_ROOT_GRAPHICS_RECORDS_PUBLICATION,
                {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                diagnostic);
        assert(graphicsRoot);
        for (const auto finite : {false, true})
            for (const auto fault : {false, true}) {
                arp::test::VulkanOriginalBindlessGraphicsContentFixture graphics;
                graphics.execute = true;
                graphics.recordSideEffects = true;
                graphics.expectedFaultBits = fault ? 1u : 0u;
                graphics.recordRanges = finite;
                arp::test::VulkanOriginalGraphicsRecordDataFixture data;
                data.finiteRanges = finite;
                arp::test::validateOriginalBindlessRootSource(graphicsRoot,
                    createVulkanRhiDevice(deviceDesc),
                    "cleanMain",
                    false,
                    graphics,
                    data,
                    true,
                    fault ? "faultFragment" : "cleanFragment");
            }
    }
#endif
    // Reuse the six-class original Root builder, without treating its CPU state
    // fixtures as native execution. Verify genuine device/payload correspondence
    // and exact native metadata quota before the numeric prepared-path smoke.
    {
        using Closure = VulkanBindlessRootNativeClosure;
        constexpr std::uint64_t nativeCpuBytes = 65536u;
        auto nativeQuota = core::makeShared<core::CpuMemoryQuota>(nativeCpuBytes);
        std::shared_ptr<const Closure> retainedClosure;
        const auto observe = [&](const auto& root,
                                 const auto& inventory,
                                 GpuDevice* originalDevice) {
            if (!originalDevice) {
                assert(retainedClosure && Closure::validateOriginal(retainedClosure));
                retainedClosure.reset();
                assert(nativeQuota->liveBytes() == 0u);
                return;
            }
            // Each independent factory scenario measures before quota
            // pressure changes that quota's historical high-water mark.
            assert(nativeQuota->liveBytes() == 0u);
            nativeQuota = core::makeShared<core::CpuMemoryQuota>(nativeCpuBytes);
            const char* id = nullptr;
            const auto count = static_cast<std::uint32_t>(root->elements().size()
                + (root->originalSnapshot()->originalBufferSource(
                       BindlessSnapshot::OriginalBufferRole::FaultReadback)
                        ? 1u
                        : 0u));
            OriginalBindlessRootNativeClosureBudget b{
                1u, true, count, count * 2u + 2u, 512u, nativeCpuBytes};
            const auto prepare = [&](const auto& budget) {
                return Closure::prepareOriginal(
                    root, inventory, *originalDevice, budget, nativeQuota, id);
            };
            auto disabled = b;
            disabled.prototypeOptIn = false;
            assert(!prepare(disabled)
                && std::string_view(id) == "RHI-BINDLESS-ROOT-NATIVE-CLOSURE-OPT-IN");
            disabled = b;
            --disabled.maxResourceEntries;
            assert(!prepare(disabled)
                && std::string_view(id) == "RHI-VULKAN-BINDLESS-ROOT-NATIVE-ENTRY-CAPACITY");
            disabled = b;
            disabled.maxNativeOwners = 1u;
            assert(!prepare(disabled)
                && std::string_view(id) == "RHI-VULKAN-BINDLESS-ROOT-NATIVE-OWNER-CAPACITY");
            assert(nativeQuota->liveBytes() == 0u);
            assert(!Closure::prepareOriginal(
                       root, inventory, *originalDevice, b, inventory->originalMetadataQuota(), id)
                && std::string_view(id) == "RHI-VULKAN-BINDLESS-ROOT-NATIVE-QUOTA-DOMAIN");
            arp::test::CpuQuotaFactoryFixture probe(nativeQuota);
            const auto peak = probe.measure([&] {
                return prepare(b);
            });
            probe.verify(peak, [&] {
                return prepare(b);
            });
            retainedClosure = prepare(b);
            if (!retainedClosure)
                std::cerr << "canonical native Root: " << id << '\n';
            assert(retainedClosure && !id && !retainedClosure->nativeExecutionProven()
                && retainedClosure->resourceEntryCount() == count
                && core::SharedOwnerProjection::sameOwner(retainedClosure->originalRoot(), root)
                && core::SharedOwnerProjection::sameOwner(
                    retainedClosure->originalInventory(), inventory));
            auto exact = b;
            exact.maxNativeOwners = retainedClosure->nativeOwnerCount();
            exact.maxRetainedOwners = retainedClosure->retainedOwnerCount();
            assert(prepare(exact));
            --exact.maxRetainedOwners;
            assert(!prepare(exact)
                && std::string_view(id) == "RHI-VULKAN-BINDLESS-ROOT-OWNER-CAPACITY");
            auto forged = std::shared_ptr<const Closure>(nativeQuota, retainedClosure.get());
            assert(std::string_view(Closure::validateOriginal(forged))
                == "RHI-VULKAN-BINDLESS-ROOT-NATIVE-OWNER");
            auto foreignDevice = createVulkanRhiDevice(deviceDesc);
            assert(!Closure::prepareOriginal(root, inventory, *foreignDevice, b, nativeQuota, id));
            assert(!arp::test::CpuQuotaFactoryFixture::verifyHotNoAllocation(
                std::array<std::shared_ptr<core::CpuMemoryQuota>, 2u>{
                    nativeQuota, root->originalMetadataQuota()},
                [&]() noexcept {
                    return Closure::validateOriginal(retainedClosure);
                }));
        };
        arp::test::validateOriginalBindlessRootSource(
            publicPackage, createVulkanRhiDevice(deviceDesc), "computeMain", false, observe);
        arp::test::validateOriginalBindlessRootSource(publicGraphicsPackage,
            createVulkanRhiDevice(deviceDesc),
            "computeMain",
            false,
            observe);
        assert(!retainedClosure && nativeQuota->liveBytes() == 0u);
        std::cout << "BINDLESS_VULKAN_ROOT_NATIVE_CLOSURE 1\n";
        std::cout
            << "canonicalRootNativeClosure: six-class Compute and selected Graphics physical payload/view-parent correspondence, original fault child, quota rollback, hot allocation0, device invalidation PASS; execution closed\n";
    }
    auto* extension =
        static_cast<BindlessCrossExtension*>(device->queryInterface(kExtensionBindlessCross));
    assert(extension);
    const auto& published = extension->capabilities();
    const auto& queried = native.runtime()->descriptorIndexing().limits;
    assert(published.deviceIdentity != 0u && published.limitsQueried);
    VkPhysicalDeviceFeatures queriedCoreFeatures{};
    vkGetPhysicalDeviceFeatures(native.runtime()->physicalDevice(), &queriedCoreFeatures);
    assert(published.indirectFirstInstance
        == (queriedCoreFeatures.drawIndirectFirstInstance == VK_TRUE));
    assert(published.indirectFirstInstance == native.runtime()->indirectFirstInstanceEnabled());
    assert(published.stageLimits[2].sampledTextures == queried.maxPerStageDescriptorSampledImages);
    assert(published.stageLimits[2].samplers == queried.maxPerStageDescriptorSamplers);
    assert(published.maxStorageBufferBindingSize == queried.maxStorageBufferRange);
    assert(published.execution == BindlessExecution::Native);
    assert(published.executablePayload && published.faultReadbackContractVersion == 1u);
    assert(published.support[2][0].operations
        == (BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::SampleLevel));
    assert(published.support[2][1].operations == BindlessOperation::None);
    assert(published.support[1][0].operations
        == (BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::SampleLevel
            | BindlessOperation::SampleGrad));
    assert(published.support[1][1].operations == BindlessOperation::None);
    assert(published.support[1][3].operations == BindlessOperation::Load);
    assert(
        published.support[1][4].operations == (BindlessOperation::Load | BindlessOperation::Query));
    assert(published.support[1][5].operations
        == (BindlessOperation::Load | BindlessOperation::Query | BindlessOperation::Store
            | BindlessOperation::Atomic));
    assert(published.support[1][2].operations == BindlessOperation::None);
    assert(
        published.support[0][0].operations == (BindlessOperation::Load | BindlessOperation::Query));
    BindlessRequirements publicRequest;
    publicRequest.optIn = true;
    publicRequest.allowEmulated = false;
    publicRequest.deviceIdentity = published.deviceIdentity;
    publicRequest.shaderPackageIdentity = publicPackage->artifact().shaderPackageIdentity;
    publicRequest.budget = {1u, 64u, publicPackage->artifact().snapshotBytes, 64u};
    for (const auto& mapping : publicPackage->artifact().tables) {
        const auto binding = std::find_if(publicPackage->artifact().physicalLayout.bindings.begin(),
            publicPackage->artifact().physicalLayout.bindings.end(),
            [&](const auto& value) {
                return value.binding == mapping.firstBinding;
            });
        assert(binding != publicPackage->artifact().physicalLayout.bindings.end());
        BindlessTableRequirement requirement;
        requirement.tableId = mapping.tableId;
        requirement.binding = *binding;
        requirement.resourceCount = partial && mapping.tableId >= 3u ? 1u : mapping.capacity;
        requirement.operations = mapping.operations;
        requirement.nonUniform = mapping.nonUniform;
        requirement.bufferLayout = mapping.bufferLayout;
        publicRequest.tables.push_back(requirement);
    }
    const auto publicPlan = extension->plan(publicRequest, publicPackage->artifact());
    if (!publicPlan.success)
        std::cerr << publicPlan.diagnosticId << '\n';
    assert(publicPlan.success && publicPlan.executable
        && publicPlan.execution == BindlessExecution::Native);
    auto unauthorizedPipeline = extension->createComputePipeline({}, "computeMain", diagnostic);
    assert(!unauthorizedPipeline);
    assert(diagnostic == "RHI-VULKAN-BINDLESS-PIPELINE-AUTHORIZATION-INVALID");
    BindlessSnapshotPool publicSnapshots(*device, publicRequest.budget);
    auto publicSnapshotCandidate =
        publicSnapshots.prepare(publicRequest, publicPackage->artifact(), diagnostic);
    if (!publicSnapshotCandidate)
        std::cerr << diagnostic << '\n';
    assert(publicSnapshotCandidate);
    auto publicSnapshot = core::shareUnique(std::move(publicSnapshotCandidate));
    assert(publicSnapshots.liveSnapshots() == 1u
        && publicSnapshots.chargedBytes() == publicPlan.snapshotBytes);
    auto publicEncoder = device->createCommandEncoder({"bindless execution gate", 1u, 1u});
    assert(publicEncoder);
    auto publicGatePass = publicEncoder->beginComputePass({"bindless execution gate"});
    assert(publicGatePass);
    assert(publicGatePass->queryInterface(kExtensionBindlessCross) != nullptr);
    assert(publicGatePass->end().success);
    auto arena = device->createDescriptorArena({"native arrays", 1, 15});
    auto descriptors =
        arena->allocateDescriptorSet({"native arrays", package->artifact().physicalLayout});
    assert(descriptors);
    auto buffer = [&](const char* name,
                      const void* bytes,
                      std::size_t size,
                      ResourceUsage usage,
                      MemoryUsage memory = MemoryUsage::CpuToGpu) {
        auto result = device->createBuffer({name, size, usage, memory, ResourceState::Undefined});
        assert(result);
        const auto payload = vulkanRhiNativePayload(*result);
        assert(payload && payload->backendMappedData());
        if (bytes) {
            std::memcpy(payload->backendMappedData(), bytes, size);
            assert(payload->backendFlushMappedWrite(0, size, diagnostic));
        }
        return result;
    };
    auto write = [&](std::uint32_t binding,
                     std::uint32_t element,
                     DescriptorType type,
                     GpuResourceId resource,
                     DescriptorResourceKind kind,
                     ResourceState state) {
        DescriptorBindingWrite write;
        write.binding = binding;
        write.arrayElement = element;
        write.type = type;
        write.resource = resource;
        write.resourceKind = kind;
        write.resourceState = state;
        const auto result = descriptors->write(write);
        if (!result.success)
            std::cerr << result.diagnostics << '\n';
        assert(result.success);
    };
    const std::uint8_t colors[2][4][4] = {
        {{20, 40, 60, 255}, {100, 50, 30, 255}, {60, 120, 40, 255}, {160, 140, 80, 255}},
        {{120, 20, 80, 255}, {80, 100, 120, 255}, {30, 140, 100, 255}, {140, 60, 40, 255}}};
    const std::uint8_t mips[2][4] = {{30, 60, 90, 255}, {90, 30, 60, 255}};
    std::array<std::uint8_t, 1792> textureData{};
    for (unsigned i = 0; i < 2; ++i) {
        std::memcpy(textureData.data() + 256 + i * 768, colors[i][0], 8);
        std::memcpy(textureData.data() + 512 + i * 768, colors[i][2], 8);
        std::memcpy(textureData.data() + 768 + i * 768, mips[i], 4);
    }
    const std::array<std::uint32_t, 5> zeroFaults{};
    auto upload = buffer(
        "texture initialization", textureData.data(), textureData.size(), ResourceUsage::CopySrc);
    // Explicit reference snapshot for this backend-private execution fixture.
    const auto& metadataLayout = package->artifact().snapshotMetadata;
    core::pmr::Vector<std::uint32_t> metadataWords(metadataLayout.wordCount, 0u);
    metadataWords[0] = 1;
    metadataWords[1] = static_cast<std::uint32_t>(package->artifact().tables.size());
    metadataWords[2] = 1;
    for (const auto& table : package->artifact().tables) {
        const auto offset = table.metadataWordOffset;
        metadataWords[offset] = table.tableId;
        metadataWords[offset + 1] = partial && table.tableId >= 3 ? 1u : table.capacity;
        metadataWords[offset + 2] = table.capacity;
        for (std::uint32_t i = 0; i < metadataWords[offset + 1]; ++i)
            metadataWords[offset + 4 + i] = table.bufferLayout.elementStrideBytes ? 1u : 0u;
    }
    auto inaccessibleMetadata = device->createBuffer({"unmappable metadata",
        metadataWords.size() * 4u,
        ResourceUsage::StorageBuffer,
        MemoryUsage::GpuOnly,
        ResourceState::ShaderRead});
    assert(inaccessibleMetadata);
    assert(
        !device->writeBuffer(*inaccessibleMetadata, metadataWords.data(), metadataWords.size() * 4u)
            .success);
    inaccessibleMetadata.reset();
    auto metadata = buffer("immutable snapshot metadata",
        nullptr,
        metadataWords.size() * 4u,
        ResourceUsage::StorageBuffer);
    assert(device->writeBuffer(*metadata, metadataWords.data(), metadataWords.size() * 4u).success);
    write(metadataLayout.binding,
        0,
        DescriptorType::ReadOnlyStorageBuffer,
        metadata->id(),
        DescriptorResourceKind::Buffer,
        ResourceState::ShaderRead);
    auto faults = buffer("faults",
        zeroFaults.data(),
        sizeof(zeroFaults),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc);
    const std::uint32_t zeroLatch = 0;
    auto faultLatch = buffer("fault latch",
        nullptr,
        sizeof(zeroLatch),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst);
    assert(package->artifact().faultLatch.contractVersion == 1u);
    write(package->artifact().faultLatch.binding,
        0,
        DescriptorType::StorageBuffer,
        faultLatch->id(),
        DescriptorResourceKind::Buffer,
        ResourceState::ShaderWrite);
    auto faultReadback = buffer("fault readback",
        nullptr,
        sizeof(zeroLatch),
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu);
    {
        auto noCapacity =
            createVulkanRhiNativeCommandPayload(resources, diagnostic, {"no fault capacity", 0, 0});
        assert(noCapacity);
        assert(!noCapacity->recordBindlessFaultLatchClear(
            *faultLatch, ResourceState::Undefined, diagnostic));
        assert(diagnostic == "RHI-VULKAN-BINDLESS-FAULT-CLEAR-RETENTION-CAPACITY");
        assert(!noCapacity->scheduleBindlessFaultReadback(*faultLatch, *faultReadback, diagnostic));
        assert(diagnostic == "RHI-VULKAN-BINDLESS-FAULT-READBACK-CAPACITY");
        auto abandonedCommand =
            createVulkanRhiNativeCommandPayload(resources, diagnostic, {"abandoned fault", 0, 1});
        assert(abandonedCommand);
        auto abandoned = abandonedCommand->scheduleBindlessFaultReadback(
            *faultLatch, *faultReadback, diagnostic);
        assert(abandoned && abandoned->status() == BindlessFaultValidationStatus::Pending);
        abandonedCommand.reset();
        assert(abandoned->status() == BindlessFaultValidationStatus::Abandoned
            && !abandoned->validate().success);
        tests::VulkanHeadlessRuntime foreignRuntime(true);
        assert(foreignRuntime.runtime());
        auto foreignDesc = deviceDesc;
        foreignDesc.runtime = foreignRuntime.runtime();
        auto foreignDevice = createVulkanRhiDevice(foreignDesc);
        assert(foreignDevice);
        auto foreignLatch = foreignDevice->createBuffer({"foreign latch",
            sizeof(zeroLatch),
            ResourceUsage::StorageBuffer | ResourceUsage::CopySrc | ResourceUsage::CopyDst,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderWrite});
        assert(foreignLatch);
        auto foreignCommand =
            createVulkanRhiNativeCommandPayload(resources, diagnostic, {"foreign fault", 0, 1});
        assert(foreignCommand);
        assert(!foreignCommand->recordBindlessFaultLatchClear(
            *foreignLatch, ResourceState::Undefined, diagnostic));
        assert(diagnostic == "RHI-VULKAN-BINDLESS-FAULT-CLEAR-DEVICE-MISMATCH");
        assert(!foreignCommand->scheduleBindlessFaultReadback(
            *foreignLatch, *faultReadback, diagnostic));
        assert(diagnostic == "RHI-VULKAN-BINDLESS-FAULT-READBACK-DEVICE-MISMATCH");
    }
    auto readback = buffer("readback", nullptr, 768, ResourceUsage::CopyDst, MemoryUsage::GpuToCpu);
    auto pixels =
        buffer("pixel readback", nullptr, 512, ResourceUsage::CopyDst, MemoryUsage::GpuToCpu);
    auto depthPixels =
        buffer("depth readback", nullptr, 512, ResourceUsage::CopyDst, MemoryUsage::GpuToCpu);
    std::array<core::UniquePtr<GpuBuffer>, 2> constants, inputs, counters;
    std::array<core::UniquePtr<GpuTexture>, 2> outputs, images;
    std::array<core::UniquePtr<GpuTextureView>, 2> outputViews, imageViews;
    std::array<core::UniquePtr<GpuSampler>, 2> samplers;
    auto command =
        createVulkanRhiNativeCommandPayload(resources, diagnostic, {"indexed fixture", 3, 1});
    assert(command);
    for (std::uint32_t i = 0; i < 2; ++i) {
        const std::array<float, 4> data{
            {float(1 + i * 4), float(2 + i * 4), float(3 + i * 4), float(4 + i * 4)}};
        const std::uint32_t input = 10u * (i + 1), counter = 2u + i;
        constants[i] = buffer("uniform", data.data(), sizeof(data), ResourceUsage::UniformBuffer);
        inputs[i] = buffer("input", &input, 4, ResourceUsage::StorageBuffer);
        counters[i] =
            buffer("counter", &counter, 4, ResourceUsage::StorageBuffer | ResourceUsage::CopySrc);
        write(6,
            i,
            DescriptorType::UniformBuffer,
            constants[i]->id(),
            DescriptorResourceKind::Buffer,
            ResourceState::ShaderRead);
        write(8,
            i,
            DescriptorType::ReadOnlyStorageBuffer,
            inputs[i]->id(),
            DescriptorResourceKind::Buffer,
            ResourceState::ShaderRead);
        write(10,
            i,
            DescriptorType::StorageBuffer,
            counters[i]->id(),
            DescriptorResourceKind::Buffer,
            ResourceState::ShaderWrite);
        TextureDesc texture;
        texture.extent = {1, 1, 1};
        texture.format = TextureFormat::R32Float;
        texture.usage = TextureUsage::Storage | TextureUsage::CopySrc | TextureUsage::CopyDst;
        outputs[i] = device->createTexture(texture);
        assert(outputs[i]);
        TextureViewDesc view;
        view.format = texture.format;
        view.usage = texture.usage;
        outputViews[i] = device->createTextureView(*outputs[i], view);
        assert(outputViews[i]);
        write(4,
            i,
            DescriptorType::StorageTexture,
            outputViews[i]->id(),
            DescriptorResourceKind::TextureView,
            ResourceState::ShaderWrite);
        const auto output = vulkanRhiNativePayload(*outputs[i]);
        assert(command->recordImageBarrier(
            *output, {}, ResourceState::Undefined, ResourceState::CopyDst, diagnostic));
        assert(command->flushPendingBarriers(diagnostic));
        BufferToTextureCopyDesc copy;
        copy.sourceBuffer = upload->id();
        copy.destinationTexture = outputs[i]->id();
        copy.bytesPerRow = 256;
        copy.rowsPerImage = 1;
        copy.extent = {1, 1, 1};
        assert(command->recordBufferToImageCopy(
            *vulkanRhiNativePayload(*upload), *output, copy, diagnostic));
        assert(command->recordImageBarrier(
            *output, {}, ResourceState::CopyDst, ResourceState::ShaderWrite, diagnostic));
        texture.format = TextureFormat::Rgba8Unorm;
        texture.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
        texture.extent = {2, 2, 1};
        texture.mipLevelCount = 2;
        images[i] = device->createTexture(texture);
        assert(images[i]);
        view.format = texture.format;
        view.usage = TextureUsage::Sampled;
        view.mipLevelCount = 2;
        imageViews[i] = device->createTextureView(*images[i], view);
        SamplerDesc sampler;
        sampler.minFilter = sampler.magFilter = i ? SamplerFilter::Linear : SamplerFilter::Nearest;
        sampler.mipFilter = i ? SamplerMipFilter::Linear : SamplerMipFilter::Nearest;
        samplers[i] = device->createSampler(sampler);
        assert(images[i] && imageViews[i] && samplers[i]);
        TextureSubresourceRange allMips;
        allMips.mipLevelCount = 2;
        const auto image = vulkanRhiNativePayload(*images[i]);
        assert(command->recordImageBarrier(
            *image, allMips, ResourceState::Undefined, ResourceState::CopyDst, diagnostic));
        assert(command->flushPendingBarriers(diagnostic));
        for (unsigned mip = 0; mip < 2; ++mip) {
            copy.destinationTexture = images[i]->id();
            copy.bufferOffsetBytes = 256 + i * 768 + mip * 512;
            copy.textureSubresources.baseMipLevel = mip;
            copy.extent = {2u >> mip, 2u >> mip, 1};
            copy.rowsPerImage = 2u >> mip;
            assert(command->recordBufferToImageCopy(
                *vulkanRhiNativePayload(*upload), *image, copy, diagnostic));
        }
        assert(command->recordImageBarrier(
            *image, allMips, ResourceState::CopyDst, ResourceState::ShaderRead, diagnostic));
        write(0,
            i,
            DescriptorType::SampledTexture,
            imageViews[i]->id(),
            DescriptorResourceKind::TextureView,
            ResourceState::ShaderRead);
        write(2,
            i,
            DescriptorType::Sampler,
            samplers[i]->id(),
            DescriptorResourceKind::Sampler,
            ResourceState::Undefined);
    }
    write(12,
        0,
        DescriptorType::StorageBuffer,
        faults->id(),
        DescriptorResourceKind::Buffer,
        ResourceState::ShaderWrite);

    std::array<core::UniquePtr<GpuTexture>, 2> publicOutputs;
    std::array<core::UniquePtr<GpuTextureView>, 2> publicOutputViews;
    std::array<core::UniquePtr<GpuBuffer>, 2> publicCounters;
    for (std::uint32_t i = 0; i < 2u; ++i) {
        const std::uint32_t initialCounter = 2u + i;
        publicCounters[i] = buffer("public bindless counter",
            &initialCounter,
            sizeof(initialCounter),
            ResourceUsage::StorageBuffer | ResourceUsage::CopySrc);
        TextureDesc desc;
        desc.extent = {1u, 1u, 1u};
        desc.format = TextureFormat::R32Float;
        desc.usage = TextureUsage::Storage | TextureUsage::CopySrc;
        publicOutputs[i] = device->createTexture(desc);
        assert(publicOutputs[i]);
        TextureViewDesc view;
        view.format = desc.format;
        view.usage = desc.usage;
        publicOutputViews[i] = device->createTextureView(*publicOutputs[i], view);
        assert(publicOutputViews[i]);
    }
    std::array<std::uint32_t, 5> publicFaultOutputInitial{};
    auto publicFaultOutput = buffer("public bindless fault output",
        publicFaultOutputInitial.data(),
        sizeof(publicFaultOutputInitial),
        ResourceUsage::StorageBuffer | ResourceUsage::CopySrc);
    auto publicReadback = buffer("public bindless counter readback",
        nullptr,
        2u * sizeof(std::uint32_t),
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu);

    for (const auto& requirement : publicRequest.tables) {
        for (std::uint32_t i = 0; i < requirement.resourceCount; ++i) {
            const auto handle = publicSnapshot->handle(requirement.tableId, i);
            ValidationResult written;
            switch (requirement.tableId) {
            case 1u:
                written =
                    publicSnapshot->writeTexture(handle, *imageViews[i], ResourceState::ShaderRead);
                break;
            case 2u: {
                DescriptorBindingWrite descriptor;
                descriptor.type = DescriptorType::Sampler;
                descriptor.resource = samplers[i]->id();
                descriptor.resourceKind = DescriptorResourceKind::Sampler;
                written = publicSnapshot->write(handle, descriptor);
                break;
            }
            case 3u:
                written = publicSnapshot->writeTexture(
                    handle, *publicOutputViews[i], ResourceState::ShaderWrite);
                break;
            case 4u:
                written = publicSnapshot->writeBuffer(handle, *constants[i]);
                break;
            case 5u:
                written = publicSnapshot->writeBuffer(handle, *inputs[i]);
                break;
            case 6u:
                written = publicSnapshot->writeBuffer(handle, *publicCounters[i]);
                break;
            default:
                assert(false);
                break;
            }
            if (!written.success)
                std::cerr << written.diagnostics << '\n';
            assert(written.success);
        }
        if (requirement.resourceCount < requirement.binding.arrayCount) {
            const auto padding = publicSnapshot->initializeInactiveSlots(
                publicSnapshot->handle(requirement.tableId, 0u));
            if (!padding.success)
                std::cerr << padding.diagnostics << '\n';
            assert(padding.success);
        }
    }
    auto ordinaryFaultWrite = publicSnapshot->writeOrdinaryBuffer(12u, *publicFaultOutput);
    if (!ordinaryFaultWrite.success)
        std::cerr << ordinaryFaultWrite.diagnostics << '\n';
    assert(ordinaryFaultWrite.success && publicSnapshot->seal(*device).success);
    const auto authorization = extension->authorize(publicPackage, *publicSnapshot, diagnostic);
    if (!authorization)
        std::cerr << diagnostic << '\n';
    assert(authorization);
    auto publicPipeline = core::shareUnique(
        extension->createComputePipeline(authorization, "computeMain", diagnostic));
    if (!publicPipeline)
        std::cerr << diagnostic << '\n';
    assert(publicPipeline);
    assert(published.preparedComputeBindingContractVersion == 1u
        && published.preparedGraphicsBindingContractVersion == 1u
        && published.preparedGraphicsPassFaultScopeContractVersion == 0u);
    const auto computeBindingBytes = publicPlan.snapshotBytes + publicPackage->sourceBytes() + 1u;
    BindlessComputeBindingPool computeBindingPool(
        *extension, {1u, computeBindingBytes, computeBindingBytes, 1u, 1u});
    BindlessComputeBindingDesc computeBindingDesc{
        true, publicSnapshot, publicPipeline, publicPlan, publicSnapshot->generation()};
    auto wrongGeneration = computeBindingDesc;
    ++wrongGeneration.generation;
    assert(!computeBindingPool.prepare(wrongGeneration, diagnostic)
        && computeBindingPool.liveBindings() == 0u && computeBindingPool.chargedBytes() == 0u);
    auto preparedCompute = computeBindingPool.prepare(computeBindingDesc, diagnostic);
    if (!preparedCompute)
        std::cerr << diagnostic << '\n';
    assert(preparedCompute && preparedCompute->validatePayload().success
        && preparedCompute->argumentBufferCount() == 0u
        && preparedCompute->argumentBufferBytes() == 0u);
    assert(!computeBindingPool.prepare(computeBindingDesc, diagnostic)
        && diagnostic == "RHI-BINDLESS-PREPARED-COMPUTE-POOL-FULL"
        && computeBindingPool.liveBindings() == 1u);
    std::weak_ptr<const BindlessPreparedComputeBinding> preparedComputeObserver = preparedCompute;

    core::pmr::Vector<BindlessResourceAccess> declaredAccesses;
    declaredAccesses.reserve(16u);
    for (const auto& requirement : publicRequest.tables) {
        if ((requirement.binding.stages & ShaderStage::Compute) == ShaderStage::None)
            continue;
        for (std::uint32_t i = 0; i < requirement.resourceCount; ++i) {
            BindlessResourceAccess access;
            access.stages = requirement.binding.stages;
            access.pipelineStages = PipelineStage::ComputeShader;
            access.access = (static_cast<std::uint32_t>(requirement.operations)
                                & static_cast<std::uint32_t>(
                                    BindlessOperation::Load | BindlessOperation::Query))
                ? ResourceAccess::ShaderRead
                : ResourceAccess::None;
            if ((static_cast<std::uint32_t>(requirement.operations)
                    & static_cast<std::uint32_t>(
                        BindlessOperation::Store | BindlessOperation::Atomic))
                != 0u)
                access.access = access.access | ResourceAccess::ShaderWrite;
            access.state = (access.access & ResourceAccess::ShaderWrite) != ResourceAccess::None
                ? ResourceState::ShaderWrite
                : ResourceState::ShaderRead;
            if (requirement.binding.type == DescriptorType::StorageTexture) {
                access.resource = publicOutputs[i]->id();
                access.kind = BindlessAccessKind::Texture;
                access.textureRange = defaultTextureSubresourceRange(publicOutputs[i]->desc());
            } else {
                const GpuBuffer* resource = nullptr;
                switch (requirement.tableId) {
                case 4u:
                    resource = constants[i].get();
                    break;
                case 5u:
                    resource = inputs[i].get();
                    break;
                case 6u:
                    resource = publicCounters[i].get();
                    break;
                default:
                    assert(false);
                    break;
                }
                access.resource = resource->id();
                access.kind = BindlessAccessKind::Buffer;
                access.bufferSize = resource->desc().sizeBytes;
            }
            declaredAccesses.push_back(access);
        }
    }
    declaredAccesses.push_back(publicSnapshot->metadataAccess());
    declaredAccesses.push_back(publicSnapshot->faultLatchAccess());
    declaredAccesses.push_back(publicSnapshot->faultReadbackAccess());
    BindlessResourceAccess faultOutputAccess;
    faultOutputAccess.resource = publicFaultOutput->id();
    faultOutputAccess.kind = BindlessAccessKind::Buffer;
    faultOutputAccess.stages = ShaderStage::Compute;
    faultOutputAccess.pipelineStages = PipelineStage::ComputeShader;
    faultOutputAccess.access = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
    faultOutputAccess.state = ResourceState::ShaderWrite;
    faultOutputAccess.bufferSize = publicFaultOutput->desc().sizeBytes;
    declaredAccesses.push_back(faultOutputAccess);

    auto publicCommandEncoder = device->createCommandEncoder({"public bindless compute", 1u, 1u});
    assert(publicCommandEncoder);
    for (const auto& requirement : publicRequest.tables) {
        if ((requirement.binding.stages & ShaderStage::Compute) == ShaderStage::None)
            continue;
        for (std::uint32_t i = 0; i < requirement.resourceCount; ++i) {
            ResourceBarrierResult barrier;
            if (requirement.binding.type == DescriptorType::StorageTexture) {
                barrier = publicCommandEncoder->transition(
                    *publicOutputs[i], ResourceState::Undefined, ResourceState::ShaderWrite);
            } else {
                GpuBuffer* resource = requirement.tableId == 4u ? constants[i].get()
                    : requirement.tableId == 5u                 ? inputs[i].get()
                                                                : publicCounters[i].get();
                const auto after = requirement.binding.type == DescriptorType::UniformBuffer
                        || requirement.binding.type == DescriptorType::ReadOnlyStorageBuffer
                    ? ResourceState::ShaderRead
                    : ResourceState::ShaderWrite;
                barrier =
                    publicCommandEncoder->transition(*resource, ResourceState::Undefined, after);
            }
            assert(barrier.success);
        }
    }
    assert(publicCommandEncoder
            ->transition(*publicFaultOutput, ResourceState::Undefined, ResourceState::ShaderWrite)
            .success);
    auto publicPass = publicCommandEncoder->beginComputePass({"public bindless compute"});
    assert(publicPass);
    assert(publicPass->setPipeline(*publicPipeline).success);
    auto* publicBindlessPass =
        static_cast<BindlessPassExtension*>(publicPass->queryInterface(kExtensionBindlessCross));
    assert(publicBindlessPass
        && publicBindlessPass->recordingIdentity() == publicCommandEncoder->recordingIdentity());
    const BindlessAccessArray publicAccessView(declaredAccesses.data(), declaredAccesses.size());
    assert(!publicBindlessPass->bindPreparedComputeBinding({}, publicAccessView).success);
    // A native factory payload called outside the frontend pool has no issuance
    // receipt. It must not acquire recording authority merely by being native.
    auto* vulkanExtension = dynamic_cast<VulkanBindlessCrossExtension*>(extension);
    assert(vulkanExtension);
    auto unissuedCompute = vulkanExtension->prepareComputeBinding(preparedCompute->owners(),
        {1u, computeBindingBytes, computeBindingBytes, 1u, 1u},
        diagnostic);
    assert(unissuedCompute
        && !publicBindlessPass->bindPreparedComputeBinding(unissuedCompute, publicAccessView)
            .success);
    unissuedCompute.reset();
    assert(!publicBindlessPass
            ->bindPreparedComputeBinding(
                preparedCompute, BindlessAccessArray(declaredAccesses.data(), 1u))
            .success);
    const auto bound =
        publicBindlessPass->bindPreparedComputeBinding(preparedCompute, publicAccessView);
    if (!bound.success)
        std::cerr << bound.diagnostics << '\n';
    assert(bound.success);
    BindlessComputeBindingPool secondBindingPool(
        *extension, {1u, computeBindingBytes, computeBindingBytes, 1u, 1u});
    auto secondPrepared = secondBindingPool.prepare(computeBindingDesc, diagnostic);
    assert(secondPrepared);
    const auto capacityRefused =
        publicBindlessPass->bindPreparedComputeBinding(secondPrepared, publicAccessView);
    assert(!capacityRefused.success
        && capacityRefused.diagnostics == "RHI-BINDLESS-PREPARED-COMPUTE-COMMAND-BUDGET");
    secondPrepared.reset();
    assert(secondBindingPool.liveBindings() == 0u && secondBindingPool.chargedBytes() == 0u);
    const std::array<std::uint32_t, 3> indirectGroups{1u, 1u, 1u};
    auto rejectedIndirectArgs = buffer("RejectedIndexedIndirectDispatch",
        indirectGroups.data(),
        sizeof(indirectGroups),
        ResourceUsage::IndirectBuffer);
    const auto rejectedIndirect = publicPass->dispatchIndirect(*rejectedIndirectArgs, 0u);
    assert(!rejectedIndirect.success
        && rejectedIndirect.diagnostics == "RHI-BINDLESS-INDIRECT-DISPATCH-UNSUPPORTED");
    assert(publicPass->dispatch(1u, 1u, 1u).success);
    assert(publicPass->end().success);
    preparedCompute.reset();
    assert(!preparedComputeObserver.expired() && computeBindingPool.liveBindings() == 1u
        && computeBindingPool.chargedBytes() == computeBindingBytes);
    assert(publicCommandEncoder
            ->transition(*publicCounters[0], ResourceState::ShaderWrite, ResourceState::CopySrc)
            .success);
    if (publicRequest.tables.back().resourceCount > 1u)
        assert(publicCommandEncoder
                ->transition(*publicCounters[1], ResourceState::ShaderWrite, ResourceState::CopySrc)
                .success);
    assert(publicCommandEncoder
            ->transition(*publicReadback, ResourceState::Undefined, ResourceState::CopyDst)
            .success);
    auto publicTransfer = publicCommandEncoder->beginTransferPass({"bindless result readback"});
    assert(publicTransfer);
    assert(publicTransfer
            ->copyBufferToBuffer(
                {publicCounters[0]->id(), publicReadback->id(), 0u, 0u, sizeof(std::uint32_t)})
            .success);
    if (publicRequest.tables.back().resourceCount > 1u)
        assert(publicTransfer
                ->copyBufferToBuffer({publicCounters[1]->id(),
                    publicReadback->id(),
                    0u,
                    sizeof(std::uint32_t),
                    sizeof(std::uint32_t)})
                .success);
    assert(publicTransfer->end().success);
    auto publicCommandBuffer = publicCommandEncoder->finish();
    assert(publicCommandBuffer);
    assert(!preparedComputeObserver.expired() && computeBindingPool.liveBindings() == 1u);
    auto* publicFaultReports = static_cast<BindlessCommandBufferExtension*>(
        publicCommandBuffer->queryInterface(kExtensionBindlessCross));
    assert(publicFaultReports);
    auto pendingReport = publicFaultReports->faultReport(publicSnapshot->generation());
    assert(pendingReport.status == BindlessFaultValidationStatus::Pending
        && !pendingReport.validate().success);
    auto publicFence = device->createFence();
    assert(publicFence);
    auto* publicQueue = device->computeQueue();
    if (!publicQueue)
        publicQueue = &device->queue();
    const auto submitted = publicQueue->submit(*publicCommandBuffer, publicFence.get());
    if (!submitted.success)
        std::cerr << submitted.diagnostics << '\n';
    assert(submitted.success && submitted.hasAcceptedWork());
    assert(!preparedComputeObserver.expired() && computeBindingPool.liveBindings() == 1u);
    publicFence->wait();
    const auto completedReport = publicFaultReports->faultReport(publicSnapshot->generation());
    assert(preparedComputeObserver.expired() && computeBindingPool.liveBindings() == 0u
        && computeBindingPool.chargedBytes() == 0u);
    std::cout << "BINDLESS_VULKAN_PREPARED_COMPUTE_RETIRED 1\n";
    {
        auto cancelledBinding = computeBindingPool.prepare(computeBindingDesc, diagnostic);
        assert(cancelledBinding);
        std::weak_ptr<const BindlessPreparedComputeBinding> cancelledObserver = cancelledBinding;
        auto cancelledEncoder =
            device->createCommandEncoder({"unsubmitted prepared abandonment", 1u, 1u});
        auto cancelledPass =
            cancelledEncoder->beginComputePass({"unsubmitted prepared abandonment"});
        assert(cancelledPass && cancelledPass->setPipeline(*publicPipeline).success);
        auto* cancelledExtension = static_cast<BindlessPassExtension*>(
            cancelledPass->queryInterface(kExtensionBindlessCross));
        assert(cancelledExtension->bindPreparedComputeBinding(cancelledBinding, publicAccessView)
                .success);
        assert(cancelledPass->end().success);
        auto cancelledCommands = cancelledEncoder->finish();
        assert(cancelledCommands);
        cancelledBinding.reset();
        assert(!cancelledObserver.expired() && computeBindingPool.liveBindings() == 1u);
        cancelledPass.reset();
        cancelledEncoder.reset();
        assert(!cancelledObserver.expired() && computeBindingPool.liveBindings() == 1u);
        cancelledCommands.reset();
        assert(cancelledObserver.expired() && computeBindingPool.liveBindings() == 0u
            && computeBindingPool.chargedBytes() == 0u);
        auto reacquired = computeBindingPool.prepare(computeBindingDesc, diagnostic);
        assert(reacquired);
    }
    assert(computeBindingPool.liveBindings() == 0u && computeBindingPool.chargedBytes() == 0u);
    std::cout << "BINDLESS_VULKAN_PREPARED_ABANDONED 1\n";
    assert(completedReport.status == BindlessFaultValidationStatus::Faulted
        && completedReport.faultBits == 1u
        && completedReport.validate().diagnostics == "RHI-BINDLESS-GPU-INDEX-FAULT");
    const auto publicReadbackPayload = vulkanRhiNativePayload(*publicReadback);
    assert(publicReadbackPayload->backendInvalidateMappedRead(
        0u, 2u * sizeof(std::uint32_t), diagnostic));
    std::array<std::uint32_t, 2> publicCountersReadback{};
    std::memcpy(publicCountersReadback.data(),
        publicReadbackPayload->backendMappedData(),
        2u * sizeof(std::uint32_t));
    assert(publicCountersReadback[0] == 16u);
    if (publicRequest.tables.back().resourceCount > 1u)
        assert(publicCountersReadback[1] == 27u);

    BindlessRequirements graphicsRequest;
    graphicsRequest.optIn = true;
    graphicsRequest.allowEmulated = false;
    graphicsRequest.deviceIdentity = published.deviceIdentity;
    graphicsRequest.shaderPackageIdentity = publicGraphicsPackage->artifact().shaderPackageIdentity;
    graphicsRequest.budget = {1u, 32u, publicGraphicsPackage->artifact().snapshotBytes, 64u};
    for (const auto& mapping : publicGraphicsPackage->artifact().tables) {
        const auto binding =
            std::find_if(publicGraphicsPackage->artifact().physicalLayout.bindings.begin(),
                publicGraphicsPackage->artifact().physicalLayout.bindings.end(),
                [&](const auto& value) {
                    return value.binding == mapping.firstBinding;
                });
        assert(binding != publicGraphicsPackage->artifact().physicalLayout.bindings.end());
        BindlessTableRequirement requirement;
        requirement.tableId = mapping.tableId;
        requirement.binding = *binding;
        requirement.resourceCount = mapping.capacity;
        requirement.operations = mapping.operations;
        requirement.nonUniform = mapping.nonUniform;
        requirement.bufferLayout = mapping.bufferLayout;
        graphicsRequest.tables.push_back(requirement);
    }
    const auto graphicsPlan = extension->plan(graphicsRequest, publicGraphicsPackage->artifact());
    if (!graphicsPlan.success)
        std::cerr << graphicsPlan.diagnosticId << '\n';
    assert(graphicsPlan.success && graphicsPlan.executable
        && graphicsPlan.execution == BindlessExecution::Native);
    BindlessSnapshotPool graphicsSnapshots(*device, graphicsRequest.budget);
    auto graphicsCandidate =
        graphicsSnapshots.prepare(graphicsRequest, publicGraphicsPackage->artifact(), diagnostic);
    if (!graphicsCandidate)
        std::cerr << diagnostic << '\n';
    assert(graphicsCandidate);
    auto graphicsSnapshot = core::shareUnique(std::move(graphicsCandidate));

    std::array<core::UniquePtr<GpuTexture>, 2> graphicsImages;
    std::array<core::UniquePtr<GpuTextureView>, 2> graphicsImageViews;
    const std::array<std::array<std::uint8_t, 8>, 2> graphicsRows{
        {{{40, 90, 150, 255, 90, 130, 180, 255}}, {{150, 70, 50, 255, 180, 100, 80, 255}}}};
    auto graphicsUpload = device->createCommandEncoder({"bindless graphics texture upload"});
    assert(graphicsUpload);
    for (std::size_t i = 0; i < graphicsImages.size(); ++i) {
        TextureDesc desc;
        desc.extent = {2u, 2u, 1u};
        desc.format = TextureFormat::Rgba8Unorm;
        desc.usage = TextureUsage::Sampled | TextureUsage::CopyDst;
        graphicsImages[i] = device->createTexture(desc);
        assert(graphicsImages[i]);
        assert(graphicsUpload
                ->transition(*graphicsImages[i], ResourceState::Undefined, ResourceState::CopyDst)
                .success);
    }
    auto graphicsTextureUploadPass =
        graphicsUpload->beginTransferPass({"bindless graphics texture upload"});
    assert(graphicsTextureUploadPass);
    for (std::size_t i = 0; i < graphicsImages.size(); ++i) {
        std::array<std::uint8_t, 512> packedRows{};
        std::memcpy(packedRows.data(), graphicsRows[i].data(), 8u);
        std::memcpy(packedRows.data() + 256u, graphicsRows[i].data(), 8u);
        UploadTextureDesc uploadDesc;
        uploadDesc.label = "bindless graphics sampled texture";
        uploadDesc.destinationTexture = graphicsImages[i]->id();
        uploadDesc.extent = {2u, 2u, 1u};
        uploadDesc.format = TextureFormat::Rgba8Unorm;
        uploadDesc.bytesPerRow = 256u;
        uploadDesc.rowsPerImage = 2u;
        uploadDesc.subresources.mipLevelCount = 1u;
        uploadDesc.subresources.arrayLayerCount = 1u;
        uploadDesc.pixelData = packedRows.data();
        uploadDesc.pixelDataSize = static_cast<std::uint32_t>(packedRows.size());
        const auto uploaded = graphicsTextureUploadPass->uploadTexture(uploadDesc);
        if (!uploaded.success)
            std::cerr << uploaded.diagnostics << '\n';
        assert(uploaded.success);
        TextureViewDesc view;
        view.format = TextureFormat::Rgba8Unorm;
        view.usage = TextureUsage::Sampled;
        graphicsImageViews[i] = device->createTextureView(*graphicsImages[i], view);
        assert(graphicsImageViews[i]);
    }
    assert(graphicsTextureUploadPass->end().success);
    for (const auto& image : graphicsImages)
        assert(graphicsUpload->transition(*image, ResourceState::CopyDst, ResourceState::ShaderRead)
                .success);
    auto graphicsUploadCommands = graphicsUpload->finish();
    assert(graphicsUploadCommands);
    auto graphicsUploadFence = device->createFence();
    assert(graphicsUploadFence);
    const auto graphicsUploadSubmit =
        device->queue().submit(*graphicsUploadCommands, graphicsUploadFence.get());
    if (!graphicsUploadSubmit.success)
        std::cerr << graphicsUploadSubmit.diagnostics << '\n';
    assert(graphicsUploadSubmit.success && graphicsUploadSubmit.hasAcceptedWork());
    graphicsUploadFence->wait();

    for (const auto& requirement : graphicsRequest.tables) {
        for (std::uint32_t i = 0; i < requirement.resourceCount; ++i) {
            const auto handle = graphicsSnapshot->handle(requirement.tableId, i);
            ValidationResult written;
            if (requirement.binding.type == DescriptorType::SampledTexture) {
                written = graphicsSnapshot->writeTexture(
                    handle, *graphicsImageViews[i], ResourceState::ShaderRead);
            } else {
                assert(requirement.binding.type == DescriptorType::Sampler);
                DescriptorBindingWrite samplerWrite;
                samplerWrite.type = DescriptorType::Sampler;
                samplerWrite.resource = samplers[i]->id();
                samplerWrite.resourceKind = DescriptorResourceKind::Sampler;
                written = graphicsSnapshot->write(handle, samplerWrite);
            }
            if (!written.success)
                std::cerr << written.diagnostics << '\n';
            assert(written.success);
        }
    }
    assert(graphicsSnapshot->seal(*device).success);
    auto graphicsAuthorization =
        extension->authorize(publicGraphicsPackage, *graphicsSnapshot, diagnostic);
    if (!graphicsAuthorization)
        std::cerr << diagnostic << '\n';
    assert(graphicsAuthorization);
    RenderPipelineDesc publicGraphicsState;
    publicGraphicsState.label = "public indexed fragment sampling";
    publicGraphicsState.colorFormat = TextureFormat::Rgba8Unorm;
    publicGraphicsState.dynamicViewportScissor = true;
    publicGraphicsState.viewport = {0, 0, 2, 2, 0, 1};
    publicGraphicsState.scissor = {0, 0, 2, 2};
    auto publicGraphicsPipeline = core::shareUnique(extension->createRenderPipeline(
        graphicsAuthorization, publicGraphicsState, "vertexMain", "fragmentMain", diagnostic));
    if (!publicGraphicsPipeline)
        std::cerr << diagnostic << '\n';
    assert(publicGraphicsPipeline);
    const auto graphicsBindingBytes =
        graphicsPlan.snapshotBytes + publicGraphicsPackage->sourceBytes() + 1u;
    BindlessGraphicsBindingPool graphicsBindingPool(
        *extension, {1u, graphicsBindingBytes, graphicsBindingBytes, 1u, 1u});
    auto preparedGraphics = graphicsBindingPool.prepare({true,
                                                            graphicsSnapshot,
                                                            publicGraphicsPipeline,
                                                            graphicsPlan,
                                                            graphicsSnapshot->generation()},
        diagnostic);
    if (!preparedGraphics)
        std::cerr << diagnostic << '\n';
    assert(preparedGraphics && preparedGraphics->validatePayload().success);
    std::weak_ptr<const BindlessPreparedGraphicsBinding> preparedGraphicsObserver =
        preparedGraphics;

    std::array<BindlessResourceAccess, 5> graphicsAccesses{};
    for (std::size_t i = 0; i < graphicsImages.size(); ++i) {
        auto& access = graphicsAccesses[i];
        access.resource = graphicsImages[i]->id();
        access.kind = BindlessAccessKind::Texture;
        access.stages = ShaderStage::Vertex | ShaderStage::Fragment;
        access.pipelineStages = PipelineStage::VertexShader | PipelineStage::FragmentShader;
        access.access = ResourceAccess::ShaderRead;
        access.state = ResourceState::ShaderRead;
        access.textureRange = defaultTextureSubresourceRange(graphicsImages[i]->desc());
    }
    graphicsAccesses[2] = graphicsSnapshot->metadataAccess();
    graphicsAccesses[3] = graphicsSnapshot->faultLatchAccess();
    graphicsAccesses[4] = graphicsSnapshot->faultReadbackAccess();
    TextureDesc graphicsTargetDesc;
    graphicsTargetDesc.extent = {2u, 2u, 1u};
    graphicsTargetDesc.format = TextureFormat::Rgba8Unorm;
    graphicsTargetDesc.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
    auto graphicsTarget = device->createTexture(graphicsTargetDesc);
    auto graphicsPixels = buffer("public bindless graphics pixel readback",
        nullptr,
        512u,
        ResourceUsage::CopyDst,
        MemoryUsage::GpuToCpu);
    assert(graphicsTarget && graphicsPixels);
    auto graphicsEncoder = device->createCommandEncoder({"public bindless graphics draw", 1u, 1u});
    assert(graphicsEncoder);
    assert(graphicsEncoder
            ->transition(*graphicsTarget, ResourceState::Undefined, ResourceState::ColorAttachment)
            .success);
    auto publicRenderPassDesc = RenderPassDesc{"public bindless graphics pass"};
    RenderPassColorAttachmentDesc graphicsAttachment;
    graphicsAttachment.target = graphicsTarget->id();
    graphicsAttachment.format = graphicsTargetDesc.format;
    graphicsAttachment.loadOp = LoadOp::Clear;
    graphicsAttachment.storeOp = StoreOp::Store;
    publicRenderPassDesc.colorAttachments.push_back(graphicsAttachment);
    auto publicRenderPass = graphicsEncoder->beginRenderPass(publicRenderPassDesc);
    assert(publicRenderPass);
    assert(!publicRenderPass
            ->bindDescriptorSet(
                graphicsSnapshot->setIndex(), *graphicsSnapshot->descriptorSet(), nullptr, 0u)
            .success);
    assert(publicRenderPass->setPipeline(*publicGraphicsPipeline).success);
    auto* publicGraphicsBindless = static_cast<BindlessPassExtension*>(
        publicRenderPass->queryInterface(kExtensionBindlessCross));
    assert(publicGraphicsBindless
        && publicGraphicsBindless->recordingIdentity() == graphicsEncoder->recordingIdentity());
    assert(publicRenderPass->draw(3u, 1u, 0u, 0u).diagnostics
        == "RHI-BINDLESS-RECORD-SNAPSHOT-REQUIRED");
    auto mismatchedGraphicsPlan = graphicsPlan;
    ++mismatchedGraphicsPlan.identity;
    assert(!publicGraphicsBindless
            ->bindSnapshot(graphicsSnapshot,
                mismatchedGraphicsPlan,
                graphicsSnapshot->generation(),
                BindlessAccessArray(graphicsAccesses.data(), graphicsAccesses.size()))
            .success);
    auto mismatchedGraphicsPackage = graphicsPlan;
    ++mismatchedGraphicsPackage.shaderPackageIdentity;
    assert(!publicGraphicsBindless
            ->bindSnapshot(graphicsSnapshot,
                mismatchedGraphicsPackage,
                graphicsSnapshot->generation(),
                BindlessAccessArray(graphicsAccesses.data(), graphicsAccesses.size()))
            .success);
    assert(!publicGraphicsBindless
            ->bindSnapshot(graphicsSnapshot,
                graphicsPlan,
                graphicsSnapshot->generation(),
                BindlessAccessArray(graphicsAccesses.data(), 4u))
            .success);
    auto missingVertexAccesses = graphicsAccesses;
    missingVertexAccesses[0].stages = ShaderStage::Fragment;
    missingVertexAccesses[0].pipelineStages = PipelineStage::FragmentShader;
    const auto missingVertexAccess = publicGraphicsBindless->bindSnapshot(graphicsSnapshot,
        graphicsPlan,
        graphicsSnapshot->generation(),
        BindlessAccessArray(missingVertexAccesses.data(), missingVertexAccesses.size()));
    assert(!missingVertexAccess.success
        && missingVertexAccess.diagnostics == "RHI-BINDLESS-ACCESS-SET-INCOMPLETE");
    const auto graphicsBound = publicGraphicsBindless->bindPreparedGraphicsBinding(
        preparedGraphics, BindlessAccessArray(graphicsAccesses.data(), graphicsAccesses.size()));
    if (!graphicsBound.success)
        std::cerr << graphicsBound.diagnostics << '\n';
    assert(graphicsBound.success);
    const auto indirectRejected =
        publicRenderPass->drawIndexedIndirect(*graphicsPixels, 0u, 1u, 20u);
    assert(!indirectRejected.success
        && indirectRejected.diagnostics
            == (published.indirectFirstInstance
                    ? "RHI-BINDLESS-INDIRECT-ARGS-INVALID"
                    : "RHI-BINDLESS-INDIRECT-FIRST-INSTANCE-UNAVAILABLE"));
    assert(publicRenderPass->setViewport(publicGraphicsState.viewport).success);
    assert(publicRenderPass->setScissor(publicGraphicsState.scissor).success);
    assert(publicRenderPass->draw(3u, 1u, 0u, 0u).success);
    assert(publicRenderPass->end().success);
    preparedGraphics.reset();
    assert(!preparedGraphicsObserver.expired() && graphicsBindingPool.liveBindings() == 1u
        && graphicsBindingPool.chargedBytes() == graphicsBindingBytes);
    assert(graphicsEncoder
            ->transition(*graphicsTarget, ResourceState::ColorAttachment, ResourceState::CopySrc)
            .success);
    assert(graphicsEncoder
            ->transition(*graphicsPixels, ResourceState::Undefined, ResourceState::CopyDst)
            .success);
    auto graphicsReadbackPass =
        graphicsEncoder->beginTransferPass({"public bindless graphics pixel readback"});
    assert(graphicsReadbackPass);
    TextureToBufferCopyDesc graphicsCopy;
    graphicsCopy.sourceTexture = graphicsTarget->id();
    graphicsCopy.destinationBuffer = graphicsPixels->id();
    graphicsCopy.textureSubresources = defaultTextureSubresourceRange(graphicsTargetDesc);
    graphicsCopy.bytesPerRow = 256u;
    graphicsCopy.rowsPerImage = 2u;
    graphicsCopy.extent = graphicsTargetDesc.extent;
    assert(graphicsReadbackPass->copyTextureToBuffer(graphicsCopy).success);
    assert(graphicsReadbackPass->end().success);
    auto graphicsCommands = graphicsEncoder->finish();
    assert(graphicsCommands);
    auto* graphicsFaultReports = static_cast<BindlessCommandBufferExtension*>(
        graphicsCommands->queryInterface(kExtensionBindlessCross));
    assert(graphicsFaultReports);
    auto graphicsPending = graphicsFaultReports->faultReport(graphicsSnapshot->generation());
    assert(graphicsPending.status == BindlessFaultValidationStatus::Pending
        && !graphicsPending.validate().success);
    auto graphicsFence = device->createFence();
    assert(graphicsFence);
    const auto graphicsSubmit = device->queue().submit(*graphicsCommands, graphicsFence.get());
    if (!graphicsSubmit.success)
        std::cerr << graphicsSubmit.diagnostics << '\n';
    assert(graphicsSubmit.success && graphicsSubmit.hasAcceptedWork());
    assert(!preparedGraphicsObserver.expired() && graphicsBindingPool.liveBindings() == 1u);
    graphicsFence->wait();
    const auto graphicsFault = graphicsFaultReports->faultReport(graphicsSnapshot->generation());
    assert(preparedGraphicsObserver.expired() && graphicsBindingPool.liveBindings() == 0u
        && graphicsBindingPool.chargedBytes() == 0u);
    std::cout << "BINDLESS_VULKAN_PREPARED_GRAPHICS_RETIRED 1\n";
    assert(graphicsFault.status == BindlessFaultValidationStatus::Clean
        && graphicsFault.faultBits == 0u && graphicsFault.validate().success);
    const auto graphicsPixelsPayload = vulkanRhiNativePayload(*graphicsPixels);
    assert(graphicsPixelsPayload->backendInvalidateMappedRead(0u, 512u, diagnostic));
    const auto* graphicsPixelBytes =
        static_cast<const std::uint8_t*>(graphicsPixelsPayload->backendMappedData());
    bool graphicsPixelWritten = false;
    for (std::size_t y = 0; y < 2u; ++y) {
        for (std::size_t x = 0; x < 2u; ++x) {
            const auto* pixel = graphicsPixelBytes + y * 256u + x * 4u;
            assert(!(pixel[0] > 240u && pixel[1] < 16u && pixel[2] > 240u));
            graphicsPixelWritten =
                graphicsPixelWritten || pixel[0] != 0u || pixel[1] != 0u || pixel[2] != 0u;
        }
    }
    assert(graphicsPixelWritten);

    // The same package declares Fragment resources, but this selected pipeline
    // has only a Vertex entry. Prepared stage custody must reflect that entry.
    {
        auto publicDepthState = publicGraphicsState;
        publicDepthState.colorFormat = TextureFormat::Unknown;
        publicDepthState.depthStencil.format = TextureFormat::D32Float;
        publicDepthState.depthStencil.depthTestEnabled = true;
        publicDepthState.depthStencil.depthWriteEnabled = true;
        publicDepthState.depthStencil.depthCompare = CompareOp::LessEqual;
        auto publicDepthPipeline = core::shareUnique(extension->createRenderPipeline(
            graphicsAuthorization, publicDepthState, "vertexMain", "", diagnostic));
        if (!publicDepthPipeline)
            std::cerr << diagnostic << '\n';
        assert(publicDepthPipeline
            && publicDepthPipeline->bindlessShaderStages() == ShaderStage::Vertex);
        auto vertexBinding = graphicsBindingPool.prepare({true,
                                                             graphicsSnapshot,
                                                             publicDepthPipeline,
                                                             graphicsPlan,
                                                             graphicsSnapshot->generation()},
            diagnostic);
        assert(vertexBinding && vertexBinding->owners()->stages() == ShaderStage::Vertex);
        std::weak_ptr<const BindlessPreparedGraphicsBinding> vertexObserver = vertexBinding;
        auto vertexAccesses = graphicsAccesses;
        for (auto& access : vertexAccesses) {
            access.stages = ShaderStage::Vertex;
            if ((access.pipelineStages & PipelineStage::FragmentShader) != PipelineStage::None)
                access.pipelineStages = PipelineStage::VertexShader;
        }
        const BindlessAccessArray vertexAccessView(vertexAccesses.data(), vertexAccesses.size());
        assert(vertexBinding->validateForRecord(*publicDepthPipeline, vertexAccessView).success);
        TextureDesc targetDesc = graphicsTargetDesc;
        targetDesc.format = TextureFormat::D32Float;
        auto target = device->createTexture(targetDesc);
        auto pixels = buffer("public prepared depth readback",
            nullptr,
            512u,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu);
        assert(target && pixels);
        auto encoder = device->createCommandEncoder({"public prepared vertex-only draw", 1u, 1u});
        assert(encoder
            && encoder->transition(
                          *target, ResourceState::Undefined, ResourceState::DepthAttachment)
                .success);
        RenderPassDesc depthPassDesc{"public prepared vertex-only depth"};
        depthPassDesc.hasDepthStencilAttachment = true;
        depthPassDesc.depthStencilAttachment.target = target->id();
        depthPassDesc.depthStencilAttachment.format = targetDesc.format;
        depthPassDesc.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
        depthPassDesc.depthStencilAttachment.depthStoreOp = StoreOp::Store;
        auto pass = encoder->beginRenderPass(depthPassDesc);
        assert(pass && pass->setPipeline(*publicDepthPipeline).success);
        auto* indexed =
            static_cast<BindlessPassExtension*>(pass->queryInterface(kExtensionBindlessCross));
        const auto vertexBound =
            indexed->bindPreparedGraphicsBinding(vertexBinding, vertexAccessView);
        if (!vertexBound.success)
            std::cerr << vertexBound.diagnostics << '\n';
        assert(vertexBound.success && pass->setViewport(publicDepthState.viewport).success
            && pass->setScissor(publicDepthState.scissor).success);
        assert(pass->draw(3u, 1u, 0u, 0u).success && pass->end().success);
        vertexBinding.reset();
        assert(!vertexObserver.expired() && graphicsBindingPool.liveBindings() == 1u);
        assert(encoder->transition(*target, ResourceState::DepthAttachment, ResourceState::CopySrc)
                .success);
        assert(
            encoder->transition(*pixels, ResourceState::Undefined, ResourceState::CopyDst).success);
        auto transfer = encoder->beginTransferPass({"public prepared depth readback"});
        auto copy = graphicsCopy;
        copy.sourceTexture = target->id();
        copy.destinationBuffer = pixels->id();
        copy.textureSubresources = defaultTextureSubresourceRange(targetDesc);
        assert(transfer && transfer->copyTextureToBuffer(copy).success && transfer->end().success);
        auto commands = encoder->finish();
        auto* reports = static_cast<BindlessCommandBufferExtension*>(
            commands->queryInterface(kExtensionBindlessCross));
        auto fence = device->createFence();
        assert(
            commands && reports && fence && device->queue().submit(*commands, fence.get()).success);
        assert(!vertexObserver.expired() && graphicsBindingPool.liveBindings() == 1u);
        fence->wait();
        assert(reports->faultReport(graphicsSnapshot->generation()).validate().success);
        assert(vertexObserver.expired() && graphicsBindingPool.liveBindings() == 0u
            && graphicsBindingPool.chargedBytes() == 0u);
        const auto mapped = vulkanRhiNativePayload(*pixels);
        assert(mapped->backendInvalidateMappedRead(0u, 512u, diagnostic));
        const auto* bytes = static_cast<const std::uint8_t*>(mapped->backendMappedData());
        for (std::size_t y = 0u; y < 2u; ++y)
            for (std::size_t x = 0u; x < 2u; ++x) {
                float depth = -1.0f;
                std::memcpy(&depth, bytes + y * 256u + x * sizeof(float), sizeof(depth));
                assert(std::isfinite(depth) && std::fabs(depth) < 1e-6f);
            }
        std::cout << "BINDLESS_VULKAN_PREPARED_VERTEX_DEPTH_RETIRED 1\n";
    }

    if (published.indirectFirstInstance) {
        const std::uint32_t indices[] = {0u, 1u, 2u};
        const DrawIndexedIndirectCommand indirectRecord{3u, 1u, 0u, 0, 1u};
        auto indexBuffer = buffer(
            "public bindless index buffer", indices, sizeof(indices), ResourceUsage::IndexBuffer);
        auto indirectBuffer = buffer("public bindless indirect args",
            &indirectRecord,
            sizeof(indirectRecord),
            ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer);
        auto undeclaredIndirectBuffer = buffer("public bindless undeclared args",
            &indirectRecord,
            sizeof(indirectRecord),
            ResourceUsage::StorageBuffer | ResourceUsage::IndirectBuffer);
        auto indirectPixels = buffer("public bindless indirect pixels",
            nullptr,
            512u,
            ResourceUsage::CopyDst,
            MemoryUsage::GpuToCpu);
        auto indirectTarget = device->createTexture(graphicsTargetDesc);
        assert(indirectTarget && indexBuffer && indirectBuffer && undeclaredIndirectBuffer
            && indirectPixels);
        std::array<BindlessResourceAccess, 6> indirectAccesses{};
        std::copy(graphicsAccesses.begin(), graphicsAccesses.end(), indirectAccesses.begin());
        indirectAccesses[5].resource = indirectBuffer->id();
        indirectAccesses[5].kind = BindlessAccessKind::Buffer;
        indirectAccesses[5].pipelineStages = PipelineStage::DrawIndirect;
        indirectAccesses[5].access = ResourceAccess::IndirectCommandRead;
        indirectAccesses[5].state = ResourceState::ShaderRead;
        indirectAccesses[5].bufferSize = sizeof(indirectRecord);
        auto indirectEncoder =
            device->createCommandEncoder({"public bindless indirect", 1u, 1u, 1u});
        assert(indirectEncoder);
        assert(indirectEncoder
                ->transition(
                    *indirectTarget, ResourceState::Undefined, ResourceState::ColorAttachment)
                .success);
        assert(indirectEncoder
                ->transition(*indirectBuffer, ResourceState::Undefined, ResourceState::ShaderRead)
                .success);
        RenderPassDesc indirectDesc = publicRenderPassDesc;
        indirectDesc.colorAttachments[0].target = indirectTarget->id();
        auto indirectPass = indirectEncoder->beginRenderPass(indirectDesc);
        assert(indirectPass && indirectPass->setPipeline(*publicGraphicsPipeline).success);
        auto* indirectExtension = static_cast<BindlessPassExtension*>(
            indirectPass->queryInterface(kExtensionBindlessCross));
        assert(indirectExtension);
        assert(indirectExtension
                ->bindSnapshot(graphicsSnapshot,
                    graphicsPlan,
                    graphicsSnapshot->generation(),
                    BindlessAccessArray(indirectAccesses.data(), indirectAccesses.size()))
                .success);
        indirectAccesses[5].resource = graphicsPixels->id();
        assert(indirectPass
                   ->drawIndexedIndirect(*undeclaredIndirectBuffer, 0u, 1u, sizeof(indirectRecord))
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-ACCESS-INCOMPLETE");
        assert(indirectPass->drawIndexedIndirect(*indirectBuffer, 0u, 2u, sizeof(indirectRecord))
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-MULTIDRAW-UNAVAILABLE");
        assert(indirectPass->setViewport(publicGraphicsState.viewport).success);
        assert(indirectPass->setScissor(publicGraphicsState.scissor).success);
        assert(indirectPass->bindIndexBuffer(*indexBuffer, IndexFormat::Uint32, 0u).success);
        const auto indirectDraw =
            indirectPass->drawIndexedIndirect(*indirectBuffer, 0u, 1u, sizeof(indirectRecord));
        if (!indirectDraw.success)
            std::cerr << indirectDraw.diagnostics << '\n';
        assert(indirectDraw.success && indirectPass->end().success);
        assert(indirectEncoder
                ->transition(
                    *indirectTarget, ResourceState::ColorAttachment, ResourceState::CopySrc)
                .success);
        assert(indirectEncoder
                ->transition(*indirectPixels, ResourceState::Undefined, ResourceState::CopyDst)
                .success);
        auto indirectReadback =
            indirectEncoder->beginTransferPass({"bindless indirect pixel readback"});
        assert(indirectReadback);
        TextureToBufferCopyDesc indirectCopy = graphicsCopy;
        indirectCopy.sourceTexture = indirectTarget->id();
        indirectCopy.destinationBuffer = indirectPixels->id();
        assert(indirectReadback->copyTextureToBuffer(indirectCopy).success);
        assert(indirectReadback->end().success);
        auto indirectCommands = indirectEncoder->finish();
        assert(indirectCommands);
        auto* indirectFaults = static_cast<BindlessCommandBufferExtension*>(
            indirectCommands->queryInterface(kExtensionBindlessCross));
        assert(indirectFaults
            && indirectFaults->faultReport(graphicsSnapshot->generation()).status
                == BindlessFaultValidationStatus::Pending);
        auto indirectFence = device->createFence();
        assert(indirectFence);
        const auto indirectSubmit = device->queue().submit(*indirectCommands, indirectFence.get());
        if (!indirectSubmit.success)
            std::cerr << indirectSubmit.diagnostics << '\n';
        assert(indirectSubmit.success && indirectSubmit.hasAcceptedWork());
        indirectFence->wait();
        assert(indirectFaults->faultReport(graphicsSnapshot->generation()).status
            == BindlessFaultValidationStatus::Clean);
        const auto pixelPayload = vulkanRhiNativePayload(*indirectPixels);
        assert(pixelPayload->backendInvalidateMappedRead(0u, 512u, diagnostic));
        const auto* bytes = static_cast<const std::uint8_t*>(pixelPayload->backendMappedData());
        bool written = false;
        for (std::size_t y = 0; y < 2u; ++y)
            for (std::size_t x = 0; x < 2u; ++x) {
                const auto* pixel = bytes + y * 256u + x * 4u;
                written = written || pixel[0] != 0u || pixel[1] != 0u || pixel[2] != 0u;
            }
        assert(written);
        auto limitedEncoder =
            device->createCommandEncoder({"public bindless indirect budget", 1u, 1u});
        assert(limitedEncoder);
        assert(limitedEncoder
                ->transition(
                    *indirectTarget, ResourceState::CopySrc, ResourceState::ColorAttachment)
                .success);
        auto limitedPass = limitedEncoder->beginRenderPass(indirectDesc);
        assert(limitedPass && limitedPass->setPipeline(*publicGraphicsPipeline).success);
        auto* limitedExtension = static_cast<BindlessPassExtension*>(
            limitedPass->queryInterface(kExtensionBindlessCross));
        indirectAccesses[5].resource = indirectBuffer->id();
        assert(limitedExtension
                   ->bindSnapshot(graphicsSnapshot,
                       graphicsPlan,
                       graphicsSnapshot->generation(),
                       BindlessAccessArray(indirectAccesses.data(), indirectAccesses.size()))
                   .diagnostics
            == "RHI-BINDLESS-INDIRECT-ACCESS-BUDGET");
        std::cout << "BINDLESS_VULKAN_INDIRECT_EXECUTED 1\n";
    } else {
        std::cout << "BINDLESS_VULKAN_INDIRECT_EXECUTED 0\n";
    }

    auto descriptorPayload = vulkanRhiNativePayload(*descriptors);
    std::weak_ptr<VulkanRhiNativeResourcePayload> observedPipeline = pipeline.payload,
                                                  observedDescriptors = descriptorPayload;
    std::weak_ptr<VulkanRhiNativeResourcePayload> observedGraphics = graphics.payload;
    std::weak_ptr<VulkanRhiNativeResourcePayload> observedDepth = depthOnly.payload;
    assert(command->retainIndexedPipeline(pipeline.payload, diagnostic));
    assert(command->retainIndexedSnapshot(descriptorPayload, diagnostic));
    command->retainResource(vulkanRhiNativePayload(*upload));
    command->retainResource(vulkanRhiNativePayload(*readback));
    assert(command->retainIndexedPipeline(graphics.payload, diagnostic));
    command->retainResource(vulkanRhiNativePayload(*pixels));
    assert(command->retainIndexedPipeline(depthOnly.payload, diagnostic));
    command->retainResource(vulkanRhiNativePayload(*depthPixels));
    assert(command->flushPendingBarriers(diagnostic));
    assert(!command->recordBindlessFaultLatchClear(*faults, ResourceState::Undefined, diagnostic));
    assert(diagnostic == "RHI-VULKAN-BINDLESS-FAULT-CLEAR-BUFFER-INVALID");
    assert(!command->recordBindlessFaultLatchClear(
        *faultLatch, ResourceState::ShaderWrite, diagnostic));
    assert(diagnostic == "RHI-VULKAN-BINDLESS-FAULT-CLEAR-PRIOR-STATE");
    assert(
        command->recordBindlessFaultLatchClear(*faultLatch, ResourceState::Undefined, diagnostic));
    assert(
        !command->recordBindlessFaultLatchClear(*faultLatch, ResourceState::Undefined, diagnostic));
    assert(diagnostic == "RHI-VULKAN-BINDLESS-FAULT-CLEAR-ALREADY-RECORDED");
    assert(command->recordComputePipelineBind(*pipeline.payload, diagnostic));
    assert(command->recordDescriptorSetBind(
        *descriptorPayload, package->artifact().set, nullptr, 0, diagnostic));
    assert(command->recordDispatch(1, 1, 1, diagnostic));
    auto faultValidation =
        command->scheduleBindlessFaultReadback(*faultLatch, *faultReadback, diagnostic);
    assert(faultValidation && faultValidation->status() == BindlessFaultValidationStatus::Pending);
    auto copyBuffer = [&](const GpuBuffer& source, std::uint64_t offset, std::uint64_t size) {
        ResourceBarrierDesc barrier;
        barrier.resource = source.id();
        barrier.before = ResourceState::ShaderWrite;
        barrier.after = ResourceState::CopySrc;
        barrier.sourceStages = PipelineStage::ComputeShader;
        barrier.destinationStages = PipelineStage::Transfer;
        barrier.sourceAccess = ResourceAccess::ShaderWrite;
        barrier.destinationAccess = ResourceAccess::TransferRead;
        const auto payload = vulkanRhiNativePayload(source);
        assert(command->recordBufferBarrier(*payload, barrier, diagnostic)
            && command->flushPendingBarriers(diagnostic));
        assert(command->recordBufferCopy(*payload,
            *vulkanRhiNativePayload(*readback),
            {source.id(), readback->id(), 0, offset, size},
            diagnostic));
    };
    copyBuffer(*counters[0], 0, 4);
    copyBuffer(*counters[1], 4, 4);
    copyBuffer(*faults, 8, 20);
    for (unsigned i = 0; i < 2; ++i) {
        const auto image = vulkanRhiNativePayload(*outputs[i]);
        assert(command->recordImageBarrier(
            *image, {}, ResourceState::ShaderWrite, ResourceState::CopySrc, diagnostic));
        assert(command->flushPendingBarriers(diagnostic));
        TextureToBufferCopyDesc copy;
        copy.sourceTexture = outputs[i]->id();
        copy.destinationBuffer = readback->id();
        copy.bufferOffsetBytes = 256 + i * 256;
        copy.bytesPerRow = 256;
        copy.rowsPerImage = 1;
        copy.extent = {1, 1, 1};
        assert(command->recordImageToBufferCopy(
            *image, *vulkanRhiNativePayload(*readback), copy, diagnostic));
    }
    TextureDesc targetDesc;
    targetDesc.extent = {2, 2, 1};
    targetDesc.format = TextureFormat::Rgba8Unorm;
    targetDesc.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
    auto target = device->createTexture(targetDesc);
    assert(target);
    const auto targetPayload = vulkanRhiNativePayload(*target);
    command->retainResource(targetPayload);
    assert(command->recordImageBarrier(
        *targetPayload, {}, ResourceState::Undefined, ResourceState::ColorAttachment, diagnostic));
    assert(command->flushPendingBarriers(diagnostic));
    RenderPassDesc pass;
    RenderPassColorAttachmentDesc attachment;
    attachment.target = target->id();
    attachment.format = targetDesc.format;
    attachment.loadOp = LoadOp::Clear;
    attachment.storeOp = StoreOp::Store;
    pass.colorAttachments.push_back(attachment);
    core::pmr::Vector<std::shared_ptr<VulkanRhiNativeResourcePayload>> targets{targetPayload};
    assert(command->recordRenderPassBegin(pass, targets, nullptr, diagnostic));
    assert(command->recordGraphicsPipelineBind(*graphics.payload, diagnostic));
    assert(command->recordDescriptorSetBind(
        *descriptorPayload, package->artifact().set, nullptr, 0, diagnostic));
    assert(command->recordDraw(3, 1, 0, 0, diagnostic));
    assert(command->recordRenderPassEnd(diagnostic));
    assert(command->recordImageBarrier(
        *targetPayload, {}, ResourceState::ColorAttachment, ResourceState::CopySrc, diagnostic));
    assert(command->flushPendingBarriers(diagnostic));
    TextureToBufferCopyDesc pixelCopy;
    pixelCopy.sourceTexture = target->id();
    pixelCopy.destinationBuffer = pixels->id();
    pixelCopy.bytesPerRow = 256;
    pixelCopy.rowsPerImage = 2;
    pixelCopy.extent = {2, 2, 1};
    assert(command->recordImageToBufferCopy(
        *targetPayload, *vulkanRhiNativePayload(*pixels), pixelCopy, diagnostic));
    TextureDesc depthDesc;
    depthDesc.extent = {2, 2, 1};
    depthDesc.format = TextureFormat::D32Float;
    depthDesc.usage = TextureUsage::RenderAttachment | TextureUsage::CopySrc;
    auto depthTarget = device->createTexture(depthDesc);
    assert(depthTarget);
    const auto depthPayload = vulkanRhiNativePayload(*depthTarget);
    command->retainResource(depthPayload);
    const auto depthRange = defaultTextureSubresourceRange(depthDesc);
    assert(depthRange.aspect == TextureAspect::Depth);
    assert(command->recordImageBarrier(*depthPayload,
        depthRange,
        ResourceState::Undefined,
        ResourceState::DepthAttachment,
        diagnostic));
    assert(command->flushPendingBarriers(diagnostic));
    RenderPassDesc depthPass;
    depthPass.hasDepthStencilAttachment = true;
    depthPass.depthStencilAttachment.target = depthTarget->id();
    depthPass.depthStencilAttachment.format = depthDesc.format;
    depthPass.depthStencilAttachment.depthLoadOp = LoadOp::Clear;
    depthPass.depthStencilAttachment.depthStoreOp = StoreOp::Store;
    assert(command->recordRenderPassBegin(depthPass, {}, depthPayload.get(), diagnostic));
    assert(command->recordGraphicsPipelineBind(*depthOnly.payload, diagnostic));
    assert(command->recordDescriptorSetBind(
        *vulkanRhiNativePayload(*descriptors), package->artifact().set, nullptr, 0, diagnostic));
    assert(command->recordDraw(3, 1, 0, 0, diagnostic));
    assert(command->recordRenderPassEnd(diagnostic));
    assert(command->recordImageBarrier(*depthPayload,
        depthRange,
        ResourceState::DepthAttachment,
        ResourceState::CopySrc,
        diagnostic));
    assert(command->flushPendingBarriers(diagnostic));
    TextureToBufferCopyDesc depthCopy;
    depthCopy.sourceTexture = depthTarget->id();
    depthCopy.destinationBuffer = depthPixels->id();
    depthCopy.textureSubresources = depthRange;
    depthCopy.bytesPerRow = 256;
    depthCopy.rowsPerImage = 2;
    depthCopy.extent = {2, 2, 1};
    assert(command->recordImageToBufferCopy(
        *depthPayload, *vulkanRhiNativePayload(*depthPixels), depthCopy, diagnostic));
    pipeline = {};
    graphics = {};
    depthOnly = {};
    descriptorPayload.reset();
    descriptors.reset();
    arena.reset();
    constants = {};
    inputs = {};
    counters = {};
    outputs = {};
    outputViews = {};
    images = {};
    imageViews = {};
    samplers = {};
    faults.reset();
    upload.reset();
    metadata.reset();
    assert(!observedPipeline.expired() && !observedDescriptors.expired()
        && !observedGraphics.expired());
    assert(command->end(diagnostic));
    assert(command->submit(diagnostic));
    assert(faultValidation->status() == BindlessFaultValidationStatus::Pending);
    assert(command->waitSubmittedFenceUntil(
        std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
    assert(faultValidation->faultBits() == 1u);
    assert(faultValidation->status() == BindlessFaultValidationStatus::Faulted);
    assert(faultValidation->validate().diagnostics == "RHI-BINDLESS-GPU-INDEX-FAULT");
    capture.end();
    const auto payload = vulkanRhiNativePayload(*readback);
    assert(payload->backendInvalidateMappedRead(0, 768, diagnostic));
    std::array<std::uint32_t, 7> computed;
    std::array<float, 2> texels;
    std::memcpy(computed.data(), payload->backendMappedData(), 28);
    const auto latched = faultValidation->faultBits();
    for (unsigned i = 0; i < 2; ++i)
        std::memcpy(
            &texels[i], static_cast<const char*>(payload->backendMappedData()) + 256 + i * 256, 4);
    for (const auto value : computed)
        std::cout << value << ' ';
    std::cout << " latch=" << latched << " textures=" << texels[0] << ',' << texels[1] << '\n';
    const std::array<std::uint32_t, 7> expected = partial
        ? std::array<std::uint32_t, 7>{{16, 3, 15, 1, 1, 7, 31}}
        : std::array<std::uint32_t, 7>{{16, 27, 15, 26, 1, 7, 31}};
    assert(computed == expected && latched == 1u && texels[0] == 1.f
        && texels[1] == (partial ? 0.f : 5.f));
    const auto pixelPayload = vulkanRhiNativePayload(*pixels);
    assert(pixelPayload->backendInvalidateMappedRead(0, 512, diagnostic));
    const auto* rgba = static_cast<const std::uint8_t*>(pixelPayload->backendMappedData());
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 2; ++x) {
            const double u = (x + 0.5) / 2 * 0.75 + 0.1, v = (y + 0.5) / 2 * 0.75 + 0.1;
            const double tx = u * 2 - 0.5, ty = v * 2 - 0.5;
            std::cout << "pixel " << x << ',' << y << ':';
            for (unsigned c = 0; c < 4; ++c) {
                double level = colors[x][unsigned(v * 2) * 2 + unsigned(u * 2)][c];
                if (y) {
                    const auto& p = colors[x];
                    const double bilinear = (p[0][c] * (1 - tx) + p[1][c] * tx) * (1 - ty)
                        + (p[2][c] * (1 - tx) + p[3][c] * tx) * ty;
                    level = bilinear * 0.75 + mips[x][c] * 0.25;
                }
                const double expectedPixel = (2 * mips[x][c] + level) / 3;
                const auto actual = rgba[y * 256 + x * 4 + c];
                std::cout << ' ' << unsigned(actual);
                assert(std::abs(actual - expectedPixel) <= 2.0);
            }
            std::cout << '\n';
        }
    const auto depthReadback = vulkanRhiNativePayload(*depthPixels);
    assert(depthReadback->backendInvalidateMappedRead(0, 512, diagnostic));
    for (unsigned y = 0; y < 2; ++y)
        for (unsigned x = 0; x < 2; ++x) {
            float depth = 1.f;
            std::memcpy(&depth,
                static_cast<const char*>(depthReadback->backendMappedData()) + y * 256 + x * 4,
                4);
            std::cout << "depth " << x << ',' << y << ": " << depth << '\n';
            assert(std::abs(depth) < 0.00001f);
        }
    assert(observedPipeline.expired() && observedDescriptors.expired() && observedGraphics.expired()
        && observedDepth.expired());
    auto clearedCommand = createVulkanRhiNativeCommandPayload(
        resources, diagnostic, {"clear reused fault latch", 0, 1});
    assert(clearedCommand);
    assert(clearedCommand->recordBindlessFaultLatchClear(
        *faultLatch, ResourceState::CopySrc, diagnostic));
    auto cleared =
        clearedCommand->scheduleBindlessFaultReadback(*faultLatch, *faultReadback, diagnostic);
    assert(cleared && cleared->status() == BindlessFaultValidationStatus::Pending);
    assert(clearedCommand->end(diagnostic) && clearedCommand->submit(diagnostic));
    assert(clearedCommand->waitSubmittedFenceUntil(
        std::chrono::steady_clock::now() + std::chrono::seconds(5), diagnostic));
    assert(cleared->status() == BindlessFaultValidationStatus::Clean && cleared->faultBits() == 0u);
    assert(cleared->validate().success);
    std::cout << "reused fault latch clean after completed fence\n";
    faultLatch.reset();
    faultReadback.reset();
    clearedCommand.reset();
    command.reset();
    retireCompletedVulkanRhiSubmittedCommands();
    drainVulkanRhiSubmittedCommandsForShutdown();
    std::cout
        << "PASS: public Vulkan BindlessCross compute plus vertex/fragment graphics, fault reports and GPU readbacks; WOCC and other backends pending\n";
}
