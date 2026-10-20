#include "../fixtures/bindless_graphics_instance64_package_fixture.h"
#include "../fixtures/bindless_graphics_matrix_package_fixture.h"
#include "../fixtures/bindless_graphics_pbr_material_package_fixture.h"
#include "../fixtures/bindless_graphics_push_package_fixture.h"
#include "../fixtures/bindless_graphics_terrain27_package_fixture.h"
#include "../fixtures/original_storage_root_closure_fixture.h"
#include "../fixtures/original_storage_texture_creation_fixture.h"
#include "../fixtures/webgpu_completed_texture_descriptor_fixture.h"
#include "../fixtures/webgpu_ordinary_color_closure_fixture.h"
#include "../fixtures/webgpu_ordinary_color_execution_data_fixture.h"
#include "../fixtures/webgpu_original_bindless_root_execution_fixture.h"
#include "../fixtures/webgpu_original_bindless_root_preparation_fixture.h"
#include "../fixtures/webgpu_original_completed_content_fixture.h"
#include "../fixtures/webgpu_original_completed_preparation_fixture.h"
#include "../fixtures/webgpu_original_device_fixture.h"
#include "../fixtures/webgpu_original_graphics_matrix_data_fixture.h"
#include "../fixtures/webgpu_original_graphics_push_data_fixture.h"
#include "../fixtures/webgpu_original_sampled_preparation_fixture.h"
#include "../fixtures/webgpu_original_storage_execution_fixture.h"
#include "../fixtures/webgpu_wocc_ibl_execution_data_fixture.h"
#include "rhi/backends/webgpu/webgpu_bindless_device_limit_plan.h"
#include "rhi/backends/webgpu/webgpu_device.h"

#include <emscripten/emscripten.h>

namespace {
EM_ASYNC_JS(int, requestPlannedDevice, (), {
    // clang-format off
    try {
        await Module.arpRequestPlannedWebGpuDevice(Module.arpBindlessRequiredLimits);
        const actual = Module.preinitializedWebGPUDevice.limits;
        for (const [name, value] of Object.entries(Module.arpBindlessRequiredLimits))
            if (actual[name] < value) throw new Error("Created device limit: " + name);
        Module.arpBindlessDevicePlan = {requiredLimits: Module.arpBindlessRequiredLimits,
            actualLimits: Object.fromEntries(Object.keys(Module.arpBindlessRequiredLimits)
                .map(name => [name, actual[name]])), createdBeforeRhi: true};
        return 1;
    } catch (error) {
        Module.printErr("Planned fixture device: " + error);
        return 0;
    }
    // clang-format on
});

void prepareTerrainDevice()
{
    using namespace arp;
    test::BindlessGraphicsTerrain27PackageFixture proof("/bindless-terrain27");
    core::pmr::String diagnostic;
    auto package = rhi::BindlessShaderPackage::load(
        "/bindless-terrain27/GraphicsTerrain27.emulated.bindless.yaml",
        {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 33u, 4u, &proof.request},
        diagnostic);
    if (!package)
        std::fprintf(stderr, "Terrain device package: %s\n", diagnostic.c_str());
    assert(package);
    std::array<std::uint32_t, 7u> facts{};
    assert(EM_ASM_INT(
        {
            // clang-format off
        if (Module.preinitializedWebGPUDevice || !Module.arpWebGpuAdapter) return 0;
        const names = (["maxBindGroups", "maxBindingsPerBindGroup",
            "maxSampledTexturesPerShaderStage", "maxSamplersPerShaderStage",
            "maxStorageTexturesPerShaderStage", "maxUniformBuffersPerShaderStage",
            "maxStorageBuffersPerShaderStage"]);
        for (let i = 0; i < names.length; ++i) {
            const value = Module.arpWebGpuAdapter.limits[names[i]];
            if (!Number.isSafeInteger(value) || value <= 0 || value > 0xffffffff) return 0;
            HEAPU32[($0 >>> 2) + i] = value;
        }
        return 1;
            // clang-format on
        },
        facts.data()));
    rhi_webgpu::WebGpuBindlessAdapterCapacity adapter{
        1u, true, facts[0], facts[1], {facts[2], facts[3], facts[4], facts[5], facts[6]}};
    rhi_webgpu::WebGpuBindlessDeviceLimitPlan plan;
    const auto* id =
        rhi_webgpu::WebGpuBindlessDeviceLimitPlanner::plan(package->artifact(), adapter, plan);
    if (id)
        std::fprintf(stderr, "Terrain device plan: %s\n", id);
    assert(!id && plan.contractVersion == 1u);
    EM_ASM({
        // clang-format off
        Module.arpBindlessRequiredLimits = {};
        // clang-format on
    });
    for (std::uint32_t i = 0u; i < plan.count; ++i)
        EM_ASM(
            {
                // clang-format off
                Module.arpBindlessRequiredLimits[UTF8ToString($0)] = $1;
                // clang-format on
            },
            plan.requiredLimits[i].name,
            plan.requiredLimits[i].value);
    assert(requestPlannedDevice());
    std::puts(
        "BINDLESS_TERRAIN27_DEVICE_PLAN_OK sealed-package queried-adapter actual-device before-rhi no-capability-grant");
}
} // namespace

