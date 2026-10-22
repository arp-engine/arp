#include "rhi/frontend/prototype_command_batch_validation.h"

#include <cassert>

using namespace arp::rhi;

int main()
{
    using Contract = PrototypeCommandBatchContract;
    PrototypeCommandBatchBudget budget{Contract::version, true, 4, 4096, 1024, 1000000000};
    PrototypeCommandBatchCapabilities caps{
        Contract::version, PrototypeCommandBatchMode::Prototype, true, true, true, true};
    for (auto backend : {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGL}) {
        const auto denied = Contract::lowering(backend, budget, caps);
        assert(!denied.executable && denied.mode == PrototypeCommandBatchMode::DiagnosticFailure
            && denied.diagnostic);
    }
    const auto approved = Contract::lowering(Backend::WebGPU, budget, caps);
    assert(approved.executable && approved.mode == PrototypeCommandBatchMode::Prototype
        && !approved.diagnostic);
    for (int i = 0; i != 6; ++i) {
        auto missing = caps;
        if (i == 0)
            missing.contractVersion = 0;
        if (i == 1)
            missing.mode = PrototypeCommandBatchMode::DiagnosticFailure;
        if (i == 2)
            missing.boundedRecording = false;
        if (i == 3)
            missing.nativeCompletion = false;
        if (i == 4)
            missing.passBoundaryOrdering = false;
        if (i == 5)
            missing.acquiredImageLeaseValidation = false;
        assert(!Contract::lowering(Backend::WebGPU, budget, missing).executable);
    }
    for (std::uint32_t version : {0u, 1u, 3u}) {
        auto old = budget;
        old.contractVersion = version;
        assert(!Contract::lowering(Backend::WebGPU, old, caps).executable);
    }
    auto implicit = budget;
    implicit.optIn = false;
    assert(!Contract::lowering(Backend::WebGPU, implicit, caps).executable);
    assert(!Contract::lowering(Backend::WebGPU, budget, {}).executable);

    RenderPassDesc pass;
    assert(!semantics::PrototypeCommandBatchPassContract::validate(pass));
    pass.subpasses.resize(2);
    assert(semantics::PrototypeCommandBatchPassContract::validate(pass));
    pass.subpasses.clear();
    pass.layeredRendering = true;
    assert(semantics::PrototypeCommandBatchPassContract::validate(pass));

    using Lowering = semantics::PrototypeCommandBatchBarrierLowering;
    ResourceBarrierDesc b;
    b.resource = {1};
    b.sourceStages = PipelineStage::TopOfPipe;
    b.after = ResourceState::ShaderWrite;
    b.destinationStages = PipelineStage::ComputeShader;
    b.destinationAccess = ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite;
    assert(!Lowering::transition(b, false));
    b.before = ResourceState::ShaderWrite;
    b.sourceStages = PipelineStage::ComputeShader;
    b.sourceAccess = ResourceAccess::ShaderWrite;
    b.after = ResourceState::ShaderRead;
    b.destinationStages = PipelineStage::FragmentShader;
    b.destinationAccess = ResourceAccess::ShaderRead;
    assert(!Lowering::transition(b, false));
    auto valid = b;
    b.destinationStages = PipelineStage::ColorAttachmentOutput;
    assert(Lowering::transition(b, false));
    b = valid;
    b.before = ResourceState::CopyDst;
    b.sourceStages = PipelineStage::Transfer;
    b.sourceAccess = ResourceAccess::TransferWrite;
    assert(Lowering::transition(b, false));
    b = valid;
    b.after = b.before;
    assert(Lowering::transition(b, false));
    b = valid;
    b.after = ResourceState::Present;
    b.destinationStages = PipelineStage::Present;
    b.destinationAccess = ResourceAccess::PresentRead;
    assert(!Lowering::transition(b, true));
    assert(Lowering::transition(b, false));
    b = valid;
    b.before = ResourceState::Undefined;
    assert(Lowering::transition(b, true)); // discard cannot retain shader-write scope

    ResourceMemoryBarrierDesc m{{1},
        PipelineStage::ComputeShader,
        PipelineStage::ComputeShader,
        ResourceAccess::ShaderWrite,
        ResourceAccess::ShaderRead};
    assert(!Lowering::memory(m, ResourceState::ShaderWrite));
    assert(Lowering::memory(m, ResourceState::ShaderRead));
    m.destinationStages = PipelineStage::DrawIndirect;
    m.destinationAccess = ResourceAccess::IndirectCommandRead;
    assert(Lowering::memory(m, ResourceState::ShaderWrite));
    m.destinationStages = PipelineStage::FragmentShader;
    m.destinationAccess = ResourceAccess::ShaderRead | ResourceAccess::ColorAttachmentRead;
    assert(Lowering::memory(m, ResourceState::ShaderWrite));
}
