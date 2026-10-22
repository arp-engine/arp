#pragma once
#include "bindless_native_scene_execution_fixture.h"

namespace arp::test {
class BindlessNativeSceneRunnerFixture final {
public:
    template <class CompletedTextures,
        class Closure,
        class Pass,
        class Content,
        class DeviceFactory>
    static void run(const std::shared_ptr<const rhi::BindlessShaderPackage>& package,
        std::uint64_t nativeStagingCeiling,
        BindlessSceneAttachmentFixture::Observer observeAttachment,
        typename BindlessNativeSceneDataFixture<CompletedTextures>::SubmittedObserver
            observeSubmitted,
        unsigned maxTerminal,
        DeviceFactory&& createDevice,
        void (*captureScene)(const char*, bool) = nullptr)
    {
        using namespace rhi;
        using Contract = BindlessSceneConformanceContract;
        using Data = BindlessNativeSceneDataFixture<CompletedTextures>;
        assert(package && package->artifact().execution == BindlessExecution::Native);
        std::size_t entries = 0u;
        for (const auto& vertex : package->modules()) {
            if (vertex.push.stages != ShaderStage::Vertex)
                continue;
            const BindlessPackageModule* fragment = nullptr;
            Contract::EntrySelection selected;
            for (const auto& candidate : package->modules()) {
                const auto selection = Contract::selectEntries(vertex.entry, candidate.entry);
                if (!selection.recognized || selection.diagnostic)
                    continue;
                assert(!fragment);
                fragment = &candidate;
                selected = selection;
            }
            assert(fragment);
            const bool representative = selected.family == Contract::Family::Mesh
                || selected.family == Contract::Family::Terrain;
            for (unsigned scenario = 0u; scenario < (representative ? maxTerminal + 3u : 2u);
                ++scenario) {
                Data data;
                data.initializeNative = true;
                data.vertex = &vertex;
                data.family = selected.family;
                data.artifact = &package->artifact();
                data.draw.material = data.terrain() ? 0u : 1u;
                data.cancelBeforeSubmit = scenario == 1u;
                if (scenario >= 2u)
                    data.terminal = static_cast<typename Data::Terminal>(scenario - 1u);
                if (scenario == maxTerminal + 2u) {
                    data.terminal = Data::Terminal::MaterialRangeRejected;
                    data.draw.material = data.terrain() ? 1u : 2u;
                }
                data.observeAttachment = observeAttachment;
                data.observeSubmitted = observeSubmitted;
                data.mixed2DArrayTextures = true;
                auto device = createDevice();
                assert(device
                    && (device->backend() == Backend::Vulkan
                        || device->backend() == Backend::Metal));
                data.nativeStagingCeiling = nativeStagingCeiling;
                BindlessNativeSceneExecutionFixture<Closure, Pass, Content, Data> observer(data);
                if (captureScene && scenario == 0u)
                    captureScene(vertex.entry.c_str(), true);
                validateOriginalBindlessRootSource(package,
                    std::move(device),
                    "unused",
                    false,
                    observer,
                    data,
                    true,
                    fragment->entry,
                    vertex.entry,
                    selected.pass != 0u,
                    OrdinaryRootMutation::None,
                    TextureFormat::Rgba16Float,
                    true);
                if (captureScene && scenario == 0u)
                    captureScene(vertex.entry.c_str(), false);
            }
            ++entries;
        }
        assert(entries == 12u || entries == 3u);
        std::printf(
            "BINDLESS_NATIVE_SCENE_LIFECYCLE_OK entries=%zu public-factory original-owners cancellation-completion retirement no-visual-or-numeric-equivalence-grant\n",
            entries);
    }
};
} // namespace arp::test