int main()
{
    const bool pushOnly = EM_ASM_INT({
        // clang-format off
            return new URLSearchParams(location.search).has('original-root-push-execution-only');
        // clang-format on
    });
    const bool matrixOnly = EM_ASM_INT({
        // clang-format off
            return new URLSearchParams(location.search).has('original-root-matrix-execution-only');
        // clang-format on
    });
    const bool skinning256 = EM_ASM_INT({
        // clang-format off
            return new URLSearchParams(location.search).has('original-root-skinning256');
        // clang-format on
    });
    assert(!skinning256 || (matrixOnly && !pushOnly));
    const bool instance64 = EM_ASM_INT({
        // clang-format off
            return new URLSearchParams(location.search).has('original-root-instance64');
        // clang-format on
    });
    assert(!instance64 || (matrixOnly && !pushOnly && !skinning256));
    const bool pbrMaterial = EM_ASM_INT({
        // clang-format off
            return new URLSearchParams(location.search).has('original-root-pbr-material');
        // clang-format on
    });
    assert(!pbrMaterial || (matrixOnly && !pushOnly && !skinning256 && !instance64));
    const bool terrain27 = EM_ASM_INT({
        // clang-format off
        return new URLSearchParams(location.search).has('original-root-terrain27');
        // clang-format on
    });
    assert(!terrain27 || (matrixOnly && !pushOnly && !skinning256 && !instance64 && !pbrMaterial));
    if (terrain27)
        prepareTerrainDevice();
    const bool storageOnly = EM_ASM_INT({
        // clang-format off
            return new URLSearchParams(location.search).has('original-root-storage-execution-only');
        // clang-format on
    });
    const bool sampledOnly = EM_ASM_INT({
        // clang-format off
            return new URLSearchParams(location.search).has('original-root-sampled-execution-only');
        // clang-format on
    });
    const bool arrayOnly = EM_ASM_INT({
        // clang-format off
                return new URLSearchParams(location.search).has('original-root-array-execution-only');
        // clang-format on
    });
    const bool composedOnly = EM_ASM_INT({
        // clang-format off
            return new URLSearchParams(location.search).has('original-root-composed-execution-only');
        // clang-format on
    });
    const bool woccIbl = EM_ASM_INT({
        // clang-format off
            return new URLSearchParams(location.search).has('original-root-wocc-ibl');
        // clang-format on
    });
    assert(!woccIbl
        || (!matrixOnly && !pushOnly && !storageOnly && !sampledOnly && !arrayOnly
            && !composedOnly));
    arp::core::pmr::String diagnostic;
    auto package = arp::rhi::BindlessShaderPackage::load(
        "/bindless-rhi-fixture/original-root/MetalComputeBuffers.emulated.bindless.yaml",
        {2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u},
        diagnostic);
    assert(package);
    EM_ASM({
        // clang-format off
            var device = Module.preinitializedWebGPUDevice;
            device.pushErrorScope("out-of-memory");
            device.pushErrorScope("internal");
            device.pushErrorScope("validation");
            Module.originalRootPreparationScopeDone = false;
        // clang-format on
    });
    const bool geometryOnly = EM_ASM_INT({
        // clang-format off
        return new URLSearchParams(location.search).has('original-root-geometry-execution-only');
        // clang-format on
    });
    if (geometryOnly) {
        using namespace arp;
        using Execute = test::WebGpuOriginalBindlessInitializedGeometryExecutionFixture;
        for (const bool instance : {false, true}) {
            const auto path = core::pmr::String("/bindless-rhi-fixture/original-root/RootGeometry")
                + (instance ? "Instance" : "Vertex") + ".emulated.bindless.yaml";
            auto graphics = rhi::BindlessShaderPackage::load(
                path, {2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
            assert(graphics);
            for (unsigned scenario = 0u;
                scenario <= static_cast<unsigned>(Execute::Scenario::RetiredLifetime);
                ++scenario)
                for (const bool fault : {false, true}) {
                    Execute execute;
                    execute.instanceRate = instance;
                    execute.fault = fault;
                    execute.scenario = static_cast<Execute::Scenario>(scenario);
                    auto device = rhi_webgpu::createWebGpuRhiDevice(
                        test::WebGpuOriginalDeviceFixture::describe(
                            "initialized bindless Geometry1",
                            64u * 1024u * 1024u,
                            4096u,
                            4u * 1024u * 1024u));
                    assert(device);
                    test::validateOriginalBindlessRootSource(graphics,
                        std::move(device),
                        "",
                        false,
                        execute,
                        execute,
                        true,
                        fault ? "faultFragment" : "fragmentMain",
                        "vertexMain");
                    std::printf(
                        "BINDLESS_WEBGPU_INITIALIZED_GEOMETRY_EXECUTION_OK rate%s scenario%u fault%u Geometry1 Root5 Content9 lifecycle12 same-root-journal budgets-returned\n",
                        instance ? "Instance" : "Vertex",
                        scenario,
                        fault ? 1u : 0u);
                    for (auto& actor : execute.initialized->actors)
                        actor.reset();
                    for (const auto& q : execute.initialized->quotas)
                        assert(!q.metadata->liveBytes() && !q.payload->liveBytes()
                            && !q.resource->liveBytes() && !q.lease->liveBytes());
                }
        }
    } else if (woccIbl) {
        using namespace arp;
        using Model = test::OriginalGraphicsWoccIblDataFixture;
        Model::inputs = test::WoccIblFrozenInputsFixture::load(
            "/wocc-ibl-inputs/manifest.yaml", test::WoccIblFrozenInputsFixture::payloadBytes);
        const auto run = [&](const auto& data, const char* path, bool reference) {
            using Data = std::decay_t<decltype(data)>;
            auto shader = rhi::BindlessShaderPackage::load(
                path, {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u}, diagnostic);
            assert(shader);
            using Execute = test::WebGpuOriginalBindlessRootExecutionFixtureImpl<true,
                true,
                false,
                false,
                false,
                Data,
                true>;
            for (unsigned scenario = 0u; scenario
                <= (reference ? 0u : static_cast<unsigned>(Execute::Scenario::RetiredLifetime));
                ++scenario)
                for (const bool fault : {false, true}) {
                    if (reference && fault)
                        continue;
                    Execute execute;
                    execute.fault = fault;
                    execute.scenario = static_cast<typename Execute::Scenario>(scenario);
                    auto device = rhi_webgpu::createWebGpuRhiDevice(
                        test::WebGpuOriginalDeviceFixture::describe("frozen actual-size WOCC IBL",
                            64u * 1024u * 1024u,
                            4096u,
                            4u * 1024u * 1024u));
                    assert(device);
                    test::validateOriginalBindlessRootSource(shader,
                        std::move(device),
                        "",
                        false,
                        execute,
                        execute,
                        true,
                        fault ? "faultFragment" : "cleanFragment",
                        "vertexMain");
                }
        };
        run(test::WebGpuWoccIblExecutionDataFixture<true>{},
            "/bindless-graphics-wocc-ibl-reference/GraphicsWoccIblReference.emulated.bindless.yaml",
            true);
        run(test::WebGpuWoccIblExecutionDataFixture<false>{},
            "/bindless-graphics-wocc-ibl/GraphicsWoccIbl.emulated.bindless.yaml",
            false);
        Model::inputs.reset();
    } else if (pushOnly) {
        using namespace arp;
        const std::filesystem::path publication =
            "/bindless-push-memory/GraphicsMatrixPush.emulated.bindless.yaml";
        test::BindlessGraphicsPushPackageFixture proof(publication, "emulated");
        for (std::size_t i = 0u; i < proof.entries.size(); ++i)
            proof.entries[i].target = &proof.targetRequests[i];
        rhi::BindlessPackageLoadBudget options{
            4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, 16u, 4u};
        options.push = &proof.request;
        auto push = rhi::BindlessShaderPackage::load(publication, options, diagnostic);
        if (!push)
            std::fprintf(stderr, "Push package: %s\n", diagnostic.c_str());
        assert(push);
        const auto run = [&](const auto& data) {
            using Execute = test::WebGpuOriginalBindlessRootExecutionFixtureImpl<true,
                false,
                false,
                false,
                true,
                std::decay_t<decltype(data)>>;
            for (unsigned scenario = 0u;
                scenario <= static_cast<unsigned>(Execute::Scenario::RetiredLifetime);
                ++scenario)
                for (const bool fault : {false, true}) {
                    Execute execute;
                    execute.fault = fault;
                    execute.scenario = static_cast<Execute::Scenario>(scenario);
                    auto device = rhi_webgpu::createWebGpuRhiDevice(
                        test::WebGpuOriginalDeviceFixture::describe(
                            "public Graphics push execution",
                            64u * 1024u * 1024u,
                            4096u,
                            4u * 1024u * 1024u));
                    assert(device && device->backend() == rhi::Backend::WebGPU);
                    test::validateOriginalBindlessRootSource(push,
                        std::move(device),
                        "",
                        false,
                        execute,
                        execute,
                        true,
                        "fragmentMain",
                        "vertexMain");
                }
        };
        run(test::WebGpuOriginalGraphicsPushDataFixture{});
        run(test::WebGpuOriginalGraphicsPushMultiDataFixture{});
    } else if (matrixOnly) {
        using namespace arp;
        const auto run = [&](const auto& data,
                             const char* path,
                             const rhi::BindlessPackageMemoryLoadRequest& proof) {
            using Data = std::decay_t<decltype(data)>;
            using Model = typename Data::DataModel;
            const std::filesystem::path directory = path;
            auto matrices = rhi::BindlessShaderPackage::load(
                directory / (std::string(Model::packageName) + ".emulated.bindless.yaml"),
                {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 4u, Data::bindingCount, 4u, &proof},
                diagnostic);
            if (!matrices)
                std::fprintf(stderr, "%s package: %s\n", Model::packageName, diagnostic.c_str());
            assert(matrices);
            using Execute = test::WebGpuOriginalBindlessRootExecutionFixtureImpl<true,
                Data::sampled,
                Data::terrain,
                false,
                !Data::sampled,
                Data>;
            for (unsigned scenario = 0u;
                scenario <= static_cast<unsigned>(Execute::Scenario::RetiredLifetime);
                ++scenario)
                for (const bool fault : {false, true}) {
                    Execute execute;
                    if constexpr (Data::sampled)
                        execute.initializeTexturePixels = Model::fillTexture;
                    execute.fault = fault;
                    execute.scenario = static_cast<Execute::Scenario>(scenario);
                    auto device = rhi_webgpu::createWebGpuRhiDevice(
                        test::WebGpuOriginalDeviceFixture::describe(
                            Model::packageName, 64u * 1024u * 1024u, 4096u, 4u * 1024u * 1024u));
                    assert(device && device->backend() == rhi::Backend::WebGPU);
                    test::validateOriginalBindlessRootSource(matrices,
                        std::move(device),
                        "",
                        false,
                        execute,
                        execute,
                        true,
                        fault ? "faultFragment" : "cleanFragment",
                        "vertexMain");
                }
        };
        if (terrain27) {
            test::BindlessGraphicsTerrain27PackageFixture proof("/bindless-terrain27");
            run(test::WebGpuOriginalGraphicsTerrain27DataFixture{},
                "/bindless-terrain27",
                proof.request);
        } else if (pbrMaterial) {
            test::BindlessGraphicsPbrMaterialPackageFixture proof("/bindless-pbr-material");
            run(test::WebGpuOriginalGraphicsPbrMaterialDataFixture{},
                "/bindless-pbr-material",
                proof.request);
        } else if (instance64) {
            test::BindlessGraphicsInstance64PackageFixture proof("/bindless-instance64");
            run(test::WebGpuOriginalGraphicsInstance64DataFixture{},
                "/bindless-instance64",
                proof.request);
        } else if (skinning256) {
            test::BindlessGraphicsMatrixPackageFixture proof("/bindless-skinning256",
                "GraphicsSkinning256",
                {"    float4x4 skinMatrices[kMaxGpuSkinningJoints];",
                    "    SkinningPalette current;",
                    "    SkinningPalette previous;"});
            run(test::WebGpuOriginalGraphicsSkinning256DataFixture{},
                "/bindless-skinning256",
                proof.request);
        } else {
            test::BindlessGraphicsMatrixPackageFixture proof("/bindless-graphics-matrix");
            run(test::WebGpuOriginalGraphicsMatrixDataFixture{},
                "/bindless-graphics-matrix",
                proof.request);
        }
    } else {
        if (!storageOnly && !sampledOnly && !arrayOnly && !composedOnly)
            arp::test::OriginalStorageTextureCreationFixture::verifyDevice(
                arp::rhi_webgpu::createWebGpuRhiDevice(
                    arp::test::WebGpuOriginalDeviceFixture::describe(
                        "original storage texture creation7",
                        64u * 1024u * 1024u,
                        4096u,
                        4u * 1024u * 1024u)),
                [](const arp::rhi::DescriptorSet& set) {
                    assert(!arp::rhi_webgpu::webGpuNativePayloadFor(set));
                },
                [](auto& device, const auto& root, const auto& inventory) {
                    arp::test::OriginalStorageRootClosureFixture::verify<
                        arp::rhi_webgpu::WebGpuBindlessRootNativeClosure>(device, root, inventory);
                });
        if (storageOnly || (!sampledOnly && !arrayOnly && !composedOnly)) {
            for (unsigned terminal = 0u; terminal != (storageOnly ? 11u : 1u); ++terminal)
                for (const bool fault : {false, true}) {
                    using Execute = arp::test::WebGpuOriginalStorageExecutionFixture;
                    Execute::terminal = terminal;
                    Execute::fault = fault;
                    arp::test::OriginalStorageTextureCreationFixture::verifyDevice(
                        arp::rhi_webgpu::createWebGpuRhiDevice(
                            arp::test::WebGpuOriginalDeviceFixture::describe(
                                "original storage execution10",
                                64u * 1024u * 1024u,
                                4096u,
                                4u * 1024u * 1024u)),
                        [](const arp::rhi::DescriptorSet& set) {
                            assert(!arp::rhi_webgpu::webGpuNativePayloadFor(set));
                        },
                        nullptr,
                        &Execute::execute);
                }
        }
        if (!storageOnly) {
            if (composedOnly) {
                auto composed = arp::rhi::BindlessShaderPackage::load(
                    "/bindless-rhi-fixture/original-root/MetalComputeDomains.emulated.bindless.yaml",
                    {2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                    diagnostic);
                assert(composed);
                for (unsigned scenario = 0u;
                    scenario <= static_cast<unsigned>(
                        arp::test::WebGpuOriginalBindlessComposedExecutionFixture::Scenario::
                            RetiredLifetime);
                    ++scenario)
                    for (const bool fault : {false, true}) {
                        arp::test::WebGpuOriginalBindlessComposedExecutionFixture execute;
                        execute.fault = fault;
                        execute.scenario = static_cast<decltype(execute.scenario)>(scenario);
                        auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                            arp::test::WebGpuOriginalDeviceFixture::describe(
                                "original composed Compute execution",
                                64u * 1024u * 1024u,
                                4096u,
                                4u * 1024u * 1024u));
                        assert(device);
                        arp::test::validateOriginalBindlessRootSource(composed,
                            std::move(device),
                            fault ? "faultMain" : "computeMain",
                            true,
                            arp::test::OriginalBindlessRootFixtureObserver{},
                            execute);
                    }
                arp::test::WebGpuOriginalBindlessComposedExecutionFixture reject;
                reject.rejectAlias = true;
                reject.preparedRefusal = arp::core::makeShared<
                    arp::test::OriginalBindlessPreparedBindingRefusalReceipt>();
                auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                    arp::test::WebGpuOriginalDeviceFixture::describe(
                        "original composed alias refusal",
                        64u * 1024u * 1024u,
                        4096u,
                        4u * 1024u * 1024u));
                assert(device);
                arp::test::validateOriginalBindlessRootSource(composed,
                    std::move(device),
                    "computeMain",
                    true,
                    arp::test::OriginalBindlessRootFixtureObserver{},
                    reject);
                reject.preparedRefusal->verifyRetired();
                std::puts(
                    "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPOSED_ALIAS_REFUSED_OK before-record no-publication quotas-zero");
                auto graphics = arp::rhi::BindlessShaderPackage::load(
                    "/bindless-rhi-fixture/original-root/GraphicsDomains.emulated.bindless.yaml",
                    {2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                    diagnostic);
                assert(graphics);
                for (unsigned scenario = 0u;
                    scenario <= static_cast<unsigned>(
                        arp::test::WebGpuOriginalBindlessComposedGraphicsExecutionFixture::
                            Scenario::RetiredLifetime);
                    ++scenario)
                    for (const bool fault : {false, true}) {
                        arp::test::WebGpuOriginalBindlessComposedGraphicsExecutionFixture execute;
                        execute.fault = fault;
                        execute.scenario = static_cast<decltype(execute.scenario)>(scenario);
                        auto graphicsDevice = arp::rhi_webgpu::createWebGpuRhiDevice(
                            arp::test::WebGpuOriginalDeviceFixture::describe(
                                "original composed Vertex Fragment execution",
                                64u * 1024u * 1024u,
                                4096u,
                                4u * 1024u * 1024u));
                        assert(graphicsDevice);
                        arp::test::validateOriginalBindlessRootSource(graphics,
                            std::move(graphicsDevice),
                            "",
                            false,
                            execute,
                            execute,
                            true,
                            fault ? "faultFragment" : "cleanFragment",
                            "vertexMain");
                    }
            }
            if (!sampledOnly && !arrayOnly && !composedOnly) {
                const auto executeOrdinaryColor = [&](const auto& data) {
                    using Data = std::decay_t<decltype(data)>;
                    using Model = typename Data::DataModel;
                    const auto path = std::string("/bindless-graphics-")
                        + (std::is_same_v<Model, arp::test::OriginalGraphicsHdrGiDataFixture>
                                ? "hdr-gi/"
                                : "color-gi/")
                        + Model::packageName + ".emulated.bindless.yaml";
                    auto package = arp::rhi::BindlessShaderPackage::load(path,
                        {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                        diagnostic);
                    assert(package);
                    using Execute = arp::test::WebGpuOriginalBindlessRootExecutionFixtureImpl<true,
                        true,
                        false,
                        false,
                        false,
                        Data,
                        true>;
                    for (unsigned scenario = 0u;
                        scenario <= static_cast<unsigned>(Execute::Scenario::RetiredLifetime);
                        ++scenario)
                        for (const bool fault : {false, true}) {
                            Execute execute;
                            execute.fault = fault;
                            execute.scenario = static_cast<typename Execute::Scenario>(scenario);
                            auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                                arp::test::WebGpuOriginalDeviceFixture::describe(Model::packageName,
                                    64u * 1024u * 1024u,
                                    4096u,
                                    4u * 1024u * 1024u));
                            assert(device);
                            arp::test::validateOriginalBindlessRootSource(package,
                                std::move(device),
                                "",
                                false,
                                execute,
                                execute,
                                true,
                                fault ? "faultFragment" : "cleanFragment",
                                "vertexMain");
                        }
                };
                executeOrdinaryColor(arp::test::WebGpuOrdinaryColorExecutionDataFixture<
                    arp::test::OriginalGraphicsColorGiDataFixture,
                    false>{});
                executeOrdinaryColor(arp::test::WebGpuOrdinaryColorExecutionDataFixture<
                    arp::test::OriginalGraphicsHdrGiDataFixture,
                    true>{});
                for (const bool hdr : {false, true}) {
                    auto gi = arp::rhi::BindlessShaderPackage::load(hdr
                            ? "/bindless-graphics-hdr-gi/GraphicsHdrGi.emulated.bindless.yaml"
                            : "/bindless-graphics-color-gi/GraphicsColorGi.emulated.bindless.yaml",
                        {4u * 1024u * 1024u, 32u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                        diagnostic);
                    assert(gi);
                    auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                        arp::test::WebGpuOriginalDeviceFixture::describe(
                            "canonical ordinary color Closure17",
                            64u * 1024u * 1024u,
                            4096u,
                            4u * 1024u * 1024u));
                    assert(device);
                    if (hdr)
                        arp::test::validateOriginalBindlessRootSource(gi,
                            std::move(device),
                            "",
                            false,
                            arp::test::WebGpuOrdinaryColorClosureFixture{},
                            arp::test::WebGpuOrdinaryColorResourcesFixture<
                                arp::test::OriginalGraphicsHdrGiDataFixture,
                                true>{},
                            true,
                            "cleanFragment",
                            "vertexMain");
                    else
                        arp::test::validateOriginalBindlessRootSource(gi,
                            std::move(device),
                            "",
                            false,
                            arp::test::WebGpuOrdinaryColorClosureFixture{},
                            arp::test::WebGpuOrdinaryColorResourcesFixture<
                                arp::test::OriginalGraphicsColorGiDataFixture,
                                false>{},
                            true,
                            "cleanFragment",
                            "vertexMain");
                }
                arp::test::WebGpuCompletedTextureDescriptorFixture::verify([] {
                    return arp::rhi_webgpu::createWebGpuRhiDevice(
                        arp::test::WebGpuOriginalDeviceFixture::describe(
                            "completed texture descriptor publication2",
                            64u * 1024u * 1024u,
                            4096u,
                            4u * 1024u * 1024u));
                });
                for (const auto* family : {"CompletedGraphicsRoot", "RootGraphicsArray"}) {
                    auto path = arp::core::pmr::String("/bindless-rhi-fixture/original-root/")
                        + family + ".emulated.bindless.yaml";
                    auto graphics = arp::rhi::BindlessShaderPackage::load(path,
                        {2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                        diagnostic);
                    assert(graphics);
                    const bool array = arp::core::StringView(family) == "RootGraphicsArray";
                    if (!array) {
                        auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                            arp::test::WebGpuOriginalDeviceFixture::describe(
                                "original completed sampled Graphics preparation",
                                64u * 1024u * 1024u,
                                4096u,
                                4u * 1024u * 1024u));
                        assert(device);
                        arp::test::validateOriginalBindlessRootSource(graphics,
                            std::move(device),
                            "",
                            false,
                            arp::test::WebGpuOriginalSampledPreparationFixture{true},
                            arp::test::WebGpuOriginalCompletedPreparationRequest{},
                            false,
                            "fragmentMain",
                            "vertexMain");
                        auto contentDevice = arp::rhi_webgpu::createWebGpuRhiDevice(
                            arp::test::WebGpuOriginalDeviceFixture::describe(
                                "original completed sampled Content9",
                                64u * 1024u * 1024u,
                                4096u,
                                4u * 1024u * 1024u));
                        assert(contentDevice);
                        arp::test::validateOriginalBindlessRootSource(graphics,
                            std::move(contentDevice),
                            "",
                            false,
                            arp::test::WebGpuOriginalCompletedContentFixture{},
                            arp::test::WebGpuOriginalCompletedContentRequest{},
                            true,
                            "fragmentMain",
                            "vertexMain");
                    }
                    for (unsigned variant = 0u; variant < (array ? 2u : 1u); ++variant) {
                        auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                            arp::test::WebGpuOriginalDeviceFixture::describe(
                                "original sampled Graphics preparation",
                                64u * 1024u * 1024u,
                                4096u,
                                4u * 1024u * 1024u));
                        assert(device);
                        arp::test::validateOriginalBindlessRootSource(graphics,
                            std::move(device),
                            "",
                            false,
                            arp::test::WebGpuOriginalSampledPreparationFixture{},
                            arp::test::WebGpuOriginalSampledPreparationRequest{},
                            false,
                            array ? (variant ? "faultFragment" : "cleanFragment") : "fragmentMain",
                            "vertexMain");
                    }
                }
                for (const auto* family : {"RootFragmentBuffers", "RootFragmentRecords"}) {
                    auto path = arp::core::pmr::String("/bindless-rhi-fixture/original-root/")
                        + family + ".emulated.bindless.yaml";
                    auto graphics = arp::rhi::BindlessShaderPackage::load(path,
                        {2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                        diagnostic);
                    assert(graphics);
                    for (const auto* fragment : {"cleanFragment", "faultFragment"}) {
                        auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                            arp::test::WebGpuOriginalDeviceFixture::describe(
                                "original bindless Graphics preparation",
                                64u * 1024u * 1024u,
                                4096u,
                                4u * 1024u * 1024u));
                        assert(device);
                        arp::test::validateOriginalBindlessRootSource(graphics,
                            std::move(device),
                            "",
                            false,
                            arp::test::WebGpuOriginalBindlessGraphicsPreparationFixture{},
                            arp::test::WebGpuOriginalBindlessGraphicsPreparationRequest{},
                            false,
                            fragment,
                            "vertexUnindexed");
                    }
                    for (const bool finiteRanges : {true, false})
                        for (unsigned scenario = 0u;
                            scenario <= static_cast<unsigned>(
                                arp::test::WebGpuOriginalBindlessGraphicsExecutionFixture::
                                    Scenario::RetiredLifetime);
                            ++scenario)
                            for (const bool fault : {false, true}) {
                                arp::test::WebGpuOriginalBindlessGraphicsExecutionFixture execute;
                                execute.records =
                                    arp::core::StringView(family) == "RootFragmentRecords";
                                execute.finiteRanges = finiteRanges;
                                execute.fault = fault;
                                execute.scenario =
                                    static_cast<decltype(execute.scenario)>(scenario);
                                auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                                    arp::test::WebGpuOriginalDeviceFixture::describe(
                                        "original bindless Fragment Graphics execution",
                                        64u * 1024u * 1024u,
                                        4096u,
                                        4u * 1024u * 1024u));
                                assert(device);
                                arp::test::validateOriginalBindlessRootSource(graphics,
                                    std::move(device),
                                    "",
                                    false,
                                    execute,
                                    execute,
                                    true,
                                    fault ? "faultFragment" : "cleanFragment",
                                    "vertexUnindexed");
                            }
                }
                for (const auto* entry : {"cleanMain", "faultMain"}) {
                    const auto desc = arp::test::WebGpuOriginalDeviceFixture::describe(
                        "original bindless Root preparation",
                        64u * 1024u * 1024u,
                        4096u,
                        4u * 1024u * 1024u);
                    auto device = arp::rhi_webgpu::createWebGpuRhiDevice(desc);
                    assert(device && device->backend() == arp::rhi::Backend::WebGPU);
                    arp::test::validateOriginalBindlessRootSource(package,
                        std::move(device),
                        entry,
                        true,
                        arp::test::WebGpuOriginalBindlessPreparationFixture{
                            arp::core::StringView(entry) == "faultMain"},
                        arp::test::WebGpuOriginalBindlessPreparationRequest{});
                    std::printf("BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_ENTRY_OK %s\n", entry);
                }
                for (unsigned scenario = 0u;
                    scenario <= static_cast<unsigned>(arp::test::
                            WebGpuOriginalBindlessRootExecutionFixture::Scenario::RetiredLifetime);
                    ++scenario) {
                    for (const bool fault : {false, true}) {
                        arp::test::WebGpuOriginalBindlessRootExecutionFixture execute;
                        execute.fault = fault;
                        execute.scenario = static_cast<decltype(execute.scenario)>(scenario);
                        auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                            arp::test::WebGpuOriginalDeviceFixture::describe(
                                "original bindless Root execution",
                                64u * 1024u * 1024u,
                                4096u,
                                4u * 1024u * 1024u));
                        assert(device);
                        arp::test::validateOriginalBindlessRootSource(package,
                            std::move(device),
                            fault ? "faultMain" : "cleanMain",
                            true,
                            arp::test::OriginalBindlessRootFixtureObserver{},
                            execute);
                    }
                }
            }
            if (!arrayOnly && !composedOnly) {
                auto graphics = arp::rhi::BindlessShaderPackage::load(
                    "/bindless-rhi-fixture/original-root/CompletedGraphicsRoot.emulated.bindless.yaml",
                    {2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                    diagnostic);
                assert(graphics);
                for (unsigned scenario = 0u;
                    scenario <= static_cast<unsigned>(
                        arp::test::WebGpuOriginalBindlessSampledExecutionFixture::Scenario::
                            RetiredLifetime);
                    ++scenario)
                    for (const bool fault : {false, true}) {
                        arp::test::WebGpuOriginalBindlessSampledExecutionFixture execute;
                        execute.fault = fault;
                        execute.scenario = static_cast<decltype(execute.scenario)>(scenario);
                        auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                            arp::test::WebGpuOriginalDeviceFixture::describe(
                                "original sampled Fragment Graphics execution",
                                64u * 1024u * 1024u,
                                4096u,
                                4u * 1024u * 1024u));
                        assert(device);
                        arp::test::validateOriginalBindlessRootSource(graphics,
                            std::move(device),
                            "",
                            false,
                            execute,
                            execute,
                            true,
                            fault ? "faultFragment" : "fragmentMain",
                            "vertexMain");
                    }
            }
            if (arrayOnly) {
                auto graphics = arp::rhi::BindlessShaderPackage::load(
                    "/bindless-rhi-fixture/original-root/RootGraphicsArray.emulated.bindless.yaml",
                    {2u * 1024u * 1024u, 16u * 1024u * 1024u, 64u, 16u, 256u, 64u},
                    diagnostic);
                assert(graphics);
                for (unsigned scenario = 0u;
                    scenario <= static_cast<unsigned>(arp::test::
                            WebGpuOriginalBindlessArrayExecutionFixture::Scenario::RetiredLifetime);
                    ++scenario)
                    for (const bool fault : {false, true}) {
                        arp::test::WebGpuOriginalBindlessArrayExecutionFixture execute;
                        execute.fault = fault;
                        execute.scenario = static_cast<decltype(execute.scenario)>(scenario);
                        auto device = arp::rhi_webgpu::createWebGpuRhiDevice(
                            arp::test::WebGpuOriginalDeviceFixture::describe(
                                "original array Fragment Graphics execution",
                                64u * 1024u * 1024u,
                                4096u,
                                4u * 1024u * 1024u));
                        assert(device);
                        arp::test::validateOriginalBindlessRootSource(graphics,
                            std::move(device),
                            "",
                            false,
                            execute,
                            execute,
                            true,
                            fault ? "faultFragment" : "cleanFragment",
                            "vertexMain");
                    }
            }
        }
    }
    EM_ASM({
        // clang-format off
            var device = Module.preinitializedWebGPUDevice;
            Promise.all([ device.popErrorScope(), device.popErrorScope(), device.popErrorScope() ])
                .then(function(errors) {
                    Module.originalRootPreparationScopePassed = errors.every(function(error) {
                        return !error;
                    });
                    Module.originalRootPreparationScopeDone = true;
                    errors.forEach(function(error) {
                        if (error)
                            Module.printErr("original Root outer scope: " + error.message);
                    });
                })
                .catch(function(error) {
                    Module.originalRootPreparationScopePassed = false;
                    Module.originalRootPreparationScopeDone = true;
                    Module.printErr("original Root outer scope: " + String(error));
                });
        // clang-format on
    });
    for (std::uint32_t i = 0u; i < 10000u
        && !EM_ASM_INT(
            {
                // clang-format off
                    return !!Module.originalRootPreparationScopeDone;
                // clang-format on
            });
        ++i)
        emscripten_sleep(1);
    assert(EM_ASM_INT({
        // clang-format off
            return !!Module.originalRootPreparationScopeDone && !!Module.originalRootPreparationScopePassed;
        // clang-format on
    }));
    std::puts(geometryOnly
            ? "BINDLESS_WEBGPU_INITIALIZED_GEOMETRY_RETIRE_OK original-owners-expired budgets-zero scopes3-drained general-execution-closed"
            : woccIbl
            ? "BINDLESS_WEBGPU_WOCC_IBL_RETIRE_OK frozen655328 original-owners-expired budgets-zero scopes3-drained"
            : pushOnly
            ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PUSH_RETIRE_OK original-owners-expired budgets-zero scopes3-drained general-execution-closed"
            : matrixOnly
            ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_MATRIX_RETIRE_OK original-owners-expired budgets-zero scopes3-drained general-execution-closed"
            : storageOnly
            ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_STORAGE_RETIRE_OK original-owners-expired budgets-zero scopes3-drained general-execution-closed"
            : composedOnly
            ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_COMPOSED_RETIRE_OK original-owners-expired budgets-zero scopes3-drained general-execution-closed"
            : arrayOnly
            ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_ARRAY_RETIRE_OK original-owners-expired budgets-zero scopes3-drained general-execution-closed"
            : sampledOnly
            ? "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_SAMPLED_RETIRE_OK original-owners-expired budgets-zero scopes3-drained general-execution-closed"
            : "BINDLESS_RHI_ORIGINAL_ROOT_WEBGPU_PREPARATION_RETIRE_OK original-owners-expired budgets-zero scopes3-drained public-execution-closed");
    return 0;
}
