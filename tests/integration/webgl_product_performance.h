#pragma once

#include "platform/render_system_ordered_graphics.h"
#include "webgl_execution_quality_smoke.h"

namespace arp::rhi::webgl::test {

EM_JS(int, productPerformanceParameter, (int index), {
    // clang-format off
    const p=new URLSearchParams(location.search);
    if(p.get('productPerf')!=='1') return 0;
    const names=['warmup','samples'];
    const value=p.get(names[index]);
    if(!value || !/^[1-9][0-9]*$/.test(value)) throw Error('PRODUCT-PERF-PARAMETER');
    const n=Number(value);
    if(!Number.isSafeInteger(n) || n>(index===0?60:240)) throw Error('PRODUCT-PERF-CAPACITY');
    return n;
    // clang-format on
});

EM_JS(void, beginProductPerformance, (), {
    // clang-format off
    Module.webGlProductPerformance={type:'WebGlProductPerformance',contractVersion:1,
        timeDomain:'CPU only; JS GL interception included',gpuTimeMs:null,
        peakDomain:'core allocator process-lifetime high-water mark; not JS heap or GPU memory',
        stages:[],frames:[],setup:null,environment:Module.arpRuntimeEnvironment,
        productEnteredAtMs:performance.now(),wasmCompileMs:null,
        gpuCompletionClaimed:false,readinessPromoted:false,productionReady:false};
    // clang-format on
});

EM_JS(void, markProductAudit, (), {
    // clang-format off
    Module.webGlQuality.active.productObservation=true;
    // clang-format on
});

EM_JS(void, productPreparationPhase, (int active), {
    // clang-format off
    Module.webGlQuality.active.preparationActive=!!active;
    // clang-format on
});

class ProductPreparationObserver final : public core::CpuWorkMeasurementObserver {
public:
    void begin() noexcept override { productPreparationPhase(1); }

    void end() noexcept override { productPreparationPhase(0); }
};

EM_JS(void,
    productMeasurement,
    (const char* stage,
        double ms,
        double allocations,
        double deallocations,
        double before,
        double after,
        double peak,
        int observed),
    {
        // clang-format off
    Module.webGlProductPerformance.stages.push({stage:UTF8ToString(stage),cpuMs:ms,
        allocations,deallocations,liveBytesBefore:before,liveBytesAfter:after,
        processPeakBytes:peak,observed:!!observed});
        // clang-format on
    });

EM_JS(void, finishProductSetup, (), {
    // clang-format off
    const p=Module.webGlProductPerformance;
    p.setup={stages:p.stages,native:Module.webGlQuality.active,
        navigation:performance.getEntriesByType('navigation').map(n=>n.toJSON()),
        resources:performance.getEntriesByType('resource').map(n=>n.toJSON())};
    p.setupFinishedAtMs=performance.now();
    p.stages=[];Module.webGlQuality.active=null;
    // clang-format on
});

EM_JS(void,
    finishProductFrame,
    (int width,
        int height,
        int phase,
        int ordinal,
        double calls,
        double draws,
        double uniforms,
        double uniformBytes,
        double bufferCalls,
        double bufferBytes,
        double textureCalls,
        double textureBytes,
        double accepted,
        double failed,
        double lost,
        int complete,
        double liveObjects,
        double logicalBuffers,
        double logicalTextures,
        double metadata,
        double upload,
        double reload,
        double framePeak,
        double commandPeak,
        int validationChecks),
    {
        // clang-format off
    const p=Module.webGlProductPerformance;
    p.frames.push({width,height,phase:['cold-frame','warmup','steady'][phase],ordinal,
        stages:p.stages,native:Module.webGlQuality.active,fixtureValidationChecks:!!validationChecks,
        telemetry:{nativeCalls:calls,draws,uniformCalls:uniforms,uniformBytes,
            bufferUpdateCalls:bufferCalls,bufferUpdateBytes:bufferBytes,
            textureUpdateCalls:textureCalls,textureUpdateBytes:textureBytes,
            accepted,failed,lost,complete:!!complete},
        live:{objects:liveObjects,logicalBufferBytes:logicalBuffers,logicalTextureBytes:logicalTextures,
            cpuMetadataBytes:metadata,cpuUploadBytes:upload,cpuReloadBytes:reload},
        quotaPeaks:{frameBytes:framePeak,commandBytes:commandPeak}});
    p.stages=[];Module.webGlQuality.active=null;
        // clang-format on
    });

class ProductPerformance final {
public:
    explicit ProductPerformance(WebGlDevice& device)
        : device_(device), warmup_(productPerformanceParameter(0)),
          samples_(productPerformanceParameter(1)), setupScope_(enabled() ? &setup_ : nullptr)
    {
        if (enabled()) {
            beginProductPerformance();
            beginNativeAudit("product-cold-setup");
            markProductAudit();
            measurements_.preparationObserver = &preparationObserver_;
        }
    }

