#pragma once
#include "buffer_declaration_type_layout_fixture.h"
#include "core/file_io.h"
#include "original_runtime_length_fixture.h"
#include "original_target_memory_fixture.h"
#include "runtime_array_length_operations_fixture.h"
#include "shader/wgsl_buffer_declaration_layout.h"
#include "spirv_wgsl_buffer_memory_lowering_fixture.h"
#include "webgpu_raw_physical_metadata_fixture.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Test-only direct native observations. This is not an RHI proof issuer.
EM_ASYNC_JS(int,
    arpTestWgslBufferMinimum,
    (const char* code, int uniformMin, int inputMin, int outputMin),
    {
        // clang-format off

        const device = Module.preinitializedWebGPUDevice;
        const resources = [];
        const errors = [];
        async function scoped(operation, expected)
        {
            device.pushErrorScope('validation');
            let value;
            try {
                value = await operation();
            } finally {
                const error = await device.popErrorScope();
                if (!!error !== expected)
                    throw new Error(
                        'WGSL minimum native error scope mismatch: ' + (error && error.message));
                if (error)
                    errors.push(String(error.message));
            }
            return value;
        }
        try {
            const module = device.createShaderModule({code : UTF8ToString(code)});
            const compilation = await module.getCompilationInfo();
            if (compilation.messages.some(m => m.type === 'error'))
                throw new Error('WGSL minimum probe compilation rejected');
            const minima = [ uniformMin, inputMin, outputMin ];
            function layout(values)
            {
                return device.createBindGroupLayout({
                    entries : values.map((size, binding) => ({
                        binding,
                        visibility : GPUShaderStage.COMPUTE,
                        buffer : {
                            type : [ 'uniform', 'read-only-storage', 'storage' ][binding],
                            minBindingSize : size,
                            hasDynamicOffset : binding === 1
                        }
                    }))
                });
            }
            function pipeline(group)
            {
                return device.createComputePipeline({
                    layout : device.createPipelineLayout({bindGroupLayouts : [group]}),
                    compute : {module, entryPoint : 'computeMain'}
                });
            }
            for (let binding = 0; binding < minima.length; ++binding) {
                const smaller = minima.slice();
                smaller[binding]--;
                await scoped(() => pipeline(layout(smaller)), true);
            }
            const groupLayout = await scoped(() => layout(minima), false);
            const accepted = await scoped(() => pipeline(groupLayout), false);
            const alignment = device.limits.minStorageBufferOffsetAlignment;
            if (!Number.isSafeInteger(alignment) || alignment <= 0)
                throw new Error('WGSL minimum alignment unavailable');
            const bufferBytes = 3 * alignment + 2 * inputMin;
            function buffer(size, usage)
            {
                const value = device.createBuffer({size, usage});
                resources.push(value);
                return value;
            }
            const uniform = buffer(uniformMin, GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST);
            const input = buffer(bufferBytes, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST);
            const output = buffer(outputMin, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC);
            const readback = buffer(outputMin, GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST);
            device.queue.writeBuffer(uniform, 0, new Uint32Array([5]));
            const values = new Uint32Array(bufferBytes / 4);
            values.fill(13);
            values.fill(42, 2 * alignment / 4, (2 * alignment + inputMin) / 4);
            device.queue.writeBuffer(input, 0, values);
            function group(groupLayout, inputSize)
            {
                return device.createBindGroup({
                    layout : groupLayout,
                    entries : [
                        {binding : 0, resource : {buffer : uniform, offset : 0, size : uniformMin}},
                        {
                            binding : 1,
                            resource : {buffer : input, offset : alignment, size : inputSize}
                        },
                        {binding : 2, resource : {buffer : output, offset : 0, size : outputMin}}
                    ]
                });
            }
            const bound = await scoped(() => group(groupLayout, inputMin), false);
            await scoped(() => {
            const encoder = device.createCommandEncoder(); const pass = encoder.beginComputePass();
            pass.setPipeline(accepted); pass.setBindGroup(0, bound, [alignment]); pass.dispatchWorkgroups(1); pass.end();
            encoder.copyBufferToBuffer(output, 0, readback, 0, outputMin); device.queue.submit([encoder.finish()]);
        }, false);
            await readback.mapAsync(GPUMapMode.READ);
            const result = new Uint32Array(readback.getMappedRange())[0];
            readback.unmap();
            if (result !== 47)
                throw new Error('WGSL minimum exact base/dynamic slice result: ' + result);
            // minBindingSize0 defers the minimum check until draw/dispatch; it is
            // never evidence that the store type has no minimum.
            const deferredLayout = await scoped(() => layout([ 0, 0, 0 ]), false);
            const deferred = await scoped(() => pipeline(deferredLayout), false);
            const short = await scoped(() => group(deferredLayout, inputMin - 4), false);
            await scoped(() => {
            const encoder = device.createCommandEncoder(); const pass = encoder.beginComputePass();
            pass.setPipeline(deferred); pass.setBindGroup(0, short, [alignment]); pass.dispatchWorkgroups(1); pass.end(); encoder.finish();
        }, true);
            await device.queue.onSubmittedWorkDone();
            Module.wgslBufferMinimumProbe = {
                uniformMin,
                inputMin,
                outputMin,
                alignment,
                result,
                expectedErrors : errors,
                originalCustody : false,
                nativeSliceAuthority : false,
                runtimeLengthNativeValidated : false
            };
            return 1;
        } catch (error) {
            Module.printErr(
                'SHADER-WGSL-MINIMUM-NATIVE-PROBE ' + String(error && error.stack || error));
            return 0;
        } finally {
            resources.forEach(resource => resource.destroy());
        }

        // clang-format on
    });
