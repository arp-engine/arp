#include "renderer/render_graph/frame_graph_compile_cache.h"

#include "renderer/render_graph/compiled_frame_graph.h"
#include "renderer/render_graph/render_graph.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace arp::render_graph {
namespace {
class Recipe final {
public:
    explicit Recipe(core::pmr::Vector<std::uint8_t>& bytes) : bytes_(bytes) { bytes_.clear(); }

    template <class T> void scalar(T value)
    {
        const auto fixed = static_cast<std::uint64_t>(value);
        const auto* p = reinterpret_cast<const std::uint8_t*>(&fixed);
        bytes_.insert(bytes_.end(), p, p + sizeof(fixed));
    }

    void text(std::string_view value)
    {
        scalar(value.size());
        const auto* p = reinterpret_cast<const std::uint8_t*>(value.data());
        bytes_.insert(bytes_.end(), p, p + value.size());
    }

    void number(float value)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        scalar(bits);
    }

    void range(const rhi::TextureSubresourceRange& v)
    {
        scalar(v.baseMipLevel);
        scalar(v.mipLevelCount);
        scalar(v.baseArrayLayer);
        scalar(v.arrayLayerCount);
        scalar(v.aspect);
    }

    void texture(const rhi::TextureDesc& v)
    {
        text(v.label);
        scalar(v.extent.width);
        scalar(v.extent.height);
        scalar(v.extent.depth);
        scalar(v.format);
        scalar(v.usage);
        scalar(v.memory);
        scalar(v.initialState);
        scalar(v.mipLevelCount);
        scalar(v.arrayLayerCount);
        scalar(v.sampleCount);
        scalar(v.cubeCompatible);
    }

    void buffer(const rhi::BufferDesc& v)
    {
        text(v.label);
        scalar(v.sizeBytes);
        scalar(v.usage);
        scalar(v.memory);
        scalar(v.initialState);
        scalar(v.hostVisibleDeviceLocal.contractVersion);
        scalar(v.hostVisibleDeviceLocal.prototypeOptIn);
    }

private:
    core::pmr::Vector<std::uint8_t>& bytes_;
};
} // namespace

FrameGraphCompileCache::FrameGraphCompileCache(rhi::GpuDevice& device, std::size_t maxEntries)
    : device_(device), capacity_(maxEntries)
{
    if (!capacity_)
        throw std::invalid_argument("RG-COMPILE-CACHE-ZERO-CAPACITY");
    entries_.reserve(capacity_);
}

