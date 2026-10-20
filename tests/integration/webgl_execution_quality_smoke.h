#pragma once

#include "rhi/backends/webgl/webgl_device.h"

#include <emscripten.h>

namespace arp::rhi::webgl::test {
EM_JS(void,
    reportExecutionOutcome,
    (const char* label,
        double accepted,
        double failed,
        double lost,
        double operations,
        double calls),
    {
        // clang-format off
    Module.webGlQuality.outcomes.push({label:UTF8ToString(label),accepted,failed,lost,acceptedOperations:operations,nativeCalls:calls});
        // clang-format on
    });

inline void observeOutcome(const char* label,
    const semantics::WebGlExecutionCounters& before,
    const semantics::WebGlExecutionCounters& after)
{
    reportExecutionOutcome(label,
        after.accepted - before.accepted,
        after.failed - before.failed,
        after.lost - before.lost,
        after.acceptedOperations - before.acceptedOperations,
        after.nativeCalls - before.nativeCalls);
}

EM_JS(void, beginNativeAudit, (const char* label), {
    // clang-format off
    const context = GL.currentContext.GLctx;
    if (!Module.webGlQuality) {
        const quality = Module.webGlQuality = {active:null, scopes:[], budgets:[], outcomes:[]};
        const methods = new Set();
        for (let p=context; p; p=Object.getPrototypeOf(p))
            for (const name of Object.getOwnPropertyNames(p))
                if (name !== 'constructor' && typeof context[name] === 'function') methods.add(name);
        for (const name of methods) {
            const original = context[name].bind(context);
            context[name] = (...args) => {
                const a = quality.active;
                const timed = a && a.productObservation && /^(compileShader|linkProgram)$/.test(name);
                const started = timed ? performance.now() : 0;
                // These are lifecycle predicates, outside GL command-call scope.
                if (a && name !== 'isContextLost') {
                    a.nativeCalls++;
                    if (/^delete/.test(name)) a.nativeDeleteCalls++;
                    if (a.preparationActive) a.preparationNativeCalls++;
                    if (/^(getParameter|getUniformLocation|readPixels|finish|create.*|compileShader|linkProgram)$/.test(name)) a.forbidden.push(name);
                    if (name === 'drawArrays' || name === 'drawElements') a.draws++;
                    if (/^uniform/.test(name)) {
                        a.uniformCalls++;
                        a.uniformBytes += typeof args[1] === 'number' ? (args.length-1)*4
                            : name.startsWith('uniformMatrix') ? args[2].byteLength : args[1].byteLength;
                    }
                    if (name === 'bufferSubData') {a.bufferUpdateCalls++; a.bufferUpdateBytes+=args[2].byteLength;}
                    if (name === 'texSubImage2D') {a.textureUpdateCalls++; a.textureUpdateBytes+=args[4]*args[5]*4;}
                }
                const result = original(...args);
                if (timed) {
                    a.compileCalls++;
                    a.compileCpuMs += performance.now()-started;
                }
                return result;
            };
        }
    }
    if (Module.webGlQuality.active) throw Error('AUDIT-NESTED');
    Module.webGlQuality.active = {label:UTF8ToString(label), nativeCalls:0,draws:0,uniformCalls:0,uniformBytes:0,
        bufferUpdateCalls:0,bufferUpdateBytes:0,textureUpdateCalls:0,textureUpdateBytes:0,forbidden:[],
        productObservation:false,compileCalls:0,compileCpuMs:0,nativeDeleteCalls:0,
        preparationActive:false,preparationNativeCalls:0};
    // clang-format on
});

EM_JS(int,
    endNativeAudit,
    (double nativeCalls,
        double draws,
        double uniformCalls,
        double uniformBytes,
        double bufferUpdateCalls,
        double bufferUpdateBytes,
        double textureUpdateCalls,
        double textureUpdateBytes,
        double allocations,
        double liveBytes,
        double objects,
        double cpuTimeMs,
        int allowAllocation),
    {
        // clang-format off
    const q=Module.webGlQuality, a=q.active; q.active=null;
    const expected={nativeCalls,draws,uniformCalls,uniformBytes,bufferUpdateCalls,bufferUpdateBytes,textureUpdateCalls,textureUpdateBytes};
    a.expected=expected; a.hostAllocations=allocations; a.hostLiveBytes=liveBytes; a.liveObjects=objects;
    a.cpuTimeMs=cpuTimeMs;
    a.success=Number.isFinite(cpuTimeMs) && cpuTimeMs>=0 && a.forbidden.length===0 && (allowAllocation || allocations===0) && Object.keys(expected).every(k=>a[k]===expected[k]);
    q.scopes.push(a);
    if (!a.success) console.error('WEBGL-QUALITY-AUDIT',JSON.stringify(a));
    return a.success?1:0;
        // clang-format on
    });

class ExecutionAudit final {
public:
    ExecutionAudit(WebGlDevice& device, const char* label)
        : device_(device), before_(device.executionTelemetry()),
          allocations_(core::defaultCpuAllocator().stats().allocationCount)
    {
        beginNativeAudit(label);
        started_ = emscripten_get_now();
    }