EM_ASYNC_JS(int, arpTestRuntimeArrayLength, (const char* code), {
    // clang-format off

    const device = Module.preinitializedWebGPUDevice;
    const owned = [];
    device.pushErrorScope('validation');
    try {
        const module = device.createShaderModule({code : UTF8ToString(code)});
        const compilation = await module.getCompilationInfo();
        if (compilation.messages.some(m => m.type === 'error'))
            throw new Error('Runtime length module rejected');
        const layout = device.createBindGroupLayout({
            entries : [
                {
                    binding : 0,
                    visibility : GPUShaderStage.COMPUTE,
                    buffer :
                        {type : 'read-only-storage', minBindingSize : 16, hasDynamicOffset : true}
                },
                {
                    binding : 1,
                    visibility : GPUShaderStage.COMPUTE,
                    buffer : {type : 'storage', minBindingSize : 8, hasDynamicOffset : false}
                }
            ]
        });
        const pipeline = device.createComputePipeline({
            layout : device.createPipelineLayout({bindGroupLayouts : [layout]}),
            compute : {module, entryPoint : 'computeMain'}
        });
        const alignment = device.limits.minStorageBufferOffsetAlignment;
        if (!Number.isSafeInteger(alignment) || alignment <= 0)
            throw new Error('Runtime length alignment unavailable');
        function buffer(size, usage)
        {
            const value = device.createBuffer({size, usage});
            owned.push(value);
            return value;
        }
        // Fixed authored test ceiling and intervals, independent of any RHI
        // resource quota or inferred device capacity. The actual alignment is queried.
        const input = buffer(3 * alignment + 64, GPUBufferUsage.STORAGE);
        const output = buffer(8, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_SRC);
        const readback = buffer(8, GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ);
        const cases = [
            {bytes : 16, dynamic : 0, count : 1},
            {bytes : 32, dynamic : alignment, count : 2},
            {bytes : 36, dynamic : 0, count : 2},
            {bytes : 48, dynamic : alignment, count : 3},
            {bytes : 60, dynamic : 0, count : 3}
        ];
        const observations = [];
        for (const c of cases) {
            const group = device.createBindGroup({
                layout,
                entries : [
                    {binding : 0, resource : {buffer : input, offset : alignment, size : c.bytes}},
                    {binding : 1, resource : {buffer : output, offset : 0, size : 8}}
                ]
            });
            const encoder = device.createCommandEncoder();
            const pass = encoder.beginComputePass();
            pass.setPipeline(pipeline);
            pass.setBindGroup(0, group, [c.dynamic]);
            pass.dispatchWorkgroups(1);
            pass.end();
            encoder.copyBufferToBuffer(output, 0, readback, 0, 8);
            device.queue.submit([encoder.finish()]);
            await readback.mapAsync(GPUMapMode.READ);
            const values = new Uint32Array(readback.getMappedRange());
            const count = values[0], stride = values[1];
            readback.unmap();
            if (count !== c.count || stride !== 16)
                throw new Error('Runtime length mismatch ' + JSON.stringify({c, count, stride}));
            observations.push({
                bindingBytes : c.bytes,
                staticOffset : alignment,
                dynamicOffset : c.dynamic,
                count,
                stride
            });
        }
        await device.queue.onSubmittedWorkDone();
        const error = await device.popErrorScope();
        if (error)
            throw new Error(error.message);
        Module.runtimeLengthOperationProbe = {
            observations,
            originalCustody : false,
            nativeSliceAuthority : false,
            rhiExecutionAvailable : false
        };
        return 1;
    } catch (error) {
        Module.printErr(
            'SHADER-RUNTIME-LENGTH-NATIVE-PROBE ' + String(error && error.stack || error));
        return 0;
    } finally {
        owned.forEach(resource => resource.destroy());
    }

    // clang-format on
});
#endif

