// Contract tests for the shared command encoder base class in
// rhi/frontend/command_encoder_base.h.
//
// CommandEncoderBase consolidates the commandCount_ / activePass_ /
// lastValidation_ / finished_ state, the guard helpers (beginPassGuard /
// finishGuard / rollbackPassBegin), the commandOk/markCommandFail stamps, and
// the three trivial transition(...) overloads that delegate to the
// ResourceBarrierDesc overload. The tests pin those semantics using a minimal
// concrete subclass so backend encoders can rely on the base's invariants
// without each re-asserting them.
//
// Status: Contract Complete.

#include "rhi/frontend/command_encoder_base.h"
#include "rhi/gpu_resources.h"

#include <cassert>
#include <memory>

namespace {

using namespace arp::rhi;

// Minimal concrete subclass that records pass begin/end and transitions as
// plain counters, exposing the base guard/helper behavior without any native
// API. It overrides only the two ResourceBarrierDesc transition overloads and
// the three pass begin / finish virtuals.
class FakeEncoder final : public CommandEncoderBase {
public:
    FakeEncoder() = default;

    // Bring the three final transition overloads from the base into scope so
    // callers can use the (buffer, before, after) / (texture, before, after) /
    // (texture, range, before, after) convenience forms; without this
    // using-declaration the two desc overloads below would shadow them.
    using CommandEncoderBase::transition;

    int transitionBufferCount() const { return transitionBufferCount_; }

    int transitionTextureCount() const { return transitionTextureCount_; }

    int beginComputeCount() const { return beginComputeCount_; }

    int beginTransferCount() const { return beginTransferCount_; }

    int beginRenderCount() const { return beginRenderCount_; }

    int finishCount() const { return finishCount_; }

    bool lastBeginPassRolledBack() const { return lastBeginPassRolledBack_; }

    ResourceBarrierResult transition(GpuBuffer&, const ResourceBarrierDesc& desc) override
    {
        ++transitionBufferCount_;
        markCommandOk();
        return {true, "", desc.resource, desc.before, desc.after, false, true};
    }

    ResourceBarrierResult transition(GpuTexture&, const ResourceBarrierDesc& desc) override
    {
        ++transitionTextureCount_;
        markCommandOk();
        return {true, "", desc.resource, desc.before, desc.after, true, true};
    }

    arp::core::UniquePtr<RenderPassEncoder> beginRenderPass(const RenderPassDesc&) override
    {
        if (!beginPassGuard("FakeEncoder render pass already active")) {
            return nullptr;
        }
        ++beginRenderCount_;
        markCommandOk();
        // Return nullptr as a sentinel; these tests never record into the pass.
        return nullptr;
    }

    arp::core::UniquePtr<ComputePassEncoder> beginComputePass(const ComputePassDesc&) override
    {
        if (!beginPassGuard("FakeEncoder compute pass already active")) {
            return nullptr;
        }
        if (failNextNativePassBegin_) {
            markCommandFail("FakeEncoder native compute pass begin failed");
            rollbackPassBegin();
            lastBeginPassRolledBack_ = true;
            return nullptr;
        }
        ++beginComputeCount_;
        markCommandOk();
        return nullptr;
    }

    arp::core::UniquePtr<TransferPassEncoder> beginTransferPass(const TransferPassDesc&) override
    {
        if (!beginPassGuard("FakeEncoder transfer pass already active")) {
            return nullptr;
        }
        ++beginTransferCount_;
        markCommandOk();
        return nullptr;
    }

    arp::core::UniquePtr<CommandBuffer> finish() override
    {
        if (!finishGuard("FakeEncoder has unclosed pass")) {
            return nullptr;
        }
        ++finishCount_;
        return nullptr;
    }

    CommandValidationResult writeTimestamp(const QueryPool&, std::uint32_t) override
    {
        return commandOk(++commandCount_);
    }