    bool enabled() const noexcept { return samples_ != 0; }

    std::uint32_t submissionBudget(std::uint32_t original) const noexcept
    {
        return original + 2 * (warmup_ + samples_);
    }

    void setupFinished()
    {
        if (!enabled())
            return;
        setupScope_.finish();
        report("setup", setup_);
        finishProductSetup();
    }

    void begin()
    {
        if (!enabled())
            return;
        before_ = device_.executionTelemetry();
        beginNativeAudit("product-frame");
        markProductAudit();
    }

    render_graph::OrderedGraphicsMeasurements* measurements() noexcept
    {
        return enabled() ? &measurements_ : nullptr;
    }

    core::CpuWorkMeasurement* inputs() noexcept { return enabled() ? &inputs_ : nullptr; }

    void finish(unsigned w,
        unsigned h,
        const std::shared_ptr<core::CpuMemoryQuota>& frameQuota,
        const std::shared_ptr<core::CpuMemoryQuota>& commandQuota,
        bool validationChecks)
    {
        if (!enabled())
            return;
        const auto after = device_.executionTelemetry();
        report("total", measurements_.total);
        report("inputs", inputs_);
        report("build", measurements_.build);
        report("compile", measurements_.compile);
        report("prepare", measurements_.prepare);
        report("record", measurements_.record);
        report("submit", measurements_.submit);
        report("presentRetire", measurements_.presentRetire);
        const auto& a = after.execution;
        const auto& b = before_.execution;
        finishProductFrame(w,
            h,
            phase_,
            ordinal_,
            a.nativeCalls - b.nativeCalls,
            a.draws - b.draws,
            a.uniformCalls - b.uniformCalls,
            a.uniformBytes - b.uniformBytes,
            a.bufferUpdateCalls - b.bufferUpdateCalls,
            a.bufferUpdateBytes - b.bufferUpdateBytes,
            a.textureUpdateCalls - b.textureUpdateCalls,
            a.textureUpdateBytes - b.textureUpdateBytes,
            a.accepted - b.accepted,
            a.failed - b.failed,
            a.lost - b.lost,
            after.complete,
            after.live.objectCount,
            after.live.logicalBufferBytes,
            after.live.logicalTextureBytes,
            after.live.cpuMetadataBytes,
            after.live.cpuUploadBytes,
            after.live.cpuReloadBytes,
            frameQuota->peakBytes(),
            commandQuota->peakBytes(),
            validationChecks);
    }

    template <class Render>
    void sample(Render&& render, unsigned w, unsigned h, bool& validationChecks)
    {
        if (!enabled())
            return;
        validationChecks = false;
        phase_ = 1;
        for (ordinal_ = 0; ordinal_ < warmup_; ++ordinal_)
            render(w, h, false);
        phase_ = 2;
        for (ordinal_ = 0; ordinal_ < samples_; ++ordinal_)
            render(w, h, false);
        phase_ = 0;
        ordinal_ = 0;
        render(w, h, true); // Original oracle follows the complete steady cohort.
        validationChecks = true;
    }

private:
    static void report(const char* name, const core::CpuWorkMeasurement& m)
    {
        productMeasurement(name,
            m.milliseconds,
            m.allocations,
            m.deallocations,
            m.liveBytesBefore,
            m.liveBytesAfter,
            m.processPeakBytes,
            m.observed);
    }

    WebGlDevice& device_;
    std::uint32_t warmup_, samples_, ordinal_ = 0;
    int phase_ = 0;
    core::CpuWorkMeasurement setup_;
    core::CpuWorkMeasurement inputs_;
    core::CpuWorkMeasurementScope setupScope_;
    semantics::WebGlExecutionTelemetryReport before_;
    render_graph::OrderedGraphicsMeasurements measurements_;
    ProductPreparationObserver preparationObserver_;
};
} // namespace arp::rhi::webgl::test
