#pragma once
#include "rhi/frontend/bindless_root_initialized_shader.h"

#include <cassert>

namespace arp::test {
class OriginalHostGeometryContentFixture final {
public:
    static void verify()
    {
        using namespace rhi;
        using C = OriginalHostGeometryContentContract;
        OriginalBindlessRootNativeClosureBudget closure{3u, true, 2u, 2u, 16u, 65536u, 2u, 72u};
        assert(!OriginalBindlessRootNativeClosureContract::validateBudget(closure));
        auto badClosure = closure;
        badClosure.maxInitializedGeometryActors = 0u;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(badClosure));
        badClosure = closure;
        badClosure.maxInitializedGeometryActors = 3u;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(badClosure));
        badClosure = closure;
        badClosure.maxInitializedGeometryBytes = 0u;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(badClosure));
        for (auto version : {1u, 2u}) {
            badClosure = closure;
            badClosure.contractVersion = version;
            assert(OriginalBindlessRootNativeClosureContract::validateBudget(badClosure));
        }
        OriginalBindlessInitializedShaderBudget shaderCustody{1u, true, 1u, 64u, 16u};
        closure.initializedShader = shaderCustody;
        assert(!OriginalBindlessRootNativeClosureContract::validateBudget(closure));
        auto wrongShader = closure;
        wrongShader.initializedShader.optIn = false;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(wrongShader));
        wrongShader = closure;
        wrongShader.initializedShader.maxActors = closure.maxResourceEntries + 1u;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(wrongShader));
        wrongShader = closure;
        wrongShader.initializedShader.maxBindingVisits = 0u;
        assert(OriginalBindlessRootNativeClosureContract::validateBudget(wrongShader));
        using ShaderCustody = OriginalBindlessInitializedShaderContract;
        assert(!ShaderCustody::validatePublicInputs({}, nullptr, 0u));
        assert(ShaderCustody::validatePublicInputs(shaderCustody, nullptr, 0u));
        std::shared_ptr<const OriginalInitializedGeometrySource> missingShader;
        assert(ShaderCustody::validatePublicInputs({}, &missingShader, 1u));
        assert(ShaderCustody::validatePublicInputs(shaderCustody, &missingShader, 1u));
        using Sources = OriginalBindlessInitializedSourcesContract;
        OriginalBindlessRootNativeClosureBudget bufferOnly{1u, true, 2u, 2u, 16u, 65536u};
        assert(!Sources::validatePublicInputs(bufferOnly, {}));
        assert(Sources::validatePublicInputs(bufferOnly, {&missingShader, 0u}));
        assert(Sources::validatePublicInputs(bufferOnly, {&missingShader, 1u}));
        auto geometryOnly = closure;
        geometryOnly.initializedShader = {};
        assert(Sources::validatePublicInputs(geometryOnly, {}));
        assert(Sources::validatePublicInputs(geometryOnly, {nullptr, 1u}));
        assert(Sources::validatePublicInputs(geometryOnly, {&missingShader, 1u}));
        bufferOnly.initializedShader = shaderCustody;
        assert(Sources::validatePublicInputs(bufferOnly, {}));
        assert(Sources::validatePublicInputs(bufferOnly, {nullptr, 0u, &missingShader, 1u}));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU, Backend::WebGL}) {
            const auto lowering =
                semantics::OriginalBindlessInitializedShaderLowering::current(backend);
            assert(!lowering.executionAvailable
                && lowering.correspondenceAvailable
                    == (backend == Backend::Vulkan || backend == Backend::Metal
                        || backend == Backend::WebGPU)
                && lowering.validatorOnly == (backend == Backend::Null));
            assert(semantics::OriginalBindlessInitializedShaderLowering::current(backend, 2u)
                    .diagnostic);
        }
        constexpr std::uint64_t cpu = 65536u;
        C::Budget b{1u,
            true,
            1u,
            C::retainedOwnerBound,
            64u,
            cpu,
            64u,
            cpu,
            cpu,
            C::Staging::VertexContentIntent{1u, true, 16u, VertexInputRate::Vertex, 4u}};
        BufferDesc d{"sealed-geometry",
            64u,
            ResourceUsage::VertexBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead};
        assert(!C::validateDeclaration(d, 64u, b));
        auto invalid = b;
        invalid.contractVersion = 2u;
        assert(C::validateBudget(invalid));
        invalid = b;
        invalid.prototypeOptIn = false;
        assert(C::validateBudget(invalid));
        invalid = b;
        --invalid.maxRetainedOwners;
        assert(C::validateBudget(invalid));
        invalid = b;
        --invalid.maxRetainedBufferBytes;
        assert(C::validateDeclaration(d, 64u, invalid));
        invalid = b;
        --invalid.maxCpuPayloadBytes;
        assert(C::validateDeclaration(d, 64u, invalid));
        invalid = b;
        invalid.maxCpuMetadataBytes = 0u;
        assert(C::validateBudget(invalid));
        assert(C::validateDeclaration(d, 48u, b));
        auto wrong = d;
        wrong.usage = ResourceUsage::VertexBuffer | ResourceUsage::StorageBuffer;
        assert(C::validateDeclaration(wrong, 64u, b));
        wrong = d;
        wrong.memory = MemoryUsage::GpuOnly;
        assert(C::validateDeclaration(wrong, 64u, b));
        wrong = d;
        wrong.initialState = ResourceState::CopyDst;
        assert(C::validateDeclaration(wrong, 64u, b));
        VertexInputConsumptionContract::Stream stream{
            0u, 16u, VertexInputRate::Vertex, 0u, 1u, 16u};
        assert(!C::validateVertexStream(b.definition, stream));
        stream.rate = VertexInputRate::Instance;
        assert(C::validateVertexStream(b.definition, stream));
        std::get<C::Staging::VertexContentIntent>(b.definition).rate = VertexInputRate::Instance;
        assert(!C::validateVertexStream(b.definition, stream));
        b.definition = C::Staging::IndexContentIntent{
            1u, true, IndexFormat::Uint16, core::UnsignedElementSpan::ByteOrder::LittleEndian, 3u};
        d.sizeBytes = 8u;
        d.usage = ResourceUsage::IndexBuffer;
        assert(!C::validateDeclaration(d, 8u, b) && C::validateVertexStream(b.definition, stream));
        d.sizeBytes = 7u;
        assert(C::validateDeclaration(d, 7u, b));
        d.sizeBytes = 8u;
        std::get<C::Staging::IndexContentIntent>(b.definition).shaderReadContractVersion = 1u;
        assert(C::validateDeclaration(d, 8u, b));
        for (const auto backend :
            {Backend::Null, Backend::Vulkan, Backend::Metal, Backend::WebGPU}) {
            const auto execution =
                semantics::OriginalBindlessGeometryExecutionLowering::current(backend);
            assert(execution.executionAvailable
                    == (backend == Backend::Vulkan || backend == Backend::Metal)
                && execution.validatorOnly == (backend == Backend::Null)
                && bool(execution.diagnostic)
                    == (backend != Backend::Vulkan && backend != Backend::Metal));
            assert(semantics::OriginalBindlessGeometryExecutionLowering::current(backend, 2u)
                    .diagnostic);
            const auto readback =
                semantics::OriginalBufferReadbackBindingLowering::lowering(backend, 3u);
            assert(readback.pendingAssociationImplemented
                    == (backend == Backend::Null || backend == Backend::Vulkan
                        || backend == Backend::Metal)
                && !readback.nativeExecutionImplemented);
            const auto lower = C::lowering(backend);
            assert(!lower.executionAvailable
                && lower.initializationAvailable
                    == (backend == Backend::Vulkan || backend == Backend::Metal
                        || backend == Backend::WebGPU)
                && lower.validatorOnly == (backend == Backend::Null));
            const auto immutableShader = C::lowering(backend, 2u);
            assert(immutableShader.initializationAvailable == lower.initializationAvailable
                && immutableShader.validatorOnly == lower.validatorOnly
                && !immutableShader.executionAvailable);
            assert(C::lowering(backend, 3u).diagnostic);
        }
        // Independent ImmutableShader/2 authenticates whole readonly backing;
        // geometry /1 cannot borrow this profile or grant shader writes.
        C::Budget shaderBudget{2u,
            true,
            1u,
            C::retainedOwnerBound,
            64u,
            cpu,
            64u,
            cpu,
            cpu,
            C::Staging::ShaderContentIntent{1u, true, DescriptorType::ReadOnlyStorageBuffer, 64u}};
        BufferDesc shaderDesc{"frozen-shader-input",
            64u,
            ResourceUsage::StorageBuffer,
            MemoryUsage::CpuToGpu,
            ResourceState::ShaderRead};
        assert(!C::validateDeclaration(shaderDesc, 64u, shaderBudget));
        auto legacyShader = shaderBudget;
        legacyShader.contractVersion = 1u;
        assert(C::validateDeclaration(shaderDesc, 64u, legacyShader));
        auto writeShader = shaderBudget;
        std::get<C::Staging::ShaderContentIntent>(writeShader.definition).type =
            DescriptorType::StorageBuffer;
        assert(C::validateDeclaration(shaderDesc, 64u, writeShader));
        std::array<std::uint8_t, 64u> actual{}, expected{};
        expected[63u] = 0x80u;
        assert(C::validateShaderBytes(shaderBudget.definition,
            DescriptorType::ReadOnlyStorageBuffer,
            actual.data(),
            actual.size(),
            expected.data(),
            expected.size()));
        actual[63u] = 0x80u;
        assert(!C::validateShaderBytes(shaderBudget.definition,
            DescriptorType::ReadOnlyStorageBuffer,
            actual.data(),
            actual.size(),
            expected.data(),
            expected.size()));
        assert(C::validateShaderBytes(shaderBudget.definition,
            DescriptorType::UniformBuffer,
            actual.data(),
            actual.size(),
            expected.data(),
            expected.size()));
        assert(C::validateShaderBytes(shaderBudget.definition,
            DescriptorType::ReadOnlyStorageBuffer,
            actual.data(),
            actual.size(),
            expected.data(),
            expected.size() - 4u));
        assert(C::validateShaderBytes(shaderBudget.definition,
            DescriptorType::ReadOnlyStorageBuffer,
            nullptr,
            actual.size(),
            expected.data(),
            expected.size()));
        std::get<C::Staging::ShaderContentIntent>(shaderBudget.definition).type =
            DescriptorType::UniformBuffer;
        assert(C::validateDeclaration(shaderDesc, 64u, shaderBudget));
        shaderDesc.usage = ResourceUsage::UniformBuffer;
        assert(!C::validateDeclaration(shaderDesc, 64u, shaderBudget));
        std::get<C::Staging::ShaderContentIntent>(shaderBudget.definition).bindingBytes = 60u;
        assert(C::validateDeclaration(shaderDesc, 64u, shaderBudget));
        using Pending = OriginalInitializedBufferPendingContract;
        using Use = OriginalInitializedBufferPendingUse;
        assert(!Pending::validateUse(5u, Use::ImmutableBindlessGeometryRead5));
        assert(!Pending::validateUse(6u, Use::ImmutableBindlessShaderRead6));
        assert(Pending::validateUse(5u, Use::ImmutableBindlessShaderRead6));
        assert(Pending::validateUse(6u, Use::ImmutableBindlessGeometryRead5));
        for (const auto version : {0u, 8u, 9u, 11u, 12u, 13u, 14u, 15u, 16u, 18u, 19u}) {
            assert(!Pending::validateRootProfile(
                Use::ImmutableBindlessShaderRead6, false, false, version));
            assert(Pending::validateRootProfile(
                Use::ImmutableBindlessShaderRead6, true, false, version));
            assert(Pending::validateRootProfile(
                Use::ImmutableBindlessShaderRead6, false, true, version));
        }
        assert(Pending::validateRootProfile(Use::ImmutableBindlessShaderRead6, false, false, 7u));
        assert(!Pending::validateStates(Use::ImmutableBindlessShaderRead6,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            6u));
        assert(Pending::validateStates(Use::ImmutableBindlessShaderRead6,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderWrite,
            ResourceState::ShaderRead,
            6u));
        assert(Pending::validateUse(1u, Use::ImmutableBindlessGeometryRead5));
        assert(Pending::validateUse(5u, Use::ImmutableGeometryRead1));
        assert(
            !Pending::validateRootProfile(Use::ImmutableBindlessGeometryRead5, false, false, 9u));
        assert(Pending::validateRootProfile(Use::ImmutableBindlessGeometryRead5, false, false, 6u));
        assert(Pending::validateRootProfile(Use::ImmutableBindlessGeometryRead5, true, false, 9u));
        assert(!Pending::validateStates(Use::ImmutableBindlessGeometryRead5,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            5u));
        assert(Pending::validateStates(Use::ImmutableBindlessGeometryRead5,
            ResourceState::ShaderRead,
            ResourceState::ShaderRead,
            ResourceState::CopyDst,
            ResourceState::ShaderRead,
            5u));
    }
};
} // namespace arp::test