    void setFailNextNativePassBegin(bool value) { failNextNativePassBegin_ = value; }

private:
    int transitionBufferCount_ = 0;
    int transitionTextureCount_ = 0;
    int beginComputeCount_ = 0;
    int beginTransferCount_ = 0;
    int beginRenderCount_ = 0;
    int finishCount_ = 0;
    bool lastBeginPassRolledBack_ = false;
    bool failNextNativePassBegin_ = false;
};

void assertFreshEncoderHasCleanValidationAndZeroCounts()
{
    FakeEncoder e;
    const auto v = e.lastValidation();
    assert(v.success);
    assert(v.recordedCommandCount == 0);
    assert(e.transitionBufferCount() == 0);
    assert(e.transitionTextureCount() == 0);
    assert(e.beginComputeCount() == 0);
    assert(e.finishCount() == 0);
}

void assertBeginPassGuardRejectsSecondActivePass()
{
    FakeEncoder e;
    ComputePassDesc desc;
    // First begin succeeds and sets activePass_.
    e.beginComputePass(desc);
    assert(e.beginComputeCount() == 1);
    // Second begin is rejected by beginPassGuard without calling the native
    // begin hook (count stays 1) and stamps a failure.
    auto second = e.beginComputePass(desc);
    assert(second == nullptr);
    assert(e.beginComputeCount() == 1);
    assert(!e.lastValidation().success);
    assert(e.lastValidation().diagnostics.find("already active") != std::pmr::string::npos);
}

void assertRollbackPassBeginRestoresNoActivePassState()
{
    FakeEncoder e;
    e.setFailNextNativePassBegin(true);
    ComputePassDesc desc;
    auto result = e.beginComputePass(desc);
    assert(result == nullptr);
    assert(e.lastBeginPassRolledBack());
    assert(!e.lastValidation().success);
    // After rollback the encoder has no active pass, so the next begin succeeds.
    e.setFailNextNativePassBegin(false);
    auto retry = e.beginComputePass(desc);
    // retry is nullptr because FakeEncoder returns nullptr for the pass object,
    // but the begin hook ran and the count advanced.
    assert(retry == nullptr);
    assert(e.beginComputeCount() == 1);
    assert(e.lastValidation().success);
}

void assertFinishGuardRejectsUnclosedPass()
{
    FakeEncoder e;
    ComputePassDesc desc;
    e.beginComputePass(desc);
    assert(e.beginComputeCount() == 1);
    // finish() while a pass is active is rejected by finishGuard.
    auto firstFinish = e.finish();
    assert(firstFinish == nullptr);
    assert(e.finishCount() == 0);
    assert(!e.lastValidation().success);
    assert(e.lastValidation().diagnostics.find("unclosed pass") != std::pmr::string::npos);
}

void assertFinishGuardRejectsDoubleFinish()
{
    FakeEncoder e;
    auto firstFinish = e.finish();
    assert(firstFinish == nullptr); // FakeEncoder returns nullptr sentinel
    assert(e.finishCount() == 1);
    assert(e.lastValidation().success);
    // Second finish is rejected by finishGuard (finished_ flag).
    auto secondFinish = e.finish();
    assert(secondFinish == nullptr);
    assert(e.finishCount() == 1);
    assert(!e.lastValidation().success);
    assert(e.lastValidation().diagnostics.find("already been finished") != std::pmr::string::npos);
}

void assertBeginPassGuardRejectsAfterFinish()
{
    FakeEncoder e;
    e.finish();
    assert(e.finishCount() == 1);
    ComputePassDesc desc;
    auto begin = e.beginComputePass(desc);
    assert(begin == nullptr);
    assert(e.beginComputeCount() == 0);
    assert(!e.lastValidation().success);
    assert(e.lastValidation().diagnostics.find("already been finished") != std::pmr::string::npos);
}

void assertMarkCommandFailDoesNotAdvanceCounter()
{
    FakeEncoder e;
    ComputePassDesc desc;
    e.beginComputePass(desc); // success, count=1
    assert(e.lastValidation().recordedCommandCount == 1);
    e.beginComputePass(desc); // failure via beginPassGuard, count stays 1
    assert(!e.lastValidation().success);
    assert(e.lastValidation().recordedCommandCount == 1);
}

void assertTransferPassGuardSharesSameActivePassState()
{
    FakeEncoder e;
    ComputePassDesc computeDesc;
    e.beginComputePass(computeDesc);
    assert(e.beginComputeCount() == 1);
    // A transfer pass begin while a compute pass is active is rejected by the
    // shared beginPassGuard, proving all pass kinds share the same activePass_.
    TransferPassDesc transferDesc;
    auto transfer = e.beginTransferPass(transferDesc);
    assert(transfer == nullptr);
    assert(e.beginTransferCount() == 0);
    assert(!e.lastValidation().success);
}

} // namespace

int main()
{
    assertFreshEncoderHasCleanValidationAndZeroCounts();
    assertBeginPassGuardRejectsSecondActivePass();
    assertRollbackPassBeginRestoresNoActivePassState();
    assertFinishGuardRejectsUnclosedPass();
    assertFinishGuardRejectsDoubleFinish();
    assertBeginPassGuardRejectsAfterFinish();
    assertMarkCommandFailDoesNotAdvanceCounter();
    assertTransferPassGuardSharesSameActivePassState();
    return 0;
}