bool FrameGraphCompileCache::makeKey(const FrameGraph& graph)
{
    const auto& encoder = graph.commandEncoderDesc_;
    if (graph.retirementSourceState_ || graph.originalIntentEpoch_ || graph.orderedExecution_
        || !graph.subgraphs_.empty() || !graph.subpassGraphs_.empty()
        || encoder.maxBindlessPipelines || encoder.maxBindlessSnapshots
        || encoder.maxBindlessIndirectAccesses || encoder.maxBindlessIndirectProcessors
        || encoder.maxBindlessGraphicsAccesses || encoder.retentionBudget || encoder.prototypeBatch)
        return false;
    Recipe key(key_);
    key.scalar(1u);
    key.text(encoder.label);
    key.scalar(graph.passCullingEnabled_);
    key.scalar(device_.transientAliasingSupported());
    key.scalar(std::getenv("WOCC_DISABLE_TRANSIENT_HEAP_ALIASING") != nullptr);
    key.scalar(graph.textures_.size());
    for (const auto& t : graph.textures_) {
        if (t.bundleTexture || t.originalAttachmentView || t.completedSampledUpload
            || t.completedSampledView || t.orderedSwapchainImage)
            return false;
        key.scalar(t.handle.id);
        key.texture(t.resolvedDesc());
        key.scalar(t.resolvedState());
        key.scalar(t.transient);
        key.scalar(t.imported);
        key.scalar(t.placed);
        key.scalar(t.presentableExternal);
        key.scalar(t.preserveInitialContents);
        key.scalar(t.externalFinalState);
        key.scalar(t.aliasScopeId);
        key.scalar(t.completedSampledStages);
    }
    key.scalar(graph.buffers_.size());
    for (const auto& b : graph.buffers_) {
        if (b.bundleBuffer || !b.resolveBuffer())
            return false;
        key.scalar(b.handle.id);
        key.buffer(b.resolveBuffer()->desc());
        key.scalar(b.resolveBuffer()->state());
        key.scalar(b.transient);
        key.scalar(b.imported());
        key.scalar(b.readOnly);
    }
    key.scalar(graph.passes_.size());
    for (const auto& p : graph.passes_) {
        if (p.subpassGroup || !p.subpassInputBindings.empty() || p.originalIntent
            || p.originalRenderPass || !p.rhiDrawCommands.empty() || !p.rhiDispatchCommands.empty()
            || !p.rhiTransferCommands.empty())
            return false;
        key.text(p.name);
        key.scalar(p.kind);
        key.scalar(p.queue);
        key.scalar(p.asyncComputeEligible);
        key.scalar(p.externalSideEffects);
        key.scalar(bool(p.callback));
        key.scalar(bool(p.renderPassCallback));
        const auto textures = [&](const auto& list) {
            key.scalar(list.size());
            for (const auto& use : list) {
                key.scalar(use.handle.id);
                key.scalar(use.state);
                key.scalar(use.stages);
                key.scalar(use.access);
                key.range(use.range);
            }
        };
        const auto buffers = [&](const auto& list) {
            key.scalar(list.size());
            for (const auto& use : list) {
                key.scalar(use.handle.id);
                key.scalar(use.state);
                key.scalar(use.stages);
                key.scalar(use.access);
                key.scalar(use.offset);
                key.scalar(use.size);
            }
        };
        textures(p.reads);
        textures(p.writes);
        buffers(p.bufferReads);
        buffers(p.bufferWrites);
        key.scalar(p.attachmentWrites.size());
        for (const auto& a : p.attachmentWrites) {
            key.scalar(a.handle.id);
            key.scalar(a.state);
            key.scalar(a.loadOp);
            key.scalar(a.storeOp);
            key.scalar(a.tileHints.discardAfterPass);
            key.scalar(a.tileHints.memorylessAttachment);
            key.scalar(a.tileHints.avoidStore);
            key.scalar(a.tileHints.preserveOnlyWhenSampledLater);
            key.number(a.clearColor.r);
            key.number(a.clearColor.g);
            key.number(a.clearColor.b);
            key.number(a.clearColor.a);
        }
    }
    return true;
}

core::SharedPtr<const CompiledFrameGraph> FrameGraphCompileCache::compile(FrameGraph& graph)
{
    if (&graph.device_ != &device_)
        throw std::invalid_argument("RG-COMPILE-CACHE-DEVICE-MISMATCH");
    if (const auto* id = graph.ownedResourceSourceDiagnostic())
        throw core::DiagnosticError(id);
    if (const auto* id = graph.sharedImportSourceDiagnostic())
        throw core::DiagnosticError(id);
    // A retained original source is never granted admission by cache history.
    if (!makeKey(graph)) {
        ++uncached_;
        return core::makeShared<CompiledFrameGraph>(graph.compile());
    }
    ++clock_;
    for (auto& entry : entries_)
        if (entry.key == key_) {
            ++hits_;
            entry.touched = clock_;
            return entry.compiled;
        }
    ++misses_;
    auto compiled = core::makeShared<CompiledFrameGraph>(graph.compile());
    Entry* entry = nullptr;
    if (entries_.size() < capacity_) {
        entries_.emplace_back();
        entry = &entries_.back();
    } else
        entry =
            &*std::min_element(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
                return a.touched < b.touched;
            });
    entry->key = key_;
    entry->compiled = compiled;
    entry->touched = clock_;
    return compiled;
}

void FrameGraphCompileCache::clear()
{
    entries_.clear();
    key_.clear();
    clock_ = 0;
}
} // namespace arp::render_graph