    bool finish(bool allowAllocation = false)
    {
        const double elapsed = emscripten_get_now() - started_;
        const auto after = device_.executionTelemetry();
        const auto& a = after.execution;
        const auto& b = before_.execution;
        return endNativeAudit(a.nativeCalls - b.nativeCalls,
                   a.draws - b.draws,
                   a.uniformCalls - b.uniformCalls,
                   a.uniformBytes - b.uniformBytes,
                   a.bufferUpdateCalls - b.bufferUpdateCalls,
                   a.bufferUpdateBytes - b.bufferUpdateBytes,
                   a.textureUpdateCalls - b.textureUpdateCalls,
                   a.textureUpdateBytes - b.textureUpdateBytes,
                   core::defaultCpuAllocator().stats().allocationCount - allocations_,
                   core::defaultCpuAllocator().stats().liveBytes,
                   after.live.objectCount,
                   elapsed,
                   allowAllocation)
            && after.complete && !after.gpuCompletionKnown;
    }

private:
    WebGlDevice& device_;
    semantics::WebGlExecutionTelemetryReport before_;
    std::uint64_t allocations_;
    double started_ = 0;
};

EM_JS(void, poisonNativeState, (), {
    // clang-format off
    const g=GL.currentContext.GLctx;
    g.useProgram(null);g.bindFramebuffer(g.FRAMEBUFFER,null);
    g.bindBuffer(g.ARRAY_BUFFER,null);g.bindBuffer(g.ELEMENT_ARRAY_BUFFER,null);
    const count=g.getParameter(g.MAX_VERTEX_ATTRIBS);
    for(let i=0;i<count;i++) g.disableVertexAttribArray(i);
    const units=g.getParameter(g.MAX_COMBINED_TEXTURE_IMAGE_UNITS);
    for(let i=0;i<units;i++) {g.activeTexture(g.TEXTURE0+i);
        if(g.getParameter(g.TEXTURE_BINDING_2D)) {g.texParameteri(g.TEXTURE_2D,g.TEXTURE_MIN_FILTER,g.NEAREST_MIPMAP_LINEAR);g.texParameteri(g.TEXTURE_2D,g.TEXTURE_WRAP_S,g.REPEAT);}
        g.bindTexture(g.TEXTURE_2D,null);}
    g.pixelStorei(g.UNPACK_ALIGNMENT,8);g.pixelStorei(g.UNPACK_FLIP_Y_WEBGL,true);
    g.pixelStorei(g.UNPACK_PREMULTIPLY_ALPHA_WEBGL,true);g.pixelStorei(g.UNPACK_COLORSPACE_CONVERSION_WEBGL,g.BROWSER_DEFAULT_WEBGL);
    g.enable(g.SCISSOR_TEST);g.scissor(0,0,0,0);g.viewport(0,0,0,0);
    g.colorMask(false,false,false,false);g.depthMask(false);g.stencilMask(0);
    g.enable(g.CULL_FACE);g.frontFace(g.CW);g.enable(g.BLEND);g.blendFunc(g.ZERO,g.ZERO);
    g.enable(g.SAMPLE_COVERAGE);g.sampleCoverage(0,false);
    // clang-format on
});
} // namespace arp::rhi::webgl::test