namespace arp::test {
class WgslBufferMinimumNativeFixture final {
public:
    static void verify(const char* path)
    {
#ifdef __EMSCRIPTEN__
        BufferDeclarationTypeLayoutFixture::verify();
        BufferDeclarationTypeLayoutFixture::real(
            path, "/buffer-layout-probe/buffer_layout_probe.spv");
        SpirvWgslBufferMemoryLoweringFixture::verify();
        SpirvWgslBufferMemoryLoweringFixture::real(
            path, "/buffer-layout-probe/buffer_layout_probe.spv");
        WebGpuRawPhysicalMetadataFixture::verify();
        OriginalTargetMemoryFixture::entry(
            "/buffer-layout-probe/buffer_layout_probe.computeMain.entry.yaml", true);
        OriginalTargetMemoryFixture::module(
            "/ordinary-pipeline-fixture/ordinary_graphics_storage_read/ordinary_graphics_storage_read.graphics.module.yaml");
        OriginalTargetMemoryFixture::module(
            "/ordinary-pipeline-fixture/ordinary_graphics_storage/ordinary_graphics_storage.graphics.module.yaml");
        OriginalTargetMemoryFixture::module(
            "/ordinary-pipeline-fixture/ordinary_graphics_push/ordinary_graphics_push.graphics.module.yaml");
        OriginalTargetMemoryFixture::module(
            "/ordinary-pipeline-fixture/ordinary_graphics_uniform/ordinary_graphics_uniform.graphics.module.yaml");
        for (const auto* variant : {"structured", "rwstructured", "byteaddress", "rwbyteaddress"}) {
            core::pmr::String publication(
                "/runtime-length-fixture/original/", &core::defaultCpuMemoryResource());
            publication += variant;
            publication += "/runtime_length_";
            publication += variant;
            publication += ".computeMain.entry.yaml";
            OriginalRuntimeLengthFixture::verify(publication.c_str());
        }
        OriginalRuntimeLengthFixture::verify(
            "/runtime-length-fixture/original/graphics/runtime_length_graphics.graphics.module.yaml",
            true);
        RuntimeArrayLengthOperationsFixture::real(
            "/runtime-length-fixture/structured.spv", "/runtime-length-fixture/structured.wgsl");
        RuntimeArrayLengthOperationsFixture::real("/runtime-length-fixture/rwstructured.spv",
            "/runtime-length-fixture/rwstructured.wgsl",
            true);
        RuntimeArrayLengthOperationsFixture::real("/runtime-length-fixture/byteaddress.spv",
            "/runtime-length-fixture/byteaddress.wgsl",
            false,
            true);
        RuntimeArrayLengthOperationsFixture::real("/runtime-length-fixture/rwbyteaddress.spv",
            "/runtime-length-fixture/rwbyteaddress.wgsl",
            true,
            true);
        auto lengthCode = core::readBinaryFile("/runtime-length-fixture/structured.wgsl",
            std::filesystem::file_size("/runtime-length-fixture/structured.wgsl"));
        assert(lengthCode.success);
        lengthCode.bytes.push_back(0u);
        assert(arpTestRuntimeArrayLength(reinterpret_cast<const char*>(lengthCode.bytes.data())));
        std::puts(
            "BINDLESS_RHI_RUNTIME_LENGTH_NATIVE_OBSERVATION_OK actual-slang finite-size base-dynamic partial-stride-tails counts1-2-2-3-3 stride16 no-original-rhi-authority");
        auto file = core::readBinaryFile(path, std::filesystem::file_size(path));
        assert(file.success);
        core::pmr::Vector<shader::WgslBufferDeclarationLayout> output{
            &core::defaultCpuMemoryResource()};
        const std::string_view source(
            reinterpret_cast<const char*>(file.bytes.data()), file.bytes.size());
        assert(!shader::WgslBufferDeclarationLayoutVerifier::verify(source, 3u, output)
            && output.size() == 3u);
        std::uint32_t minima[3]{};
        for (const auto& f : output) {
            assert(f.group == 0u && f.binding < 3u && !minima[f.binding]
                && f.minimumBindingBytes <= UINT32_MAX);
            minima[f.binding] = static_cast<std::uint32_t>(f.minimumBindingBytes);
            assert(f.addressSpace
                == static_cast<shader::WgslBufferDeclarationLayout::AddressSpace>(f.binding + 1u));
        }
        assert(minima[0] == 4u && minima[1] == 48u && minima[2] == 4u);
        file.bytes.push_back(0u);
        assert(arpTestWgslBufferMinimum(
            reinterpret_cast<const char*>(file.bytes.data()), minima[0], minima[1], minima[2]));
        std::puts(
            "BINDLESS_RHI_WGSL_DECLARATION_MINIMUM_OK actual-slang uniform4 storage48 early-errors3 deferred-error1 base-dynamic-slice47 no-original-native-authority");
#else
        (void)path;
#endif
    }
};
} // namespace arp::test
