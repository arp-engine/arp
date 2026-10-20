"""Guard L2 recording API coverage; this is a source contract, not a C++ parser."""
from collections import Counter
from pathlib import Path
import re
import unittest
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tooling"))
from source_contract_sections import decision_bodies, fragment as source_fragment
from cpp_source_evidence import CppSource


class CommandRecordingApiInventoryTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]
    CURRENT_CONTROL_SOURCES = {'original_recorded_state_domain.h': ('src/rhi/frontend/resource_recorded_state_domain.h', None),
     'original_texture_content_control.h': ('src/rhi/frontend/resource_texture_content_control.h', None),
     'query_slot_declaration_control.h': ('src/rhi/frontend/query_slot_declaration_control.h', None),
     'recorded_resource_state_control.h': ('src/rhi/frontend/resource_state_control.h', None),
     'recorded_texture_state_control.h': ('src/rhi/frontend/resource_texture_state_control.h', None)}
    CURRENT_READBACK_SOURCES = {'texture_readback_layout.h': ('src/rhi/frontend/transfer_readback_input.h',
                                   'frontend_transfer_readback_layout_h'),
     'texture_readback_request_declaration.h': ('src/rhi/frontend/transfer_readback_input.h',
                                                'frontend_transfer_readback_request_declaration_h'),
     'texture_readback_source_lease.h': ('src/rhi/frontend/transfer_readback_input.h',
                                         'frontend_transfer_readback_source_lease_h')}
    WEBGPU_SECTIONS = {'webgpu_accepted_original_compute_pipeline_factory.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preparation.h',
                                                             'backends_webgpu_webgpu_accepted_original_compute_pipeline_factory_h'),
     'webgpu_accepted_original_graphics_pipeline_factory.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preparation.h',
                                                              'backends_webgpu_webgpu_accepted_original_graphics_pipeline_factory_h'),
     'webgpu_accepted_original_pipeline_validation.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preparation.h',
                                                        'backends_webgpu_webgpu_accepted_original_pipeline_validation_h'),
     'webgpu_artifact_creation_native_methods.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preflight.h',
                                                   'backends_webgpu_webgpu_artifact_creation_native_methods_h'),
     'webgpu_artifact_creation_scope_support.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preparation.h',
                                                  'backends_webgpu_webgpu_artifact_creation_scope_support_h'),
     'webgpu_canonical_buffer_upload_source.h': ('src/rhi/backends/webgpu/webgpu_upload_preparation.h',
                                                 'backends_webgpu_webgpu_canonical_buffer_upload_source_h'),
     'webgpu_canonical_texture_content_upload_source.h': ('src/rhi/backends/webgpu/webgpu_upload_preparation.h',
                                                          'backends_webgpu_webgpu_canonical_texture_content_upload_source_h'),
     'webgpu_canonical_texture_upload_source.h': ('src/rhi/backends/webgpu/webgpu_upload_preparation.h',
                                                  'backends_webgpu_webgpu_canonical_texture_upload_source_h'),
     'webgpu_frozen_command_scalars.h': ('src/rhi/backends/webgpu/webgpu_command_inputs.h',
                                         'backends_webgpu_webgpu_frozen_command_scalars_h'),
     'webgpu_frozen_render_pass_binding.h': ('src/rhi/backends/webgpu/webgpu_command_inputs.h',
                                             'backends_webgpu_webgpu_frozen_render_pass_binding_h'),
     'webgpu_frozen_transfer_arguments.h': ('src/rhi/backends/webgpu/webgpu_command_inputs.h',
                                            'backends_webgpu_webgpu_frozen_transfer_arguments_h'),
     'webgpu_generic_command_projection.h': ('src/rhi/backends/webgpu/webgpu_command_inputs.h',
                                             'backends_webgpu_webgpu_generic_command_projection_h'),
     'webgpu_generic_command_usage.h': ('src/rhi/backends/webgpu/webgpu_command_inputs.h',
                                        'backends_webgpu_webgpu_generic_command_usage_h'),
     'webgpu_original_artifact_compute_creation.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preparation.h',
                                                     'backends_webgpu_webgpu_original_artifact_compute_creation_h'),
     'webgpu_original_artifact_compute_factory.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preparation.h',
                                                    'backends_webgpu_webgpu_original_artifact_compute_factory_h'),
     'webgpu_original_artifact_creation_source.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preparation.h',
                                                    'backends_webgpu_webgpu_original_artifact_creation_source_h'),
     'webgpu_original_artifact_graphics_creation.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preparation.h',
                                                      'backends_webgpu_webgpu_original_artifact_graphics_creation_h'),
     'webgpu_original_artifact_graphics_factory.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preparation.h',
                                                     'backends_webgpu_webgpu_original_artifact_graphics_factory_h'),
     'webgpu_original_buffer_upload_source.h': ('src/rhi/backends/webgpu/webgpu_upload_preparation.h',
                                                'backends_webgpu_webgpu_original_buffer_upload_source_h'),
     'webgpu_original_depth_table_expansion_creation_validation.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preparation.h',
                                                                     'backends_webgpu_webgpu_original_depth_table_expansion_creation_validation_h'),
     'webgpu_original_depth_table_expansion_pipeline_preflight.h': ('src/rhi/backends/webgpu/webgpu_pipeline_preflight.h',
                                                                    'backends_webgpu_webgpu_original_depth_table_expansion_pipeline_preflight_h'),
     'webgpu_original_texture_readback_destination.h': ('src/rhi/backends/webgpu/webgpu_readback_preparation.h',
                                                        'backends_webgpu_webgpu_original_texture_readback_destination_h'),
     'webgpu_original_texture_readback_device_staging_budget.h': ('src/rhi/backends/webgpu/webgpu_readback_preparation.h',
                                                                  'backends_webgpu_webgpu_original_texture_readback_device_staging_budget_h'),
     'webgpu_original_texture_readback_mapping.h': ('src/rhi/backends/webgpu/webgpu_readback_results.h',
                                                    'backends_webgpu_webgpu_original_texture_readback_mapping_h'),
     'webgpu_original_texture_readback_numerical_result.h': ('src/rhi/backends/webgpu/webgpu_readback_results.h',
                                                             'backends_webgpu_webgpu_original_texture_readback_numerical_result_h'),
     'webgpu_original_texture_readback_pair_projection.h': ('src/rhi/backends/webgpu/webgpu_readback_preparation.h',
                                                            'backends_webgpu_webgpu_original_texture_readback_pair_projection_h'),
     'webgpu_original_texture_readback_publication.h': ('src/rhi/backends/webgpu/webgpu_readback_results.h',
                                                        'backends_webgpu_webgpu_original_texture_readback_publication_h'),
     'webgpu_original_texture_readback_source.h': ('src/rhi/backends/webgpu/webgpu_readback_preparation.h',
                                                   'backends_webgpu_webgpu_original_texture_readback_source_h'),
     'webgpu_original_texture_readback_statistics.h': ('src/rhi/backends/webgpu/webgpu_readback_results.h',
                                                       'backends_webgpu_webgpu_original_texture_readback_statistics_h'),
     'webgpu_original_texture_upload_destination.h': ('src/rhi/backends/webgpu/webgpu_upload_preparation.h',
                                                      'backends_webgpu_webgpu_original_texture_upload_destination_h'),
     'webgpu_original_texture_upload_source.h': ('src/rhi/backends/webgpu/webgpu_upload_preparation.h',
                                                 'backends_webgpu_webgpu_original_texture_upload_source_h'),
     'webgpu_owned_texture_readback_snapshot.h': ('src/rhi/backends/webgpu/webgpu_readback_results.h',
                                                  'backends_webgpu_webgpu_owned_texture_readback_snapshot_h')}
    def read_webgpu_source(self, name):
        path, section = self.WEBGPU_SECTIONS.get(name, ("src/rhi/backends/webgpu/"+name, None))
        return self.read_source(path, section)

    def read_source(self, path, fragment=None, follow_decisions=True):
        source = CppSource((self.ROOT / path).read_text(encoding="utf-8-sig"))
        if fragment:
            source = source_fragment(source, fragment)
        return (CppSource(source + ("\n" + decision_bodies(self.ROOT, source) if follow_decisions else ""))
                if path.startswith("src/rhi/") else source)

    def read_rhi_api_and_implementation(self, header):
        # Existing behavior assertions follow the relocated implementation;
        # abstraction-only dependency checks read the header separately.
        implementation = {
            "src/rhi/gpu_device.h": "src/rhi/frontend/device_device.inl",
            "src/rhi/gpu_pipeline.h": "src/rhi/frontend/pipeline_interface_validation.inl",
        }[header]
        return CppSource(CppSource((self.ROOT / implementation).read_text(encoding="utf-8-sig"))
                + "\n" + CppSource((self.ROOT / header).read_text(encoding="utf-8-sig")))

    def read_validation_section(self, relative_path, section):
        source = CppSource((self.ROOT / relative_path).read_text(encoding="utf-8-sig"))
        begin = "// region " + section + "\n"
        end = "// endregion " + section + "\n"
        self.assertEqual(source.count(begin), 1)
        self.assertEqual(source.count(end), 1)
        selected=source.split(begin, 1)[1].split(end, 1)[0]
        return CppSource(selected + "\n" + decision_bodies(self.ROOT, selected))

    def read_command_contract_sections(self, model_path, lowering_path, section):
        # Preserve assertions over the complete former contract while testing
        # the layer boundaries independently below.
        return (self.read_validation_section(model_path, section)
                + self.read_validation_section(lowering_path, section))

    def test_command_l2_models_exclude_policy_storage_and_native_dependencies(self):
        for name in ("command_model.h", "command_retention_budget.h"):
            source=self.read_source("src/rhi/frontend/"+name, follow_decisions=False)
            self.assertNotRegex(source, r'rhi/(?:semantics|backends)/')
            self.assertNotRegex(source, r'\bBackend\b|\blowering\s*\(')
            self.assertNotRegex(source, r'core::(?:makeUnique|makeShared)|std::shared_ptr')

    def test_shared_command_runtime_consumes_decisions_without_defining_backend_policy(self):
        for name in ("command_sources.h", "command_usage.h", "command_state.h", "command_recording.h", "resource_leases.h"):
            source=self.read_source("src/rhi/frontend/"+name, follow_decisions=False)
            self.assertNotRegex(source, r'case\s+Backend::')
            self.assertNotRegex(source, r'#include\s+"rhi/backends/')
            self.assertNotRegex(source, r'\b(?:wgpu|vk|MTL)\w+\s*\(')

    def test_command_lowering_policy_has_no_runtime_ownership_or_native_execution(self):
        for path in (self.ROOT / "src/rhi/semantics").glob("command*lowering.h"):
            source=CppSource(path.read_text(encoding="utf-8-sig"))
            self.assertIn("namespace arp::rhi::semantics",source)
            self.assertNotRegex(source, r'#include\s+"(?:rhi/backends/|renderer/|platform/)')
            self.assertNotRegex(source, r'core::(?:makeUnique|makeShared)|std::shared_ptr|QuotaBoundSharedObjectIdentity')
            self.assertNotRegex(source, r'\b(?:wgpu|vk|MTL)\w+\s*\(')

    def test_upper_layers_cannot_include_shared_command_internals(self):
        for directory in ["src/renderer", "src/platform", "src/shader", "games", "samples"]:
            for path in (self.ROOT / directory).rglob("*"):
                if path.suffix in {".h", ".cpp", ".mm", ".inl"}:
                    self.assertNotRegex(CppSource(path.read_text(encoding="utf-8-sig")), r'#include\s+"rhi/frontend/command_')

    # Counts preserve convenience overload coverage. Empty tuples explicitly
    # classify informational getters; constructors on other encoder types are
    # separate recordings and cannot be mistaken for execution in this stream.
    INVENTORY = {
        "CommandEncoder": {
            "queryInterface": (1, ()), "recordingIdentity": (1, ()), "lastValidation": (1, ()),
            "prepareBindlessGraphicsPass": (1, ("PrepareBindlessGraphicsPass",)),
            "transition": (5, ("BufferBarrier", "TextureBarrier")),
            "memoryBarrier": (2, ("BufferMemoryBarrier", "TextureMemoryBarrier")),
            "aliasBarrier": (1, ("AliasBarrier",)),
            "beginRenderPass": (1, ("BeginRenderPass",)), "beginComputePass": (1, ("BeginComputePass",)),
            "beginTransferPass": (1, ("BeginTransferPass",)), "finish": (1, ("Finish",)),
            "pushDebugGroup": (1, ("PushDebugGroup",)), "popDebugGroup": (1, ("PopDebugGroup",)),
            "insertDebugMarker": (1, ("InsertDebugMarker",)),
        },
        "RenderPassEncoder": {
            "queryInterface": (1, ()), "currentSubpassIndex": (1, ()),
            "setPipeline": (1, ("BindGraphicsPipeline",)), "setViewport": (1, ("SetViewport",)),
            "setScissor": (1, ("SetScissor",)), "bindVertexBuffer": (1, ("BindVertexBuffer",)),
            "bindIndexBuffer": (1, ("BindIndexBuffer",)),
            "bindTessellationFactorBuffer": (1, ("BindTessellationFactorBuffer",)),
            "bindDescriptorSet": (1, ("BindRenderDescriptor",)),
            "bindDescriptorTable": (1, ("BindRenderDescriptorTable",)),
            "pushConstants": (2, ("PushConstants",)), "executeSecondary": (1, ("ExecuteSecondaryCommandBuffer",)),
            "executeRenderBundle": (1, ("ExecuteRenderBundle",)), "drawIndexed": (1, ("DrawIndexed",)),
            "drawIndexedIndirect": (1, ("DrawIndexedIndirect",)),
            "drawIndexedIndirectSequence": (1, ("DrawIndexedIndirect",)),
            "drawIndexedIndirectCount": (1, ("DrawIndexedIndirectCount",)), "draw": (1, ("Draw",)),
            "writeTimestamp": (1, ("Timestamp",)), "writePipelineStatistics": (1, ("PipelineStatistics",)),
            "nextSubpass": (1, ("NextSubpass",)), "end": (1, ("EndRenderPass",)),
        },
        "ComputePassEncoder": {
            "queryInterface": (1, ()), "setPipeline": (1, ("BindComputePipeline",)),
            "bindDescriptorSet": (1, ("BindComputeDescriptor",)),
            "bindDescriptorTable": (1, ("BindComputeDescriptorTable",)), "pushConstants": (2, ("PushConstants",)),
            "dispatch": (1, ("Dispatch",)), "dispatchIndirect": (1, ("DispatchIndirect",)),
            "writeTimestamp": (1, ("Timestamp",)), "writePipelineStatistics": (1, ("PipelineStatistics",)),
            "end": (1, ("EndComputePass",)),
        },
        "TransferPassEncoder": {
            "copyBufferToBuffer": (2, ("BufferCopy", "LegacySizeOnlyBufferCopy")),
            "copyBufferToTexture": (1, ("BufferToTextureCopy",)),
            "copyTextureToBuffer": (1, ("TextureToBufferCopy",)),
            "copyTextureToTexture": (1, ("TextureToTextureCopy",)),
            "uploadBuffer": (1, ("BufferUpload",)), "uploadTexture": (1, ("TextureUpload",)),
            "readbackTexture": (1, ("TextureReadback",)), "end": (1, ("EndTransferPass",)),
        },
    }

    def test_generic_original_scalars_bind_exact_graph_quota_and_shared_usage_validator(self):
        source=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_scalars")
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_command_inputs.h", "backends_webgpu_webgpu_frozen_command_scalars_h")
        self.assertIn("arguments->validateUsage",source)
        self.assertIn("arguments->matchesUsageKind",backend)
        self.assertLess(backend.index("CommandRecordingOrder::validate"),backend.index("arguments->matchesUsageKind"))
        for token in ["OriginalCommandUsage::validateOriginal", "hasQuotaBoundSharedOwner(arguments)",
                      "sameOwner(quota,usage->originalMetadataQuota())", "sameOwner(quota,arguments->originalMetadataQuota())",
                      "arguments->validateCapacity", "retainedOwnerCount()>budget.maxGraphOwners-additionalOwners",
                      "arguments_->matches(command,input)", "arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backend).diagnostic"]:
            self.assertIn(token,source)
        self.assertIn("nativeExecutionProven() const noexcept {return false;}",source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalScalarsPassed",runner)
        self.assertIn("genericOriginalScalarsPassed: genericOriginalScalarsPassed",runner)

    def test_generic_original_usage_reuses_shared_sequence_and_closes_native_execution(self):
        source=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_usage")
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_command_inputs.h", "backends_webgpu_webgpu_generic_command_usage_h")
        shared=self.read_validation_section("src/rhi/frontend/command_model.h","command_usage_sequence")
        self.assertIn("CommandUsageSequenceContract::validate",source)
        self.assertIn("CommandUsageSequenceContract::validate",backend)
        self.assertIn("CommandUsageContract::validReferences",shared)
        self.assertIn("core::OrderedSpanCursor cursor",shared)
        self.assertNotIn("CommandUsageContract::validReferences",backend)
        for token in ["OriginalCommandResourceSources::validateOriginal", "sources->originalBudget()",
                      "CpuDomain::Graph", "sameControlBlock(quota,sources->originalMetadataQuota())",
                      "RHI-ORIGINAL-USAGE-OWNER-CAPACITY", "semantics::CommandRecordingOrder::validate",
                      "lease->originalObject()==&object", "arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backend).diagnostic"]:
            self.assertIn(token,source)
        self.assertIn("nativeExecutionProven() const noexcept {return false;}",source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalUsagePassed",runner)
        self.assertIn("genericOriginalUsagePassed: genericOriginalUsagePassed",runner)

    def test_generic_original_source_table_retains_typed_quota_bound_sources(self):
        source=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        for token in ["std::variant<std::shared_ptr<const OriginalCommandBufferLease>",
                      "std::shared_ptr<const OriginalCommandTextureLease>", "core::BoundedSequence<Source>",
                      "core::SharedIdentityProjection::inspect", "Lease::validateOriginal(value)",
                      "SharedOwnerProjection::sameOwner(value->originalMetadataQuota(),quota)",
                      "maxSourceOwners[index]", "RHI-ORIGINAL-SOURCES-DUPLICATE", "hasQuotaBoundSharedOwner"]:
            self.assertIn(token,source)
        self.assertNotIn("using Source=std::shared_ptr<const void>",source)
        self.assertIn("nativeExecutionProven() const noexcept {return false;}",source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalResourceSourcesPassed",runner)
        self.assertIn("genericOriginalResourceSourcesPassed: genericOriginalResourceSourcesPassed",runner)
        self.assertNotIn("case Backend::",source)

    def test_generic_resource_lease_is_typed_original_cpu_lifetime_only(self):
        source=self.read_validation_section("src/rhi/frontend/resource_leases.h","original_command_resource_lease")
        for token in ["std::is_same_v<T,GpuBuffer> || std::is_same_v<T,GpuTexture>",
                      "core::ColdOwnedObjectSlot<T>", "source_.bindCold(source)",
                      "source_.adoptOriginal(source", "hasQuotaBoundSharedOwner", "object.id()==id_",
                      "layout_.matches(object.desc())", "CpuMemoryQuotaScope", "core::makeUnique",
                      "limitBytes()==metadataBytes_", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        for token in ["shared_ptr<const void>", "nativeHandle", "source->state()", "source->allocation()", "new "]:
            self.assertNotIn(token,source)
        self.assertNotIn("lowering(Backend",source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalResourceLeasePassed",runner)
        self.assertIn("genericOriginalResourceLeasePassed: genericOriginalResourceLeasePassed",runner)
        self.assertNotIn("case Backend::",source)

    def test_original_sampler_custody_extends_existing_owner_and_registration(self):
        lease=self.read_validation_section("src/rhi/frontend/resource_leases.h","original_command_resource_lease")
        for token in ["std::is_same_v<T,GpuSampler>", "SamplerResourceLayout", "CommandReferenceDomain::Sampler",
                      "OriginalCommandSamplerLease", "core::ColdOwnedObjectSlot<T>"]:
            self.assertIn(token,lease)
        registration=CppSource((self.ROOT / "src/rhi/frontend/resource_registration.h").read_text(encoding="utf-8-sig"))
        for token in ["OriginalSamplerRegistrationRequest", "b.samplers.diagnostic()", "samplers_(budget.samplers.maxSamplers)",
                      "case OriginalRegisteredResourceKind::Sampler", "return &samplers_", "OriginalSamplerRegistration_lowering"]:
            self.assertIn(token,registration)
        sampler=CppSource((self.ROOT / "tests/fixtures/original_command_sampler_custody_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["samplerLowering", "Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU",
                      "!decision.executionAvailable", "validateRegisteredLifetime", "generation() > generation",
                      "pressure.verify", "verifyHotNoAllocation", "observed.expired()"]:
            self.assertIn(token,sampler)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& dependencies && sampler && samplerDescriptor",runner)
        self.assertIn("originalSamplerCustodyPassed:sampler",runner)

    def test_original_sampler_descriptor_is_nonmemory_canonical_reference(self):
        contract=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_typed_source")
        for token in ["validateGeneralSamplerFootprint", "samplerReferenceLowering", "CompareOp::Never",
                      "ResourceState::Undefined", "ResourceAccess::None", "binding.dynamicOffset",
                      "RHI-ORIGINAL-TYPED-SAMPLER-TABLE-ADAPTER-MISSING"]:
            self.assertIn(token,contract)
        consumption=self.read_source("src/rhi/frontend/command_usage.h","frontend_command_descriptor_consumption_h")
        for token in ["Sampler", "samplerReferences", "samplerSourceIndex", "sources.originalLeaseIndex(*sampler)",
                      "RHI-ORIGINAL-CONSUMPTION-SAMPLER-PROFILE", "++result.logicalAccesses"]:
            self.assertIn(token,consumption)
        projection=self.read_source("src/rhi/frontend/command_usage_projection.h")
        self.assertIn("render.logicalAccesses-render.textureAccesses-render.samplerReferences",projection)
        self.assertIn("shader.logicalAccesses-shader.textureAccesses-shader.samplerReferences",projection)
        self.assertIn("OriginalTypedUsageProjectionContract::isGeneral(budget) ? nullptr",projection)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["consumed.logicalAccesses==4u", "consumed.samplerReferences==1u", "projected.counts.contributions==3u",
                      "projected.counts.shaderBuffers==1u", "projected.counts.shaderSamplers==1u", "!leaked"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("originalSamplerDescriptorReferencePassed:samplerDescriptor",runner)

    def test_original_sampler_native_loss_reuses_exact_subscription_and_native_source(self):
        contract=self.read_validation_section("src/rhi/frontend/resource_loss_validation.h","original_sampler_native_loss_join")
        for token in ["OriginalTypedNativeLossJoinContract::registrationVersion", "budget.samplers.requested()",
                      "budget.samplers.diagnostic()", "OriginalTypedNativeLossJoinContract::validate({1u,true},budget)",
                      "recordingAvailable=false,executionAvailable=false"]:
            self.assertIn(token,contract)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend,contract)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_original_device_native_loss.h",follow_decisions=False)
        self.assertEqual(native.count("const char* validate(const rhi::RenderPipeline& pipeline)"),1)
        method=native.split("const char* validate(const rhi::GpuSampler& sampler)",1)[1].split("\n    }",1)[0]
        self.assertIn("device_ && payload && payload->sampler && payload->ownerDevice==device_",method)
        for forbidden in ["emscripten_webgpu_get_device", "makeShared", "makeUnique", "Callback"]:
            self.assertNotIn(forbidden,method)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        creation=device.split("using Loss=OriginalSamplerNativeLossJoinContract",1)[1].split("record->object_",1)[0]
        self.assertLess(creation.index("Loss::validateRegistration"),creation.index("Loss::lowering"))
        self.assertLess(creation.index("validateOriginalTypedNativeLossJoin(*observed)"),
                        creation.index("validateOriginalNativeLossResource(object)"))
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("const rhi::GpuSampler& sampler) const noexcept override",backend)
        self.assertIn("return originalLossSource_.validate(sampler)",backend)
        fixture=self.read_source("tests/fixtures/original_command_sampler_custody_fixture.h")
        for token in ["prepare(6u)", "WebGpuForeignDeviceFixture::Scope", "nativeSource.validate(*absent)",
                      "payload->sampler=nullptr", "payload->ownerDevice=nullptr", "NATIVE-LOST",
                      "SHUTDOWN", "retained.reset()", "!observed->nativeExecutionProven()"]:
            self.assertIn(token,fixture)
        integration=self.read_source("tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp")
        self.assertIn("OriginalCommandSamplerCustodyFixture::verifyNativeLossJoin",integration)
        projection=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        self.assertIn("11u,7u,1u,3u,1u,false,&observedRequest,2u,1u",projection)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("&& samplerDescriptor && samplerLoss",runner)
        self.assertIn("originalSamplerNativeLossJoinPassed:samplerLoss",runner)

    def test_general_descriptor_binding_reuses_canonical_sources_and_frozen_registry(self):
        contract=self.read_validation_section("src/rhi/frontend/descriptor_general_binding_validation.h","original_general_descriptor_binding")
        for token in ["OriginalGeneralDescriptorBinding_lowering", "validateWholeBoundView",
                      "validateGeneralTextureFootprint", "validateGeneralSamplerFootprint", "executionAvailable = false"]:
            self.assertIn(token,contract)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend,contract)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_original_general_binding_lowering.h",follow_decisions=False)
        for token in ["validateSetElements", "Dependencies::capture", "validateFootprint",
                      "validateConsumedOriginal", "issued.facts()", "originalTargetBufferMemory",
                      "observeOriginalWebGpuResource", "output = {}", "group = 0u", "entry.size = 0u"]:
            self.assertIn(token,native)
        for forbidden in ["wgpuDeviceCreateBindGroup", "wgpuQueueSubmit", "wgpuDeviceGetLimits",
                          "makeShared", "makeUnique", "nativeExecutionProven()"]:
            self.assertNotIn(forbidden,native)
        objects=self.read_source("src/rhi/backends/webgpu/webgpu_backend_objects.cpp",follow_decisions=False)
        sampler=objects.split("class WebGpuRhiSampler final",1)[1].split("class WebGpuRhiPipelineLayout",1)[0]
        self.assertIn("registry->observeOrdinaryResources",sampler)
        self.assertIn("registry->validateOrdinaryResources",sampler)
        fixture=self.read_source("tests/fixtures/original_general_descriptor_binding_fixture.h")
        for token in ["verifyHotNoAllocation", "verifyNoHotQueries", "native->sampler = nullptr",
                      "native->parentTexture.reset()", "bad.registrationGeneration", "!entry.textureView",
                      "validateOriginalWebGpuResource"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("originalGeneralDescriptorBindingPassed:generalBinding",runner)
        self.assertIn("&& samplerLoss && generalBinding",runner)

    def test_general_descriptor_transaction_keeps_original_candidate_and_execution_gate(self):
        contract=self.read_source("src/rhi/frontend/descriptor_general_write_validation.h")
        for token in ["maxRetainedAllocationBytes", "completeRetainedOwnerCount",
                      "OriginalGeneralDescriptorWrite_decide", "maxRetainedOwnerSlots"]:
            self.assertIn(token,contract)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp",follow_decisions=False)
        for token in ["OriginalGeneralWriteState", "lowerFrozen", "SharedAllocationProjection",
                      "generalContentDiagnostic", "core::UniquePtr<OriginalGeneralWriteState>"]:
            self.assertIn(token,native)
        objects=self.read_source("src/rhi/backends/webgpu/webgpu_backend_descriptors.cpp",follow_decisions=False)
        self.assertIn("finishScoped(false, true)",objects)
        self.assertIn("cancelScoped(false, true)",objects)
        fixture=self.read_source("tests/fixtures/original_general_descriptor_write_fixture.h")
        for token in ["Scopes::releaseScope", "sourceOwners", "maxRetainedAllocationBytes",
                      "CpuQuotaFactoryFixture", "failed-replacement-preserved"]:
            self.assertIn(token,fixture)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp",follow_decisions=False)
        self.assertIn("caps_.executablePayload = false",device)

    def test_generic_original_contract_has_explicit_closed_four_backend_decisions(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_model.h","src/rhi/semantics/command_input_admission_lowering.h","original_command_admission")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",contract)
        self.assertNotIn("true,",contract)
        self.assertIn("executionAvailable=false",contract)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_command_inputs.h")
        self.assertIn("using Family=rhi::CommandInputContract::Family",source)
        self.assertIn("return rhi::CommandInputContract::family(kind)",source)
        self.assertNotIn("case K::TextureReadback:return Family::TextureReadback",source)
        classifier=self.input_family_contract_source()
        vocabulary=self.read_validation_section("src/rhi/frontend/command_model.h","command_usage")
        body=vocabulary.split("enum class CommandUsageKind",1)[1].split("};",1)[0]
        names=re.findall(r"\b([A-Z][A-Za-z0-9]*)\b",re.sub(r"//[^\n]*","",body))
        for name in set(names)-{"CommandUsageKind"}:
            self.assertIn("case K::"+name+":",classifier)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalAdmissionContractPassed",runner)
        self.assertIn("genericOriginalAdmissionContractPassed: genericOriginalAdmissionContractPassed",runner)

    def input_family_contract_source(self):
        return self.read_validation_section("src/rhi/frontend/command_model.h","command_input")

    def test_readback_committed_declaration_does_not_grant_map_access(self):
        transaction=self.read_source("src/rhi/backends/webgpu/webgpu_original_state_transaction.h")
        copies=self.read_source("src/rhi/backends/webgpu/webgpu_original_texture_readback_copies.h")
        self.assertEqual(transaction.count("for (const auto& entry:entries_)"),1)
        self.assertIn("validateCommittedReadbackDeclarationOriginal",transaction)
        self.assertIn("self->status_!=Status::Committed",transaction)
        default=transaction.split("const char* validate()",1)[1].split("static const char* validateCommittedReadbackDeclarationOriginal",1)[0]
        self.assertIn("unmappedAccessDiagnostic()",default)
        self.assertIn("validateOwnedObservations()",default)
        native=copies.split("static const char* validateNativeCompletedDeclarationOriginal",1)[1].split("// Public terminal revocation",1)[0]
        for token in ["hasQuotaBoundSharedOwner", "NativeStatus::Completed", "ticketGeneration_", "validateCommittedReadbackDeclarationOriginal", "validateOriginalDecisions", "validateNativeFacts"]:self.assertIn(token,native)
        self.assertNotIn("allowMapped",transaction+copies)
        self.assertIn("return self->pair()->destination()->unmappedAccessDiagnostic()",copies)

    def test_all_public_recording_methods_have_explicit_inventory(self):
        source = CppSource((self.ROOT / "src/rhi/gpu_commands.h").read_text(encoding="utf-8-sig"))
        source = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
        vocabulary = self.read_validation_section("src/rhi/frontend/command_model.h","command_usage")
        for name, inventory in self.INVENTORY.items():
            with self.subTest(encoder=name):
                body = source.split("class ARP_API " + name + " {", 1)[1].split("\nclass ARP_API ", 1)[0]
                actual = Counter(re.findall(r"\bvirtual\b[^\n]*?\b(\w+)\s*\(", body))
                actual.pop(name)  # destructor
                self.assertEqual(actual, Counter({method: spec[0] for method, spec in inventory.items()}))
                for _, kinds in inventory.values():
                    for kind in kinds:
                        self.assertRegex(vocabulary, r"\b" + kind + r"\b")

    def test_webgpu_dependency_bodies_cannot_return_stat_only_success(self):
        source = self.read_source("src/rhi/backends/webgpu/webgpu_native_commands.cpp")
        for name, descriptor, gate in (
            ("recordBufferBarrier", "ResourceBarrierDesc", "transitionRecordingDiagnostic"),
            ("recordTextureBarrier", "ResourceBarrierDesc", "transitionRecordingDiagnostic"),
            ("recordBufferMemoryBarrier", "ResourceMemoryBarrierDesc", "memoryBarrierRecordingDiagnostic"),
            ("recordTextureMemoryBarrier", "ResourceMemoryBarrierDesc", "memoryBarrierRecordingDiagnostic"),
        ):
            with self.subTest(entry=name):
                body = source.split("bool WebGpuNativeCommandPayload::" + name + "(", 1)[1].split("\nbool ", 1)[0]
                self.assertIn("const rhi::" + descriptor + "& desc", body)
                self.assertIn("diagnostics = " + gate + "();", body)
                self.assertIn("return false;", body)
                self.assertNotIn("return true;", body)
                self.assertNotIn("recordCommand(", body)
                self.assertNotIn("impl_->stats", body)
        for gate, diagnostic in (
            ("transitionRecordingDiagnostic", "RHI-WEBGPU-BARRIER-LOWERING-UNPROVEN"),
            ("memoryBarrierRecordingDiagnostic", "RHI-WEBGPU-MEMORY-BARRIER-LOWERING-UNPROVEN"),
        ):
            body = source.split("const char* WebGpuNativeCommandPayload::" + gate + "()", 1)[1].split("\n}", 1)[0]
            self.assertLess(body.index("restrictedRecordingDiagnostic()"), body.index(diagnostic))
            self.assertNotIn("return nullptr", body)
        public = self.read_source("src/rhi/backends/webgpu/webgpu_backend_commands.cpp")
        self.assertIn("recordBufferBarrier(*nativeBuffer, desc, diagnostics)", public)
        self.assertRegex(public, r"recordTextureBarrier\(\s*\*nativeTexture,\s*desc,\s*diagnostics\)")
        self.assertIn("recordBufferMemoryBarrier(*nativeBuffer, desc, diagnostics)", public)
        self.assertIn("recordTextureMemoryBarrier(*nativeTexture, desc, diagnostics)", public)
        buffer = public.split("rhi::ResourceBarrierResult transition(\n        rhi::GpuBuffer& buffer,", 1)[1]
        texture = public.split("rhi::ResourceBarrierResult transition(\n        rhi::GpuTexture& texture,", 1)[1]
        for body, mutation in ((buffer,"transitionWebGpuRhiBuffer"),(texture,"transitionWebGpuRhiTexture")):
            self.assertLess(body.index("transitionRecordingDiagnostic()"),body.index(mutation))

    def test_webgpu_normalized_state_uses_exact_range_and_uniform_metadata_fails_closed(self):
        source = self.read_source("src/rhi/backends/webgpu/webgpu_backend_objects.cpp")
        body = source.split("rhi::ResourceBarrierResult transitionWebGpuRhiTexture(",1)[1].split("\narp::core::UniquePtr",1)[0]
        self.assertIn("recordSubresourceState(desc.textureSubresources,desc.before,desc.after,journal)",body)
        self.assertNotIn("recordState(desc.after,journal)",body)
        self.assertIn('if(range!=rhi::defaultTextureSubresourceRange(desc_))return "RHI-WEBGPU-BARRIER-SUBRESOURCE-STATE-UNPROVEN"',source)
        payload = self.read_source("src/rhi/backends/webgpu/webgpu_native_payloads.h").split("struct WebGpuNativeTexturePayload",1)[1].split("class WebGpuNativeTextureFactory final",1)[0]
        for token in ("RecordedTextureStateControl", "RecordedWholeResourceStateView", "std::in_place,desc,desc.initialState", "state_->write(range,before,after,journal)", "state_->writeWhole(after,journal)"):
            self.assertIn(token,payload)
        self.assertNotIn("RecordedResourceStateControl",payload)
        self.assertNotIn("setState(desc.after)",body)

    def test_normalized_texture_creation_storage_has_one_authority_and_precedes_native_publication(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        body=source.split("WebGpuNativeTextureFactory::create(const rhi::TextureDesc& desc,bool originalUpload,bool originalAttachment)",1)[1].split("createWebGpuNativeTextureViewPayload",1)[0]
        self.assertLess(body.index("NormalizedTextureStateCreationContract::inspect"),body.index("wgpuDeviceCreateTexture"))
        self.assertLess(body.index("core::makeShared<WebGpuNativeTexturePayload>"),body.index("wgpuDeviceCreateTexture"))
        view=self.read_source("src/rhi/frontend/resource_whole_state_view.h")
        for forbidden in ("VersionedMutationValue<", "VersionedMutationArray<", "pmr::Vector", "makeShared", "makeUnique", "wgpu"):
            self.assertNotIn(forbidden,view)
        self.assertEqual(re.findall(r"friend class (\w+);",view),["RecordedWholeResourceStateView"])
        control=self.read_source("src/rhi/frontend/resource_texture_state_control.h")
        for token in ("uniformWholeState", "values_.size()", "writeWhole", "values_.writeIndices", "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM"):
            self.assertIn(token,control)
        states=self.read_source("src/rhi/backends/webgpu/webgpu_original_resource_states.h")
        self.assertIn("using Control=rhi::common::RecordedWholeResourceStateView",states)
        self.assertIn("control.identity()",states)
        recording=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",recording),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& normalizedTextureCreationPassed",runner)
        self.assertIn("normalizedTextureCreationPassed: normalizedTextureCreationPassed",runner)

    def test_canonical_root_shared_mechanics_keep_public_seal_gate_and_private_cleanup(self):
        source=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        for public,internal in (("applyOriginalPrototypePlan","applyOriginalPlan"),("commitOriginalPendingOwnership","commitOriginalOwnership")):
            body=source.split("const char* "+public+"() noexcept",1)[1].split("\n    }",1)[0]
            self.assertLess(body.index("cpuMutationDiagnostic()"),body.index("return "+internal+"()"))
            private=source.split("\nprivate:\n",1)[1]
            mechanics=private.split("const char* "+internal+"() noexcept",1)[1].split("\n    }",1)[0]
            self.assertNotIn("abandonOriginalPendingOwnership",mechanics)
            self.assertNotIn("cpuMutationDiagnostic",mechanics)
            self.assertIn("validate()",mechanics)
        self.assertIn('rollbackOriginalOwnership();return "RHI-ORIGINAL-TRANSACTION-JOURNAL-ADMISSION"',source)
        self.assertNotIn("friend class WebGpu",source)
        seal=self.read_validation_section("src/rhi/frontend/command_recording.h","original_command_recording_seal")
        self.assertIn("inspectOriginalPreparation",seal)
        private=seal[seal.index("\nprivate:\n"): ]
        self.assertIn("root_->applyOriginalPlan",private)
        self.assertIn("friend class arp::rhi_webgpu::WebGpuOrdinaryRecording",seal)
        self.assertIn("friend class arp::rhi_webgpu::WebGpuOrdinarySubmission",seal)
        self.assertEqual(seal.count("root_->commitOriginalNativeOwnership"),2)
        self.assertIn("budget_.contractVersion != 11u", private)
        self.assertIn("Drain::Action::RetireUnpublished", private)
        self.assertIn("commitOriginalSubmission(std::uint32_t generalSubmissionVersion = 0u) const noexcept {return root_->commitOriginalNativeOwnership(generalSubmissionVersion);}",private)

    def test_frozen_input_metadata_cannot_issue_native_recording_authority(self):
        for name in ("webgpu_frozen_command_scalars.h", "webgpu_original_command_inputs.h", "webgpu_original_resource_states.h"):
            with self.subTest(component=name):
                source = self.read_webgpu_source(name)
                self.assertIn("public core::QuotaBoundSharedObjectIdentity", source)
                self.assertNotIn("public WebGpuRecordedCommandProof", source)
                self.assertNotIn("friend class", source.replace("friend class WebGpuFrozenCommandScalars;", "").replace("friend class WebGpuOriginalCommandInputs;", "").replace("friend class WebGpuOriginalResourceStates;", ""))
                self.assertNotRegex(source, r"\bwgpu\w+\s*\(")
                body = source.split("const char* executionDiagnostic() const noexcept", 1)[1].split("private:", 1)[0]
                self.assertIn("return webGpuGenericRecordingUsageDiagnostic;", body)
                self.assertNotIn("return nullptr;", body.split("const char* compareRaw", 1)[0])
        validation = self.read_source("src/rhi/backends/webgpu/webgpu_original_transfer_state_validation.h")
        self.assertNotRegex(validation, r"\bwgpu\w+\s*\(")
        self.assertNotIn("friend class", validation)
        self.assertNotIn("recordState(", validation)
        self.assertNotIn("WebGpuRecordedCommandProof", validation)
        access = self.read_source("src/rhi/backends/webgpu/webgpu_original_scalar_access_validation.h")
        self.assertNotRegex(access, r"\bwgpu\w+\s*\(")
        self.assertNotIn("friend class", access)
        self.assertNotIn("recordState(", access)
        self.assertNotIn("WebGpuRecordedCommandProof", access)
        graphics = self.read_source("src/rhi/backends/webgpu/webgpu_original_graphics_stream_validation.h")
        self.assertNotRegex(graphics, r"\bwgpu\w+\s*\(")
        self.assertNotIn("friend class", graphics)
        self.assertNotIn("recordState(", graphics)
        self.assertNotIn("WebGpuRecordedCommandProof", graphics)
        transfer = self.read_source("src/rhi/backends/webgpu/webgpu_original_transfer_stream_validation.h")
        self.assertNotRegex(transfer, r"\bwgpu\w+\s*\(")
        self.assertNotIn("friend class", transfer)
        self.assertNotIn("recordState(", transfer)
        self.assertNotIn("WebGpuRecordedCommandProof", transfer)


        proof = self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertNotIn("friend class WebGpuFrozenCommandScalars", proof)
        self.assertNotIn("friend class WebGpuOriginalCommandInputs", proof)
        self.assertNotIn("friend class WebGpuOriginalResourceStates", proof)

    def test_original_compute_batch_keeps_one_existing_recording_authority(self):
        source = self.read_source("src/rhi/backends/webgpu/webgpu_native_ordinary_recording.cpp")
        prepare = source.split("WebGpuOrdinaryRecording::prepareOriginalCompute(", 1)[1].split("WebGpuOrdinaryRecording::recordOriginalCompute(", 1)[0]
        self.assertIn("validateOriginalComputeInputs(states,range)", prepare)
        self.assertIn("ordinaryOriginalStates=result->originalStates_", prepare)
        self.assertNotRegex(prepare, r"\bwgpu\w+\s*\(")
        self.assertNotIn("WebGpuNativeCommandPreparation::open", prepare)
        record = source.split("WebGpuOrdinaryRecording::recordOriginalCompute(", 1)[1].split("\n}", 1)[0]
        self.assertLess(record.index("recordDiagnostic()"), record.index("WebGpuNativeCommandPreparation::open"))
        self.assertIn("arguments.records()[3u].scalar", record)
        self.assertIn("arguments.text(0u)", record)
        self.assertIn("popScopes(self,scopeCount)", record)
        self.assertNotRegex(record, r"\bwgpu(?:DeviceCreate|QueueSubmit)\w*\s*\(")
        prepare_graphics = source.split("WebGpuOrdinaryRecording::prepareOriginalGraphics(", 1)[1].split("WebGpuOrdinaryRecording::recordOriginalGraphics(", 1)[0]
        self.assertIn("WebGpuOriginalGraphicsStreamValidation::validate(states,range)", prepare_graphics)
        self.assertNotRegex(prepare_graphics, r"\bwgpu\w+\s*\(")
        self.assertNotIn("WebGpuNativeCommandPreparation::open", prepare_graphics)
        graphics = source.split("WebGpuOrdinaryRecording::recordOriginalGraphics(", 1)[1].split("\n}", 1)[0]
        self.assertLess(graphics.index("recordDiagnostic()"), graphics.index("WebGpuNativeCommandPreparation::open"))
        self.assertIn("WebGpuAttachmentOperationLowering::load(attachment.loadOp)", graphics)
        self.assertIn("WebGpuAttachmentOperationLowering::store(attachment.storeOp)", graphics)
        self.assertIn("self->popScopes(self,scopeCount)", graphics)
        self.assertNotRegex(graphics, r"\bwgpu(?:DeviceCreate|QueueSubmit)\w*\s*\(")
        proof = self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);", proof),
                         ["WebGpuOrdinaryRecording", "WebGpuOrdinarySubmission"])

    def test_original_transfer_batch_reuses_guarded_lowering_without_bypass(self):
        source = self.read_source("src/rhi/backends/webgpu/webgpu_native_ordinary_recording.cpp")
        prepare = source.split("WebGpuOrdinaryRecording::prepareOriginalTransfer(", 1)[1].split("WebGpuOrdinaryRecording::recordOriginalTransfer(", 1)[0]
        self.assertIn("WebGpuOriginalTransferStreamValidation::validate(states)", prepare)
        self.assertIn("validateOriginalTransferInputs(states,owner->nativeDevice())", prepare)
        self.assertNotRegex(prepare, r"\bwgpu\w+\s*\(")
        self.assertNotIn("WebGpuNativeCommandPreparation::open", prepare)
        record = source.split("WebGpuOrdinaryRecording::recordOriginalTransfer(", 1)[1].split("\n}", 1)[0]
        self.assertLess(record.index("recordDiagnostic()"), record.index("WebGpuNativeCommandPreparation::open"))
        self.assertIn("inputs.descriptors()[0].arguments", record)
        self.assertIn("wgpuCommandEncoderPushDebugGroup", record)
        self.assertIn("wgpuCommandEncoderPopDebugGroup", record)
        self.assertIn("WebGpuNativeTransferCopyLowering::emitOriginal", record)
        self.assertIn("self->popScopes(self,scopeCount)", record)
        self.assertNotIn("ordinaryTransferDispatch", record)
        self.assertNotIn("frozenTransferUsage", record)
        self.assertNotRegex(record, r"\bwgpu(?:DeviceCreate|QueueSubmit)\w*\s*\(")
        legacy = self.read_source("src/rhi/backends/webgpu/webgpu_native_transfer_commands.cpp")
        for name in ("recordBufferCopy", "recordBufferToTextureCopy", "recordTextureToBufferCopy", "recordTextureToTextureCopy"):
            body = legacy.split("bool WebGpuNativeCommandPayload::" + name + "(",1)[1].split("\n}",1)[0]
            self.assertLess(body.index("preparedRecordingDiagnostic()"), body.index("WebGpuNativeTransferCopyLowering::emit"))
            self.assertLess(body.index("ordinaryTransferDispatch"), body.index("WebGpuNativeTransferCopyLowering::emit"))
        lowering = self.read_source("src/rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h")
        self.assertNotIn("WebGpuRecordedCommandProof", lowering)
        self.assertNotIn("ordinaryTransferDispatch", lowering)
        self.assertNotRegex(lowering, r"\bwgpu(?:DeviceCreate|QueueSubmit)\w*\s*\(")
        self.assertIn("WGPUTextureAspect_Undefined", lowering)
        self.assertIn("RHI-WEBGPU-TRANSFER-EXTENT-OVERFLOW", lowering)

    def test_transfer_dependency_lowering_has_no_native_authority(self):
        source = self.read_source("src/rhi/semantics/transfer_dependency_lowering.h")
        profile = self.read_source("src/rhi/backends/webgpu/webgpu_original_transfer_dependency_validation.h")
        for text in (source, profile):
            self.assertNotIn("WebGpuRecordedCommandProof", text)
            self.assertNotIn("friend class", text)
            self.assertNotRegex(text, r"\bwgpu\w+\s*\(")
        self.assertIn("return webGpuGenericRecordingUsageDiagnostic;", profile)
        self.assertIn("Operation::ImplicitOrderedUsage", profile)
        self.assertIn("RHI-TRANSFER-DEPENDENCY-BACKEND-INVALID", source)

    def test_original_state_transaction_metadata_has_no_native_authority(self):
        source = self.read_source("src/rhi/backends/webgpu/webgpu_original_state_transaction.h")
        self.assertNotIn("WebGpuRecordedCommandProof", source)
        self.assertNotRegex(source, r"\bwgpu\w+\s*\(")
        self.assertEqual(re.findall(r"friend class (\w+);", source), ["WebGpuOriginalStateTransaction"])
        self.assertIn("return webGpuGenericRecordingUsageDiagnostic;", source)
        self.assertIn("validateInitialSequence(states_,budget_.maxDebugDepth)", source)
        self.assertIn("matchesPending(*entry.pending,journal_)", source)
        self.assertIn("Control::Observation{entry.pending->value(),entry.pending->revision()}", source)
        self.assertNotIn("const_cast", source)
        proof = self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);", proof), ["WebGpuOrdinaryRecording", "WebGpuOrdinarySubmission"])

    def test_initial_dependency_mapping_cannot_replace_current_transaction(self):
        states = self.read_source("src/rhi/backends/webgpu/webgpu_original_transfer_state_validation.h")
        profile = self.read_source("src/rhi/backends/webgpu/webgpu_original_transfer_dependency_validation.h")
        self.assertIn("states->validate()", states.split("static Result validateInitialSequence",1)[0])
        self.assertIn("states->validateSourceOwners()", states.split("static Result validateInitialSequence",1)[1])
        self.assertIn("states->validate()", profile.split("static const char* validateInitialMapping",1)[0])
        self.assertIn("states->validateSourceOwners()", profile.split("static const char* validateInitialMapping",1)[1])
        self.assertNotIn("skipValidation", states + profile)

    def test_original_dependency_native_batch_uses_existing_authority_and_atomic_commit(self):
        base = self.ROOT / "src/rhi/backends/webgpu"
        recording = CppSource((base / "webgpu_native_ordinary_recording.cpp").read_text(encoding="utf-8-sig"))
        prepare = recording.split("WebGpuOrdinaryRecording::prepareOriginalTransferDependency(",1)[1].split("WebGpuOrdinaryRecording::validateOriginalDependencyInputs",1)[0]
        self.assertIn("WebGpuOriginalTransferDependencyValidation::validate(states)",prepare)
        self.assertIn("originalDependencyDecisions_=mapping.dependencies",prepare)
        self.assertNotRegex(prepare,r"\bwgpu\w+\s*\(")
        record = recording.split("WebGpuOrdinaryRecording::recordOriginalTransferDependency(",1)[1].split("\n}",1)[0]
        self.assertLess(record.index("originalDependencyDecisions_"),record.index("WebGpuOriginalStateTransaction::apply"))
        self.assertLess(record.index("WebGpuOriginalStateTransaction::apply"),record.index("WebGpuNativeCommandPreparation::open"))
        self.assertIn("WebGpuNativeTransferCopyLowering::emitOriginal",record)
        self.assertIn("arguments()->text(2u)",record)
        allocate = recording.split("WebGpuOrdinaryRecording::allocate(",1)[1].split("\n}",1)[0]
        self.assertLess(allocate.index("ordinaryOriginalStateTransaction=originalTransaction"),allocate.index("WebGpuOrdinaryCompletionReservation::prepare"))
        submission = CppSource((base / "webgpu_native_ordinary_submission.cpp").read_text(encoding="utf-8-sig"))
        self.assertIn("retained.submitWith(reservation.ticket()",submission)
        self.assertIn("WebGpuOriginalStateTransaction::commit(state.ordinaryOriginalStateTransaction)",submission)
        self.assertNotIn("retained.submit(reservation.ticket()",submission)
        observe = submission.split("void WebGpuOrdinarySubmission::observe",1)[1].split("void WebGpuOrdinarySubmission::completeQueue",1)[0]
        self.assertLess(observe.index("graph->quarantineState()"),observe.index("retained.resolve(reservation.ticket(),false)"))
        self.assertLess(observe.index("if (!success && graph) graph->quarantineState()"),observe.index("pool.resolve(reservation.ticket(),success)"))
        graph = self.read_webgpu_source("webgpu_ordinary_completion_retention.h")
        matches = graph.split("bool matches(void* device)",1)[1].split("bool reserveSignal",1)[0]
        self.assertIn("sameOwner(command_->originalStateTransaction(),transaction_)",matches)
        self.assertNotIn("transaction_->validate",matches)
        native = CppSource((base / "webgpu_native_commands.cpp").read_text(encoding="utf-8-sig"))
        self.assertIn("retained.invalidate([](auto& graph) noexcept { graph.quarantineState(); })",native)
        proof = self.read_webgpu_source("webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_buffer_upload_scalar_capture_cannot_supply_staging_authority(self):
        source=self.read_source("src/rhi/frontend/transfer_upload_input.h", "frontend_transfer_buffer_upload_arguments_h")
        self.assertIn("TransferCopyArguments::capture(BufferCopyDesc",source)
        for forbidden in ["friend class", "WebGpuRecordedCommandProof", "native", "makeShared", "makeUnique", "executionAvailable"]:
            self.assertNotIn(forbidden,source)
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")
        original=self.read_source("src/rhi/backends/webgpu/webgpu_original_command_inputs.h")
        self.assertRegex(self.input_family_contract_source(),r"case\s+K::BufferUpload:\s*return\s+Family::BufferUpload;")
        projection=self.read_source("src/rhi/backends/webgpu/webgpu_command_inputs.h", "backends_webgpu_webgpu_generic_command_projection_h")
        self.assertIn("domain >= static_cast<std::size_t>(WebGpuCommandReferenceDomain::BindlessRoot)",projection)
        self.assertIn("RHI-WEBGPU-COMMAND-PROJECTION-DOMAIN-UNPROVEN",projection)

    def test_original_upload_source_requires_real_payload_and_ready_evidence(self):
        base=self.ROOT / "src/rhi/backends/webgpu"
        source=self.read_webgpu_source("webgpu_original_buffer_upload_source.h")
        self.assertEqual(re.findall(r"friend class (\w+);",source),["WebGpuOriginalTextureUploadSource","WebGpuOriginalBufferUploadSource"])
        self.assertNotIn("WebGpuRecordedCommandProof",source)
        impl=CppSource((base / "webgpu_staging_upload_pool.cpp").read_text(encoding="utf-8-sig"))
        prepare=impl.split("WebGpuOriginalBufferUploadSource::prepareOriginal(",1)[1].split("WebGpuOriginalBufferUploadSource::initialize(",1)[0]
        self.assertNotRegex(prepare,r"\bwgpu\w+\s*\(")
        self.assertIn("pool->originalSourceCount()>=p.maxOriginalSourceBuffers",prepare)
        self.assertIn("maxOriginalSourceGpuBytes-size",prepare)
        self.assertIn("core::CpuMemoryQuotaScope payload(payloadQuota)",prepare)
        start=impl.split("void WebGpuOriginalBufferUploadSource::start(",1)[1].split("WebGpuOriginalBufferUploadSource::Status",1)[0]
        self.assertIn("buffer_->sealWrites()",start)
        self.assertIn("writeWebGpuNativeBufferPayload",start)
        self.assertIn("wgpuQueueOnSubmittedWorkDone(queue_,info)",start)
        self.assertIn("WebGpuNativeErrorScopeBatch::pop",start)
        observe=impl.split("void WebGpuOriginalBufferUploadSource::observe(",1)[1].split("const char* WebGpuOriginalBufferUploadSource::diagnostic",1)[0]
        self.assertLess(observe.index("evidence_.allReceived()"),observe.index("Status::Ready"))
        self.assertIn("evidence_.proven()",observe)
        self.assertIn("if (status()!=Status::Pending) return",observe)
        self.assertIn("validateOrdinaryResources(&registeredObservation_,1u)",impl)
        self.assertIn("return webGpuGenericRecordingUsageDiagnostic",impl)
        pool=self.read_webgpu_source("webgpu_staging_upload_pool.h")
        self.assertIn("originalSourceContractVersion = 0u",pool)
        self.assertIn("originalSourcePrototypeOptIn = false",pool)
        self.assertIn("originalSourceAllocator_.memoryStats().liveResourceCount",pool)
        allocator=CppSource((base / "webgpu_allocator.cpp").read_text(encoding="utf-8-sig"))
        self.assertLess(allocator.index("allocation.releaseToken ="),allocator.index("ledger_->budget.recordAllocation"))

    def test_original_upload_projection_requires_exact_original_transfer_join(self):
        base=self.ROOT / "src/rhi/backends/webgpu"
        source=self.read_webgpu_source("webgpu_generic_command_projection.h")
        for token in ["maxOriginalUploadSources = 0u", "maxOriginalUploadBytes = 0u",
                      "source->hasQuotaBoundSharedOwner(source)", "source->validate()",
                      "transfer->find(original.id)", "sameOwner(joined->buffer, original.buffer)",
                      "sameOwner(joined->allocationOwner(), original.allocationOwner())",
                      "RHI-WEBGPU-COMMAND-PROJECTION-UPLOAD-DUPLICATE",
                      "!add(result->uploads_.values().size())",
                      "allocation(uploads_.values()[index]->resource())"]:
            self.assertIn(token,source)
        prepare=source.split("static core::UniquePtr<WebGpuGenericCommandProjection> prepare(",1)[1].split("const char* validate()",1)[0]
        self.assertLess(prepare.index("validateUploads(sources.transfer"),prepare.index("core::CpuMemoryQuotaScope"))
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")
        self.assertNotIn("WebGpuRecordedCommandProof",source)
        states=self.read_webgpu_source("webgpu_original_resource_states.h")
        # Upload capsules are joined to an existing Transfer control; no second
        # native/state owner may be manufactured for the same upload resource.
        self.assertNotIn("graph.originalUploads()",states)
        self.assertIn("graph.transfer()->resources()",states)

    def test_original_upload_full_input_preserves_descriptor_and_state_semantics(self):
        base=self.ROOT / "src/rhi/backends/webgpu"
        binding=self.read_webgpu_source("webgpu_original_buffer_upload_validation.h")
        for token in ["graph.validate()", "upload.source!=S::OriginalBufferUpload",
                      "upload.resource!=arguments.source()", "source.resource!=arguments.source()",
                      "destination.resource!=arguments.destination()", "arguments.backendOwnedStaging()",
                      "arguments.stagingIntent()!=rhi::StagingIntent::Upload",
                      "copy.sizeBytes!=original.sizeBytes()", "arguments.frameSerial()!=original.frameSerial()",
                      "WebGpuTransferCopyValidation::validate"]:
            self.assertIn(token,binding)
        self.assertNotIn("WebGpuRecordedCommandProof",binding)
        self.assertNotRegex(binding,r"\bwgpu\w+\s*\(")
        inputs=self.read_webgpu_source("webgpu_original_command_inputs.h")
        self.assertIn("std::get_if<rhi::BufferUploadArguments>",inputs)
        self.assertIn("WebGpuOriginalBufferUploadValidation::validate",inputs)
        state=self.read_webgpu_source("webgpu_original_transfer_state_validation.h")
        self.assertIn("if (upload) copy=&upload->copyArguments()",state)
        self.assertIn("source!=rhi::ResourceState::CopySrc || destination!=rhi::ResourceState::CopyDst",state)
        stream=self.read_webgpu_source("webgpu_original_transfer_stream_validation.h")
        self.assertNotIn("case K::BufferUpload:case K::TextureUpload",stream)

    def test_original_upload_native_recording_uses_original_graph_and_common_submission(self):
        base=self.ROOT / "src/rhi/backends/webgpu"
        source=CppSource((base / "webgpu_native_ordinary_recording.cpp").read_text(encoding="utf-8-sig"))
        prepare=source.split("WebGpuOrdinaryRecording::prepareOriginalBufferUpload(",1)[1].split("const char* WebGpuOrdinaryRecording::validateOriginalUploadInputs()",1)[0]
        self.assertNotRegex(prepare,r"\bwgpu\w+\s*\(")
        self.assertIn("WebGpuOriginalStateTransaction::prepareOriginal",prepare)
        record=source.split("const char* WebGpuOrdinaryRecording::recordOriginalBufferUpload(",1)[1].split("std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::allocate",1)[0]
        for token in ["WebGpuOriginalStateTransaction::apply", "std::get<rhi::BufferUploadArguments>",
                      "WebGpuNativeTransferCopyLowering::emitOriginal", "self->finishBuffer()", "self->popScopes"]:
            self.assertIn(token,record)
        self.assertNotIn("wgpuQueueSubmit",record)
        proof=self.read_webgpu_source("webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_texture_upload_packing_is_cpu_only_and_preflights_before_copy(self):
        source=self.read_source("src/rhi/frontend/transfer_upload_packing.h")
        self.assertEqual(re.findall(r"friend class (\w+);",source),["TextureUploadPacking"])
        for token in ["core::PreflightReplay<Mip>::run", "input->hasQuotaBoundSharedOwner(input)",
                      "core::CpuMemoryQuotaScope metadata(metadataQuota)", "core::CpuMemoryQuotaScope packed(packedQuota)",
                      "resize(static_cast<std::size_t>(packedBytes),0u)", "input_->pixelBytes().data()+source",
                      "RHI-TEXTURE-UPLOAD-ORIGINAL-SOURCE-UNPROVEN"]:
            self.assertIn(token,source)
        validation=self.read_source("src/rhi/frontend/transfer_upload_layout_validation.h")
        for token in ["TextureTransferValidation::textureUploadRegionValid", "core::StridedIndexRange",
                      "core::containsUnsignedRange", "d.mipLevel!=d.subresources.baseMipLevel",
                      "d.arrayLayer!=d.subresources.baseArrayLayer", "kTextureUploadRowPitchAlignment",
                      "RHI-TEXTURE-UPLOAD-LAYOUT-SOURCE-OVERLAP", "RHI-TEXTURE-UPLOAD-LAYOUT-BLOCK-REGION"]:
            self.assertIn(token,validation)
        for forbidden in ["WebGpuRecordedCommandProof", "completionEpoch", "effectiveMipLevel", "WebGpuOriginalBufferUploadSource"]:
            self.assertNotIn(forbidden,source+validation)
        self.assertNotRegex(source+validation,r"\bwgpu\w+\s*\(")

    def test_texture_upload_generation_cannot_echo_caller_revision_as_proof(self):
        source=self.read_source("src/rhi/frontend/transfer_upload_generation_validation.h")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertRegex(source,rf"case\s+Backend::{backend}:\s*return\s*\{{(?:true|false),\s*false,")
        for token in ["packing->hasQuotaBoundSharedOwner(packing)", "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN",
                      "RHI-TEXTURE-UPLOAD-GENERATION-VERSION", "RHI-TEXTURE-UPLOAD-GENERATION-OPT-IN", "return lowering(backend).diagnostic"]:
            self.assertIn(token,source)
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")
        self.assertNotIn("friend class",source)
        self.assertNotIn("return nullptr",source)
        self.assertNotIn("WebGpuRecordedCommandProof",source)

    def test_texture_content_transactions_cannot_publish_native_completed_content(self):
        source=self.read_source("src/rhi/frontend/transfer_upload_content_transaction.h")
        for token in ["TextureUploadContentSource", "TextureUploadContentLedger", "TextureUploadContentTransaction",
                      "claim.contentGeneration!=packing->inputOwner()->scalarFields().resourceGeneration()",
                      "core::VersionedMutationValue<ContentValue>", "core::MutationJournal journal_",
                      "core::AtomicCapacityLease countLease_,byteLease_", "core::BoundedSequence<std::shared_ptr<Domain>>",
                      "core::overlapsUnsignedRanges", "RHI-TEXTURE-CONTENT-DOMAIN-OVERLAP",
                      "RHI-TEXTURE-CONTENT-TRANSACTION-FOREIGN-EXPECTATION", "control.matchesPending(*pending_,journal_)",
                      "journal_.rollback()", "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN"]:
            self.assertIn(token,source)
        transaction=source.split("class TextureUploadContentTransaction final",1)[1]
        private=transaction.split("\nprivate:",1)[1]
        for token in ["markNativeSubmitted()", "markNativeCompleted()", "writeNativeCommitted", "journal_.commit()", "control.matches(*committed_)"]: self.assertIn(token,transaction)
        self.assertIn("markNativeSubmitted()",private)
        self.assertIn("markNativeCompleted()",private)
        self.assertIn("friend class rhi_webgpu::WebGpuOriginalTextureUploadTransaction",transaction)
        apply=transaction.split("static const char* apply(",1)[1].split("void abandon()",1)[0]
        self.assertNotIn("ContentState::Submitted",apply)
        self.assertNotIn("ContentState::Completed",apply)
        self.assertIn("ContentState::Proposed",apply)
        self.assertNotIn("WebGpuRecordedCommandProof",source)
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")
        self.assertNotRegex(source,r"friend class .*Fixture")

    def test_original_texture_destination_creates_fresh_scoped_exclusive_payload_only(self):
        base=self.ROOT / "src/rhi/backends/webgpu"
        header=self.read_webgpu_source("webgpu_original_texture_upload_destination.h")
        source=CppSource((base / "webgpu_staging_upload_pool.cpp").read_text(encoding="utf-8-sig")).split("WebGpuOriginalTextureUploadDestination::WebGpuOriginalTextureUploadDestination",1)[1].split("const char* WebGpuOriginalTextureUploadSource::validateBudget",1)[0]
        for token in ["WebGpuNativeTextureFactory::createOriginalUpload", "tryRegisterFreshTexture", "observeTransferResources",
                      "validateTransferResources", "WebGpuNativeErrorScopeBatch::push", "WebGpuNativeErrorScopeBatch::pop",
                      "evidence_.allReceived()", "evidence_.proven()", "validateOriginal()", "RHI-TEXTURE-DESTINATION-FOREIGN-CONTENT",
                      "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN"]:
            self.assertIn(token,source)
        for forbidden in ["wgpuQueueWrite", "wgpuQueueSubmit", "wgpuQueueOnSubmittedWorkDone", "ContentState::Submitted", "ContentState::Completed", "WebGpuRecordedCommandProof"]:
            self.assertNotIn(forbidden,source+header)
        self.assertNotRegex(source+header,r"friend class .*Fixture")
        native=self.read_webgpu_source("webgpu_native_payloads.h")
        guard=native.split("const char* ordinaryContentAccessDiagnostic() const noexcept",1)[1].split("const auto& contentAccessPolicy()",1)[0]
        for token in ["originalCanvas_->claimed_", "RHI-WEBGPU-ORIGINAL-CANVAS-EXCLUSIVE-USE", "accessPolicy_.ordinaryAccessDiagnostic()"]:
            self.assertIn(token,guard)
        factory=native.split("class WebGpuNativeTextureFactory final",1)[1].split("};",1)[0]
        self.assertIn("friend class WebGpuOriginalTextureUploadDestination",factory)
        self.assertLess(factory.index("private:"),factory.index("createOriginalUpload"))
        resources=CppSource((base / "webgpu_backend_objects.cpp").read_text(encoding="utf-8-sig"))
        registration=resources.split("bool WebGpuRhiResourceRegistry::tryRegisterFreshTexture",1)[1].split("void WebGpuRhiResourceRegistry::registerTexture",1)[0]
        self.assertIn("resources_.find(texture.id().value)!=resources_.end()",registration)
        self.assertIn("resources_.emplace",registration)
        self.assertNotIn("resources_[",registration)
        for name in ["webgpu_native_transfer_copy_lowering.h", "webgpu_transfer_copy_validation.h", "webgpu_native_transfer_commands.cpp", "webgpu_native_commands.cpp", "webgpu_native_resources.cpp", "webgpu_backend_descriptors.cpp", "webgpu_native_ordinary_physical_groups.cpp"]:
            self.assertIn("ordinaryContentAccessDiagnostic()" if name in ["webgpu_native_transfer_copy_lowering.h", "webgpu_transfer_copy_validation.h", "webgpu_native_transfer_commands.cpp"] else "ordinaryAccessDiagnostic()",CppSource((base / name).read_text(encoding="utf-8-sig")))
        proof=self.read_webgpu_source("webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_original_texture_source_retains_original_destination_through_buffer_callbacks(self):
        base=self.ROOT / "src/rhi/backends/webgpu"
        header=self.read_webgpu_source("webgpu_original_texture_upload_source.h")
        buffer=self.read_webgpu_source("webgpu_original_buffer_upload_source.h")
        impl=CppSource((base / "webgpu_staging_upload_pool.cpp").read_text(encoding="utf-8-sig"))
        wrapper=impl.split("const char* WebGpuOriginalTextureUploadSource::validateBudget",1)[1]
        for token in ["WebGpuOriginalBufferUploadSource::prepareOriginalTexture", "destination->validate()", "packing.packedBytes().size()", "maxRetainedContentBytes", "maxRetainedDestinationTexelBytes", "core::CpuMemoryQuotaScope wrapper(wrapperQuota)", "RHI-TEXTURE-UPLOAD-ORIGINAL-GENERATION-OWNER-UNPROVEN"]:
            self.assertIn(token,wrapper)
        self.assertIn("textureDestination_",buffer)
        self.assertIn("result->textureDestination_=std::move(destination)",impl)
        self.assertIn("registry_->findDescriptorFacts(self->resourceId_)",impl)
        self.assertIn("tryRegisterFreshBuffer",impl)
        self.assertIn("wgpuBufferGetSize",impl)
        self.assertIn("wgpuBufferGetUsage",impl)
        for forbidden in ["weak_ptr", "wgpuQueueWriteTexture", "ContentState::Submitted", "ContentState::Completed", "WebGpuRecordedCommandProof", "Callback"]:
            self.assertNotIn(forbidden,wrapper+header)
        self.assertNotRegex(header+buffer,r"friend class .*Fixture")
        proof=self.read_webgpu_source("webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_original_texture_packed_mapping_preserves_geometry_without_write_authority(self):
        source=self.read_source("src/rhi/frontend/transfer_upload_copy_mapping.h")
        for token in ["hasQuotaBoundSharedOwner", "mipIndex>=packing->mipLayouts().size()", "containsUnsignedRange", "value.subresources=mip.range", "value.extent=mip.extent", "value.origin=mip.origin", "value.rowsPerImage=mip.rowCount", "value.bytesPerRow=mip.packedBytesPerRow", "value.bufferOffsetBytes=mip.packedOffset", "value.contentGeneration=fields.resourceGeneration", "output=value", "expected.matches(input)"]:
            self.assertIn(token,source)
        binding=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h", "backends_webgpu_webgpu_original_texture_upload_source_h").split("static const char* mapPackedCopyOriginal",1)[1].split("private:",1)[0]
        for token in ["self->hasQuotaBoundSharedOwner(self)", "self->validate()", "input->hasQuotaBoundSharedOwner(input)", "input.get()!=self->packing()->inputOwner().get()", "SharedOwnerProjection::sameOwner", "self->resource().id"]:
            self.assertIn(token,binding)
        for forbidden in ["core::make", "core::pmr", "std::vector", "std::weak_ptr", "wgpu", "nativeCopy", "WebGpuRecordedCommandProof", "ContentState::Submitted", "ContentState::Completed"]:
            self.assertNotIn(forbidden,source+binding)
        original=self.read_source("src/rhi/backends/webgpu/webgpu_original_command_inputs.h")
        self.assertIn("case K::TextureUpload:return Family::TextureUpload",self.input_family_contract_source())

    def test_original_texture_graph_retains_exact_pair_without_ordinary_transfer_authority(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_command_inputs.h", "backends_webgpu_webgpu_generic_command_projection_h")
        for token in ["validateTextureUploads", "maxOriginalTextureUploadSources", "maxTexturePackedBytes", "maxTextureContentBytes", "maxTextureDestinationTexelBytes", "source->hasQuotaBoundSharedOwner(source)", "source->validate()", "RHI-WEBGPU-COMMAND-PROJECTION-TEXTURE-UPLOAD-DUPLICATE", "allocation(source->destination()->resource())", "Source::OriginalTextureUpload", "Domain::Texture", "Domain::Upload", "source->destination()->resource().texture.get()"]:
            self.assertIn(token,source)
        for forbidden in ["WebGpuRecordedCommandProof", "wgpuQueue", "wgpuCommandEncoder", "ContentState::Submitted", "ContentState::Completed"]:
            self.assertNotIn(forbidden,source)
        original=self.read_source("src/rhi/backends/webgpu/webgpu_original_command_inputs.h")
        self.assertIn("case K::TextureUpload:return Family::TextureUpload",self.input_family_contract_source())
        transfer=self.read_source("src/rhi/backends/webgpu/webgpu_transfer_copy_validation.h")
        self.assertIn("ordinaryContentAccessDiagnostic()",transfer)

    def test_original_texture_full_inputs_bind_snapshot_label_budgets_and_state_owners_without_execution(self):
        base=self.ROOT / "src/rhi/backends/webgpu"
        source=self.read_webgpu_source("webgpu_original_texture_upload_validation.h")
        for token in ["graph.validate()", "input->hasQuotaBoundSharedOwner(input)", "texture.source!=S::OriginalTextureUpload", "upload.source!=S::OriginalTextureUpload", "texture.sourceIndex!=upload.sourceIndex", "texture.resource!=fields.destinationTexture", "upload.resource!=fields.destinationTexture", "WebGpuOriginalTextureUploadSource::mapPackedCopyOriginal"]:
            self.assertIn(token,source)
        inputs=self.read_webgpu_source("webgpu_original_command_inputs.h")
        for token in ["maxTextureUploadDescriptors=0u", "maxTextureUploadMipMappings=0u", "maxTextureUploadRetainedPixelBytes=0u", "TextureUploadOriginalInputArguments", "WebGpuOriginalTextureUploadValidation::validate", "scalars->arguments()->text(i)!=input.label()", "TextureUploadLayoutValidation::mipCount(input)"]:
            self.assertIn(token,inputs)
        states=self.read_webgpu_source("webgpu_original_resource_states.h")
        self.assertIn("graph.originalTextureUploads()",states)
        self.assertIn("fromTransfer(source->resource())",states)
        self.assertIn("fromTransfer(source->destination()->resource())",states)
        for forbidden in ["WebGpuRecordedCommandProof", "wgpuQueue", "wgpuCommandEncoder", "ContentState::Submitted", "ContentState::Completed"]:
            self.assertNotIn(forbidden,source+inputs+states)
        state=self.read_webgpu_source("webgpu_original_transfer_state_validation.h")
        self.assertNotIn("case K::TextureUpload",state)
        proof=self.read_webgpu_source("webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_original_texture_current_transaction_requires_exact_pending_owner_and_preserves_initial_gates(self):
        base=self.ROOT / "src/rhi/backends/webgpu"
        source=self.read_webgpu_source("webgpu_original_texture_upload_transaction.h")
        for token in ["states->validate()", "validateInitialStream(states)", "maxOriginalSources=0u", "maxMipMappings=0u", "maxRetainedContentBytes=0u", "content_->matchesOriginalAdmission", "source->validateReadyDeclaration()", "content_->validate()", "control.pending()", "control.matches(original.observation)", "Content::apply(self->content_)", "RHI-WEBGPU-TEXTURE-TRANSACTION-RECORDING-UNPROVEN", "mapPackedCopyCurrent", "webGpuNativeDeviceLost()"]: self.assertIn(token,source)
        for forbidden in ["WebGpuRecordedCommandProof", "wgpuQueue", "wgpuCommandEncoder", "ContentState::Submitted", "ContentState::Completed"]: self.assertNotIn(forbidden,source)
        impl=CppSource((base / "webgpu_staging_upload_pool.cpp").read_text(encoding="utf-8-sig"))
        initial=impl.split("const char* WebGpuOriginalTextureUploadDestination::validateOriginal()",1)[1].split("const char* WebGpuOriginalTextureUploadDestination::validateContentJoin",1)[0]
        self.assertIn("validateNativeDeclaration()",initial)
        self.assertIn("ContentState::Uninitialized",initial)
        self.assertIn("current->internalRevision()!=expected_->internalRevision()",initial)
        header=self.read_webgpu_source("webgpu_original_texture_upload_source.h")
        self.assertLess(header.index("private:"),header.index("validateReadyDeclaration()"))
        self.assertEqual(re.findall(r"friend class (\w+);",header),["WebGpuOriginalTextureUploadTransaction","WebGpuOriginalTextureUploadSource"])
        content=self.read_source("src/rhi/frontend/transfer_upload_content_transaction.h")
        for token in ["matchesOriginalAdmission", "expected_.domain_.get()==expected.domain_.get()", "expected_.value_.revision==expected.value_.revision", "control.matchesPending(*pending_,journal_)"]: self.assertIn(token,content)
        proof=self.read_webgpu_source("webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_original_texture_copy_lowering_preserves_exact_geometry_without_execution(self):
        source=self.read_source("src/rhi/frontend/transfer_upload_copy.h")
        for token in ["case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU", "TextureUploadPackedCopyMapping::validateOriginal", "Outcome::Prototype", "packing.get()==other.packing.get()", "SharedOwnerProjection::sameOwner", "original.matches(other.original)", "original.origin.width", "original.origin.height", "original.origin.depth : original.subresources.baseArrayLayer", "original.extent.depth : original.subresources.arrayLayerCount", "blocksForExtent", "RHI-TEXTURE-COPY-LOWERING-PHYSICAL-OVERFLOW", "RHI-TEXTURE-COPY-LOWERING-DECISION-MISMATCH"]: self.assertIn(token,source)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_original_texture_upload_copy_validation.h")
        for token in ["mapPackedCopyCurrent", "Lowering::validateOriginal", "buffer.ownerDevice!=expectedDevice", "texture.ownerDevice!=expectedDevice", "originalUploadExclusive()", "texture.dimension!=dimension", "texture.rhiFormat!=mapped.format", "kTextureUploadRowPitchAlignment", "WGPU_TEXEL_COPY_BUFFER_INFO_INIT"]: self.assertIn(token,native)
        for forbidden in ["WebGpuRecordedCommandProof", "ContentState::Submitted", "ContentState::Completed", "wgpuQueue", "wgpuCommandEncoder", "friend class"]: self.assertNotIn(forbidden,source+native)
        ordinary=self.read_source("src/rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h")
        self.assertIn("ordinaryContentAccessDiagnostic()",ordinary)

    def test_original_texture_native_profile_uses_existing_proof_and_completion_with_private_content_mutations(self):
        base=self.ROOT / "src/rhi/backends/webgpu"
        emit=self.read_webgpu_source("webgpu_native_original_texture_upload_lowering.h")
        self.assertEqual(re.findall(r"friend class (\w+);",emit),["WebGpuOrdinaryRecording"])
        for token in ["WebGpuOriginalTextureUploadCopyValidation::validateCurrent", "transaction->decisions()[index]", "decision.original.bufferOffsetBytes", "decision.original.bytesPerRow", "decision.original.rowsPerImage", "decision.original.subresources.baseMipLevel", "decision.physicalOrigin", "decision.physicalExtent", "wgpuCommandEncoderCopyBufferToTexture"]: self.assertIn(token,emit)
        self.assertNotIn("wgpuQueueWriteTexture",emit)
        record=CppSource((base / "webgpu_native_ordinary_recording.cpp").read_text(encoding="utf-8-sig"))
        for token in ["prepareOriginalTextureUpload", "recordOriginalTextureUpload", "ordinaryOriginalTextureTransaction=originalTextureTransaction", "WebGpuOriginalTextureUploadTransaction::apply", "WebGpuNativeOriginalTextureUploadLowering::emit", "self.originalTextureTransaction_->abandon()"]: self.assertIn(token,record)
        submit=CppSource((base / "webgpu_native_ordinary_submission.cpp").read_text(encoding="utf-8-sig"))
        for token in ["retained.submitWith", "WebGpuOriginalTextureUploadTransaction::markSubmitted", "WebGpuOriginalTextureUploadTransaction::markCompleted", "evidence_.allReceived()", "evidence_.proven()", "graph->quarantineState()"]: self.assertIn(token,submit)
        self.assertLess(submit.index("if (!evidence_.allReceived()) return;"),submit.index("WebGpuOriginalTextureUploadTransaction::markCompleted"))
        transaction=self.read_webgpu_source("webgpu_original_texture_upload_transaction.h")
        private=transaction.split("\nprivate:\n    friend class WebGpuOrdinarySubmission;",1)[1]
        self.assertIn("static const char* markSubmitted",private)
        self.assertIn("static const char* markCompleted",private)
        self.assertIn("invalidateRecordedState()",transaction)
        completion=self.read_webgpu_source("webgpu_ordinary_completion_retention.h")
        self.assertIn("command_->originalTextureTransaction(),textureTransaction_",completion)
        self.assertIn("textureTransaction_->invalidate()",completion)
        self.assertEqual(re.findall(r"friend class (\w+);",self.read_webgpu_source("webgpu_recorded_command_proof.h")),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_texture_readback_input_cannot_issue_original_result_authority(self):
        source=self.read_source("src/rhi/frontend/transfer_readback_arguments.h")
        self.assertEqual(re.findall(r"friend class (\w+);",source),["TextureReadbackInputSnapshot"])
        for token in ["TextureReadbackArguments::captureScalarFields", "core::CpuMemoryQuotaScope metadata(metadataQuota)",
                      "CommandVariableArguments::prepareOriginal", "TextureCopyMipLayoutArguments::capture",
                      "core::BoundedSequence<TextureToBufferCopyDesc::MipLayout>", "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN"]:
            self.assertIn(token,source)
        for forbidden in ["WebGpuRecordedCommandProof", "WebGpuNative", "completionEpoch", "mapAsync", "effectiveMipLevel", "std::make_shared"]:
            self.assertNotIn(forbidden,source)
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")
        original=self.read_source("src/rhi/backends/webgpu/webgpu_original_command_inputs.h")
        self.assertIn("case K::TextureReadback:return Family::TextureReadback",self.input_family_contract_source())
        upload=self.read_source("src/rhi/frontend/transfer_upload_input.h", "frontend_transfer_upload_arguments_h")
        self.assertIn("TextureCopyMipLayoutArguments::capture(a).matches(b)",upload)

    def test_texture_readback_layout_is_owned_preflight_metadata_only(self):
        source=self.read_source("src/rhi/frontend/transfer_readback_input.h", "frontend_transfer_readback_layout_h")
        for token in ["core::PreflightReplay<Mip>::run", "core::CpuMemoryQuotaScope scope(quota)",
                      "input->hasQuotaBoundSharedOwner(input)", "core::BoundedSequence<Mip>",
                      "count>budget.maxMipCopies", "d.sizeBytes>budget.maxReadbackBytes",
                      "RHI-TEXTURE-READBACK-ORIGINAL-OWNER-UNPROVEN"]:
            self.assertIn(token,source)
        self.assertEqual(re.findall(r"friend class (\w+);",source),["TextureReadbackLayout"])
        validation=self.read_source("src/rhi/frontend/transfer_readback_layout_validation.h")
        for token in ["TextureTransferValidation::copyLayoutValid", "TextureSubresourceExtent::resolve", "core::StridedIndexRange",
                      "RHI-TEXTURE-READBACK-LAYOUT-LEGACY-EXTENT", "RHI-TEXTURE-READBACK-LAYOUT-BLOCK-EDGE",
                      "RHI-TEXTURE-READBACK-LAYOUT-OVERLAP", "first.bufferOffsetBytes!=d.destinationOffsetBytes",
                      "TextureReadbackMip mapped", "output=mapped"]:
            self.assertIn(token,validation)
        for forbidden in ["WebGpuRecordedCommandProof", "WebGpuNative", "mapAsync", "completionEpoch", "std::make_shared"]:
            self.assertNotIn(forbidden,source+validation)
        self.assertNotRegex(source+validation,r"\bwgpu\w+\s*\(")
        upload=self.read_source("src/rhi/frontend/transfer_upload_layout_validation.h")
        self.assertIn("using TextureUploadDestinationLayout=TextureResourceLayout",upload)

    def test_readback_source_binding_preserves_original_object_and_closed_authority(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_source_h")
        for token in ["core::UniquePtr<rhi::GpuTexture>& source", "core::CpuMemoryQuotaScope scope(quota)",
                      "layout->hasQuotaBoundSharedOwner(layout)", "registry->sharedOwnerMatches(registry)",
                      "registry->observeTransferResources", "registry.validateTransferResources",
                      "webGpuNativePayloadFor(source)", "ordinaryContentAccessDiagnostic()",
                      "recordedState().matches(result->state_)", "observation.ownerCount()+cpuOwners",
                      "RHI-READBACK-SOURCE-EXECUTION-UNPROVEN", "RHI-READBACK-SOURCE-STATE-PENDING"]:
            self.assertIn(token,source)
        adoption=source.index("result->source_=std::move(source)")
        self.assertGreater(adoption,source.index("auto result=shareOriginal"))
        self.assertGreater(adoption,source.index("recordedState().matches(result->state_)"))
        self.assertEqual(re.findall(r"friend class (\w+);",source),["WebGpuOriginalTextureReadbackSource"])
        for forbidden in ["WebGpuRecordedCommandProof", "completionEpoch", "mapAsync", "createWebGpuRhiBuffer", "std::make_shared"]:
            self.assertNotIn(forbidden,source)
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")

    def test_readback_destination_is_cold_private_creation_with_unpublished_result(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_destination_h")
        for token in ["source->hasQuotaBoundSharedOwner(source)", "source->validate()", "wgpuDeviceGetLimits", "core::Utf8Decoder::validate(label)",
                      "core::CpuMemoryQuotaScope metadata(metadataQuota)", "core::CpuMemoryQuotaScope resource(resourceQuota)",
                      "core::CpuMemoryQuotaScope payload(resultQuota)", "result->result_.resize", "WebGpuNativeBufferCreation::prepareReadbackOriginal",
                      "WebGpuNativeErrorScopeBatch::push", "WebGpuNativeErrorScopeBatch::pop", "evidence_.proven()",
                      "wgpuBufferGetSize", "wgpuBufferGetUsage", "WebGpuNativeBufferMapState::matches", "sameOwner(allocation_.releaseToken",
                      "RHI-READBACK-DESTINATION-EXECUTION-UNPROVEN", "bool resultPublished() const noexcept {return resultPublication_.isComplete();}"]:
            self.assertIn(token,source)
        for forbidden in ["registerBuffer", "WebGpuRecordedCommandProof", "completionEpoch", "wgpuBufferMapAsync",
                          "wgpuDeviceCreateCommandEncoder", "wgpuQueue", "g_readbackSamples", "std::make_shared"]:
            self.assertNotIn(forbidden,source)
        resources=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        self.assertIn("bufferDesc.mappedAtCreation = false",resources)
        self.assertEqual(re.findall(r"friend class (\w+);",source),["WebGpuOriginalTextureReadbackDestination","WebGpuOriginalTextureReadbackPublication","WebGpuOriginalTextureReadbackMapping","WebGpuOriginalTextureReadbackResult"])

    def test_completed_upload_declaration_cannot_synthesize_readback_or_completed_status(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_texture_upload_transaction.h")
        for token in ["validateCompletedDeclarationOriginal", "self->hasQuotaBoundSharedOwner(self)",
                      "status_!=Status::Completed", "validateNativeContentDeclaration(Content::Status::Completed)",
                      "content_->matchesOriginalAdmission", "content_->validate()", "content_->status()!=expected",
                      "control.matches(original.observation)", "friend class WebGpuOrdinarySubmission"]:
            self.assertIn(token,source)
        method=source[source.index("static const char* validateCompletedDeclarationOriginal"):source.index("static const char* apply")]
        for forbidden in ["status_=", "recordState", "mapAsync", "wgpu", "WebGpuRecordedCommandProof"]:
            self.assertNotIn(forbidden,method)
        self.assertIn("return self->validateCompletedDeclaration()",method)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_readback_content_coverage_is_owned_logical_linear_metadata_only(self):
        source=self.read_source("src/rhi/frontend/transfer_readback_coverage.h")
        for token in ["readback->hasQuotaBoundSharedOwner(readback)", "upload->hasQuotaBoundSharedOwner(upload)",
                      "fields.sourceResource!=upload->inputOwner()->scalarFields().destination()",
                      "upload->destinationLayout().matches(source)", "reads.size()>budget.maxReadbackMips",
                      "writes.size()>budget.maxUploadMips", "std::size_t cursor=0u", "while(cursor<writes.size()",
                      "core::containsUnsignedRange(write.origin.width", "core::containsUnsignedRange(write.origin.height",
                      "core::containsUnsignedRange(write.origin.depth", "core::containsUnsignedRange(write.range.baseArrayLayer",
                      "RHI-TEXTURE-READBACK-COVERAGE-EXECUTION-UNPROVEN"]:
            self.assertIn(token,source)
        for forbidden in ["physicalExtent", "WebGpuNative", "Completed", "WebGpuRecordedCommandProof", "makeShared", "makeUnique", "mapAsync"]:
            self.assertNotIn(forbidden,source)
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")

    def test_completed_upload_view_is_sealed_original_graph_with_no_native_authority(self):
        view=self.read_source("src/rhi/backends/webgpu/webgpu_completed_texture_upload_declaration.h")
        self.assertEqual(re.findall(r"friend class (\w+);",view),["WebGpuOriginalTextureUploadTransaction"])
        self.assertLess(view.index("WebGpuCompletedTextureUploadDeclaration()=default"),view.index("public:"))
        for token in ["QuotaBoundSharedObjectIdentity", "self->hasQuotaBoundSharedOwner(self)",
                      "self->validateCompletedDeclaration()", "completedTextureResource()", "retainedNativeAllocationBytes()"]:
            self.assertIn(token,view)
        for forbidden in ["generic_command_projection.h", "original_texture_upload_transaction.h", "makeShared", "makeUnique",
                          "WebGpuRecordedCommandProof", "wgpu", "status_="]:
            self.assertNotIn(forbidden,view)
        transaction=self.read_source("src/rhi/backends/webgpu/webgpu_original_texture_upload_transaction.h")
        self.assertIn("final : public WebGpuCompletedTextureUploadDeclaration",transaction)
        self.assertIn("validateCompletedDeclaration() const noexcept override",transaction)
        self.assertIn("projection()->retainedAllocationBytes()",transaction)

    def test_completed_readback_source_is_sealed_covered_bounded_original_graph(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_source_h")
        for token in ["prepareCompletedOriginal", "WebGpuCompletedTextureUploadDeclaration::validateOriginal(completed)",
                      "TextureReadbackContentCoverage::validateOriginal", "maxCompletedGraphs", "maxReadbackMips", "maxUploadMips",
                      "observation.ownerCount()+2u", "b.maxTargetAllocationBytes", "b.maxRetainedNativeBytes", "b.maxRetainedContentBytes",
                      "original.sameOwnersAndLayout(observation)", "originalUploadExclusive()", "result->completed_=std::move(completed)",
                      "completed_->completedTexture()", "RHI-READBACK-SOURCE-STATE-STALE"]:
            self.assertIn(token,source)
        for forbidden in ["original_texture_upload_transaction.h", "generic_command_projection.h", "WebGpuRecordedCommandProof", "wgpuQueue", "mapAsync"]:
            self.assertNotIn(forbidden,source)
        destination=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_destination_h")
        self.assertIn("RHI-READBACK-DESTINATION-SOURCE-PROFILE",destination)

    def test_completed_readback_destination_has_private_profile_and_full_graph_budget(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_destination_h")
        for token in ["prepareCompletedOriginal", "source->retainedCompletedGraphCount()>b.maxCompletedGraphs",
                      "source->retainedContentBytes()>b.maxRetainedContentBytes", "SourceProfile::Ordinary", "SourceProfile::CompletedUpload",
                      "source->retainedSourceOwnerCount()+1u", "source->retainedNativeAllocationBytes()>b.maxRetainedSourceBytes",
                      "RHI-READBACK-DESTINATION-SOURCE-PROFILE", "RHI-READBACK-COMPLETED-DESTINATION-CONTENT-CAPACITY"]:
            self.assertIn(token,source)
        self.assertLess(source.index("private:\n    static std::shared_ptr<WebGpuOriginalTextureReadbackDestination> prepareAdmitted"),source.index("source->hasCompletedUpload()!=(profile"))
        for forbidden in ["wgpuBufferMapAsync", "wgpuDeviceCreateCommandEncoder", "WebGpuRecordedCommandProof", "registerBuffer"]:
            self.assertNotIn(forbidden,source)

    def test_original_readback_pair_reuses_original_mips_without_emission(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_pair_projection_h")
        for token in ["destination->hasQuotaBoundSharedOwner(destination)", "destination->validate()", "source.validateReadbackContentProfile()",
                      "MemoryBudgetLedger::canAdd", "source.retainedSourceOwnerCount()+4u", "b.maxRetainedNativeBytes", "b.maxRetainedContentBytes",
                      "b.maxUnpublishedResultBytes", "source.layout()->mipCopies().size()>b.maxMipCopies", "core::CpuMemoryQuotaScope scope(quota)",
                      "TextureReadbackLayout::validateOriginalMip", "sameOwner(input,self->layout()->inputOwner())",
                      "WebGpuNativeTransferCopyLowering::textureDiagnostic", "output=self->layout()->mipCopies()[index]",
                      "RHI-READBACK-PAIR-EXECUTION-UNPROVEN"]:
            self.assertIn(token,source)
        for forbidden in ["BoundedSequence", "Vector<", "mapAsync", "wgpuQueue", "WebGpuRecordedCommandProof", "original_texture_upload_transaction.h", "generic_command_projection.h"]:
            self.assertNotIn(forbidden,source)
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")
        self.assertEqual(re.findall(r"friend class (\w+);",source),["WebGpuOriginalTextureReadbackPairProjection"])

    def test_readback_allocation_records_are_original_sealed_ledger_inventory(self):
        view=self.read_source("src/rhi/backends/webgpu/webgpu_completed_texture_upload_declaration.h")
        self.assertIn("retainedNativeAllocationRecordCount()",view)
        self.assertIn("SharedAllocationProjection::Record retainedNativeAllocationRecordAt",view)
        transaction=self.read_source("src/rhi/backends/webgpu/webgpu_original_texture_upload_transaction.h")
        for token in ["originalSource()->resource().allocationRecord()", "originalSource()->destination()->resource().allocationRecord()"]:
            self.assertIn(token,transaction)
        pair=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_pair_projection_h")
        for token in ["core::SharedAllocationProjection::inspect", "sourceRecords>=b.maxAllocationRecords", "inventory.bytes!=bytes",
                      "source.retainedNativeAllocationRecordAt(index)", "destination->allocationRecord()", "allocationRecordCount()", "allocationRecordAt(std::size_t index)"]:
            self.assertIn(token,pair)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_source_h")
        self.assertIn("completed_->retainedNativeAllocationRecordAt(index)",source)
        for forbidden in ["generic_command_projection.h", "original_texture_upload_transaction.h", "WebGpuRecordedCommandProof", "std::make_shared"]:
            self.assertNotIn(forbidden,source+pair+view)

    def test_readback_graph_membership_is_typed_budgeted_and_private(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_command_inputs.h", "backends_webgpu_webgpu_generic_command_projection_h")
        for token in ["OriginalTextureUpload, OriginalTextureReadback", "maxOriginalReadbackPairs=0u", "maxReadbackMipCopies=0u",
                      "maxReadbackRetainedContentBytes=0u", "maxReadbackUnpublishedResultBytes=0u", "validateReadbacks", "validateReadbackReferences",
                      "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-MULTI-PAIR-UNPROVEN", "pair->hasQuotaBoundSharedOwner(pair)", "pair->validate()",
                      "reference.domain!=Domain::Texture", "result.domain!=Domain::Readback", "pair->allocationRecordAt(index)",
                      "readbacks_(budget.maxOriginalReadbackPairs)", "readbacks_.append(sources.readbacks[i])", "pair->allocationRecordCount()"]:
            self.assertIn(token,source)
        resolve=source[source.index("if(reference.source==Source::OriginalTextureReadback ||"):source.index("if(reference.source==Source::OriginalTextureUpload)")]
        self.assertIn("return pair.get()",resolve)
        self.assertNotIn("Domain::Buffer",resolve)
        inputs=self.read_source("src/rhi/backends/webgpu/webgpu_original_command_inputs.h")
        self.assertIn("case K::TextureReadback:return Family::TextureReadback",self.input_family_contract_source())
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_readback_full_input_and_states_retain_exact_complete_original_graph(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_command_inputs.h")
        for token in ["TextureReadbackOriginalInputArguments", "Family::TextureReadback", "maxReadbackDescriptors=0u", "maxReadbackMipMappings=0u",
                      "maxReadbackUnpublishedResultBytes=0u", "WebGpuOriginalTextureReadbackValidation::validate", "scalars->arguments()->text(i)!=input.label()"]:
            self.assertIn(token,source)
        validation=self.read_source("src/rhi/backends/webgpu/webgpu_original_texture_readback_validation.h")
        for token in ["graph.validate()", "input->hasQuotaBoundSharedOwner(input)", "texture.source!=S::OriginalTextureReadback",
                      "readback.source!=S::OriginalTextureReadback", "sameOwner(input,pair->layout()->inputOwner())", "mapMipOriginal",
                      "texture.resource!=fields.sourceResource", "readback.resource!=fields.destinationReadbackBuffer"]:
            self.assertIn(token,validation)
        states=self.read_source("src/rhi/backends/webgpu/webgpu_original_resource_states.h")
        for token in ["graph.originalReadbacks()", "retainedNativeResourceAt(i)", "RHI-WEBGPU-ORIGINAL-STATES-READBACK-SOURCE-MISSING",
                      "destination.resourceId(),Domain::Buffer,buffer,Control{buffer->recordedState()}"]:
            self.assertIn(token,states)
        for forbidden in ["wgpuQueue", "mapAsync", "WebGpuRecordedCommandProof"]:
            self.assertNotIn(forbidden,source+validation+states)

    def test_readback_declaration_separates_identity_from_initial_access(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_source_h")
        destination=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_destination_h")
        pair=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_pair_projection_h")
        for code in [source,destination,pair]:
            self.assertIn("const char* validateReadyDeclaration() const noexcept",code)
            self.assertNotIn("bool allowPending",code)
            self.assertNotIn("WebGpuRecordedCommandProof",code)
            self.assertNotIn("wgpuQueue",code)
        ready=source.split("const char* validateReadyDeclaration() const noexcept",1)[1].split("const char* executionDiagnostic",1)[0]
        self.assertNotIn("matches(state_)",ready)
        self.assertIn("WebGpuCompletedTextureUploadDeclaration::validateOriginal(completed_)",ready)
        self.assertIn("validateCurrentState(*registry_,observation_)",source)
        self.assertIn("recordedState().matches(state_)",source)
        self.assertIn("if(!evidence_.proven())",destination)
        identity=destination.split("const char* validateNativeIdentity() const noexcept",1)[1].split("void observe",1)[0]
        for token in ["originalBuffer_","recordedState().active()","allocation_.releaseToken","wgpuBufferGetSize","wgpuBufferGetUsage"]:
            self.assertIn(token,identity)
        self.assertNotIn("wgpuBufferGetMapState",identity)
        self.assertNotIn("recordedState().pending()",identity)
        self.assertIn("!WebGpuNativeBufferMapState::matches(native_->buffer,WGPUBufferMapState_Unmapped)",destination)
        self.assertIn("destination_->validateReadyDeclaration()",pair)
        self.assertIn("if(const auto* id=self->validate())return id;",pair)

    def test_readback_state_intent_is_explicit_positional_and_source_only(self):
        graph=self.read_source("src/rhi/backends/webgpu/webgpu_command_inputs.h", "backends_webgpu_webgpu_generic_command_projection_h")
        inputs=self.read_source("src/rhi/backends/webgpu/webgpu_original_command_inputs.h")
        states=self.read_source("src/rhi/backends/webgpu/webgpu_original_transfer_state_validation.h")
        for token in ["OriginalTextureReadback, OriginalTextureReadbackState", "maxReadbackStateReferences=0u", "stateReferences==maxStateReferences",
                      "RHI-WEBGPU-COMMAND-PROJECTION-READBACK-STATE-REFERENCE", "if(reference.source==Source::OriginalTextureReadbackState)return nullptr;"]:
            self.assertIn(token,graph)
        self.assertIn("RHI-WEBGPU-ORIGINAL-READBACK-STATE-KIND",inputs)
        self.assertIn("kind!=rhi::CommandUsageKind::TextureBarrier",inputs)
        for token in ["case K::TextureReadback:diagnostic=validateReadback", "stateBefore(states,fields.sourceResource,i,source)",
                      "stateBefore(states,fields.destinationReadbackBuffer,i,destination)", "source==rhi::ResourceState::CopySrc && destination==rhi::ResourceState::CopyDst",
                      "RHI-WEBGPU-ORIGINAL-READBACK-STATE-MISMATCH", "defaultTextureSubresourceRange(source->textureDesc)"]:
            self.assertIn(token,states)
        for forbidden in ["WebGpuRecordedCommandProof", "wgpuQueue", "recordState(", "mapAsync"]:
            self.assertNotIn(forbidden,states)

    def test_readback_current_reuses_original_journal_with_private_profile(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_state_transaction.h")
        for token in ["enum class Profile {Transfer,Readback}", "prepareReadbackOriginal", "validateInitialReadbackStream", "maxPrivateAccessClaims=0u",
                      "maxUnpublishedResultBytes=0u", "Profile::Readback", "originalReadback()->validateReadyDeclaration()", "unmappedAccessDiagnostic()",
                      "nativeResource()->recordState(rhi::ResourceState::CopyDst,&self->journal_)", "matchesPending(*entry.pending,journal_)",
                      "retainedNativeResourceAt(i)", "invalidateRecordedState();journal_.rollback();status_=Status::Invalidated"]:
            self.assertIn(token,source)
        for forbidden in ["bool allowPending", "WebGpuRecordedCommandProof", "wgpuQueue", "wgpuBufferMapAsync", "reinterpret_cast", "const_cast"]:
            self.assertNotIn(forbidden,source)
        self.assertEqual(source.count("struct Entry {"),1)
        self.assertEqual(source.count("core::MutationJournal journal_;"),1)
        self.assertIn("commands[0].kind!=K::TextureBarrier",source)
        self.assertIn("commands[2].kind!=K::TextureReadback",source)

    def test_readback_copy_lowering_reuses_original_geometry_without_authority(self):
        lowering=self.read_source("src/rhi/frontend/transfer_readback_copy.h")
        copies=self.read_source("src/rhi/backends/webgpu/webgpu_original_texture_readback_copies.h")
        current=self.read_source("src/rhi/backends/webgpu/webgpu_original_state_transaction.h")
        for token in ["RHI-READBACK-COPY-LOWERING-NULL-VALIDATOR-ONLY", "Backend::Vulkan:case Backend::Metal", "RHI-READBACK-COPY-LOWERING-BACKEND-INVALID",
                      "TextureReadbackLayout::validateOriginalMip", "value.original=original", "expected.matches(decision)"]:
            self.assertIn(token,lowering)
        for token in ["maxMipMappings=0u,maxCopyDecisions=0u", "Status::Prepared", "decisions_(capacity)", "mapReadbackMipCurrent", "source.validateReadbackContentProfile()", "source.validateReadyDeclaration()",
                      "textureDiagnostic(texture,mapped.range,mapped.extent)", "RHI-WEBGPU-READBACK-COPIES-RECORDING-UNPROVEN"]:
            self.assertIn(token,copies)
        for forbidden in ["blocksForExtent", "WebGpuRecordedCommandProof", "wgpuQueue", "wgpuCommandEncoder", "wgpuBufferMapAsync"]:
            self.assertNotIn(forbidden,lowering+copies)
        self.assertEqual(re.findall(r"friend class (\w+);",copies),["WebGpuOriginalTextureReadbackCopies","WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])
        mapper=current.split("static const char* mapReadbackMipCurrent",1)[1].split("const auto& states()",1)[0]
        self.assertIn("Status::Pending && self->status_!=Status::Committed",mapper)
        self.assertIn("self->validate()",mapper)
        self.assertIn("sameOwner(input,layout->inputOwner())",mapper)
        self.assertLess(mapper.index("validateOriginalMip"),mapper.index("output=layout->mipCopies()[index]"))

    def test_readback_cold_recording_binds_common_owners_without_gpu_authority(self):
        recording=self.read_source("src/rhi/backends/webgpu/webgpu_native_ordinary_recording.cpp")
        completion=self.read_source("src/rhi/backends/webgpu/webgpu_ordinary_completion_retention.h")
        cold=recording.split("WebGpuOrdinaryRecording::prepareOriginalTextureReadback",1)[1].split("const char* WebGpuOrdinaryRecording::validateOriginalTextureReadbackInputs",1)[0]
        for token in ["prepareReadbackOriginal", "WebGpuOriginalTextureReadbackCopies::prepareOriginal", "budget.maxMipCopies", "budget.native.maxCommands", "initialDependencyMapping", "Status::Prepared"]:
            self.assertIn(token,cold)
        for forbidden in ["wgpuCommandEncoder", "wgpuQueue", "wgpuBufferMapAsync", "WebGpuRecordedCommandProof"]:
            self.assertNotIn(forbidden,cold)
        self.assertIn("ordinaryOriginalReadbackCopies=originalReadbackCopies",recording)
        for token in ["readbackCopies_", "originalReadbackCopies()", "sameOwner", "readbackCopies_->validate()"]:
            self.assertIn(token,completion)

    def test_readback_native_recording_consumes_original_dependency_and_mips(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_ordinary_recording.cpp")
        record=source.split("const char* WebGpuOrdinaryRecording::recordOriginalTextureReadback",1)[1].split("WebGpuOrdinaryRecording::allocate",1)[0]
        self.assertLess(record.index("ImplicitOrderedUsage"),record.index("WebGpuOriginalStateTransaction::apply"))
        for token in ["Status::RecordingPending", "self->pushScopes()", "WebGpuNativeOriginalTextureReadbackLowering::emit", "self->finishBuffer()", "self->popScopes(self,scopeCount)"]:
            self.assertIn(token,record)
        emitter=self.read_source("src/rhi/backends/webgpu/webgpu_native_original_texture_readback_lowering.h")
        self.assertEqual(re.findall(r"friend class (\w+);",emitter),["WebGpuOrdinaryRecording","WebGpuConformanceClearFrame"])
        for token in ["Status::Pending", "validateCurrent", "mip.range.baseMipLevel", "mip.physicalOrigin", "mip.physicalExtent", "mip.bufferOffsetBytes", "mip.bytesPerRow", "mip.rowsPerImage", "WGPUTextureAspect_All", "wgpuCommandEncoderCopyTextureToBuffer"]:
            self.assertIn(token,emitter)
        for forbidden in ["blocksForExtent", "alignCpu", "wgpuQueue", "wgpuBufferMapAsync"]:
            self.assertNotIn(forbidden,emitter)
        submit=self.read_source("src/rhi/backends/webgpu/webgpu_native_ordinary_submission.cpp")
        self.assertIn("validateNativeRecorded",submit)

    def test_readback_native_completion_is_private_and_independent_from_cpu_commit(self):
        copies=self.read_source("src/rhi/backends/webgpu/webgpu_original_texture_readback_copies.h")
        private=copies.split("friend class WebGpuOrdinarySubmission;",1)[1]
        for token in ["markNativeRecorded", "markNativeSubmitted", "markNativeCompleted", "ticketIndex_!=index", "ticketGeneration_!=generation", "nativeQueue_!=queue", "NativeStatus::Completed"]:
            self.assertIn(token,private)
        current=self.read_source("src/rhi/backends/webgpu/webgpu_original_state_transaction.h")
        self.assertNotIn("markNative",current)
        self.assertNotIn("NativeStatus",current)
        submit=self.read_source("src/rhi/backends/webgpu/webgpu_native_ordinary_submission.cpp")
        complete=submit.split("if (!evidence_.allReceived()) return;",1)[1]
        self.assertLess(complete.index("evidence_.proven() && graph && graph->matches"),complete.index("markNativeCompleted"))
        self.assertLess(submit.index("markNativeSubmitted"),submit.index("wgpuQueueSubmit"))
        for forbidden in ["wgpuBufferMapAsync", "wgpuBufferGetMappedRange", "resultPublished"]:
            self.assertNotIn(forbidden,submit+copies)

    def test_readback_cold_mapping_reuses_owned_storage_and_exclusive_core_ticket(self):
        mapping=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_mapping_h")
        for token in ["maxMapOperations=0u,maxCallbacks=0u,maxResultDeclarations=0u,maxMipLayouts=0u", "maxCpuMapMetadataBytes", "maxCpuResultMetadataBytes", "Status::Prepared", "NativeStatus::Unrecorded", "destination->result_.data()", "destination->resultPublication_.reserve()", "resultPublication_.release(ticket_)", "resultPublication_.matches(ticket_)"]:
            self.assertIn(token,mapping)
        for forbidden in ["wgpuQueue", "WebGpuRecordedCommandProof", "signalFromHost", "result_.resize"]:
            self.assertNotIn(forbidden,mapping)
        destination=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_destination_h")
        self.assertIn("core::CpuMemoryQuotaScope payload(resultQuota)",destination)
        self.assertIn("result->result_.resize",destination)
        self.assertIn("core::runtime::CompletionSignal resultPublication_",destination)

    def test_readback_native_map_requires_exact_proof_and_all_evidence(self):
        mapping=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_mapping_h")
        start=mapping.split("static const char* start",1)[1].split("const auto& copies()",1)[0]
        self.assertLess(start.index("validateNativeCompletionOriginal"),start.index("wgpuBufferMapAsync"))
        self.assertLess(start.index("callback_.release()"),start.index("wgpuBufferMapAsync"))
        for token in ["maxScopeCallbacks=0u", "scopeCallbackCount=WebGpuNativeErrorScopeBatch::count", "Status::Prepared", "WGPUMapMode_Read,0u,self->result_->bytes_", "wgpuBufferReadMappedRange", "WGPUStatus_Success", "wgpuBufferUnmap", "WGPUBufferMapState_Mapped", "WGPUBufferMapState_Unmapped", "validateNativeCompletedDeclarationOriginal", "evidence_.proven()", "resultPublication_.complete(ticket_)"]:self.assertIn(token,mapping)
        publish=mapping.split("void joinEvidence()",1)[1].split("void unmapIssued()",1)[0]
        self.assertLess(publish.index("evidence_.proven()"),publish.index("resultPublication_.complete"))
        self.assertNotIn("const_cast",mapping)
        destination=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_destination_h")
        self.assertIn("mutable core::pmr::Vector<unsigned char> result_",destination)

    def test_readback_native_map_state_observes_real_browser_and_bridge(self):
        state=self.read_source("src/rhi/backends/webgpu/webgpu_native_buffer_map_state.h")
        for token in ["wgpuBufferGetMapState(buffer)!=expected", "WebGPU.getJsObject", 'b.mapState==="unmapped"', 'b.mapState==="mapped"', "if(!b)return 0"]:self.assertIn(token,state)
        for file in ["webgpu_original_texture_readback_destination.h", "webgpu_original_texture_readback_mapping.h"]:
            source=self.read_webgpu_source(file)
            self.assertIn("WebGpuNativeBufferMapState::matches",source)
            self.assertNotIn("wgpuBufferGetMapState",source)

    def test_readback_result_snapshot_binds_original_publication_ticket(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_mapping_h")
        result=source.split("struct WebGpuOriginalTextureReadbackMappingBudget",1)[0]
        for token in ["validatePublishedOriginal", "hasQuotaBoundSharedOwner", "isComplete(self->ticket_)", "OriginalPaddedBufferBytes", "const core::runtime::CompletionSignal::Ticket ticket_"]:self.assertIn(token,result)
        self.assertIn("resultPublication_.reset()",source)
        self.assertIn("result_->ticket_.generation!=ticket_.generation",source)
        self.assertNotIn("webGpuNativeDeviceLost",result)

    def test_query_input_is_exact_cpu_metadata_not_native_owner(self):
        source=self.read_source("src/rhi/frontend/query_pool_arguments.h")
        self.assertEqual(re.findall(r"friend class (\w+);",source),["QueryPoolInputSnapshot"])
        for token in ["scopePointerPresent_==(d.timingScopeIds!=nullptr)", "QueryPoolDescriptorValidation::fromFields", "core::BoundedSequence<std::uint64_t>", "maxCpuMetadataBytes", "maxCpuLabelBytes", "maxCpuScopeBytes", "d.timingScopeIdCount && !d.timingScopeIds", "CommandScalarArguments::Query{index}", "SharedOwnerProjection::sameOwner", "RHI-QUERY-ORIGINAL-OWNER-UNPROVEN", "Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU"]:self.assertIn(token,source)
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")
        self.assertNotIn("QueryExecutionState::Available",source)
        self.assertNotIn("std::make_shared",source)
        common=self.read_source("src/rhi/gpu_common.h")
        wrapper=common.split("inline QueryPoolDescriptorValidation validateQueryPoolDesc",1)[1].split("struct DeviceCapabilities",1)[0]
        self.assertIn("QueryPoolDescriptorValidation::fromFields",wrapper)

    def test_readback_preparation_retains_existing_pending_ticket_until_original_scopes_drain(self):
        sink=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_publication_h")
        for token in ["initializeDestinationOriginal", "CreationDrainRetention", "creationDrained", "!entry.destination->creationScopesObserved()", "PREPARATION-PLAN", "PREPARATION-QUOTA", "PREPARATION-DESTINATION-MISMATCH", "additionalNative=native-entry->charge.native", "entry->preparationIssued", "core::makeShared<CreationDrainRetention>(self,ticket)"]:self.assertIn(token,sink)
        creation=sink.split("const char* bindDestinationCold(Pool::Ticket",1)[1].split("bool containsRequestId",1)[0]
        self.assertLess(creation.index("core::makeShared<CreationDrainRetention>"),creation.index("Destination::initialize(destination)"))
        for token in ["Request::validateOriginal", "source.validateReadbackContentProfile()", "Staging::validateOriginal", "maxRetainedNativeBytes-charged_.native", "destination->creationDrainConsumer_", "PREPARATION-ALLOCATION"]:self.assertIn(token,creation)
        destination=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_destination_h")
        callback=destination.split("static void completeScope",1)[1]
        for token in ["evidence_.allReceived()", "CreationDrainConsumer::consumeOnce", "callback->owner->creationDrainConsumer_"]:self.assertIn(token,callback)
        for token in ["WebGpuRecordedCommandProof", "signalFromHost", "g_readbackSamples"]:self.assertNotIn(token,creation+callback)

    def test_readback_creation_separates_cold_cpu_payload_and_hot_native_issue(self):
        sink=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_publication_h")
        for token in ["bindDestinationOriginalCold", "initializeBoundDestinationOriginal", "entry->destination && !entry->destination->creationCallbacksIssued()"]:self.assertIn(token,sink)
        hot=sink.split("const char* initializeBoundDestination(Pool::Ticket",1)[1].split("// Selected private compatibility helper",1)[0]
        for token in ["Destination::initialize(destination)", "creationDrainConsumer_", "preparationIssued=true", "creationCallbacksIssued()"]:self.assertIn(token,hot)
        for token in ["makeShared", "makeUnique", "CpuMemoryQuotaScope"]:self.assertNotIn(token,hot)
        destination=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_destination_h")
        hot=destination.split("static const char* initialize(",1)[1].split("Status status()",1)[0]
        self.assertIn("WebGpuNativeBufferCreation::initialize",hot)
        for token in ["makeShared", "makeUnique", "CpuMemoryQuotaScope", "createWebGpuNativeBufferPayload"]:self.assertNotIn(token,hot)
        resources=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        self.assertEqual(resources.count("lowerNativeBufferCreation("),3)
        hot=resources.split("const char* WebGpuNativeBufferCreation::initialize(",1)[1].split("bool writeWebGpuNativeBufferPayload(",1)[0]
        for token in ["hasQuotaBoundSharedOwner", "self->attempted_", "wgpuDeviceCreateBuffer", "if(!buffer)return", "self->ready_=true"]:self.assertIn(token,hot)
        for token in ["makeShared", "makeUnique", "CpuMemoryQuotaScope", "signalFromHost", "WebGpuRecordedCommandProof"]:self.assertNotIn(token,hot)

    def test_readback_snapshot_intent_keeps_completed_evidence_and_default_rejection(self):
        intent=self.read_source("src/rhi/frontend/transfer_readback_content_intent.h")
        for token in ["Unspecified,QueueOrderedSnapshot,CompletedOriginalUpload", "CONTENT-COMPLETED-EVIDENCE-REQUIRED", "CONTENT-OPT-IN", "validateDecision", "Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU", "nativeExecutionProven() const noexcept {return false;}"]:self.assertIn(token,intent)
        for token in ["wgpu", "signalFromHost", "WebGpuRecordedCommandProof", "makeShared", "makeUnique"]:self.assertNotIn(token,intent)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_source_h")
        for token in ["prepareLeasedSnapshotOriginal", "Content::validateDecision", "prepareLeasedAdmitted", "result->content_=content", "validateReadbackContentProfile", "if(completed_)return WebGpuCompletedTextureUploadDeclaration::validateOriginal(completed_)", "!leased_ || content_.mode!=Content::Mode::QueueOrderedSnapshot", "RHI-READBACK-PAIR-SOURCE-PROFILE", "TextureReadbackContentCoverage::validateOriginal", "ordinaryContentAccessDiagnostic()"]:self.assertIn(token,source)
        result=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_mapping_h")
        self.assertIn("contentMode() const noexcept {return destination_->source()->contentMode();}",result)

    def test_early_readback_pending_slots_attach_only_exact_original_native_map(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_publication_h")
        for token in ["prepareForDevicePlan", "aggregatePublicationCounts", "reserveRequestOriginal", "Plan::validateRequestOriginal", "containsRequestId", "NOT-ATTACHED", "rejectBeforeAttachment", "STATISTICS-MISMATCH", "ATTACH-QUOTA", "source=mapping->publicationReservation_.reserve()", "entry->charge.native=native", "PREPARATION-DEVICE-LOST", "if(entry.mapping)entry.mapping->notifyTerminalConsumer()"]:
            self.assertIn(token,source)
        admission=source.split("static std::optional<Reservation> reserveRequestOriginal",1)[1].split("static std::optional<Reservation> reserveTrackedOriginal",1)[0]
        for token in ["canCharge(intent)", "core::CpuMemoryQuotaScope", "core::makeUnique<Record>(nullptr,intent)", "records_.reserve(entry)", "request->originalMetadataQuota()", "statistics->originalMetadataQuota()"]:self.assertIn(token,admission)
        for token in ["createBuffer", "createWebGpu", "Mapping::prepareOriginal", "signalFromHost", "g_readbackSamples", "WebGpuRecordedCommandProof"]:self.assertNotIn(token,admission)
        attach=source.split("const char* attach(Pool::Ticket ticket",1)[1].split("bool canCharge",1)[0]
        for token in ["Request::validateOriginal", "Numerical::validateAttachedOriginal", "numerical->originalPlan()", "mapping->validate()", "sameOwner(registry_", "maxNativeBytesPerRequest", "maxContentBytesPerRequest"]:self.assertIn(token,attach)
        for token in ["makeUnique", "makeShared", "Mapping::start", "wgpu", "new Record", "g_readbackSamples"]:self.assertNotIn(token,attach)

    def test_device_readback_staging_retains_original_aggregate_allocation_lifetime(self):
        owner=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_device_staging_budget_h")
        for token in ["AtomicCapacityBudget", "AtomicCapacityLease", "maxDeviceStagingBytes", "sameOwner(self->registry_,source->registry())", "publicSourceLease()", "CpuDomain::Device", "CpuDomain::Request", "CpuDomain::Resource", "const std::shared_ptr<const Charge> charge_", "const std::shared_ptr<void> allocation_", "RHI-READBACK-DEVICE-STAGING-CAPACITY", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,owner)
        destination=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_destination_h")
        for token in ["prepareDeviceOriginal", "LOWERING-MISMATCH", "CpuDomain::Result", "reserveOriginal(deviceBudget,source,diagnostic)", "retainAllocationOriginal(deviceBudget,result->allocation_.releaseToken", "result->allocation_.releaseToken=std::move(lifetime)", "self->native_->allocationReleaseToken=self->allocation_.releaseToken"]:
            self.assertIn(token,destination)
        self.assertLess(destination.index("reserveOriginal(deviceBudget,source,diagnostic)"),destination.index("result->allocator_->allocate"))
        for token in ["wgpu", "signalFromHost", "WebGpuRecordedCommandProof", "g_readbackSamples", "make_shared"]:self.assertNotIn(token,owner)

    def test_public_readback_cold_budget_domains_and_lowering_keep_execution_closed(self):
        plan=self.read_source("src/rhi/frontend/transfer_readback_cold_budget.h")
        for token in ["CpuDomain", "Device,Request,Resource,Result,Count", "validateQuotas", "sameControlBlock", "limitBytes()!=limits[i]", "const TextureReadbackColdBudget budget_", "const Quotas quotas_", "maxDeviceStagingBytes", "maxCpuResultBytes", "validateRequestOriginal", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,plan)
        recipe=self.read_source("src/rhi/backends/webgpu/webgpu_texture_readback_cold_budget_lowering.h")
        for token in ["Plan::validateOriginal(plan)", "OUTPUT-BUSY", "Plan::aggregatePublicationCounts", "WebGpuOrdinaryRecording::callbackCount", "WebGpuOriginalTextureReadbackMapping::scopeCallbackCount", "maxCpuDeviceBytes", "maxCpuRequestBytes", "maxCpuResourceBytes", "maxCpuResultBytes", "recipe.originalPlan=plan", "PRIVATE-RECIPE-ONLY"]:
            self.assertIn(token,recipe)
        for token in ["wgpu", "signalFromHost", "g_readbackSamples", "WebGpuRecordedCommandProof", "make_shared", "createBuffer"]:
            self.assertNotIn(token,plan+recipe)

    def test_public_readback_source_lease_preserves_original_object_and_native_gate(self):
        source=self.read_source("src/rhi/frontend/transfer_readback_input.h", "frontend_transfer_readback_source_lease_h")
        for token in ["core::UniquePtr<GpuTexture>& source", "lease->source_.adoptOriginal", "retainedOwnerCount", "hasQuotaBoundSharedOwner", "SharedOwnerProjection::sameOwner", "source.id()==request.source()", "sourceLayout().matches(source.desc())", "const GpuTexture* sourceTexture()", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        for token in ["wgpu", "vk", "NativeSync", "signalFromHost", "estimatedSizeBytes", "make_shared"]:self.assertNotIn(token,source)
        binding=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_source_h")
        for token in ["prepareLeasedOriginal", "captureOrdinaryOriginal", "lease->sourceTexture()", "result->leased_=std::move(lease)", "RHI-READBACK-SOURCE-LEASE-LAYOUT-MISMATCH", "leased_->validate()"]:self.assertIn(token,binding)
        capture=binding.split("static const char* captureOrdinaryOriginal",1)[1].split("static const char* validateDeclaration",1)[0]
        for token in ["observeTransferResources", "validateDeclaration", "validateCurrentState", "maxSourceOwners", "maxRetainedAllocationBytes", "recordedState().observe()"]:self.assertIn(token,capture)
        for token in ["wgpuTextureCreate", "createWebGpuRhiTexture", "wgpuQueueSubmit", "signalFromHost", "WebGpuRecordedCommandProof"]:self.assertNotIn(token,binding)

    def test_readback_cold_source_adoption_is_final_and_fail_closed(self):
        source=self.read_source("src/rhi/frontend/transfer_readback_input.h", "frontend_transfer_readback_source_lease_h")
        cold=source.split("static std::shared_ptr<TextureReadbackSourceLease> prepareColdOriginal",1)[1].split("static const char* validateColdOriginal",1)[0]
        self.assertIn("lease->source_.bindCold(source)",cold)
        self.assertNotIn("std::move(source)",cold)
        validation=source.split("static const char* validateColdOriginal",1)[1].split("static const char* adoptOriginal",1)[0]
        for token in ["hasQuotaBoundSharedOwner", "self->source_", "self->source_.matchesCold(candidate)", "validateSourceOriginal"]:self.assertIn(token,validation)
        self.assertNotIn("*self->candidate_",source)
        adoption=source.split("static const char* adoptOriginal",1)[1].split("static const char* validateOriginal",1)[0]
        for token in ["validateColdOriginal(lease,*source)", "lease->source_.adoptOriginal", "validateSourceOriginal(lease->request_,candidate)"]:self.assertIn(token,adoption)
        for token in ["makeUnique", "makeShared", "CpuMemoryQuotaScope", "wgpu", "release()"]:self.assertNotIn(token,adoption)
        fixture=CppSource((self.ROOT / "tests/fixtures/texture_readback_source_lease_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyColdCpu(c)", "pressure.measure(factory)", "pressure.verify(peak,factory)", "Lease::adoptOriginal(forged", "c.texture.reset()", "committedAllocations"]:self.assertIn(token,fixture)

    def test_atomic_readback_creation_composes_cold_receipts_before_adoption(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_owned_texture_readback_preparation.h")
        factory=source.split("static std::shared_ptr<WebGpuOwnedTextureReadbackPreparation> prepareOriginal",1)[1].split("const std::shared_ptr<const rhi::TextureReadbackRequestDeclaration>& originalRequest",1)[0]
        for token in ["Contract::validateOriginal", "Staging::validateOriginal", "completion->retained.accepting()", "completion->originalReadbackPlan()", "completion->nativeDevice()", "Publication::reserveRequestOriginal", "Lease::prepareColdOriginal", "Source::prepareColdLeasedSnapshotOriginal", "Destination::prepareDeviceColdOriginal", "shareOriginal(core::makeUnique", "bindColdCandidateDestinationOriginal", "source->validateColdCandidateOriginal", "Lease::adoptOriginal"]:self.assertIn(token,factory)
        self.assertLess(factory.index("shareOriginal(core::makeUnique"),factory.index("bindColdCandidateDestinationOriginal"))
        self.assertLess(factory.index("bindColdCandidateDestinationOriginal"),factory.index("Lease::adoptOriginal"))
        for token in ["makeUnique", "makeShared", "reserve", "wgpu"]:self.assertNotIn(token,factory.split("Lease::adoptOriginal",1)[1])
        for token in ["wgpuDeviceCreate", "wgpuQueueSubmit", "WebGpuRecordedCommandProof", "new ", "std::vector", "std::string "]:self.assertNotIn(token,source)
        for token in ["initializeBoundDestinationOriginal", "Contract::validateAction", "reservation_.cancel()", "core::pmr::String passLabel_", "defaultCpuMemoryResource"]:self.assertIn(token,source)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_owned_readback_context_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyColdAdmission(e,device", "arpAtomicReadbackCreateCalls", "Destination::initialize(destination)", "quota3-exact-less-exhausted", "q[domain]->limitBytes()-baseline[domain]-peak[domain]+1u", "Phase::CreationReady", "base-capability-closed"]:self.assertIn(token,fixture)

    def test_original_readback_atomic_execution_binding_restores_unissued_state(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_publication_h")
        binding=source.split("const char* bindRecordingAndMappingCold(Pool::Ticket",1)[1].split("const char* record(Pool::Ticket",1)[0]
        for token in ["bindRecording(ticket", "attach(ticket", "entry->recording->readbackDrainConsumer_.reset()", "entry->recording.reset()"]:self.assertIn(token,binding)
        for token in ["wgpu", "charged_", "makeShared", "makeUnique", "retire(ticket)", "entry->mapping.reset()", "entry->issued="]:self.assertNotIn(token,binding)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_texture_readback_snapshot_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["bindRecordingAndMappingOriginalCold(recording,forgedMap", "bindRecordingAndMappingOriginalCold(recording,mapping,forgedNumerical)", "bindRecordingAndMappingOriginalCold(recording,{}", "q[1]->liveBytes()==bindingBaseline", "EXECUTION-NOT-BOUND", "ATOMIC_EXECUTION_BINDING_NATIVE_OK"]:self.assertIn(token,fixture)

    def test_owned_readback_advancement_consumes_original_receipts_without_new_proof(self):
        cold=self.read_source("src/rhi/backends/webgpu/webgpu_owned_texture_readback_recording_preparation.h")
        for token in ["lowerOriginal(plan,recipe)", "completion->originalReadbackPlan()", "statistics->originalMetadataQuota()", "GraphPreparation::prepareOriginal", "Recording::prepareOriginalTextureReadback", "Mapping::prepareOriginal", "Numerical::prepareOriginal", "Numerical::attachOriginal", "core::CpuMemoryQuotaScope scope(quota)", "shareOriginal(core::makeUnique", "const std::shared_ptr<const Plan> plan_"]:self.assertIn(token,cold)
        for token in ["wgpu", "WebGpuRecordedCommandProof", "signalFromHost", "std::vector", "std::make_shared"]:self.assertNotIn(token,cold)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_owned_texture_readback_preparation.h")
        advance=source.split("const char* prepareNextCold()",1)[1].split("const char* record()",1)[0]
        self.assertLess(advance.index("bindRecordingAndMappingOriginalCold"),advance.index("coldRecording_=std::move(cold)"))
        self.assertLess(advance.index("bindSubmissionOriginal"),advance.index("submission_=std::move(submission)"))
        hot=source.split("const char* record()",1)[1].split("private:",1)[0]
        for token in ["Contract::validateAction", "reservation_.recordOriginal()", "reservation_.submitOriginal()", "reservation_.startOwnedOriginal()"]:self.assertIn(token,hot)
        for token in ["makeShared", "makeUnique", "CpuMemoryQuotaScope", "wgpu", "signalFromHost"]:self.assertNotIn(token,hot)
        for token in ["callbacksOutstanding() || quarantinedSubmission()", "readbackRecordingCallbacksIssued()", "readbackRecordingCallbacksDrained()", "mapping()->callbacksIssued()", "mapping()->callbacksDrained()", "submission_->callbacksDrained()", "!completion_->originalLossDelivered()"]:self.assertIn(token,source)
        mapping=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_mapping_h")
        for token in ["callbacksIssued_=true", "callbacksIssued_ && evidence_.allReceived()"]:self.assertIn(token,mapping)
        publication=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_publication_h")
        owned=publication.split("const char* startOwned(Pool::Ticket",1)[1].split("const WebGpuOriginalTextureReadbackPublicationBudget",1)[0]
        for token in ["start(ticket)", "!entry->mapping->callbacksIssued()", "entry->submission->callbacksDrained()", "Submission::Status::Completed", "PreparationFailure::MappingPreparation"]:self.assertIn(token,owned)
        legacy=publication.split("const char* start(Pool::Ticket",1)[1].split("const char* startOwned(Pool::Ticket",1)[0]
        self.assertNotIn("entry->preparationFailure=",legacy)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_owned_readback_advancement_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyHotNoAllocation", "pressure.verify(peak,factory)", "subPressure.verify(subPeak,subFactory)", "quarantineDestination_.expired()", "detachOriginalDeviceLoss", "originalLossDelivered()", "write(pixels)", "bytes+256u", "verifyCancelDrain", "verifyErrors", "verifyLoss"]:self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["ownedReadbackAdvancementPassed", "ownedReadbackAdvancementCancel.length===3", "ownedReadbackAdvancementError.length===3", "ownedReadbackAdvancementLoss.length===4"]:self.assertIn(token,runner)

    def test_owned_cpu_snapshot_seals_original_output_and_retains_no_native_owner(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_owned_texture_readback_snapshot_h")
        for token in ["public rhi::TextureReadbackOwnedSnapshot", "SnapshotContract::validateColdOriginal", "Plan::CpuDomain::Request", "Plan::CpuDomain::Result", "analyses_.resize", "bytes_.resize", "core::pmr::Vector", "friend class WebGpuOriginalTextureReadbackPublication", "Numerical::Outcome::Completed", "numerical->originalPlan()", "numerical->originalResult()", "result->layout()", "result->contentMode()!=mode_", "published_=true"]:self.assertIn(token,source)
        fields=source.split("const std::shared_ptr<const Request> request_",1)[1]
        for token in ["Destination", "Mapping", "Result>", "Numerical>", "WGPU", "registry", "Plan>"]:self.assertNotIn(token,fields)
        hot=source.split("const char* publishOriginal(",1)[1].split("const std::shared_ptr<const Request> request_",1)[0]
        for token in ["makeShared", "makeUnique", "CpuMemoryQuotaScope", "resize", "wgpu", "signalFromHost"]:self.assertNotIn(token,hot)
        publication=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_publication_h")
        for token in ["bindOwnedSnapshotOriginalCold", "Snapshot::validateColdBindingOriginal", "snapshot->originalMetadataQuota()", "entry->ownedSnapshot->publishOriginal", "!entry->canceled", "delivered.ownedSnapshot=entry->ownedSnapshot", "output.ownedSnapshot"]:self.assertIn(token,publication)
        owned=self.read_source("src/rhi/backends/webgpu/webgpu_owned_texture_readback_preparation.h")
        self.assertLess(owned.index("Snapshot::prepareOriginal") if "Snapshot::prepareOriginal" in owned else owned.index("WebGpuOwnedTextureReadbackSnapshot::prepareOriginal"),owned.index("Lease::adoptOriginal"))
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_owned_readback_advancement_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyDetachedSnapshot", "native.expired()", "c.quotas[3]->liveBytes()==512u", "snapshot.reset()", "OWNED_SNAPSHOT_NATIVE_OK"]:self.assertIn(token,fixture)

    def test_owned_public_shutdown_uses_original_bounded_cancellation_and_gate_facts(self):
        publication=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_publication_h")
        shutdown=publication.split("static const char* cancelAllOriginal",1)[1].split("bool acceptingOriginal",1)[0]
        for token in ["self->shutdownRequested_=true", "self->pending()", "self->records_.containsMatching", "if(entry.canceled)", "self->cancel(selected)", "pollOriginal(self,suppressed,diagnostic)"]:self.assertIn(token,shutdown)
        for token in ["makeShared", "makeUnique", "CpuMemoryQuotaScope", "wgpu", "signalFromHost"]:self.assertNotIn(token,shutdown)
        self.assertEqual(publication.count('if(!self->acceptingOriginal()){diagnostic="RHI-READBACK-PUBLICATION-CLOSED"'),2)
        terminal=publication.split("static bool terminal(const Record&",1)[1].split("static bool executionOutstanding",1)[0]
        self.assertLess(terminal.index("!entry.mapping->callbacksDrained()"),terminal.index("Mapping::Status::DeviceLost"))
        context=self.read_source("src/rhi/backends/webgpu/webgpu_owned_texture_readback_context.h")
        for token in ["resolveLowering", "nativeDevice()!=nullptr", "originalLossArmed()", "acceptingOriginal()", "originalLossDelivered() || webGpuNativeDeviceLost()", "callbacksOutstandingOriginal()", "retained.failed()", "shutdownDisposition(facts)", "output=std::move(delivered)", "output.diagnostic", "output.snapshot"]:self.assertIn(token,context)
        self.assertLess(context.index("Publication::cancelAllOriginal"),context.index("detachOriginalDeviceLoss"))
        completion=self.read_source("src/rhi/backends/webgpu/webgpu_ordinary_completion_retention.h")
        armed=completion.split("bool originalLossArmed()",1)[1].split("bool originalLossDelivered()",1)[0]
        for token in ["nativeState", "registrationCapacity.charged()", "callbackCapacity.charged()", "ExternalCallbackGate::Phase::Armed"]:self.assertIn(token,armed)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_owned_readback_advancement_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyPublicDelivery", "verifyPublicShutdown", "c.extension->prepareOwnedOriginal", "c.extension->pollOriginal", "OWNED_PUBLIC_LOSS_OK", "OWNED_PUBLIC_SHUTDOWN_OK", "OWNED_PUBLIC_DELIVERY_OK"]:self.assertIn(token,fixture)

    def test_owned_pending_device_destructor_preserves_original_callback_owners(self):
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_owned_readback_advancement_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyPendingDeviceDestruction", "verifyDestructorLoss", "c.device.reset()", "c.extension=nullptr", "Phase::CanceledDraining", "native.expired()", "originalLossArmed()", "originalLossDelivered()", "OWNED_PUBLIC_DESTRUCTOR_PENDING_OK", "OWNED_PUBLIC_DESTRUCTOR_LOSS_OK"]:self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["ownedReadbackPendingDestructorPassed", "ownedReadbackDestructorPending.length===4", "ownedReadbackDestructorLoss.length===4"]:self.assertIn(token,runner)
        context=self.read_source("src/rhi/backends/webgpu/webgpu_owned_texture_readback_context.h")
        self.assertIn("~WebGpuOwnedTextureReadbackExtension() override {(void)shutdownOriginal();}",context)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_owned_legacy_numerical_equivalence_does_not_open_legacy_native_schedule(self):
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_original_texture_readback_statistics_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyLegacySnapshot", "SnapshotContract::validatePublishedOriginal", "applyTextureReadbackStatistics(legacy,format", "equal(values.scalar,legacy)", "legacy.channelStatisticsVersion==1u", "equal(values.channels.channels[channel]"]:self.assertIn(token,fixture)
        commands=self.read_source("src/rhi/backends/webgpu/webgpu_native_commands.cpp")
        legacy=commands.split("bool WebGpuNativeCommandPayload::scheduleTextureReadback(",1)[1].split("std::shared_ptr<WebGpuNativeCommandPayload> createWebGpuNativeOrdinaryTransferCommandPayload",1)[0]
        self.assertLess(legacy.index("restrictedRecordingDiagnostic()"),legacy.index("BorrowedTextureReadbackRetirementContract::lowering"))
        self.assertNotIn("wgpuDeviceCreateBuffer",legacy)
        owned=self.read_source("src/rhi/backends/webgpu/webgpu_owned_texture_readback_context.h")
        for token in ["mapWebGpuNativeTextureReadbackBuffer", "pollWebGpuNativeTextureReadback", "g_readbackSamples", "applyTextureReadbackStatistics", "scheduleTextureReadback"]:self.assertNotIn(token,owned)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("ownedReadbackLegacyNumericalPassed",runner)

    def test_borrowed_retirement_has_four_backend_decisions_and_no_global_webgpu_queue(self):
        contract=self.read_source("src/rhi/frontend/transfer_readback_borrowed_retirement.h")
        for token in ["case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU", "ExplicitBoundedLegacy", "originalBoundedBudgetConfigured", "RetiredTextureReadbackExtension", "return false;", "RHI-READBACK-BORROWED-VULKAN-RETIRED", "RHI-READBACK-BORROWED-WEBGPU-RETIRED"]:self.assertIn(token,contract)
        for backend,enum in [("webgpu","WebGPU"),("vulkan","Vulkan")]:
            provider=CppSource((self.ROOT / f"src/rhi/backends/{backend}/{backend}_device.cpp").read_text(encoding="utf-8-sig"))
            self.assertIn(f"RetiredTextureReadbackExtension<rhi::Backend::{enum}>",provider)
        webgpu="\n".join(CppSource((self.ROOT / f"src/rhi/backends/webgpu/{name}").read_text(encoding="utf-8-sig")) for name in ["webgpu_native_runtime.cpp","webgpu_native_runtime.h","webgpu_native_command_payload_impl.h","webgpu_native_commands.cpp"])
        for token in ["g_readbackMutex","g_readbackSamples","ProductionReadbackState","publishProductionReadback","pollWebGpuNativeTextureReadback","mapWebGpuNativeTextureReadbackBuffer","PendingTextureReadback","pendingTextureReadbacks","WebGpuNativeTextureReadbackSample"]:self.assertNotIn(token,webgpu)
        for backend,files in [("webgpu",["webgpu_backend_commands.cpp","webgpu_native_commands.cpp"]),("vulkan",["vulkan_command_objects.cpp","vulkan_native_commands.cpp"])]:
            for name in files:
                text=CppSource((self.ROOT / f"src/rhi/backends/{backend}/{name}").read_text(encoding="utf-8-sig"))
                body=text.split("scheduleTextureReadback(",1)[1].split("\n}",1)[0]
                self.assertIn("BorrowedTextureReadbackRetirementContract::lowering",body)
                for token in ["wgpuDeviceCreateBuffer","vkCreateBuffer","copyTextureToBuffer","CopyImageToBuffer","push_back"]:self.assertNotIn(token,body)
        metal=self.read_source("src/rhi/backends/metal/metal_texture_readback.cpp")
        for token in ["BorrowedTextureReadbackRetirementContract::lowering", "budget_.maxBytes!=0u", "budget_.maxPending!=0u", "borrowedDecision.executionAvailable"]:self.assertIn(token,metal)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])
        fixture=CppSource((self.ROOT / "tests/fixtures/borrowed_texture_readback_retirement_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["queryInterface(rhi::kExtensionTextureReadback)","recordedCommandCount==count","!extension.poll(output)","!extension.supportsTrackedRequests()"]:self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& borrowedReadbackRetirementPassed",runner)

    def test_vulkan_borrowed_global_queue_retirement_preserves_other_readbacks(self):
        source=self.read_source("src/rhi/backends/vulkan/vulkan_native_commands.cpp")
        header=self.read_source("src/rhi/backends/vulkan/vulkan_native_commands.h")
        for token in ["VulkanNativeTextureReadbackSample", "PendingVulkanTextureReadback", "g_textureReadbackMutex", "g_textureReadbacks", "pollVulkanNativeTextureReadback", "applyTextureReadbackStatistics"]:self.assertNotIn(token,source+header)
        for token in ["scheduleBindlessFaultReadback", "resolveBindlessFaultReadbacks", "completedGpuTiming()", "recordImageToBufferCopy", "recordImageToImageCopy"]:self.assertIn(token,source)
        self.assertIn("BorrowedTextureReadbackRetirementContract::lowering(rhi::Backend::Vulkan)",source)

    def test_owned_execution_consumes_the_same_resolved_l2_decision_as_availability(self):
        contract=self.read_source("src/rhi/frontend/transfer_readback_owned_preparation.h")
        for token in ["static constexpr Decision resolveLowering", "if(backend!=Backend::WebGPU)return result", "result.diagnostic=validate(facts)", "decltype(result.disposition)::PrototypeContextReady", "return validateExecutionContext(value)", "virtual TextureReadbackOwnedPreparationContract::Decision executionDecision()", "return executionDecision().executionAvailable"]:self.assertIn(token,contract)
        context=self.read_source("src/rhi/backends/webgpu/webgpu_owned_texture_readback_context.h")
        for token in ["executionDecision() const noexcept override", "resolveLowering(rhi::Backend::WebGPU,{});", "const auto decision=executionDecision();diagnostic=decision.diagnostic", "if(!decision.executionAvailable)return {}"]:self.assertIn(token,context)
        self.assertNotIn("bool executionAvailable() const noexcept override",context)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_owned_readback_context_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyDecision(e,true)", "verifyDecision(e,false", "OWNED_RESOLVED_LOWERING_NATIVE_OK", "OWNED_RESOLVED_LOWERING_LOSS_OK"]:self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& ownedReadbackResolvedLoweringPassed",runner)

    def test_owned_public_concurrent_requests_do_not_share_identity_or_callback_retirement(self):
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_owned_readback_advancement_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["authorRequest", "verifyPublicConcurrent", "verifyPublicConcurrentCancel", "publicPairBudget", "second.request->label()==c.request->label()", "second.request->identity().requestId!=c.request->identity().requestId", "RHI-READBACK-PUBLICATION-REQUEST-DUPLICATE", "RHI-READBACK-PUBLICATION-CAPACITY", "RHI-READBACK-DEVICE-STAGING-CAPACITY", "sameOwner(secondOutput.originalRequest,second.request)", "sameOwner(firstOutput.originalRequest,c.request)", "Scopes::releaseScope(0u)", "canceled.expired()", "OWNED_PUBLIC_CONCURRENT_CANCEL_OK", "OWNED_PUBLIC_CONCURRENT_OK"]:self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& ownedReadbackPublicConcurrentPassed",runner)
        budget=CppSource((self.ROOT / "tests/fixtures/texture_readback_cold_budget_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("pair.publication.maxPendingResults==2u",budget)

    def test_owned_public_device_isolation_uses_exact_registry_and_quota_namespace(self):
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_owned_readback_advancement_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyPublicDeviceIsolation", "peer.recipe", "RHI-WEBGPU-TRANSFER-RESOURCE-MISSING", "RHI-READBACK-OWNED-REQUEST-QUOTA", "sameOwner(first.budgetContext().registry,peer.budgetContext().registry)", "sameOwner(output.originalRequest,c.request)", "poll(peer,peerOutput,peerPixels)", "OWNED_PUBLIC_DEVICE_ISOLATION_OK"]:self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& ownedReadbackPublicDeviceIsolationPassed",runner)

    def test_original_object_cold_adoption_reuses_core_slot_without_identity_inference(self):
        source=CppSource((self.ROOT / "src/core/cold_owned_object_slot.h").read_text(encoding="utf-8-sig"))
        for token in ["class ColdOwnedObjectSlot final", "UniquePtr<T> object_", "const T* candidate_", "std::is_nothrow_invocable_r_v<bool", "!matchesCold(*source) || !validate(*source)", "object_=std::move(source);candidate_=nullptr"]:self.assertIn(token,source)
        for token in ["makeUnique", "makeShared", "new ", "delete ", "signalFromHost", "Gpu", "RHI", "std::vector"]:self.assertNotIn(token,source)
        lease=self.read_source("src/rhi/frontend/transfer_readback_input.h", "frontend_transfer_readback_source_lease_h")
        self.assertIn("core::ColdOwnedObjectSlot<GpuTexture> source_",lease)
        self.assertNotIn("const GpuTexture* candidate_",lease)
        fixture=CppSource((self.ROOT / "tests/unit/bounded_retention_tests.cpp").read_text(encoding="utf-8-sig"))
        for token in ["testColdOwnedObjectSlot", "source->identity=41u", "source.get()==exact", "exhausted.charged()", "abandoned.reset()", "destroyed==3u", "CORE_COLD_OWNED_OBJECT_SLOT_OK"]:self.assertIn(token,fixture)

    def test_original_readback_request_and_registry_scope_cannot_infer_identity(self):
        request=self.read_source("src/rhi/frontend/transfer_readback_input.h", "frontend_transfer_readback_request_declaration_h")
        for token in ["identity.requestId", "identity.frameSerial", "identity.resourceGeneration", "hasQuotaBoundSharedOwner", "SharedOwnerProjection::sameOwner", "const TextureReadbackIdentity identity_", "scalarFields().sourceResource()", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,request)
        for token in ["allocateTextureReadbackRequestId", "wgpu", "WebGpuRecordedCommandProof", "completionEpoch"]:self.assertNotIn(token,request)
        sink=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_publication_h")
        for token in ["prepareForOriginalRegistry", "sharedOwnerMatches(registry)", "self->registry_,pair->destination()->source()->registry()", "Request::validateOriginal(request,mapping->resultDeclaration()->layout())", "Numerical::validateAttachedOriginal(numerical,mapping)", "RHI-READBACK-PUBLICATION-REQUEST-DUPLICATE", "delivered.originalRequest=entry->request", "delivered.numericalResult=entry->numerical"]:self.assertIn(token,sink)
        self.assertNotIn("allocateTextureReadbackRequestId",sink)

    def test_original_readback_terminal_numerics_are_cold_and_cycle_free(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_numerical_result_h")
        for token in ["maxViews", "maxAnalyzedPixels", "maxRetainedResultBytes", "maxCpuMetadataBytes", "values_.resize(plan_->views().size())", "mapping->terminalConsumer_", "SharedOwnerProjection::sameOwner(self->source_,mapping->resultDeclaration())", "Mapping::Status::Prepared", "NativeStatus::Unrecorded", "hasQuotaBoundSharedOwner", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        terminal=source.split("void consumeOriginalTerminal()",1)[1]
        for token in ["makeUnique", "makeShared", "resize", "push_back", "wgpu", "WebGpuRecordedCommandProof", "g_readbackSamples"]:self.assertNotIn(token,terminal)
        self.assertNotIn("std::shared_ptr<Mapping>",source.split("const std::shared_ptr<const Plan> plan_",1)[1])
        mapping=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_mapping_h")
        for token in ["if(!evidence_.allReceived())return;", "auto consumer=std::move(terminalConsumer_)", "consumer->consumeOriginalTerminal()", "consumer->consumeOriginalLoss()", "callback->owner->notifyTerminalConsumer()"]:self.assertIn(token,mapping)

    def test_original_readback_statistics_require_exact_published_layout(self):
        plan=self.read_source("src/rhi/frontend/transfer_readback_input.h", "frontend_transfer_readback_statistics_plan_h")
        for token in ["maxViews", "maxAnalyzedPixels", "maxOriginalReadbackBytes", "maxCpuMetadataBytes", "hasQuotaBoundSharedOwner", "SharedOwnerProjection::sameOwner", "StridedIndexRange", "containsUnsignedRange", "samples>UINT32_MAX", "textureReadbackChannelCount", "SliceDomain::DepthSlice", "nativePublicationProven() const noexcept {return false;}"]:
            self.assertIn(token,plan)
        consumer=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_statistics_h")
        for token in ["validatePublishedOriginal(result)", "validateOriginal(plan,result->layout())", "originalByteCapacity()!=result->byteCapacity()", "analyzeTextureReadback(", "analyzeTextureReadbackChannels(", "RHI-READBACK-STATISTICS-NONFINITE-OR-INVALID", "Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU"]:
            self.assertIn(token,consumer)
        for token in ["makeUnique", "makeShared", "pmr::Vector", "WebGpuRecordedCommandProof", "wgpu", "sampleResultQueue", "trackedRequestId"]:
            self.assertNotIn(token,consumer)

    def test_original_query_source_lowering_cannot_promote_api_only_support(self):
        source=self.read_source("src/rhi/semantics/original_query_source_lowering.h")
        for token in ["nativeSourceSupported() const noexcept {return false;}", "version!=1u", "!prototypeOptIn", "input->validateDeclaration()", "WebGpuTimestampCommandPointGate::decide(facts)", "CapabilityMode::DiagnosticFailure", "CapabilityMode::Degraded", "RHI-WEBGPU-ORIGINAL-QUERY-PAYLOAD-UNIMPLEMENTED", "RHI-WEBGPU-ORIGINAL-QUERY-STATISTICS-UNSUPPORTED", "matchesOriginal", "Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU"]:self.assertIn(token,source)
        self.assertNotIn("CapabilityMode::Native",source)
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")
        gate=self.read_source("src/rhi/backends/webgpu/webgpu_original_query_source_gate.h")
        for token in ["webGpuNativeTimestampCommandPointFacts()", "webGpuNativeDeviceLost()", "RHI-WEBGPU-ORIGINAL-QUERY-DEVICE-LOST"]:self.assertIn(token,gate)
        self.assertNotIn("createQuery",gate)

    def test_original_query_creation_owner_does_not_open_execution(self):
        source=self.read_source("src/rhi/frontend/query_creation.h")
        for token in ["maximumWebGpuQueryCount=4096u", "executionSupported() const noexcept {return false;}", "hasQuotaBoundSharedOwner", "Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU"]:self.assertIn(token,source)
        owner=self.read_source("src/rhi/backends/webgpu/webgpu_original_query_owner.h")
        for token in ["core::ExternalObjectSlot device_,query_", "maxCpuOwnerBytes", "maxCpuCallbackBytes", "WebGpuNativeErrorScopeBatch::push", "WebGpuNativeErrorScopeBatch::pop", "wgpuQuerySetGetCount", "wgpuQuerySetGetType", "if(!evidence_.allReceived())return", "QueryExecutionState::NotRecorded", "resultAvailable() const noexcept {return false;}", "WebGPU.getJsObject(Number($0))===Module.preinitializedWebGPUDevice"]:self.assertIn(token,owner)
        for token in ["createWebGpuRhiQueryPool", "std::make_shared", "WebGpuRecordedCommandProof", "QueryExecutionState::Available", "wgpuCommandEncoder", "wgpuQueueSubmit", "wgpuQueueWrite"]:self.assertNotIn(token,owner)
        initialize=owner.split("static const char* initialize",1)[1].split("Status status()",1)[0]
        self.assertNotIn("makeUnique",initialize)
        self.assertNotIn("makeShared",initialize)

    def test_original_query_binding_retains_exact_owner_without_execution(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_query_binding.h")
        for token in ["const rhi::QueryPoolOriginalInputArguments input_", "const std::shared_ptr<const WebGpuOriginalQueryOwner> owner_", "decision().matchesOriginal(input->originalInput())", "SharedOwnerProjection::sameOwner(owner,owner_)", "WebGpuOriginalQueryOwner::validateReadyOriginal", "WebGpuOriginalQuerySourceGate::admissionDiagnostic", "recordingSupported() const noexcept {return false;}", "maxNativeOwners", "maxQuerySlots", "maxScopeIds", "maxLabelBytes"]:self.assertIn(token,source)
        for token in ["makeUnique", "makeShared", "pmr::Vector", "WebGpuRecordedCommandProof", "wgpu", "QueryExecutionState::Available"]:self.assertNotIn(token,source)

    def test_query_slot_declarations_are_not_native_evidence(self):
        source=self.read_source("src/rhi/frontend/query_slot_declaration_control.h")
        for token in ["core::VersionedMutationValue<Phase>", "Quarantined", "RetireFailure", "SnapshotDeclared", "value_.matchesPending", "Event::Reset && pending()", "nativeExecutionProven() const noexcept {return false;}", "nativeAvailabilityProven() const noexcept {return false;}", "RHI-QUERY-SLOT-NATIVE-LIFECYCLE-UNIMPLEMENTED"]:self.assertIn(token,source)
        for token in ["QueryExecutionState::Available", "wgpu", "std::make_shared", "std::vector", "WebGpuRecordedCommandProof"]:self.assertNotIn(token,source)

    def test_query_command_points_preserve_exact_whole_stream_without_native_proof(self):
        source=self.read_source("src/rhi/frontend/query_command_points.h")
        for token in ["semantics::CommandRecordingOrder", "order.append(kind)", "order.complete()", "input[query].commandOrdinal!=i", "original.scalar().matches(command.scalar)", "SharedOwnerProjection::sameOwner(self->stream_,stream)", "maxReferencedSlots", "maxReferencedScopeIds", "maxReferencedLabelBytes", "maxStreamTextBytes", "maxStreamOffsets", "maxStreamPushBytes", "pipelineBindOrdinal", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        for token in ["WebGpuRecordedCommandProof", "wgpu", "std::vector", "std::make_shared", "QueryExecutionState::Available"]:
            self.assertNotIn(token,source)

    def test_original_query_record_reservation_has_cpu_journal_only(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_query_record_reservation.h")
        for token in ["points->hasQuotaBoundSharedOwner", "count!=points->points().size()", "originalCommand().matches(points->points()[i].original)", "SharedOwnerProjection::sameOwner(graph,observations[j].originalGraph())", "Slots::validateCurrentOriginal", "control.declare(Control::Event::Record,&self->journal_)", "control.observePending(self->journal_)", "matchesPending(*entry.pending,journal_)", "journal_.rollback()", "~WebGpuOriginalQueryRecordReservation(){abandon();}", "maxReferencedQuerySlots", "maxReferencedScopeIds", "maxReferencedLabelBytes", "nativeExecutionProven() const noexcept {return false;}", "nativeAvailabilityProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        for token in ["wgpu", "WebGpuRecordedCommandProof", "QueryExecutionState::Available", "journal_.commit", "Event::Submit", "Event::Complete", "std::make_shared", "std::vector"]:
            self.assertNotIn(token,source)

    def test_original_readback_publication_reserves_before_native_map(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_publication_h")
        for token in ["core::runtime::CompletionRetentionPool<Record>", "Mapping::Status::Prepared", "NativeStatus::Unrecorded", "mapping->hasQuotaBoundSharedOwner", "publicationReservation_.canReserve()", "publicationReservation_.reserve()", "records_.reserve(entry)", "records_.submitWith(ticket", "Mapping::startAdmitted(original.mapping,original.source)", "entry.mapping->mapCompletionObserved() && entry.mapping->scopeCompletionsObserved()", "Result::validatePublishedOriginal", "RHI-READBACK-PUBLICATION-OUTPUT-BUSY", "maxCpuSinkBytes", "maxCpuRecordBytes"]:
            self.assertIn(token,source)
        for token in ["WebGpuRecordedCommandProof", "wgpu", "std::vector", "std::make_shared", "signalFromHost", "g_readbackSamples", "applyTextureReadbackStatistics"]:
            self.assertNotIn(token,source)
        mapping=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_mapping_h")
        self.assertIn("friend class WebGpuOriginalTextureReadbackPublication",mapping)
        self.assertIn("publicationReservation_.matches(publicationTicket)",mapping)
        self.assertIn("RHI-WEBGPU-READBACK-MAP-PUBLICATION-OWNER-REQUIRED",mapping)

    def test_owned_readback_public_contract_has_no_backend_payload_or_implicit_execution(self):
        source=self.read_source("src/rhi/frontend/transfer_readback_owned_preparation.h")
        for token in ["arp.rhi.TextureReadbackOwnedPreparation/1", "PrototypeRequiresDeviceContext", "bool executionAvailable=false", "RHI-READBACK-OWNED-VULKAN-UNIMPLEMENTED", "RHI-READBACK-OWNED-METAL-UNIMPLEMENTED", "RHI-READBACK-OWNED-NULL-VALIDATOR-ONLY", "RHI-READBACK-OWNED-DEVICE-CONTEXT-UNIMPLEMENTED", "TextureReadbackColdBudgetPlan::validateRequestOriginal", "TextureReadbackSourceLease::validateSourceOriginal", "Plan::CpuDomain::Request", "validateTransitionIntent", "prepareNextCold", "core::UniquePtr<GpuTexture>&", "const std::uint8_t* publishedBytes()", "const TextureReadbackViewAnalysis* publishedAnalyses()", "facts.outstandingCallbacks", "facts.quarantinedSubmissions && !facts.actualDeviceLossObserved"]:
            self.assertIn(token.replace("Plan::CpuDomain::Request","TextureReadbackColdBudgetPlan::CpuDomain::Request"),source)
        for forbidden in ["backends/", "wgpu", "WGPU", "WebGpu", "NativeSync", "std::vector", "std::string ", "std::make_shared", "makeUnique", "makeShared", "signalFromHost", "WebGpuRecordedCommandProof"]:self.assertNotIn(forbidden,source)
        self.assertLess(source.index("if(facts.outstandingCallbacks)"),source.index("if(facts.quarantinedSubmissions"))
        lowering=self.read_source("src/rhi/semantics/transfer_dependency_lowering.h")
        self.assertIn("validateTransitionIntent(desc,current,texture,wholeRange)",lowering)
        publicValidation=lowering.split("static const char* validateTransitionIntent(")[1].split("static Decision memory(")[0]
        self.assertNotIn("Backend::",publicValidation)
        self.assertNotIn("equivalent()",publicValidation)

    def test_original_loss_registration_is_budgeted_and_native_userdata_is_suppressed_first(self):
        source=self.read_source("src/rhi/frontend/resource_device_loss_registration.h")
        for token in ["contractVersion!=1u", "!b.prototypeOptIn", "!b.maxRegistrations || !b.maxCallbacks", "maxCpuMetadataBytes!=deviceQuotaBytes", "RHI-DEVICE-LOSS-REGISTRATION-VULKAN-UNIMPLEMENTED", "RHI-DEVICE-LOSS-REGISTRATION-METAL-UNIMPLEMENTED", "RHI-DEVICE-LOSS-REGISTRATION-VALIDATOR-ONLY", "RHI-DEVICE-LOSS-REGISTRATION-NATIVE-PROTOTYPE", "if(outstandingGraphs)", "quarantined && !actualLoss"]:self.assertIn(token,source)
        for token in ["backends/", "WGPU", "wgpu", "makeShared", "makeUnique", "WebGpuRecordedCommandProof", "signalFromHost"]:self.assertNotIn(token,source)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.cpp")
        registration=native.split("std::uintptr_t prepareWebGpuNativeDeviceLossRegistration(")[1].split("bool observeWebGpuNativeDeviceLoss(")[0]
        self.assertLess(registration.index("state.phase=2;state.userdata=0;state.callback=0"),registration.index("getWasmTableEntry(callback)(context)"))
        self.assertIn("state.phase=3;state.userdata=0;state.callback=0",registration)
        self.assertIn("emscripten::val::take_ownership",registration)
        for token in ["std::vector", "std::make_shared", "Module.arpOriginalLossStates", "new Map", "signalFromHost"]:self.assertNotIn(token,registration)
        commands=self.read_source("src/rhi/backends/webgpu/webgpu_native_commands.cpp")
        detach=commands.split("const char* WebGpuOrdinaryCompletionOwner::detachOriginalDeviceLoss(")[1].split("std::shared_ptr<WebGpuOrdinaryCompletionOwner> prepareWebGpuOriginalReadbackCompletionOwner(")[0]
        for token in ["sharedOwnerMatches", "validateDetachment", "owner->retained.pending()", "owner->retained.failed()", "detachWebGpuNativeDeviceLossRegistration", "registration.owner.reset()"]:self.assertIn(token,detach)
        self.assertLess(detach.index("detachWebGpuNativeDeviceLossRegistration"),detach.index("registration.owner.reset()"))
        for token in ["makeShared", "makeUnique", "CpuMemoryQuotaScope", "signalFromHost"]:self.assertNotIn(token,detach)
        factory=commands.split("std::shared_ptr<WebGpuOrdinaryCompletionOwner> prepareWebGpuOriginalReadbackCompletionOwner(")[1].split("std::shared_ptr<WebGpuOrdinaryCompletionOwner> prepareWebGpuOrdinaryCompletionOwner(")[0]
        for token in ["TextureReadbackColdBudgetPlan::validateOriginal(plan)", "sameBudget(lossBudget,b.deviceLoss)", "CpuDomain::Device", "registrationCapacity->used()>=registrationCapacity->capacity()", "registrationCapacity.charged()", "callbackCapacity.charged()", "core::CpuMemoryQuotaScope scope(quota)", "registration.owner=owner"]:self.assertIn(token,factory)
        self.assertLess(factory.rindex("core::makeUnique"),factory.index("prepareWebGpuNativeDeviceLossRegistration"))
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_actual_owned_readback_context_gates_explicit_prototype_and_drains_shutdown(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_owned_texture_readback_context.h")
        for token in ["public rhi::TextureReadbackOwnedPreparationExtension", "core::UniquePtr<Context>", "core::CpuMemoryQuotaScope scope", "Plan::CpuDomain::Device", "context->registry=registry_", "context->plan=plan", "Staging::prepareOriginal(plan,registry_", "Publication::prepareForDevicePlan(plan,registry_", "prepareWebGpuOriginalReadbackCompletionOwner", "if(!decision.executionAvailable)return {}", "WebGpuOwnedTextureReadbackPreparation::prepareOriginal", "validatePublishedOriginal(original.ownedSnapshot)", "Publication::cancelAllOriginal", "shutdownDisposition(facts)", "originalLossArmed()", "context_->publication->pending()", "context_->staging->usedBytes()", "originalLossDelivered()", "detachOriginalDeviceLoss", "context_.reset()"]:self.assertIn(token,source)
        self.assertLess(source.index("core::makeUnique<Context>"),source.index("prepareWebGpuOriginalReadbackCompletionOwner"))
        self.assertLess(source.index("Publication::prepareForDevicePlan"),source.index("prepareWebGpuOriginalReadbackCompletionOwner"))
        self.assertIn('if(bindingConsumed_)return "RHI-READBACK-OWNED-CONTEXT-CONSUMED"',source)
        self.assertIn("bindingConsumed_=!Plan::originalLossBindingAvailable(plan)",source)
        self.assertNotIn("bindingConsumed_=false",source.split("const char* shutdownOriginal()")[1].split("private:")[0])
        hot=source.split("bool pollOriginal(")[1]
        for token in ["makeShared", "makeUnique", "CpuMemoryQuotaScope", "signalFromHost", "wgpu", "g_readbackSamples", "WebGpuRecordedCommandProof"]:self.assertNotIn(token,hot)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        for token in ["ownedTextureReadbackExtension_(registry_)", "extensionId==rhi::kExtensionTextureReadbackOwnedPreparation", "static_cast<rhi::TextureReadbackOwnedPreparationExtension*>(&ownedTextureReadbackExtension_)"]:self.assertIn(token,device)

    def test_original_readback_complete_graph_uses_explicit_budgets_and_request_quota(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_texture_readback_graph_preparation.h")
        for token in ["lowerOriginal(plan,recipe)", "sameOwner(deviceBudget->originalPlan(),plan)", "Plan::CpuDomain::Request", "RHI-READBACK-GRAPH-REQUEST-QUOTA", "input->labelOwner()->originalMetadataQuota()", "passLabel.size()>plan->budget().graph.maxPassLabelBytes", "recipe.pair,quota", "recipe.projection,quota", "recipe.usage,quota", "recipe.variables,quota", "recipe.scalars,quota", "recipe.inputs,quota", "recipe.states,quota", "WebGpuOriginalTransferStateValidation::validate(states,0u)"]:self.assertIn(token,source)
        for forbidden in ["wgpu", "WebGpuRecordedCommandProof", "65536", "std::vector", "std::make_shared", "prepareWebGpuOrdinaryCompletionOwner"]:self.assertNotIn(forbidden,source)
        lowering=self.read_source("src/rhi/backends/webgpu/webgpu_texture_readback_cold_budget_lowering.h")
        for token in ["const auto& g=b.graph", "g.maxCompletedSourceGraphs", "g.maxProjectedOwners", "g.maxAllocationRecords", "g.maxReferences", "g.maxReferenceUsages", "g.maxCommandTextBytes", "g.maxDescriptorRecords", "g.maxReadbackDescriptors", "g.maxSourceReferences", "recipe.completion={1u,true,b.maxPendingRequests,b.maxCpuDeviceBytes}"]:self.assertIn(token,lowering)
        self.assertNotIn("makeShared",lowering)
        contract=self.read_source("src/rhi/frontend/transfer_readback_command_graph_budget.h")
        for token in ["contractVersion!=1u", "!b.prototypeOptIn", "commandCount=5u,referenceCount=3u,descriptorCount=2u", "maxCpuMetadataBytes!=requestQuotaBytes", "MemoryBudgetLedger::canAdd", "RHI-READBACK-GRAPH-BUDGET-LABEL-RANGE"]:self.assertIn(token,contract)

    def test_original_readback_execution_retains_actual_full_callback_drain(self):
        core=CppSource((self.ROOT / "src/core/runtime/completion_drain_consumer.h").read_text(encoding="utf-8-sig"))
        for token in ["auto consumer=std::move(original)", "if(!drained || !original)return false", "consumeOriginalCallbackDrain"]:self.assertIn(token,core)
        for token in ["wgpu", "WebGpuRecordedCommandProof", "signalFromHost", "makeShared", "makeUnique"]:self.assertNotIn(token,core)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_readback_results.h", "backends_webgpu_webgpu_original_texture_readback_publication_h")
        for token in ["bindRecordingOriginal", "bindSubmissionOriginal", "ExecutionDrainRetention", "Recording::recordOriginalTextureReadback(entry->recording)", "Submission::submit(entry->submission)", "executionOutstanding(*entry)", "readbackRecordingCallbacksDrained()", "callbacksDrained()", "Plan::CpuDomain::Device", "owner->retained.capacity()!=plan_->budget().maxPendingRequests", "sameOwner(submission->originalRecording(),entry->recording)", "sameOwner(recording->originalMetadataQuota(),recordQuota_)", "RHI-READBACK-PUBLICATION-EXECUTION-SUBMISSION", "readbackDrainConsumer_.reset()"]:self.assertIn(token,source)
        for method,next_method in [("const char* record(Pool::Ticket", "const char* bindSubmission("),("const char* submit(Pool::Ticket", "class CreationDrainRetention")]:
            hot=source.split(method)[1].split(next_method)[0]
            for token in ["makeShared", "makeUnique", "CpuMemoryQuotaScope"]:self.assertNotIn(token,hot)
        recording=self.read_source("src/rhi/backends/webgpu/webgpu_native_ordinary_recording.cpp").split("void WebGpuOrdinaryRecording::complete(")[1]
        self.assertLess(recording.index("--self.remaining_"),recording.index("self.status()==Status::DeviceLost"))
        self.assertIn("CompletionDrainConsumer::consumeOnce(self.readbackRecordingCallbacksDrained()",recording)
        submission=self.read_source("src/rhi/backends/webgpu/webgpu_native_ordinary_submission.cpp")
        observe=submission.split("void WebGpuOrdinarySubmission::observe(")[1]
        self.assertLess(observe.index("evidence_.observe(participant,proven)"),observe.index("status()!=Status::Pending"))
        self.assertEqual(submission.count("CompletionDrainConsumer::consumeOnce(callback->owner->callbacksDrained()"),2)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_original_query_slots_have_exclusive_stable_bounded_storage(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_query_slots.h")
        for token in ["core::BoundedSequence<core::UniquePtr<Control>>", "maxCpuGraphBytes", "maxCpuSlotBytes", "slotReservation_.canReserve()", "slotReservation_.reserve()", "slotReservation_.release(ticket_)", "slotReservation_.matches(self->ticket_)", "const Control::Observation slot_", "SharedOwnerProjection::sameOwner(self,observation.graph_)", "slots_.values()[observation.index_]->matches", "core::MutationJournal journal", "journal.rollback();self->invalidateAll", "journal.commit()", "recordingSupported() const noexcept {return false;}", "resultAvailable() const noexcept {return false;}"]:self.assertIn(token,source)
        for token in ["wgpu", "slotReservation_.complete", "signalFromHost", "std::make_shared", "QueryExecutionState::Available", "WebGpuRecordedCommandProof"]:self.assertNotIn(token,source)
        owner=self.read_source("src/rhi/backends/webgpu/webgpu_original_query_owner.h")
        self.assertIn("friend class WebGpuOriginalQuerySlots",owner)
        self.assertIn("mutable core::runtime::CompletionSignal slotReservation_",owner)

    def test_generic_original_buffer_upload_retains_declared_source_without_inventing_bytes(self):
        source=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_buffer_upload_source")
        for token in ["OriginalCommandBufferLease::validateOriginal", "BufferUploadArguments::captureScalarFields", "CommandVariableArguments::prepareOriginal", "source->resource()!=arguments.source()", "destination->resource()!=arguments.destination()", "containsUnsignedRange", "nativeExecutionProven() const noexcept {return false;}", "GpuResourceId resource() const noexcept {return {};}"]:self.assertIn(token,source)
        for token in ["backends/", "wgpu", "WebGpuRecordedCommandProof", "ByteBuffer", "std::make_shared", "signalFromHost"]:self.assertNotIn(token,source)
        table=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        self.assertIn("containsOriginalLease(sources,count,value->originalSourceLease())",table)
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        for token in ["arguments->matches((*source)->arguments())", "resourceMatches(usage,i,0u,arguments->source())", "(*source)->sizeBytes()>budget.maxRetainedUploadBytes-uploadBytes"]:self.assertIn(token,inputs)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalBufferUploadInputsPassed",runner)
        self.assertIn("genericOriginalBufferUploadInputsPassed: genericOriginalBufferUploadInputsPassed",runner)

    def test_generic_original_upload_sources_bind_exact_snapshot_destination_without_native_authority(self):
        source=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_texture_upload_source")
        for token in ["TextureUploadInputSnapshot::validateOriginal", "OriginalCommandTextureLease::validateOriginal", "originalPixelQuota()", "TextureUploadLayoutValidation::inspectPackedBytes", "GpuResourceId resource() const noexcept {return {};}", "nativeExecutionProven() const noexcept {return false;}", "core::makeUnique"]:self.assertIn(token,source)
        for token in ["backends/", "WebGpuRecordedCommandProof", "wgpu", "std::make_shared", "inspectCpuAllocationContaining"]:self.assertNotIn(token,source)
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        for token in ["sameOwner(arguments->originalInput(),(*source)->originalInput())", "maxRetainedUploadBytes-uploadBytes", "TextureUploadLayoutValidation::mipCount(input)", "input.label()!=scalars->originalArguments()->text(i)", "RHI-ORIGINAL-INPUTS-UPLOAD-OWNER"]:self.assertIn(token,inputs)
        table=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        for token in ["OriginalCommandTextureUploadSource", "value.get(),1u", "RHI-ORIGINAL-SOURCES-UPLOAD-DESTINATION"]:self.assertIn(token,table)
        usage=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_usage")
        self.assertIn("RHI-ORIGINAL-USAGE-UPLOAD-GRAPH-QUOTA",usage)
        projection=CppSource((self.ROOT / "src/core/shared_identity_projection.h").read_text(encoding="utf-8-sig"))
        self.assertIn("keyNamespace",projection)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for name in ["genericOriginalTextureUploadInputsPassed", "sharedIdentityNamespacePassed"]:
            self.assertIn("&& "+name,runner)
            self.assertIn(name+": "+name,runner)

    def test_original_state_contract_cannot_infer_issuers_from_scalar_values_or_backend_names(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_model.h","src/rhi/semantics/command_state_lowering.h","original_command_state_admission")
        for token in ["BufferCommittedRevision", "TextureRangeCommittedRevision", "CanonicalParentRange", "StateFreeRegisteredLifetime", "CanonicalRegisteredClosure", "maxDeviceDomains!=1u", "maxResourceRegistrations", "maxStateObservations", "maxTextureRanges", "maxRetainedOwners", "maxCpuMetadataBytes", "originalIssuerAvailable=false,executionAvailable=false", "RHI-ORIGINAL-STATE-REGISTRATION-UNPROVEN"]:self.assertIn(token,source)
        for backend in ["VULKAN", "METAL", "WEBGPU"]:self.assertIn("RHI-ORIGINAL-STATE-"+backend+"-ISSUER-UNIMPLEMENTED",source)
        for token in ["backends/", "new ", "std::make_shared", "RecordedResourceStateControl(", "signalFromHost", "WebGpuRecordedCommandProof", "nativeHandle"]:self.assertNotIn(token,source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalStateContractPassed",runner)
        self.assertIn("genericOriginalStateContractPassed: genericOriginalStateContractPassed",runner)

    def test_original_l2_native_loss_uses_sealed_domain_receiver_and_independent_opt_in(self):
        lifetime=self.read_source("src/rhi/frontend/resource_device_lifetime.h")
        for token in ["class NativeLossReceiver final", "friend class GpuDevice", "std::weak_ptr<OriginalDeviceLifetime>", "deliverNativeLoss() const noexcept", "Terminal::NativeLost", "RHI-ORIGINAL-DEVICE-LIFETIME-NATIVE-LOST"]:self.assertIn(token,lifetime)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        for token in ["prepareOriginalDeviceNativeLoss", "detachOriginalDeviceNativeLoss", "originalDeviceNativeLossDiagnostic", "OriginalDeviceLifetime::NativeLossReceiver(originalLifetime_)", "RHI-ORIGINAL-DEVICE-LOSS-REGISTRY-ALREADY-PREPARED", "RHI-ORIGINAL-DEVICE-LOSS-QUOTA-DOMAIN", "hasQuotaBoundSharedOwner(binding)", "validateOriginalNativeLossResource(object)"]:self.assertIn(token,device)
        contract=self.read_source("src/rhi/frontend/resource_device_native_loss.h")
        for token in ["DeviceLossRegistrationContract::validateBudget", "retainedOwnerCount=1u", "executionAvailable=false", "RHI-ORIGINAL-DEVICE-LOSS-NULL-VALIDATOR-ONLY", "RHI-ORIGINAL-DEVICE-LOSS-VULKAN-UNIMPLEMENTED", "RHI-ORIGINAL-DEVICE-LOSS-METAL-UNIMPLEMENTED"]:self.assertIn(token,contract)
        for source in [lifetime,device,contract]:
            for token in ["webGpuDeviceLostInfo", "wgpu", "WebGPU.getJsObject"]:self.assertNotIn(token,source)

    def test_webgpu_original_l2_native_loss_captures_source_and_suppresses_before_release(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_device_native_loss.h")
        for token in ["core::makeUnique", "shareOriginal", "nativeCallbackLease_=binding", "ExternalCallbackGate", "prepareWebGpuNativeDeviceLossRegistration(device,binding.get(),&deliver)", "receiver_.deliverNativeLoss()", "payload->ownerDevice==device_", "payload->parentTexture->ownerDevice==device_"]:self.assertIn(token,source)
        runtime=self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.cpp")
        capture=runtime.split("WebGpuOriginalDeviceLossSource::WebGpuOriginalDeviceLossSource()",1)[1].split("const char* WebGpuNativeBufferMapReceipt::prepareCold",1)[0]
        self.assertIn("if(webGpuNativeDeviceAvailable())device_=emscripten_webgpu_get_device();",capture)
        suppression=source[source.index("const char* suppressNativeBorrow() const"):source.index("virtual const char* beforeDetach()")]
        self.assertLess(suppression.index("detachWebGpuNativeDeviceLossRegistration(nativeState_)"),suppression.index("nativeCallbackLease_.reset()"))
        detach=source[source.index("const char* detach()"):source.index("protected:")]
        self.assertLess(detach.index("holdNativeLease()"),detach.index("suppressNativeBorrow()"))
        delivery=source[source.index("static void deliver("):source.index("const rhi::OriginalDeviceLifetime::NativeLossReceiver receiver_")]
        for token in ["core::make", "new ", "push_back", "resize", "wait", "webGpuNativeDeviceLost"]:self.assertNotIn(token,delivery)
        for token in ["std::vector", "std::make_shared", "std::make_unique", "Module.webGpuDeviceLostInfo"]:self.assertNotIn(token,source)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("WebGpuOriginalDeviceLossSource originalLossSource_",device)
        self.assertIn("closeOriginalDeviceLifetimeForShutdown()",device)
        self.assertNotIn("emscripten_webgpu_get_device",device)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalDeviceNativeLossPassed",runner)
        self.assertIn("originalDeviceNativeLossPassed: originalDeviceNativeLossPassed",runner)

    def test_original_transaction_retains_canonical_graph_and_embedded_journal_before_resource_release(self):
        source=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        for token in ["class OriginalCommandStateTransaction final", "Graph::validateOriginal(graph)", "device.validateOriginalDeviceLifetime(graph->device_)", "graph->validateGraph(false)", "core::AtomicCapacityLease countLease_", "core::MutationJournal journal_", "beginOriginalPendingOwnership", "commitOriginalPendingOwnership", "abandonOriginalPendingOwnership", "shareOriginal", "control->matchesPending(*entry.pending,journal_)", "normalized->matchesPending(*entry.normalizedPending,journal_)", "arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend).diagnostic"]:self.assertIn(token,source)
        self.assertLess(source.index("const std::shared_ptr<const Graph> graph_"),source.index("core::MutationJournal journal_"))
        for forbidden in ["backends/", "wgpu", "std::vector", "std::make_shared", "const_cast", "CpuReplay", "RecordedCommandProof", "waitIdle"]:self.assertNotIn(forbidden,source)
        begin=source[source.index("const char* beginOriginalPendingOwnership()"):source.index("const char* commitOriginalPendingOwnership()")]
        self.assertLess(begin.index("canPrepare(journal_)"),begin.index("control->prepare(journal_)"))
        for token in ["core::make", "reserve(", "push_back", "resize("]:self.assertNotIn(token,begin)

    def test_original_state_sequence_resolves_exact_cells_without_mutable_authority(self):
        source=self.read_source("src/rhi/frontend/command_state.h", "frontend_command_state_sequence_h")
        for token in ["class OriginalCommandStateSequence final", "Graph::validateOriginal(graph)", "Sequence::resolve", "geometry.containsCell", "Sequence::validateBarrier", "Sequence::validateMemoryBarrier", "CommandRecordingOrder", "maxStateCells-result.stateCells", "maxSteps-result.steps", "RHI-ORIGINAL-SEQUENCE-ALIAS-BINDING-UNPROVEN", "RHI-ORIGINAL-SEQUENCE-READBACK-DESTINATION-STATE", "originalDestinationLease()"]:self.assertIn(token,source)
        for forbidden in ["backends/", "wgpu", "const_cast", "core::make", "std::vector", "MutationJournal", "prepareOriginalOwnership", "writeBuffer", "RecordedCommandProof"]:self.assertNotIn(forbidden,source)
        root=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        self.assertIn("preflightOriginalStateSequence",root)
        self.assertIn("OriginalCommandStateSequence::inspect(graph_,budget)",root)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_resource_states_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["timelineFactory", "Sequence::inspect", "resolveOriginalTextureCell", "verifyHotNoAllocation", "overlapping.textureSubresources=whole", "BINDLESS_RHI_ORIGINAL_STATE_SEQUENCE_OK"]:self.assertIn(token,fixture)

    def test_original_state_sequence_limits_and_four_backend_gates_remain_independent(self):
        contract=self.read_validation_section("src/rhi/frontend/command_model.h","original_command_state_sequence")
        for token in ["maxCommands=0u,maxDebugDepth=0u", "maxStateCells=0u,maxSteps=0u", "contractVersion!=1u", "!budget.prototypeOptIn"]:self.assertIn(token,contract)
        geometry=self.read_source("src/rhi/frontend/resource_texture_state_range.h")
        for token in ["bool cellRange", "bool containsCell", "grid.coordinates", "point.aspect==range.aspect", "containsUnsignedRange"]:self.assertIn(token,geometry)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalStateSequencePassed",runner)
        self.assertIn("originalStateSequencePassed: originalStateSequencePassed",runner)
        self.assertIn("&& originalStateSequenceReadbackPassed",runner)
        self.assertIn("originalStateSequenceReadbackPassed: originalStateSequenceReadbackPassed",runner)
        self.assertNotIn("case Backend::",contract)

    def test_original_native_loss_observation_is_device_issued_opaque_custody(self):
        source=self.read_source("src/rhi/frontend/resource_loss_validation.h", "frontend_resource_native_loss_observation_h")
        private=source[source.index("private:"):]
        for token in ["friend class GpuDevice", "const std::shared_ptr<OriginalDeviceLifetime> domain_", "const OriginalDeviceLifetime::Ticket lifetime_", "const std::shared_ptr<const OriginalDeviceNativeLossBinding> binding_", "sameOwner(domain_,other.domain_)", "sameOwner(binding_,other.binding_)", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        self.assertIn("OriginalDeviceNativeLossObservation(std::shared_ptr",private)
        for token in ["WGPUDevice", "NativeLossReceiver", "detach()", "friend class WebGpu", "CpuReplay"]:
            self.assertNotIn(token,source)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        for token in ["observeOriginalDeviceNativeLoss", "validateOriginalDeviceNativeLoss", "sameOwner(originalLifetime_,observation.domain_)", "sameOwner(originalLossBinding_,observation.binding_)", "observation.domain_->device_!=this", "hasQuotaBoundSharedOwner(originalLossBinding_)"]:
            self.assertIn(token,device)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_device_native_loss_observation_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyMissing", "verifyNative", "verifyHotNoAllocation", "OBSERVATION-FOREIGN", "LIFETIME-RETIRED", "LIFETIME-SHUTDOWN", "!std::is_constructible_v"]:self.assertIn(token,fixture)
        loss=CppSource((self.ROOT / "tests/fixtures/webgpu_original_l2_device_loss_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("validateOriginalDeviceNativeLoss(*live.lossObservation)",loss)
        self.assertIn("BINDLESS_RHI_ORIGINAL_NATIVE_LOSS_OBSERVATION_LOSS_OK",loss)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalNativeLossObservationPassed",runner)
        self.assertIn("originalNativeLossObservationPassed: originalNativeLossObservationPassed",runner)

    def test_original_complete_access_preflight_binds_portable_layout_and_original_sources(self):
        source=self.read_source("src/rhi/frontend/command_state.h", "frontend_command_access_validation_h")
        for token in ["Sequence::inspect(graph,budget)", "OriginalCommandAccessContract::bufferCopyAlignment", "copyLayoutValid", "copyBlockExtentValid", "full->writeMip", "previousEnd", "TextureUploadLayoutValidation::Mapper", "TextureReadbackLayoutValidation::Mapper", "mip.matches(original.mipCopies()[i])", "ResourceAccessUsageValidation::validate", "DepthStencilAttachmentInputContract::diagnostic", "RHI-ORIGINAL-ACCESS-LAYERED-PASS-UNPROVEN", "PASS-OVERLAP"]:self.assertIn(token,source)
        for token in ["backends/", "wgpu", "core::make", "const_cast", "MutationJournal", "std::vector", "RecordedCommandProof"]:self.assertNotIn(token,source)
        contract=self.read_validation_section("src/rhi/frontend/command_model.h","original_command_access")
        for token in ["bufferCopyAlignment=4u", "WebGPU buffer-copy", "kTextureUploadRowPitchAlignment"]:self.assertIn(token,contract)
        root=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        self.assertIn("preflightOriginalAccesses",root)
        self.assertIn("OriginalCommandAccessValidation::inspect(graph_,budget)",root)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["&& originalAccessPassed", "&& originalAccessReadbackPassed", "&& originalAccessUploadPassed", "originalAccessPassed: originalAccessPassed"]:self.assertIn(token,runner)
        self.assertNotIn("case Backend::",contract)

    def test_reusable_batch_revision_admission_preserves_rollback_without_identity_consumption(self):
        sequence=CppSource((self.ROOT / "src/core/monotonic_id_sequence.h").read_text(encoding="utf-8-sig"))
        for token in ["hasRemaining(std::uint64_t count)", "count-1u<=std::numeric_limits<std::uint64_t>::max()-next"]:self.assertIn(token,sequence)
        value=CppSource((self.ROOT / "src/core/versioned_mutation_value.h").read_text(encoding="utf-8-sig"))
        for token in ["canWriteBatch", "writes!=UINT64_MAX", "sequence_.hasRemaining(writes+1u)"]:self.assertIn(token,value)
        array=CppSource((self.ROOT / "src/core/versioned_mutation_array.h").read_text(encoding="utf-8-sig"))
        self.assertIn("stamp_.canWriteBatch(journal,writes)",array)
        for name in ["recorded_resource_state_control.h", "recorded_texture_state_control.h"]:
            self.assertIn("canWriteBatch",self.read_source(*self.CURRENT_CONTROL_SOURCES[name]))
        for name in ["versioned_mutation_value_fixture.h", "versioned_mutation_array_fixture.h"]:
            fixture=CppSource((self.ROOT / "tests/fixtures" / name).read_text(encoding="utf-8-sig"))
            for token in ["canWriteBatch", "UINT64_MAX-3u", "UINT64_MAX-2u", "journal.rollback()"]:self.assertIn(token,fixture)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_access_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["prepareOriginalResourceRegistration", "preflightOriginalAccesses", "verifyHotNoAllocation", "COPY-MIP-OVERLAP", "COPY-TEXTURE-LAYOUT", "COPY-MIP-COUNT", "COPY-TEXTURE-EXTENT", "BUFFER-USAGE", "BINDLESS_RHI_ORIGINAL_ACCESS_OK"]:self.assertIn(token,fixture)

    def test_canonical_native_lifecycle_inspection_never_issues_execution_or_state_authority(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_lifecycle")
        for token in ["CanonicalOriginalNativeLifecycle/1", "validateSourceBudget", "OriginalCommandStateTransaction::validateOriginal(root)", "hasOriginalPrototypePlan", "root->retainedOwnerCount()", "SharedOwnerProjection::hasIndependentOwners", "QuarantinedBeforeSubmit", "QuarantinedAfterSubmit", "DeviceLostRetained", "SubmittedQueueAndCallbacksDrained", "sealedIssuerAvailable=false,executionAvailable=false", "case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU"]:self.assertIn(token,source)
        for forbidden in ["backends/", "WebGpuRecordedCommandProof", "std::vector", "std::make_shared", "MutationJournal journal", "journal_.", "root->apply", "root->commit", "root->abandon", "core::make", "const_cast"]:self.assertNotIn(forbidden,source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalNativeLifecyclePassed",runner)
        self.assertIn("canonicalNativeLifecyclePassed: canonicalNativeLifecyclePassed",runner)

    def test_canonical_lifecycle_capacity_retains_original_source_and_charges_independent_submission_quota(self):
        source=self.read_validation_section("src/rhi/frontend/command_recording.h","original_command_lifecycle_reservation")
        for token in ["Seal::validateOriginal(seal)", "Contract::validateCapacityRequest", "maxSubmissionOwners", "core::CpuMemoryQuotaScope scope(seal->submission_)", "core::makeUnique", "shareOriginal", "core::AtomicCapacityLease submission_,callbacks_,registrations_", "{source,&source->pendingSubmissions_}", "{source,&source->callbackTickets_}", "{source,&source->callbackRegistrations_}", "canAcquire(request.pendingSubmissions)", "seal->lifecycleReserved_=true", "if(published_)seal_->lifecycleReserved_=false", "nativeExecutionProven() const noexcept {return false;}"]:self.assertIn(token,source)
        self.assertLess(source.index("auto result=shareOriginal"),source.index("seal->lifecycleReserved_=true"))
        for forbidden in ["makeShared<core::AtomicCapacityBudget>", "backends/", "WebGpuRecordedCommandProof", "MutationJournal", "stateControl", "root_->apply", "root_->commit", "std::vector", "std::make_shared", "inspectTransition("]:self.assertNotIn(forbidden,source)
        original=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        for token in ["pendingSubmissions_(budget.maxPendingSubmissions)", "callbackTickets_(budget.maxCallbackTickets)", "callbackRegistrations_(budget.maxCallbackRegistrations)", "originalReservedSubmissionCount", "originalReservedCallbackTicketCount", "originalReservedCallbackRegistrationCount"]:self.assertIn(token,original)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalLifecycleCapacityPassed",runner)
        self.assertIn("canonicalLifecycleCapacityPassed: canonicalLifecycleCapacityPassed",runner)

    def test_canonical_live_identity_is_private_and_keeps_cold_state_strict(self):
        header=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.h")
        self.assertIn("validateCanonicalLive",header[header.index("private:"):])
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        live=source[source.index("const char* WebGpuNativeCommandPreparation::validateCanonicalLive"):source.index("const char* WebGpuCanonicalColdCommand::validate()")]
        for token in ["validateCanonicalIdentity(original,true)", "Status::Pending", "root->journalEmpty()", "!state.finished", "!state.commandBuffer", "state.renderPass || state.computePass"]:self.assertIn(token,live)
        identity=source[source.index("const char* WebGpuNativeCommandPreparation::validateCanonicalIdentity"):source.index("const char* WebGpuNativeCommandPreparation::validateCanonicalLive")]
        for token in ["inspectCanonicalCommandOwnership", "inspectShellIdentity(original.command_,original.admission_,original.device_,original.queue_", "original.admission_->originalClosure()->nativeDevice(),original.identity_", "nativeColdCommandClaimed_"]:self.assertIn(token,identity)
        shared=source[source.index("WebGpuNativeCommandPreparation::ShellIdentity WebGpuNativeCommandPreparation::inspectShellIdentity"):source.index("const char* WebGpuNativeCommandPreparation::inspectCanonicalColdCommand")]
        for token in ["sameOwner(state.canonicalColdAdmission,admission)", "state.recordingIdentity!=identity", "state.device!=device", "state.queue!=queue", "state.device!=closureDevice", "state.ordinaryOriginalStateTransaction", "sharedOwnerMatches(command)"]:self.assertIn(token,shared)
        for token in ["wgpuQueueSubmit", "wgpuDeviceCreateCommandEncoder", "applyOriginalPlan", "commitOriginalOwnership", "completion_->", "makeUnique", "makeShared"]:self.assertNotIn(token,live+identity)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_lifecycle")
        cold=contract[contract.index("static const char* inspectOriginalPreparation"):contract.index("static const char* inspectOriginalOwnership")]
        self.assertIn("Status::Prepared",cold)
        self.assertIn("return inspectOriginalOwnership",cold)
        root=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        self.assertEqual(re.findall(r"friend class (\w+);",root)[:1],["OriginalCommandRecordingSeal"])

    def test_canonical_queue_submission_contract_keeps_unimplemented_execution_closed(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_queue_submission")
        for token in ["contractVersion=0u", "prototypeOptIn=false", "submissionAvailable=false", "completionCommitAvailable=false", "nativeSyncAvailable=false",
                      "case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU", "MaySubmit BEFORE", "Never commit in pool.submitWith", "No rollback/retry/cancel", "issued callbacks drain/suppress"]:self.assertIn(token,contract)
        for token in ["wgpuQueueSubmit(", "commitOriginalOwnership(", "friend class", "makeShared", "makeUnique", "signalFromHost"]:self.assertNotIn(token,contract)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_queue_submission_contract_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["d.submissionAvailable==(backend==rhi::Backend::WebGPU)", "d.completionCommitAvailable==(backend==rhi::Backend::WebGPU)", "!d.nativeSyncAvailable", "E::MaySubmit", "A::InvalidateAndRetain", "E::CompletionProven", "E::BeforeSubmitScopesDrained", "A::InvalidateAndRelease"]:self.assertIn(token,fixture)

    def test_canonical_submission_publishes_retention_before_native_boundary(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        entry=source[source.index("const char* WebGpuCanonicalIssuerPreparation::submitOriginal("):source.index("bool WebGpuCanonicalIssuerPreparation::hasOriginalCompletedSubmission")]
        for token in ["Contract::validateIntent(intent)", "arp::rhi::semantics::OriginalCommandQueueSubmissionLowering::lowering(rhi::Backend::WebGPU)", "decision.submissionAvailable", "decision.completionCommitAvailable", "hasQuotaBoundSharedOwner", "state->maySubmit", "submitCanonical"]:self.assertIn(token,entry)
        hot=source[source.index("const char* WebGpuOrdinarySubmission::submitCanonical("):source.index("void WebGpuOrdinarySubmission::completeCanonical(")]
        self.assertLess(hot.index("validateCanonicalLive()"),hot.index("pool.submit(state->ticket)"))
        self.assertLess(hot.index("pool.submit(state->ticket)"),hot.index("native.submitted=true"))
        self.assertLess(hot.index("native.submitted=true"),hot.index("WebGpuNativeCompletionReceipt::invoke"))
        self.assertLess(hot.index("slot.owner=state;slot.gate.arm()"),hot.index("WebGpuNativeCompletionReceipt::invoke"))
        for forbidden in ["submitWith", "commitOriginalSubmission", "makeShared", "makeUnique", "WebGpuOrdinaryCompletionReservation", "rollback"]:self.assertNotIn(forbidden,hot)

    def test_canonical_submission_commits_only_after_exact_native_evidence_and_drain(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        finish=source[source.index("void WebGpuOrdinarySubmission::finishCanonicalSubmission()") :]
        for token in ["canonicalIssuanceComplete_", "evidence_.allReceived()", "foreignCallbacksDrainedExceptLoss()", "pool.retainedInvalidated", "canonicalInvocationProven_", "evidence_.proven()", "queueNativeReceiptReceived", "validateCanonicalLive()", "state->detachLoss()", "seal->commitOriginalSubmission()", "state->submissionCompleted=true", "pool.resolve(state->ticket,true)"]:self.assertIn(token,finish)
        self.assertLess(finish.index("validateCanonicalLive()"),finish.index("commitOriginalSubmission()"))
        self.assertLess(finish.index("state->submissionCompleted=true"),finish.index("pool.resolve(state->ticket,true)"))
        callback=source[source.index("void WebGpuOrdinarySubmission::completeCanonical("):source.index("void WebGpuOrdinarySubmission::finishCanonicalSubmission()")]
        for token in ["slot->gate.beginDelivery()", "slot->ticket.generation==state->ticket.generation", "queueNativeReceiptReceived=true", "evidence_.observe", "slot->gate.finishDelivery();slot->owner.reset()"]:self.assertIn(token,callback)
        self.assertLess(callback.index("slot->gate.finishDelivery();slot->owner.reset()"),callback.index("self->finishCanonicalSubmission()"))

    def test_canonical_queue_suppression_requires_native_termination_or_physical_loss(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        release=source[source.index("void releaseTerminalIfDrained()"):source.index("void quarantine(")]
        for token in ["foreignCallbacksDrainedExceptLoss()", "Phase::Delivered", "if(maySubmit && !queueNativeReceiptReceived && !lost)return", "detachLoss()", "pool.releaseInvalidated(ticket)"]:self.assertIn(token,release)
        cancel=source[source.index("const char* cancel("):source.index("const std::shared_ptr<core::CpuMemoryQuota> submissionQuota")]
        submitted=cancel[cancel.index("if(maySubmit)"):cancel.index("// Cancellation cannot")]
        self.assertIn("quarantine",submitted)
        for token in ["rollback", "pool.cancel", "detachLoss"]:self.assertNotIn(token,submitted)

    def test_canonical_native_receipts_are_cold_bounded_and_suppress_partial_registration(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.cpp")
        receipt=source[source.index("WebGpuNativeCompletionReceipt::inspect"):source.index("prepareWebGpuNativeDeviceLossRegistration(")]
        prepare=receipt[receipt.index("WebGpuNativeCompletionReceipt::prepare("):receipt.index("WebGpuNativeCompletionReceipt::pushScopes(")]
        self.assertEqual(prepare.count("=function("),2)
        self.assertNotIn(".then(",prepare)
        for token in ["catch(error){return 0;}", "Object.getOwnPropertyDescriptor", "Phase::Prepared", "Phase::Bound", "Phase::Delivered", "Phase::Suppressed", "state.userdata=0;state.complete=0", "getWasmTableEntry"]:self.assertIn(token,prepare)
        bind=receipt[receipt.index("WebGpuNativeCompletionReceipt::bind("):receipt.index("WebGpuNativeCompletionReceipt::prepared(")]
        for token in ["popErrorScope()", "onSubmittedWorkDone()", "promise.then(state.resolved,state.failed)", "state.phase=state.suppressed;state.userdata=0;state.complete=0", "arpCanonicalScopeStackUnproven=true"]:self.assertIn(token,bind)
        for token in ["signalFromHost", "new TypeError", "new Promise", "queue.submit"]:self.assertNotIn(token,bind)

    def test_canonical_scoped_recording_uses_exact_slots_and_private_seal_without_submission(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        scoped=source[source.index("const char* WebGpuCanonicalIssuerPreparation::recordOriginal("):source.index("const char* WebGpuCanonicalIssuerPreparation::submitOriginal(")]
        entry=scoped[:scoped.index("bool WebGpuCanonicalIssuerPreparation::hasOriginalRecordedCommand")]
        for token in ["Contract::validateIntent(intent)", "arp::rhi::semantics::OriginalCommandScopedRecordingLowering::lowering(rhi::Backend::WebGPU)", "decision.recordingAvailable", "hasQuotaBoundSharedOwner", "self->validate()", "recordCanonical"]:self.assertIn(token,entry)
        record=scoped[scoped.index("const char* WebGpuOrdinaryRecording::recordCanonical("):scoped.index("void WebGpuOrdinaryRecording::completeCanonical(")]
        self.assertLess(record.index("validateCanonicalCold"),record.index("applyOriginalRecordingPlan"))
        self.assertLess(record.index("WebGpuNativeErrorScopeBatch::push"),record.index("wgpuDeviceCreateCommandEncoder"))
        self.assertLess(record.index("wgpuCommandEncoderFinish"),record.index("wgpuDevicePopErrorScope"))
        for token in ["projection.copies.values()", "operation.copy", "WebGpuNativeTransferCopyLowering::emit", "projection.passes.values()[ordered.passIndex].renderPass", "projection.operations.values()", "slot.owner=state;slot.gate.arm()", "callback.userdata1=state->slots[i].get()", "canonicalIssuanceComplete_=true"]:self.assertIn(token,record)
        callback=scoped[scoped.index("void WebGpuOrdinaryRecording::completeCanonical("):]
        for token in ["slot->role!=S::Role::RecordingScope", "slot->gate.beginDelivery()", "slot->ticket.generation==state->ticket.generation", "pool.get(slot->ticket)", "validateCanonicalIdentity", "canonicalEvidence_.observe", "slot->gate.finishDelivery();slot->owner.reset()"]:self.assertIn(token,callback)
        self.assertLess(callback.index("slot->gate.finishDelivery();slot->owner.reset()"),callback.index("self->finishCanonicalRecording()"))
        for forbidden in ["wgpuQueueSubmit", "commitOriginalOwnership", "WebGpuOrdinaryCompletionReservation", "makeUnique", "makeShared", "std::vector", "signalFromHost"]:self.assertNotIn(forbidden,scoped)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])
        root=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        self.assertEqual(re.findall(r"friend class (\w+);",root[:root.index("class ConstructionKey")]),["OriginalCommandRecordingSeal","OriginalGeneralOutputSnapshotContract"])
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalScopedRecordingPassed",runner)
        self.assertIn("canonicalScopedRecordingPassed: canonicalScopedRecordingPassed",runner)

    def test_canonical_issuer_storage_is_opaque_and_ordinary_execution_refuses_it(self):
        header=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_issuer_preparation.h")
        public=header[header.index("public:\n    using Budget"):header.index("private:",header.index("public:\n    using Budget"))]
        for forbidden in ["WebGpuRecordedCommandProof", "WebGpuOrdinaryRecording", "WebGpuOrdinarySubmission", "nativeDevice()", "nativeQueue()", "record(", "submit(", "deliver("]:
            self.assertNotIn(forbidden,public)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        prepare=source[source.index("WebGpuCanonicalCallbackCustody::prepareOriginalIssuers("):source.index("const char* WebGpuCanonicalCallbackCustody::validateOriginalIssuers")]
        for forbidden in ["wgpu", "MutationJournal", "State::Slot>", "WebGpuNativeCommandPreparation::allocate", "WebGpuOrdinaryCompletionReservation", "ordinaryOriginalStateTransaction", "applyOriginalPlan", "std::make_shared"]:
            self.assertNotIn(forbidden,prepare)
        self.assertLess(prepare.index("auto result=Wrapper::shareOriginal"),prepare.index("state.issuersClaimed=true"))
        for name in ["recording", "submission"]:
            h=CppSource((self.ROOT / ("src/rhi/backends/webgpu/webgpu_native_ordinary_"+name+".h")).read_text(encoding="utf-8-sig"))
            self.assertIn("std::weak_ptr<WebGpuCanonicalCallbackCustody::State>",h)
            self.assertNotIn("const std::shared_ptr<WebGpuCanonicalCallbackCustody::State> canonicalState_;",h)
            self.assertIn("const std::weak_ptr<void> canonicalState_;",h)
            self.assertIn("const bool typedCanonicalMode_=false;",h)
            body=CppSource((self.ROOT / ("src/rhi/backends/webgpu/webgpu_native_ordinary_"+name+".cpp")).read_text(encoding="utf-8-sig"))
            status=body[body.index("::status() const noexcept"):body.index("const char* WebGpuOrdinary",body.index("::status() const noexcept"))]
            self.assertLess(status.index("if(canonicalMode_)"),status.index("completion_"))
        state=source[source.index("void releaseTerminalIfDrained()"):source.index("static void consumeNativeLoss")]
        self.assertLess(state.index("submission.reset()"),state.index("recording.reset()"))
        self.assertLess(state.index("recording.reset()"),state.index("lowering.reset()"))
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("success: success && geometryReadbackCompositionContractPassed && originalStreamGeometryExecutionPassed && canonicalIssuerPreparationPassed",runner)

    def test_canonical_recording_seal_removes_CPU_mutation_without_native_event_authority(self):
        seal=self.read_validation_section("src/rhi/frontend/command_recording.h","original_command_recording_seal")
        for token in ["Root::validateOriginal(root)", "compareOriginalGraph", "Contract::inspectOriginalPreparation", "additionalOwnerCount=3u", "core::CpuMemoryQuotaScope scope(root->originalMetadataQuota())", "core::makeUnique", "shareOriginal", "root->recordingSealed_=true", "published_=true", "rollbackOriginalOwnership", "SharedOwnerProjection::sameOwner", "nativeExecutionProven() const noexcept {return false;}"]:self.assertIn(token,seal)
        self.assertLess(seal.index("auto result=shareOriginal"),seal.index("root->recordingSealed_=true"))
        for forbidden in ["backends/", "WebGpuRecordedCommandProof", "friend class arp::test", "MutationJournal journal", "std::vector", "std::make_shared", "inspectTransition("]:self.assertNotIn(forbidden,seal)
        root=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        for name in ["materializeOriginalPrototypePlan", "applyOriginalPrototypePlan", "beginOriginalPendingOwnership", "commitOriginalPendingOwnership", "abandonOriginalPendingOwnership"]:
            body=root[root.index("const char* "+name):]
            self.assertIn("cpuMutationDiagnostic()",body[:body.index("\n    }")])
        self.assertIn("friend class OriginalCommandRecordingSeal",root)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalRecordingSealPassed",runner)
        self.assertIn("canonicalRecordingSealPassed: canonicalRecordingSealPassed",runner)

    def test_canonical_copy_pass_lowering_preserves_complete_mips_and_original_custody(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        payload=source[source.index("class WebGpuCanonicalCopyPassLowering::Payload"):source.index("class WebGpuCanonicalCallbackCustody::RuntimeState")]
        for token in ["closure.entries_.findIf", "TransferCopyProjection::visit", "TransferCopyInputSnapshot::validateOriginal", "WebGpuTransferCopyValidation::validate",
                      "WebGpuNativeTransferCopyLowering::textureDiagnostic", "writeSeal->transferWritesSealed()", "attachmentTransition", "maxCopyOperations",
                      "validatePassShape", "PASS-FORMAT", "core::BoundedSequence<Operation>", "using Pass=WebGpuOriginalRenderPassPayload", "Pass(original.get(),0u)"]:
            self.assertIn(token,payload)
        builder=self.read_source("src/rhi/backends/webgpu/webgpu_original_render_pass_payload.h")
        for token in ["core::BoundedSequence<WGPURenderPassColorAttachment>", "WebGpuAttachmentOperationLowering::load", "WebGpuAttachmentOperationLowering::store", "WebGpuAttachmentOperationLowering::depthStencil", "desc.hasResolveTarget ? view(desc.resolveTarget) : nullptr", "maxDraw.maxDrawCount=draws", "renderPass.label=webGpuNativeStringView(pass->desc().label)"]:
            self.assertIn(token,builder)
        state=source[source.index("class WebGpuCanonicalCallbackCustody::RuntimeState"):]
        release=state[state.index("void releaseTerminalIfDrained()"):state.index("static void consumeNativeLoss")]
        self.assertLess(release.index("foreignCallbacksDrained()"),release.index("lowering.reset()"))
        self.assertLess(release.index("lowering.reset()"),release.index("pool.releaseInvalidated(ticket)"))
        self.assertLess(state.index("lowering.reset();"),state.index("pool.cancel(ticket)"))
        prepare=state[state.index("WebGpuCanonicalCallbackCustody::prepareOriginalLowering("):state.index("std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::allocateCanonical")]
        for token in ["state.loweringClaimed", "Payload::inspectCopies", "Payload::inspectPass", "core::CpuMemoryQuotaScope scope(admission.originalRoot()->originalMetadataQuota())",
                      "core::makeUnique<Payload>", "payload->validate(budget)", "state.lowering=std::move(payload)", "original.owners_>lifecycle.maxCallbackOwners"]:
            self.assertIn(token,prepare)
        self.assertLess(prepare.index("auto result=Lowering::shareOriginal"),prepare.index("state.loweringClaimed=true"))
        for token in ["wgpuQueueSubmit(", "wgpuDeviceCreateCommandEncoder(", "signalFromHost", "applyOriginalPrototypePlan", "WebGpuOriginalStateTransaction::prepare", "std::make_shared"]:
            self.assertNotIn(token,payload+prepare)
        projection=self.read_source("src/rhi/frontend/transfer_copy.h", "frontend_transfer_copy_projection_h")
        for token in ["full->writeMip(i,mip)", "RHI-COPY-PROJECTION-SCALAR-BINDING", "RHI-COPY-PROJECTION-FIRST-MIP", "projected.destinationSubresources.baseMipLevel+=", "projected.bufferOffsetBytes=mip.bufferOffsetBytes", "return id"]:
            self.assertIn(token,projection)
        for token in ["WebGpu", "backends/", "wgpu", "makeShared", "makeUnique", "reserve(", "mipExtent"]:self.assertNotIn(token,projection)
        wrapper=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_copy_pass_lowering.h")
        public=wrapper[wrapper.index("public:\n    using Budget"):wrapper.index("private:",wrapper.index("public:\n    using Budget"))]
        for token in ["Payload&", "nativeDevice()", "nativeQueue()", "emit(", "submit(", "record(", "WebGpuRecordedCommandProof"]:self.assertNotIn(token,public)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_transfer_validation.h","src/rhi/semantics/command_lowering.h","original_command_copy_pass_lowering")
        for token in ["case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU", "additionalOwnerCount=2u", "BARRIER-SCOPE", "BARRIER-RANGE", "SUBPASS-UNPROVEN", "EMPTY-PASS", "COLOR-CAPACITY"]:self.assertIn(token,contract)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalCopyPassLoweringPassed",runner)
        self.assertIn("canonicalCopyPassLoweringPassed: canonicalCopyPassLoweringPassed",runner)

    def test_typed_native_execution_requires_real_issuers_and_frozen_ownership(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        retained=source.split("WebGpuCanonicalCallbackCustody::validateTypedRetainedState(",1)[1].split("WebGpuOrdinaryRecording::validateTypedLive",1)[0]
        self.assertLess(retained.index("validateTypedRetained(*graph->command)"),retained.index("validateRetainedResults(seal)"))
        for token in ["sharedOwnerMatches(state)","state->pool.get(state->ticket)","typedCanonicalState()","observers_.inspect(state->observerTicket)","slot->ticket.generation","matchesIssued","validateRetainedMapReceipts"]:
            self.assertIn(token,retained)
        record=source.split("WebGpuOrdinaryRecording::recordTyped(",1)[1].split("WebGpuOriginalTypedIssuerPreparation::recordOriginal",1)[0]
        self.assertLess(record.index("validateTypedRetainedState(state)"),record.index("applyOriginalRecordingPlan()"))
        self.assertLess(record.index("validateDispatch"),record.index("applyOriginalRecordingPlan()"))
        for token in ["WebGpuNativeInvocation::guarded", "WebGpuNativeCompletionReceipt::pushScopes", "WebGpuNativeCompletionReceipt::bind", "NativeInvocationUncertain", "finishCanonicalRecording"]:
            self.assertIn(token,record)
        for forbidden in ["maySubmit=", "makeShared", "makeUnique", "signalFromHost", "prepareWebGpuNativeDeviceLossRegistration", "wgpuQueueSubmit"]:
            self.assertNotIn(forbidden,record)
        runtime=self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.cpp")
        self.assertIn("s.phase===s.delivered && s.delivering",runtime)
        self.assertIn("state.userdata=0;state.complete=0",runtime)
        decision=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h")
        frozen=decision.split("const char* validateImmutableBindings()",1)[1].split("static CandidateOperation",1)[0]
        for forbidden in ["P::visit(", "CapturedJoin", "applyOriginal", "makeShared", "makeUnique"]:self.assertNotIn(forbidden,frozen)
        self.assertIn("P::validateFrozenBinding",frozen)
        for file in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp"]:
            self.assertIn("OriginalTypedNativeExecutionFixture::verifyContract()",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_native_execution_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["recordPending", "hasOriginalCompletedSubmission", "values[0]==43u", "values[1]==36u", "verifyHotNoAllocation", "CANCEL-CALLBACKS-PENDING", "Scopes::releaseScopes"]:self.assertIn(token,fixture)
        for forbidden in ["maySubmit=", "applyOriginalPrototypePlan", "completeTyped(", "deliverNativeLoss"]:self.assertNotIn(forbidden,fixture)

    def test_typed_graphics_acceptance_reuses_original_actor_and_actual_issuers(self):
        actor=CppSource((self.ROOT / "tests/fixtures/original_typed_attachment_dependency_probe_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["template<bool execute=false", "if constexpr(execute)OriginalTypedNativeGraphicsExecutionFixture::authorizeSource(admission)",
                      "if constexpr(!execute)", "GRAPHICS_VALUES_OK", "Root::Status::Committed".replace("Root::", "rhi::OriginalCommandStateTransaction::")]:
            self.assertIn(token,actor)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_native_graphics_execution_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["authorizeSource", "source.maxNativePayloadObjects=5u+1u+1u", "Issuers::recordOriginal", "Issuers::submitOriginal",
                      "verifyHotNoAllocation", "Root::Status::Pending", "Root::Status::Committed", "WGPULoadOp_Clear",
                      "nativePass.maxDraw.maxDrawCount", "dependency.original.following->scope.first", "Scopes::releaseScopes", "submission->liveBytes()"]:
            self.assertIn(token,fixture)
        for forbidden in ["applyOriginalPrototypePlan", "maySubmit=", "wgpuQueueSubmit", "wgpuRenderPassEncoderDraw", "completeTyped(", "deliverNativeLoss"]:
            self.assertNotIn(forbidden,fixture)
        factory=CppSource((self.ROOT / "tests/fixtures/original_accepted_graphics_pipeline_factory_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("device->prepareOriginalDeviceNativeLoss(lossBudget,*observedRequest,lossQuota)",factory)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["GRAPHICS_CONTRACT", "GRAPHICS_EXECUTION", "GRAPHICS_VALUES", "GRAPHICS_RELEASE"]:self.assertIn(token,runner)

    def test_typed_finite_acceptance_retains_real_views_and_original_issuers(self):
        builder=CppSource((self.ROOT / "tests/fixtures/original_typed_shader_dependency_probe_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["template<bool finite=false", "target.minimumBindingBytes<buffers[buffer]->layout().sizeBytes",
                      "e.write.bufferBindingRange={2u,0u,target.minimumBindingBytes}", "DescriptorBufferBindingRangeContract::resolve",
                      "OriginalFiniteDescriptorWriteFixture::commitActual", "sourceBudget.contractVersion=targetMemory ? 4u : 3u", "sourceBudget.finiteFullBoundViewOptIn=true"]:
            self.assertIn(token,builder)
        write=CppSource((self.ROOT / "tests/fixtures/original_finite_descriptor_write_fixture.h").read_text(encoding="utf-8-sig"))
        actual=write.split("static const char* commitActual",1)[1].split("template<class Context,class Pipeline> const char* begin",1)[0]
        for token in ["beginOriginalFiniteDescriptorWrite", "drain(object);finishActual(object,domains)", "object.hasOriginalFiniteContent()"]:self.assertIn(token,actual)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_native_execution_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["Builder::prepare<finite,targetMemory>", "SHADER-ENTRY-RUNTIME-LENGTH-AVAILABLE-TARGET-PROFILE", "FINITE_PROFILE_REJECT_OK",
                      "finishColdCallObservation", "!live->retained.transaction", "Issuers::recordOriginal", "Issuers::submitOriginal"]:self.assertIn(token,fixture)
        for forbidden in ["applyOriginalPrototypePlan", "maySubmit=", "completeTyped(", "deliverNativeLoss"]:self.assertNotIn(forbidden,fixture)
        graphics=CppSource((self.ROOT / "tests/fixtures/original_typed_attachment_dependency_probe_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["bool finite=false", "OriginalFiniteDescriptorWriteFixture::commitActual", "sourceBudget.contractVersion=3u",
                      "shared.runtimeArrayLength(declarations[0].write.bufferBindingRange.sizeBytes", "FINITE_GRAPHICS_VALUES_OK", "FINITE_GRAPHICS_RELEASE_OK"]:self.assertIn(token,graphics)
        retained=CppSource((self.ROOT / "tests/fixtures/original_typed_native_graphics_execution_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["original->sourceContractVersion()==3u", "element.declaration.bufferAccessBytes", "FINITE_GRAPHICS_EXECUTION_OK"]:self.assertIn(token,retained)
        source=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_source")
        helper=source.split("const char* validateFrozenFiniteFootprints()",1)[1].split("bool nativeExecutionProven",1)[0]
        for token in ["validateFullBoundView", "definition_", "layout().sizeBytes", "maxFrozenDynamicOffsets"]:self.assertIn(token,helper)
        for token in ["validateFiniteContent", "GpuDevice", "validateObjectLocal"]:self.assertNotIn(token,helper)
        self.assertIn("if(const auto* id=validateFrozenFiniteFootprints())return id;",source)
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        immutable=closure.split("static const char* validateTypedBinding",1)[1].split("// Allocation-free dry mapping",1)[0]
        self.assertIn("(*frozen)->validateFrozenFiniteFootprints()",immutable)

    def test_target_finite_typed_execution_consumes_profile_and_exact_frozen_join(self):
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["Request{2u,true,Request::Profile::FiniteTargetMemory1}", "Request::Profile::FiniteWrite1", "diagnoseOriginalNativeIdentity(identity)", "frozenPipelineSourceIndex()!=binding->pipelineSourceIndex", "frozenLogicalSet()!=slot->set"]:self.assertIn(token,closure)
        source=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_source")
        shared=source.split("const char* validateFrozenFiniteFootprints()",1)[1].split("bool nativeExecutionProven",1)[0]
        for token in ["validateBudget(budget_)", "sameOwner(declaration.buffer,*buffer)", "descriptorBindingWriteMatches(declaration.write,element.declaration.write)", "declaration.dynamicOffset!=element.declaration.dynamicOffset"]:self.assertIn(token,shared)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_native_execution_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verify<true,true>", "TARGET_FINITE_EXECUTION_VALUES_OK", "TARGET_FINITE_MUTATIONS_OK", "TARGET_FINITE_PHYSICAL_LOSS_OK", "preparePhysicalLosses<targetMemory>", "executePhysicalLosses<true>", "values[0]==43u && values[1]==36u"]:self.assertIn(token,fixture)
        for forbidden in ["applyOriginalPrototypePlan", "CapturedJoin", "maySubmit=", "originalBudget()"]:self.assertNotIn(forbidden,fixture)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)

    def test_target_finite_candidate_distinct_profile_reuses_original_transaction(self):
        contract=self.read_validation_section("src/rhi/frontend/descriptor_buffer_write_validation.h","original_finite_target_memory_write")
        for token in ["contractVersion!=1u", "prototypeOptIn", "Shared::validateBudget", "VULKAN-ISSUER-MISSING", "METAL-ISSUER-MISSING", "NULL-VALIDATOR-ONLY"]:self.assertIn(token,contract)
        resources=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        for token in ["sizeof(Request)==sizeof(LegacyRequest)", "alignof(Request)==alignof(LegacyRequest)", "state->request=request", "targetMemory()", "diagnoseOriginalTargetMemoryBufferInterval", "lowerTargetMemoryBindingDiagnostic", "state.targetMemory()!=targetMemory", "FiniteTargetMemory1"]:self.assertIn(token,resources)
        identity=self.read_validation_section("src/rhi/frontend/descriptor_validation.h","original_descriptor_native_identity")
        self.assertIn("request.contractVersion==2u && request.profile==Request::Profile::FiniteTargetMemory1",identity)
        source=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_source")
        for token in ["version==3u || version==4u", "budget_.contractVersion)", "object_.adoptOriginal"]:self.assertIn(token,source)
        assembly=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        self.assertIn("(assembly==4u && source==4u)",assembly)
        self.assertIn("(assembly==3u && source==3u)",assembly)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_finite_descriptor_write_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["commitActual", "targetMemory=false", "cancelOriginalFiniteWrite", "RHI-ORIGINAL-SCOPED-WRITE-PROFILE", "verifyHotNoAllocation"]:self.assertIn(token,fixture)

    def test_target_memory_interval_diagnostic_is_independent_and_reuses_original_join(self):
        contract=self.read_validation_section("src/rhi/frontend/descriptor_buffer_declaration_validation.h","original_target_memory_interval")
        for token in ["prototypeOptIn", "contractVersion!=1u", "validateType", "writeAvailable=false,executionAvailable=false",
                      "RHI-ORIGINAL-TARGET-INTERVAL-NULL-VALIDATOR-ONLY", "RHI-ORIGINAL-TARGET-INTERVAL-VULKAN-ISSUER-MISSING",
                      "RHI-ORIGINAL-TARGET-INTERVAL-METAL-ISSUER-MISSING"]:self.assertIn(token,contract)
        observation=contract.split("struct Observation",1)[1].split("static constexpr",1)[0]
        for token in ["runtimeLength", "WGPU", "shared_ptr", "nativeDevice"]:self.assertNotIn(token,observation)
        declaration=self.read_validation_section("src/rhi/frontend/descriptor_buffer_declaration_validation.h","original_target_buffer_declaration")
        for token in ["output={}", "originalTargetBufferMemory", "runtimeArrayStrideBytes", "minimumBindingBytes", "original->vertex()"]:self.assertIn(token,declaration)
        for token in ["originalRuntimeLengthOperation", "runtimeLengthAvailableTargetDiagnostic", "wgpu"]:self.assertNotIn(token,declaration)
        runtime=self.read_validation_section("src/rhi/frontend/descriptor_buffer_declaration_validation.h","original_runtime_length_interval")
        self.assertLess(runtime.index("runtimeLengthAvailableTargetDiagnostic"),runtime.index("OriginalTargetBufferDeclarationContract::diagnose"))
        for token in ["originalRuntimeLengthOperationCount", "originalRuntimeLengthOperation", "diagnoseImpl(child,set,binding,type,selected,requireOperation)"]:self.assertIn(token,runtime)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        for token in ["diagnoseOriginalTargetMemoryBufferInterval", "diagnoseOriginalRuntimeProfileBufferIntervalImpl<true>",
                      "OriginalTargetMemoryIntervalContract::lowering(backend())", "OriginalTargetMemoryIntervalContract::validateType(type,request.contractVersion)",
                      "if constexpr(!targetMemory)if(facts.target.hasRuntimeArray)"]:self.assertIn(token,device)
        smoke=CppSource((self.ROOT / "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp").read_text(encoding="utf-8-sig"))
        self.assertIn("RHI-ORIGINAL-TARGET-INTERVAL-NULL-VALIDATOR-ONLY",smoke)

    def test_typed_issuer_preparation_preserves_original_issuer_authority(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        prepare=source[source.index("std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation> WebGpuCanonicalCallbackCustody::prepareTypedIssuers("):source.index("const char* WebGpuCanonicalCallbackCustody::validateTypedIssuers(")]
        for token in ["decision.preparationAvailable", "inspectTypedIssuers", "state.issuersClaimed", "allocateTyped(command.command_", "allocateTyped(recording", "WebGpuNativeCompletionReceipt::prepare", "receipt.matches(command.device_", "S::lossIndex", "Scope", "Queue", "receipts[i].release()"]:
            self.assertIn(token,prepare)
        self.assertLess(prepare.index("auto result=Wrapper::shareOriginal"),prepare.index("WebGpuNativeCompletionReceipt::prepare"))
        self.assertLess(prepare.index("receipts[i].release()"),prepare.index("state.issuersClaimed=true"))
        for token in ["core::makeUnique<S::Slot>", "CompletionRetentionPool", "prepareWebGpuNativeDeviceLossRegistration", "wgpuQueueSubmit", "applyOriginalPlan", "gate.arm()", "WebGpuNativeCompletionReceipt::bind"]:
            self.assertNotIn(token,prepare)
        for issuer in ["Recording", "Submission"]:
            for mode,tag,typ in [("legacy", "!typedCanonicalMode_ && !rootCanonicalMode_", "WebGpuCanonicalCallbackCustody::State"), ("typed", "typedCanonicalMode_", "TypedState"), ("root", "rootCanonicalMode_", "RootState")]:
                accessor=source.split("WebGpuOrdinary"+issuer+"::"+mode+"CanonicalState() const noexcept",1)[1].split("}",1)[0]
                self.assertIn(tag+" ? std::static_pointer_cast<"+typ+">(canonicalState_.lock()) : nullptr",accessor)
        header=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h")
        public=header[header.index("public:\n    using Request"):header.index("private:",header.index("public:\n    using Request"))]
        for token in ["State&", "WebGpuRecordedCommandProof", "nativeDevice", "nativeQueue", "record(", "submit(", "deliver("]:
            self.assertNotIn(token,public)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_issuer_preparation_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["captureStart(3)", "captureEnd(3u,true)", "exact->issuersClosedForInspection()", "measured-1u", "arpTypedIssuerLossStates.length===21", "userdata-cleared", "native-binding-mutation5"]:
            self.assertIn(token,fixture)
        for token in ["maySubmit=", "applyOriginalPrototypePlan", "deliverNativeLoss", "completeTyped("]:
            self.assertNotIn(token,fixture)
        smoke=CppSource((self.ROOT / "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp").read_text(encoding="utf-8-sig"))
        self.assertIn("OriginalTypedIssuerPreparationFixture::verifyContract()",smoke)
        custody_fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_callback_custody_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("custody=live->custody;assert(custody && live->coldIssuers",custody_fixture)
        self.assertIn("live->submission->liveBytes()==issuerCancelledBytes[1]",custody_fixture)
        validate=source.split("const char* WebGpuCanonicalCallbackCustody::validateTypedIssuers(",1)[1].split("bool WebGpuCanonicalCallbackCustody::typedIssuersClosed",1)[0]
        self.assertLess(validate.index("sharedOwnerMatches(state)"),validate.index("matchesPrepared"))
        self.assertLess(validate.index("matchesPrepared"),validate.index("inspectTypedIssuers("))
        self.assertIn("if(!state->slots[i] ||",validate)
        closed=source.split("bool WebGpuCanonicalCallbackCustody::typedIssuersClosed",1)[1].split("std::shared_ptr<const WebGpuOriginalTypedIssuerPreparation>",1)[0]
        self.assertIn("if(validateTypedIssuers(original))return false;",closed)

    def test_typed_callback_custody_reuses_original_loss_and_single_pool(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        typed=source[source.index("const char* WebGpuCanonicalCallbackCustody::inspectTypedOriginal("):source.index("WebGpuCanonicalCopyPassLowering::prepareOriginal",source.index("const char* WebGpuCanonicalCallbackCustody::inspectTypedOriginal("))]
        for token in ["dynamic_pointer_cast<const WebGpuOriginalObservedNativeLoss>", "typedColdObserverPreparationAvailable", "observerPreparationAvailable", "observers_.attach", "std::shared_ptr<const void>(state,&loss)", "typedCallbackCustodyClaimed_=true", "observers_.inspect(state->observerTicket)", "core::makeShared<S>", "core::makeUnique<S::Graph>", "core::makeUnique<S::Slot>"]:
            self.assertIn(token,typed)
        self.assertLess(typed.index("core::makeUnique<Wrapper>"),typed.index("observers_.attach"))
        self.assertLess(typed.index("observers_.attach"),typed.index("typedCallbackCustodyClaimed_=true"))
        validation=typed[typed.index("const char* WebGpuCanonicalCallbackCustody::validateTypedOriginal("):]
        self.assertNotIn("std::string_view(id)",validation)
        self.assertNotIn("OBSERVER-CAPACITY",validation)
        for token in ["prepareWebGpuNativeDeviceLossRegistration", "wgpuQueueSubmit", "applyOriginalPrototypePlan", "prepareOriginalLowering", "WebGpuRecordedCommandProof"]:
            self.assertNotIn(token,typed)
        state=source[source.index("class WebGpuCanonicalCallbackCustody::RuntimeState"):source.index("WebGpuCanonicalCallbackCustody::WebGpuCanonicalCallbackCustody")]
        self.assertEqual(state.count("Pool pool;"),1)
        self.assertIn("observers_.suppress(this->observerTicket",state)
        self.assertIn("finishDeferredTerminal()",state)
        receiver=state[state.index("static void consumeOriginalPhysicalLoss"):state.index("static void consumeNativeLoss")]
        self.assertIn("terminal!=rhi::OriginalDeviceNativeLossTerminal::NativeLost",receiver)
        header=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_callback_custody.h")
        public=header[header.index("public:\n    using Request"):header.index("private:",header.index("public:\n    using Request"))]
        for token in ["State&", "nativeDevice", "nativeQueue", "attach(", "deliver(", "record(", "submit("]:
            self.assertNotIn(token,public)

    def test_canonical_callback_custody_retains_exact_cold_graph_and_native_loss(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        boundary=source[source.index("class WebGpuCanonicalCallbackCustody::RuntimeState"):source.index("std::shared_ptr<WebGpuOrdinaryRecording> WebGpuOrdinaryRecording::allocateCanonical")]
        for token in ["CompletionRetentionPool<Graph>", "ExternalCallbackGate", "core::makeShared<State>",
                      "core::makeUnique<State::Graph>(command)", "core::CpuMemoryQuotaScope submissionScope(admission.originalSubmissionQuota())",
                      "core::CpuMemoryQuotaScope callbackScope(admission.originalCallbackQuota())", "core::makeUnique<State::Slot>",
                      "prepareWebGpuNativeDeviceLossRegistration(command->device_", "detachWebGpuNativeDeviceLossRegistration(loss.nativeState)",
                      "loss.owner=state;command->callbackCustodyClaimed_=true", "owner->pool.invalidateAndRetain",
                      "RHI-WEBGPU-CANONICAL-CALLBACK-NATIVE-DETACH", "loss.owner.reset()", "State::roleAt(i)", "pool.canCancel(ticket)"]:
            self.assertIn(token,boundary)
        canonical_prepare=boundary.split("WebGpuCanonicalCallbackCustody::prepareOriginal(",1)[1].split(
            "const char* WebGpuCanonicalCallbackCustody::validate()",1)[0]
        self.assertLess(canonical_prepare.index("auto result=shareOriginal"),canonical_prepare.index("loss.nativeState=prepareWebGpuNativeDeviceLossRegistration"))
        self.assertLess(boundary[boundary.index("const char* cancel("):].index("pool.canCancel(ticket)"),boundary[boundary.index("const char* cancel("):].index("detachWebGpuNativeDeviceLossRegistration(loss.nativeState)"))
        legacy_detach=boundary[boundary.index("} else {",boundary.index("bool detachLoss()")):boundary.index("void releaseTerminalIfDrained()") ]
        self.assertLess(legacy_detach.index("detachWebGpuNativeDeviceLossRegistration(loss.nativeState)"),legacy_detach.index("loss.gate.detach()"))
        loss=boundary[boundary.index("static void consumeNativeLoss"):boundary.index("const char* cancel(")]
        self.assertIn("invalidateOriginalOwnership(*graph.command)",loss)
        self.assertNotIn("lowering.reset()",loss)
        self.assertNotIn("pool.invalidate()",loss)
        self.assertLess(loss.index("slot->gate.finishDelivery()"),loss.index("owner->releaseTerminalIfDrained()"))
        cancel=boundary[boundary.index("const char* cancel("):boundary.index("const std::shared_ptr<core::CpuMemoryQuota> submissionQuota")]
        self.assertLess(cancel.index("CANCEL-CALLBACKS-PENDING"),cancel.index("detachWebGpuNativeDeviceLossRegistration"))
        for token in ["ordinaryOriginalStateTransaction", "ordinaryOriginalTextureTransaction", "signalFromHost",
                      "wgpuQueueSubmit", "wgpuDeviceCreateCommandEncoder", "std::make_shared", "std::vector", "applyOriginalPrototypePlan"]:
            self.assertNotIn(token,boundary)
        wrapper=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_callback_custody.h")
        public=wrapper[wrapper.index("public:\n    using Budget"):wrapper.index("private:",wrapper.index("public:\n    using Budget"))]
        for token in ["nativeDevice()", "nativeQueue()", "WebGpuRecordedCommandProof", "deliver(", "submit(", "record(", "State&"]:
            self.assertNotIn(token,public)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording", "WebGpuOrdinarySubmission"])
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_callback_custody")
        for token in ["case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU", "additionalOwnerCount=11u", "b.maxCallbackSlots!=8u"]:
            self.assertIn(token,contract)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalCallbackCustodyPassed",runner)
        self.assertIn("canonicalCallbackCustodyPassed: canonicalCallbackCustodyPassed",runner)

    def test_canonical_cold_command_keeps_opaque_native_payload_and_seal_claim(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for token in ["prepareCanonical", "inspectCanonicalColdCommand", "nativeColdCommandClaimed_", "core::CpuMemoryQuotaScope scope(admission->originalRoot()->originalMetadataQuota())", "command->impl_->canonicalColdAdmission=admission", "admission->originalClosure()->nativeDevice()", "WebGpuCanonicalColdCommand::shareOriginal", "validateCanonical(*result,false)", "state.encoder || state.commandBuffer || state.renderPass || state.computePass", "state.canonicalColdAdmission) {diagnostic=", "state.recordingIdentity!=identity", "state.queue!=queue", "state.device!=device", "RECORDING-UNPROVEN"]:self.assertIn(token,source)
        body=source[source.index("WebGpuNativeCommandPreparation::prepareCanonical"):source.index("WebGpuNativeCommandPreparation::validateCanonical")]
        self.assertLess(body.index("validateCanonical(*result,false)"),body.index("seal->nativeColdCommandClaimed_=true"))
        for token in ["wgpuQueueSubmit", "wgpuDeviceCreateCommandEncoder", "CpuReplay", "MutationJournal"]:self.assertNotIn(token,body)
        wrapper=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_cold_command.h")
        private=wrapper[wrapper.index("private:"):]
        self.assertIn("const std::shared_ptr<WebGpuNativeCommandPayload> command_",private)
        for token in ["nativeQueue()", "nativeDevice()", "WebGpuRecordedCommandProof", "submit(", "record(", "open("]:self.assertNotIn(token,wrapper)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_cold_command")
        for token in ["case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU", "additionalOwnerCount=3u", "recordingAvailable=false,executionAvailable=false"]:self.assertIn(token,contract)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_cold_command_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifySingleUse", "retainedOwnerCount()==51u", "COMMAND-DUPLICATE", "verifyActualLoss", "NATIVE-LOST", "originalReservedCallbackTicketCount()==8u"]:self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalColdCommandPassed",runner)
        self.assertIn("canonicalColdCommandPassed: canonicalColdCommandPassed",runner)

    def test_canonical_cold_admission_derives_exact_Source_request_and_owner_union(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_cold_admission.h")
        for token in ["WebGpuErrorScopeContract::count", "OriginalDeviceNativeLossContract::callbackCount", "OriginalDeviceNativeLossContract::registrationCount", "compareCapacity(required,reservation->originalRequest())", "closure->nativeLossDiagnostic()", "compareOriginalRoot(root)", "core::CpuMemoryQuotaScope scope(closure->originalRoot()->originalMetadataQuota())", "core::makeUnique", "shareOriginal", "maxRecordingOwners", "maxSubmissionOwners", "maxCallbackOwners", "nativeExecutionProven() const noexcept {return false;}"]:self.assertIn(token,source)
        for token in ["WebGpuRecordedCommandProof", "wgpuQueueSubmit", "MutationJournal", "core::makeShared<core::AtomicCapacityBudget>", "std::vector", "const_cast", "WebGpuOriginalResourceStates"]:self.assertNotIn(token,source)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_cold")
        for token in ["existingDeviceLossCallbacks", "runtimeLossRegistrations", "RHI-ORIGINAL-NATIVE-COLD-REQUIREMENTS-CAPACITY", "callbacks>UINT32_MAX", "arp::rhi::semantics::OriginalCommandNativeLifecycleLowering::lowering(backend)"]:self.assertIn(token,contract)
        scopes=self.read_validation_section("src/rhi/semantics/webgpu_error_scope.h","webgpu_error_scope")
        self.assertIn("Filter::Internal,Filter::OutOfMemory,Filter::Validation",scopes)
        self.assertNotIn("webgpu_native_payloads.h",scopes)
        for file in ["webgpu_native_error_scope_batch.h", "webgpu_native_ordinary_shader_validation.h"]:
            self.assertIn("WebGpuErrorScopeContract::count",self.read_webgpu_source(file))
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_cold_admission_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "retainedOwnerCount()==48u", "callbackTickets==8u", "callbackRegistrations==8u", "forgedRequirements", "verifyHotNoAllocation", "originalReservedCallbackTicketCount()==8u"]:self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalColdAdmissionPassed",runner)
        self.assertIn("canonicalColdAdmissionPassed: canonicalColdAdmissionPassed",runner)

    def test_canonical_loss_bound_closure_retains_exact_subscription_before_registry(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["prepareOriginalLossBound", "device.validateOriginalDeviceNativeLoss(loss)", "std::optional<rhi::OriginalDeviceNativeLossObservation> loss_", "loss_->diagnostic()", "loss.has_value()", "loss_.has_value()", "additional=1u+(lossBound ? 1u : 0u)", "RHI-WEBGPU-CANONICAL-CLOSURE-LOSS-UNBOUND"]:self.assertIn(token,source)
        body=source[source.index("prepareOriginalLossBound"):source.index("private:",source.index("prepareOriginalLossBound"))]
        self.assertLess(body.index("validateOriginalDeviceNativeLoss"),body.index("prepareOriginalImpl"))
        recipe=CppSource((self.ROOT / "tests/fixtures/original_command_access_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertLess(recipe.index("prepareOriginalDeviceNativeLoss"),recipe.index("prepareOriginalResourceRegistration"))
        self.assertIn("withNativeLossOriginalPlan",recipe)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_loss_bound_closure_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "retainedOwnerCount()==43u", "OBSERVATION-FOREIGN", "REGISTRY-ALREADY-PREPARED", "verifyHotNoAllocation", "context.nativeLossObservation.reset()"]:self.assertIn(token,fixture)
        loss=CppSource((self.ROOT / "tests/fixtures/webgpu_original_l2_device_loss_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("prepareOriginalLossBound(live.transaction",loss)
        self.assertIn("same-root-owner18",loss)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalLossBoundClosurePassed",runner)
        self.assertIn("canonicalLossBoundClosurePassed: canonicalLossBoundClosurePassed",runner)

    def test_canonical_native_closure_retains_exact_root_without_second_state_graph(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["Root::validateOriginal(root)", "Status::Prepared", "hasOriginalPrototypePlan", "Sources::validateRegisteredLifetime", "authored.maxNativePayloadObjects", "core::CpuMemoryQuotaScope scope(root->originalMetadataQuota())", "core::SharedAllocationProjection::inspect", "core::SharedIdentityProjection::inspect", "entry.registration.generation()!=entry.generation", "matchesParent", "observeOriginalWebGpuResource", "validateOriginalWebGpuResource", "mappedDiagnostic", "nativeExecutionProven() const noexcept {return false;}"]:self.assertIn(token,source)
        for forbidden in ["MutationJournal", "WebGpuOriginalResourceStates", "WebGpuOriginalStateTransaction", "recordState", "recordedState().observe", "WebGpuRecordedCommandProof", "std::vector", "const_cast"]:self.assertNotIn(forbidden,source)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_closure")
        for token in ["case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU", "recordingIssuerAvailable=false,executionAvailable=false", "CPU commit is never an event", "quarantines root/native owners"]:self.assertIn(token,contract)
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_backend_objects.cpp")
        for token in ["registry->observeTransferResources(&id_,1u,&observation)", "registry->observeOrdinaryResources(&id_,1u,&observation)", "registry->validateTransferResources(&observation,1u)"]:self.assertIn(token,backend)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalNativeClosurePassed",runner)
        self.assertIn("canonicalNativeClosurePassed: canonicalNativeClosurePassed",runner)

    def test_original_plan_uses_same_root_quota_journal_and_sealed_unique_finals(self):
        source=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        for token in ["std::optional<Plan> plan_", "core::BoundedSequence<FinalCell>", "core::BoundedSequence<AccessStep>", "core::BoundedSequence<Transition>", "core::CpuMemoryQuotaScope scope(originalMetadataQuota())", "plan.cells.findIf", "resolveOriginalTextureCell", "canWriteBatch(journal_,writesTo", "validatePlannedFinals", "matchesPending(*cell.texturePending,journal_)", "matches(*cell.scalarCommitted)", "RHI-ORIGINAL-PLAN-UNIFORM-ASPECT-UNPROVEN", "applyOriginalPrototypePlan"]:self.assertIn(token,source)
        cold=source[source.index("const char* materializeOriginalPrototypePlan"):source.index("const char* applyOriginalPrototypePlan")]
        self.assertLess(cold.index("preflightPlanWrites()"),cold.index("Plan plan("))
        hot=source[source.index("const char* applyOriginalPlan()"):source.index("const char* commitOriginalOwnership()")]
        self.assertLess(hot.index("preflightPlanWrites()"),hot.index("control->prepare(journal_)"))
        self.assertIn("normalized->write(b.textureSubresources,b.before,b.after,&journal_)",hot)
        for forbidden in ["core::make", "reserve(", "push_back", "resize(", "const_cast", "wgpu"]:self.assertNotIn(forbidden,hot)
        self.assertLess(source.index("std::optional<Plan> plan_"),source.index("core::MutationJournal journal_"))
        contract=self.read_validation_section("src/rhi/frontend/command_model.h","original_command_state_plan")
        self.assertNotIn("lowering(Backend",contract)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalPlanPassed",runner)
        self.assertIn("originalPlanPassed: originalPlanPassed",runner)
        self.assertNotIn("case Backend::",contract)

    def test_original_transaction_count_reuses_original_source_alias_and_private_mutable_binding(self):
        source=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        for token in ["friend class OriginalCommandStateTransaction", "mutable core::AtomicCapacityBudget stateTransactions_", "stateTransactions_(budget.maxStateTransactions)", "originalStateTransactionCount()", "stateTransactions_.capacity()!=budget_.maxStateTransactions"]:self.assertIn(token,source)
        resource=self.read_source("src/rhi/gpu_resources.h")
        self.assertIn("friend class OriginalCommandStateTransaction",resource)
        self.assertIn("bindOriginalRecordedStateSource(Source& source)",resource)
        self.assertNotIn("const_cast",resource)
        payload=self.read_source("src/rhi/backends/webgpu/webgpu_native_payloads.h")
        for token in ["friend class rhi::GpuBuffer", "friend class rhi::GpuTexture", "originalRecordedStateForBinding()"]:self.assertIn(token,payload)
        self.assertNotIn("auto& recordedState() noexcept",payload)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalTransactionOwnershipPassed",runner)
        self.assertIn("originalTransactionOwnershipPassed: originalTransactionOwnershipPassed",runner)

    def test_core_pending_observation_seals_nonrecycling_enlistment_epoch(self):
        source=CppSource((self.ROOT / "src/core/versioned_mutation_value.h").read_text(encoding="utf-8-sig"))
        for token in ["MonotonicIdSequence enlistments_", "enlistments_.tryAcquire()", "input.enlistmentEpoch_==enlistmentEpoch_", "if(snapshot_.pending())return true", "enlistmentEpoch_=epoch", "WriteResult::EnlistmentExhausted"]:self.assertIn(token,source)
        self.assertLess(source.index("if(journal && enlistmentExhausted())"),source.index("const auto next=sequence_.tryAcquire()"))
        for forbidden in ["std::vector", "core::make", "new ", "push_back", "wgpu"]:self.assertNotIn(forbidden,source)
        array=CppSource((self.ROOT / "src/core/versioned_mutation_array.h").read_text(encoding="utf-8-sig"))
        self.assertIn("stamp_(false,firstRevision,firstEnlistmentEpoch)",array)
        self.assertNotIn("MonotonicIdSequence",array)
        for fixture in ["versioned_mutation_value_fixture.h", "versioned_mutation_array_fixture.h", "original_texture_state_fixture.h"]:
            self.assertIn("matchesPending",CppSource((self.ROOT / "tests/fixtures" / fixture).read_text(encoding="utf-8-sig")))

    def test_pending_enlistment_exhaustion_has_stable_rhi_diagnostics_and_mandatory_browser_proof(self):
        for name,diagnostic in [("recorded_resource_state_control.h","RHI-RESOURCE-STATE-ENLISTMENT-EXHAUSTED"),("recorded_texture_state_control.h","RHI-RESOURCE-STATE-ENLISTMENT-EXHAUSTED"),("query_slot_declaration_control.h","RHI-QUERY-SLOT-ENLISTMENT-EXHAUSTED")]:
            self.assertIn(diagnostic,self.read_source(*self.CURRENT_CONTROL_SOURCES[name]))
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& pendingEnlistmentEpochPassed",runner)
        self.assertIn("pendingEnlistmentEpochPassed: pendingEnlistmentEpochPassed",runner)

    def test_original_registered_state_domain_is_private_one_shot_embedded_and_terminal(self):
        domain=self.read_source("src/rhi/frontend/resource_recorded_state_domain.h")
        for token in ["friend class rhi::GpuDevice", "mutable std::optional<OriginalDeviceLifetime::Ticket>", "RHI-ORIGINAL-RESOURCE-STATE-DOMAIN-ALREADY-BOUND", "ticket.diagnostic()"]:self.assertIn(token,domain)
        self.assertLess(domain.index("private:"),domain.index("const char* bind("))
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        self.assertLess(device.index("record->validate()"),device.index("bindOriginalStateDomain(object"))
        self.assertLess(device.index("bindOriginalStateDomain(object"),device.index("object.originalRegistration_.record_=std::move(record)"))
        for token in ["RHI-ORIGINAL-RESOURCE-STATE-DOMAIN-PENDING", "RHI-ORIGINAL-RESOURCE-STATE-DOMAIN-INACTIVE", "if(!control)return nullptr", "control->domain_.bind(ticket)"]:self.assertIn(token,device)
        for name in ["recorded_resource_state_control.h", "recorded_texture_state_control.h"]:
            control=self.read_source(*self.CURRENT_CONTROL_SOURCES[name])
            for token in ["OriginalRecordedStateDomain domain_", "originalDomainDiagnostic()", "!domain_.diagnostic()", "if(const auto* id=domain_.diagnostic())return id", "observePending", "matchesPending"]:self.assertIn(token,control)
        for source in [domain]:
            for forbidden in ["std::vector", "core::make", "push_back", "wgpu", "callback", "invalidate()"]:self.assertNotIn(forbidden,source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalStateDomainPassed",runner)
        self.assertIn("originalStateDomainPassed: originalStateDomainPassed",runner)

    def test_normalized_pending_observation_reuses_exact_sealed_core_journal_stamp(self):
        array=CppSource((self.ROOT / "src/core/versioned_mutation_array.h").read_text(encoding="utf-8-sig"))
        for token in ["using PendingObservation=typename Stamp::PendingObservation", "snapshot_.ownedBy(journal)", "stamp_.observePending(journal)", "stamp_.matchesPending(observation,journal)"]:self.assertIn(token,array)
        texture=self.read_source("src/rhi/frontend/resource_texture_state_control.h")
        for token in ["class PendingObservation final", "friend class RecordedTextureStateControl", "Values::PendingObservation stamp_", "values_.observePending(journal)", "values_.matchesPending(observation.stamp_,journal)", "uniformState(observation.range_)==observation.state_"]:self.assertIn(token,texture)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_original_l2_device_loss_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["!control.active()", "!control.prepare(foreign)", "!view.matchesPending(*pending,live.journal)", "closed(buffer,live.bufferPending)", "live.journal.rollback()", "BINDLESS_RHI_ORIGINAL_STATE_DOMAIN_NATIVE_OK"]:self.assertIn(token,fixture)

    def test_original_resource_states_bind_exact_input_ranges_owner_union_and_keep_execution_closed(self):
        source=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_resource_states")
        for token in ["OriginalCommandInputs::validateOriginal", "validateRegisteredLifetime", "registeredOwnerUnion", "device.originalDeviceLifetimeTicket()", "originalCommittedStateDiagnostic(point.range)", "observeOriginalCommittedState(point.range)", "validateOriginalCommittedState(entry.observation)", "entry.point.matches(point)", "sameOwner(inputs_,inputs)", "maxTextureRanges", "maxStateObservations", "RHI-ORIGINAL-RESOURCE-STATES-QUOTA-DOMAIN", "core::BoundedSequence", "core::makeUnique", "nativeExecutionProven() const noexcept {return false;}", "arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)"]:
            self.assertIn(token,source)
        projection=self.read_source("src/rhi/frontend/command_texture_ranges.h")
        for token in ["copy->sourceRange()", "copy->destinationRange()", "(*snapshot)->scalarFields()", "fields.textureSubresources", "layout.baseMipLevel", "upload->originalInput()->scalarFields().range()", "readback->originalInput()->scalarFields().range()", "TextureAspect::Stencil", "RHI-ORIGINAL-TEXTURE-RANGES-OPERAND-UNSUPPORTED"]:
            self.assertIn(token,projection)
        for source in [source,projection]:
            for forbidden in ["backends/", "wgpu", "std::vector", "std::make_shared", "signalFromHost", "dynamic_cast"]:self.assertNotIn(forbidden,source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalResourceStatesPassed",runner)
        self.assertIn("originalResourceStatesPassed: originalResourceStatesPassed",runner)
        for fixture in ["upload", "readback"]:
            self.assertIn("OriginalCommandTextureTransferStatesFixture::verify",CppSource((self.ROOT / ("tests/fixtures/original_command_texture_"+fixture+"_inputs_fixture.h")).read_text(encoding="utf-8-sig")))

    def test_original_texture_controls_are_creation_bound_normalized_or_native_uniform_and_fail_closed(self):
        resource=self.read_source("src/rhi/gpu_resources.h")
        for token in ["class CommittedStateObservation final", "originalNormalizedState_->geometry().matches(desc())", "originalNormalizedState_->committedDiagnostic(range)", "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM", "RHI-ORIGINAL-TEXTURE-STATE-PARTIAL-CONTROL-UNPROVEN", "observation.normalized_!=originalNormalizedState_", "observation.uniform_!=originalUniformState_", "OriginalRegisteredResourceKind::Texture"]:
            if token=="RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM":continue
            self.assertIn(token,resource)
        control=self.read_source("src/rhi/frontend/resource_texture_state_control.h")
        for token in ["core::VersionedMutationArray<ResourceState>", "geometry_(requireGeometry(desc))", "uniformState(range)", "RHI-ORIGINAL-TEXTURE-STATE-NONUNIFORM", "values_.writeIndices", "isTextureResourceState"]:self.assertIn(token,control)
        for backend,name,cls in [("null","null_resources.cpp","NullTexture"),("vulkan","vulkan_resources.cpp","VulkanRhiTexture"),("metal","metal_backend_objects.cpp","MetalRhiTexture")]:
            source=CppSource((self.ROOT / "src/rhi/backends" / backend / name).read_text(encoding="utf-8-sig"));start=source.index("class "+cls+" final");body=source[start:source.index("\nclass ",start+6)]
            self.assertIn("RecordedTextureStateControl state_",body)
            self.assertIn("bindOriginalRecordedState(state_)",body)
            self.assertIn("state_.write(range,before,after,journal)",body)
            for token in ["MutationSnapshot", "subresourceStates_", "push_back", "kMaxSubresourceStates"]:self.assertNotIn(token,body)
        web=self.read_source("src/rhi/backends/webgpu/webgpu_backend_objects.cpp")
        self.assertIn("if(nativePayload_ && nativePayload_->texture && nativePayload_->ownerDevice)",web)
        array=CppSource((self.ROOT / "src/core/versioned_mutation_array.h").read_text(encoding="utf-8-sig"))
        for token in ["snapshot_(values_)", "stamp_.tryWrite(false,journal)", "indexAt(i)>=size()", "pmr::Vector<T>", "snapshot_.prepare(*journal)"]:self.assertIn(token,array)
        for token in ["new ", "std::make_shared", "std::vector", "resize(", "push_back("]:self.assertNotIn(token,array)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalTextureControlPassed",runner)
        self.assertIn("originalTextureControlPassed: originalTextureControlPassed",runner)

    def test_original_texture_range_normalizes_explicit_geometry_without_original_state_authority(self):
        source=self.read_source("src/rhi/frontend/resource_texture_state_range.h")
        for token in ["TextureResourceLayout::capture(desc)", "bytesPerBlock(desc.format)", "core::RowMajorIndexGrid", "containsUnsignedRange", "textureAspectMatchesFormat", "grid.size()*planes", "range.aspect==TextureAspect::Stencil", "originalRangeIssuerAvailable() const noexcept {return false;}", "arp::rhi::semantics::OriginalCommandStateAdmissionLowering::lowering(backend)"]:self.assertIn(token,source)
        for token in ["backends/", "new ", "std::make_shared", "WebGpuRecordedCommandProof", "RecordedResourceStateControl", "MutationJournal", "initialState="]:self.assertNotIn(token,source)
        grid=CppSource((self.ROOT / "src/core/row_major_index_grid.h").read_text(encoding="utf-8-sig"))
        self.assertIn("rows<=maxElements/columns",grid)
        self.assertIn("row*columns_+column",grid)
        self.assertIn("index/columns_",grid)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalTextureStateRangePassed",runner)
        self.assertIn("originalTextureStateRangePassed: originalTextureStateRangePassed",runner)

    def test_original_buffer_graph_retains_exact_inputs_and_counts_registered_owner_union(self):
        source=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_buffer_states")
        for token in ["ConstructionKey()=default", "OriginalCommandInputs::validateOriginal", "validateRegisteredLifetime(sources,device)", "observeOriginalCommittedState", "validateOriginalCommittedState(entry.observation)", "sameOwner(inputs_,inputs)", "device.validateOriginalDeviceLifetime(device_)", "core::BoundedSequence<Entry>", "registeredOwnerUnion", "RHI-ORIGINAL-BUFFER-STATES-SOURCE-UNSUPPORTED"]:self.assertIn(token,source)
        for token in ["backends/", "new ", "std::make_shared", "WebGpuRecordedCommandProof", "RecordedResourceStateControl(", "signalFromHost"]:self.assertNotIn(token,source)
        table=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        for token in ["registeredOwnerUnion", "SharedIdentityProjection::inspect", "fixedOwnerCount", "owners=cpuOwners+fixed+registrations", "RHI-ORIGINAL-STATE-REGISTRATION-CAPACITY"]:self.assertIn(token,table)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalBufferStatesPassed",runner)
        self.assertIn("originalBufferStatesPassed: originalBufferStatesPassed",runner)

    def test_original_buffer_state_observation_binds_registered_original_controls_and_rejects_pending(self):
        resource=self.read_source("src/rhi/gpu_resources.h")
        for token in ["class CommittedStateObservation final", "originalCommittedStateDiagnostic", "observeOriginalCommittedState", "validateOriginalCommittedState", "bindOriginalRecordedState", "observation.control_!=originalStateControl_", "control_->pending()", "control_->matches(observed_)", "generation_(registration_.generation())"]:self.assertIn(token,resource)
        self.assertNotIn("enum class ResourceState",resource)
        self.assertIn("enum class ResourceState",self.read_source("src/rhi/gpu_common.h"))
        for backend,name,cls in [("null","null_resources.cpp","NullBuffer"),("vulkan","vulkan_resources.cpp","VulkanRhiBuffer"),("metal","metal_backend_objects.cpp","MetalRhiBuffer")]:
            source=CppSource((self.ROOT / "src/rhi/backends" / backend / name).read_text(encoding="utf-8-sig"));start=source.index("class "+cls+" final");body=source[start:source.index("\nclass ",start+6)]
            self.assertIn("RecordedResourceStateControl state_",body)
            self.assertIn("bindOriginalRecordedState(state_)",body)
            self.assertIn("state_.writeBuffer",body)
            self.assertNotIn("MutationSnapshot",body)
        web=self.read_source("src/rhi/backends/webgpu/webgpu_backend_objects.cpp")
        self.assertIn("if(nativePayload_ && nativePayload_->buffer && nativePayload_->ownerDevice)",web)
        self.assertIn("bindOriginalRecordedStateSource(*nativePayload_)",web)
        self.assertNotIn("bindOriginalRecordedState(metadataState_)",web)
        control=self.read_source("src/rhi/frontend/resource_state_control.h")
        self.assertIn("value_.prepare(journal)",control)
        self.assertIn("isBufferResourceState(after)",control)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalBufferStatePassed",runner)
        self.assertIn("originalBufferStatePassed: originalBufferStatePassed",runner)

    def test_original_resource_registrations_are_issued_by_actual_factories_and_bind_canonical_sources(self):
        source=self.read_source("src/rhi/frontend/resource_registration.h")
        for token in ["ConstructionKey()=default", "friend class GpuDevice", "std::weak_ptr<OriginalResourceRegistration>", "core::AtomicCapacityLease", "core::MonotonicIdSequence", "maxBuffers", "maxTextures", "maxTextureViews", "perResourceOwnerCount=2u", "originalStateIssuerAvailable=false", "nativeLossObserved=false,executionAvailable=false"]:self.assertIn(token,source)
        for token in ["RecordedResourceStateControl", "ResourceState", "initialState", "backends/", "std::make_shared", "WebGpuRecordedCommandProof", "signalFromHost"]:self.assertNotIn(token,source)
        for backend,filename in [("null","null_backend.cpp"),("vulkan","vulkan_device.cpp"),("metal","metal_device.cpp"),("webgpu","webgpu_device.cpp")]:
            factory=CppSource((self.ROOT / "src/rhi/backends" / backend / filename).read_text(encoding="utf-8-sig"))
            self.assertIn("completeOriginalResourceCreation",factory)
            self.assertIn(",&texture)",factory)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        for token in ["originalRegistry_->generations_.tryAcquire()", "core::makeShared<OriginalResourceRegistration>", "object.originalRegistration_.record_=std::move(record)", "parentRecord->object_!=parent", "object.textureId()!=parent->id()"]:self.assertIn(token,device)
        table=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        for token in ["validateRegisteredLifetime", "device.originalResourceRegistrationDiagnostic()", "matchesParent", "value->originalSource()->originalObject()", "value->originalDestinationLease()->originalObject()"]:self.assertIn(token,table)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalResourceRegistrationPassed",runner)
        self.assertIn("originalResourceRegistrationPassed: originalResourceRegistrationPassed",runner)

    def test_original_device_lifetime_is_sealed_weak_and_grants_no_resource_authority(self):
        source=self.read_source("src/rhi/frontend/resource_device_lifetime.h")
        for token in ["friend class GpuDevice", "ConstructionKey()=default", "std::weak_ptr<OriginalDeviceLifetime>", "core::LifetimeEpoch", "std::atomic<Terminal>", "terminal_.compare_exchange_strong", "quota_->limitBytes()!=budget_.maxCpuMetadataBytes", "nativeExecutionProven() const noexcept {return false;}"]:self.assertIn(token,source)
        receiver=source.split("class NativeLossReceiver",1)[1].split("class Ticket",1)[0]
        self.assertNotIn("epoch_",receiver)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        for token in ["GpuDevice(const GpuDevice&)=delete", "GpuDevice(GpuDevice&&)=delete", "prepareOriginalDeviceLifetime", "core::CpuMemoryQuotaScope", "core::makeShared<OriginalDeviceLifetime>", "domain.get()!=originalLifetime_.get()", "OriginalDeviceLifetime::Terminal::Shutdown", "retireOriginalDeviceLifetime"]:self.assertIn(token,device)
        for token in ["backends/", "wgpu", "std::make_shared", "new ", "RecordedResourceStateControl", "WebGpuRecordedCommandProof", "signalFromHost"]:self.assertNotIn(token,source)
        contract=self.read_validation_section("src/rhi/frontend/resource_device_validation.h","original_device_lifetime")
        self.assertIn("resourceIssuerAvailable=false,nativeLossObserved=false,executionAvailable=false",contract)
        self.assertIn("maxRetainedOwners<retainedOwnerCount",contract)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalDeviceLifetimePassed",runner)
        self.assertIn("originalDeviceLifetimePassed: originalDeviceLifetimePassed",runner)

    def test_generic_original_readback_adapters_retain_existing_lease_request_and_destination(self):
        texture=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_readback_texture_source")
        for token in ["TextureReadbackSourceLease::validateOriginal", "lease_->sourceTexture()", "request->originalMetadataQuota()", "layout->originalMetadataQuota()", "input->labelOwner()->originalMetadataQuota()", "retainedOwnerCount", "nativeExecutionProven() const noexcept {return false;}"]:self.assertIn(token,texture)
        readback=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_texture_readback_source")
        for token in ["OriginalCommandReadbackTextureSource::validateOriginal", "OriginalCommandBufferLease::validateOriginal", "desc.destinationReadbackBuffer!=destination->resource()", "containsUnsignedRange", "GpuResourceId resource() const noexcept {return {};}"]:self.assertIn(token,readback)
        for source in [texture,readback]:
            for token in ["backends/", "wgpu", "std::make_shared", "WebGpuRecordedCommandProof", "signalFromHost", "core::UniquePtr<GpuTexture>", "ByteBuffer"]:self.assertNotIn(token,source)
        table=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        for token in ["RHI-ORIGINAL-SOURCES-READBACK-SOURCE", "RHI-ORIGINAL-SOURCES-READBACK-DESTINATION", "Lease::domain==CommandReferenceDomain::Readback"]:self.assertIn(token,table)
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        for token in ["sameOwner(arguments->originalInput(),(*source)->originalInput())", "sameOwner(*texture,(*source)->originalSource())", "maxUnpublishedResultBytes-resultBytes", "RHI-ORIGINAL-INPUTS-READBACK-TEXT-CAPACITY"]:self.assertIn(token,inputs)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalTextureReadbackInputsPassed",runner)
        self.assertIn("genericOriginalTextureReadbackInputsPassed: genericOriginalTextureReadbackInputsPassed",runner)

    def test_original_readback_existing_chain_revalidates_original_quota_and_frozen_mips(self):
        for filename,budget in [("texture_readback_layout.h","TextureReadbackLayoutBudget"),("texture_readback_request_declaration.h","TextureReadbackRequestDeclarationBudget"),("texture_readback_source_lease.h","TextureReadbackSourceLeaseBudget")]:
            source=self.read_source(*self.CURRENT_READBACK_SOURCES[filename])
            self.assertIn("const "+budget+" budget_",source)
            self.assertIn("originalMetadataQuota()->limitBytes()!=budget_.maxCpuMetadataBytes",source)
            for token in ["std::make_shared", "backends/", "WebGpuRecordedCommandProof", "signalFromHost"]:self.assertNotIn(token,source)
        layout=self.read_source("src/rhi/frontend/transfer_readback_input.h", "frontend_transfer_readback_layout_h")
        for token in ["TextureReadbackInputSnapshot::validateOriginal", "Validation::Mapper mapper", "mips_.values()[i].matches(expected)"]:self.assertIn(token,layout)
        lease=self.read_source("src/rhi/frontend/transfer_readback_input.h", "frontend_transfer_readback_source_lease_h")
        self.assertIn("self->validateMetadata()",lease)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalReadbackOwnershipQuotasPassed",runner)
        self.assertIn("originalReadbackOwnershipQuotasPassed: originalReadbackOwnershipQuotasPassed",runner)

    def test_original_readback_input_keeps_immutable_actual_quota_provenance(self):
        source=self.read_source("src/rhi/frontend/transfer_readback_arguments.h")
        for token in ["const TextureReadbackInputSnapshotBudget budget_", "originalMetadataQuota()->limitBytes()!=budget_.maxCpuMetadataBytes", "label_->originalMetadataQuota()->limitBytes()!=budget_.maxCpuLabelBytes", "TextureReadbackInputSnapshot::validateOriginal(input)", "label_->validateCapacity", "mipLayouts().size()>budget_.maxMipLayouts"]:self.assertIn(token,source)
        for token in ["inspectCpuAllocationContaining", "backends/", "WebGpuRecordedCommandProof", "std::make_shared", "std::vector"]:self.assertNotIn(token,source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalReadbackInputQuotasPassed",runner)
        self.assertIn("originalReadbackInputQuotasPassed: originalReadbackInputQuotasPassed",runner)

    def test_original_upload_input_freezes_pixel_quota_without_inspection_or_native_authority(self):
        source=self.read_source("src/rhi/frontend/transfer_upload_input.h", "frontend_transfer_upload_arguments_h")
        for token in ["const std::shared_ptr<core::CpuMemoryQuota> pixelQuota_", "const TextureUploadInputSnapshotBudget budget_", "originalPixelQuota()", "pixelQuota_->limitBytes()!=budget_.maxCpuPixelBytes", "label_->originalMetadataQuota()->limitBytes()!=budget_.maxCpuLabelBytes", "TextureUploadInputSnapshot::validateOriginal(input)", "label_->validateCapacity", "pixels_.size()!=scalar_.pixelDataSize()"]:
            self.assertIn(token,source)
        for token in ["inspectCpuAllocationContaining", "backends/", "WebGpuRecordedCommandProof", "std::make_shared", "std::vector"]:self.assertNotIn(token,source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalUploadInputQuotasPassed",runner)
        self.assertIn("originalUploadInputQuotasPassed: originalUploadInputQuotasPassed",runner)

    def test_complete_original_pass_inputs_reuse_sequence_and_consume_full_budget(self):
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        for token in ["hasQuotaBoundSharedOwner(*pass)", "sameOwner((*pass)->originalMetadataQuota(),quota)", "(*pass)->consumeCapacity(remainingPass)", "RenderPassReferenceSequenceContract::validate", "OriginalCommandTextureViewSource", "originalParentLease()->layout()", "storedTextBytes()", "maxPassAttachmentReferences", "value->matches(**input)"]:
            self.assertIn(token,inputs)
        for token in ["maxPassColorAttachments", "maxPassSubpasses", "maxPassAttachmentIndices", "maxPassDependencies"]:self.assertIn(token,inputs)
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_command_inputs.h", "backends_webgpu_webgpu_frozen_render_pass_binding_h")
        self.assertIn("RenderPassReferenceSequenceContract::validate",backend)
        shared=self.read_validation_section("src/rhi/frontend/command_draw_validation.h","render_pass_reference_sequence")
        for token in ["color.hasResolveTarget!=color.resolveTarget.isValid()", "desc.hasDepthStencilAttachment", "viewAt(consumed++", "consumed==referenceCount"]:self.assertIn(token,shared)
        args=self.read_source("src/rhi/frontend/command_arguments.h", "frontend_command_render_pass_arguments_h")
        self.assertIn("consumeInputCapacity",args)
        self.assertLess(args.index("DepthStencilAttachmentInputContract::diagnostic"),args.index("b.maxColorAttachments-="))
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalPassInputsPassed",runner)
        self.assertIn("genericOriginalPassInputsPassed: genericOriginalPassInputsPassed",runner)

    def test_original_view_source_retains_canonical_parent_without_pass_or_native_proof(self):
        source=self.read_validation_section("src/rhi/frontend/resource_leases.h","original_command_texture_view_source")
        for token in ["OriginalCommandTextureViewLease::validateOriginal", "OriginalCommandTextureLease::validateOriginal", "view->parentResource()==parent->resource()", "completeRetainedOwnerCount", "core::CpuMemoryQuotaScope", "core::makeUnique", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        for token in ["backends/", "WebGpu", "wgpu", "std::vector", "std::make_shared", "signalFromHost"]:self.assertNotIn(token,source)
        table=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        self.assertIn("OriginalCommandTextureViewSource",table)
        self.assertIn("sameOwner(*original,required)",table)
        self.assertIn("containsOriginalLease(sources,count,value->originalParentLease())",table)
        self.assertIn("RHI-ORIGINAL-SOURCES-VIEW-PARENT",table)
        lease=self.read_validation_section("src/rhi/frontend/resource_leases.h","original_command_resource_lease")
        self.assertIn("parentOf(object)==parent_",lease)
        self.assertIn("object.textureId()",lease)
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        self.assertIn("RHI-ORIGINAL-INPUTS-PASS-OWNER",inputs)
        self.assertIn("RenderPassReferenceSequenceContract::validate",inputs)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalViewSourcePassed",runner)
        self.assertIn("genericOriginalViewSourcePassed: genericOriginalViewSourcePassed",runner)

    def test_full_copy_snapshot_preserves_mips_without_source_or_native_authority(self):
        source=self.read_source("src/rhi/frontend/transfer_copy.h", "frontend_transfer_copy_input_snapshot_h")
        for token in ["TransferCopyArguments::captureScalarFields", "TextureCopyMipLayoutArguments::capture", "input.mipLayouts", "maxMipMappings", "core::BoundedSequence", "hasQuotaBoundSharedOwner", "core::CpuMemoryQuotaScope", "core::makeUnique", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        for token in ["backends/", "WebGpu", "wgpu", "std::vector", "std::make_shared", "signalFromHost"]:self.assertNotIn(token,source)
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        for token in ["TransferCopyInputSnapshot::validateOriginal", "RHI-ORIGINAL-INPUTS-COPY-QUOTA", "RHI-ORIGINAL-INPUTS-MIP-CAPACITY", "core::SharedIdentityProjection::inspect", "value->matches(*desc)"]:
            self.assertIn(token,inputs)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalCopyMipsPassed",runner)
        self.assertIn("genericOriginalCopyMipsPassed: genericOriginalCopyMipsPassed",runner)

    def test_generic_original_inputs_bind_fields_and_typed_references_without_execution(self):
        source=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        for token in ["OriginalCommandScalars::validateOriginal", "SharedOwnerProjection::sameOwner", "CommandInputContract::consumeDescriptor", "maxInputDescriptors", "maxGraphOwners", "usage.compareOriginalReference", "copy->source()", "copy->destination()", "dependency->firstResource()", "dependency->secondResource()", "sourceAdapterDiagnostic", "nativeExecutionProven() const noexcept {return false;}", "arp::rhi::semantics::OriginalCommandAdmissionLowering::lowering(backend)", "core::makeUnique", "core::BoundedSequence"]:
            self.assertIn(token,source)
        for token in ["backends/", "WebGpu", "wgpu", "std::vector", "std::make_shared", "signalFromHost", "NativeSync"]:
            self.assertNotIn(token,source)
        webgpu=self.read_source("src/rhi/backends/webgpu/webgpu_original_command_inputs.h")
        self.assertIn("CommandInputContract::consumeDescriptor",webgpu)
        self.assertIn("dependency->firstResource()",webgpu)
        self.assertIn("dependency->secondResource()",webgpu)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& genericOriginalInputsPassed",runner)
        self.assertIn("genericOriginalInputsPassed: genericOriginalInputsPassed",runner)

    def test_texture_upload_input_capture_cannot_issue_native_source_authority(self):
        source=self.read_source("src/rhi/frontend/transfer_upload_input.h", "frontend_transfer_upload_arguments_h")
        self.assertEqual(re.findall(r"friend class (\w+);",source),["TextureUploadInputSnapshot"])
        for token in ["core::CpuMemoryQuotaScope metadata(metadataQuota)","core::CpuMemoryQuotaScope pixels(pixelQuota)",
                      "CommandVariableArguments::prepareOriginal", "input.pixelDataSize && !input.pixelData",
                      "TextureUploadArguments::sameMipLayout", "callerPixelPointerPresent_!=(input.pixelData!=nullptr)",
                      "std::memcpy(result->pixels_.data(),input.pixelData,input.pixelDataSize)",
                      "RHI-TEXTURE-UPLOAD-ORIGINAL-SOURCE-UNPROVEN"]:
            self.assertIn(token,source)
        for forbidden in ["WebGpuRecordedCommandProof", "WebGpuNative", "completionEpoch", "effectiveMipLevel", "TransferCopyArguments"]:
            self.assertNotIn(forbidden,source)
        self.assertNotRegex(source,r"\bwgpu\w+\s*\(")
        original=self.read_source("src/rhi/backends/webgpu/webgpu_original_command_inputs.h")
        self.assertIn("case K::TextureUpload:return Family::TextureUpload",self.input_family_contract_source())


    def test_original_stream_compilation_is_cpu_only_and_keeps_original_owners(self):
        source=self.read_validation_section("src/rhi/frontend/command_stream_program.h","original_command_stream_program")
        self.assertEqual(re.findall(r"friend class (\w+);",source),["OriginalCommandStreamProgram"])
        for forbidden in ["OriginalCommandStateTransaction", "MutationJournal", "WebGpuRecordedCommandProof", "wgpuQueue", "std::vector", "std::make_shared", "std::make_unique"]:
            self.assertNotIn(forbidden,source)
        self.assertIn("core::BoundedSequence<Operation>",source)
        self.assertIn("core::PreflightReplay<Operation>::run",source)
        self.assertIn("CommandInputContract::consumeDescriptor",source)
        self.assertIn("TransferCopyProjection::visit",source)
        self.assertIn("bool nativeExecutionProven() const noexcept {return false;}",source)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        payload=native[native.index("class WebGpuCanonicalCopyPassLowering::Payload"):native.index("class WebGpuCanonicalCallbackCustody::RuntimeState")]
        self.assertIn("Stream::visitOriginal",payload)
        self.assertIn("inputs.descriptors()[operation.descriptorIndex]",payload)
        self.assertIn("operation.firstElement!=count",payload)
        self.assertIn("operation.elementCount",payload)
        self.assertNotIn("static const rhi::OriginalCommandInputs::Descriptor* input(",payload)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalStreamProgramPassed",runner)
        self.assertIn("originalStreamProgramPassed: originalStreamProgramPassed",runner)

    def test_original_stream_full_executor_outcomes_remain_explicit(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_usage.h","src/rhi/semantics/command_lowering.h","original_command_stream")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",source)
        self.assertIn("NULL-VALIDATOR-ONLY",source)
        for backend in ["VULKAN", "METAL", "WEBGPU"]:
            self.assertIn("RHI-ORIGINAL-STREAM-"+backend+"-EXECUTOR-MISSING",source)
            self.assertIn("RHI-ORIGINAL-BINDLESS-STREAM-"+backend+"-EXECUTOR-MISSING",source)
        self.assertNotIn("CapabilityMode::Native",source)

    def test_ordered_copy_attachment_requires_explicit_version_and_closed_families(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_ordered_copy_attachment")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",contract)
        for token in ["OriginalCommandStreamProgram::inspect", "default:return false", "RecordedResourceStateSequence::validateBarrier", "desc.textureSubresources!=whole", "-RANGE", "-SCOPE", "-ASPECT"]:
            self.assertIn(token,contract)
        for forbidden in ["wgpu", "makeShared", "MutationJournal", "WebGpuRecordedCommandProof"]:
            self.assertNotIn(forbidden,contract)
        cold=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_cold")
        for token in ["b.contractVersion!=1u && b.contractVersion!=2u", "b.contractVersion==1u && (b.maxCopyElements || b.maxAttachmentReferences)", "if(!b.prototypeOptIn)", "r.contractVersion!=b.contractVersion"]:
            self.assertIn(token,cold)
        admission=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_cold_admission.h")
        for token in ["deriveRequirements", "OriginalCommandOrderedCopyAttachmentContract::inspect", "counts.commands", "counts.renderPasses", "requirements_(required)", "compareCapacity(required,reservation->originalRequest())"]:
            self.assertIn(token,admission)

    def test_ordered_native_recorder_consumes_cold_stable_per_pass_descriptors(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        payload=source[source.index("class WebGpuCanonicalCopyPassLowering::Payload"):source.index("class WebGpuCanonicalCallbackCustody::RuntimeState")]
        for token in ["core::BoundedSequence<Pass> passes", "core::BoundedSequence<OrderedOperation> operations", "pass.finalize([&](rhi::GpuResourceId id)"]:
            self.assertIn(token,payload)
        builder=self.read_source("src/rhi/backends/webgpu/webgpu_original_render_pass_payload.h")
        for token in ["renderPass.nextInChain=&maxDraw.chain", "renderPass.colorAttachments=colors.values().data()"]:
            self.assertIn(token,builder)
        recorder=source[source.index("const char* WebGpuOrdinaryRecording::recordCanonical"):]
        for token in ["projection.operations.values()", "originalOperation.firstElement", "originalOperation.elementCount", "projection.passes.values()[ordered.passIndex]", "case K::Finish", "wgpuCommandEncoderFinish", "case K::BeginRenderPass", "case K::EndRenderPass"]:
            self.assertIn(token,recorder)
        for forbidden in ["core::makeShared", "core::makeUnique", "reserve(", "append("]:
            self.assertNotIn(forbidden,recorder[:recorder.index("#endif\n}")+len("#endif\n}")])
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_ordered_copy_attachment_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifySingleUse", "clear-red copy-red clear-green copy-green", "genuine-second-pass-usage-error", "CALLBACK-QUARANTINED", "retained.expired()", "originalSubmissionSlotsAtLoss", "all-nine-quotas-zero"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["&& orderedCopyAttachmentPassed", "orderedCopyAttachmentPassed: orderedCopyAttachmentPassed", '"COLD"', '"TRANSFER_ONLY"', "ORDERED_COPY_ATTACHMENT_LIFECYCLE_OK"]:
            self.assertIn(token,runner)

    def test_canonical_upload_staging_requirements_reuse_exact_mapping_without_authority(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_transfer_validation.h","src/rhi/semantics/command_lowering.h","original_command_upload_staging")
        for token in ["OriginalCommandStreamProgram::visitOriginal", "TextureUploadLayoutValidation::inspectPackedBytes", "source->originalDestinationLease()->layout()", "core::MemoryBudgetLedger", "maxBufferBytes", "maxPackedTextureBytes", "maxTotalBytes", "if(!result.diagnostic)"]:
            self.assertIn(token,source)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",source)
        self.assertEqual(source.count("-JOIN-MISSING"),3)
        for forbidden in ["backends/", "WebGpu", "wgpu", "makeShared", "makeUnique", "MutationJournal", "std::vector", "resourceGeneration", "frameSerial"]:
            self.assertNotIn(forbidden,source)
        original=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_texture_upload_source")
        self.assertIn("TextureUploadLayoutValidation::inspectPackedBytes",original)
        self.assertNotIn("TextureUploadLayoutValidation::Mapper mapper",original)
        mapping=self.read_source("src/rhi/frontend/transfer_upload_layout_validation.h")
        for token in ["Mapper mapper(input,destination)", "if(!mapper(i,mip))return mapper.diagnostic()", "bytes=mapper.packedBytes()", "if (!index) if (const auto* id=validate(input_,destination_)) return id"]:
            self.assertIn(token,mapping)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_upload_staging_contract_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["refused.required.matches({})", "allocationCount==allocations", "BINDLESS_RHI_UPLOAD_STAGING_REQUIREMENTS_OK", "!decision.initializationAvailable && !decision.recordingAvailable"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalUploadStagingRequirementsPassed",runner)
        self.assertIn("originalUploadStagingRequirementsPassed: originalUploadStagingRequirementsPassed",runner)

    def test_canonical_initialized_source_uses_original_object_and_bounded_native_receipts(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h", "backends_webgpu_webgpu_canonical_buffer_upload_source_h")
        for token in ["Contract::inspectOriginal", "inspectCpuAllocationCharge(candidate.get())", "hasIndependentOwners", "Lease::prepareColdOriginal", "Lease::adoptOriginal", "originalObject->sealWrites()", "observeOriginalWebGpuResource", "validateOriginalWebGpuResource", "Receipt::prepare", "Receipt::bind", "prepareWebGpuNativeDeviceLossRegistration", "ExternalCallbackGate", "CompletionEvidenceBatch", "Receipt::writeBuffer", "sameOwner(original->originalSourceLease(),state_->lease)", "committed->diagnostic()"]:
            self.assertIn(token,source)
        self.assertLess(source.index("auto result=shareOriginal"),source.index("Lease::adoptOriginal"))
        self.assertLess(source.index("nativeLoss.loss=prepareWebGpuNativeDeviceLossRegistration"),source.index("Lease::adoptOriginal"))
        self.assertLess(source.index("Lease::adoptOriginal"),source.index("originalObject->sealWrites()"))
        body=source[source.index("const char* initialize(const std::shared_ptr<State>&"):source.index("const Contract::Budget budget;")]
        self.assertLess(body.index("mayWrite=true"),body.index("Receipt::writeBuffer"))
        for forbidden in ["makeShared", "makeUnique", "reserve(", "resize(", "MutationJournal", "WebGpuRecordedCommandProof", "createWebGpuRhiBuffer"]:
            self.assertNotIn(forbidden,body)
        retire=source[source.index("void retire()"):source.index("void finishInitialization()")]
        self.assertIn("mayWrite && !queueObserved",retire)
        self.assertIn("!physicalLoss",retire)
        self.assertLess(retire.index("detachWebGpuNativeDeviceLossRegistration"),retire.index("loss.owner.reset()"))
        for forbidden in ["MutationJournal", "WebGpuOriginalStateTransaction", "WebGpuRecordedCommandProof", "std::make_shared", "std::vector", "const_cast"]:
            self.assertNotIn(forbidden,source)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_buffer_upload_source_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifySingleUse", "!original->hostWritesSealed()", "std::memcmp", "same-candidate-on-refusal", "retained.expired()", "callbacksOutstandingAtLoss()", "REGISTRATION-FAILED", "all-eight-quotas-zero"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalInitializedBufferStagingPassed",runner)
        self.assertIn("canonicalInitializedBufferStagingPassed: canonicalInitializedBufferStagingPassed",runner)

    def test_immutable_initialized_index_uses_same_source_and_genuine_completion(self):
        contract=self.read_validation_section("src/rhi/frontend/command_transfer_validation.h","original_command_initialized_buffer_staging")
        for token in ["IndexContentIntent", "indexContent.diagnostic()", "indexContent.empty()", "expectedUsage", "expectedState", "IndexBuffer", "maxAnalysisIndices"]:
            self.assertIn(token,contract)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h", "backends_webgpu_webgpu_canonical_buffer_upload_source_h")
        start=source.index("static const char* validateInitializedIndexContent")
        body=source[start:source.index("bool overlapsCpuQuota",start)]
        for token in ["validateOwner(self)", "Lease::validateOriginal(lease)", "sameOwner(lease,state.lease)", "readyDiagnostic()", "initializedReadyFacts()", "state.payload.data()", "maxAnalysisIndices", "output={}"]:
            self.assertIn(token,body)
        for token in ["makeShared", "makeUnique", "reserve(", "resize(", "MutationJournal", "wgpuQueueSubmit"]:
            self.assertNotIn(token,body)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_buffer_upload_source_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["profile<3u", "verifyInitializedIndices", "verifyNoHotQueries", "forgedLease", "forgedSource", "wgpuCommandEncoderCopyBufferToBuffer", "std::memcmp", "i%2u", "callbacksOutstandingAtLoss()", "IMMUTABLE_INDEX16", "IMMUTABLE_INDEX32"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& immutableOriginalIndexContentPassed",runner)
        self.assertIn("immutableOriginalIndexContentPassed: immutableOriginalIndexContentPassed",runner)

    def test_initialized_vertex_uses_original_actor_and_completion_without_draw_authority(self):
        contract=self.read_validation_section("src/rhi/frontend/command_transfer_validation.h","original_command_initialized_buffer_staging")
        for token in ["VertexContentIntent", "vertexContent.diagnostic()", "vertexContent.empty()",
                      "UINT64_MAX / strideBytes", "validateVertexPayload", "validateVertexRange",
                      "VertexInputConsumptionContract::validateStreamRange", "ResourceUsage::VertexBuffer"]:
            self.assertIn(token,contract)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h", "backends_webgpu_webgpu_canonical_buffer_upload_source_h")
        body=source.split("static const char* validateInitializedVertexContent",1)[1].split("bool overlapsCpuQuota",1)[0]
        for token in ["validateOwner(self)", "Lease::validateOriginal(lease)", "sameOwner(lease,state.lease)",
                      "readyDiagnostic()", "initializedReadyFacts()", "Contract::validateVertexRange"]:
            self.assertIn(token,body)
        for token in ["makeShared", "makeUnique", "resize(", "wgpuQueueSubmit", "wgpuRenderPassEncoderDraw"]:
            self.assertNotIn(token,body)
        self.assertIn("WGPUBufferUsage_Vertex",source)
        fixture=self.read_source("tests/fixtures/webgpu_canonical_buffer_upload_source_fixture.h")
        for token in ["profile<5u", "verifyInitializedVertices", "IMMUTABLE_VERTEX", "IMMUTABLE_INSTANCE",
                      "verifyNoHotQueries", "wgpuCommandEncoderCopyBufferToBuffer", "std::memcmp"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("&& immutableOriginalVertexContentPassed",runner)
        self.assertIn("immutableOriginalVertexContentPassed: immutableOriginalVertexContentPassed",runner)

    def test_geometry_content_custody_reuses_original_closure_and_initialized_fetch(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_closure")
        for token in ["GeometryContentCustody", "geometryContent.diagnostic()", "geometryContent.empty()",
                      "geometryContentLossBoundLowering", "RHI-ORIGINAL-NATIVE-CLOSURE-GEOMETRY-SCHEMA"]:
            self.assertIn(token,contract)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["using GeometryContentInput = IndexContentInput", "prepareOriginalGeometryContentLossBound",
                      "validateGeometryContents", "access.bindingCommandIndex", "Fetch::visitInitialized",
                      "validateInitializedVertexContent", "validateInitializedVertexRange", "analyzeInitializedIndices",
                      "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-FOOTPRINT-MISMATCH", "RHI-WEBGPU-CANONICAL-CLOSURE-GEOMETRY-COVERAGE"]:
            self.assertIn(token,source)
        body=source.split("static const char* validateGeometryContents",1)[1].split("static const char* validateIndexBindings",1)[0]
        for token in ["makeShared", "makeUnique", "resize(", "wgpuQueueSubmit", "wgpuRenderPassEncoderDraw"]:
            self.assertNotIn(token,body)
        fetch=self.read_source("src/rhi/frontend/command_geometry_fetch.h")
        self.assertIn("vertexAnalyze(point,binding,stream,*fields,buffer)",fetch)
        fixture=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        self.assertIn("BINDLESS_RHI_GEOMETRY_CONTENT_CUSTODY_OK",fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("&& originalGeometryContentCustodyPassed",runner)
        self.assertIn("originalGeometryContentCustodyPassed: originalGeometryContentCustodyPassed",runner)

    def test_initialized_index_shader_read_is_independent_and_preserves_seals(self):
        contract=self.read_validation_section("src/rhi/frontend/command_transfer_validation.h","original_command_initialized_buffer_staging")
        for token in ["shaderReadContractVersion", "shaderReadOptIn", "shaderReadEnabled",
                      "RHI-INITIALIZED-INDEX-SHADER-READ-VERSION", "RHI-INITIALIZED-INDEX-SHADER-READ-OPT-IN",
                      "usage|ResourceUsage::StorageBuffer", "desc.usage!=expectedUsage(b)",
                      "indexShaderReadLowering(device.backend())", "buffer.gpuWritesSealed()"]:
            self.assertIn(token,contract)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h", "backends_webgpu_webgpu_canonical_buffer_upload_source_h")
        self.assertIn("Contract::shaderReadEnabled(budget)?usage|WGPUBufferUsage_Storage:usage",source)
        self.assertIn("object->gpuWritesSealed()",source)
        decisions=self.read_source("src/rhi/semantics/command_lowering.h")
        self.assertIn("indexShaderReadLowering",decisions)
        self.assertIn("return lowering(backend, 2u)",decisions)
        fixture=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        for token in ["upload.indexContent.shaderReadContractVersion=1u", "upload.indexContent.shaderReadOptIn=true",
                      "RHI-INITIALIZED-CANONICAL-STAGING-LAYOUT", "!buffer->hostWritesSealed()",
                      "bufferLease->originalObject()->gpuTransferWritesSealed()"]:
            self.assertIn(token,fixture)

    def test_index_content_custody_extends_original_closure_without_recording_authority(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_closure")
        for token in ["IndexContentCustody", "indexContent.diagnostic()", "indexContent.empty()", "maxInitializedBytes", "indexContentLossBoundLowering"]:
            self.assertIn(token,contract)
        for backend in ["NULL", "VULKAN", "METAL", "WEBGPU"]:
            self.assertIn("RHI-ORIGINAL-INDEX-CUSTODY-"+backend,contract)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["IndexContentInput", "validateIndexBindings", "indexForSource", "entry.indexContent", "validateInitializedIndexContent", "initializedIndexOwnerBound", "stream.originalInputs()->visitGeometryAccesses", "INDEX-COVERAGE", "INDEX-DUPLICATE", "INDEX-QUOTA-DOMAIN"]:
            self.assertIn(token,source)
        self.assertIn("return version==3u||version==4u",source)
        self.assertIn("inspectEntry(*root,index,mappedDiagnostic,typed,tables,general)",source)
        self.assertNotIn("stateContractVersion()>=3u",source)
        body=source[source.index("template<class At> static const char* validateIndexBindings"):source.index("// Allocation-free dry mapping")]
        for token in ["wgpu", "MutationJournal", "makeShared", "makeUnique", "reserve(", "resize("]:
            self.assertNotIn(token,body)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_pipeline_binding_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyGeometryIndexCustody", "pressure.verify", "initialized.reset()", "retained.expired()", "GEOMETRY-FOOTPRINT-MISMATCH", "RENDER-FOOTPRINT-MISMATCH", "GEOMETRY_INDEX_CUSTODY_OK"]:
            self.assertIn(token,fixture)
        factory=self.read_source("tests/fixtures/original_accepted_graphics_pipeline_factory_fixture.h")
        for token in ["Context positional(create,input,layoutDesc,state,19u,6u,2u,true)",
                      "!positional.device->observeOriginalDeviceNativeLoss(diagnostic)",
                      "RHI-ORIGINAL-DEVICE-LOSS-UNPREPARED",
                      "Context geometry(create,input,layoutDesc,state,19u,rhi::OriginalTypedNativeLossJoinContract::registrationVersion,2u,true,0u,OriginalCommandPipelineBindingFixture::indexedDescriptorCount,false,&observers)",
                      "verifyGeometryGraphics(*geometry.device"]:
            self.assertIn(token,factory)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalGeometryIndexCustodyPassed",runner)
        self.assertIn("originalGeometryIndexCustodyPassed: originalGeometryIndexCustodyPassed",runner)

    def test_indirect_sequence_public_inventory_preserves_each_single_record(self):
        source=self.read_source("src/rhi/gpu_commands.h")
        body=source[source.index("virtual CommandValidationResult drawIndexedIndirectSequence"):source.index("virtual CommandValidationResult drawIndexedIndirectCount")]
        self.assertLess(body.index("if(!range.valid())"),body.index("for(std::uint32_t ordinal"))
        self.assertIn("drawIndexedIndirect(buffer,address,1u,stride)",body)
        self.assertIn("if(!result.success)return result",body)
        for token in ["readback(", "mapAsync", "memcpy", "queueSubmit"]:
            self.assertNotIn(token,body)

    def test_original_indexed_fetch_consumes_scalars_and_current_geometry_without_native_authority(self):
        source=self.read_source("src/rhi/frontend/command_geometry_fetch.h")
        for token in ["OriginalCommandInputs::validateOriginal(inputs)", "contractVersion!=(Produced?8u:preparedRoot?7u:5u)",
                      "K::DrawIndexed", "draw->vertexOffset", "draw->firstInstance",
                      "point.pipelineBind", "point.indexBind", "maxBindingScans", "maxAnalyzedIndices",
                      "ANALYZER-CORRESPONDENCE", "VertexInputConsumptionContract::inspectStream",
                      "VertexInputConsumptionContract::validateStreamRange", "RESTART-PROFILE",
                      "checked.diagnostic?checked:walk<Produced>", "nativeExecutionProven()"]:
            self.assertIn(token,source)
        for forbidden in ["makeShared", "makeUnique", "pmr::Vector", "wgpu", "MutationJournal", "queueSubmit"]:
            self.assertNotIn(forbidden,source)
        decisions=self.read_source("src/rhi/semantics/command_draw_lowering.h")
        for backend in ["NULL", "VULKAN", "METAL", "WEBGPU"]:
            self.assertIn("RHI-ORIGINAL-INDEXED-FETCH-"+backend,decisions)
        fixture=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        for token in ["verifyIndexedDrawFetch", "analyzeInitializedIndices", "result.bindingScans==11u",
                      "late-negative-no-output", "INDEX-UNBOUND", "forged", "hot0 query0"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("&& originalIndexedDrawFetchPassed",runner)
        self.assertIn("originalIndexedDrawFetchPassed: originalIndexedDrawFetchPassed",runner)

    def test_indexed_shader_consumption_joins_existing_state_stream_and_keeps_execution_closed(self):
        model=self.read_source("src/rhi/frontend/command_model.h")
        for token in ["indexedDrawConsumptionVersion", "indexedDrawConsumptionOptIn",
                      "RHI-ORIGINAL-CONSUMPTION-INDEXED-PROFILE", "INDEXED-VERSION", "INDEXED-OPT-IN",
                      "a.indexedDrawConsumptionVersion==b.indexedDrawConsumptionVersion",
                      "a.indexedDrawConsumptionOptIn==b.indexedDrawConsumptionOptIn"]:
            self.assertIn(token,model)
        consumption=self.read_source("src/rhi/frontend/command_usage.h")
        for token in ["isConsumption(kind,budget)", "kind==K::DrawIndexed", "indexedDrawLowering",
                      "supported(kind,budget.contractVersion,budget.indexedDrawConsumptionOptIn)"]:
            self.assertIn(token,consumption)
        stream=self.read_source("src/rhi/frontend/command_stream_program.h")
        for token in ["visitIndexedDrawFetch", "RHI-ORIGINAL-STREAM-INDEXED-CONSUMPTION-REQUIRED",
                      "if(const auto*id=validate())", "Fetch::visit(originalInputs()",
                      "OriginalCommandDescriptorConsumptionContract::isConsumption"]:
            self.assertIn(token,stream)
        decisions=self.read_source("src/rhi/semantics/command_input_admission_lowering.h")
        for backend in ["NULL", "VULKAN", "METAL"]:
            self.assertIn("RHI-ORIGINAL-INDEXED-CONSUMPTION-"+backend,decisions)
        for fixture,marker in [("original_command_descriptor_consumption_fixture.h","INDEXED_SHADER_CONSUMPTION_OK"),
                               ("original_command_pipeline_binding_fixture.h","INDEXED_STATE_STREAM_OK")]:
            self.assertIn(marker,self.read_source("tests/fixtures/"+fixture))
        fixture=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        for token in ["OriginalWholeDescriptorWriteFixture::commitActual", "descriptorBinding(4u,0u)",
                      "RHI-ORIGINAL-CONSUMPTION-DESCRIPTOR-MISSING", "result.bindingScans==14u",
                      "program->counts().descriptorBinds==3u", "program->counts().implicitAccesses==9u",
                      "typedSources==4u", "typedSources-1u", "source->liveBytes()==sourceBytes",
                      "RHI-ORIGINAL-SOURCES-ASSEMBLY-CAPACITY", "set.layout.bindings[0u].binding==3u",
                      "wrongBinding.write.binding=0u", "RHI-ORIGINAL-WHOLE-WRITE-BINDING",
                      "descriptor->contentRevision()==revision", "geometryCapacity=root->originalBudget().maxInputDescriptors",
                      "b.maxIndexBindings=geometryCapacity+1u", "b.maxVertexBindings=geometryCapacity+1u",
                      "auto exactCapacity=factory()", "geometry Inputs unexpectedly accepted negative",
                      "RHI-WEBGPU-CANONICAL-CLOSURE-DESCRIPTOR-UNBOUND",
                      "verifyGeometryIndexCustody(graph,program,device,state,initialized,true)",
                      "verifyIndexedStateStream(join(),device,joinedFetch,analyze,observe,initialized)",
                      "verifyIndexedStateStream(join(),device,joinedFetch,analyze,observe,initialized,true)",
                      "BINDLESS_RHI_GEOMETRY_PIPELINE_CUSTODY_REFUSED", "joinedRefs[5u]={6u}",
                      "BINDLESS_RHI_GEOMETRY_DESCRIPTOR_INVENTORY_OK", "descriptorCount=indexedDescriptorCount",
                      "assert(descriptorCount && descriptorCount<=indexedDescriptorCount)",
                      "arena.maxSets=descriptorCount", "arena.maxDescriptors=descriptorCount",
                      "indexed descriptor arena registration: %.*s", "indexed descriptor set registration: %.*s",
                      "graph=stateRetained.lock()", "GEOMETRY_UNBOUND_CUSTODY_REFUSED"]:
            self.assertIn(token,fixture)
        self.assertNotIn("b.maxIndexBindings=9u",fixture)
        smoke=self.read_source("tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp")
        self.assertIn("OriginalTargetMemoryFixture::loadModuleWithTargetMemory",smoke)
        memory=self.read_source("tests/fixtures/original_target_memory_fixture.h")
        for token in ["vertex.prepare(bare->vertex())", "fragment.prepare(bare->fragment())",
                      "budget.targetMemory=&request", "SHADER-MODULE-TARGET-MEMORY-NOT-REQUESTED",
                      "quota->liveBytes()==baseline", "out.stageMask==binding.stageMask"]:
            self.assertIn(token,memory)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        for name in ["originalIndexedShaderConsumptionPassed", "originalIndexedStateStreamPassed"]:
            self.assertIn("&& "+name,runner)
            self.assertIn(name+": "+name,runner)

    def test_state4_plan_materializes_the_counted_render_binding_steps(self):
        state=self.read_source("src/rhi/frontend/command_state.h")
        plan=state[state.index("const char* populatePlan(Plan& plan)"):state.index("const char* validatePlannedFinals()")]
        self.assertIn("budget_.contractVersion == 3u || budget_.contractVersion == 4u",plan)
        self.assertIn("graph_->visitRenderDescriptorAccesses",plan)
        self.assertIn("budget_.renderDescriptorUsage, addImplicitBuffer",plan)
        fixture=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        for token in ["root->materializeOriginalPrototypePlan(shortPlan)",
                      "!root->hasOriginalPrototypePlan()", "recording->liveBytes()==rootBytes",
                      "planAllocations", "geometry State4 plan: %s",
                      "root->plannedAccessStepCount()==(expectGeometryConsumption ? 14u : expectUnboundDescriptor ? 5u : 11u)"]:
            self.assertIn(token,fixture)

    def test_canonical_texture_packing_retains_exact_originals_and_normative_layout(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_texture_upload_packing_source")
        for token in ["OriginalCommandTextureUploadSource::validateOriginal", "sameOwner(source->originalInput(),packing->inputOwner())", "packing->destinationLayout().matches", "inspectPackedBytes", "maxPixelBytes", "maxPackedBytes", "maxMipMappings", "hasIndependentOwners", "packedCharge.quota==domain.get()"]:
            self.assertIn(token,source)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",source)
        self.assertEqual(source.count("-PAYLOAD-MISSING"),2)
        self.assertIn("Disposition::Prototype,true,false",source)
        for forbidden in ["backends/", "wgpu", "MutationJournal", "WebGpuRecordedCommandProof", "memcpy", "copyRows", "std::vector"]:
            self.assertNotIn(forbidden,source)

    def test_canonical_texture_staging_joins_real_initialized_bytes_and_keeps_content_closed(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h", "backends_webgpu_webgpu_canonical_texture_upload_source_h")
        for token in ["Binding::validateOriginal", "device.validateOriginalDeviceNativeLoss", "device.validateOriginalResourceRegistration(*destination)", "device.validateOriginalResourceRegistration(*buffer->originalLease()->originalObject())", "compareFrozenPayload", "observeOriginalCommittedState", "validateOriginalCommittedState(expected_)", "validateOriginalWebGpuResource", "buffer_->readyDiagnostic()", "sameOwner(original,self->binding_)", "TextureUploadPackedCopyMapping::mapOriginal", "return arp::rhi::semantics::OriginalCommandTextureUploadPackingSourceLowering::lowering(rhi::Backend::WebGPU).diagnostic", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        for forbidden in ["queue.submit", "wgpuQueueSubmit", "wgpuCommandEncoder", "MutationJournal", "TextureUploadContentTransaction", "WebGpuRecordedCommandProof", "std::make_shared", "const_cast"]:
            self.assertNotIn(forbidden,source)
        buffer=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h", "backends_webgpu_webgpu_canonical_buffer_upload_source_h")
        self.assertIn("std::memcmp(bytes,state_->payload.data(),state_->payload.size())",buffer)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_texture_upload_source_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["std::memcmp", "pending-stale-target-refused", "callbacksOutstandingAtLoss()==4u", "all-fourteen-quotas-zero", "target-content-execution-closed", "PAYLOAD-JOIN"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalTextureStagingPassed",runner)
        self.assertIn("canonicalTextureStagingPassed: canonicalTextureStagingPassed",runner)

    def test_canonical_content_staging_composes_exact_factory_expectation_without_authority(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h", "backends_webgpu_webgpu_canonical_texture_content_upload_source_h")
        # Completed input inspection is a separate opt-in consumer of the old
        # immutable staging actor; the constructor still cannot mint generations.
        source=source.split("static const char* validateCompletedInputFacts",1)[0]
        for token in ["staging->hasQuotaBoundSharedOwner(staging)", "target.validateOriginalContent(content)", "staging->stagingReadyDiagnostic()", "OriginalTextureContentUploadValidation::inspect", "staging->overlapsCpuQuota(contentQuota.get())", "device.overlapsOriginalDeviceCpuOwner(quota)", "OriginalTextureContentStagingContract::lowering(backend)"]:
            self.assertIn(token,source)
        for forbidden in ["wgpu", "MutationJournal", "WebGpuRecordedCommandProof", "friend class WebGpuOrdinary", "resourceGeneration", "frameSerial"]:
            self.assertNotIn(forbidden,source)
        contract=self.read_validation_section("src/rhi/frontend/resource_texture_validation.h","original_texture_content_staging")
        self.assertEqual(contract.count("-PAYLOAD-MISSING"),2)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_texture_upload_source_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["RHI-CANONICAL-CONTENT-CONTROL-PENDING", "RHI-CANONICAL-CONTENT-EXPECTATION-STALE", "sixteen-quotas-zero", "verifyContentBinding"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalContentStagingPassed",runner)

    def test_upload_cold_version_three_requires_exact_joins_and_stops_before_issuers(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_cold_admission.h")
        for token in ["OriginalCommandOrderedUploadContract::inspect", "maxBufferUploadDeclaredBytes", "maxTextureUploadPixelBytes", "pending.totalUploadBytes=upload.staging.totalBytes", "required={}", "required=pending", "WebGpuCanonicalUploadJoins::validateOriginal(joins)", "sameOwner(joins->originalRoot(),root)", "joins->overlapsCpuOwner", "originalUploadJoins", "UPLOAD-QUOTA-DOMAIN"]:
            self.assertIn(token,source)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_cold")
        for token in ["b.contractVersion!=3u", "UPLOAD-PROFILE", "UPLOAD-REQUIREMENTS-PROFILE", "r.totalUploadBytes-r.bufferUploadBytes", "maxPackedTextureUploadBytes", "maxTextureWriteTexelBytes", "COLD-JOIN-ONLY"]:
            self.assertIn(token,contract)
        runtime=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        guard=runtime[runtime.index("const char* WebGpuNativeCommandPreparation::inspectCanonicalColdCommand"):runtime.index("const char* WebGpuNativeCommandPreparation::inspectCanonicalCommandOwnership")]
        self.assertNotIn("uploadLowering(rhi::Backend::WebGPU).diagnostic",guard)
        issuer=runtime[runtime.index("std::shared_ptr<const WebGpuCanonicalIssuerPreparation> WebGpuCanonicalCallbackCustody::prepareOriginalIssuers"): ]
        self.assertIn("lowering->budget_.contractVersion==3u",issuer)
        self.assertIn("BufferContract::validateWork(required.bufferUploads,required.textureUploads)",issuer)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_upload_cold_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.verify(peak,make)", "!refused.totalUploadBytes", "ColdCommand::prepareOriginal", "originalReservedCallbackTicketCount", "substitutedCallback", "command-storage-budget-refused"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalUploadColdPassed",runner)

    def test_upload_projection_freezes_exact_original_ordinals_without_execution(self):
        runtime=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        body=runtime[runtime.index("template<class Visitor> static const char* inspectUploads"):runtime.index("static const char* validatePass")]
        for token in ["WebGpuCanonicalUploadJoins::validateOriginal", "sameOwner(joins->originalRoot(),closure.originalRoot())", "commandIndex!=operation.commandIndex", "sameOwnersAndLayout", "inspectBufferTextureLayout", "originalUploadExclusive", "mapPackedCopyOriginal", "UPLOAD-COVERAGE", "maxUploadCopyOperations"]:
            self.assertIn(token,body)
        for forbidden in ["wgpuCommandEncoder", "wgpuQueue", "MutationJournal", "TextureUploadContentTransaction", "emit(", "new "]:
            self.assertNotIn(forbidden,body)
        for token in ["uploads(uploadCount)", "ordered.uploadFirst=uploadFirst", "stored.uploadCount==added", "std::uint64_t(count)+uploadCount!=admission.requirements().nativeCopyCommands"]:
            self.assertIn(token,runtime)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_upload_projection_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.verifySingleUse", "verifyHotNoAllocation", "inspectUploadProjection", "root->journalEmpty()", "metadata-grants-no-execution"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalUploadProjectionPassed",runner)
        self.assertIn("canonicalUploadProjectionPassed: canonicalUploadProjectionPassed",runner)

    def test_initialized_buffer_upload_pending_requires_private_real_journal_authority(self):
        root=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        pending=root[root.index("const char* validateInitializedBufferPending"):root.index("struct OriginalContentUpload")]
        for token in ["!recordingSealed_", "status_!=Status::Pending", "captured.registration_.matchesIdentity(&object", "captured.control_!=object.originalStateControl_", "entry.observation.revision()==captured.revision()", "OriginalInitializedBufferPendingUse::CopySource1", "OriginalInitializedBufferPendingContract::validateStates", "cell->scalarPending->revision()!=captured.revision()", "matchesPending(*cell->scalarPending,journal_)"]:
            self.assertIn(token,pending)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h", "backends_webgpu_webgpu_canonical_buffer_upload_source_h")
        for token in ["compareInitializedUploadFacts", "state_->evidence.proven()", "state_->drained()", "committed->diagnostic()"]:
            self.assertIn(token,source)
        runtime=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for token in ["seal->validateInitializedBufferPending", "projection.joins->validateRetained(*seal)", "case K::BufferUpload:case K::TextureUpload", "upload.command!=originalOperation.commandIndex", "ordered.uploadFirst+i", "BufferContract::validateWork(required.bufferUploads,required.textureUploads)"]:
            self.assertIn(token,runtime)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_transfer_validation.h","src/rhi/semantics/command_lowering.h","original_command_initialized_buffer_upload_execution")
        self.assertIn('if(textures)return "RHI-ORIGINAL-INITIALIZED-BUFFER-UPLOAD-TEXTURE-PROFILE"',contract)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalBufferUploadExecutionPassed",runner)

    def test_initialized_texture_upload_requires_exact_original_state_content_and_minted_generation(self):
        root=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        body=root[root.index("const char* validateInitializedTexturePending"):root.index("std::optional<OriginalContentUpload> contentUploadAt")]
        for token in ["state.registration_.matchesIdentity(&object", "content.registration_.matchesIdentity(&object", "state.normalized_!=object.originalNormalizedState_", "content.control_!=object.originalContent_.get()", "entry.point.commandIndex==command", "entry.observation.revision()==state.revision()", "cell.initial.value==content.value_.value", "cell.initial.revision!=content.value_.revision", "matchesPending(*cell.pending,journal_)", "step.generation<=previous", "previous!=cell.pending->value().generation", "step.before!=ResourceState::CopyDst", "range->rangeCellCount(state.range())"]:
            self.assertIn(token,body)
        for forbidden in ["frameSerial", "resourceGeneration=", "MutationJournal journal", "makeShared", "makeUnique", "wgpu"]:
            self.assertNotIn(forbidden,body)
        runtime=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        recording=runtime[runtime.index("const char* WebGpuOrdinaryRecording::recordCanonical"):runtime.index("void WebGpuOrdinaryRecording::completeCanonical")]
        self.assertLess(recording.index("projection.joins->validateRetained(*seal)"),recording.index("wgpuDeviceCreateCommandEncoder"))
        self.assertIn("emitOriginalContentUpload",recording)
        emitter=self.read_source("src/rhi/backends/webgpu/webgpu_native_transfer_copy_lowering.h")
        ordinary=emitter[emitter.index("const rhi::BufferToTextureCopyDesc& desc,"):emitter.index("static bool emitOriginalContentUpload")]
        self.assertLess(ordinary.index("ordinaryContentAccessDiagnostic()"),ordinary.index("emitBufferTextureLayout"))
        content=emitter[emitter.index("static bool emitOriginalContentUpload"):emitter.index("static bool emitBufferTextureLayout")]
        self.assertIn("originalUploadExclusive()",content)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_upload_projection_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["completed->contentGeneration()==2u", "completed->revision()==proof->destinationContentExpectation().revision()+root->plannedContentUploadCount()+1u", "completed->contentGeneration()!=29u", "assert(pixel==20u)", "wgpuTextureDestroy", "RHI-CANONICAL-CONTENT-CONTROL-PENDING", "RHI-CANONICAL-CONTENT-EXPECTATION-STALE"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalTextureUploadExecutionPassed",runner)

    def test_cpu_quota_overlap_checks_pointer_and_controlblock_without_retained_list(self):
        source=CppSource((self.ROOT / "src/core/cpu_allocator.cpp").read_text(encoding="utf-8-sig"))
        body=source[source.index("bool cpuAllocationOverlapsQuotaOwner"):source.index("bool transferCpuAllocationCharge")]
        for token in ["inspectCpuAllocationCharge(exactAllocation).found", "original.get()==quota.get()", "original.owner_before(quota)", "quota.owner_before(original)"]:
            self.assertIn(token,body)
        for forbidden in ["makeShared", "makeUnique", "std::vector", "pmr::Vector"]:
            self.assertNotIn(forbidden,body)
        fixture=CppSource((self.ROOT / "tests/unit/bounded_retention_tests.cpp").read_text(encoding="utf-8-sig"))
        for token in ["testAllocationQuotaOwnerOverlap", "controlAlias", "pointerAlias", "allocationCount==allocations"]:
            self.assertIn(token,fixture)

    def test_ordered_upload_profile_reuses_complete_stream_and_independent_work(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_transfer_validation.h","src/rhi/semantics/command_lowering.h","original_command_ordered_upload")
        for token in ["OriginalCommandStreamProgram::inspect", "OriginalCommandStreamProgram::visitOriginal", "OriginalCommandUploadStagingContract::inspect", "OriginalTextureContentUploadValidation::inspect", "TextureUploadDestinationValidation::validate", "maxTextureWriteTexelBytes", "maxNativeCopyCommands", "result.required=pending"]:
            self.assertIn(token,source)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",source)
        for forbidden in ["backends/", "wgpu", "MutationJournal", "makeUnique", "makeShared", "resourceGeneration", "frameSerial"]:
            self.assertNotIn(forbidden,source)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_ordered_upload_contract_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("!inspect(bad).required.staging.totalBytes",fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& orderedUploadProfileContractPassed",runner)

    def test_canonical_upload_join_retains_exact_originals_without_native_work(self):
        header=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_upload_joins.h")
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        source=source[source.index("const char* WebGpuCanonicalUploadJoins::capture"):source.index("std::shared_ptr<WebGpuNativeCommandPayload> WebGpuNativeCommandPreparation::allocate")]
        for token in ["compareOriginalUpload(original)", "sameOwner(original,staging->originalPackingSource()->originalSource())", "sameOwner(*lease,sourceLease)", "RHI-WEBGPU-UPLOAD-JOIN-STAGING-NOT-IN-ORIGINAL-SOURCE", "entries[ordinal].commandIndex!=command", "core::SharedIdentityProjection::inspect", "core::SharedAllocationProjection::inspect", "authored.maxNativePayloadObjects", "authored.maxNativeStagingBytes", "sameOwnersAndLayout", "CpuMemoryQuotaScope scope(root->originalMetadataQuota())"]:
            self.assertIn(token,source)
        for forbidden in ["wgpuCommand", "wgpuQueueSubmit", "MutationJournal", "WebGpuRecordedCommandProof", "TextureUploadContentTransaction", "std::make_shared", "std::vector", "const_cast"]:
            self.assertNotIn(forbidden,source+header)
        self.assertIn("return false;",header)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_upload_joins_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.verify(peak,make)", "ORDER-COVERAGE", "abandonOriginalPendingOwnership", "verifyHotNoAllocation", "nativeExecutionProven", "retainedOwnerCount"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalUploadJoinsPassed",runner)

    def test_canonical_content_creation_and_plan_cannot_issue_native_completion(self):
        control=self.read_source("src/rhi/frontend/resource_texture_content_control.h")
        self.assertNotIn("MutationJournal journal_",control)
        self.assertIn("core::VersionedMutationValue<Value>",control)
        self.assertIn("core::MonotonicIdSequence",control)
        self.assertNotRegex(control,r"friend class .*Fixture")
        root=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        cpu=root.split("const char* commitOriginalOwnership() noexcept",1)[1].split("\n    }",1)[0]
        self.assertIn("RHI-CANONICAL-CONTENT-NATIVE-COMPLETION-REQUIRED",cpu)
        self.assertNotIn("complete(journal_)",cpu)
        self.assertEqual(root.count("core::MutationJournal journal_"),1)
        self.assertIn("cell.control->complete(journal_, plan_->contentBudget.contractVersion)",root)
        self.assertIn("OriginalTextureContentUploadValidation::inspect",root)
        creation=self.read_validation_section("src/rhi/frontend/resource_texture_validation.h","original_texture_content_creation")
        self.assertEqual(creation.count("-PAYLOAD-MISSING"),2)
        self.assertIn("Disposition::Prototype,true,false",creation)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_texture_content_plan_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("plannedContentGeneration(2u)==4u",fixture)
        self.assertIn("public-commit-refused",fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalContentCreationPassed",runner)

    def test_native_texture_creation_queries_exact_unbound_image_and_keeps_execution_closed(self):
        allocator = self.read_source("src/rhi/backends/vulkan/vulkan_allocator.cpp")
        create = allocator.split("VulkanRhiVmaImageAllocation VulkanRhiAllocator::createImage(", 1)[1]
        bounded = create.split("if (maxNativeAllocationBytes) {", 2)[-1].split("} else created", 1)[0]
        self.assertLess(bounded.index("vkGetImageMemoryRequirements"), bounded.index("created = vmaAllocateMemoryForImage"))
        self.assertLess(bounded.index("requirements.size > maxNativeAllocationBytes"), bounded.index("created = vmaAllocateMemoryForImage"))
        self.assertIn("vmaInfo.size > maxNativeAllocationBytes", bounded)
        self.assertLess(bounded.index("ledger_->budget.canAllocate(requirements.size)"),
                        bounded.index("created = vmaAllocateMemoryForImage"))
        self.assertIn("vmaBindImageMemory", bounded)
        self.assertIn("vmaDestroyImage", bounded)
        resources = self.read_source("src/rhi/backends/vulkan/vulkan_native_resources.cpp")
        query = resources.split("VulkanRhiNativeResourceAllocator::queryTextureAllocationBytes(", 1)[1].split("VulkanRhiNativeResourceResult", 1)[0]
        for token in ["VulkanTextureDescriptor::imageInfo(desc, 0u, VK_SAMPLE_COUNT_1_BIT)", "vkGetImageMemoryRequirements", "vkDestroyImage", "syntheticRuntime()"]:
            self.assertIn(token, query)
        for token in ["estimateTexture", "VK_IMAGE_CREATE_ALIAS_BIT", "vmaCreateImage"]:
            self.assertNotIn(token, query)
        native = self.read_source("src/rhi/backends/vulkan/vulkan_native_commands.cpp")
        for method in ["recordImageBarrier", "recordUploadStagingTextureCopy", "recordBufferToImageCopy", "recordImageToBufferCopy", "recordImageToImageCopy"]:
            bodies = native.split("bool VulkanRhiNativeCommandPayload::" + method + "(")[1:]
            self.assertTrue(bodies)
            for body in bodies:
                body = body.split("\n}", 1)[0]
                self.assertIn("backendVulkanOriginalContentExclusive()", body)
                self.assertIn("RHI-TEXTURE-CONTENT-EXCLUSIVE-USE", body)
        control = self.read_source("src/rhi/frontend/resource_texture_content_control.h")
        for method in ["canUpload", "propose", "complete"]:
            body = control.split("const char* " + method + "(", 1)[1].split("\n    }", 1)[0]
            self.assertIn("OriginalUndefinedTextureUploadPlanContract::validateControl", body)
            self.assertIn("budget_.contractVersion, planVersion", body)
        fixture = self.read_source("tests/fixtures/vulkan_original_texture_creation_fixture.h")
        for token in ["queryOriginalContentTextureAllocation", "--tooSmall.maxNativeAllocationBytes", "pressure.verify", "Uninitialized", "texture->state()==ResourceState::Undefined", "q[index]", "owner->liveBytes()"]:
            self.assertIn(token, fixture)

    def test_undefined_texture_upload_plan_keeps_original_graph_and_completion_closed(self):
        root = self.read_source("src/rhi/frontend/command_state.h")
        inspection = root.split("ContentCellsInspection inspectUndefinedTextureUploads(", 1)[1].split("const char* preflightContentPlan", 1)[0]
        for token in ["Contract::validateIntent", "graph_->stateContractVersion() != 1u",
                      "contentUploadAt(command)", "OriginalTextureContentUploadValidation::inspect",
                      "defaultTextureSubresourceRange", "transitionAt(other)", "other >= command",
                      "transitions != 1u", "graph_->textureEntries()", "Contract::validateInitial",
                      "SharedOwnerProjection::hasIndependentOwners", "maxCommandVisits", "maxProjectionVisits",
                      "maxContentControls", "maxUploadCommands", "maxContentSteps"]:
            self.assertIn(token, inspection)
        for forbidden in ["makeShared", "MutationJournal", "vkCmd", "writeBuffer", "->propose("]:
            self.assertNotIn(forbidden, inspection)
        control = self.read_source("src/rhi/frontend/resource_texture_content_control.h")
        for method in ["canUpload", "propose", "complete"]:
            body = control.split("const char* " + method + "(", 1)[1].split("\n    }", 1)[0]
            self.assertIn("OriginalUndefinedTextureUploadPlanContract::validateControl", body)
            self.assertIn("budget_.contractVersion, planVersion", body)
        profile = self.read_source("src/rhi/frontend/resource_texture_validation.h").split("class OriginalUndefinedTextureUploadPlanContract", 1)[1].split("class OriginalCompletedTextureReadPlanContract", 1)[0]
        self.assertIn("controlVersion == 3u && planVersion == 7u", profile)
        self.assertIn("controlVersion == 1u && planVersion == 1u", profile)
        self.assertIn("RHI-UNDEFINED-TEXTURE-UPLOAD-NATIVE-LIFECYCLE-UNAVAILABLE", self.read_source("src/rhi/frontend/command_recording.h"))
        fixture = self.read_source("tests/fixtures/original_undefined_texture_upload_plan_fixture.h")
        for token in ["verifyContract", "verifyRoot", "verifyHotNoAllocation", "pressure.verify", "DUPLICATE",
                      "plannedContentGeneration(2u) == 2u", "abandonOriginalPendingOwnership", "originalState->revision()"]:
            self.assertIn(token, fixture)

    def test_vulkan_original_texture_staging_requires_real_flush_and_same_inventory(self):
        native = self.read_source("src/rhi/backends/vulkan/vulkan_original_texture_upload.cpp")
        factory = native.split("VulkanOriginalTextureUploadStaging::prepareOriginal(", 1)[1].split("VulkanOriginalTextureUploadStaging::validateOriginal(", 1)[0]
        for token in ["Contract::validateIntent", "OriginalCommandTextureUploadPackingSource::validateOriginal",
                      "device.overlapsOriginalDeviceCpuOwner", "originalContentCpuQuota", "validateOriginalResourceRegistration",
                      "observeOriginalContent", "Uninitialized", "observeOriginalCommittedState", "ResourceState::Undefined",
                      "device.createBuffer", "MemoryUsage::CpuToGpu", "ResourceUsage::CopySrc",
                      "estimatedSizeBytes > b.maxNativeStagingBytes", "device.writeBuffer", "object->sealWrites()",
                      "OriginalCommandBufferLease::prepareOriginal", "core::CpuMemoryQuotaScope"]:
            self.assertIn(token, factory)
        self.assertLess(factory.index("device.writeBuffer"), factory.index("object->sealWrites()"))
        self.assertLess(factory.index("object->sealWrites()"), factory.index("OriginalCommandBufferLease::prepareOriginal"))
        device = self.read_source("src/rhi/backends/vulkan/vulkan_device.cpp")
        create = device.split("createBuffer(const rhi::BufferDesc& desc)", 1)[1].split("writeBuffer(", 1)[0]
        self.assertLess(create.index("createVulkanRhiBuffer"), create.index("resourceRegistry_->registerResource"))
        for token in ["Staging::validateOriginal", "inventory->originalLeaseIndex(staging->originalStagingLease())",
                      "inventory->originalLeaseIndex(packing->originalDestinationLease())",
                      "inventory->originalLeaseIndex(packing->originalSource())", "sameOwner(inputs, graph->originalInputs())",
                      "Contract::validateExecutionBudget", "prepareOriginalSubmissionFence", "pendingMemoryBarriers.reserve",
                      "seal->nativeColdCommandClaimed_ = true", "originalBindlessCustody = custody"]:
            self.assertIn(token, native)
        for forbidden in ["MutationJournal", "StagingUploadPool", "vkQueueWaitIdle", "vkDeviceWaitIdle", "initialized = true"]:
            self.assertNotIn(forbidden, native)

    def test_vulkan_original_upload_fence_only_commits_same_seal_after_real_completion(self):
        native = self.read_source("src/rhi/backends/vulkan/vulkan_original_texture_upload.cpp")
        pending = native.split("VulkanOriginalTextureUploadCommand::validatePending()", 1)[1].split("VulkanOriginalTextureUploadCommand::record(", 1)[0]
        for token in ["validateRetainedOwnership", "staging_->validateIdentity", "validateInitializedBufferPending",
                      "validateInitializedTexturePending", "uploadCommand_", "staging_->textureState_", "staging_->content_"]:
            self.assertIn(token, pending)
        self.assertNotIn("Staging::validateOriginal", pending)
        record = native.split("VulkanOriginalTextureUploadCommand::record(", 1)[1].split("VulkanOriginalTextureUploadCommand::beforeSubmit", 1)[0]
        for token in ["applyOriginalRecordingPlan", "validatePending", "recordValidatedOriginalImageBarrier",
                      "flushPendingBarriers", "TextureUploadPackedCopyMapping::mapOriginal",
                      "recordValidatedOriginalTextureCopy", "command.end", "RecordingValidatedAndDrained"]:
            self.assertIn(token, record)
        complete = native.split("VulkanOriginalTextureUploadCommand::fenceCompleted(", 1)[1]
        for token in ["command_ != &command", "syntheticRuntime()", "!command.impl_->submitted",
                      "!command.impl_->submitFenceCompleted", "QuarantinedAfterSubmit", "SubmittedQueueAndCallbacksDrained",
                      "validatePending()", "commitOriginalSubmission(4u)", "CompletionProven", "submissionUnproven(command)"]:
            self.assertIn(token, complete)
        self.assertLess(complete.index("!command.impl_->submitFenceCompleted"), complete.index("commitOriginalSubmission(4u)"))
        shared = self.read_source("src/rhi/backends/vulkan/vulkan_native_commands.cpp")
        for name in ["recordImageBarrier", "recordUploadStagingTextureCopy"]:
            wrapper = shared.split("bool VulkanRhiNativeCommandPayload::" + name + "(")[-1].split("\n}", 1)[0]
            self.assertIn("backendVulkanOriginalContentExclusive()", wrapper)
        header = self.read_source("src/rhi/backends/vulkan/vulkan_original_texture_upload.h")
        self.assertIn("ConstructionKey() = default", header)
        self.assertNotIn("friend class VulkanOriginalTextureUploadCommand", self.read_source("src/rhi/frontend/command_state.h"))

    def test_initialized_staging_contract_keeps_four_backends_and_no_generic_authority(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_transfer_validation.h","src/rhi/semantics/command_lowering.h","original_command_initialized_buffer_staging")
        for token in ["device.validateOriginalDeviceNativeLoss", "device.validateOriginalResourceRegistration", "buffer.originalCommittedStateDiagnostic()", "payloadBytes!=desc.sizeBytes", "buffer.allocation().estimatedSizeBytes!=payloadBytes", "buffer.hostWritesSealed()", "b.maxNativeBuffers!=1u", "if(!b.prototypeOptIn)"]:
            self.assertIn(token,source)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",source)
        for profile in ["CANONICAL-STAGING", "INDEX"]:
            for backend in ["VULKAN", "METAL"]:
                self.assertIn("RHI-INITIALIZED-"+profile+"-"+backend+"-PAYLOAD-MISSING",source)
        self.assertIn("Disposition::Prototype,true",source)
        for forbidden in ["wgpu", "MutationJournal", "WebGpuRecordedCommandProof", "CapabilityMode::Native"]:
            self.assertNotIn(forbidden,source)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.cpp")
        write=native[native.index("bool WebGpuNativeCompletionReceipt::writeBuffer"):native.index("bool WebGpuNativeCompletionReceipt::bind")]
        self.assertIn("queue.writeBuffer",write)
        self.assertIn("catch(error){return 0;}",write)

    def test_canonical_readback_result_contract_keeps_native_gates_closed(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_readback_result")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",contract)
        self.assertEqual(contract.count("-PAYLOAD-MISSING"),3)
        for token in ["SharedIdentityProjection::inspect", "SharedAllocationProjection::inspect", "CpuDomain::Result", "RHI-ORIGINAL-READBACK-RESULT-DESTINATION-ALIAS"]:
            self.assertIn(token,contract)
        for forbidden in ["wgpu", "makeUnique", "makeShared", "MutationJournal", "WebGpuRecordedCommandProof"]:
            self.assertNotIn(forbidden,contract)

    def test_canonical_readback_joins_do_not_bridge_legacy_or_native_authority(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_transfer_validation.h","src/rhi/semantics/command_lowering.h","original_command_readback_join")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",contract)
        self.assertIn("d.usage==ResourceUsage::CopyDst && d.memory==MemoryUsage::GpuToCpu",contract)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        joins=native.split("const char* WebGpuCanonicalReadbackJoins::capture",1)[1].split("bool WebGpuCanonicalUploadJoins::overlapsCpuOwner",1)[0]
        for token in ["sameOwner(original,input.source)", "wgpuBufferGetUsage", "WGPUBufferUsage_MapRead", "WebGpuNativeBufferMapState::matches", "OriginalCommandReadbackResultContract::inspect", "SharedAllocationProjection::inspect", "Lowering::lowerOriginal", "Lowering::validateOriginal", "core::CpuMemoryQuotaScope", "core::makeUnique"]:
            self.assertIn(token,joins)
        for forbidden in ["WebGpuOriginalStateTransaction", "WebGpuRecordedCommandProof", "wgpuQueueSubmit", "wgpuCommandEncoder", "wgpuBufferMapAsync", "commitOriginal", "friend class WebGpuCanonicalReadbackJoins"]:
            self.assertNotIn(forbidden,joins)
        header=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_readback_joins.h")
        self.assertNotIn("WebGpuOriginalTextureReadbackCopies",header)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalReadbackJoinsPassed && canonicalReadbackJoinsLossPassed",runner)

    def test_ordered_readback_profile_keeps_same_seal_and_sole_native_issuers(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_ordered_readback")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",contract)
        self.assertIn("TransferDependencyLowering::transition",contract)
        self.assertIn("D::Prototype,true,true,true,false,false",contract)
        joins=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_readback_joins.h")
        for token in ["validateRetained", "OriginalCommandRecordingSeal", "friend class WebGpuOrdinaryRecording"]:
            self.assertIn(token,joins)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertNotIn("friend class WebGpuCanonicalReadback",proof)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for token in ["readbackJoins->validateRetained(seal)", "inspectReadbacks", "ordered.readbackFirst+i", "readback.command!=originalOperation.commandIndex", "readback.join!=ordered.readbackJoin"]:
            self.assertIn(token,native)
        fixture=CppSource((self.ROOT / "tests/fixtures/webgpu_canonical_readback_joins_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["prepareOrderedLoss", "beginOrderedScopeUncertainty", "quotas10-zero", "originalRecordingSlotsDrained", "WebGpuNativeReadbackSeedFixture::verify"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["&& canonicalOrderedReadbackPassed", "orderedReadbackProbes.length===10", "orderedReadbackLoss.length===5", "canonicalOrderedReadbackPassed: canonicalOrderedReadbackPassed"]:
            self.assertIn(token,runner)

    def test_canonical_result_cold_storage_has_no_publication_or_legacy_authority(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_readback_result_storage")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return {true,false,false,D::ValidatorOnly",contract)
        snapshot=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_readback_result_snapshot")
        for token in ["core::pmr::Vector", "CpuMemoryQuotaScope result(quota)", "publishedBytes() const noexcept {return published_ ? bytes_.data() : nullptr;}", "SharedOwnerProjection::sameOwner(self->request_,request)"]:
            self.assertIn(token,snapshot)
        storage=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h")
        for token in ["core::BoundedSequence", "hasIndependentOwners", "overlapsOriginalDeviceCpuOwner", "overlapsCpuOwner(quota)", "snapshots_.findIf", "Admission::validateOriginal"]:
            self.assertIn(token,storage)
        self.assertNotIn("publishOriginalCommitted",storage.split("private:",1)[0])
        for forbidden in ["WebGpuOriginalStateTransaction", "WebGpuRecordedCommandProof", "wgpuBufferMapAsync", "wgpuQueueSubmit", "std::make_shared", "std::vector", "CompletionSignal::complete"]:
            self.assertNotIn(forbidden,storage+snapshot)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["&& canonicalResultStoragePassed", "canonicalResultStoragePassed: canonicalResultStoragePassed", "CANONICAL_RESULT_STORAGE_LOSS_OK"]:
            self.assertIn(token,runner)

    def test_canonical_readback_map_contract_cannot_issue_native_authority(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_readback_map")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":return",source)
        for token in ["required={}", "source.maxCallbackTickets", "source.maxCallbackRegistrations", "UINT64_MAX/participantsPerMap", "originalTickets>UINT64_MAX-callbacks", "phase==P::Published || phase==P::Retired", "RetainRootAndIssueMap", "KeepUnpublished"]:
            self.assertIn(token,source)
        for forbidden in ["backends/", "wgpu", "WebGpuRecordedCommandProof", "friend class", "makeShared", "makeUnique", "commitOriginal", "MutationJournal"]:
            self.assertNotIn(forbidden,source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalReadbackMapContractPassed",runner)

    def test_canonical_map_cold_profile_requires_source_capacity_and_same_result_admission(self):
        admission=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_cold_admission.h")
        for token in ["budget.contractVersion==5u", "OriginalCommandReadbackMapContract::deriveRequirements", "pending.capacity.callbackTickets=static_cast", "pending.capacity.callbackRegistrations=static_cast"]:
            self.assertIn(token,admission)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        custody=native.split("class WebGpuCanonicalCallbackCustody::RuntimeState",1)[1].split("const char* WebGpuCanonicalCallbackCustody::validate()",1)[0]
        for token in ["core::pmr::Vector<core::UniquePtr<Slot>>", "slots.resize(callbacks)", "i<slots.size()", "if(i==lossIndex)continue", "sameOwner(results->originalAdmission(),command->originalAdmission())", "requirements.readbackMap.maps", "budget.maxCallbackSlots,results"]:
            self.assertIn(token,custody)
        issuers=native.split("WebGpuCanonicalCallbackCustody::prepareOriginalIssuers(",1)[1].split("WebGpuOrdinaryRecording::recordCanonical",1)[0]
        self.assertIn("arp::rhi::semantics::OriginalCommandReadbackMapLowering::lowering",issuers)
        self.assertNotIn("wgpuBufferMapAsync",custody+issuers)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording", "WebGpuOrdinarySubmission"])
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["MAP_COLD_PRESSURE_OK", "MAP_COLD_LOSS_OK", "&& canonicalReadbackMapColdPassed", "canonicalReadbackMapColdPassed: canonicalReadbackMapColdPassed"]:
            self.assertIn(token,runner)

    def test_native_map_receipt_consumes_c_bridge_without_publication_authority(self):
        header=self.read_source("src/rhi/backends/webgpu/webgpu_native_buffer_map_receipt.h")
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.cpp")
        adapter=native.split("const char* WebGpuNativeBufferMapReceipt::prepareCold",1)[1].split("const char* WebGpuNativeCompletionReceipt::inspect",1)[0]
        for token in ["wgpuBufferMapAsync", "wgpuBufferReadMappedRange", "wgpuBufferUnmap", "WebGpuNativeBufferMapState::matches", "MAP-INVOCATION-UNCERTAIN", "MAP-UNMAP-UNCERTAIN", "MAP-UNMAP-UNPROVEN", "getWasmTableEntry", "phase_=Phase::Pending"]:
            self.assertIn(token,adapter)
        for forbidden in [".mapAsync(", "mMapState", "mPendingMapRequest", "bufferOnUnmaps", "makeUnique", "makeShared", "CompletionSignal", "publish", "commitOriginal", "WebGpuRecordedCommandProof"]:
            self.assertNotIn(forbidden,adapter)
        for token in ["OriginalCommand", "std::vector", "std::make_unique", "friend class"]:
            self.assertNotIn(token,header)
        cold=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        preparation=cold.split("WebGpuCanonicalCallbackCustody::prepareOriginal(",1)[1].split("const char* WebGpuCanonicalCallbackCustody::validate()",1)[0]
        self.assertIn("state->prepareOriginalMapReceipts",preparation)
        shared=cold.split("static Role roleAt",1)[1].split("bool foreignCallbacksDrained()",1)[0]
        for token in ["snapshot->originalRequest()", "snapshot->byteCapacity()", "map.map.prepareCold", "RuntimeState::consumeMapScope", "receipt.release()"]:
            self.assertIn(token,shared)
        for forbidden in ["map.start()", "map.readAndUnmap", "wgpuBufferMapAsync"]:
            self.assertNotIn(forbidden,preparation)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["canonicalNativeMapReceiptProbes.length===8", "&& canonicalNativeMapReceiptPassed", "canonicalNativeMapReceiptPassed: canonicalNativeMapReceiptPassed"]:
            self.assertIn(token,runner)

    def test_map_drain_cannot_replace_c_future_or_independent_publication(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_transfer_validation.h","src/rhi/semantics/command_lowering.h","original_command_readback_map_drain")
        for token in ["mapCallbacks==f.mapsIssued", "safeUnmaps==f.mapsIssued", "scopeCallbacksDrained==f.mapsIssued", "queueLifetimeProven || f.nativeLoss", "copiedMaps!=r.maps", "successfulScopes!=r.scopeCallbacks", "bool mayPublish=false", "arp::rhi::semantics::OriginalCommandReadbackMapLowering::lowering(backend)"]:
            self.assertIn(token,contract)
        for forbidden in ["friend class", "wgpu", "backends/", "makeShared", "makeUnique", "commitOriginal", "CompletionSignal", "WebGpuRecordedCommandProof"]:
            self.assertNotIn(forbidden,contract)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalMapDrainContractPassed",runner)

    def test_retained_mapping_validation_keeps_exact_receipt_request_and_seal(self):
        storage=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h")
        retained=storage.split("static const char* validateOriginalRetained",1)[1].split("template<class Visitor>",1)[0]
        for token in ["hasQuotaBoundSharedOwner(self)", "hasQuotaBoundSharedOwner(seal)", "sameOwner(seal,reservation->originalSeal())", "Status::Pending", "journalEmpty()", "bool(request)!=bool(receipt)", "validateSnapshots()"]:
            self.assertIn(token,retained)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        capture=native.split("WebGpuCanonicalReadbackJoins::capture",1)[1].split("WebGpuCanonicalReadbackJoins::inspect",1)[0]
        for token in ["sameOwner(active->request,original->originalSource()->originalRequest())", "receipt.validateRetainedBinding", "sizeBytes>SIZE_MAX", "else if(!WebGpuNativeBufferMapState::matches(buffer->buffer,WGPUBufferMapState_Unmapped))"]:
            self.assertIn(token,capture)
        adapter=self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.cpp").split("WebGpuNativeBufferMapReceipt::validateRetainedBinding",1)[1].split("WebGpuNativeInvocation::guarded",1)[0]
        for token in ["buffer_!=buffer", "!range_.matches(range)", "invocationUncertain_", "futureIssued_", "!success_", "Phase::Pending ? WGPUBufferMapState_Pending", "WGPUBufferMapState_Unmapped : WGPUBufferMapState_Mapped"]:
            self.assertIn(token,adapter)
        for forbidden in ["commitOriginal", "publishOriginal", "wgpuBufferMapAsync", "wgpuQueueSubmit", "makeShared", "makeUnique"]:
            self.assertNotIn(forbidden,retained+capture+adapter)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& canonicalRetainedMapValidationPassed",runner)

    def test_canonical_map_executor_retains_same_state_and_keeps_result_unpublished(self):
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        state=native.split("class WebGpuCanonicalCallbackCustody::RuntimeState",1)[1].split("WebGpuCanonicalCallbackCustody::WebGpuCanonicalCallbackCustody",1)[0]
        for token in ["submissionProven", "mapIssuanceActive", "mapDrainActive", "mapDrainDecision()", "RetireTerminal", "readOriginalUnpublished(owner->results", "slot->map.nativeUnmapProven()", "destroyAndDrainAfterNativeLoss()", "scope.scopeIssued=true", "map.mapIssued=true;++nextMap", "foreignCallbacksDrainedExceptLoss()"]:
            self.assertIn(token,state)
        hot=state.split("void advanceMaps()",1)[1].split("void quarantine",1)[0]
        for forbidden in ["makeShared", "makeUnique", "resize(", "push_back(", "wgpuQueueSubmit", "commitOriginalSubmission", "publishedBytes"]:
            self.assertNotIn(forbidden,hot)
        finish=native.split("void WebGpuOrdinarySubmission::finishCanonicalSubmission()",1)[1]
        for token in ["state->results", "state->submissionProven=true", "state->advanceMaps()", "Action::RetireUnpublished", "validateRetainedResults", "seal->commitOriginalSubmission()"]:
            self.assertIn(token,finish)
        snapshot=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_readback_result_snapshot")
        public=snapshot.split("const auto& originalRequest()",1)[1].split("private:",1)[0]
        self.assertIn("publishedBytes() const noexcept {return published_ ? bytes_.data() : nullptr;}",public)
        for forbidden in ["void*", "readAndUnmap", "write", "publishOriginal"]:
            self.assertNotIn(forbidden,public)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["canonicalMapExecutionProbes.length===7", "canonicalMapExecutionLossProbes.length===4", "&& canonicalMapExecutionPassed"]:
            self.assertIn(token,runner)

    def test_snapshot_publication_validator_cannot_issue_native_authority(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_transfer_validation.h","src/rhi/semantics/command_lowering.h","original_command_readback_snapshot_publication")
        for token in ["validateIntent(intent)", "OriginalCommandReadbackMapDrainContract::inspect(required,drain)", "RetireUnpublished", "originalSealAndRootCommitted", "originalJournalEmpty", "validatedOriginalRequests!=required.maps", "independentResultQuota", "unpublishedSnapshots!=required.maps", "terminalObserved", "bool publicationAvailable=false"]:
            self.assertIn(token,contract)
        for forbidden in ["friend class", "wgpu", "backends/", "makeShared", "makeUnique", "commitOriginal", "WebGpuRecordedCommandProof", "CompletionSignal"]:
            self.assertNotIn(forbidden,contract)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend+":",contract)

    def test_canonical_publication_consumes_private_commit_and_freezes_all_snapshots(self):
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        freeze=native.split("WebGpuCanonicalReadbackResultStorage::publishOriginalCommitted",1)[1].split("WebGpuCanonicalReadbackJoins::capture",1)[0]
        for token in ["hasQuotaBoundSharedOwner(self)", "hasQuotaBoundSharedOwner(seal)", "sameOwner(seal,reservation->originalSeal())", "contractVersion!=5u", "validateSnapshots()", "Status::Committed", "journalEmpty()", "Publication::inspect", "Action::FreezeIndependentSnapshots", "snapshot->published_=true"]:
            self.assertIn(token,freeze)
        for forbidden in ["makeShared", "makeUnique", "wgpu", "memcpy", "CompletionSignal", "WebGpuRecordedCommandProof"]:
            self.assertNotIn(forbidden,freeze)
        finish=native.split("void WebGpuOrdinarySubmission::finishCanonicalSubmission()",1)[1]
        self.assertLess(finish.index("seal->commitOriginalSubmission()"),finish.index("state->publishCommittedResults(seal)"))
        self.assertLess(finish.index("state->publishCommittedResults(seal)"),finish.index("state->submissionCompleted=true"))
        header=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_callback_custody.h")
        self.assertIn("std::optional<PublicationIntent> publication=std::nullopt",header)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for token in ["canonicalSnapshotPublicationProbes.length===7", "&& canonicalSnapshotPublicationPassed", "CANONICAL_SNAPSHOT_SURVIVOR_LOSS_OK"]:
            self.assertIn(token,runner)

    def test_original_typed_declarations_cannot_issue_native_or_factory_authority(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_usage.h","src/rhi/semantics/command_lowering.h","original_command_typed_source")
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_typed_input")
        for token in ["CanonicalOriginalTypedSources/1", "validateRegisteredLifetime", "registrationGeneration", "hasIndependentOwners", "maxTypedNativeCommandCalls", "SOURCE-AUTHORED-NATIVE-BUDGET", "TABLE-ACCESS-UNDECLARED", "LOWERING-UNBOUND", "BUFFER-RANGE", "executionAvailable=false"]:
            self.assertIn(token,source)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend,source)
        for token in ["CommandUsageContract::validReferences", "CommandVariableArguments::validateInput", "validateExactDescriptorSetLayout", "validateDynamicOffsetArguments", "validateQueryDeclaration", "validatePipelineKind"]:
            self.assertIn(token,inputs)
        for forbidden in ["wgpu", "friend class", "WebGpuRecordedCommandProof", "commitOriginal", "nativeHandle", "makeShared", "makeUnique", "std::vector", "std::string ", "return new "]:
            self.assertNotIn(forbidden,source+inputs)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_typed_source_contract_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyOriginalResources", "REGISTRATION-FOREIGN-DEVICE", "ORIGINAL-SOURCES-OWNER", "DESCRIPTOR-GENERATION", "UINT64_MAX", "arena->reset()", "lowering(rhi::Backend::WebGPU).sourceAdapterAvailable"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalTypedSourceContractPassed",runner)
        smoke=CppSource((self.ROOT / "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp").read_text(encoding="utf-8-sig"))
        self.assertIn("OriginalCommandTypedSourceContractFixture::verifyOriginalResources",smoke)

    def test_original_pipeline_metadata_copy_retains_core_nested_owners_without_factory_proof(self):
        copies=self.read_source("src/rhi/frontend/pipeline_state_copy.h").split("// Preserve each nested core resource during descriptor copying.",1)[0]
        for token in ["class PipelineLayoutStateCopy final", "class ShaderReflectionStateCopy final", "class ShaderPackageStateCopy final", "core::pmr::Vector<DescriptorLayoutDesc>(resource)", "core::pmr::String(resource)", "result.entrypoints.resize", "result.pushConstantMembers.resize", "result.reflection.pushConstantMembers.resize"]:
            self.assertIn(token,copies)
        for forbidden in ["std::vector", "std::string ", "makeShared", "nativePayload", "shareOriginal", "wgpu", "commitOriginal", "friend class"]:
            self.assertNotIn(forbidden,copies)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_pipeline_metadata_copy_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["std::pmr::null_memory_resource()", "pressure.measure", "pressure.verify(layoutPeak", "pressure.verify(packagePeak", "pressure.verify(reflectionPeak", "metadata-not-factory-proof", "arrayDims.data()!=", "source.descriptorSets[0].layoutName=\"changed\""]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalPipelineMetadataCopyPassed",runner)

    def test_original_pipeline_factory_receipts_reuse_registry_and_keep_typed_execution_closed(self):
        registry=self.read_source("src/rhi/frontend/resource_registration.h")
        for token in ["maxPipelineLayouts", "maxGraphicsPipelines", "maxComputePipelines", "perPipelineOwnerCount=4u", "matchesObjectIdentity", "class OriginalRegisteredObject", "TYPED-VERSION", "pipelineLifetimeIssuerAvailable=false", "hasResourceId(kind) && record"]:
            self.assertIn(token,registry)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        for token in ["createOriginalPipelineLayout", "createOriginalRenderPipeline", "createOriginalComputePipeline", "sharedOwnerMatches(layout)", "ShaderPackageStateCopy::copy", "originalLayoutOwner_=layout", "candidate->validationReport().success", "RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING"]:
            self.assertIn(token,device)
        factory=device.split("createOriginalPipelineLayout(const",1)[1].split("virtual arp::core::UniquePtr<DescriptorArena>",1)[0]
        self.assertLess(factory.index("reserveOriginalPipelineRegistration"),factory.index("auto candidate=createPipelineLayout"))
        pipeline=device.split("template<class Pipeline,class Desc,class Factory>",1)[1].split("template<class Control>",1)[0]
        self.assertLess(pipeline.index("reserveOriginalPipelineRegistration"),pipeline.index("auto candidate=factory"))
        self.assertLess(pipeline.index("ShaderPackageStateCopy::copy"),pipeline.index("auto candidate=factory"))
        for forbidden in ["wgpu", "nativeHandle", "commitOriginal", "WebGpuRecordedCommandProof", "CpuReplay"]:
            self.assertNotIn(forbidden,factory+pipeline)
        for path in ["null/null_pipelines.cpp", "vulkan/vulkan_pipeline.cpp", "metal/metal_backend_objects.cpp", "webgpu/webgpu_backend_objects.cpp"]:
            self.assertIn("PipelineLayoutStateCopy::copy(desc)",CppSource((self.ROOT / "src/rhi/backends" / path).read_text(encoding="utf-8-sig")))
        fixture=CppSource((self.ROOT / "tests/fixtures/original_pipeline_factory_registration_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyLayout", "verifyPipelines", "verifyWebGpuFacadeAndLossGate", "pressure.measure", "pressure.verify", "verifyHotNoAllocation", "NATIVE-LOSS-JOIN-MISSING", "OWNER-CAPACITY", "FACTORY-FAILED", "caller-mutated", "allocationIdentity==allocation.allocationIdentity"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalPipelineFactoryLayoutPassed && originalPipelineFactoryWebGpuPassed",runner)

    def test_original_descriptor_factories_bind_genuine_epoch_and_preserve_complete_content(self):
        registry=self.read_source("src/rhi/frontend/resource_registration.h")
        for token in ["maxDescriptorArenas", "maxDescriptorSets", "maxDescriptorTables", "perDescriptorOwnerCount=3u", "descriptorLifetimeIssuerAvailable=false", "DESCRIPTOR-VERSION", "ALLOCATION-STALE", "allocationEpoch_.isCurrent()"]:
            self.assertIn(token,registry)
        descriptors=self.read_source("src/rhi/gpu_descriptors.h")
        for token in ["DescriptorContentObservation final", "const Write write_", "const std::uint64_t revision_", "core::LifetimeObserver<const Descriptor>", "successfulBindingWrite", "successfulDescriptorWrite", "originalArenaOwner_", "CONTENT-CHANGED", "CONTENT-EXPIRED", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,descriptors)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        for token in ["createOriginalDescriptorArena", "allocateOriginalDescriptorSet", "allocateOriginalDescriptorTable", "reserveOriginalDescriptorRegistration", "sameCurrentEpoch(record->allocationEpoch_)", "arena->sharedOwnerMatches(arena)", "candidate->originalArenaOwner_=arena", "RHI-ORIGINAL-DESCRIPTOR-NATIVE-LOSS-JOIN-MISSING"]:
            self.assertIn(token,device)
        for path in ["null/null_descriptors.cpp", "vulkan/vulkan_descriptors.cpp", "metal/metal_backend_descriptors.cpp", "webgpu/webgpu_backend_descriptors.cpp"]:
            source=CppSource((self.ROOT / "src/rhi/backends" / path).read_text(encoding="utf-8-sig"))
            for token in ["successfulBindingWrite", "successfulDescriptorWrite", "return generation_.ticket();", "core::withMutationRollback", "DescriptorStateCopy::copy(desc)"]:
                self.assertIn(token,source)
            self.assertNotIn("std::pmr::vector",source)
        null=self.read_source("src/rhi/backends/null/null_descriptors.cpp")
        self.assertIn("bindings_[elementIndex] = { true, write }",null)
        vk=self.read_source("src/rhi/backends/vulkan/vulkan_descriptors.cpp")
        self.assertIn("writes_[write.index] = write",vk)
        self.assertNotIn("resources_[write.index] = write.resource",vk)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_descriptor_factory_content_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["wrapperPressure.verify", "pressure.verify(arenaPeak", "verifyOwnerAndVersion", "verifyNativeLossGate", "std::pmr::null_memory_resource()", "sameCurrentEpoch(foreignEpoch.ticket())", "descriptorBindingWriteMatches", "content-not-factory-or-native-proof"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalDescriptorFactoryPassed && descriptorContentPassed && originalDescriptorLossGatePassed",runner)

    def test_original_descriptor_freeze_cannot_issue_canonical_or_native_authority(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_source")
        for token in ["core::ColdOwnedObjectSlot<Descriptor>", "core::BoundedSequence<FrozenElement>", "shareOriginal(std::move(result))", "hasQuotaBoundSharedOwner(self)", "RHI-ORIGINAL-DESCRIPTOR-SOURCE-INCOMPLETE", "RHI-ORIGINAL-DESCRIPTOR-SOURCE-WRITE-MISMATCH", "positionalAdapterAvailable=false,executionAvailable=false", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        dependencies=self.read_validation_section("src/rhi/frontend/command_descriptor_dependencies.inl","original_command_descriptor_dependencies")
        for token in ["sameCurrentEpoch(arena->allocationEpochTicket())", "matchesParent(arena->originalResourceRegistration())", "validateSetElements", "validateTableElements", "SharedOwnerProjection::sameOwner"]:
            self.assertIn(token,dependencies)
        self.assertLess(source.index("shareOriginal(std::move(result))"),source.index("object_.adoptOriginal"))
        for prohibited in ["WebGpuRecordedCommandProof", "MutationJournal", "OrdinaryCommand", "std::vector", "std::make_shared", "const_cast", "GpuResourceId resource()"]:
            self.assertNotIn(prohibited,source)
        self.assertNotRegex(source,r"\b(?:wgpu|vk|MTL)\w+\s*\(")
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])
        fixture=CppSource((self.ROOT / "tests/fixtures/original_descriptor_source_freeze_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.verifySingleUse", "verifyHotNoAllocation", "std::pmr::null_memory_resource()", "retainedCharge.allocationIdentity==originalCharge.allocationIdentity", "RHI-ORIGINAL-DESCRIPTOR-SOURCE-PARENT", "RHI-DESCRIPTOR-CONTENT-CHANGED", "RHI-ORIGINAL-REGISTRATION-ALLOCATION-STALE"]:
            self.assertIn(token,fixture)

    def test_original_typed_assembly_preserves_canonical_acyclic_publication(self):
        sources=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        descriptor=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_source")
        usage=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_usage")
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_source_assembly_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["prepareColdOriginalTyped", "appendOriginalTyped", "sealForGraph", "SharedIdentityProjection", "RHI-ORIGINAL-SOURCES-ASSEMBLY-SEALED", "RHI-ORIGINAL-SOURCES-ASSEMBLY-FOREIGN-INVENTORY"]:
            self.assertIn(token,sources)
        self.assertIn("std::weak_ptr<const OriginalCommandResourceSources>",descriptor)
        self.assertIn("matchesCanonicalSources",descriptor)
        self.assertIn("sealForGraph()",usage)
        for token in ["root.get()==exactRoot", "verifyHotNoAllocation", "rootObserver.expired()", "failed-publication-cold", "RHI-COMMAND-ORDER-PIPELINE", "RHI-ORIGINAL-TYPED-DESCRIPTOR-STATE-JOIN-MISSING", "owners14-records6-total23"]:
            self.assertIn(token,fixture)
        for section in ["original_command_resource_states", "original_command_buffer_states"]:
            self.assertIn("sources->typedStateJoinDiagnostic()",self.read_validation_section("src/rhi/frontend/command_state.h",section))
        for token in ["RHI-ORIGINAL-TYPED-DESCRIPTOR-STATE-JOIN-MISSING", "RHI-ORIGINAL-TYPED-PIPELINE-STATE-JOIN-MISSING"]:
            self.assertIn(token,sources)
        self.assertIn("RHI-ORIGINAL-TYPED-INPUT-POSITIONAL-JOIN-MISSING",self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs"))
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalTypedAssemblyPassed &&",runner)
        self.assertIn("originalTypedAssemblies.length===2",runner)

    def test_original_pipeline_full_definition_is_frozen_before_backend_creation(self):
        registry=self.read_source("src/rhi/frontend/resource_registration.h")
        for token in ["perFullDefinitionPipelineOwnerCount=5u", "fullPipelineDefinitionCustodyAvailable=false", "version>=4u", "ownerCount(kind,registry_->budget_.contractVersion)"]:
            self.assertIn(token,registry)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h").split("template<class Pipeline,class Desc,class Factory>",1)[1].split("template<class Control>",1)[0]
        for token in ["RenderPipelineStateCopy::copy(desc)", "ComputePipelineStateCopy::copy(desc)", "definition->shaderPackage=package.get()", "factory(definition ? *definition : frozen)", "originalRenderDefinition_=std::move(definition)", "originalComputeDefinition_=std::move(definition)"]:
            self.assertIn(token,device)
        self.assertLess(device.index("definition=core::makeShared"),device.index("auto candidate=factory"))
        pipeline=self.read_rhi_api_and_implementation("src/rhi/gpu_pipeline.h")
        for token in ["originalFullDefinitionDiagnostic", "originalRenderPipelineDefinition", "originalComputePipelineDefinition", "RHI-ORIGINAL-PIPELINE-FULL-DEFINITION-MISSING"]:
            self.assertIn(token,pipeline)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_pipeline_full_definition_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyContractAndCopies", "verifyCompute", "verifyHost", "OWNER-CAPACITY", "pressure.measure", "pressure.verify", "verifyHotNoAllocation", "allocationIdentity==allocation.allocationIdentity", "std::pmr::null_memory_resource()", "artifact-native-closed"]:
            self.assertIn(token,fixture)
        self.assertIn("&& originalPipelineFullDefinitionPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_original_shader_artifact_publication_is_quota_bound_after_all_checks(self):
        for name in ["single_entry_shader_artifact", "graphics_shader_module_artifact"]:
            header=CppSource((self.ROOT / ("src/shader/"+name+".h")).read_text(encoding="utf-8-sig"))
            loader=CppSource((self.ROOT / ("src/shader/"+name+".cpp")).read_text(encoding="utf-8-sig"))
            self.assertIn("public core::QuotaBoundSharedObjectIdentity",header)
            self.assertIn("hasQuotaBoundSharedOwner(self)",header)
            self.assertIn("nativeExecutionProven() const noexcept {return false;}",header)
            self.assertNotIn("quota_;",header)
            self.assertIn("core::makeUnique<",loader)
            self.assertIn("shareOriginal(std::move(result))",loader)
            self.assertLess(loader.index("REFLECTION-FILE"),loader.index("shareOriginal(std::move(result))"))
            for forbidden in ["WebGpuRecordedCommandProof", "MutationJournal", "nativeHandle", "std::vector", "std::make_shared"]:
                self.assertNotIn(forbidden,header+loader)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_shader_artifact_owner_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyHotNoAllocation", "forged", "nonowning", "missing", "validateOriginal", "consumption-native-closed"]:
            self.assertIn(token,fixture)
        self.assertIn("&& originalArtifactOwnerPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_original_pipeline_artifact_memory_input_is_genuine_and_path_independent(self):
        header=self.read_source("src/rhi/frontend/pipeline_single_entry_input.h")
        source=self.read_source("src/rhi/frontend/pipeline_single_entry_input.cpp")
        for token in ["QuotaBoundSharedObjectIdentity", "hasQuotaBoundSharedOwner(self)", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,header)
        for token in ["populateSingleEntryShaderInterface", "shareOriginal(std::move(result))", "sameControlBlock", "contractVersion!=1u"]:
            self.assertIn(token,source)
        for forbidden in ["readBinaryFile", "loadFromFile", "loadFromYaml", "nativeHandle", "std::make_shared", "std::vector"]:
            self.assertNotIn(forbidden,header+source)
        lowering=self.read_source("src/rhi/backends/webgpu/webgpu_binding_space_lowering.cpp")
        memory=lowering.split("WebGpuBindingSpaceLoweringLoadResult loadOriginalWebGpuBindingSpaceLowering(",1)[1].split("WebGpuBindingSpaceLoweringLoadResult loadBindless",1)[0]
        self.assertIn("selectTarget(input,rhi::Backend::WebGPU)",memory)
        self.assertIn("lowerWebGpuBindingSpaceReflection(input->package(),input->artifact()->module())",memory)
        self.assertIn("lowerWebGpuBindingSpaceReflection(package,cached.module)",lowering)
        for forbidden in ["loadFromFile", "loadFromYaml", "readTextFile"]: self.assertNotIn(forbidden,memory)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_single_entry_pipeline_input_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "verifyHotNoAllocation", "verifyFileLowering", "sameOwner", "consumption-native-closed"]:
            self.assertIn(token,fixture)
        self.assertIn("&& originalArtifactMemoryInputPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_original_artifact_compute_consumption_uses_shared_native_emitters(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        factory=source.split("WebGpuOriginalArtifactComputeFactory::create(",1)[1].split("WebGpuOriginalArtifactComputeFactory::validateConsumedOriginal(",1)[0]
        for token in ["selectTarget(input,rhi::Backend::WebGPU)", "emitWebGpuNativeShaderModule", "emitWebGpuNativeComputePipeline", "target.bytes", "CpuMemoryQuotaScope", "validateBudget", "sourceSha256", "nativeDeviceAvailable"]:
            if token == "nativeDeviceAvailable": self.assertIn("webGpuNativeDeviceAvailable()",factory)
            else: self.assertIn(token,factory)
        for forbidden in ["readTextFile", "loadFromFile", "loadFromYaml", "loadWebGpuBindingSpaceLowering("]: self.assertNotIn(forbidden,factory)
        self.assertGreaterEqual(source.count("return emitWebGpuNativeComputePipeline("),1)
        ownership=self.read_source("src/rhi/backends/webgpu/webgpu_pipeline_ownership.h")
        self.assertIn("std::shared_ptr<const rhi::OriginalSingleEntryPipelineInput> compute;",ownership)
        for token in ["core::VersionedOwnedValue", "const SourceCustody sources_", "sources_.ordinary().compute", "sources_.expanded()->space()->computeInput()", "ExpandedBindingFact", "semantics.contractVersion!=1u", "semantics.contractVersion!=2u"]: self.assertIn(token,ownership)
        self.assertIn("friend class WebGpuOriginalArtifactComputeFactory",ownership)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_artifact_compute_consumption_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "verifyHotNoAllocation", "verifyActualLoss", "sameOwner", "quotas3-zero"]: self.assertIn(token,fixture)
        self.assertIn("&& originalArtifactComputeConsumptionPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))
        self.assertIn("RHI-WEBGPU-ORIGINAL-ARTIFACT-PIPELINE-ORDINARY-BRIDGE",source)
        self.assertIn("RHI-ORIGINAL-ARTIFACT-CREATION-RECEIPT-MISSING",self.read_validation_section("src/rhi/frontend/pipeline_creation_validation.h","original_pipeline_artifact_consumption"))

    def test_original_artifact_creation_joins_real_native_scopes_without_command_authority(self):
        actor=self.read_source("src/rhi/backends/webgpu/webgpu_pipeline_preparation.h", "backends_webgpu_webgpu_original_artifact_compute_creation_h")
        selected=self.read_validation_section("src/rhi/frontend/pipeline_creation_validation.h","original_depth_table_expansion_creation")
        for token in ["OriginalArtifactCreationBudgetValidation::validateEnvelope", "NativeEmissionContract::validateBudget",
                      "Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU", "emission.emissionAvailable"]:
            self.assertIn(token,selected)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_pipeline_preparation.h", "backends_webgpu_webgpu_original_artifact_creation_source_h")
        for token in ["VersionedSharedOwner<Request,Input,Definition,2u>", "owner->space()->computeInput()",
                      "owner->space()->graphicsInput()", "sizeof(Request)==sizeof(OrdinaryBudget)"]:
            self.assertIn(token,source)
        for profile in ["compute", "graphics"]:
            selected_actor=self.read_webgpu_source(f"webgpu_original_artifact_{profile}_creation.h")
            for token in ["prepareTableExpansion", "cancelTableExpansion", "matchesCurrentDevice", "ExpansionValidation::query",
                          "createTableExpansion", "validateConsumedTableExpansion", "pipeline_.reset()", "const Source source_"]:
                self.assertIn(token,selected_actor)
        for token in ["OriginalComputePipelineCreation", "hasIndependentOwners", "CreationReceiptLifecycle", "WebGpuNativeErrorScopeBatch::push", "callbacks_.pop", "WebGpuOriginalArtifactComputeFactory::create", "sameOwner", "shareOriginal", "preinitializedWebGPUDevice", "QUOTA-LIMIT", "CANONICAL-JOIN-MISSING", "lifecycle_.observe"]:
            self.assertIn(token,actor)
        support=self.read_source("src/rhi/backends/webgpu/webgpu_pipeline_preparation.h", "backends_webgpu_webgpu_artifact_creation_scope_support_h")
        for token in ["WebGpuNativeErrorScopeBatch::pop", "WGPUPopErrorScopeStatus_Success", "WGPUErrorType_NoError", "core::makeUnique<Callback>", "observeCreationScope", "webgpu_pipeline_preflight.h"]:
            self.assertIn(token,support)
        methods=self.read_source("src/rhi/backends/webgpu/webgpu_pipeline_preflight.h", "backends_webgpu_webgpu_artifact_creation_native_methods_h")
        for token in ["createComputePipeline", "createRenderPipeline", "popErrorScope", "pushErrorScope", "webGpuNativeDeviceLost", "webGpuNativeDeviceAvailable"]:
            self.assertIn(token,methods)
        lifecycle=CppSource((self.ROOT / "src/core/runtime/creation_receipt_lifecycle.h").read_text(encoding="utf-8-sig"))
        for token in ["CompletionEvidenceBatch", "evidence_.observe", "evidence_.allReceived()", "validate()", "if(!failure_)"]:
            self.assertIn(token,lifecycle)
        for forbidden in ["WebGpuRecordedCommandProof", "MutationJournal", "wgpuQueueSubmit", "std::make_shared", "std::vector", "readTextFile"]:
            self.assertNotIn(forbidden,actor)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_artifact_compute_creation_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "verifyHotNoAllocation", "verifyActualLoss", "missingOriginalArtifactEntry", "pop rejection", "Promises::releaseScope(2u)", "quotas3-zero"]:
            self.assertIn(token,fixture)
        self.assertIn("&& originalArtifactComputeCreationPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_original_accepted_compute_factory_has_sealed_l2_receipt_and_reuses_exact_payload(self):
        pipeline=self.read_rhi_api_and_implementation("src/rhi/gpu_pipeline.h")
        receipt=pipeline.split("class OriginalComputePipelineCreation :",1)[1].split("struct PipelineValidationReport",1)[0]
        for token in ["QuotaBoundSharedObjectIdentity", "OriginalComputePipelineCreation()=default", "hasQuotaBoundSharedOwner", "virtual const char* validate()", "usesCpuQuotaDomain"]: self.assertIn(token,receipt)
        for forbidden in ["nativeHandle", "WebGPU", "WGPU", "nativePayload"]: self.assertNotIn(forbidden,receipt)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        for token in ["createOriginalArtifactComputePipeline", "REGISTRY-VERSION", "usesCpuQuotaDomain", "createAcceptedOriginalComputePipeline", "originalComputeCreation_=std::move(creation)"]: self.assertIn(token,device)
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_backend_objects.cpp")
        factory=backend.split("WebGpuAcceptedOriginalComputePipelineFactory::create(",1)[1].split("arp::core::UniquePtr<rhi::RenderPipeline> createWebGpuRhiRenderPipeline",1)[0]
        for token in ["WebGpuAcceptedOriginalPipelineValidation::validate", "validateWebGpuRhiComputePipelineImpl", "actor->pipeline_", "ShaderReflectionStateCopy::copy"]: self.assertIn(token,factory)
        for forbidden in ["createShaderModule", "createWebGpuNativeComputePipelinePayload", "readTextFile", "loadWebGpuBindingSpaceLowering(", "WebGpuRecordedCommandProof"]: self.assertNotIn(forbidden,factory)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_accepted_compute_pipeline_factory_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "verifyHotNoAllocation", "arpAcceptedRecreations===0", "verifyActualLoss", "quotas7-zero"]: self.assertIn(token,fixture)
        self.assertIn("&& originalAcceptedComputeFactoryPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))
        payloads=self.read_source("src/rhi/backends/webgpu/webgpu_native_payloads.h")
        compute_release=payloads.split("~WebGpuNativeComputePipelinePayload()",1)[1].split("class WebGpuNativeZeroBaseIndirectProducer",1)[0]
        self.assertNotIn("webGpuNativeDeviceLost()",compute_release)
        self.assertIn("wgpuComputePipelineRelease(pipeline)",compute_release)
        self.assertIn("wgpuPipelineLayoutRelease(layout)",compute_release)
        self.assertIn("arpAcceptedLossObjects.some(p=>wrappers.includes(p))",fixture)
        allocator=self.read_source("src/rhi/semantics/native_pipeline_state_lowering.h")
        self.assertIn("core::pmr::Vector<NativeComputePipelineStateSemantic> semantics { &core::defaultCpuMemoryResource() }",allocator)

    def test_original_graphics_memory_input_preserves_combined_and_selected_targets(self):
        header=self.read_source("src/rhi/frontend/pipeline_graphics_input.h")
        source=self.read_source("src/rhi/frontend/pipeline_graphics_input.cpp")
        for token in ["QuotaBoundSharedObjectIdentity", "hasQuotaBoundSharedOwner(self)", "sourceCount=2u", "combinedModule=true", "validatorOnly=true", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,header)
        for token in ["populateGraphicsModuleShaderInterface", "sameControlBlock", "shareOriginal", "contractVersion!=1u"]:
            self.assertIn(token,source)
        for forbidden in ["loadFromYaml", "loadFromFile", "readBinaryFile", "nativeHandle", "std::vector", "std::make_shared"]:
            self.assertNotIn(forbidden,header+source)
        merge=self.read_source("src/rhi/frontend/pipeline_package_reflection.cpp")
        for token in ["GraphicsShaderInterfaceMerger::merge", "pushConstantWebGpuBindingGroup", "pushConstantWebGpuBindingIndex", "ShaderPushConstantAbiIdentity::matchesMembers", "RHI-GRAPHICS-MERGE-LOGICAL-BINDING", "ShaderPackageStateCopy::copy"]:
            self.assertIn(token,merge)
        for backend in ["null/null_pipelines.cpp", "vulkan/vulkan_pipeline.cpp", "metal/metal_backend_pipelines.cpp", "webgpu/webgpu_binding_space_lowering.cpp"]:
            self.assertIn("ShaderPushConstantAbiIdentity::matchesMembers",CppSource((self.ROOT / "src/rhi/backends" / backend).read_text(encoding="utf-8-sig")))
        fixture=CppSource((self.ROOT / "tests/fixtures/original_graphics_pipeline_input_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "verifyHotNoAllocation", "verifyFileLowering", "sameOwner", "nonowning", "consumption-native-closed"]:
            self.assertIn(token,fixture)
        self.assertIn("&& originalGraphicsMemoryInputPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_original_graphics_consumption_reuses_real_emitters_without_execution_authority(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        factory=source.split("WebGpuOriginalArtifactGraphicsFactory::create(",1)[1].split("WebGpuOriginalArtifactGraphicsFactory::validateConsumedOriginal(",1)[0]
        definition=source.split("WebGpuOriginalArtifactGraphicsFactory::validateDefinition(",1)[1].split("WebGpuOriginalArtifactGraphicsFactory::create(",1)[0]
        for token in ["validateProfile", "matchesDefinition", "validateSingleEntryVertexInput", "PipelineShaderDeclarationContract", "validateOriginalWebGpuRhiRenderPipeline"]: self.assertIn(token,definition)
        for token in ["selectTarget(input,rhi::Backend::WebGPU)", "RenderPipelineStateCopy::copy", "validateDefinition", "emitWebGpuNativeShaderModule", "emitWebGpuNativeRenderPipeline", "target.sources[0]", "sameControlBlock", "CpuMemoryQuotaScope"]:
            self.assertIn(token,factory)
        for forbidden in ["readTextFile", "loadFromFile", "loadFromYaml", "loadWebGpuBindingSpaceLowering(", "WebGpuRecordedCommandProof", "wgpuQueueSubmit"]:
            self.assertNotIn(forbidden,factory)
        self.assertGreaterEqual(source.count("return emitWebGpuNativeRenderPipeline("),1)
        self.assertIn("if(issued.artifactInput() || issued.graphicsInput())",source)
        ownership=self.read_source("src/rhi/backends/webgpu/webgpu_pipeline_ownership.h")
        self.assertIn("std::shared_ptr<const rhi::OriginalGraphicsPipelineInput> graphics;",ownership)
        self.assertIn("sources_.expanded()->space()->graphicsInput()",ownership)
        selected=self.read_validation_section("src/rhi/backends/webgpu/native_validation.h","webgpu_original_depth_table_expansion_native")
        for token in ["physicalShape", "validateJoin", "expandedBindingFact", "matchesRawHexDigest"]: self.assertIn(token,selected)
        self.assertIn("friend class WebGpuOriginalArtifactGraphicsFactory",ownership)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_artifact_graphics_consumption_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "verifyHotNoAllocation", "verifyActualLoss", "quotas3-zero", "native-wrappers2-zero", "sameOwner", "std::swap(p.vertexShader,p.fragmentShader)"]:
            self.assertIn(token,fixture)
        self.assertIn("&& originalGraphicsConsumptionPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))
        payloads=self.read_source("src/rhi/backends/webgpu/webgpu_native_payloads.h")
        release=payloads.split("~WebGpuNativeRenderPipelinePayload()",1)[1].split("struct WebGpuNativeComputePipelinePayload",1)[0]
        self.assertNotIn("webGpuNativeDeviceLost()",release)

    def test_original_graphics_creation_is_sealed_and_uses_shared_actual_receipts(self):
        actor=self.read_source("src/rhi/backends/webgpu/webgpu_pipeline_preparation.h", "backends_webgpu_webgpu_original_artifact_graphics_creation_h")
        for token in ["OriginalGraphicsPipelineCreation", "RenderPipelineStateCopy::copy", "validateDefinition", "hasIndependentOwners", "CreationReceiptLifecycle", "lifecycle_.observe", "callbacks_.prepare", "callbacks_.pop", "Kind::Graphics", "WebGpuOriginalArtifactGraphicsFactory::create", "graphicsInput()", "preinitializedWebGPUDevice", "QUOTA-LIMIT", "CANONICAL-JOIN-MISSING"]: self.assertIn(token,actor)
        for forbidden in ["WebGpuRecordedCommandProof", "MutationJournal", "wgpuQueueSubmit", "std::make_shared", "std::vector", "readTextFile"]: self.assertNotIn(forbidden,actor)
        facade=self.read_rhi_api_and_implementation("src/rhi/gpu_pipeline.h").split("class OriginalGraphicsPipelineCreation :",1)[1].split("struct PipelineValidationReport",1)[0]
        self.assertIn("OriginalGraphicsPipelineCreation()=default;",facade)
        self.assertIn("friend class rhi_webgpu::WebGpuOriginalArtifactGraphicsCreation",facade)
        self.assertNotIn("native",facade)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_artifact_graphics_creation_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "verifyHotNoAllocation", "verifyActualLoss", "missingOriginalGraphicsEntry", "pop rejection", "Promises::releaseScope(2u)", "mutated caller", "quotas3-zero"]: self.assertIn(token,fixture)
        self.assertIn("&& originalGraphicsCreationPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_accepted_graphics_factory_adopts_exact_payload_through_shared_admission(self):
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        for token in ["createOriginalArtifactGraphicsPipeline", "OriginalAcceptedGraphicsPipelineFactoryContract", "createOriginalAcceptedArtifactPipeline", "usesCpuQuotaDomain", "graphicsCreation", "originalGraphicsCreation_=std::move(graphicsCreation)"]: self.assertIn(token,device)
        shared=self.read_source("src/rhi/backends/webgpu/webgpu_pipeline_preparation.h", "backends_webgpu_webgpu_accepted_original_pipeline_validation_h")
        for token in ["Creation::validateOriginal", "PipelineDefinitionIdentity::matches", "hasIndependentOwners"]: self.assertIn(token,shared)
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_backend_objects.cpp")
        factory=backend.split("WebGpuAcceptedOriginalGraphicsPipelineFactory::create(",1)[1].split("arp::core::UniquePtr<rhi::RenderPipeline> createWebGpuRhiRenderPipeline",1)[0]
        for token in ["WebGpuAcceptedOriginalPipelineValidation::validate", "validateWebGpuRhiRenderPipelineImpl", "actor->pipeline_", "ShaderReflectionStateCopy::copy"]: self.assertIn(token,factory)
        for forbidden in ["emitWebGpu", "createWebGpuNative", "readTextFile", "wgpuQueueSubmit", "WebGpuRecordedCommandProof"]: self.assertNotIn(forbidden,factory)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_accepted_graphics_pipeline_factory_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "verifyHotNoAllocation", "arpAcceptedGraphicsRecreations===0", "sameOwner(firstNative", "verifyActualLoss", "quotas7-zero", "native-wrappers2-zero"]: self.assertIn(token,fixture)
        self.assertIn("&& originalAcceptedGraphicsFactoryPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_pipeline_definition_identity_covers_every_authored_field_without_hash_substitution(self):
        helper=self.read_source("src/rhi/frontend/pipeline_definition_identity.h")
        source=self.read_rhi_api_and_implementation("src/rhi/gpu_pipeline.h")+self.read_source("src/rhi/gpu_subpass.h")
        for struct in ["ShaderGeneratedArtifacts", "ShaderReflectionBinding", "ShaderReflectionDesc", "ShaderPackageDesc", "BlendComponent", "ColorTargetState", "StencilFaceState", "DepthStencilState", "MultisampleState", "ViewportState", "ScissorState", "VertexAttributeDesc", "VertexBufferLayoutDesc", "SubpassDesc", "SubpassDependency", "RenderPassCompatibilityDesc", "RenderPipelineDesc", "ComputePipelineDesc"]:
            body=source.split("struct "+struct+" {",1)[1].split("\n};",1)[0]
            body=re.sub(r"//[^\n]*", "",body)
            fields=re.findall(r"(?:^|\n)\s+[\w:]+(?:<[^\n>]+>)?\s+(\w+)\s*(?:=|\{|;)",body)
            self.assertTrue(fields,struct)
            method=helper.split("matches(const "+struct+"& a,",1)[1].split("\n    }",1)[0]
            for field in fields: self.assertIn("a."+field,method,struct+"."+field)
        for forbidden in ["stableShaderPackageHash", "stableRenderPipelineStateKey", "memcmp", "std::vector"]: self.assertNotIn(forbidden,helper)
        for token in ["std::equal", "pipelineFloatBits", "ShaderPushConstantAbiIdentity::matchesMembers", "matchesDefinition"]: self.assertIn(token,helper)

    def test_accepted_pipeline_wrapper_seal_precedes_registry_publication(self):
        pipeline=self.read_rhi_api_and_implementation("src/rhi/gpu_pipeline.h")
        selected=pipeline.split("class OriginalRegisteredPipeline :",1)[1].split("class ARP_API RenderPipeline",1)[0]
        for token in ["friend class rhi_webgpu::WebGpuAcceptedOriginalComputePipelineFactory", "friend class rhi_webgpu::WebGpuAcceptedOriginalGraphicsPipelineFactory", "std::weak_ptr<const OriginalComputePipelineCreation>", "std::weak_ptr<const OriginalGraphicsPipelineCreation>", "acceptedComputeWrapperCreation_.lock()", "acceptedGraphicsWrapperCreation_.lock()", "sameOwner", "WRAPPER-SEAL"]: self.assertIn(token,selected)
        self.assertEqual(selected.count("acceptedWrapperSealDiagnostic(original"),2)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h").split("auto candidate=factory(definition ?",1)[1].split("catch(const std::bad_alloc&)",1)[0]
        self.assertLess(device.index("acceptedWrapperSealDiagnostic(creation)"),device.index("record->object_=candidate.get()"))
        self.assertLess(device.index("acceptedWrapperSealDiagnostic(graphicsCreation)"),device.index("record->object_=candidate.get()"))
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_backend_objects.cpp")
        self.assertEqual(backend.count("wrapper->acceptedComputeWrapperCreation_=creation"),1)
        self.assertEqual(backend.count("wrapper->acceptedGraphicsWrapperCreation_=creation"),1)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_accepted_pipeline_wrapper_seal_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["OriginalPipelineSubstituteFacade", "Mode::DifferentReceipt", "observer.expired()", "metadataBefore", "null_memory_resource", "retry=c.publish()"]: self.assertIn(token,fixture)
        self.assertIn("&& originalAcceptedWrapperSealPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_original_texture_view_host_references_retire_after_device_loss(self):
        payloads=self.read_source("src/rhi/backends/webgpu/webgpu_native_payloads.h")
        texture=payloads.split("~WebGpuNativeTexturePayload()",1)[1].split("class WebGpuNativeTextureFactory",1)[0]
        view=payloads.split("~WebGpuNativeTextureViewPayload()",1)[1].split("struct WebGpuNativeSamplerPayload",1)[0]
        self.assertIn("!browserSurfaceTexture && !webGpuNativeDeviceLost()",texture)
        self.assertIn("wgpuTextureDestroy(texture)",texture)
        self.assertIn("wgpuTextureRelease(texture)",texture)
        self.assertNotIn("if (!webGpuNativeDeviceLost()) wgpuTextureRelease",texture)
        self.assertIn("wgpuTextureViewRelease(view)",view)
        self.assertNotIn("webGpuNativeDeviceLost()",view)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_accepted_graphics_pipeline_factory_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["arpPipelinePositionalAttachmentObjects.length===2", "arpPipelinePositionalAttachmentObjects.every(p=>wrappers.includes(p))", "!Module.arpPipelinePositionalAttachmentObjects.some(p=>wrappers.includes(p))", "positional.retire()"]: self.assertIn(token,fixture)
        self.assertIn("PIPELINE_POSITIONAL_ATTACHMENT_RETIREMENT_OK",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_descriptor_dependency_wrappers_release_after_final_custody_and_loss(self):
        payloads=self.read_source("src/rhi/backends/webgpu/webgpu_native_payloads.h")
        for symbol,release in [("Buffer","wgpuBufferRelease(buffer)"),("Sampler","wgpuSamplerRelease(sampler)"),("DescriptorGroup","wgpuBindGroupRelease(group)"),("RenderBundle","wgpuRenderBundleRelease(bundle)")]:
            body=payloads.split("~WebGpuNative"+symbol+"Payload()",1)[1].split("\n};",1)[0]
            self.assertIn(release,body)
            if symbol=="Buffer":
                self.assertIn("if (!webGpuNativeDeviceLost()) {\n                wgpuBufferDestroy(buffer)",body)
                self.assertNotIn("if (!webGpuNativeDeviceLost()) wgpuBufferRelease",body)
            else: self.assertNotIn("webGpuNativeDeviceLost()",body)
        commands=self.read_source("src/rhi/backends/webgpu/webgpu_native_commands.cpp")
        for symbol in ["releaseBindGroupLayout", "releaseBindGroup", "releaseRenderBundleEncoder", "releaseRenderBundle"]:
            body=commands.split("void "+symbol+"(",1)[1].split("\n}",1)[0]
            self.assertNotIn("webGpuNativeDeviceLost()",body)
            self.assertIn("= nullptr",body)
        resources=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        self.assertNotIn("if (group && !webGpuNativeDeviceLost()) wgpuBindGroupLayoutRelease",resources)
        self.assertNotIn("if (!webGpuNativeDeviceLost()) wgpuBindGroupLayoutRelease",resources)
        self.assertIn("auto mergedGroup = wgpuDeviceCreateBindGroup(device, &groupDesc);\n    wgpuBindGroupLayoutRelease(layout);",commands)
        probe=CppSource((self.ROOT / "tests/fixtures/webgpu_descriptor_dependency_retirement_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["wgpuDeviceCreateBindGroup", "wgpuBindGroupAddRef", "wgpuRenderBundleEncoderFinish", "live(true);caller_.reset()", "custodian_.reset()", "live(false)", "!quota_->liveBytes()", "CpuMemoryQuotaLease", "WebGPU.Internals.jsObjects"]: self.assertIn(token,probe)
        self.assertIn("&& descriptorDependencyRetirementPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_geometry_usage4_preserves_provenance_and_complete_dependency_coverage(self):
        projection=self.read_source("src/rhi/frontend/command_usage_projection.h")
        for token in ["struct GeometryPoint", "OriginalCommandGeometryBindingContract::Access original",
                      "geometryBindings", "budget.geometryUsage.matches(original.geometryUsage)",
                      "ScopeContract::Contribution::GeometryBinding", "ScopeContract::BufferUsage::Index",
                      "ScopeContract::BufferUsage::Vertex", "GEOMETRY-SCOPE", "GEOMETRY-COVERAGE",
                      "graph->visitGeometryAccesses", "P::GeometryPoint", "original.binding.registrationGeneration",
                      "bufferUseOf", "scopeOf", "accessOf", "bindRangeNeighborhood", "ENDPOINT-COVERAGE"]:
            self.assertIn(token,projection)
        for forbidden in ["makeShared", "makeUnique", "pmr::Vector", "BoundedSequence", "queueSubmit", "wgpu"]:
            self.assertNotIn(forbidden,projection)
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_usage_projection")
        for token in ["OriginalGeometryUsageBudget geometryUsage", "b.geometryUsage.diagnostic()",
                      "GEOMETRY-SCHEMA", "b.contractVersion==4u ? 3u", "readbackIntent"]:
            self.assertIn(token,contract)
        decisions=self.read_command_contract_sections("src/rhi/frontend/command_shader_validation.h",
            "src/rhi/semantics/command_lowering.h","original_typed_usage_projection")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:self.assertIn("Backend::"+backend,decisions)
        self.assertIn("version==3u || version==4u",decisions)
        self.assertIn("CapabilityMode::Equivalent,false,true,false,false,nullptr",decisions)
        fixture=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        for token in ["verifyGeometryDependencies", "K::BufferBarrier", "K::BufferMemoryBarrier",
                      "original-index-provenance initial-and-between", "P::GeometryPoint",
                      "frozenCounts", "weakBarrier.destinationAccess", "--shortDependency.maxPairChecks",
                      "--shortDependency.maxProjectionPasses", "--shortDependency.maxContributionVisits",
                      "--shortDependency.maxEndpointContributions", "--shortDependency.maxDependencies",
                      "!emitted", "verifyHotNoAllocation", "verifyNoHotQueries", "observer.expired()"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("&& originalGeometryUsageDependencyPassed &&",runner)
        self.assertIn("originalGeometryUsageDependencyPassed: originalGeometryUsageDependencyPassed",runner)

    def test_geometry_state_stream_profiles_reuse_original_observations(self):
        model=self.read_source("src/rhi/frontend/command_model.h")
        for token in ["struct OriginalGeometryUsageBudget", "geometryUsage.diagnostic()", "GEOMETRY-LEGACY-SCHEMA",
                      "budget.contractVersion!=4u", "budget.contractVersion-1u"]: self.assertIn(token,model)
        state=self.read_source("src/rhi/frontend/command_state.h")
        for token in ["footprint.matches(budget_.geometryUsage)", "RHI-ORIGINAL-SEQUENCE-GEOMETRY-STATE",
                      "inputs_->visitGeometryAccesses", "bufferBefore(graph,access.resource,access.commandIndex,state)",
                      "geometry.bindings>budget.maxImplicitAccesses-implicitAccesses"]: self.assertIn(token,state)
        stream=self.read_source("src/rhi/frontend/command_stream_program.h")
        for token in ["GEOMETRY-FOOTPRINT-MISMATCH", "geometryBinds", "maxImplicitAccesses",
                      "budget_.contractVersion!=4u", "graph_->visitGeometryAccesses"]: self.assertIn(token,stream)
        fixture=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        for token in ["verifyGeometryStateStream", "state4 sequence4 stream4 inputs5", "snapshot-no-read-authority",
                      "shortSequence.maxSteps", "shortSequence.maxStateCells", "shortStream",
                      "verifyHotNoAllocation", "verifyNoHotQueries", "retained.expired()"]: self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("&& originalGeometryStateStreamPassed && originalGeometryStateRefusalPassed &&",runner)

    def test_geometry_access_projection_and_backend_decisions_are_explicit(self):
        pairs=self.read_source("src/core/unsigned_range.h").split("countUnorderedPairsUnsigned",1)[1].split("// Finite nonempty intervals",1)[0]
        for token in ["output=0", "count<UInt{2}", "canMultiplyUnsigned(lhs,rhs)"]:
            self.assertIn(token,pairs)
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        for token in ["visitGeometryAccesses", "walkGeometryAccesses", "validateAccessBudget",
                      "budget_.contractVersion!=5u", "budget.maxPairChecks", "core::countUnorderedPairsUnsigned",
                      "ResourceAccess::VertexAttributeRead", "ResourceAccess::IndexRead",
                      "PipelineStage::VertexInput", "ResourceAccessUsageValidation::validate",
                      "scopeEndCommandIndex", "registrationGeneration"]:
            self.assertIn(token,inputs)
        projection=inputs.split("walkGeometryAccesses(",2)[-1].split("static bool resourceMatches",1)[0]
        for token in ["makeShared", "makeUnique", "createBuffer", "wgpu", "MutationJournal"]:
            self.assertNotIn(token,projection)
        lowering=self.read_source("src/rhi/semantics/command_state_lowering.h")
        for token in ["geometryInputAccessLowering", "case Backend::Null", "case Backend::Vulkan",
                      "case Backend::Metal", "case Backend::WebGPU", "RHI-GEOMETRY-ACCESS-WEBGPU-DEPENDENCY-UNPROVEN",
                      "CapabilityMode::Native,false,true,nullptr"]: self.assertIn(token,lowering)
        for backend in ["metal","webgpu"]:
            code=CppSource((self.ROOT / ("src/rhi/backends/"+backend+"/"+backend+"_backend_commands.cpp")).read_text(encoding="utf-8-sig"))
            self.assertEqual(code.count("geometryInputBarrierDiagnostic"),4)
        vulkan=self.read_source("src/rhi/backends/vulkan/vulkan_native_commands.cpp")
        for token in ["VK_PIPELINE_STAGE_VERTEX_INPUT_BIT", "VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT", "VK_ACCESS_INDEX_READ_BIT"]:
            self.assertIn(token,vulkan)
        for backend,file in [("vulkan","vulkan_command_objects.cpp"),("null","null_commands.cpp")]:
            code=CppSource((self.ROOT / ("src/rhi/backends/"+backend+"/"+file)).read_text(encoding="utf-8-sig"))
            self.assertEqual(code.count("validateGeometryBarrier"),4)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalGeometryAccessPassed &&",runner)

    def test_geometry_inputs_join_original_sources_and_keep_execution_closed(self):
        contract=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_geometry_binding")
        for token in ["usage.originalSources()->sources()", "command.firstReference", "pipelineCommand.firstReference",
                      "pipelineBindCommandIndex", "scopeBeginCommandIndex", "bufferRegistration.diagnostic()",
                      "pipelineRegistration.diagnostic()", "matchesIdentity", "registrationGeneration",
                      "originalRenderPipelineDefinition", "validateVertexBindingShape", "validateIndexBindingShape",
                      "OriginalCommandTypedSourceLowering::lowering"]: self.assertIn(token,contract)
        for forbidden in ["makeShared", "makeUnique", "std::vector", "WebGpuNative", "MutationJournal", "createBuffer"]:
            self.assertNotIn(forbidden,contract)
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        for token in ["OriginalCommandGeometryBinding", "inputBudget.contractVersion!=5u", "maxVertexBindings",
                      "maxIndexBindings", "pipelineBind=i", "pipelineBind=SIZE_MAX", "consumeDescriptor",
                      "OriginalCommandGeometryBindingContract::validate"]: self.assertIn(token,inputs)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalGeometryPositionalPassed &&",runner)
        self.assertIn("BINDLESS_RHI_GEOMETRY_POSITIONAL_OK inputs5 index3",runner)

    def test_pipeline_positional_inputs_use_same_sequence_and_original_scope_contract(self):
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        for token in ["OriginalCommandPipelineBinding,OriginalCommandDescriptorBinding,OriginalCommandGeometryBinding>", "const RenderPipeline*", "const ComputePipeline*", "OriginalCommandInputsBudget budget", "inputBudget.contractVersion>=2u", "semantics::CommandRecordingOrder order", "order.append(kind)", "CommandInputContract::consumeDescriptor", "OriginalCommandPipelineBindingContract::validate", "scopeBegin=i", "order.complete()", "PIPELINE-CAPACITY"]: self.assertIn(token,inputs)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_usage.h","src/rhi/semantics/command_lowering.h","original_command_pipeline_binding")
        for token in ["validatePipelineKind", "Adapter::validateOriginal", "registrationGeneration", "scopeBeginCommandIndex", "command.firstReference", "lowering(Backend", "executionAvailable=false"]: self.assertIn(token,contract)
        for forbidden in ["std::vector", "WebGpuNative", "RecordedCommandProof", "makeShared", "MutationJournal", "OriginalCommandState"]: self.assertNotIn(forbidden,contract)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_pipeline_binding_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure", "pressure.verify", "verifyHotNoAllocation", "null_memory_resource", "wrongUsage", "PIPELINE-GENERATION", "PIPELINE-SOURCE", "PIPELINE-SCOPE"]: self.assertIn(token,fixture)
        self.assertIn("&& originalPipelinePositionalPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_descriptor_whole_buffer_guard_does_not_infer_remaining_tail(self):
        policy=self.read_source("src/rhi/frontend/descriptor_whole_buffer_binding.h")
        for token in ["containsUnsignedRange", "dynamicOffset == 0u", "contractVersion != 1u", "ReadOnlyStorageBuffer", "LimitsUnavailable", "bufferBytes <= maximum"]: self.assertIn(token,policy)
        for forbidden in ["bufferAccessOffset", "bufferAccessBytes", "makeShared", "WGPU", "OriginalCommandState"]: self.assertNotIn(forbidden,policy)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_commands.cpp")
        self.assertNotIn("sizeBytes - dynamicOffset",native)
        self.assertIn("WebGpuDescriptorDynamicOffsetValidation::validateBinding",native)
        self.assertEqual(native.count("if (!validateDynamicOffsetValue("),5)
        ordinary=self.read_source("src/rhi/backends/webgpu/webgpu_backend_descriptors.cpp")
        self.assertIn("DescriptorWholeBufferBinding::preservesRanges",ordinary)
        self.assertIn("&& descriptorWholeBufferPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_original_descriptor_limits_stay_inside_accepted_wrapper_custody(self):
        pipeline=self.read_rhi_api_and_implementation("src/rhi/gpu_pipeline.h")
        field="OriginalDescriptorBufferBindingContract::Limits originalDescriptorBufferLimits_;"
        self.assertEqual(pipeline.count(field),1)
        self.assertIn(field,pipeline.split("class OriginalRegisteredPipeline",1)[1].split("private:",1)[1])
        method=pipeline.split("originalDescriptorBufferBindingDiagnostic(",1)[1].split("const char* originalFullDefinitionDiagnostic",1)[0]
        for token in ["validateRequest", "originalArtifactComputeDiagnostic", "originalArtifactGraphicsDiagnostic", "validateBinding"]: self.assertIn(token,method)
        self.assertNotIn("query",method)
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_backend_objects.cpp")
        self.assertEqual(backend.count("wrapper->originalDescriptorBufferLimits_=WebGpuOriginalDescriptorBufferLimitsQuery::query"),2)
        query=self.read_source("src/rhi/backends/webgpu/webgpu_original_descriptor_buffer_limits_query.h")
        for token in ["Number.isSafeInteger", "device !== Module.preinitializedWebGPUDevice", "Number.MAX_SAFE_INTEGER", "catch (_) { return 0; }", "minStorageBufferOffsetAlignment"]: self.assertIn(token,query)
        self.assertIn("&& originalDescriptorBufferLimitsPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_descriptor_slot_shapes_have_shared_no_allocation_diagnostics(self):
        source=self.read_source("src/rhi/frontend/descriptor_set_validation.h")
        shape=source.split("class DescriptorSlotShapeContract final",1)[1].split("inline bool descriptorSetLayoutMatches",1)[0]
        for token in ["TableMatch { Coverage, ExactOriginal }", "findTableSlot", "descriptorLayoutDefinitionMatches", "table.stages != slot->stages", "table.capacity != slot->capacity", "SHAPE-MATCH-MODE"]: self.assertIn(token,shape)
        for forbidden in ["ValidationResult", "makeShared", "GpuDevice", "WebGpuNative"]: self.assertNotIn(forbidden,shape)
        adapter=self.read_source("src/rhi/frontend/descriptor_descriptors.cpp").split("ValidationResult validateDescriptorTableSlot",1)[1].split("ValidationResult validateDescriptorLayout",1)[0]
        self.assertIn("DescriptorSlotShapeContract::TableMatch::Coverage",adapter)
        self.assertNotIn("for (",adapter)
        typed=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_typed_input")
        helpers=typed.split("static const char* validateSetShape",1)[1].split("static const char* validateTableBinding",1)[0]
        for token in ["noexcept", "validateDynamicOffsetArgumentsDiagnostic", "DescriptorSlotShapeContract::validateSet", "DescriptorSlotShapeContract::validateTable"]: self.assertIn(token,helpers)
        for forbidden in ["ValidationResult", "validateDescriptorResources", "makeShared"]: self.assertNotIn(forbidden,helpers)
        self.assertIn("&& descriptorSlotShapePassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_original_depth_table_artifacts_preserve_explicit_array_semantics(self):
        cmake=CppSource((self.ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig"))
        producer=cmake.split("set(ARP_ORIGINAL_DESCRIPTOR_DEPTH_FIXTURE_DIR",1)[1].split("add_executable(arp_rhi_descriptor_contract_tests",1)[0]
        for token in ["descriptor_depth_compute descriptor_depth", "slang spv metal wgsl reflection.yaml", "--single-entry computeMain", "--graphics-module", "foreach(stage Vertex Fragment)", "arp_original_descriptor_depth_fixture", "--preload-file=", "scripts/spirv_layout.py"]: self.assertIn(token,producer)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_descriptor_depth_artifacts_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["SingleEntryShaderArtifact::validateOriginal", "GraphicsShaderModuleArtifact::validateOriginal", "arrayCount==3u", "depthTexture", "ShaderStage::Compute", "ShaderStage::Fragment", "reflection.bindings.empty()", "computeWorkgroupSizeX==5u", "OriginalSingleEntryPipelineInput::prepare", "OriginalGraphicsPipelineInput::prepare", "nativeExecutionProven", "liveBytes"]: self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalDescriptorDepthArtifactsPassed &&",runner)
        browser=CppSource((self.ROOT / "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp").read_text(encoding="utf-8-sig"))
        self.assertIn("OriginalDescriptorDepthArtifactsFixture::verify",browser)
        self.assertIn("/original-descriptor-depth-fixture/descriptor_depth/descriptor_depth.graphics.module.yaml",browser)

    def test_webgpu_descriptor_array_abi_rejects_before_native_lowering(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        layout=source.split("std::optional<WGPUBindGroupLayoutEntry> nativeLayoutEntry",1)[1].split("switch (physicalType)",1)[0]
        self.assertLess(layout.index("validateCount"),layout.index("WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT"))
        self.assertIn("entry.bindingArraySize = 0u",layout)
        for symbol,emit in [("WebGpuOriginalArtifactComputeFactory::create","emitWebGpuNativeComputePipeline"),("WebGpuOriginalArtifactGraphicsFactory::create","emitWebGpuNativeRenderPipeline")]:
            factory=source.split(symbol,1)[1].split("catch(const std::bad_alloc&)",1)[0]
            self.assertLess(factory.index("WebGpuDescriptorArrayAbiContract::validateBindings"),factory.index(emit))
        contract=self.read_validation_section("src/rhi/backends/webgpu/native_validation.h","webgpu_descriptor_array_abi")
        for token in ["version=1u", "nativeArraysAvailable=false", "ARRAY-ABI-UNSUPPORTED", "noexcept"]: self.assertIn(token,contract)
        for token in ["makeShared", "GpuDevice", "ValidationResult"]: self.assertNotIn(token,contract)
        probe=CppSource((self.ROOT / "tests/fixtures/webgpu_descriptor_array_abi_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["getCompilationInfo", "popErrorScope", "Array.from(device.features)", "device!==Module.preinitializedWebGPUDevice", "arpArrayGateNativeCalls", "CpuMemoryQuotaLease", "WebGpuAsyncWaitFixture::until"]: self.assertIn(token,probe)
        self.assertIn("&& descriptorArrayAbiPassed &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_buffer_binding_ranges_are_versioned_frozen_and_fail_closed_before_backend_write(self):
        contract=self.read_source("src/rhi/frontend/descriptor_buffer_binding_range.h")
        for token in ["contractVersion=1u", "contractVersion!=2u", "validateSchema", "core::containsUnsignedRange", "result={}", "bufferBytes-range.offsetBytes", "range.offsetBytes+dynamicOffset", "SUBRANGE-LOWERING-MISSING"]: self.assertIn(token,contract)
        for token in ["ValidationResult", "makeShared", "std::vector", "bufferAccess"]: self.assertNotIn(token,contract)
        gate=self.read_source("src/rhi/gpu_descriptors.h").split("virtual ValidationResult write(const DescriptorBindingWrite& write) final",1)[1].split("virtual bool isCurrentAllocation",1)[0]
        self.assertLess(gate.index("DescriptorBufferBindingRangeContract::writeDiagnostic"),gate.index("writeImpl(write)"))
        self.assertLess(gate.index("writeImpl(write)"),gate.index("commitContentWrite()"))
        equality=self.read_source("src/rhi/frontend/descriptor_set_validation.h").split("inline bool descriptorBindingWriteMatches",1)[1].split("inline bool descriptorTableWriteMatches",1)[0]
        self.assertIn("expected.bufferBindingRange.matches(actual.bufferBindingRange)",equality)
        vulkan=self.read_source("src/rhi/backends/vulkan/vulkan_descriptors.cpp").split("static bool sameWrite",1)[1].split("rhi::ValidationResult validateTextureViewContract",1)[0]
        self.assertIn("descriptorBindingWriteMatches(lhs,rhs)",vulkan)
        self.assertNotIn("lhs.binding",vulkan)
        frozen=CppSource((self.ROOT / "tests/fixtures/original_descriptor_source_freeze_fixture.h").read_text(encoding="utf-8-sig"))
        for field in ["contractVersion", "offsetBytes", "sizeBytes"]: self.assertIn("++e[0].write.bufferBindingRange."+field,frozen)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& descriptorBufferBindingRangePassed &&",runner)

    def test_original_pipeline_source_is_exact_acyclic_cpu_custody(self):
        adapter=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_pipeline_source")
        for token in ["PipelineDefinitionIdentity::matches", "originalAcceptedSharedQuotaBound", "sharedOwnerMatches", "registrationGeneration", "independentAcceptedCpuQuotaDiagnostic", "overlapsOriginalDeviceCpuOwner", "cpuOwnerSlots=10u", "positionalAdapterAvailable=false,executionAvailable=false", "originalArtifactGraphicsDiagnostic", "originalArtifactComputeDiagnostic"]:
            self.assertIn(token,adapter)
        for forbidden in ["WebGpuNative", "RecordedCommandProof", "OriginalCommandResourceSources", "inspectCpuAllocationContaining", "cpuAllocationOverlapsQuotaOwner", "std::vector", "createPipeline"]:
            self.assertNotIn(forbidden,adapter)
        pipeline=self.read_rhi_api_and_implementation("src/rhi/gpu_pipeline.h")
        publication=pipeline.split("shareAcceptedOriginal(",1)[1].split("bool originalAcceptedSharedQuotaBound",1)[0]
        for token in ["CpuMemoryQuotaScope", "originalWrapperQuota_", "core::shareUnique", "originalAcceptedShared_=true"]: self.assertIn(token,publication)
        sources=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        for token in ["assemblyVersion_!=2u", "ASSEMBLY-PIPELINE-VERSION", "core::RowMajorIndexGrid", "cpuIdentity", "originalLayout()"]: self.assertIn(token,sources)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_pipeline_source_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.verify", "wrapperPressure.verify", "verifyHotNoAllocation", "rootObserver.expired()", "registeredOwnerUnion", "SHARED-QUOTA-RECEIPT", "ASSEMBLY-SEALED", "retainForLoss", "verifyLoss"]: self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalPipelineSourcePassed &&",runner)
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        gate=inputs.split("if(kind==CommandUsageKind::BindGraphicsPipeline",1)[1].split("if(required==Family::Scalar)continue",1)[0]
        for token in ["BindComputePipeline", "OriginalCommandDescriptorBindingContract::isBinding(kind)", "inputBudget.contractVersion<3u", "RHI-ORIGINAL-TYPED-INPUT-POSITIONAL-JOIN-MISSING"]: self.assertIn(token,gate)

    def test_original_finite_write_commits_only_completed_candidate_and_keeps_emission_closed(self):
        l2=self.read_source("src/rhi/gpu_descriptors.h")
        finish=l2.split("const char* finishOriginalFiniteWrite() noexcept",1)[1].split("const char* cancelOriginalFiniteWrite",1)[0]
        self.assertLess(finish.index("finishOriginalFiniteWriteImpl()"),finish.index("commitContentWrite()"))
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_backend_descriptors.cpp")
        commit=backend.split("const char* finishOriginalFiniteWriteImpl() noexcept override",1)[1].split("const char* cancelOriginalFiniteWriteImpl",1)[0]
        self.assertLess(commit.index("finiteCandidateDiagnostic()"),commit.index("writes_.swap"))
        resources=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        ready=resources.split("const char* WebGpuNativeDescriptorGroupPayload::finiteCandidateDiagnostic()",1)[1].split("WebGpuNativeDescriptorGroupPayload::finiteCandidateWrites",1)[0]
        self.assertIn("state.validate()",ready)
        self.assertIn("evidence().proven()",ready)
        callback=resources.split("void WebGpuNativeDescriptorGroupPayload::observeCreationScope",1)[1].split("const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalFiniteGroup",1)[0]
        self.assertNotIn("commitContentWrite",callback)
        self.assertNotIn("writes_.swap",callback)
        source=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_source")
        self.assertIn("RHI-ORIGINAL-DESCRIPTOR-SOURCE-FINITE-COMPOSITION-CLOSED",source)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_payloads.h")
        self.assertIn('if(containsOriginalFiniteWrite())return "RHI-ORIGINAL-FINITE-WRITE-EMISSION-CLOSED"',native)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("success: success && geometryReadbackCompositionContractPassed && originalStreamGeometryExecutionPassed && canonicalIssuerPreparationPassed && originalFiniteDescriptorWritePassed &&",runner)

    def test_original_finite_source_diagnostic_is_borrowed_exact_and_grants_no_source_authority(self):
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        gate=device.split("diagnoseOriginalFiniteDescriptorSource(",1)[1].split("beginOriginalFiniteDescriptorWrite(",1)[0]
        for token in ["request.diagnostic()", "sourceDiagnosticDecision(backend())", "quota->limitBytes()!=request.sourceCpuBytes", "validateOriginalResourceRegistration(object)"]:
            self.assertIn(token,gate)
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_backend_descriptors.cpp")
        join=backend.split("diagnoseFiniteSource(",1)[1].split("ordinaryWriteAdmissionDiagnostic()",1)[0]
        self.assertIn('if(finiteCandidate_)return "RHI-ORIGINAL-FINITE-WRITE-PENDING"',join)
        resources=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        join=resources.split("WebGpuNativeDescriptorGroupPayload::finiteSourceDiagnostic(",1)[1].split("WebGpuNativeDescriptorGroupPayload::prepareOriginalDepthTable",1)[0]
        for token in ["state.validate()", "evidence().proven()", "sameOwner(pipeline,original)", "sameOwner(input.buffer,declaration.buffer)", "sameOwner(quota,declaration.buffer->originalMetadataQuota())", "independentAcceptedCpuQuotaDiagnostic(quota)", "overlapsOwner(quota,owner)", "descriptorBindingWriteMatches", "input.dynamicOffset!=declaration.dynamicOffset", "observeSuccessfulBindingWrite"]:
            self.assertIn(token,join)
        for forbidden in ["makeShared", "makeUnique", "createBindGroup", "GetBindGroupLayout", "return group", "RecordedCommandProof"]:
            self.assertNotIn(forbidden,join)
        source=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_source")
        self.assertIn("budget.contractVersion!=1u && !canonicalVersion(budget.contractVersion)",source)
        self.assertIn("return version==2u || completedDepthVersion(version)",source)
        self.assertIn("return version==6u",source)
        self.assertIn("RHI-ORIGINAL-DESCRIPTOR-SOURCE-FINITE-COMPOSITION-CLOSED",source)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalFiniteSourceDiagnosticPassed &&",runner)

    def test_original_finite_source3_reuses_unique_custody_inventory_and_keeps_execution_closed(self):
        source=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_source")
        for token in ["!finiteVersion(budget.contractVersion)", "finitePipelineSourceIndex", "finiteFullBoundViewOptIn", "finiteFootprintContractVersion!=1u", "maxFrozenDynamicOffsets", "finiteDeclarations_", "validateFullBoundView", "validateFiniteContent", "validatePipeline", "object_.adoptOriginal", "canonicalResources_.lock()", "nativeExecutionProven() const noexcept {return false;}"]:
            self.assertIn(token,source)
        prepare=source.split("auto result=core::makeUnique<OriginalCommandDescriptorSource>",1)[1].split("static const char* validateOriginal",1)[0]
        self.assertLess(prepare.index("shareOriginal(std::move(result))"),prepare.index("object_.adoptOriginal"))
        for forbidden in ["WebGpuNative", "RecordedCommandProof", "nativeHandle", "std::vector", "std::make_shared"]:
            self.assertNotIn(forbidden,source)
        sources=self.read_validation_section("src/rhi/frontend/command_sources.h","original_command_resource_sources")
        self.assertIn("assembly.contractVersion!=3u",sources)
        self.assertIn("(assembly==3u && source==3u)",sources)
        self.assertIn("allowsDescriptorSourceVersion(self->assemblyVersion_",sources)
        self.assertIn("allowsDescriptorSourceVersion(assemblyVersion_",sources)
        footprint=self.read_command_contract_sections("src/rhi/frontend/command_usage.h","src/rhi/semantics/command_lowering.h","original_command_typed_source").split("static const char* validateFullBoundView",1)[1].split("static const char* validateSetElements",1)[0]
        for token in ["element.stages!=shaderStages(binding.stages)", "ResourceAccess::ShaderRead | ResourceAccess::ShaderWrite", "diagnoseFiniteAccess", "element.write.bufferBindingRange.sizeBytes", "element.bufferAccessOffset,element.bufferAccessBytes"]:
            self.assertIn(token,footprint)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalFiniteSourceCustodyPassed &&",runner)

    def test_original_descriptor_inputs3_keeps_original_root_offsets_and_execution_gate(self):
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        for token in ["OriginalCommandDescriptorBindingContract::validate", "activePipeline=binding->sourceIndex", "activePipeline=SIZE_MAX", "maxFrozenDynamicOffsets-frozenOffsets", "maxDescriptorBindings", "maxTableBindings", "inputBudget.contractVersion<3u"]:
            self.assertIn(token,inputs)
        binding=self.read_command_contract_sections("src/rhi/frontend/command_usage.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_binding")
        for token in ["matchesCanonicalSources", "frozenContentRevision", "frozenPipelineSourceIndex", "frozenLogicalSet", "frozenFiniteDeclarations", "arguments.offsets(commandIndex)", "DescriptorSlotShapeContract::TableMatch::ExactOriginal", "TABLE-PROFILE-UNAVAILABLE", "preservesRanges", "originalDescriptorBufferBindingDiagnostic"]:
            self.assertIn(token,binding)
        for forbidden in ["makeShared", "makeUnique", "std::vector", "RecordedCommandProof", "WebGpuNative", "nativeHandle", "createBindGroup", "CpuReplay"]:
            self.assertNotIn(forbidden,binding)
        self.assertIn("nativeExecutionProven() const noexcept {return false;}",inputs)

    def test_selected_depth_table_inputs4_preserves_consumption_and_execution_gates(self):
        binding=self.read_command_contract_sections("src/rhi/frontend/command_usage.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_binding")
        for token in ["inputsVersion=3u", "inputsVersion==4u", "validateDepthTable", "validateDepthTableExpansionWrites", "originalDepthTablePipelineDiagnostic", "validateSource(binding,*table,sources)"]:
            self.assertIn(token,binding)
        consumption=self.read_validation_section("src/rhi/frontend/command_usage.h","Command descriptor consumption")
        self.assertEqual(consumption.count("OriginalCommandDescriptorConsumptionContract::inputsProfileMatches("),4)
        self.assertNotIn("inputs->originalBudget().contractVersion!=budget.contractVersion+2u",consumption)
        self.assertIn("RHI-ORIGINAL-CONSUMPTION-TABLE-PROFILE",consumption)
        native=CppSource((self.ROOT / "src/rhi/backends/webgpu/webgpu_native_resources.cpp").read_text(encoding="utf-8-sig"))
        for token in ["depthTableLowering(rhi::Backend::WebGPU,containsOriginalDepthTableWrite())", "originalFiniteWrite_->compute.get()==&pipeline", "originalFiniteWrite_->graphics.get()==&pipeline", "RHI-ORIGINAL-TABLE-BINDING-PIPELINE"]:
            self.assertIn(token,native)

    def test_original_table_inputs3_diagnosis_consumes_original_source_and_fails_closed(self):
        binding=self.read_command_contract_sections("src/rhi/frontend/command_usage.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_binding")
        table=binding.split("static const char* diagnoseTableDeclaration",1)[1].split("static const char* validate(",1)[0]
        for token in ["binding.kind()!=input.scalar.kind()", "sources.sources()[binding.sourceIndex]", "validateSource(binding,*table,sources)", "DescriptorSlotShapeContract::TableMatch::ExactOriginal", "return arp::rhi::semantics::OriginalCommandDescriptorBindingLowering::tableProfileDiagnostic()"]:
            self.assertIn(token,table)
        self.assertNotIn("return nullptr",table)
        for token in ["tableLoweringContractVersion=1u", "CapabilityMode::DiagnosticFailure", "positionalAvailable=false", "executionAvailable=false", "case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU"]:
            self.assertIn(token,binding)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalTablePositionalDiagnosticPassed &&",runner)

    def test_original_sparse_texture_projection_uses_command_index_lookup(self):
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_inputs")
        lookup=inputs.split("const Descriptor* descriptorAt",1)[1].split("std::uint32_t retainedOwnerCount",1)[0]
        self.assertIn("descriptors_.findIf",lookup)
        self.assertIn("value.commandIndex==command",lookup)
        ranges=self.read_source("src/rhi/frontend/command_texture_ranges.h")
        self.assertIn("inputs->descriptorAt(commandIndex)",ranges)
        self.assertNotIn("descriptorIndex++",ranges)
        sequence=self.read_source("src/rhi/frontend/command_state.h", "frontend_command_state_sequence_h")
        self.assertIn("graph.originalInputs()->descriptorAt(command)",sequence)
        transaction=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        self.assertIn("graph_->originalInputs()->descriptorAt(command)",transaction)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalTypedTextureProjectionPassed &&",runner)

    def test_original_descriptor_consumption_projection_cannot_open_state_or_native(self):
        projection=self.read_source("src/rhi/frontend/command_usage.h", "frontend_command_descriptor_consumption_h")
        for forbidden in ["makeShared", "makeUnique", "std::vector", "RecordedCommandProof", "WebGpuNative", "nativeHandle", "createBindGroup", "CpuReplay", "MutationJournal"]:
            self.assertNotIn(forbidden,projection)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_model.h","src/rhi/semantics/command_input_admission_lowering.h","original_command_descriptor_consumption")
        self.assertIn("stateAvailable=false,executionAvailable=false",contract)
        for section in ["original_command_resource_states", "original_command_buffer_states"]:
            text=self.read_validation_section("src/rhi/frontend/command_state.h",section)
            self.assertIn("typedStateJoinDiagnostic()",text)
        state=self.read_command_contract_sections("src/rhi/frontend/command_model.h","src/rhi/semantics/command_state_lowering.h","original_command_state_admission")
        self.assertIn('budget.contractVersion!=1u',state)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count('friend class '),2)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& originalDescriptorConsumptionPassed &&",runner)

    def test_original_depth_table_consumption_cannot_bypass_buffer_state_or_execution(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_model.h","src/rhi/semantics/command_input_admission_lowering.h","original_command_descriptor_consumption")
        for token in ["depthTableFootprintVersion", "depthTableFullBoundViewOptIn", "maxTextureAccesses",
                      "RHI-ORIGINAL-CONSUMPTION-LEGACY-SCHEMA", "RHI-ORIGINAL-CONSUMPTION-TEXTURE-BUDGET"]:
            self.assertIn(token,contract)
        projection=self.read_source("src/rhi/frontend/command_usage.h", "frontend_command_descriptor_consumption_h")
        for token in ["enum class Kind {Buffer,TextureView,Sampler}", "textureParentSourceIndex", "textureViewSourceIndex",
                      "textureParentRegistrationGeneration", "point.bufferSourceIndex=SIZE_MAX",
                      "sources.originalLeaseIndex", "latestDescriptorTableBinding",
                      "RHI-ORIGINAL-CONSUMPTION-TABLE-MISSING", "RHI-ORIGINAL-CONSUMPTION-TEXTURE-CAPACITY"]:
            self.assertIn(token,projection)
        state=self.read_validation_section("src/rhi/frontend/command_model.h","original_command_state_admission")
        self.assertIn("budget.descriptorConsumption.contractVersion!=1u",state)
        self.assertIn("RHI-ORIGINAL-STATE-CONSUMPTION-PROFILE",state)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_depth_table_publication_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyConsumption<Pipeline>(inputs,device,retained)", "assert(!emitted)",
                      "RHI-ORIGINAL-CONSUMPTION-DESCRIPTOR-MISSING", "RHI-ORIGINAL-CONSUMPTION-TABLE-MISSING",
                      "verifyHotNoAllocation", "verifyNoHotQueries"]:
            self.assertIn(token,fixture)

    def test_render_table_usage_preserves_shadowed_bindings_without_buffer_projection_authority(self):
        contract=self.read_validation_section("src/rhi/frontend/command_model.h","original_command_state_admission")
        for token in ["depthTableFootprintVersion", "depthTableFullBoundViewOptIn", "maxTextureAccesses",
                      "RHI-ORIGINAL-RENDER-USAGE-LEGACY-SCHEMA"]:
            self.assertIn(token,contract)
        projection=self.read_source("src/rhi/frontend/command_usage.h", "frontend_command_descriptor_consumption_h")
        render=projection.split("static RenderBindingResult renderBindings",1)[1].split("static constexpr bool isConsumption",1)[0]
        for token in ["K::BindRenderDescriptorTable", "projectBoundTable", "maxTextureAccesses"]:
            self.assertIn(token,projection)
        self.assertNotIn("latestDescriptorTableBinding",render)
        usage=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_usage_projection")
        for token in ["RHI-ORIGINAL-USAGE-PROJECTION-CONSUMPTION-PROFILE", "RHI-ORIGINAL-USAGE-PROJECTION-RENDER-PROFILE"]:
            self.assertIn(token,usage)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_depth_table_publication_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["result.bindings==2u", "result.textureAccesses==6u", "active.textureAccesses==3u", "assert(!emitted"]:
            self.assertIn(token,fixture)

    def test_depth_table_state_capture_preserves_render_union_and_refuses_old_sequence_and_transactions(self):
        state=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_resource_states")
        for token in ["visitTextureObservations", "budget.renderDescriptorUsage", "Consumption::renderBindingsOriginal",
                      "Projection::Point::Origin::RenderBinding", "Projection::Point::Origin::ShaderConsumption",
                      "access.textureParentSourceIndex", "access.textureViewSourceIndex", "entry.point.matches(point)"]:
            self.assertIn(token,state)
        sequence=self.read_source("src/rhi/frontend/command_state.h", "frontend_command_state_sequence_h")
        self.assertIn("graph->stateContractVersion()!=budget.contractVersion",sequence)
        transaction=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        self.assertIn("graph->budget_.contractVersion!=budget.contractVersion",transaction)
        self.assertIn("RHI-ORIGINAL-TRANSACTION-RENDER-FOOTPRINT-MISMATCH",transaction)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_depth_table_publication_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyStates(shadowed,device,stateQuota,2u)", "verifyStates(inputs,device,stateQuota,1u)",
                      "entry.observation.state()==rhi::ResourceState::Undefined", "probe.measureRetained(factory)",
                      "RHI-ORIGINAL-SEQUENCE-STATE-PROFILE", "RHI-ORIGINAL-SEQUENCE-TEXTURE-ACCESS-STATE"]:
            self.assertIn(token,fixture)

    def test_texture_transaction_reuses_exact_graph_journal_and_closes_native_completion(self):
        transaction=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        for token in ["textureTransactionLowering(device.backend(), budget.contractVersion)", "device.overlapsOriginalDeviceCpuOwner(quota)",
                      "sourceIndex==SIZE_MAX", "OriginalCommandGeometryBindingContract::Access>", "visitRenderDescriptorAccesses", "core::MutationJournal journal_",
                      "graph->budget_.renderDescriptorUsage.matches(budget.renderDescriptorUsage)",
                      "RHI-ORIGINAL-TEXTURE-TRANSACTION-NATIVE-COMPLETION-REQUIRED"]:
            self.assertIn(token,transaction)
        self.assertNotIn("RHI-ORIGINAL-TRANSACTION-STATE3-PLAN-UNAVAILABLE",transaction)
        lowering=self.read_source("src/rhi/semantics/command_state_lowering.h")
        for token in ["textureTransactionLowering", "RHI-ORIGINAL-TEXTURE-TRANSACTION-NULL-VALIDATOR-ONLY",
                      "RHI-ORIGINAL-TEXTURE-TRANSACTION-VULKAN-JOIN-UNAVAILABLE",
                      "RHI-ORIGINAL-TEXTURE-TRANSACTION-METAL-JOIN-UNAVAILABLE",
                      "RHI-ORIGINAL-TEXTURE-TRANSACTION-NATIVE-CLOSURE-UNAVAILABLE"]:
            self.assertIn(token,lowering)

    def test_depth_table_sequence_uses_tagged_texture_intents_and_combined_access_budget(self):
        sequence=self.read_source("src/rhi/frontend/command_state.h", "frontend_command_state_sequence_h")
        for token in ["graph->stateContractVersion()!=budget.contractVersion", "budget.contractVersion>=3u",
                      "visitRenderDescriptorAccesses", "implicit.logicalAccesses-implicit.textureAccesses",
                      "maxImplicitAccesses-implicitAccesses", "entry.point.origin!=Projection::Point::Origin::ExplicitOperand",
                      "implicit?0u", "state!=entry.point.state", "entry.point.stages,entry.point.access",
                      "RHI-ORIGINAL-SEQUENCE-SHADER-TEXTURE-STATE"]:
            self.assertIn(token,sequence)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_depth_table_publication_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifySequence(shadowed,states,device,stateQuota)", "Probe::Missing", "Probe::Late",
                      "Probe::WrongRange", "Probe::WrongBefore", "Probe::WrongState", "result.steps==14u",
                      "RHI-ORIGINAL-SEQUENCE-IMPLICIT-CAPACITY", "entry.observation.state()==S::Undefined"]:
            self.assertIn(token,fixture)

    def test_texture_stream_keeps_exact_state_render_footprints_and_shadowed_bind_ordinals(self):
        contract=self.read_validation_section("src/rhi/frontend/command_usage.h","original_command_stream")
        for token in ["OriginalRenderDescriptorUsageBudget renderDescriptorUsage", "RHI-ORIGINAL-STREAM-RENDER-LEGACY-SCHEMA",
                      "s.contractVersion!=b.contractVersion", "RHI-ORIGINAL-STREAM-RENDER-PROFILE"]:
            self.assertIn(token,contract)
        program=self.read_source("src/rhi/frontend/command_stream_program.h")
        for token in ["stateContractVersion()!=budget.contractVersion", "renderDescriptorUsage.matches",
                      "RHI-ORIGINAL-STREAM-RENDER-FOOTPRINT-MISMATCH", "visitRenderDescriptorAccesses",
                      "out.firstImplicitAccess=c.implicitAccesses", "chargeImplicit", "CommandUsageKind::BindRenderDescriptorTable"]:
            self.assertIn(token,program)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_typed_stream_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyTextures", "implicit==9u", "operation.implicitAccessCount==3u",
                      "RHI-ORIGINAL-STREAM-STATE-PROFILE", "RHI-ORIGINAL-STREAM-QUOTA-DOMAIN"]:
            self.assertIn(token,fixture)

    def test_typed_descriptor_state_requires_exact_sequence_and_transaction_plan(self):
        state=self.read_command_contract_sections("src/rhi/frontend/command_model.h","src/rhi/semantics/command_state_lowering.h","original_command_state_admission")
        for token in ["struct TypedDecision", "contractVersion=2u", "descriptorConsumption", "RHI-ORIGINAL-STATE-LEGACY-SCHEMA"]:
            self.assertIn(token,state)
        graph=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_resource_states")
        for token in ["device.originalDeviceLifetimeTicket()", "const TypedDecision consumptionDecision_", "Consumption::walkOriginal", "budget_.descriptorConsumption"]:
            self.assertIn(token,graph)
        walker=self.read_source("src/rhi/frontend/command_usage.h", "frontend_command_descriptor_consumption_h")
        self.assertEqual(walker.count("friend class "),1)
        self.assertIn("friend class rhi::OriginalCommandResourceStates",walker)
        sequence=self.read_source("src/rhi/frontend/command_state.h", "frontend_command_state_sequence_h")
        for token in ["RHI-ORIGINAL-SEQUENCE-STATE-PROFILE", "maxImplicitAccesses", "bufferBefore(*graph", "RHI-ORIGINAL-SEQUENCE-SHADER-STATE"]:
            self.assertIn(token,sequence)
        transaction=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_state_transaction")
        for token in ["RHI-ORIGINAL-TYPED-TRANSACTION-PLAN-REQUIRED", "RHI-ORIGINAL-TRANSACTION-FOOTPRINT-MISMATCH", "graph_->visitDescriptorAccesses", "plan.steps.values().size()!=result.steps"]:
            self.assertIn(token,transaction)
        for forbidden in ["CpuReplay", "Dummy", "WebGpuNative", "createBindGroup"]:
            self.assertNotIn(forbidden,transaction)
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        self.assertIn("stateContractVersion()!=stateVersion",closure)
        self.assertIn("RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-PROFILE",closure)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)

    def test_typed_stream_uses_same_state_root_and_keeps_native_authority_closed(self):
        source=self.read_validation_section("src/rhi/frontend/command_stream_program.h","original_command_stream_program")
        for token in ["using Root=std::variant", "inputs_.descriptorAt(index)", "inspectTyped", "prepareOriginalTyped",
                      "OriginalCommandAccessValidation::inspect", "graph_->visitDescriptorAccesses", "compareOriginalStates",
                      "RHI-ORIGINAL-STREAM-FOOTPRINT-MISMATCH", "RHI-ORIGINAL-STREAM-TYPED-STATE-REQUIRED",
                      "maxPipelineBinds", "maxDescriptorBinds", "maxConsumptions", "maxImplicitAccesses"]:
            self.assertIn(token,source)
        self.assertNotIn("const std::shared_ptr<const OriginalCommandInputs> inputs_;",source)
        for forbidden in ["OriginalCommandStateTransaction", "MutationJournal", "GpuDevice&", "wgpu", "makeShared", "std::vector"]:
            self.assertNotIn(forbidden,source)
        graph=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_resource_states")
        self.assertIn("friend class OriginalCommandStreamProgram;",graph)
        walker=self.read_source("src/rhi/frontend/command_usage.h", "frontend_command_descriptor_consumption_h")
        self.assertEqual(walker.count("friend class "),1)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_usage.h","src/rhi/semantics/command_lowering.h","original_command_stream")
        self.assertIn("RHI-ORIGINAL-STREAM-LEGACY-SCHEMA",contract)
        for backend in ["VULKAN", "METAL", "WEBGPU"]:
            self.assertIn("RHI-ORIGINAL-STREAM-"+backend+"-EXECUTOR-MISSING",contract)
            self.assertIn("RHI-ORIGINAL-BINDLESS-STREAM-"+backend+"-EXECUTOR-MISSING",contract)

    def test_original_native_closure2_schema_cannot_bypass_typed_stream(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_closure")
        for token in ["maxPipelineSources", "maxDescriptorSources", "RHI-ORIGINAL-NATIVE-CLOSURE-LEGACY-SCHEMA", "RHI-ORIGINAL-NATIVE-CLOSURE-TYPED-SOURCE-BUDGET", "typedLowering"]:
            self.assertIn(token,contract)
        typed=contract.split("static constexpr Decision typedLowering",1)[1]
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend,typed)
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        prepare=closure.split("static std::shared_ptr<const WebGpuCanonicalNativeClosure> prepareOriginalImpl",1)[1].split("public:",1)[0]
        self.assertLess(prepare.index("RHI-WEBGPU-CANONICAL-CLOSURE-TYPED-STREAM-REQUIRED"),prepare.index("Root::validateOriginal(root)"))
        self.assertIn("stateContractVersion()!=stateVersion",prepare)

    def test_original_native_closure2_loss_bound_requires_exact_registration7_subscription(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_closure")
        decision=contract.split("static constexpr Decision typedLossBoundLowering",1)[1].split("// Availability",1)[0]
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend,decision)
        self.assertEqual(decision.count("false,false,false"),3)
        self.assertIn("D::Prototype,true,false,false",decision)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        entry=source.split("prepareOriginalTypedLossBound",1)[1].split("private:",1)[0]
        self.assertLess(entry.index("validateIntent(budget)"),entry.index("validateOriginalDeviceNativeLoss(loss)"))
        self.assertIn("typedLossBoundLowering(device.backend())",entry)
        self.assertIn("prepareOriginalImpl",entry)
        self.assertLess(entry.index("validateOriginalTypedNativeLossJoin(loss)"),entry.index("prepareOriginalImpl"))
        registry=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        self.assertIn("RHI-ORIGINAL-PIPELINE-NATIVE-LOSS-JOIN-MISSING",registry)
        self.assertIn("RHI-ORIGINAL-DESCRIPTOR-NATIVE-LOSS-JOIN-MISSING",registry)

    def test_original_cold_profiles_require_legacy_state_and_closure_before_lowering(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_cold")
        self.assertIn("stateVersion==1u && closureVersion==1u",contract)
        decision=contract.split("static constexpr TypedDecision typedLowering",1)[1].split("static constexpr UploadDecision uploadLowering",1)[0]
        self.assertEqual(decision.count("false,false,false"),4)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend,decision)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_cold_admission.h")
        derive=source.split("static const char* deriveRequirements",1)[1].split("private:",1)[0]
        self.assertLess(derive.index("validateLegacyProfiles"),derive.index("Requirements pending"))
        inspect=source.split("static const char* inspect(const",1)[1]
        self.assertLess(inspect.index("Closure::validateOriginal"),inspect.index("validateLegacyProfiles"))
        self.assertLess(inspect.index("validateLegacyProfiles"),inspect.index("nativeLossDiagnostic"))
        self.assertLess(inspect.index("validateLegacyProfiles"),inspect.index("Reservation::validateOriginal"))

    def test_original_native_closure2_keeps_one_root_and_native_payload_inventory(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["prepareOriginalTyped", "Stream::validateOriginal", "compareOriginalStates", "maxPipelineSources", "maxDescriptorSources", "diagnoseOriginalClosureBinding", "diagnoseOriginalClosurePipeline", "validateConsumedOriginal", "allocationRecord()", "SharedIdentityProjection::inspect", "stream_"]:
            self.assertIn(token,source)
        self.assertEqual(source.count("core::BoundedSequence<Entry>"),1)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)
        payload=self.read_source("src/rhi/backends/webgpu/webgpu_native_payloads.h")
        diagnostic=payload.split("struct WebGpuNativeDescriptorGroupPayload",1)[1].split("public:",1)[0]
        self.assertIn("diagnoseOriginalClosureBinding",diagnostic)

    def test_original_finite_descriptor_native_identity_is_factory_frozen(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        for token in ["prepared.originalGroup=payload->group", "prepared.originalLayout=payload->layout",
                      "group!=state.originalGroup", "layout!=state.originalLayout", "current.buffer->ownerDevice!=state.nativeDevice",
                      "state.entries[i].binding==physical", "finiteNativeIdentityDiagnostic()"]:
            self.assertIn(token,source)
        contract=self.read_validation_section("src/rhi/frontend/descriptor_validation.h","original_descriptor_native_identity")
        for token in ["Request::Profile::WholeWrite1", "prototypeOptIn", "executionAvailable=false"]:
            self.assertIn(token,contract)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend,contract)
        identity=source.split("const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalNativeIdentity(",1)[1].split("const char* WebGpuNativeDescriptorGroupPayload::diagnoseOriginalFiniteGroup",1)[0]
        for token in ["RHI-ORIGINAL-WHOLE-WRITE-UNCOMMITTED", "state.validate()", "originalScopedContentDiagnostic()", "evidence().proven()", "finiteNativeIdentityDiagnostic()"]:
            self.assertIn(token,identity)
        self.assertLess(identity.index("state.validate()"),identity.index("state.descriptor.get()->originalScopedContentDiagnostic()"))
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)

    def test_original_whole_write_uses_one_scoped_candidate_and_final_L2_commit(self):
        contract=self.read_validation_section("src/rhi/frontend/descriptor_buffer_write_validation.h","original_whole_descriptor_write")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend,contract)
        for token in ["prototypeOptIn", "maxCpuBytes", "RHI-ORIGINAL-WHOLE-WRITE-RANGE-PROFILE", "RHI-ORIGINAL-WHOLE-WRITE-DYNAMIC-PROFILE"]:
            self.assertIn(token,contract)
        l2=self.read_source("src/rhi/gpu_descriptors.h")
        finish=l2.split("const char* finishOriginalWholeWrite() noexcept",1)[1].split("const char* cancelOriginalWholeWrite",1)[0]
        self.assertLess(finish.index("finishOriginalWholeWriteImpl()"),finish.index("commitContentWrite()"))
        source=self.read_command_contract_sections("src/rhi/frontend/command_sources.h","src/rhi/semantics/command_lowering.h","original_command_descriptor_source")
        self.assertEqual(source.count("object.originalScopedContentDiagnostic()"),1)
        self.assertEqual(source.count("object->originalScopedContentDiagnostic()"),1)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        self.assertIn("std::variant<rhi::OriginalFiniteDescriptorWriteRequest,rhi::OriginalWholeDescriptorWriteRequest>",native)
        self.assertIn("prepareOriginalScopedImpl",native)
        self.assertEqual(native.count("Lifecycle lifecycle{Contract::callbackCount}"),1)
        lowering=self.read_source("src/rhi/backends/webgpu/webgpu_original_whole_buffer_binding_lowering.h")
        for token in ["OriginalCommandBufferLease::validateOriginal", "originalDescriptorBufferBindingDiagnostic", "issued.facts()", "originalTargetBufferMemory", "entry.offset=0u;entry.size=source->sizeBytes"]:
            self.assertIn(token,lowering)
        for forbidden in ["RuntimeLengthInterval", "runtimeArrayLength", "validateFiniteInterval", "makeShared", "makeUnique"]:
            self.assertNotIn(forbidden,lowering)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)

    def test_original_typed_loss_registry_requires_explicit_request_and_weak_issued_subscription(self):
        contract=self.read_validation_section("src/rhi/frontend/resource_loss_validation.h","original_typed_native_loss_join")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::"+backend,contract)
        for token in ["registrationVersion=7u", "contractVersion!=1u", "prototypeOptIn", "acceptedPipelineJoinAvailable=false,scopedGroupJoinAvailable=false", "recordingAvailable=false,executionAvailable=false"]:
            self.assertIn(token,contract)
        registry=self.read_source("src/rhi/frontend/resource_registration.h")
        for token in ["std::weak_ptr<const OriginalDeviceNativeLossBinding> loss_", "loss_.lock()", "loss->hasQuotaBoundSharedOwner(loss)", "loss->diagnostic()", "loss->originalMetadataQuota()", "RHI-ORIGINAL-TYPED-LOSS-EXPLICIT-REQUEST-REQUIRED"]:
            self.assertIn(token,registry)
        self.assertNotIn("std::shared_ptr<const OriginalDeviceNativeLossBinding> loss_",registry)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        entry=device.split("const char* GpuDevice::prepareOriginalTypedNativeLossRegistration",1)[1].split("private:",1)[0]
        self.assertLess(entry.index("validate(request,budget)"),entry.index("validateOriginalDeviceNativeLoss(loss)"))
        self.assertLess(entry.index("validateOriginalDeviceNativeLoss(loss)"),entry.index("prepareOriginalResourceRegistrationImpl"))
        self.assertEqual(device.count("originalLossBinding_ && originalRegistry_->budget_.contractVersion<=6u"),2)
        # Finite, whole and general set transactions plus depth table publication.
        self.assertEqual(device.count("return OriginalTypedNativeLossJoinContract::groupDiagnostic()"),4)
        self.assertIn("OriginalTypedNativeLossJoinContract::pipelineDiagnostic()",device)
        for source in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp"]:
            self.assertIn("OriginalTypedNativeLossRegistrationFixture::verifyContract()",CppSource((self.ROOT / source).read_text(encoding="utf-8-sig")))
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_native_loss_registration_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.verify(peak,registryFactory)", "ticket.diagnostic()", "subscription->setLimitBytes(bytes-1u)", "verifyHotNoAllocation", "verifyNoHotQueries", "OBSERVATION-FOREIGN", "SHUTDOWN"]:
            self.assertIn(token,fixture)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& typedLossRegistrationContract && typedLossRegistrationNative",runner)

    def test_original_typed_loss_pipeline_joins_accepted_payload_to_existing_captured_source_before_publication(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_device_native_loss.h")
        for kind, factory in [("Compute", "Compute"),("Render", "Graphics")]:
            method=source.split("const char* validate(const rhi::"+kind+"Pipeline& pipeline)",1)[1].split("\n    }",1)[0]
            self.assertIn("WebGpuOriginalArtifact"+factory+"Factory::validateConsumedOriginal(payload)",method)
            self.assertIn("payload->issuedContract()->nativeDevice()==device_",method)
            self.assertNotIn("emscripten_webgpu_get_device",method)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        publish=device.split("core::UniquePtr<Pipeline> GpuDevice::createOriginalPipeline(",1)[1].split("template<class Control>",1)[0]
        self.assertLess(publish.index("acceptedWrapperSealDiagnostic(graphicsCreation)"),publish.index("validateOriginalNativeLossPipeline(*candidate)"))
        self.assertLess(publish.index("validateOriginalNativeLossPipeline(*candidate)"),publish.index("record->object_=candidate.get()"))
        self.assertIn("creation->usesCpuQuotaDomain(loss->originalMetadataQuota())",device)
        self.assertIn("originalRegistry_->overlapsCpuQuota(wrapperQuota)",device)
        self.assertEqual(device.count('return "RHI-ORIGINAL-TYPED-LOSS-PIPELINE-NATIVE-SOURCE-UNAVAILABLE";'),2)
        self.assertIn("return validateOriginalNativeLossPipeline(object)",device)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_native_loss_pipeline_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["registry.verify(registryPeak,factory)", "wrapper.verify(wrapperPeak,factory)", "WebGpuForeignDeviceFixture::Scope", "PIPELINE-NATIVE-SOURCE", "verifyActualLoss", "NATIVE-LOST", "verifyHotNoAllocation", "verifyNoHotQueries"]:
            self.assertIn(token,fixture)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)

    def test_original_typed_loss_bound_join_matches_exact_registry_and_subscription_before_allocation(self):
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        entry=device.split("const char* GpuDevice::validateOriginalTypedNativeLossJoin",1)[1].split("const char* GpuDevice::detachOriginalDeviceNativeLoss",1)[0]
        for token in ["validateOriginalDeviceNativeLoss(observation)", "registrationVersion", "originalRegistry_->validate()", "originalRegistry_->loss_.lock()", "sameOwner(subscription,observation.binding_)", "sameOwner(originalRegistry_->domain_,observation.domain_)"]:
            self.assertIn(token,entry)
        self.assertNotIn("makeShared",entry)
        # The general transaction consumes the same group loss subscription.
        self.assertEqual(device.count("validateOriginalTypedNativeLossGroupInputs(object,pipeline,quotas)"),3)
        group=device.split("const char* GpuDevice::validateOriginalTypedNativeLossGroupInputs",1)[1].split("public:",1)[0]
        self.assertIn("validateOriginalResourceRegistration(*pipeline)",group)
        self.assertIn("independentCpuQuotaDiagnostic(quota)",group)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& typedLossBoundClosures && typedLossBoundTerminals && typedLossBoundPhysicalLoss",runner)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)

    def test_typed_native_cold_has_independent_value_schema_without_native_or_legacy_adaptation(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_shader_validation.h","src/rhi/semantics/command_lowering.h","original_typed_native_cold")
        for token in ["OriginalTypedNativeColdRequest", "contractVersion!=1u", "prototypeOptIn", "synchronizationVersion!=1u", "OriginalUsageScopeBoundaries1", "maxWork", "maxCallbacks", "maxEncoders", "maxCommandBuffers", "maxRetainedOwners", "maxCpuMetadataBytes", "core::MemoryBudgetLedger::canAdd", "commands=nativeCalls=0u", "COUNT-OVERFLOW", "CALLBACK-RESERVATION"]:
            self.assertIn(token,contract)
        for token in ["makeShared", "makeUnique", "wgpu", "OriginalCommandNativeColdBudget", "prepareOriginal", "DeviceCapabilities", "applyTo"]:
            self.assertNotIn(token,contract)
        self.assertIn("arp::rhi::semantics::OriginalCommandNativeColdLowering::typedLowering(backend)",contract)
        legacy=self.read_command_contract_sections("src/rhi/frontend/command_recording.h","src/rhi/semantics/command_lowering.h","original_command_native_cold")
        self.assertIn("stateVersion==1u && closureVersion==1u",legacy)
        decision=legacy.split("static constexpr TypedDecision typedLowering",1)[1].split("static constexpr UploadDecision uploadLowering",1)[0]
        self.assertEqual(decision.count("false,false,false"),4)
        for file in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp", "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"]:
            self.assertIn("OriginalTypedNativeColdContractFixture::verifyContract()",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_native_cold_contract_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["UINT64_MAX", "UINT32_MAX", "verifyHotNoAllocation", "COUNT-TOTALS", "WORK-CAPACITY", "no-provenance-or-sync-proof"]:
            if token=="WORK-CAPACITY":self.assertIn(token,contract)
            else:self.assertIn(token,fixture)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)

    def test_dependent_shader_probe_preserves_original_actors_and_native_proof_boundary(self):
        source=CppSource((self.ROOT / "tests/fixtures/original_typed_shader_dependency_probe_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["descriptor_state_slots.computeMain.entry.yaml", "OriginalWholeDescriptorWriteFixture::commitActual",
                      "OriginalCommandDescriptorStateFixture::verify", "prepareOriginalTypedLossBound", "Projection::visit",
                      "Scope::inspect", "DEPENDENCY-NOT-FROZEN", "originalTarget", "real-completion-map",
                      "wgpuQueueSubmit", "wait(queue) && map", "wgpuBufferGetConstMappedRange", "context->retire()"]:
            self.assertIn(token,source)
        for forbidden in ["createShaderModule", "createComputePipeline", "commitOriginalPendingOwnership", "applyOriginalPrototypePlan",
                          "WebGpuRecordedCommandProof::", "NativeSync", "std::vector", "std::make_shared"]:
            self.assertNotIn(forbidden,source)
        state=CppSource((self.ROOT / "tests/fixtures/original_command_descriptor_state_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("footprint.maxLogicalAccesses",state)
        self.assertNotIn("originalCommands==17u",state)
        stream=CppSource((self.ROOT / "tests/fixtures/original_command_typed_stream_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("b.maxConsumptions=footprint.maxConsumptions",stream)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for flag in ["typedShaderDependencyProbeContract", "typedShaderDependencyProbe", "typedShaderDependencyProbeRelease"]:
            self.assertIn("&& "+flag,runner)
            self.assertIn(flag+": "+flag,runner)

    def test_original_root_scoped_projection_is_private_and_preserves_public_guards(self):
        walker = self.read_source("src/rhi/frontend/command_texture_ranges.h").split(
            "class OriginalCommandBindlessRootAccesses final", 1)[1].split(
            "class OriginalCommandTextureRanges final", 1)[0]
        public = walker.split("private:", 1)[0]
        self.assertIn("return visitOriginal<false>", public)
        self.assertNotIn("visitValidatedOriginal", public)
        private = walker.split("private:", 1)[1]
        for token in ["friend class arp::rhi::OriginalCommandResourceStates;",
                      "friend class arp::rhi::OriginalCommandStreamProgram;",
                      "friend class arp::rhi::OriginalCommandStateTransaction;",
                      "return visitOriginal<true>", "if constexpr (!Validated)",
                      "OriginalCommandInputs::validateOriginal(inputs)",
                      "root.readValidatedDeclaration", "root.readValidatedShaderDeclaration",
                      "root.readOriginalDeclaration", "root.readOriginalShaderDeclaration"]:
            self.assertIn(token, private)
        source = self.read_source("src/rhi/frontend/command_sources.h")
        root = source.split("class ARP_API OriginalCommandBindlessRootSource final", 1)[1].split(
            "// endregion original_command_bindless_root_source", 1)[0]
        self.assertIn("friend class common::OriginalCommandBindlessRootAccesses;", root)
        private = root.rsplit("private:", 1)[1]
        for name in ["readValidatedDeclaration", "readValidatedShaderDeclaration"]:
            self.assertIn(name, private)
            self.assertNotIn(name, root.split("public:", 1)[1].split("private:", 1)[0])
        impl = self.read_source("src/rhi/frontend/bindless_command_source.cpp")
        read = impl.split("OriginalCommandBindlessRootSource::readOriginalDeclaration(", 1)[1].split(
            "OriginalCommandBindlessRootSource::readValidatedDeclaration(", 1)[0]
        self.assertIn("if (!valid())", read)
        self.assertIn("return readValidatedDeclaration", read)
        shader = impl.split("OriginalCommandBindlessRootSource::readOriginalShaderDeclaration(", 1)[1].split(
            "OriginalCommandBindlessRootSource::readValidatedShaderDeclaration(", 1)[0]
        self.assertIn("if (const auto* id = validate())", shader)
        self.assertIn("return readValidatedShaderDeclaration", shader)

    def test_original_sequence_and_range_validation_scopes_keep_public_entry_guards(self):
        walker = self.read_source("src/rhi/frontend/command_texture_ranges.h").split(
            "class OriginalCommandBindlessRootAccesses final", 1)[1].split(
            "class OriginalCommandTextureRanges final", 1)[0]
        self.assertIn("friend class OriginalCommandStateSequence;", walker.split("private:", 1)[1])
        self.assertIn("validateScopedGraphicsBufferRanges", walker)
        self.assertIn("validateOriginalGraphicsBufferRanges", walker)
        state = self.read_source("src/rhi/frontend/command_state.h")
        sequence = state.split("class OriginalCommandStateSequence final", 1)[1].split(
            "class OriginalCommandAccessValidation final", 1)[0]
        self.assertIn("Graph::validateOriginal(graph)", sequence)
        self.assertEqual(sequence.count("OriginalCommandBindlessRootAccesses::visitValidatedOriginal("), 3)
        dependency_scope = sequence.split("static const char* validateRootBufferDependencies(", 1)[1].split(
            "// The scoped CPU projector", 1)[0]
        for token in ("RecordedResourceAccessDependencies", "maxDependencyVisits",
                      "OriginalCommandIndirectAccesses::visit(", "dependencies.consume(combined)"):
            self.assertIn(token, dependency_scope)
        self.assertLess(sequence.index("Graph::validateOriginal(graph)"),
                        sequence.index("validateRootBufferDependencies(*graph, result)"))
        self.assertNotIn("OriginalCommandBindlessRootAccesses::visit(", sequence)
        source = self.read_source("src/rhi/frontend/command_sources.h")
        root = source.split("class ARP_API OriginalCommandBindlessRootSource final", 1)[1].split(
            "// endregion original_command_bindless_root_source", 1)[0]
        public, private = root.split("public:", 1)[1].rsplit("private:", 1)
        impl = self.read_source("src/rhi/frontend/bindless_command_source.cpp")
        for public_name, private_name in [
                ("originalOrdinaryBufferRequirement", "readValidatedOrdinaryBufferRequirement"),
                ("originalOrdinaryBufferMemberMemory", "readValidatedOrdinaryBufferMemberMemory"),
                ("validateOriginalGraphicsBufferRanges", "validateScopedGraphicsBufferRanges")]:
            self.assertIn(private_name, private)
            self.assertNotIn(private_name, public)
            guarded = impl.split("OriginalCommandBindlessRootSource::" + public_name + "(", 1)[1].split(
                "OriginalCommandBindlessRootSource::" + private_name + "(", 1)[0]
            self.assertIn("validateOriginal(root)", guarded)
            self.assertIn("return " + private_name, guarded)
        scoped = impl.split("OriginalCommandBindlessRootSource::validateScopedGraphicsBufferRanges(", 1)[1].split(
            "bool OriginalCommandBindlessRootSource::matchesCanonicalSources", 1)[0]
        self.assertIn("readValidatedOrdinaryBufferRequirement(root", scoped)
        self.assertIn("readValidatedOrdinaryBufferMemberMemory(root", scoped)
        self.assertNotIn("validateOriginal(root)", scoped)

    def test_original_plan_population_validates_once_without_exposing_unguarded_queries(self):
        state = self.read_source("src/rhi/frontend/command_state.h")
        populate = state.split("const char* populatePlan(Plan& plan) const noexcept", 1)[1].split(
            "const char* validateRetainedPlan", 1)[0]
        self.assertEqual(populate.count("OriginalCommandResourceStates::validateOriginal(graph_)"), 1)
        self.assertLess(populate.index("OriginalCommandResourceStates::validateOriginal(graph_)"),
                        populate.index("sources(*graph_).sources()"))
        self.assertIn("Timeline::bufferBefore(*graph_", populate)
        self.assertIn("Timeline::resolveValidatedTextureCell(*graph_", populate)
        for token in ["Timeline::resolveOriginalBuffer", "Timeline::resolveOriginalTextureCell",
                      "std::function", "static thread_local", "visitor(", "emit("]:
            self.assertNotIn(token, populate)
        timeline = self.read_source("src/rhi/frontend/command_state.h", "frontend_command_state_sequence_h")
        public = timeline.split("static const char* resolveOriginalBuffer", 1)[1].split(
            "private:", 1)[0]
        self.assertEqual(public.count("Graph::validateOriginal(graph)"), 2)
        self.assertIn("friend class arp::rhi::OriginalCommandStateTransaction;", timeline)
        self.assertIn("resolveValidatedTextureCell", timeline.split("private:", 1)[1])

    def test_original_dependency_projection_reuses_complete_bounded_state_and_usage_walkers(self):
        source=self.read_source("src/rhi/frontend/command_usage_projection.h", "frontend_command_typed_dependency_projection_h")
        for token in ["P::captureStateBound", "P::visitCaptured", "S::validateBufferContribution", "S::validateBufferPair",
                      "OrderedIntervalNeighborhood", "resolveCapturedBuffer", "resolveCapturedTextureCell", "OriginalTextureStateRange::capture",
                      "isWholeAspect", "registrationGeneration", "descriptorIndex", "referenceIndex", "textureLayout", "maxProjectionPasses",
                      "maxContributionVisits", "maxPairChecks", "maxEndpointContributions", "maxTextureCells", "Endpoint", "validateCoverage",
                      "InitialBookkeeping", "BetweenScopes", "FinalBookkeeping", "IN-SCOPE-OR-AMBIGUOUS", "PARTIAL-TEXTURE-RANGE",
                      "STENCIL-PLANE-UNPROVEN", "ENDPOINT-COVERAGE", "Result{id,{}}"]:
            self.assertIn(token,source)
        for token in ["GpuDevice&", "GpuDevice*", "makeShared", "makeUnique", "wgpu", "friend class", "std::vector", "NativeSync"]:
            self.assertNotIn(token,source)
        usage=self.read_source("src/rhi/frontend/command_usage_projection.h", "frontend_command_typed_usage_projection_h")
        for token in ["class CapturedJoin", "inspectStateBound(stream,graph,request)", "visitCaptured", "validateTypedProjectionJoin()",
                      "visitCapturedRenderAccesses", "visitCapturedDescriptorAccesses", "visitValidatedOriginal"]:
            self.assertIn(token,usage)
        consumption=self.read_source("src/rhi/frontend/command_usage.h", "frontend_command_descriptor_consumption_h")
        self.assertEqual(consumption.count("friend class "),1)
        self.assertIn("template<bool Joined=false,class Visitor> static Result walkOriginal",consumption)
        timeline=self.read_source("src/rhi/frontend/command_state.h", "frontend_command_state_sequence_h")
        self.assertEqual(timeline.count("friend class "),2)
        self.assertIn("friend class OriginalTypedUsageProjection;",timeline)
        self.assertIn("friend class arp::rhi::OriginalCommandStateTransaction;",timeline)
        self.assertIn("return resolveValidatedTextureCell(*graph",timeline)
        self.assertIn("OriginalCommandStateSequence::bufferBefore(*joined.graph_",usage)
        self.assertIn("OriginalCommandStateSequence::resolveValidatedTextureCell(*joined.graph_",usage)
        self.assertIn("measureRetained(factory)",CppSource((self.ROOT / "tests/fixtures/original_typed_dependency_decision_fixture.h").read_text(encoding="utf-8-sig")))
        core=CppSource((self.ROOT / "src/core/ordered_interval_neighborhood.h").read_text(encoding="utf-8-sig"))
        for token in ["maxInputs_", "ContainsPoint", "AmbiguousBoundary", "result_={status,{},{},0u}"]:
            self.assertIn(token,core)
        tests=CppSource((self.ROOT / "tests/unit/bounded_retention_tests.cpp").read_text(encoding="utf-8-sig"))
        self.assertIn("testOrderedIntervalNeighborhood();",tests)
        self.assertIn("std::next_permutation(permutation.begin(),permutation.end())",tests)

    def test_original_loss_observer_core_has_bounded_terminal_lifetime_without_native_authority(self):
        core=CppSource((self.ROOT / "src/core/runtime/bounded_terminal_observers.h").read_text(encoding="utf-8-sig"))
        for token in ["core::BoundedSequence<core::UniquePtr<Slot>>", "core::makeUnique<Slot>()", "core::MonotonicIdSequence domains_", "generations_", "ExternalCallbackGate", "ticket.domain!=domain_", "slot.generation==ticket.generation", "output={}", "terminal_=event", "slot.callback(slot.owner.get(),event)", "finishDelivery()", "SuppressionFailed", "Status::Reentrant"]:self.assertIn(token,core)
        self.assertLess(core.index("terminal_=event"),core.index("slot.callback(slot.owner.get(),event)"))
        self.assertLess(core.index("if(!suppress(slot.owner.get()))"),core.index("slot.gate->detach()"))
        for token in ["CompletionRetentionPool", "std::vector", "std::make_unique", "std::make_shared", "signalFromHost", "wgpu", "rhi/", "new ", "delete "]:self.assertNotIn(token,core)
        contract=self.read_validation_section("src/rhi/frontend/resource_device_validation.h","original_device_native_loss_observer")
        for token in ["output={}", "OriginalDeviceNativeLossContract::validateIntent", "registrationCount", "callbackCount", "maxObserverSlots", "maxBindingOwnedObjects", "OWNER-OVERFLOW", "SUBSCRIPTION-COUNT", "bindingAvailable=false", "observerIssuanceAvailable=false", "executionAvailable=false"]:self.assertIn(token,contract)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:self.assertIn("Backend::"+backend,contract)
        for token in ["backends/", "CapturedJoin", "MutationJournal", "WebGpuRecordedCommandProof", "DeviceCapabilities", "applyTo"]:self.assertNotIn(token,contract)
        fixture=CppSource((self.ROOT / "tests/fixtures/bounded_terminal_observers_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["detached<8u", "Event::Lost,Event::Shutdown,Event::Retired", "Status::SuppressionFailed", "Status::Reentrant", "next.domain!=previous.domain", "UINT64_MAX", "pressure.measureRetained(factory)", "pressure.verify(bytes,factory)", "verifyHotNoAllocation", "borrowed[i].expired()"]:self.assertIn(token,fixture)
        for file in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp", "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"]:
            self.assertIn("OriginalDeviceNativeLossObserverContractFixture::verify()",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))
        self.assertIn("BoundedTerminalObserversFixture::verify()",CppSource((self.ROOT / "tests/unit/bounded_retention_tests.cpp").read_text(encoding="utf-8-sig")))
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& typedLossObserverContract",runner);self.assertIn("typedLossObserverContract: typedLossObserverContract",runner)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)

    def test_original_observed_loss_binding_reuses_native_subscription_without_issuing_receivers(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_observed_native_loss.h")
        for token in ["Contract::inspect", "observers_(request.maxObserverSlots)", "publishNative(device,binding)", "shareOriginal", "core::makeUnique", "holdNativeLease()", "suppressNativeBorrow()", "Terminal::Shutdown", "Terminal::Retired", "Terminal::NativeLost", "LIVE-DETACH", "terminalDiagnostic_"]:self.assertIn(token,source)
        self.assertLess(source.index("suppressNativeBorrow()"),source.index("notify(Terminal::Shutdown);return id"))
        for token in ["prepareWebGpuNativeDeviceLossRegistration", "CompletionRetentionPool", ".attach(", "WebGpuRecordedCommandProof", "std::make_shared", "std::vector", "CapturedJoin"]:self.assertNotIn(token,source)
        legacy=self.read_source("src/rhi/backends/webgpu/webgpu_original_device_native_loss.h")
        self.assertEqual(legacy.count("prepareWebGpuNativeDeviceLossRegistration("),1)
        self.assertLess(legacy.index("receiver_.deliverNativeLoss()"),legacy.index("binding->afterNativeLoss()"))
        fields=legacy.split("const rhi::OriginalDeviceLifetime::NativeLossReceiver receiver_;",1)[1].split("};",1)[0]
        for token in ["request_", "observers_", "terminalDiagnostic_"]:self.assertNotIn(token,fields)
        device=self.read_rhi_api_and_implementation("src/rhi/gpu_device.h")
        for token in ["prepareOriginalLossBinding", "if(!lowering.bindingAvailable)", "bindOriginalObservedNativeLoss", "originalLossBinding_->retireObservers()", "originalLossBinding_->shutdown()"]:self.assertIn(token,device)
        observation=self.read_source("src/rhi/frontend/resource_loss_validation.h", "frontend_resource_native_loss_observation_h")
        public=observation.split("public:",1)[1].split("private:",1)[0]
        for token in ["WGPU", "nativeState_", "NativeLossReceiver", "attach(", "detach(", "deliver("]:self.assertNotIn(token,public)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_observed_native_loss_binding_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure(factory)", "pressure.verify(peak,factory)", "captureNext(0u,true)", "s.phase=4", "droppedBinding_.expired()", "WebGpuForeignDeviceFixture::Scope", "verifyHotNoAllocation", "verifyActualLoss", "no-typed-observer-issued"]:self.assertIn(token,fixture)
        self.assertNotIn(".attach(",fixture)
        for file in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp", "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"]:
            self.assertIn("OriginalObservedNativeLossBindingFixture::verifyContract()",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for flag in ["typedLossObserverBindingContract", "typedLossObserverBindingNative", "typedLossObserverBindingLoss"]:
            self.assertIn("&& "+flag,runner);self.assertIn(flag+": "+flag,runner)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)

    def test_graphics_command8_keeps_connection_scoped_and_successor_authority_closed(self):
        command=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_cold_command")
        for token in ["request.contractVersion>8u", "additionalOwners=3u",
                      "request.contractVersion==admissionVersion", "generalLifecycleVersion(request.contractVersion)",
                      "request.maxRetainedOwners>admissionCeiling", "rootOwners>source.maxGraphOwners"]:
            self.assertIn(token,command)
        lowering=self.read_validation_section("src/rhi/semantics/command_lowering.h","original_typed_native_cold_command")
        graphics=lowering.split("if(version==8u)",1)[1].split("// Command6/7",1)[0]
        for backend in ["NULL-VALIDATOR-ONLY","VULKAN-JOIN-MISSING","METAL-JOIN-MISSING","WEBGPU-CONNECTION-ONLY"]:
            self.assertIn("RHI-ORIGINAL-GRAPHICS-COLD-COMMAND-"+backend,graphics)
        self.assertEqual(graphics.count("false,false,false"),3)
        self.assertEqual(graphics.count("D::Prototype,true,false,false"),1)
        callback=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_callback_custody")
        self.assertIn("request.contractVersion>8u",callback)
        for region in ["original_typed_issuer_preparation","original_typed_native_execution"]:
            successor=self.read_validation_section("src/rhi/frontend/command_shader_validation.h",region)
            self.assertIn("request.contractVersion>8u",successor)
        test=self.read_source("tests/contract/original_general_graphics_content_contract_tests.cpp")
        for token in ["BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_INTENT_OK",
                      "C::validateOwners(admissionOwners,intent,source,lifecycle,admissionOwners,46u)",
                      "C::validateOwners(admissionOwners,intent,source,lifecycle,ceiling,49u)"]:
            self.assertIn(token,test)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        declaration=fixture.index("const auto graphicsCommandOwners=graphicsNativeCommand ?")
        publication=fixture.index("auto sources=Sources::prepareOriginal",declaration)
        self.assertLess(declaration,publication)
        for token in ["textureOwners+graphicsCommandOwners", "verifyComposed(cold,true)",
                      "if(command)cold.reset()", "command.reset();cold.reset()",
                      "BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_RETAIN_OK", "BINDLESS_RHI_GENERAL_GRAPHICS_COMMAND8_RETIRE_OK"]:
            self.assertIn(token,fixture)

    def test_graphics_callback8_observer_join_keeps_recording_and_execution_closed(self):
        callback=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_callback_custody")
        for token in ["request.contractVersion>8u", "request.contractVersion==8u && !cold.work.completeGraphicsCounts()",
                      "RHI-TYPED-CALLBACK-CUSTODY-GRAPHICS-PROFILE", "3u+request.maxCallbackSlots",
                      "cold.contractVersion!=request.contractVersion", "rootOwners>source.maxGraphOwners",
                      "request.maxRetainedOwners>commandCeiling", "request.maxRetainedOwners>admissionCeiling"]:
            self.assertIn(token,callback)
        lowering=self.read_validation_section("src/rhi/semantics/command_lowering.h","original_typed_callback_custody")
        graphics=lowering.split("if(version==8u)",1)[1].split("// Callback6/7",1)[0]
        for backend in ["NULL-VALIDATOR-ONLY", "VULKAN-CUSTODY-MISSING", "METAL-CUSTODY-MISSING", "WEBGPU-OBSERVER-ONLY"]:
            self.assertIn("RHI-ORIGINAL-GRAPHICS-CALLBACK-"+backend,graphics)
        self.assertEqual(graphics.count("false,false,false"),3)
        self.assertEqual(graphics.count("false,true,false,false"),1)
        self.assertNotIn("true,true",graphics)
        for region in ["original_typed_issuer_preparation", "original_typed_native_execution"]:
            self.assertIn("request.contractVersion>8u",self.read_validation_section("src/rhi/frontend/command_shader_validation.h",region))
        fixture=self.read_source("tests/contract/original_general_graphics_content_contract_tests.cpp")
        for token in ["BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_INTENT_OK", "C::additionalOwners(intent)==11u",
                      "participants.commandNativeCallbacks==7u", "participants.reservedCallbacks==8u",
                      "participants.sharedNativeLossCallbacks==1u", "C::validateOwners(241u,0u,intent,252u,252u,source,lifecycle,49u)"]:
            self.assertIn(token,fixture)
        workload=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        self.assertLess(workload.index("const auto graphicsCallbackOwners="),workload.index("auto sources=Sources::prepareOriginal",workload.index("const auto graphicsCallbackOwners=")))
        for token in ["textureOwners+graphicsCommandOwners+graphicsCallbackOwners", "OriginalTypedCallbackCustodyFixture::verifyGraphics8(command,graphicsIssuerOwners+graphicsOutputOwners)",
                      "if(custody)command.reset()", "custody->cancelOriginal()", "BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_RETAIN_OK", "BINDLESS_RHI_GENERAL_GRAPHICS_CALLBACK8_RETIRE_OK"]:
            self.assertIn(token,workload)
        self.assertIn("callbackQuotas[i]->liveBytes()==callbackQuotaBaseline[i]",workload)

    def test_typed_cold_command_is_separate_opaque_connection_and_sticky_claim(self):
        contract=self.read_command_contract_sections("src/rhi/frontend/command_shader_validation.h","src/rhi/semantics/command_lowering.h","original_typed_native_cold_command")
        for token in ["additionalOwners=3u", "maxCommandPayloads!=1u", "OWNER-OVERFLOW", "maxGraphOwners", "maxRecordingOwners", "maxSubmissionOwners", "maxCallbackOwners"]:self.assertIn(token,contract)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:self.assertIn("Backend::"+backend,contract)
        wrapper=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_command.h")
        public=wrapper[wrapper.index("    static std::shared_ptr<const WebGpuOriginalTypedColdCommand> prepareOriginal"):wrapper.index("private:")]
        for forbidden in ["WGPU", "command_", "device_", "queue_", "recordingIdentity()", "Callback", "Proof"]:self.assertNotIn(forbidden,public)
        for forbidden in ["WebGpuCanonicalColdAdmission", "OriginalCommandNativeColdCommandBudget", "WebGpuRecordedCommandProof", "CapturedJoin", "MutationJournal"]:self.assertNotIn(forbidden,wrapper)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        typed=source[source.index("const char* WebGpuNativeCommandPreparation::inspectTypedColdCommand"):source.index("class WebGpuCanonicalCopyPassLowering::Payload")]
        for token in ["WebGpuOriginalTypedColdAdmission::validateOriginal", "C::validateOwners", "core::CpuMemoryQuotaScope", "allocate({})", "connect(*command,closure->nativeDevice()", "validateTypedUnclaimed(*result)", "seal->nativeColdCommandClaimed_=true", "inspectShellIdentity", "state.finished || state.submitted || state.encoder || state.commandBuffer || state.renderPass || state.computePass"]:self.assertIn(token,typed)
        self.assertLess(typed.index("validateTypedUnclaimed(*result)"),typed.index("seal->nativeColdCommandClaimed_=true"))
        for forbidden in ["wgpuDeviceCreateCommandEncoder", "wgpuQueueSubmit", "prepareWebGpuNativeDeviceLossRegistration", "applyOriginal", "commitOriginal", "validateCanonical", "CapturedJoin"]:self.assertNotIn(forbidden,typed)
        impl=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_payload_impl.h")
        self.assertIn("std::shared_ptr<const void> canonicalColdAdmission",impl)
        self.assertNotIn("typedColdAdmission",impl)
        header=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.h")
        self.assertIn("validateTypedUnclaimed",header[header.index("private:"):])
        self.assertNotIn("bool",header[header.index("validateTypedOriginal"):header.index("static bool open")])
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording", "WebGpuOrdinarySubmission"])
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for flag in ["typedColdCommandContract", "typedColdCommandNative"]:
            self.assertIn("&& "+flag,runner);self.assertIn(flag+": "+flag,runner)
        for file in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp", "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"]:
            self.assertIn("OriginalTypedColdCommandFixture::verifyContract()",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_cold_command_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["pressure.measure(factory)", "verifySingleUse(bytes,rebuild,factory)", "verifyHotNoAllocation", "prepareRetainedPlan", "mutateActual", "forgedAdmission", "retained.expired()", "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE"]:self.assertIn(token,fixture)

    def test_typed_cold_admission_is_independent_original_seal_and_source_authority(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_admission.h")
        for token in ["WebGpuOriginalTypedNativePayload", "OriginalCommandLifecycleReservation", "OriginalCommandRecordingSeal", "WebGpuErrorScopeContract::count", "OriginalDeviceNativeLossContract::callbackCount", "OriginalDeviceNativeLossContract::registrationCount", "required.work=payload->work()", "C::validateSourceAuthority", "seal->compareOriginalRoot(root)", "R::validateOriginal(reservation)", "core::makeUnique", "core::CpuMemoryQuotaScope", "shareOriginal", "Status::Prepared", "journalEmpty", "OriginalTypedRenderStateContract::validate", "overlapsNativeOwner", "RHI-WEBGPU-TYPED-COLD-RESERVATION-DEMAND", "RHI-WEBGPU-TYPED-COLD-LIVE-EXECUTOR-MISSING"]:
            self.assertIn(token,source)
        for token in ["WebGpuCanonicalColdAdmission", "WebGpuRecordedCommandProof", "wgpuDevice", "wgpuQueue", "applyOriginal", "commitOriginal", "validateRetainedOwnership", "CapturedJoin"]:
            self.assertNotIn(token,source)
        render=self.read_validation_section("src/rhi/frontend/command_shader_usage_validation.h","original_typed_render_state")
        for token in ["viewport=scissor=false", "viewportCommandDiagnostic", "scissorCommandDiagnostic", "std::isfinite", "RHI-ORIGINAL-TYPED-RENDER-STATE-INCOMPLETE"]:
            self.assertIn(token,render)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_shader_validation.h","src/rhi/semantics/command_lowering.h","original_typed_native_cold")
        for token in ["maxNativePayloadObjects", "maxTypedNativeCommandCalls", "maxPendingSubmissions", "maxCallbackTickets", "maxCallbackRegistrations", "SOURCE-PAYLOAD-OVERFLOW", "admissionLowering"]:
            self.assertIn(token,contract)
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:self.assertIn("Backend::"+backend,contract)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for flag in ["typedColdAdmissionContract", "typedColdAdmissionMetadataReject", "typedColdAdmissionNative"]:
            self.assertIn("&& "+flag,runner)
            self.assertIn(flag+": "+flag,runner)
        for file in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp", "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"]:
            self.assertIn("OriginalTypedColdAdmissionFixture::verifyContract()",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))

    def test_typed_retained_factors_preserve_public_guards_and_exact_private_comparisons(self):
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        public=closure.split("const char* validate() const noexcept",1)[1].split("const char* compareOriginalRoot",1)[0]
        for token in ["Root::validateOriginal(root_)","Root::Status::Prepared","Stream::validateOriginal(stream_)","compareOriginalStates(root_->originalGraph())"]:
            self.assertLess(public.index(token),public.index("return validateImmutableBindings();"))
        native=closure.split("const char* validateImmutableBindings() const noexcept",1)[1].split("static ",1)[0]
        self.assertGreater(closure.rfind("private:",0,closure.index("const char* validateImmutableBindings")),closure.index("const char* executionDiagnostic()"))
        for token in ["entry.sourceIndex!=i","entry.object.matches(*current)","validateEntry(entry,tables)","validateTypedBinding(*root_,entry,general)","inspectPayloads","inspectOwners","inspectAllocations","allocations.bytes==bytes_"]:
            self.assertIn(token,native)
        allocation=closure.split("static core::SharedAllocationProjection::Result inspectAllocations",1)[1].split("static std::size_t inspectPayloads",1)[0]
        for token in ["SharedAllocationProjection::inspect", "entryAllocationSlots", "stagingAllocationRecord", "allocationRecord"]:
            self.assertIn(token,allocation)
        payload=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_native_payload.h")
        public=payload.split("const char* validate() const noexcept",1)[1].split("const char* compareOriginalDecision",1)[0]
        self.assertLess(public.index("preflight(decision_,request_)"),public.index("return validateImmutableRecords();"))
        self.assertIn("QUOTA-DOMAIN",public)
        replay=payload.split("const char* validateImmutableRecords() const noexcept",1)[1].split("static bool sameCounts",1)[0]
        for token in ["walk(*decision_,request_","record.matches(records_.values()[index++])","pass.matches(","sameCounts(inspected_,replay)","PASS-COVERAGE","ORIGINAL-BINDING"]:
            self.assertIn(token,replay)
        for text in [native,replay]:
            for token in ["Status::Pending","P::visit","captureStateBound","applyOriginal","commitOriginal","makeUnique","makeShared","bool skip","requireClaim"]:
                self.assertNotIn(token,text)
        self.assertIn("private:\n    // Reuse the original walker",payload)
        self.assertIn("friend class WebGpuCanonicalCallbackCustody;",payload)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(re.findall(r"friend class (\w+);",proof),["WebGpuOrdinaryRecording","WebGpuOrdinarySubmission"])

    def test_typed_native_payload_binds_same_decision_and_shared_pass_builder_without_issuer(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_native_payload.h")
        for token in ["D::validateOriginal", "sourceEntry(closure,original.sourceIndex)", "sourceEntry(closure,original.pipelineSourceIndex)", "record.dependency=&decision.records()[dependencyIndex++]", "scalar.matches", "original.matches", "inspectWorkTotals", "validateWork", "CpuMemoryQuotaScope", "core::makeUnique", "core::BoundedSequence<Record>", "core::BoundedSequence<Pass>", "LIFECYCLE-EXECUTOR-MISSING", "Pass(record.pass,record.passDrawCount)", "passes_.visitMutable", "sameCounts(inspected,replay)"]:
            self.assertIn(token,source)
        for forbidden in ["wgpu", "WebGpuRecordedCommandProof", "OriginalCommandNativeColdBudget", "std::vector", "std::make_shared", "DeviceCapabilities", "applyTo", "commitOriginal"]:
            self.assertNotIn(forbidden,source)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_shader_validation.h","src/rhi/semantics/command_lowering.h","original_typed_native_payload")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:self.assertIn("Backend::"+backend,contract)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)
        for file in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp", "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"]:
            self.assertIn("OriginalTypedNativePayloadFixture::verifyContract()",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for flag in ["typedNativePayloadContract", "typedNativePayloadFrozen"]:
            self.assertIn("&& "+flag,runner)
            self.assertIn(flag+": "+flag,runner)

    def test_table_payload2_has_exact_versioned_profile_and_consumes_lowering(self):
        contract=self.read_source("src/rhi/frontend/command_shader_validation.h")
        region=contract.split("class OriginalTypedNativePayloadContract final",1)[1].split("// endregion",1)[0]
        for token in ["request.contractVersion<1u || request.contractVersion>8u", "freezeVersion==1u && closureVersion==2u", "freezeVersion==2u && closureVersion==3u"]:
            self.assertIn(token,region)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_native_payload.h")
        for token in ["C::profileMatches(request,decision->originalRequest().contractVersion,closure->closureContractVersion())",
                      "OriginalTypedNativePayloadLowering::lowering(rhi::Backend::WebGPU,request.contractVersion)",
                      "!lowering.payloadAvailable", "case K::BindRenderDescriptorTable", "case K::BindComputeDescriptorTable",
                      "table && request.contractVersion!=2u", "originalPhysicalGroup(record,physicalGroup)"]:
            self.assertIn(token,source)

    def test_geometry_readback_composition_preserves_exact_profiles_and_closed_native_gates(self):
        contract=self.read_source("src/rhi/frontend/command_shader_validation.h")
        for token in ["version==4u || version==5u", "version==3u || version==5u",
                      "validateWork(shader,maximum,geometry)", "freezeVersion==5u && closureVersion==7u",
                      "COMPOSED-READBACK-REQUIRED",
                      "COMPOSED-GEOMETRY-REQUIRED", "READBACK-COMPLETION-REQUIRED",
                      "cold.contractVersion==request.contractVersion"]:
            self.assertIn(token,contract)
        joins=self.read_validation_section("src/rhi/frontend/command_transfer_validation.h", "original_command_readback_join")
        for token in ["b.contractVersion!=2u && b.contractVersion!=3u", "programProfileMatches",
                      "stateVersion==4u && inputsVersion==5u", "b.stream.contractVersion==4u",
                      "program->compareOriginalStates(root->originalGraph())", "program->compareOriginalInputs(inputs)"]:
            self.assertIn(token,joins)
        for forbidden in ["makeUnique", "makeShared", "wgpu", "MutationJournal", "commitOriginal"]:
            self.assertNotIn(forbidden,joins)
        lowering=self.read_source("src/rhi/semantics/command_lowering.h")
        for token in ["geometryReadbackLossBoundLowering", "RHI-TYPED-NATIVE-READBACK-COMPLETION-REQUIRED",
                      "RHI-TYPED-NATIVE-EXECUTION-METAL-PAYLOAD-MISSING"]:
            self.assertIn(token,lowering)
        for path in ["tests/contract/rhi_command_retention_contract_tests.cpp",
                     "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp",
                     "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"]:
            self.assertIn("OriginalGeometryReadbackCompositionFixture::verifyContract()",self.read_source(path))
        public=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.execution=rhi::BindlessExecution::DiagnosticFailure",public)
        self.assertIn("caps_.executablePayload=false",public)

    def test_geometry_payload4_consumes_original_custody_without_live_authority(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h", "original_typed_native_payload")
        for token in ["freezeVersion==4u && closureVersion==6u", "GEOMETRY-SCHEMA", "GEOMETRY-BUDGET",
                      "work.hasGeometry()", "request.contractVersion==4u"]:
            self.assertIn(token,contract)
        cold=self.read_validation_section("src/rhi/frontend/command_shader_validation.h", "original_typed_native_cold")
        for token in ["vertexBinds", "indexBinds", "indexedDraws", "hasGeometry()", "GEOMETRY-SCHEMA",
                      "work.vertexBinds", "work.indexBinds", "work.indexedDraws", "MemoryBudgetLedger::canAdd"]:
            self.assertIn(token,cold)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_native_payload.h")
        for token in ["case K::BindVertexBuffer", "case K::BindIndexBuffer", "case K::DrawIndexed",
                      "OriginalCommandGeometryBindingContract::validate", "record.binding->indexContent",
                      "record.pipeline->generation!=original.pipelineRegistrationGeneration", "record.geometry",
                      "bufferBindingBytes", "a.work.matches(b.work)", "GEOMETRY-PROFILE"]:
            self.assertIn(token,source)
        for forbidden in ["wgpu", "commitOriginal", "applyOriginal", "WebGpuRecordedCommandProof", "std::vector"]:
            self.assertNotIn(forbidden,source)
        fixture=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        self.assertIn('"geometry-indexed3",4u',fixture)
        self.assertIn("payload->work().indexedDraws==3u",fixture)
        self.assertIn("payload.reset()",fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("&& originalGeometryNativePayloadPassed",runner)
        self.assertIn("originalGeometryNativePayloadPassed: originalGeometryNativePayloadPassed",runner)

    def test_geometry_execution4_reuses_original_native_emitter_and_callback_completion(self):
        contract=self.read_source("src/rhi/frontend/command_shader_validation.h")
        for cls in ["OriginalTypedNativeColdCommandContract", "OriginalTypedCallbackCustodyContract",
                    "OriginalTypedIssuerPreparationContract", "OriginalTypedNativeExecutionContract"]:
            section=contract.split("class "+cls,1)[1].split("// endregion",1)[0]
            maximum=8
            self.assertIn(f"request.contractVersion<1u || request.contractVersion>{maximum}u",section)
        self.assertIn("RHI-TYPED-CALLBACK-CUSTODY-GEOMETRY-PROFILE",contract)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        emission=native.split("void WebGpuOrdinaryRecording::emitTyped(",1)[1].split("WebGpuOrdinaryRecording::recordTyped(",1)[0]
        for token in ["case K::BindVertexBuffer", "case K::BindIndexBuffer", "case K::DrawIndexed",
                      "record.physicalVertexSlot", "record.bufferBindingBytes", "wgpuRenderPassEncoderSetVertexBuffer",
                      "wgpuRenderPassEncoderSetIndexBuffer", "wgpuRenderPassEncoderDrawIndexed",
                      "d->firstIndex,d->vertexOffset,d->firstInstance"]:
            self.assertIn(token,emission)
        for token in ["wgpuQueueSubmit", "wgpuBufferGetSize", "makeShared", "makeUnique"]:
            self.assertNotIn(token,emission)
        fixture=self.read_source("tests/fixtures/original_typed_cold_admission_fixture.h")
        for token in ["geometryExecutionOwnerDemand", "Command::prepareOriginal(admission,commandRequest,id)",
                      "Custody::prepareOriginal", "Issuers::prepareOriginal", "Issuers::recordOriginal",
                      "Issuers::submitOriginal", "Scopes::holdScopes", "Root::Status::Committed",
                      "retainedCommand.expired()", "retainedIssuers.expired()", "GEOMETRY_NATIVE_EXECUTION4_OK"]:
            self.assertIn(token,fixture)
        self.assertIn("geometry Execution4 recording:",fixture)
        self.assertIn("issuers->recordingDiagnostic()",fixture)
        self.assertIn("Scopes::holdScopes(true)",fixture)
        geometry=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        preparation=geometry.split("static GeometryDependencyOwners prepareGeometryDependencyFreeze(",1)[1].split("static void verifyGeometryDependencyFreeze(",1)[0]
        for forbidden in ["executeGeometry", "emscripten_sleep", "waitHeldScopes", "wgpuQueueSubmit"]:
            self.assertNotIn(forbidden,preparation)
        for token in ["return {std::move(admission)", "retained", "inputsQuota,stateQuota,streamQuota,recording"]:
            self.assertIn(token,preparation)
        execution=geometry.split("static void verifyGeometryDependencyFreeze(",1)[1].split("template<class Analyzer,class Visitor>",1)[0]
        self.assertLess(execution.index("prepareGeometryDependencyFreeze<CompleteRenderState>"),execution.index("executeGeometry"))
        self.assertIn("Root::Status::Committed",execution)
        self.assertIn("owners.retained.expired()",execution)
        self.assertIn("!quota->liveBytes()",execution)
        self.assertIn("caps_.executablePayload=false",self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp"))
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("&& originalGeometryNativeExecutionPassed",runner)

    def test_geometry_pending_reads_require_original_capture_and_seal_journal(self):
        model=self.read_source("src/rhi/frontend/command_model.h")
        for token in ["OriginalInitializedBufferPendingContract", "ImmutableGeometryRead1", "captured==expected", "initial==expected", "final==expected", "pending==expected"]:
            self.assertIn(token,model)
        root=self.read_source("src/rhi/frontend/command_state.h")
        body=root.split("const char* validateInitializedBufferPending(",1)[1].split("struct OriginalContentUpload",1)[0]
        for token in ["graph_->budget_.contractVersion!=4u", "contractVersion!=5u", "drawConsumptionContractVersion!=1u", "!geometry.drawConsumptionOptIn", "captured.revision()", "matchesPending(*cell->scalarPending,journal_)"]:
            self.assertIn(token,body)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h", "backends_webgpu_webgpu_canonical_buffer_upload_source_h")
        self.assertIn("committed->diagnostic()",source)
        self.assertIn("validateInitializedIndexContentWithReady",source)
        self.assertIn("validateInitializedVertexRangeWithReady",source)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        body=native.split("const char* WebGpuNativeCommandPreparation::validateTypedAdmissionRetained(",1)[1].split("const char* WebGpuNativeCommandPreparation::validateTypedRetained(",1)[0]
        for token in ["source.initializedReadyFacts()", "source.state_->committed", "seal->validateInitializedBufferPending", "ImmutableGeometryRead1", "closure->validateImmutableBindings(geometryReady)", "geometryPendingReadLowering"]:
            self.assertIn(token,body)
        fixture=self.read_source("tests/fixtures/original_typed_cold_admission_fixture.h")
        geometry=fixture.split("static void executeGeometry(",1)[1].split("static void verifyMetadataRefusal",1)[0]
        for token in ["initialized->readyDiagnostic()", "validateInitializedIndexContent(", "RHI-CANONICAL-UPLOAD-SOURCE-STATE-STALE", "hasOriginalRecordedCommand()", "Root::Status::Committed"]:
            self.assertIn(token,geometry)
        for token in ["assert(submissionBase&&!callbackBase)", "submission->liveBytes()==submissionBase", "callback->liveBytes()==callbackBase"]:
            self.assertIn(token,geometry)
        self.assertLess(geometry.index("retainedIssuers.expired()"),geometry.index("retainedIssuers.reset()"))
        for token in ["retainedCommand.reset()", "retainedIssuers.reset()"]:
            self.assertLess(geometry.index(token),geometry.index("custody.reset()"))
        ownership=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        final=ownership.split("static void verifyGeometryDependencyFreeze(",1)[1].split("template<class Analyzer,class Visitor>",1)[0]
        self.assertLess(final.index("owners.admission.reset()"),final.index("!submission->liveBytes()"))
        self.assertLess(final.index("assert(owners.retained.expired())"),final.index("owners.retained.reset()"))
        self.assertLess(final.index("owners.retained.reset()"),final.index("for(const auto& quota:owners.quotas)"))
        for token in ["originalReservedSubmissionCount()", "originalReservedCallbackTicketCount()", "originalReservedCallbackRegistrationCount()"]:
            self.assertIn(token,final)

    def test_geometry_cold4_admits_only_paired_original_payload_and_explicit_render_state(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h", "original_typed_native_cold")
        for token in ["!geometryProfile(request.contractVersion) && request.maxWork.hasGeometry()",
                      "geometryProfile(request.contractVersion) && !work.completeGeometryCounts()",
                      "validateWork(required.work,request.maxWork,request.contractVersion==4u)", "GEOMETRY-BUDGET"]:
            self.assertIn(token,contract)
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_admission.h")
        for token in ["version<1u || version>8u", "C::validatePayloadProfile", "C::validateLifecycleProfile",
                      "OriginalTypedRenderStateContract::validate", "C::validateSourceAuthority"]:
            self.assertIn(token,backend)
        fixture=self.read_source("tests/fixtures/original_command_pipeline_binding_fixture.h")
        for token in ["CompleteRenderState", "completePositions", "indexedGeometryColdOwnerDemand",
                      "indexedGeometryCompleteCommandCount", "indexedGeometryCompleteNativeCalls",
                      "verifyGeometryDependencyFreeze<true>", "verifyGeometryRenderStateRefusal", "verifyGeometry(payload,device)",
                      "maxNativePayloadObjects=indexedSourceCount+2u", "maxTypedNativeCommandCalls=indexedGeometryCompleteNativeCalls"]:
            self.assertIn(token,fixture)
        authored=fixture.split("static void verifyGeometryAuthoredProgram()",1)[1].split("static void verifyContract()",1)[0]
        for token in ["geometryDependencyCommands<false>()", "geometryDependencyCommands<true>()",
                      "CommandUsageSequenceContract::validate", "CommandRecordingOrder::validate",
                      "RHI-COMMAND-ORDER-PIPELINE", "indexedSourceBudget(true)", "indexedSourceBudget(false)",
                      "C::validateSourceAuthority", "SOURCE-PAYLOAD-CAPACITY"]:
            self.assertIn(token,authored)
        geometry=fixture.split("static void verifyGeometryGraphics(",1)[1]
        self.assertIn("const auto budget=indexedSourceBudget(readable)",geometry)
        self.assertNotIn("budget.maxNativePayloadObjects=",geometry)
        self.assertIn("const auto commands=geometryDependencyCommands<CompleteRenderState>()",fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("&& originalGeometryColdAdmissionPassed",runner)
        self.assertIn("originalGeometryColdAdmissionPassed: originalGeometryColdAdmissionPassed",runner)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.executablePayload=false",device)

    def test_table_payload2_cannot_inherit_cold1_executor(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_admission.h")
        derive=source.split("static const char* deriveImmutableRequirements",1)[1].split("static const char* inspectBinding",1)[0]
        self.assertLess(derive.index("payload->originalRequest().contractVersion!=1u"),derive.index("payload->records()"))
        self.assertIn("RHI-WEBGPU-TYPED-COLD-PAYLOAD-PROFILE",derive)
        fixture=self.read_source("tests/fixtures/original_depth_table_content_plan_fixture.h")
        for token in ["OriginalTypedNativePayloadFixture::verify", "coldRefused.required.contractVersion", "coldRefused.required.work.commands",
                      "payload->passes().size()==4u", "pass.depth.depthLoadOp==WGPULoadOp_Clear", "pass.depth.depthStoreOp==WGPUStoreOp_Store"]:
            self.assertIn(token,fixture)

    def test_table_cold2_requires_paired_payload_and_versioned_lowering_before_claim(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_admission.h")
        derive=source.split("static const char* deriveImmutableRequirements",1)[1].split("static const char* inspectBinding",1)[0]
        self.assertIn("C::validatePayloadProfile(request,payload->originalRequest().contractVersion)",derive)
        self.assertIn("OriginalTypedNativeColdLowering::admissionLowering",derive)
        self.assertLess(derive.index("C::validatePayloadProfile"),derive.index("required=lifecycleRequirements(request.contractVersion)"))
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        inspect=native.split("const char* WebGpuNativeCommandPreparation::inspectTypedColdCommand",1)[1].split("prepareTypedOriginal",1)[0]
        self.assertIn("C::validateProfile(request,admission->originalRequest().contractVersion)",inspect)
        self.assertIn("OriginalTypedNativeColdCommandLowering::lowering",inspect)
        # Callback and retained scans preserve exact version pairing.
        callback=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_callback_custody")
        self.assertIn("cold.contractVersion!=request.contractVersion",callback)
        retained=native.split("const char* WebGpuNativeCommandPreparation::validateTypedRetained",1)[1].split("validateTypedOriginal",1)[0]
        admission=native.split("WebGpuNativeCommandPreparation::validateTypedAdmissionRetained",1)[1].split("WebGpuNativeCommandPreparation::validateTypedRetained(",1)[0]
        self.assertIn("OriginalTypedNativePayloadContract::profileMatches",admission)
        self.assertIn("OriginalTypedNativeColdContract::validatePayloadProfile",admission)
        self.assertIn("validateTypedAdmissionRetained(admission)",retained)
        self.assertIn("OriginalTypedNativeColdCommandContract::validateProfile",retained)
        fixture=self.read_source("tests/fixtures/original_depth_table_content_plan_fixture.h")
        for token in ["pressure.verifySingleUse(commandBytes,rebuild,connect)", "cold.reset();reservation.reset();seal.reset()",
                      "sameOwner(closure->originalStream(),retained.program)", "oldCallback", "ORIGINAL_TABLE_NATIVE_COLD2_OK"]:
            self.assertIn(token,fixture)

    def test_typed_dependency_decision_freezes_authentic_loss_closure_without_native_authority(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h")
        for token in ["Closure::validateOriginal", "C::closureProfileMatches(request.contractVersion,closure->closureContractVersion())", "closure->nativeLossDiagnostic()",
                      "compareOriginalClosure", "hasQuotaBoundSharedOwner", "CpuDomain::Recording", "CpuMemoryQuotaScope",
                      "core::makeUnique", "shareOriginal", "core::BoundedSequence", "P::inspect", "P::visit", "original.matches",
                      "EXECUTOR-PAYLOAD-MISSING", "InterScopeCommandListBoundary", "maxRetainedOwners", "maxCpuMetadataBytes"]:
            self.assertIn(token,source)
        for token in ["GpuDevice&", "GpuDevice*", "wgpu", "WebGpuRecordedCommandProof", "std::make_shared", "std::vector", "NativeSync", "friend class WebGpuOrdinary"]:
            self.assertNotIn(token,source)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_shader_validation.h","src/rhi/semantics/command_lowering.h","original_typed_dependency_projection")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:self.assertIn("Backend::"+backend,contract)
        for file in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp",
                     "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"]:
            self.assertIn("OriginalTypedDependencyDecisionFixture::verifyContract()",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for flag in ["typedDependencyDecisionContract", "typedDependencyDecisionFrozen", "typedDependencyDecisionIncompatibleRejected", "typedDependencyDecisionReject",
                     "typedDependencyDecisionComputePositions", "typedDependencyDecisionAttachmentPositions"]:
            self.assertIn("&& "+flag,runner)
            self.assertIn(flag+": "+flag,runner)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_dependency_decision_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("retainForActualLoss",fixture)
        self.assertIn("originalClosure()->nativeLossDiagnostic()",fixture)
        for file in ["tests/fixtures/original_command_descriptor_slots_fixture.h", "tests/fixtures/original_finite_descriptor_write_fixture.h"]:
            self.assertIn("OriginalTypedDependencyDecisionFixture::retainForActualLoss",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))

    def test_original_attachment_dependency_probe_preserves_original_intent_and_proof_boundary(self):
        source=CppSource((self.ROOT / "tests/fixtures/original_typed_attachment_dependency_probe_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["OriginalAcceptedGraphicsPipelineFactoryFixture::prepareContext", "OriginalRegisteredPipeline::shareAcceptedOriginal",
                      "Source::prepareOriginal", "LoadOp::Clear", "LoadOp::Load", "StoreOp::Store", "K::TextureMemoryBarrier",
                      "compareOriginalInput", "points[1].scope.first==11u", "visitStateBound", "DEPENDENCY-NOT-FROZEN",
                      "WebGpuAttachmentOperationLowering::load", "WebGpuAttachmentOperationLowering::store", "runtimeArrayLength",
                      "wgpuQueueSubmit", "wait(queue) && map", "bytes[y*pitch.alignedValue", "verifyHotNoAllocation",
                      "verifyNoHotQueries", "journalEmpty()", "!state.transaction->nativeExecutionProven()", "context->retire()"]:
            self.assertIn(token,source)
        for forbidden in ["createShaderModule", "createRenderPipeline", "applyOriginalPrototypePlan", "WebGpuRecordedCommandProof::",
                          "NativeSync", "std::vector", "std::make_shared"]:
            self.assertNotIn(forbidden,source)
        state=CppSource((self.ROOT / "tests/fixtures/original_command_descriptor_state_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["OriginalCommandTextureRanges::visit", "OriginalTextureStateRange::capture", "rangeCellCount", "bufferReferences+textureCells"]:
            self.assertIn(token,state)
        for file in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp",
                     "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"]:
            self.assertIn("OriginalTypedAttachmentDependencyProbeFixture::verifyContract()",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for flag in ["typedAttachmentDependencyProbeContract", "typedAttachmentDependencyProbe", "typedAttachmentDependencyProbeRelease"]:
            self.assertIn("&& "+flag,runner)
            self.assertIn(flag+": "+flag,runner)

    def test_state_bound_usage_projection_reuses_captured_join_and_entire_contribution_mapper(self):
        projection=self.read_source("src/rhi/frontend/command_usage_projection.h", "frontend_command_typed_usage_projection_h")
        for token in ["inspectStateBound", "visitStateBound", "OriginalCommandResourceStates::validateOriginal", "stream->compareOriginalStates(graph)",
                      "graph->visitRenderDescriptorAccesses", "graph->visitDescriptorAccesses", "walkContributions"]:
            self.assertIn(token,projection)
        joined=projection.split("static Result walkStateBound",1)[1].split("template<class Visitor> static Result walk(",1)[0]
        self.assertNotIn("GpuDevice",joined)
        self.assertNotIn("device.backend",joined)
        descriptor_mapper=projection.split("const auto buffer=",1)[1].split("const auto accessPoint=",1)[0]
        self.assertEqual(descriptor_mapper.count("use.offsetBytes=access.offsetBytes"),1)
        graph=self.read_validation_section("src/rhi/frontend/command_state.h","original_command_resource_states")
        self.assertIn("friend class common::OriginalTypedUsageProjection;",graph)
        self.assertIn("Consumption::renderBindingsOriginal",graph)
        self.assertIn("Consumption::walkOriginal(inputs_,footprint",graph)
        self.assertIn("device_.diagnostic()",graph)
        contract=self.read_command_contract_sections("src/rhi/frontend/command_shader_validation.h","src/rhi/semantics/command_lowering.h","original_typed_state_bound_usage")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:self.assertIn("Backend::"+backend,contract)
        for token in ["prototypeOptIn", "OriginalTypedUsageProjectionContract::validateBudget", "executionAvailable=false"]:self.assertIn(token,contract)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)
        consumption=self.read_source("src/rhi/frontend/command_usage.h", "frontend_command_descriptor_consumption_h")
        self.assertEqual(consumption.count("friend class "),1)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        for flag in ["typedStateBoundUsageContract", "typedStateBoundUsageNative", "typedStateBoundUsageReject", "typedStateBoundUsageRootReject"]:
            self.assertIn("&& "+flag,runner)
            self.assertIn(flag+": "+flag,runner)

    def test_typed_usage_scope_is_positional_bounded_value_validation_without_sync_authority(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_usage_validation.h","typed_shader_usage_scope")
        for token in ["CommandRecordingOrder", "commandIndex!=i", "ComputeDispatch,command,command", "Kind::RenderPass,begin,i", "core::containsUnsignedRange", "core::countUnorderedPairsUnsigned", "core::overlapsUnsignedRanges", "maxPairChecks", "INCOMPATIBLE-BUFFER-USAGE", "WRITABLE-ALIAS", "RecordedResourceStateSequence::validateBarrier", "RecordedResourceStateSequence::validateMemoryBarrier", "DEPENDENCY-NOT-FROZEN", "arp::rhi::semantics::OriginalTypedNativeColdLowering::lowering(backend)"]:
            self.assertIn(token,contract)
        for token in ["makeShared", "makeUnique", "wgpu", "friend class", "DeviceCapabilities", "applyTo", "ImplicitOrderedUsage"]:
            self.assertNotIn(token,contract)
        for file in ["tests/contract/rhi_command_retention_contract_tests.cpp", "tests/smoke/rhi_null_hidden_frame_smoke_tests.cpp", "tests/integration/rhi_webgpu_bindless_pipeline_browser_smoke.cpp"]:
            self.assertIn("TypedShaderUsageScopeContractFixture::verifyContract()",CppSource((self.ROOT / file).read_text(encoding="utf-8-sig")))
        self.assertIn("&& typedUsageScopeValueContract &&",CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")))

    def test_general_shader_usage_values_and_original_projection_keep_public_execution_closed(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_usage_validation.h","typed_shader_usage_scope")
        for token in ["UniformRead", "TextureUsage::StorageRead", "TEXTURE-WRITABLE-ALIAS",
                      "version==5u", "synchronizationProven()const noexcept{return false;}",
                      "nativeExecutionProven()const noexcept{return false;}"]:
            self.assertIn(token,contract)
        fixture=self.read_source("tests/fixtures/typed_shader_usage_scope_contract_fixture.h")
        for token in ["verifyGeneralShaderResources()", "old.contractVersion=4u", "BINDLESS_RHI_GENERAL_SHADER_USAGE5_VALUE_OK",
                      "verifyHotNoAllocation", "!d.recordingAvailable && !d.executionAvailable"]:
            self.assertIn(token,fixture)
        projection=self.read_source("src/rhi/frontend/command_usage_projection.h", "frontend_command_typed_usage_projection_h")
        self.assertIn("BufferUsage::UniformRead",projection)
        self.assertIn("TextureUsage::StorageRead",projection)
        self.assertIn("budget.contractVersion==5u",projection)
        self.assertIn("OriginalTypedUsageProjectionContract::programVersion",projection)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.execution=rhi::BindlessExecution::DiagnosticFailure",device)
        self.assertIn("caps_.executablePayload=false",device)

    def test_general_original_resources_reuse_source_consumption_and_exact_footprints(self):
        model=self.read_source("src/rhi/frontend/command_model.h")
        for token in ["generalShaderResourcesVersion", "generalShaderResourcesOptIn",
                      "validateGeneralResources", "RHI-ORIGINAL-CONSUMPTION-GENERAL-OPT-IN"]:
            self.assertIn(token,model)
        consumption=self.read_source("src/rhi/frontend/command_usage.h", "frontend_command_descriptor_consumption_h")
        for token in ["validateGeneralTextureFootprint", "sources.originalLeaseIndex(*view)",
                      "sources.originalLeaseIndex(parent)", "projected.textureParentRegistrationGeneration",
                      "budget.generalShaderResourcesVersion", "result.textureAccesses"]:
            self.assertIn(token,consumption)
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_usage_projection")
        for token in ["programVersion", "geometryUsage.empty() ? 3u : 4u",
                      "RHI-ORIGINAL-USAGE-PROJECTION-GENERAL-PROFILE"]:
            self.assertIn(token,contract)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["shareAcceptedOriginal", "Sources::appendOriginalTyped", "Inputs::prepareOriginal",
                      "P::visitStateBound", "Scope", "GENERAL_ORIGINAL_PROJECTION5_OK"]:
            if token != "Scope": self.assertIn(token,fixture)
        self.assertIn("Root::prepareOriginalOwnership",fixture)
        self.assertIn("root->materializeOriginalPrototypePlan",fixture)
        for forbidden in ["queueSubmit", "wgpuCommandEncoder", "setLimitBytes"]:
            self.assertNotIn(forbidden,fixture)

    def test_general_resource_input_profiles_preserve_legacy_table_and_geometry_admission(self):
        model=self.read_validation_section("src/rhi/frontend/command_model.h","original_command_descriptor_consumption")
        for token in ["inputsProfileMatches", "validateGeneralResources(footprintVersion,generalVersion,generalOptIn)",
                      "footprintVersion<1u || footprintVersion>4u", "inputsVersion==footprintVersion+2u",
                      "if (footprintVersion==4u) return inputsVersion==6u",
                      "footprintVersion==2u && (inputsVersion==3u || inputsVersion==4u)",
                      "footprintVersion==3u && inputsVersion==5u"]:
            self.assertIn(token,model)
        fixture=self.read_source("tests/fixtures/original_command_descriptor_consumption_fixture.h")
        for token in ["Profile::inputsProfileMatches(footprint,0u,false,inputs)==legacy",
                      "Profile::inputsProfileMatches(footprint,1u,true,inputs)==opted",
                      "!Profile::inputsProfileMatches(footprint,1u,false,inputs)",
                      "!Profile::inputsProfileMatches(footprint,0u,true,inputs)",
                      "Profile::inputsProfileMatches(footprint,2u,true,inputs)==(footprint==4u && inputs==6u)",
                      "OriginalCommandPipelineBindingContract::validateBudget({4u,true,1u,1u})"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        printed=runner.split("print: function (value)",1)[1].split("printErr: function (value)",1)[0]
        self.assertIn('message === "BINDLESS_RHI_GENERAL_ORIGINAL_PROJECTION_SCENARIO_OK private-submission-numerical-unproven-public-closed"',printed)
        self.assertIn("success:success && projected && inventory",runner)
        self.assertIn("originalGeneralResourceOwnerInventoryPassed:inventory",runner)

    def test_general_dependency5_reuses_usage5_and_leaves_native_freeze_closed(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_dependency_projection")
        self.assertIn("request.contractVersion!=4u && request.contractVersion!=5u",contract)
        self.assertIn("request.projection.contractVersion!=(request.contractVersion==7u ? 6u : request.contractVersion==6u ? 5u : request.contractVersion==5u ? 4u : request.contractVersion)",contract)
        projection=self.read_source("src/rhi/frontend/command_usage_projection.h","frontend_command_typed_dependency_projection_h")
        self.assertIn("OriginalTypedUsageProjectionContract::programVersion(request.usages.projection)",projection)
        self.assertIn("record.projectionVersion!=4u && record.projectionVersion!=5u",projection)
        self.assertIn("record.projectionVersion==4u || record.projectionVersion==5u",projection)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["D::visit(program,graph,dependency", "D::validateFrozenBinding(program,graph,dependency,record,frozenCounts)",
                      "BINDLESS_RHI_GENERAL_ORIGINAL_DEPENDENCY5_OK", "!projected.synchronizationProven()",
                      "validateFreezeIntent({5u,true,dependency,owners,bytes})", "!leaked && !refused.counts.dependencies"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("success:success && projected && inventory && dependencies",runner)
        self.assertIn("originalGeneralDependencyProjectionPassed:dependencies",runner)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.executablePayload=false",device)

    def test_original_typed_usage_projection_covers_every_render_binding_and_reuses_bound_views(self):
        consumption=self.read_source("src/rhi/frontend/command_usage.h", "frontend_command_descriptor_consumption_h")
        self.assertEqual(consumption.count("DescriptorBufferBindingRangeContract::resolve"),1)
        self.assertEqual(consumption.count("OriginalCommandTypedSourceContract::validateFullBoundView"),1)
        self.assertEqual(consumption.count("projectBoundView(descriptor,"),2)
        render=consumption.split("static RenderBindingResult renderBindings",1)[1].split("static constexpr bool isConsumption",1)[0]
        for token in ["kind!=K::BindRenderDescriptor", "OriginalCommandDescriptorBindingContract::validate", "RenderBindingResult{id,result.commandIndex,0u,0u}", "maxBindings", "scopeBeginCommandIndex", "pipelineBindCommandIndex"]:
            self.assertIn(token,render)
        self.assertNotIn("latestDescriptorSetBinding",render)
        projection=self.read_source("src/rhi/frontend/command_usage_projection.h", "frontend_command_typed_usage_projection_h")
        for token in ["Stream::validateOriginal", "stream->streamContractVersion()!=OriginalTypedUsageProjectionContract::programVersion(budget)", "C::visitRenderBindings", "C::visit(inputs,device", "OriginalCommandTextureRanges::visit", "DescriptorType::ReadOnlyStorageBuffer", "DescriptorType::StorageBuffer", "resolveRenderPass", "STENCIL-PLANE-UNPROVEN", "implicitAccesses", "attachmentReferences", "Result{id,{}}"]:
            self.assertIn(token,projection)
        for token in ["makeShared", "makeUnique", "BoundedSequence", "pmr::Vector", "friend class WebGpu", "applyOriginalPrototypePlan", "wgpu"]:
            self.assertNotIn(token,projection)
        runner=CppSource((self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig"))
        self.assertIn("&& typedUsageProjectionContract && typedUsageProjectionNative && typedUsageProjectionReject && originalRenderBindUsage &&",runner)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_command_descriptor_consumption_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("active.consumptions==1u && active.logicalAccesses==2u",fixture)
        self.assertEqual(re.findall(r"friend class ([^;]+);",projection),
                         ["OriginalTypedDependencyProjection","arp::rhi::OriginalGeneralOutputSnapshotContract","OriginalTypedUsageProjection"])
        self.assertIn("all.bindings==3u && all.logicalAccesses==6u",fixture)
        proof=self.read_source("src/rhi/backends/webgpu/webgpu_recorded_command_proof.h")
        self.assertEqual(proof.count("friend class "),2)

    def test_texture_usage2_preserves_distinct_points_and_captured_footprints(self):
        projection=self.read_source("src/rhi/frontend/command_usage_projection.h", "frontend_command_typed_usage_projection_h")
        for token in ["struct TexturePoint", "C::Access original", "ScopeContract::TextureUse", "renderBindingTextures", "shaderTextures",
                      "maxTextureAccesses", "maxTextureCells", "chargeCells", "OriginalTextureStateRange::capture",
                      "access.textureParentRegistrationGeneration", "use.range=access.textureRange", "FOOTPRINT-MISMATCH",
                      "budget.renderBindings.matches(original.renderDescriptorUsage)",
                      "stream->streamContractVersion()!=OriginalTypedUsageProjectionContract::programVersion(budget)"]:
            self.assertIn(token,projection)
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_usage_projection")
        for token in ["TEXTURE-SCHEMA", "TEXTURE-BUDGET", "b.contractVersion==3u ? 2u : b.contractVersion",
                      "shaderConsumption.contractVersion!=footprintVersion", "renderBindings.contractVersion!=footprintVersion"]:
            self.assertIn(token,contract)
        for forbidden in ["makeShared", "makeUnique", "pmr::Vector", "std::vector", "createCommandEncoder", "queueSubmit"]:
            self.assertNotIn(forbidden,projection)

    def test_mixed_usage3_keeps_both_original_readback_operands_and_independent_budget(self):
        projection=self.read_source("src/rhi/frontend/command_usage_projection.h", "frontend_command_typed_usage_projection_h")
        for token in ["struct ReadbackBufferPoint", "struct ReadbackTexturePoint", "retainedReadbackStateVersion!=1u",
                      "retainedReadbackStateOptIn", "originalTextureOwner()", "originalDestinationLease()",
                      "sameOwner(input->originalInput(),(*source)->originalInput())", "inventory.originalLeaseIndex(parent)",
                      "inventory.originalLeaseIndex(destination)", "READBACK-CORRESPONDENCE", "maxReadbacks", "maxReadbackMips",
                      "operation.referenceCount!=2u", "operation.firstReference+1u",
                      "parentIndex!=usage.references()[operation.firstReference].sourceIndex",
                      "maxReadbackDeclaredBytes", "ScopeContract::TextureUsage::CopySrc", "ScopeContract::BufferUsage::CopyDst",
                      "ResourceAccess::TransferRead", "ResourceAccess::TransferWrite", "READBACK-COVERAGE"]:
            self.assertIn(token,projection)
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_usage_projection")
        for token in ["READBACK-SCHEMA", "READBACK-BUDGET", "b.contractVersion==3u ? 2u : b.contractVersion"]:
            self.assertIn(token,contract)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_depth_table_content_plan_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["ORIGINAL_TABLE_READBACK_USAGE3_OK", "ReadbackBufferPoint", "ReadbackTexturePoint", "!emitted",
                      "verifyHotNoAllocation", "--invalid.projection.maxReadbackDeclaredBytes"]:
            self.assertIn(token,fixture)

    def test_mixed_dependency3_reuses_neighborhood_and_preserves_native_profile_refusal(self):
        projection=self.read_source("src/rhi/frontend/command_usage_projection.h", "frontend_command_typed_dependency_projection_h")
        for token in ["bufferUseOf", "P::ReadbackBufferPoint", "P::ReadbackTexturePoint",
                      "(record.projectionVersion==3u || record.projectionVersion==4u || record.projectionVersion==5u) && endpoint.scope.kind==S::Kind::TransferCopy",
                      "OrderedIntervalNeighborhood", "ENDPOINT-COVERAGE"]:
            self.assertIn(token,projection)
        lowering=self.read_command_contract_sections("src/rhi/frontend/command_shader_validation.h",
            "src/rhi/semantics/command_lowering.h", "original_typed_usage_projection")
        for token in ["case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU",
                      "JOIN-MISSING", "CapabilityMode::Equivalent,false,true,false,false,nullptr"]:
            self.assertIn(token,lowering)
        freeze=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_dependency_projection")
        self.assertIn("request.contractVersion!=1u && request.contractVersion!=2u && request.contractVersion!=3u && request.contractVersion!=4u && request.contractVersion!=5u && request.contractVersion!=6u)",freeze)
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        self.assertIn("RHI-WEBGPU-CANONICAL-CLOSURE-CONTENT-READBACK-PROFILE",closure)

    def test_texture_dependency2_visits_exact_cells_and_keeps_old_freeze_closed(self):
        projection=self.read_source("src/rhi/frontend/command_usage_projection.h", "frontend_command_typed_dependency_projection_h")
        for token in ["projectionVersion", "cellIndex(record.range,ordinal,index)", "cellRange(index,cell.range)",
                      "NONUNIFORM-TEXTURE-ENDPOINTS", "bindRangeNeighborhood", "validateTexturePair", "textureUseOf",
                      "texture.registrationGeneration!=record.registrationGeneration", "request.contractVersion==1u && !geometry->isWholeAspect(output.range)",
                      "record.projectionVersion==1u && !geometry->isWholeAspect(record.range)"]:
            self.assertIn(token,projection)
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_dependency_projection")
        self.assertIn("request.projection.contractVersion!=(request.contractVersion==7u ? 6u : request.contractVersion==6u ? 5u : request.contractVersion==5u ? 4u : request.contractVersion)",contract)
        self.assertIn("RHI-ORIGINAL-DEPENDENCY-FREEZE-PROFILE",contract)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_depth_table_publication_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyUsages", "verifyUsageUnavailable", "maxTextureCells", "maxPairChecks", "ENDPOINT-COVERAGE", "ORIGINAL_TABLE_USAGE_DEPENDENCY_OK"]:
            self.assertIn(token,fixture)
        smoke=CppSource((self.ROOT / "tests/contract/original_depth_table_expansion_preflight_smoke.py").read_text(encoding="utf-8-sig"))
        self.assertIn("ORIGINAL_TABLE_USAGE_DEPENDENCY_OK",smoke)

    def test_table_closure3_consumes_original_group_and_content_root_without_execution(self):
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["prepareOriginalTableLossBound", "budget.contractVersion!=3u",
                      "tableLossBoundLowering(device.backend())", "validateOriginalTypedNativeLossJoin(loss,{2u,true})",
                      "!root->plannedContentCellCount()", "stream->streamContractVersion()!=stateVersion",
                      "validateTableBinding", "matchesCanonicalSources(*originalSources)",
                      "sameOwner(*view,writes[i].view)", "depthContentDiagnostic()", "validateConsumedTableExpansion(p)"]:
            self.assertIn(token,closure)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_depth_table_content_plan_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyCustody", "pressure.verify(closureBytes,factory)", "pressure.verify(freezeBytes,freeze)",
                      "native->group=nullptr", "native->bindings[1u].textureView=native->bindings[0u].textureView",
                      "verifyHotNoAllocation", "verifyNoHotQueries", "verifyUnavailable"]:
            self.assertIn(token,fixture)
        lowering=self.read_source("src/rhi/semantics/command_lowering.h")
        self.assertIn("tableLossBoundLowering",lowering)
        for backend in ["NULL-VALIDATOR-ONLY", "VULKAN-JOIN-MISSING", "METAL-JOIN-MISSING", "WEBGPU-RECORDING-UNPROVEN"]:
            self.assertIn("RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-"+backend,lowering)

    def test_dependency_freeze2_preserves_exact_version_pair_and_public_refusal(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_dependency_projection")
        self.assertIn("request.projection.contractVersion!=(request.contractVersion==7u ? 6u : request.contractVersion==6u ? 5u : request.contractVersion==5u ? 4u : request.contractVersion)",contract)
        decision=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h")
        self.assertIn("C::closureProfileMatches(request.contractVersion,closure->closureContractVersion())",decision)
        self.assertIn("OriginalTypedDependencyProjectionLowering::freezeLowering",decision)
        self.assertIn("RHI-WEBGPU-TYPED-DEPENDENCY-EXECUTOR-PAYLOAD-MISSING",decision)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.executablePayload=false",device)

    def test_geometry_readback_cold_custody_preserves_legacy_recording_and_map_refusals(self):
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        capture=native.split("WebGpuCanonicalReadbackJoins::capture",1)[1].split("WebGpuCanonicalReadbackJoins::inspect",1)[0]
        self.assertIn("version!=1u && version!=2u && version!=3u",capture)
        immutable=native.split("WebGpuCanonicalReadbackJoins::validateImmutableProgramBindings()",1)[1].split("WebGpuCanonicalReadbackJoins::validateFrozenEntries",1)[0]
        self.assertIn("programProfileMatches(budget_",immutable)
        mapping=native.split("WebGpuCanonicalReadbackJoins::validateRetainedMapping",1)[1].split("WebGpuCanonicalReadbackJoins::overlapsCpuOwner",1)[0]
        self.assertIn("budget_.contractVersion!=1u && budget_.contractVersion!=2u",mapping)
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        entry=closure.split("prepareOriginalGeometryReadbackLossBound",1)[1].split("private:",1)[0]
        for token in ["budget.contractVersion!=7u", "geometryReadbackLossBoundLowering(device.backend())",
                      "device.validateOriginalTypedNativeLossJoin(loss,{lossVersion,true})", "descriptorDomainVersion(budget.contractVersion,*root)",
                      "Root::validateOriginal(root)", "std::move(readbacks),contents,count"]:
            self.assertIn(token,entry)
        self.assertIn("readbacks->joinContractVersion()!=(budget.contractVersion==7u ? 3u : 2u)",closure)
        mapping=closure.split("static bool usesTableExpansion",1)[1].split("static Entry inspectEntry",1)[0]
        for token in ["version!=7u", "version==3u || version==4u", "sources(root)->sources()",
                      "OriginalCommandDescriptorTableSource", "OriginalCommandDescriptorSetSource",
                      "pipelineSourceVersion(", "descriptorDomainVersion(version,root)==2u", "version,tables,ordinaryDescriptors)"]:
            self.assertIn(token,mapping)
        self.assertIn("usesTableExpansion(budget.contractVersion,*root)",closure)
        self.assertIn("usesTableExpansion(budget_.contractVersion,*root_)",closure)
        fixture=self.read_source("tests/fixtures/original_vertex_geometry_execution_fixture.h")
        for token in ["initializeStreams(device,resources)", "RenderAttachment|rhi::TextureUsage::CopySrc",
                      "createOriginalContentTexture", "BINDLESS_RHI_GEOMETRY_READBACK_NATIVE_CUSTODY_OK",
                      "root->journalEmpty()", "budget.maxGraphOwners", "allocated"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("geometryReadbackNativeCustodyPassed && geometryReadbackFrozenDependenciesPassed && geometryReadbackFrozenPayloadPassed && geometryReadbackColdAdmissionPassed && geometryReadbackColdCommandPassed && geometryReadbackExecutionPassed && !!coldFaultPreparation",runner)
        self.assertIn("executed && retired && geometryReadbackNativeCustodyPassed",runner)
        self.assertIn("geometryReadbackFrozenDependenciesPassed && geometryReadbackFrozenPayloadPassed && geometryReadbackColdAdmissionPassed && geometryReadbackColdCommandPassed && geometryReadbackExecutionPassed && progress.includes",runner)
        for token in ["Frozen::prepareOriginal(closure,frozenRequest,id)", "frozen->compareOriginalClosure(closure)",
                      "dependencies.counts.dependencies==5u", "accesses.counts.contributions==11u",
                      "--badFreeze.projection.maxDependencies", "--badFreeze.projection.maxPairChecks",
                      "--badFreeze.projection.maxContributionVisits", "badFreeze.contractVersion=4u",
                      "BINDLESS_RHI_GEOMETRY_READBACK_FREEZE5_OK", "frozen->validate()"]:
            self.assertIn(token,fixture)

    def test_composed_cold5_retains_exact_lifecycle_and_independent_quota_domains(self):
        lowering=self.read_source("src/rhi/semantics/command_lowering.h")
        region=lowering.split("class OriginalTypedNativeColdLowering final",1)[1].split("// endregion",1)[0]
        self.assertIn("version<1u || version>7u",region)
        self.assertIn("D::Prototype,true,false,false",region)
        admission=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_admission.h")
        self.assertIn("C::readbackProfile(request.contractVersion)",admission)
        self.assertIn("deriveRequirements(",admission)
        self.assertIn("C::validateSourceAuthority",admission)
        fixture=self.read_source("tests/fixtures/original_typed_cold_admission_fixture.h")
        for token in ["requestFor(payload,5u)","legacy.contractVersion=1u",
                      "--bad.readbackMap.maxTotalCallbackTickets","--bad.readbackMap.maxTotalCallbackRegistrations",
                      "pressure.verify(retained,factory)","verifyHotNoAllocation", "BINDLESS_RHI_GEOMETRY_READBACK_COLD5_OK"]:
            self.assertIn(token,fixture)
        geometry=self.read_source("tests/fixtures/original_vertex_geometry_execution_fixture.h")
        for token in ["budget.maxCallbackTickets=budget.maxCallbackRegistrations=12u",
                      "OriginalTypedColdAdmissionFixture::verifyComposed(payload,device)",
                      "retired[18u]=seal->originalSubmissionQuota()", "retired[19u]=seal->originalCallbackQuota()",
                      "BINDLESS_RHI_GEOMETRY_READBACK_COLD5_RETIRE_OK"]:
            self.assertIn(token,geometry)
        runner=(self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")
        self.assertIn('frozenPayloadMessages.length === 9 && Object.entries(frozenPayloadCases).every(',runner)
        for token in ['"finite-compute":4','"geometry-readback":1','geometryReadbackColdAdmissionPassed && geometryReadbackColdCommandPassed && geometryReadbackExecutionPassed && progress.includes',
                      'geometryReadbackColdAdmissionPassed && geometryReadbackColdCommandPassed && geometryReadbackExecutionPassed && !!coldFaultPreparation']:
            self.assertIn(token,runner)

    def test_composed_command5_connects_exact_admission_without_live_issuance(self):
        lowering=self.read_source("src/rhi/semantics/command_lowering.h")
        region=lowering.split("class OriginalTypedNativeColdCommandLowering final",1)[1].split("// endregion",1)[0]
        self.assertIn("version<1u || version>8u",region)
        self.assertIn("D::Prototype,true,false,false",region)
        fixture=self.read_source("tests/fixtures/original_typed_cold_command_fixture.h")
        for token in ["request{graphics ? 8u : 5u,true,1u,admission->originalRequest().maxRetainedOwners",
                      "admission->originalRequest().contractVersion==request.contractVersion",
                      "less.maxRetainedOwners=commandOwners-1u", "command->retainedOwnerCount()==commandOwners",
                      "Command::prepareOriginal(admission,legacy,id)",
                      "RHI-WEBGPU-TYPED-COLD-COMMAND-DUPLICATE", "forgedAdmission", "exhausted.charged()",
                      "verifyHotNoAllocation", "BINDLESS_RHI_GEOMETRY_READBACK_COMMAND5_OK"]:
            self.assertIn(token,fixture)
        geometry=self.read_source("tests/fixtures/original_vertex_geometry_execution_fixture.h")
        self.assertIn("OriginalTypedColdCommandFixture::verifyComposed(admission)",geometry)
        self.assertIn("admission.reset();assert(!retained.expired() && !command->validate())",geometry)
        runner=(self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")
        self.assertIn("geometryReadbackColdCommandPassed && geometryReadbackExecutionPassed && progress.includes",runner)
        self.assertIn("geometryReadbackColdCommandPassed && geometryReadbackExecutionPassed && !!coldFaultPreparation",runner)
        self.assertIn("BINDLESS_RHI_GEOMETRY_READBACK_COMMAND5_RETIRE_OK",runner)

    def test_composed_execution5_requires_exact_map_result_and_real_same_root_completion(self):
        storage=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h")
        self.assertIn("readbackJoinVersion(admission->originalRequest().contractVersion)",storage)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        mapping=native.split("WebGpuCanonicalReadbackJoins::validateRetainedMapping",1)[1].split("WebGpuCanonicalReadbackJoins::overlapsCpuOwner",1)[0]
        for token in ["budget_.contractVersion!=3u","seal->hasQuotaBoundSharedOwner(seal)",
                      "request->hasQuotaBoundSharedOwner(request)","Root::Status::Pending","validateImmutableProgramBindings()",
                      "validateFrozenEntries(&active,false)"]:
            self.assertIn(token,mapping)
        fixture=self.read_source("tests/fixtures/original_vertex_geometry_execution_fixture.h")
        for token in ["executeComposed(std::move(command),device,resultQuota,mode)","Storage::prepareOriginalTyped",
                      "Scopes::holdScopes()", "root->commitOriginalPendingOwnership()", "native->buffer=nullptr",
                      "results->retainedOwnerCount()-admission->retainedOwnerCount()-1u",
                      "RHI-WEBGPU-TYPED-CALLBACK-OWNER-CAPACITY",
                      "root->status()==Root::Status::Committed", "output[y*256u+x*4u+channel]==expected[channel]",
                      "retainedResults.expired()", "retainedResults.reset();snapshot.reset()",
                      "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_RETIRE_OK"]:
            self.assertIn(token,fixture)
        runner=(self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")
        self.assertIn("geometryReadbackExecutionPassed && progress.includes",runner)
        self.assertIn("geometryReadbackExecutionPassed && !!coldFaultPreparation",runner)
        self.assertIn("numericalRenderedReadback:geometryReadbackExecutionPassed",runner)

    def test_composed_execution5_lifecycle_never_promotes_cancellation_or_loss_to_success(self):
        fixture=self.read_source("tests/fixtures/original_vertex_geometry_execution_fixture.h")
        for token in ["LifecycleCase", "RecordingCancel", "SubmissionLoss", "MapLoss",
                      "RHI-WEBGPU-CANONICAL-CALLBACK-CANCEL-CALLBACKS-PENDING",
                      "Root::Status::Abandoned", "Root::Status::Invalidated",
                      "WebGpuNativeMapExecutionProbeFixture::Probe::PendingLoss",
                      "WebGpuNativeMapExecutionProbeFixture::retirePendingAfterLoss()",
                      "custody->physicalLossDelivered()", "!custody->pendingOriginalRecords()",
                      "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_LIFECYCLE_OK",
                      "BINDLESS_RHI_GEOMETRY_READBACK_EXECUTION5_LIFECYCLE_RETIRE_OK"]:
            self.assertIn(token,fixture)
        runner=(self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")
        self.assertIn("renderedValidationSucceeded:false",runner)
        self.assertIn("geometry-lifecycle",runner)
        smoke=(self.ROOT / "tests/contract/bindless_webgpu_rhi_smoke.py").read_text(encoding="utf-8-sig")
        self.assertIn("--stream-geometry-lifecycle-scenario",smoke)
        for case in ["recording-cancel","submission-loss","map-loss"]:
            self.assertIn(case,smoke)

    def test_composed_invocation_uncertainty_uses_authentic_probes_and_keeps_public_closed(self):
        fixture=self.read_source("tests/fixtures/original_vertex_geometry_execution_fixture.h")
        for token in ["instrumentRecordingException(nativeDevice)", "Probe::ThrowAfter",
                      "RHI-WEBGPU-TYPED-RECORDING-INVOCATION-UNCERTAIN",
                      "RHI-WEBGPU-CANONICAL-SUBMISSION-INVOCATION-UNCERTAIN",
                      "RHI-WEBGPU-NATIVE-MAP-INVOCATION-UNCERTAIN",
                      "Issuers::recordOriginal(issuers,execution)==id",
                      "Issuers::submitOriginal(issuers,execution)==id",
                      "scopes-released-still-retained", "custody->physicalLossDelivered()",
                      "RHI-WEBGPU-TYPED-RECORDING-SCOPE-PUSH-UNCERTAIN",
                      "RHI-WEBGPU-CANONICAL-SUBMISSION-SCOPE-PUSH-UNCERTAIN",
                      "no-guessed-pop", "loss-only-retirement"]:
            self.assertIn(token,fixture)
        runner=(self.ROOT / "tests/contract/original_depth_table_expansion_preflight_smoke.py").read_text(encoding="utf-8-sig")
        cmake=(self.ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")
        for case in ["recording-uncertain","submission-uncertain","map-uncertain",
                     "recording-scope-uncertain","submission-scope-uncertain"]:
            self.assertIn("--native-"+case,runner)
            self.assertIn(case,cmake)
        for token in ["no-retry", "actual-device-loss", "result.nativeCalls.getMappedRange===0",
                      "unpublished completion0 result-quota-zero"]:
            self.assertIn(token,runner)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.execution=rhi::BindlessExecution::DiagnosticFailure",device)
        self.assertIn("caps_.executablePayload=false",device)

    def test_composed_registration_uncertainty_retains_original_graph_without_fake_receipts(self):
        fixture=self.read_source("tests/fixtures/original_vertex_geometry_execution_fixture.h")
        for token in ["Probe::instrumentLoss(nativeDevice,queue,queueRegistration)",
                      "Probe::Probe::PartialScope", "authentic-listener-then-throw",
                      "scopes-released-no-queue-proof-retained",
                      "authentic-scope-queue-promises-held-through-loss",
                      "RHI-WEBGPU-CANONICAL-SUBMISSION-REGISTRATION-FAILED"]:
            self.assertIn(token,fixture)
        branch=fixture.split("if(mode==LifecycleCase::QueueRegistrationUncertain",1)[1].split("bool submitInvoked",1)[0]
        for forbidden in ["wgpuQueueSubmit", "completeMap(", "signalFromHost", "setLimitBytes", "prepareOriginal"]:
            self.assertNotIn(forbidden,branch)
        runner=(self.ROOT / "tests/contract/original_depth_table_expansion_preflight_smoke.py").read_text(encoding="utf-8-sig")
        cmake=(self.ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")
        for case in ["queue-registration-uncertain","scope-registration-uncertain"]:
            self.assertIn("--native-"+case,runner)
            self.assertIn(case,cmake)
        self.assertIn("foreign-callback-drain",runner)

    def test_geometry_dependency_freeze4_consumes_only_original_closure6(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_dependency_projection")
        self.assertIn("closureProfileMatches",contract)
        self.assertIn("freezeVersion==4u ? closureVersion==6u",contract)
        self.assertIn("request.contractVersion!=4u",contract)
        decision=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h")
        self.assertIn("C::closureProfileMatches(request.contractVersion,closure->closureContractVersion())",decision)
        self.assertIn("P::inspect(stream,graph,request.projection)",decision)
        self.assertIn("P::validateFrozenBinding",decision)
        self.assertIn("core::CpuMemoryQuotaScope",decision)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_typed_dependency_decision_fixture.h").read_text(encoding="utf-8-sig"))
        self.assertIn("C::closureProfileMatches(4u,6u)",fixture)
        self.assertIn("!C::closureProfileMatches(4u,5u)",fixture)
        geometry=CppSource((self.ROOT / "tests/fixtures/original_command_pipeline_binding_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["verifyGeometryDependencyFreeze", "BINDLESS_RHI_GEOMETRY_DEPENDENCY_FREEZE_OK", "K::BufferMemoryBarrier",
                      "prepareOriginalGeometryContentLossBound", "Freeze::prepareOriginal", "ENDPOINT-CAPACITY",
                      "indexedGeometryFreezeOwnerDemand", "previousGraphOnlyOwnerCapacity", "beforeFreezeAllocations"]:
            self.assertIn(token,geometry)
        runner=(self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")
        self.assertIn("originalGeometryDependencyFreezePassed",runner)

    def test_content_cells_creation_keeps_original_domain_and_execution_closed(self):
        control=self.read_source("src/rhi/frontend/resource_texture_content_control.h")
        self.assertIn("core::VersionedMutationArray<Value>",control)
        self.assertIn("cells_->matches(observation.stamp)",control)
        self.assertNotIn("MutationJournal journal_",control)
        factory=self.read_source("src/rhi/frontend/device_device.inl")
        self.assertIn("OriginalTextureContentCreationContract::isCellCreation(budget.contractVersion)",factory)
        self.assertIn("budget.maxContentCells",factory)
        self.assertIn("result->originalContent_=std::move(content)",factory)
        transaction=self.read_source("src/rhi/frontend/command_state.h")
        self.assertIn("RHI-CANONICAL-CONTENT-PLAN-CELL-PROFILE-UNIMPLEMENTED",transaction)
        runner=(self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")
        self.assertIn("canonicalContentCreationPassed=canonicalContentCellsCreationPassed &&",runner)
        self.assertIn("BINDLESS_RHI_CONTENT_CELLS_LOSS_OK",runner)
        policy=self.read_source("src/rhi/frontend/resource_texture_content_access.h")
        self.assertIn("OriginalAttachmentExclusive",policy)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        self.assertIn("OriginalTextureContentCreationContract::isCellCreation(decision.contractVersion)",native)
        self.assertIn("? create(desc, false, true) : create(desc, true)",native)

    def test_content_plan2_uses_frozen_original_pass_and_same_journal(self):
        transaction=self.read_source("src/rhi/frontend/command_state.h")
        for token in ["inspectRangeContentPlan", "visitOriginalContentCommand", "descriptorAt(begin)",
                      "Origin::ShaderConsumption", "RangeContentControl", "RangeContentCell",
                      "maxProjectionVisits", "cells_->prepare(journal_)", "proposeCell(",
                      "RHI-ORIGINAL-CONTENT-PLAN-COMMITTED", "completeCell("]:
            self.assertIn(token,transaction)
        self.assertIn("RHI-ORIGINAL-TEXTURE-TRANSACTION-NATIVE-COMPLETION-REQUIRED",transaction)
        self.assertEqual(transaction.count("core::MutationJournal journal_"),1)
        fixture=CppSource((self.ROOT / "tests/fixtures/original_texture_content_cells_plan_fixture.h").read_text(encoding="utf-8-sig"))
        for token in ["preflightOriginalContentCells", "materializeOriginalPrototypePlan", "abandonOriginalPendingOwnership",
                      "verifyHotNoAllocation", "LOAD-UNINITIALIZED", "BINDLESS_RHI_CONTENT_PLAN2_OK"]:
            self.assertIn(token,fixture)
        runner=(self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")
        self.assertIn('s.startsWith("BINDLESS_RHI_CONTENT_PLAN2_OK")',runner)

    def test_content_readback3_is_original_range_read_only_and_versioned(self):
        transaction=self.read_source("src/rhi/frontend/command_state.h")
        for token in ["A::ReadbackRead", "originalTextureOwner()", "p.range!=(*readback)->originalInput()->scalarFields().range()",
                      "RHI-ORIGINAL-CONTENT-PLAN-READBACK-STATE", "RHI-ORIGINAL-CONTENT-PLAN-READBACK-PROFILE",
                      "RHI-ORIGINAL-CONTENT-PLAN-READBACK-CAPACITY", "b.maxReadbackCommands", "b.contractVersion"]:
            self.assertIn(token,transaction)
        self.assertEqual(transaction.count("core::MutationJournal journal_"),1)
        sequence=self.read_source("src/rhi/frontend/resource_texture_validation.h")
        for token in ["ReadbackRead", "RHI-ORIGINAL-CONTENT-SEQUENCE-READBACK-PROFILE",
                      "RHI-ORIGINAL-CONTENT-SEQUENCE-READBACK-PASS", "budget.contractVersion!=3u"]:
            self.assertIn(token,sequence)
        lowering=self.read_source("src/rhi/semantics/command_state_lowering.h")
        for backend in ["NULL-VALIDATOR-ONLY", "VULKAN-JOIN-UNAVAILABLE", "METAL-JOIN-UNAVAILABLE", "NATIVE-CLOSURE-UNAVAILABLE"]:
            self.assertIn("RHI-ORIGINAL-CONTENT-READBACK-PLAN-"+backend,lowering)
        for token in ["wgpu", "makeShared", "makeUnique", "BoundedSequence"]:
            self.assertNotIn(token,lowering)

    def test_content_readback3_cannot_enter_legacy_native_lifecycle(self):
        recording=self.read_source("src/rhi/frontend/command_recording.h")
        self.assertIn("RHI-ORIGINAL-NATIVE-LIFECYCLE-CONTENT-READBACK-PROFILE",recording)
        transaction=self.read_source("src/rhi/frontend/command_state.h")
        self.assertIn("RHI-ORIGINAL-CONTENT-READBACK-NATIVE-COMPLETION-UNAVAILABLE",transaction)
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        self.assertEqual(closure.count("RHI-WEBGPU-CANONICAL-CLOSURE-CONTENT-READBACK-PROFILE"),2)
        fixture=self.read_source("tests/fixtures/original_texture_content_cells_plan_fixture.h")
        for token in ["prepareRetainedOriginal", "correspondence", "preflightOriginalContentCells",
                      "inspectOriginalPreparation", "abandonOriginalPendingOwnership",
                      "verifyHotNoAllocation", "READBACK-PROFILE", "READ-UNINITIALIZED",
                      "BINDLESS_RHI_CONTENT_READBACK_PLAN3_OK"]:
            self.assertIn(token,fixture)
        runner=(self.ROOT / "tests/fixtures/bindless_webgpu_rhi_runner.html").read_text(encoding="utf-8-sig")
        self.assertIn("canonicalContentReadbackPlan3Passed",runner)

    def test_execution2_record_and_submit_consume_exact_issuer_profile(self):
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for method, call in [("recordOriginal", "WebGpuOrdinaryRecording::recordTyped"),
                             ("submitOriginal", "WebGpuOrdinarySubmission::submitTyped")]:
            body=native.split("const char* WebGpuOriginalTypedIssuerPreparation::"+method,1)[1].split("\n}",1)[0]
            self.assertIn("C::validateProfile(request,self->request_.contractVersion)",body)
            self.assertLess(body.index("C::validateProfile"),body.index(call))
            self.assertIn("lowering(rhi::Backend::WebGPU,request.contractVersion,request.readbackCompletion.contractVersion,request.generalSubmission.contractVersion)",body)
        retained=native.split("const char* WebGpuNativeCommandPreparation::validateTypedRetained",1)[1].split("validateTypedOriginal",1)[0]
        self.assertLess(retained.index("validateTypedAdmissionRetained"),retained.index("inspectShellIdentity"))
        self.assertIn("validateProfile",retained)
        emitter=native.split("void WebGpuOrdinaryRecording::emitTyped",1)[1].split("const char* WebGpuOrdinaryRecording::recordTyped",1)[0]
        self.assertIn("case K::BindComputeDescriptorTable",emitter)
        self.assertIn("case K::BindRenderDescriptorTable",emitter)
        self.assertIn("originalPhysicalGroup(record,group)",emitter)

    def test_cell_completion_keeps_original_pending_journal_and_private_authority(self):
        transaction=self.read_source("src/rhi/frontend/command_state.h")
        commit=transaction.split("const char* commitOriginalNativeOwnership(",1)[1].split("const char* finalizeOriginalOwnership",1)[0]
        self.assertLess(commit.index("validate()"),commit.index("completeCell("))
        self.assertIn("invalidateOriginalOwnership()",commit)
        self.assertIn("entry.control->cells_->observePending(journal_)",commit)
        self.assertIn("entry.committed=Content::Cells::Observation",commit)
        cpu=transaction.split("const char* commitOriginalOwnership()",1)[1].split("const char* commitOriginalNativeOwnership",1)[0]
        self.assertIn("budget_.contractVersion==3u",cpu)
        self.assertIn("RHI-ORIGINAL-TEXTURE-TRANSACTION-NATIVE-COMPLETION-REQUIRED",cpu)
        control=self.read_source("src/rhi/frontend/resource_texture_content_control.h")
        completion=control.split("const char* completeCell",1)[1].split("const OriginalTextureContentCreationBudget",1)[0]
        self.assertIn("cells_->observePending(journal)",completion)
        self.assertIn("!(*current==expected)",completion)
        self.assertIn("completed.state=State::Completed",completion)
        self.assertIn("completed,&journal",completion)
        self.assertNotIn("tryAcquire",completion)
        self.assertEqual(transaction.count("core::MutationJournal journal_"),1)
        public=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.executablePayload=false",public)

    def test_content_cell_sequence_only_resolves_bounded_values(self):
        source=self.read_validation_section("src/rhi/frontend/resource_texture_validation.h","original_texture_content_creation")
        for token in ["class OriginalTextureContentCellSequence", "geometry.containsCell", "maxCommandVisits", "READ-ACTIVE-ATTACHMENT", "LOAD-UNINITIALIZED", "UNCLOSED-ATTACHMENT"]:
            self.assertIn(token,source)
        for forbidden in ["makeUnique", "makeShared", "pmr::Vector", "queueSubmit", "MutationJournal journal_"]:
            self.assertNotIn(forbidden,source)
        lowering=self.read_source("src/rhi/semantics/command_state_lowering.h")
        self.assertIn("textureContentSequenceLowering",lowering)
        self.assertIn("RHI-ORIGINAL-CONTENT-SEQUENCE-NATIVE-CLOSURE-UNAVAILABLE",lowering)

    def test_retained_readback_owner_profile_has_explicit_lowering_and_no_native_authority(self):
        source=self.read_source("src/rhi/frontend/transfer_readback_input.h", "frontend_transfer_readback_source_lease_h")
        retained=source.split("prepareRetainedOriginal",1)[1].split("prepareColdOriginal",1)[0]
        for token in ["OriginalCommandTextureLease::validateOriginal(owner)", "sameOwner(quota,owner->originalMetadataQuota())", "validateSourceOriginal(request,*owner->originalObject())", "retainedOriginalOwnerCount", "RHI-READBACK-SOURCE-LEASE-OWNER-PROFILE"]:
            self.assertIn(token,retained)
        for forbidden in ["adoptOriginal", "createTexture", "observeOriginalContent", "signalFromHost", "wgpu", "new GpuTexture"]:
            self.assertNotIn(forbidden,retained)
        lowering=self.read_source("src/rhi/semantics/transfer_lowering.h")
        for token in ["TextureReadbackSourceLeaseLowering", "Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU", "nativeExecutionImplemented=false", "RHI-READBACK-SOURCE-LEASE-COMPLETION-UNPROVEN"]:
            self.assertIn(token,lowering)

    def test_retained_readback_owner_profile_cannot_enter_older_native_consumers(self):
        sources=self.read_validation_section("src/rhi/frontend/command_sources.h", "original_command_readback_texture_source")
        for token in ["budget.contractVersion!=lease->contractVersion()", "RHI-ORIGINAL-READBACK-TEXTURE-LEASE-PROFILE", "requiredOwnerCount(lease)"]:
            self.assertIn(token,sources)
        inventory=self.read_validation_section("src/rhi/frontend/command_sources.h", "original_command_resource_sources")
        self.assertIn("value->completeOwnerCount()",inventory)
        binding=self.read_source("src/rhi/backends/webgpu/webgpu_readback_preparation.h", "backends_webgpu_webgpu_original_texture_readback_source_h")
        self.assertIn("lease->contractVersion()!=1u",binding)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        capture=native.split("WebGpuCanonicalReadbackJoins::capture",1)[1].split("WebGpuCanonicalReadbackJoins::inspect",1)[0]
        self.assertIn("input.source->contractVersion()!=1u",capture)
        self.assertLess(capture.index("RHI-WEBGPU-READBACK-JOIN-SOURCE-PROFILE"),capture.index("observeOriginalWebGpuResource"))

    def test_typed_readback_state_requires_explicit_source2_correspondence_and_root_profile(self):
        model=self.read_validation_section("src/rhi/frontend/command_model.h", "original_command_state_admission")
        for token in ["retainedReadbackStateVersion", "retainedReadbackStateOptIn",
                      "RHI-ORIGINAL-STATE-READBACK-PROFILE", "RHI-ORIGINAL-STATE-READBACK-VERSION",
                      "RHI-ORIGINAL-STATE-READBACK-OPT-IN"]:
            self.assertIn(token,model)
        state=self.read_validation_section("src/rhi/frontend/command_state.h", "original_command_resource_states")
        for token in ["typedReadbackLowering", "decision.retainedReadbackStateAvailable",
                      "sources->retainedReadbackCorrespondence().enabled()", "(*readback)->contractVersion()!=2u",
                      "RHI-ORIGINAL-TYPED-STATE-READBACK-MISSING", "RHI-ORIGINAL-TYPED-STATE-SOURCE-PROFILE"]:
            self.assertIn(token,state)
        transaction=self.read_source("src/rhi/frontend/command_state.h")
        for token in ["graph->budget_.retainedReadbackStateVersion!=budget.retainedReadbackStateVersion",
                      "graph->budget_.retainedReadbackStateOptIn!=budget.retainedReadbackStateOptIn",
                      "RHI-ORIGINAL-TRANSACTION-READBACK-PROFILE"]:
            self.assertIn(token,transaction)

    def test_typed_readback_state_lowering_is_cpu_only_on_all_four_backends(self):
        source=self.read_source("src/rhi/semantics/command_state_lowering.h")
        lowering=source.split("static constexpr TypedDecision typedReadbackLowering",1)[1].split("struct TransactionDecision",1)[0]
        for token in ["version!=1u", "Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU",
                      "READBACK-STATE-NULL-VALIDATOR-ONLY", "READBACK-STATE-VULKAN-JOIN-MISSING",
                      "READBACK-STATE-METAL-JOIN-MISSING", "false,true,true,false,nullptr,true"]:
            self.assertIn(token,lowering)
        for forbidden in ["wgpu", "makeShared", "makeUnique", "GpuDevice", "MutationJournal", "signalFromHost"]:
            self.assertNotIn(forbidden,lowering)

    def test_mixed_readback_join_authenticates_program_state_root_and_keeps_execution_closed(self):
        contract=self.read_validation_section("src/rhi/frontend/command_transfer_validation.h", "original_command_readback_join")
        for token in ["validateProgramIntent", "b.contractVersion!=2u", "b.stream.contractVersion!=(b.contractVersion==3u ? 4u : 3u)",
                      "Root::validateOriginal(root)", "Status::Prepared", "programProfileMatches(b,root->originalGraph()->stateContractVersion(),root->originalGraph()->originalInputs()->originalBudget().contractVersion,root->originalContentPlanContractVersion())",
                      "program->compareOriginalStates(root->originalGraph())", "program->compareOriginalInputs(inputs)",
                      "OriginalCommandReadbackResultContract::inspectProgram", "counts().matches(projected.counts)"]:
            self.assertIn(token,contract)
        for forbidden in ["wgpu", "makeUnique", "makeShared", "MutationJournal", "commitOriginal"]:
            self.assertNotIn(forbidden,contract)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        joins=native.split("const char* WebGpuCanonicalReadbackJoins::capture",1)[1].split("bool WebGpuCanonicalUploadJoins::overlapsCpuOwner",1)[0]
        for token in ["version==1u && input.source->contractVersion()!=1u", "(version==2u || version==3u) && input.source->contractVersion()!=2u",
                      "originalTextureOwner()", "ordinaryAccessDiagnostic()", "originalAttachmentExclusive()",
                      "inspectProgram(root,program,budget)", "PROGRAM-JOIN-EXECUTION-PROFILE", "budget_.contractVersion!=1u"]:
            self.assertIn(token,joins)
        cold=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_cold_admission.h")
        self.assertIn("readbacks->joinContractVersion()!=1u",cold)
        self.assertIn("RHI-WEBGPU-CANONICAL-COLD-READBACK-JOIN-PROFILE",cold)

    def test_retained_readback_correspondence_uses_same_original_graph_and_charges_hidden_metadata(self):
        sources=self.read_validation_section("src/rhi/frontend/command_sources.h", "original_command_resource_sources")
        for token in ["OriginalRetainedReadbackCorrespondence", "retainedReadback_.enabled()", "nestedReadbackOwners(*value)",
                      "value->originalSource()->originalLease()->originalTextureOwner()", "value->contractVersion()!=2u",
                      "RHI-ORIGINAL-RETAINED-READBACK-SOURCE-CAPACITY", "RHI-ORIGINAL-RETAINED-READBACK-OWNER-CAPACITY",
                      "source.originalSource()->completeOwnerCount()", "OriginalCommandTextureLease::retainedOwnerCount"]:
            self.assertIn(token,sources)
        inputs=self.read_validation_section("src/rhi/frontend/command_usage.h", "original_command_inputs")
        for token in ["inputBudget.retainedReadback.matches", "retainedReadbackCorrespondence()",
                      "RHI-ORIGINAL-INPUTS-RETAINED-READBACK-PROFILE", "inputBudget.retainedReadback.enabled()",
                      "(*source)->originalSource()->originalLease()->originalTextureOwner()"]:
            self.assertIn(token,inputs)

    def test_mixed_readback_result_uses_authenticated_program_and_shared_result_projection(self):
        source=self.read_validation_section("src/rhi/frontend/command_recording.h", "original_command_readback_result")
        mixed=source.split("static Inspection inspectProgram",1)[1].split("private:",1)[0]
        for token in ["validateProgramIntent(b)", "OriginalCommandStreamProgram::validateOriginal(program)",
                      "OriginalCommandInputs::validateOriginal(inputs)", "program->compareOriginalInputs(inputs)",
                      "program->operations()", "inspectDeclarations(inputs,b,pending,program->counts())"]:
            self.assertIn(token,mixed)
        for token in ["b.contractVersion!=2u", "RHI-ORIGINAL-MIXED-READBACK-RESULT-COMMAND",
                      "SharedIdentityProjection::inspect", "SharedAllocationProjection::inspect", "CpuDomain::Result"]:
            self.assertIn(token,source)
        for forbidden in ["wgpu", "makeShared", "makeUnique", "commitOriginal", "signalFromHost"]:
            self.assertNotIn(forbidden,mixed)
        fixture=self.read_source("tests/fixtures/texture_readback_retained_source_fixture.h")
        for token in ["ORIGINAL_MIXED_READBACK_RESULT2_OK", "RHI-ORIGINAL-STREAM-OWNER", "RHI-ORIGINAL-STREAM-INPUTS",
                      "verifyHotNoAllocation", "maxUniqueResultBytes", "programQuota->liveBytes()==0u"]:
            self.assertIn(token,fixture)

    def test_mixed_readback_result_has_four_backend_cpu_only_lowering(self):
        source=self.read_source("src/rhi/semantics/command_lowering.h")
        lowering=source.split("class OriginalCommandMixedReadbackResultLowering",1)[1].split("// endregion original_command_mixed_readback_result",1)[0]
        for token in ["Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU", "version!=2u",
                      "recordingAvailable=false", "mapAvailable=false", "publicationAvailable=false"]:
            self.assertIn(token,lowering)
        for forbidden in ["wgpu", "GpuDevice", "makeUnique", "makeShared", "MutationJournal"]:
            self.assertNotIn(forbidden,lowering)

    def test_retained_readback_correspondence_lowering_cannot_authorize_native_execution(self):
        source=self.read_source("src/rhi/semantics/command_lowering.h")
        lowering=source.split("class OriginalRetainedReadbackCorrespondenceLowering",1)[1].split("class OriginalCommandTypedSourceLowering",1)[0]
        for token in ["Backend::Null", "Backend::Vulkan", "Backend::Metal", "Backend::WebGPU", "version!=1u",
                      "nativeExecutionImplemented=false", "RHI-ORIGINAL-RETAINED-READBACK-COMPLETION-UNPROVEN"]:
            self.assertIn(token,lowering)
        for forbidden in ["wgpu", "makeShared", "makeUnique", "CpuMemoryQuota", "GpuDevice", "signalFromHost"]:
            self.assertNotIn(forbidden,lowering)

    def test_mixed_native_closure4_requires_same_authentic_join_and_complete_endpoints(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["prepareOriginalTableReadbackLossBound", "budget.contractVersion!=4u",
                      "WebGpuCanonicalReadbackJoins::validateOriginal(readbacks)",
                      "sameOwner(readbacks->originalRoot(),root)", "sameOwner(readbacks->originalProgram(),stream)",
                      "inventory.originalLeaseIndex(source)", "originalDestinationLease()", "sameOwnersAndLayout",
                      "coveredReadbackSource", "readbacks->copies_.values()", "SharedIdentityProjection::inspect",
                      "READBACK-COVERAGE", "READBACK-CAPACITY", "CONTENT-READBACK-PROFILE"]:
            self.assertIn(token,source)
        for forbidden in ["wgpuCommandEncoderCopy", "wgpuQueueSubmit", "signalFromHost", "std::make_shared"]:
            self.assertNotIn(forbidden,source)
        self.assertEqual(source.count("validateTypedBinding(*root,entry,general)"),2)
        self.assertIn("validateTypedBinding(*root_,entry,general)",source)
        fixture=self.read_source("tests/fixtures/original_depth_table_content_plan_fixture.h")
        for token in ["ORIGINAL_TABLE_READBACK_CUSTODY4_OK", "custody.first->validate()", "custody.second->validate()",
                      "RHI-WEBGPU-TYPED-PAYLOAD-DEPENDENCY-PROFILE", "pressure.verify(closureBytes,make)",
                      "pressure.verify(freezeBytes,freeze)", "forgedJoin", "verifyHotNoAllocation"]:
            self.assertIn(token,fixture)

    def test_mixed_closure4_and_freeze3_leave_recording_map_completion_public_execution_closed(self):
        lowering=self.read_source("src/rhi/semantics/command_lowering.h")
        for backend in ["NULL-VALIDATOR-ONLY", "VULKAN-JOIN-MISSING", "METAL-JOIN-MISSING", "WEBGPU-RECORDING-UNPROVEN"]:
            self.assertIn("RHI-ORIGINAL-NATIVE-CLOSURE-TABLE-READBACK-"+backend,lowering)
        decision=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h")
        self.assertIn("C::closureProfileMatches(request.contractVersion,closure->closureContractVersion())",decision)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        self.assertIn("RHI-WEBGPU-READBACK-PROGRAM-JOIN-EXECUTION-PROFILE",native)
        payload=self.read_validation_section("src/rhi/frontend/command_shader_validation.h", "original_typed_native_payload")
        self.assertIn("freezeVersion==2u && closureVersion==3u",payload)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.executablePayload=false",device)

    def test_selected_execution_preserves_complete_legacy_loss_coverage_in_publication_smoke(self):
        source=self.read_source("tests/fixtures/original_depth_table_expansion_pipeline_fixture.h")
        self.assertEqual(source.count("nativePublicationRequested && !nativeExecutionRequested"),2)
        for token in ["verifyActualLoss<Desc>(true)", "false,true,true,true)",
                      "OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested,true,true,false,true)"]:
            self.assertIn(token,source)
        runner=(self.ROOT / "tests/contract/original_depth_table_expansion_preflight_smoke.py").read_text()
        for token in ["ORIGINAL_TABLE_CONTENT_PLAN2_LOSS_OK", "ORIGINAL_TABLE_NATIVE_CUSTODY_LOSS_OK",
                      "ORIGINAL_TABLE_NATIVE_COLD2_LOSS_OK", "ORIGINAL_TABLE_READBACK_CUSTODY4_OK", "capture_progress=True"]:
            self.assertIn(token,runner)

    def test_mixed_payload3_borrows_exact_join2_native_inputs_without_execution_authority(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h", "original_typed_native_payload")
        for token in ["freezeVersion==3u && closureVersion==4u", "maxTransferPasses", "maxReadbackCommands",
                      "maxReadbackCopies", "READBACK-SCHEMA", "READBACK-REQUIREMENTS",
                      "OriginalTypedNativeColdContract::validateMixedWork(work,{transfers,readbacks,copies},request.maxWork,OriginalTypedNativeColdContract::geometryProfile(request.contractVersion))",
                      "OriginalTypedNativeColdContract::inspectMixedWorkTotals(work,{transfers,readbacks,copies},commands,calls)"]:
            self.assertIn(token,contract)
        cold=self.read_validation_section("src/rhi/frontend/command_shader_validation.h", "original_typed_native_cold")
        for token in ["inspectMixedWorkTotals", "COUNT-OVERFLOW", "commands=calls=0u",
                      "validateWork(shader,maximum,geometry)", "validateMixedWork(required.work,required.transfers,request.maxWork,geometryProfile(request.contractVersion))"]:
            self.assertIn(token,cold)
        source=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_native_payload.h")
        for token in ["case K::BeginTransferPass", "case K::EndTransferPass", "case K::TextureReadback",
                      "joins->joinContractVersion()!=(request.contractVersion==5u ? 3u : 2u)", "sameOwner(joins->originalProgram(),closure.originalStream())",
                      "sameOwner(joins->originalRoot(),closure.originalRoot())", "sameOwner(original,join.source)",
                      "joins->copies_.values()", "TextureReadbackCopyLowering::validateOriginal",
                      "inspectTextureBufferLayout", "record.destination", "readbackCopyCount", "READBACK-COVERAGE"]:
            self.assertIn(token,source)
        for token in ["OriginalTypedNativeColdContract::geometryProfile(request.contractVersion)",
                      "OriginalTypedNativeColdContract::readbackProfile(request.contractVersion)",
                      "request.contractVersion!=2u && request.contractVersion!=3u && request.contractVersion!=5u"]:
            self.assertIn(token,source)
        for forbidden in ["wgpuQueueSubmit", "wgpuCommandEncoderCopy", "WebGpuRecordedCommandProof", "signalFromHost"]:
            self.assertNotIn(forbidden,source)
        transfer=self.read_source("src/rhi/backends/webgpu/webgpu_transfer_copy_validation.h")
        ordinary=transfer.split("const rhi::TextureToBufferCopyDesc& a) noexcept",1)[1].split("// Pure layout inspection",1)[0]
        self.assertIn("ordinaryContentAccessDiagnostic()",ordinary)
        self.assertIn("inspectTextureBufferLayout(s,d,a)",ordinary)
        fixture=self.read_source("tests/fixtures/original_depth_table_content_plan_fixture.h")
        for token in ["ORIGINAL_TABLE_READBACK_PAYLOAD3_OK", "custody.payload->validate()", "forgedPayload",
                      "CpuMemoryQuotaLease exhausted", "READBACK-BUDGET", "rejected.required.work.commands"]:
            self.assertIn(token,fixture)

    def test_mixed_cold3_requires_exact_lifecycle2_and_keeps_live_consumers_closed(self):
        lifecycle=self.read_validation_section("src/rhi/frontend/command_recording.h", "original_command_native_lifecycle")
        for token in ["validateContentProfile", "budget.contractVersion!=2u && contentVersion==3u",
                      "budget.contractVersion==2u && contentVersion!=3u", "LIFECYCLE-MIXED-CONTENT-PROFILE"]:
            self.assertIn(token,lifecycle)
        reservation=self.read_validation_section("src/rhi/frontend/command_recording.h", "original_command_lifecycle_reservation")
        self.assertIn("lowering(backend,seal_->originalBudget().contractVersion)",reservation)
        cold=self.read_validation_section("src/rhi/frontend/command_shader_validation.h", "original_typed_native_cold")
        for token in ["!readbackProfile(request.contractVersion) && !request.maxTransfers.empty()",
                      "lifecycleVersion==(generalProfile(request.contractVersion) ? generalLifecycleVersion(request.contractVersion) : readbackProfile(request.contractVersion) ? 2u : 1u)",
                      "validateMixedWork(required.work,required.transfers,request.maxWork,geometryProfile(request.contractVersion))"]:
            self.assertIn(token,cold)
        admission=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_admission.h")
        for token in ["validateLifecycleProfile(request,seal->originalBudget().contractVersion)",
                      "payload->transferPassCount()", "seal->compareOriginalRoot(root)", "validateSourceAuthority"]:
            self.assertIn(token,admission)
        callback=self.read_validation_section("src/rhi/frontend/command_shader_validation.h", "original_typed_callback_custody")
        self.assertIn("request.contractVersion<1u || request.contractVersion>8u",callback)
        self.assertIn("cold.contractVersion!=request.contractVersion",callback)
        self.assertIn("RHI-TYPED-CALLBACK-CUSTODY-TRANSFER-PROFILE",callback)
        execution=self.read_validation_section("src/rhi/frontend/command_shader_validation.h", "original_typed_native_execution")
        self.assertIn("request.contractVersion<1u || request.contractVersion>8u",execution)
        self.assertIn("RHI-TYPED-NATIVE-EXECUTION-READBACK-COMPLETION-MISSING",
                      self.read_source("src/rhi/semantics/command_lowering.h"))
        fixture=self.read_source("tests/fixtures/original_depth_table_content_plan_fixture.h")
        for token in ["ORIGINAL_TABLE_READBACK_COLD3_OK", "pressure.verifySingleUse(commandBytes,rebuild,connect)",
                      "legacyLifecycle.contractVersion=1u", "forgedCold", "retainedCold.expired()",
                      "command->validate()", "!sources->originalReservedCallbackRegistrationCount()"]:
            self.assertIn(token,fixture)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.execution=rhi::BindlessExecution::DiagnosticFailure",device)
        self.assertIn("caps_.executablePayload=false",device)

    def test_selected_mixed_cold3_keeps_full_legacy_quota_pressure_in_publication_smoke(self):
        fixture=self.read_source("tests/fixtures/original_depth_table_content_plan_fixture.h")
        self.assertIn("reserveExecution ? 0u : pressure.measureRetained(admit)",fixture)
        self.assertIn("if(!reserveExecution)pressure.verify(admissionBytes,admit)",fixture)
        self.assertIn("reserveExecution ? 0u : pressure.measureRetained(connect)",fixture)
        self.assertIn("if(!reserveExecution)pressure.verifySingleUse(commandBytes,rebuild,connect)",fixture)
        self.assertIn("ORIGINAL_TABLE_READBACK_COLD3_PROGRESS",fixture)
        pipeline=self.read_source("tests/fixtures/original_depth_table_expansion_pipeline_fixture.h")
        self.assertIn("nativePublicationRequested && !nativeExecutionRequested",pipeline)
        self.assertIn("OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested,true,true,false,true)",pipeline)

    def test_mixed_callback3_issuer3_reuses_original_state_and_native_receipts_without_execution(self):
        fixture=self.read_source("tests/fixtures/original_depth_table_content_plan_fixture.h")
        for token in ["mixedIssuersRequested ? 0u : pressure.measureRetained(admit)",
                      "if(!mixedIssuersRequested)pressure.verifySingleUse(commandBytes,rebuild,connect)",
                      "OriginalTypedCallbackCustodyFixture::verifyMixed(live,mixedRecordingRequested)",
                      "preclaim.required.retainedOwners-1u", "quota-pressure=covered-by-native-execution-smoke"]:
            self.assertIn(token,fixture)
        callbacks=self.read_source("tests/fixtures/original_typed_callback_custody_fixture.h")
        for token in ["ORIGINAL_TABLE_READBACK_ISSUER3_OK", "countEnd(0u)", "measured-1u",
                      "OriginalTypedIssuerPreparationFixture::verifyCold(live,recordReadback)", "state.expired()",
                      "RHI-TYPED-NATIVE-EXECUTION-ISSUER-PROFILE", "RHI-TYPED-NATIVE-EXECUTION-VERSION"]:
            self.assertIn(token,callbacks)
        self.assertIn("live.submission->liveBytes()==submissionBase && live.callback->liveBytes()==callbackBase",callbacks)
        self.assertIn("assert(state.expired());state.reset();",callbacks)
        self.assertIn("live.retained.recordingQuota->liveBytes()==live.recordingBeforeCommand",callbacks)
        self.assertLess(fixture.index("recordingBeforeCommand=retained.recordingQuota->liveBytes()"),fixture.index("}live{retained,command,recordingBeforeCommand"))
        issuer=self.read_source("tests/fixtures/original_typed_issuer_preparation_fixture.h")
        self.assertIn("live.request.contractVersion,true,1u,1u,7u",issuer)
        pipeline=self.read_source("tests/fixtures/original_depth_table_expansion_pipeline_fixture.h")
        self.assertIn("nativeExecutionRequested && !OriginalDepthTableContentPlanFixture::mixedIssuersRequested",pipeline)
        self.assertIn("rhi::OriginalTypedNativeColdCommandContract::additionalOwners + rhi::OriginalTypedCallbackCustodyContract::additionalOwners({3u",fixture)
        runner=self.read_source("tests/contract/original_depth_table_expansion_preflight_smoke.py")
        for token in ["--native-issuers", "ORIGINAL_TABLE_READBACK_ISSUER3_OK Graphics", "physical-registration0"]:
            self.assertIn(token,runner)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        preparation=native[native.index("WebGpuCanonicalCallbackCustody::prepareTypedOriginal("):]
        preparation=preparation[:preparation.index("WebGpuCanonicalCallbackCustody::validateTypedOriginal(")]
        self.assertLess(preparation.index("typedCallbackCustodyClaimed_)"),preparation.index("observers_.pending()>=binding->observers_.capacity()"))
        self.assertLess(preparation.index("inspectTypedOriginal(command,request,results,publication)"),preparation.index("typedCallbackCustodyClaimed_)"))

    def test_mixed_readback_recording_consumes_frozen_join_and_keeps_submission_closed(self):
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        emitter=native.split("void WebGpuOrdinaryRecording::emitTyped(",1)[1].split("WebGpuOrdinaryRecording::recordTyped(",1)[0]
        for token in ["case K::BeginTransferPass", "case K::EndTransferPass", "case K::TextureReadback",
                      "record.readbackCopies[mip]", "copy.commandIndex!=record.original.commandIndex",
                      "WebGpuNativeOriginalTextureReadbackLowering::emitFrozen"]:
            self.assertIn(token,emitter)
        for token in ["wgpuQueueSubmit", "wgpuBufferMapAsync", "makeShared", "makeUnique"]:
            self.assertNotIn(token,emitter)
        frozen=native.split("WebGpuCanonicalReadbackJoins::validateImmutableProgramBindings()",1)[1].split("bool WebGpuCanonicalUploadJoins::overlapsCpuOwner",1)[0]
        for token in ["hasOriginalRecordingSeal()", "Root::Status::Pending", "journalEmpty()",
                      "observeOriginalWebGpuResource", "sameOwnersAndLayout", "Lowering::validateOriginal"]:
            self.assertIn(token,frozen)
        for token in ["wgpuBufferGetSize", "wgpuTextureGetUsage", "wgpuBufferMapAsync"]:
            self.assertNotIn(token,frozen)
        common=self.read_source("src/rhi/backends/webgpu/webgpu_native_original_texture_readback_lowering.h")
        for token in ["mip.physicalOrigin", "mip.physicalExtent", "mip.bufferOffsetBytes", "WGPUTextureAspect_DepthOnly"]:
            self.assertIn(token,common)
        state=self.read_source("src/rhi/frontend/command_state.h")
        self.assertIn("RHI-ORIGINAL-CONTENT-READBACK-NATIVE-COMPLETION-UNAVAILABLE",state)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.executablePayload=false",device)

    def test_typed_mixed_result_storage_consumes_original_map_reservation(self):
        cold = self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_admission.h")
        for token in ["hasReadbackDemand(request)", "OriginalCommandReadbackResultContract::inspectProgram",
                      "OriginalCommandReadbackMapContract::deriveRequirements", "required.capacity.callbackTickets=static_cast"]:
            self.assertIn(token, cold)
        storage = self.read_source("src/rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h")
        for token in ["prepareOriginalTyped", "prepareBound", "TypedAdmission::validateOriginal",
                      "sameOwner(joins->originalRoot(),admission->originalRoot())",
                      "sameOwner(joins->originalProgram(),closure->originalStream())",
                      "validateTypedRequirements", "validateTypedAdmissionRetained"]:
            self.assertIn(token, storage)
        for forbidden in ["wgpuBufferMapAsync", "wgpuQueueSubmit", "std::make_shared", "std::vector"]:
            self.assertNotIn(forbidden, storage)
        contract = self.read_source("src/rhi/frontend/command_shader_validation.h")
        self.assertIn("RHI-TYPED-CALLBACK-CUSTODY-READBACK-MAP-OPT-IN", contract)
        fixture = self.read_source("tests/fixtures/original_depth_table_content_plan_fixture.h")
        self.assertIn("ORIGINAL_TABLE_READBACK_STORAGE2_OK", fixture)
        self.assertIn("sources->originalReservedCallbackTicketCount()==12u", fixture)

    def test_typed_readback_map_custody_uses_original_runtime_receipts(self):
        contract = self.read_source("src/rhi/frontend/command_shader_validation.h")
        for token in ["ReadbackMapIntent", "request.readbackMap.contractVersion!=1u",
                      "request.readbackMap.prototypeOptIn", "map.maps!=cold.readback.resultDeclarations",
                      "cold.readbackMap.totalCallbackTickets!=total", "cold.readbackMap.totalCallbackRegistrations!=total"]:
            self.assertIn(token, contract)
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        shared = native.split("static Role roleAt", 1)[1].split("bool foreignCallbacksDrained()", 1)[0]
        for token in ["visitOriginalMapBindings", "prepareOriginalMapReceipts", "validateOriginalMapReceipts",
                      "map.map.prepareColdRange", "map.map.validateRetainedRange", "matchesPrepared",
                      "snapshot->originalRequest()", "joins.native_.values()[ordinal][1].buffer"]:
            self.assertIn(token, shared)
        for forbidden in ["wgpuBufferMapAsync", "wgpuQueueSubmit", "wgpuBufferGetSize", "makeShared", "makeUnique"]:
            self.assertNotIn(forbidden, shared)
        typed = native.split("WebGpuCanonicalCallbackCustody::prepareTypedOriginal", 1)[1].split(
            "WebGpuCanonicalCallbackCustody::validateTypedOriginal", 1)[0]
        self.assertIn("state->prepareOriginalMapReceipts", typed)
        self.assertLess(typed.index("state->prepareOriginalMapReceipts"), typed.index("observers_.attach"))
        legacy = native.split("WebGpuCanonicalCallbackCustody::prepareOriginal(", 1)[1].split(
            "WebGpuCanonicalCallbackCustody::validate()", 1)[0]
        self.assertIn("state->prepareOriginalMapReceipts", legacy)
        self.assertIn("results->readbackMapRequirements()", native)
        self.assertIn("recordedReadbackCompletionVersion", native)
        self.assertIn("caps_.executablePayload=false", self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp"))

    def test_typed_readback_completion_reuses_authenticated_pending_chain(self):
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        retained = native.split("WebGpuNativeCommandPreparation::validateTypedAdmissionRetained", 1)[1].split(
            "WebGpuNativeCommandPreparation::validateTypedRetained(", 1)[0]
        for token in ["validateRetainedOwnership", "nativeColdCommandClaimed_", "typedCallbackCustodyClaimed_",
                      "validateImmutableBindings", "validateImmutableRecords", "&admission->requirements()",
                      "RHI-WEBGPU-TYPED-COLD-RETAINED-PENDING"]:
            self.assertIn(token, retained)
        self.assertLess(retained.index("validateRetainedOwnership"), retained.index("&admission->requirements()"))
        for token in ["recordedReadbackCompletionVersion", "RHI-TYPED-NATIVE-READBACK-COMPLETION-RECORDED-PROFILE",
                      "validateReadbackProfile", "commitOriginalReadbackSubmission", "state->mapDrainFacts()",
                      "validateRetainedMapReceipts", "validateRetainedBinding"]:
            self.assertIn(token, native)
        self.assertNotIn("RHI-WEBGPU-TYPED-READBACK-STORAGE-MAP-CLOSED", native)
        state = self.read_source("src/rhi/frontend/command_state.h")
        self.assertIn("plan_->contentBudget.contractVersion==3u&&!readbackProven", state)
        self.assertIn("failure||!cell.written", state)

    def test_mixed_readback_loss_reuses_original_issuers_and_native_callback_drain(self):
        fixture = self.read_source("tests/fixtures/original_depth_table_content_plan_fixture.h")
        for token in ["PendingReadbackLoss", "pending.root=retained.transaction.get()",
                      "WebGpuNativeMapExecutionProbeFixture::Probe::PendingLoss",
                      "WebGpuNativeMapExecutionProbeFixture::restore(true)",
                      "WebGpuNativeMapExecutionProbeFixture::retirePendingAfterLoss()",
                      "MapScopes::holdScopes()", "MapScopes::waitHeldScopes()", "MapScopes::restoreScopes()",
                      "MapScopes::releaseScope(0u)", "MapScopes::releaseScope(1u)", "MapScopes::releaseScopes()",
                      "ORIGINAL_TABLE_READBACK_MAP_LOSS1_PARTIAL_DRAIN_OK",
                      "pending.custody->physicalLossDelivered()", "pending.custody->pendingOriginalRecords()==1u",
                      "Root::Status::Invalidated", "!pending.snapshot->publishedBytes()",
                      "pending.results.expired()", "native.cold.reset()", "!pending.resultQuota->liveBytes()",
                      "ORIGINAL_TABLE_READBACK_%s_LOSS1_OK"]:
            self.assertIn(token, fixture)
        terminal = fixture.split("static void verifyReadbackLoss(", 1)[1].split("#endif", 1)[0]
        for forbidden in ["wgpuQueueSubmit", "applyOriginalPrototypePlan", "commitOriginalPendingOwnership",
                          "signalFromHost", "setLimitBytes", "prepareOriginal"]:
            self.assertNotIn(forbidden, terminal)
        main = self.read_source("tests/contract/original_depth_table_expansion_artifact_tests.cpp")
        self.assertIn("verifyTypedMapDrainContract()", main)
        registration = self.read_source("tests/fixtures/original_depth_table_registration_fixture.h")
        self.assertIn("retainForLoss==(!geometry&&OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested)", registration)
        self.assertIn("contentCells&&!OriginalDepthTableContentPlanFixture::mixedReadbackLossRequested", registration)
        publication = self.read_source("tests/fixtures/original_depth_table_publication_fixture.h")
        self.assertIn("RHI-ORIGINAL-TRANSACTION-INVALIDATED", publication)
        self.assertIn("is(retained.transaction->validate(),rootExpected)", publication)
        contracts = self.read_source("tests/fixtures/original_command_readback_result_contract_fixture.h")
        self.assertIn("verifyMapDrain({1u,1u,3u,12u,12u})", contracts)
        runner = (self.ROOT / "tests/contract/original_depth_table_expansion_preflight_smoke.py").read_text()
        for token in ["--native-submitted-loss", "--native-map-loss", "result.nativeCalls.getMappedRange===0",
                      "result.nativeCalls.mapAsync===", "result.nativeCalls.unmap>0",
                      "publication0 completion0", "result-submission-callback-quotas-zero"]:
            self.assertIn(token, runner)
        device = self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.executablePayload=false", device)

    def test_general_native_closure_requires_original_group_and_nonmemory_sampler_custody(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["prepareOriginalGeneralResourcesLossBound", "budget.contractVersion!=8u&&budget.contractVersion!=9u",
                      "device.validateOriginalTypedNativeLossJoin(loss,{1u,true})",
                      "validateGeneralProfile", "Request{3u,true,Request::Profile::GeneralWrite1}",
                      "diagnoseOriginalGeneralClosureBinding", "bindingResource()", "Object o; o.sampler=lease->originalObject()",
                      "validateOriginalWebGpuResource(*entry.object.sampler,sampler)"]:
            self.assertIn(token,source)
        self.assertIn("return object.resource() ? backing().allocationRecord()",source)
        self.assertIn("geometry=budget.contractVersion>=5u&&budget.contractVersion<=7u",source)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        join=native.split("WebGpuNativeDescriptorGroupPayload::diagnoseOriginalGeneralClosureBinding",1)[1].split(
            "WebGpuNativeDescriptorGroupPayload::diagnoseOriginalFiniteGroup",1)[0]
        for token in ["generalContentDiagnostic()", "OriginalCommandDescriptorDependencies::capture(sources,element.sourceIndex)",
                      "sameOwner(canonical,*retained)", "element.registrationGeneration!=e.registrationGeneration",
                      "element.stages!=e.stages", "element.access!=e.access"]:
            self.assertIn(token,join)
        for forbidden in ["wgpuQueueSubmit", "MutationJournal", "std::make_shared", "new ", "setLimitBytes"]:
            self.assertNotIn(forbidden,join)

    def test_general_dependency_freeze_consumes_native_custody_budget_and_keeps_payload_closed(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_dependency_projection")
        self.assertIn("request.contractVersion<1u || request.contractVersion>8u",contract)
        self.assertIn("freezeVersion==6u && closureVersion==8u",contract)
        decision=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_dependency_decision.h")
        for token in ["request.contractVersion==6u", "closure->budget_.maxRetainedOwners",
                      "sources->originalBudget().maxGraphOwners", "request.maxRetainedOwners>ownerCeiling",
                      "C::closureProfileMatches", "closure->nativeLossDiagnostic()"]:
            self.assertIn(token,decision)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["Freeze::Request freezeBudget{6u,true,dependency", "frozenDecision->counts().dependencies==4u",
                      "frozenDecision->compareOriginalClosure(peer)", "closure->validate() && frozenDecision->validate()",
                      "retainedClosure.reset();retained.reset();assert(!recording->liveBytes())"]:
            self.assertIn(token,fixture)
        payload=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_payload")
        self.assertIn("request.contractVersion<1u || request.contractVersion>8u",payload)
        smoke=self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py")
        self.assertIn('result.get("originalGeneralDependencyFreezePassed") is True',smoke)

    def test_graphics_freeze8_isolates_closure10_and_keeps_successors_closed(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_dependency_projection")
        for token in ["request.contractVersion==8u", "request.projection.contractVersion!=6u",
                      "freezeVersion==8u && closureVersion==10u", "!projection.maxAttachmentReferences",
                      "!projection.geometryUsage.drawConsumptionEmpty()", "projection.maxReadbacks",
                      "RHI-ORIGINAL-GRAPHICS-FREEZE-ATTACHMENT-REQUIRED", "RHI-ORIGINAL-GRAPHICS-FREEZE-COMPOSITION"]:
            self.assertIn(token,contract)
        lowering=self.read_source("src/rhi/semantics/command_lowering.h").split("class OriginalTypedDependencyProjectionLowering final",1)[1].split("// endregion",1)[0]
        for backend in ["NULL-VALIDATOR-ONLY", "VULKAN-JOIN-MISSING", "METAL-JOIN-MISSING", "WEBGPU-CPU-CANDIDATE-ONLY"]:
            self.assertIn("RHI-ORIGINAL-GRAPHICS-FREEZE-"+backend,lowering)
        self.assertIn("Disposition::EquivalentCpuProjection,true,false",lowering)
        cold=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_cold")
        payload=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_payload")
        self.assertIn("request.contractVersion<1u || request.contractVersion>8u",cold)
        self.assertIn("request.contractVersion<1u || request.contractVersion>8u",payload)
        test=self.read_source("tests/contract/original_general_graphics_content_contract_tests.cpp")
        self.assertIn("F::closureProfileMatches(8u,10u)",test)
        self.assertIn("!F::closureProfileMatches(old,10u)",test)

    def test_graphics_payload8_requires_exact_graphics_tuple_and_leaves_execution_closed(self):
        payload=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_payload")
        for token in ["validateGraphicsProfile", "!work.completeGraphicsCounts()", "!colorAttachments",
                      "request.contractVersion==8u && freezeVersion==8u && closureVersion==10u",
                      "RHI-ORIGINAL-GRAPHICS-PAYLOAD-PROFILE"]:
            self.assertIn(token,payload)
        lowering=self.read_validation_section("src/rhi/semantics/command_lowering.h","original_typed_native_payload")
        for backend in ["NULL-VALIDATOR-ONLY", "VULKAN-JOIN-MISSING", "METAL-JOIN-MISSING", "WEBGPU-INPUT-ONLY"]:
            self.assertIn("RHI-ORIGINAL-GRAPHICS-PAYLOAD-"+backend,lowering)
        self.assertIn("D::DiagnosticFailure,false,false,false",lowering)
        self.assertIn("D::Prototype,true,false,false",lowering)
        successor=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_cold_command")
        self.assertIn("request.contractVersion<1u || request.contractVersion>8u",successor)
        test=self.read_source("tests/contract/original_general_graphics_content_contract_tests.cpp")
        self.assertIn("C::profileMatches(intent,8u,10u)",test)
        self.assertIn("work.commands==14u && work.nativeCalls==9u",test)

    def test_graphics_cold8_lifecycle5_keep_exact_root_and_all_native_gates_closed(self):
        cold=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_cold")
        for token in ["completeGraphicsCounts", "!computePasses && !dispatches",
                      "renderPasses && draws", "viewports && scissors && attachmentReferences",
                      "request.contractVersion>8u", "request.contractVersion==8u && !request.maxWork.completeGraphicsCounts()",
                      "request.contractVersion==8u && !required.work.completeGraphicsCounts()",
                      "version==8u ? 5u", "RHI-ORIGINAL-GRAPHICS-COLD-PROFILE"]:
            self.assertIn(token,cold)
        lifecycle=self.read_validation_section("src/rhi/frontend/command_recording.h","original_command_native_lifecycle")
        for token in ["budget.contractVersion==5u", "contentVersion==6u ? nullptr",
                      "RHI-ORIGINAL-NATIVE-LIFECYCLE-GRAPHICS-CONTENT-PROFILE-UNAVAILABLE",
                      "!root->originalMixedContentCounts()", "!root->hasOriginalFiniteGeneralShaderResourceProfile()",
                      "root->hasOriginalGeneralShaderResourceProfile()",
                      "RHI-ORIGINAL-NATIVE-LIFECYCLE-GRAPHICS-ROOT-PROFILE"]:
            self.assertIn(token,lifecycle)
        for region,prefix in [("original_typed_native_cold","COLD"),("original_command_native_lifecycle","LIFECYCLE")]:
            decision=self.read_validation_section("src/rhi/semantics/command_lowering.h",region)
            for backend in ["NULL-VALIDATOR-ONLY","VULKAN-","METAL-","WEBGPU-"]:
                self.assertIn("RHI-ORIGINAL-GRAPHICS-"+prefix+"-"+backend,decision)
            self.assertIn("false,false",decision)
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_admission.h")
        self.assertIn("version<1u || version>8u",backend)
        self.assertIn("RHI-WEBGPU-TYPED-COLD-GRAPHICS-ROOT-PROFILE",backend)
        self.assertIn("root->originalContentPlanContractVersion()!=6u",backend)
        self.assertIn("!root->originalMixedContentCounts()",backend)
        command=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_cold_command")
        self.assertIn("request.contractVersion>8u",command)
        test=self.read_source("tests/contract/original_general_graphics_content_contract_tests.cpp")
        for token in ["C::validateLifecycleProfile(intent,5u)","L::validateContentProfile(legacy,6u)",
                      "C::validateOwnerCapacity(bad,UINT32_MAX,UINT32_MAX,5u,46u,48u)",
                      "BINDLESS_RHI_GENERAL_GRAPHICS_COLD8_LIFECYCLE5_INTENT_OK"]:
            self.assertIn(token,test)

    def test_graphics_cold8_reuses_original_seal_before_proposal_and_never_executes(self):
        backend=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_admission.h")
        for token in ["C::validatePayloadProfile", "C::validateLifecycleProfile", "C::validateOwnerCapacity",
                      "payload->originalRequest().maxRetainedOwners", "C::validateSourceAuthority"]:
            self.assertIn(token,backend)
        for forbidden in ["wgpuQueueSubmit", "wgpuDeviceCreateCommandEncoder", "wgpuRenderPassEncoderDraw", "setLimitBytes"]:
            self.assertNotIn(forbidden,backend)
        fixture=self.read_source("tests/fixtures/original_typed_cold_admission_fixture.h").split("verifyGraphics8",1)[1].split("verifyComposed",1)[0]
        for token in ["requestFor(payload,8u)", "Seal::prepareOriginal", "Reservation::prepareOriginal",
                      "pressure.measureRetained(make)", "pressure.verify(peak,make)", "RHI-ORIGINAL-TRANSACTION-RECORDING-SEALED",
                      "preclaim.required.capacity.callbackTickets==8u", "native->pipeline=nullptr", "admission->validate()"]:
            self.assertIn(token,fixture)
        root=self.read_source("tests/fixtures/original_general_finite_graphics_content_root_fixture.h")
        self.assertLess(root.index("inspectColdCandidate(candidate,stream,rebuildOriginalCandidate)"),root.index("root->applyOriginalPrototypePlan()"))
        self.assertIn("candidate.reset();checkUnchanged()",root)
        source=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["budget.maxNativePayloadObjects+=capacity.encoders+capacity.commandBuffers",
                      "ColdAdmissionFixture::verifyGraphics8", "cold->validate()", "coldCandidateDone",
                      "graphics-Cold-abandoned-root", "source", "originalReservedCallbackRegistrationCount()"]:
            self.assertIn(token,source)
        runner=self.read_source("tests/fixtures/original_general_finite_graphics_content_root_runner.html")
        self.assertIn("nativeColdAdmissionValidated:nativeCold&&coldMatch",runner)
        self.assertIn("coldCandidateIndependentOfCpuBaseline",runner)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.executablePayload=false",device)

    def test_graphics_payload8_reuses_immutable_native_inputs_with_independent_owner_budget(self):
        native=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_native_payload.h")
        for token in ["request.contractVersion==8u", "decision->originalRequest().maxRetainedOwners",
                      "C::additionalOwners", "C::validateGraphicsProfile(result.work,result.colorAttachments)"]:
            self.assertIn(token,native)
        for forbidden in ["wgpuQueueSubmit", "wgpuCommandEncoderBeginRenderPass", "wgpuRenderPassEncoderDraw", "setLimitBytes"]:
            self.assertNotIn(forbidden,native)
        fixture=self.read_source("tests/fixtures/original_general_finite_native_projection_fixture.h")
        for token in ["verifyGraphicsPayloadActual", "OriginalTypedNativePayloadFixture::verify(frozen,quotas",
                      "work.commands==14u && work.nativeCalls==9u", "changed.passDrawCount",
                      "color.view=nullptr", "color.loadOp=WGPULoadOp_Undefined", "color.storeOp=WGPUStoreOp_Undefined"]:
            self.assertIn(token,fixture)
        root=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["heldGraphicsPayload->originalDecision()", "graphics-Pending-root", "heldGraphicsPayload.reset()",
                      "RHI-WEBGPU-CANONICAL-CLOSURE-PLAN-STATUS"]:
            self.assertIn(token,root)
        runner=self.read_source("tests/fixtures/original_general_finite_graphics_content_root_runner.html")
        self.assertIn("nativePayloadInputsValidated:nativePayload&&payloadInputsMatch",runner)

    def test_general_native_closure_browser_requires_custody_without_execution_promotion(self):
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["closure->resourceBindingCount()==6u", "closure->nativePayloadCount()==sourceCount",
                      "nativeSampler->sampler=nullptr", "nativeSampler->ownerDevice=nullptr",
                      "retainedClosure.expired() && retained.expired()", "retainedClosure.reset();retained.reset();assert(!recording->liveBytes())", "GENERAL_NATIVE_CLOSURE8_OK"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("originalGeneralNativeClosurePassed:generalClosure",runner)
        smoke=self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py")
        self.assertIn('result.get("originalGeneralNativeClosurePassed") is True',smoke)
        device=self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.execution=rhi::BindlessExecution::DiagnosticFailure",device)
        self.assertIn("caps_.executablePayload=false",device)

    def test_general_payload6_consumes_only_freeze6_closure8_without_execution_promotion(self):
        contract=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_payload")
        self.assertIn("request.contractVersion==6u && freezeVersion==6u && closureVersion==8u",contract)
        lowering=self.read_source("src/rhi/semantics/command_lowering.h").split("class OriginalTypedNativePayloadLowering final",1)[1].split("// endregion",1)[0]
        for token in ["if(version==8u)", "version<1u || version>7u", "case Backend::Null", "case Backend::Vulkan", "case Backend::Metal", "case Backend::WebGPU",
                      "RHI-ORIGINAL-GRAPHICS-PAYLOAD-WEBGPU-INPUT-ONLY", "D::Prototype,true,false,false"]:
            self.assertIn(token,lowering)
        cold=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_cold")
        self.assertIn("request.contractVersion>8u",cold)
        fixture=self.read_source("tests/fixtures/original_typed_native_payload_fixture.h")
        for token in ["general.contractVersion=6u", "C::profileMatches(general,6u,8u)", "generalBad.contractVersion=8u"]:
            self.assertIn(token,fixture)

    def test_general_cold6_uses_original_lifecycle3_and_separate_owner_domains(self):
        cold=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_cold")
        for token in ["request.contractVersion>8u", "generalProfile(request.contractVersion) ? generalLifecycleVersion(request.contractVersion)",
                      "validateOwnerCapacity", "std::uint64_t(payloadOwners)+additionalOwners",
                      "std::uint64_t(payloadCeiling)+additionalOwners", "rootOwners>sourceCeiling",
                      "ceiling>UINT32_MAX"]:
            self.assertIn(token,cold)
        admission=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_cold_admission.h")
        for token in ["C::validateOwnerCapacity", "payload->originalRequest().maxRetainedOwners",
                      "root->originalContentPlanContractVersion()!=4u", "hasOriginalGeneralShaderResourceProfile",
                      "RHI-WEBGPU-TYPED-COLD-GENERAL-ROOT-PROFILE"]:
            self.assertIn(token,admission)
        lifecycle=self.read_validation_section("src/rhi/frontend/command_recording.h","original_command_native_lifecycle")
        self.assertIn("budget.contractVersion==3u",lifecycle)
        self.assertIn("contentVersion==4u ? nullptr",lifecycle)
        self.assertIn("RHI-ORIGINAL-NATIVE-LIFECYCLE-GENERAL-ROOT-PROFILE",lifecycle)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["coldPressure.verify", "shortReservation", "forgedCold", "cold->validate()",
                      "payload.reset();reservation.reset();seal.reset()", "cold.reset()",
                      "WebGpuOriginalTypedColdCommand::prepareOriginal(cold", "BINDLESS_RHI_GENERAL_NATIVE_COLD6_OK"]:
            self.assertIn(token,fixture)
        command=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_cold_command")
        self.assertIn("request.contractVersion>8u",command)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("originalGeneralNativeColdPassed:generalCold",runner)
        self.assertIn("&& generalCold",runner)
        self.assertIn('result.get("originalGeneralNativeColdPassed") is True',
                      self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py"))

    def test_general_command6_keeps_original_claim_and_immutable_native_budget(self):
        command=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_native_cold_command")
        for token in ["lifecycle.contractVersion!=OriginalTypedNativeColdContract::generalLifecycleVersion(request.contractVersion)", "request.maxRetainedOwners>admissionCeiling",
                      "rootOwners>source.maxGraphOwners", "!OriginalTypedNativeColdContract::generalProfile(request.contractVersion) && owners>source.maxGraphOwners"]:
            self.assertIn(token,command)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for token in ["admission->originalRequest().maxRetainedOwners,root->retainedOwnerCount()",
                      "validateTypedUnclaimed(*result)", "seal->nativeColdCommandClaimed_=true"]:
            self.assertIn(token,native)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["measureSingleUse(rebindCommand,makeCommand)", "verifySingleUse(commandPeak,rebindCommand,makeCommand)",
                      "forgedCommand", "retainedCold.expired()", "command->validate()", "BINDLESS_RHI_GENERAL_NATIVE_COMMAND6_OK"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("originalGeneralNativeCommandPassed:generalCommand",runner)
        self.assertIn('result.get("originalGeneralNativeCommandPassed") is True',
                      self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py"))

    def test_general_callback6_keeps_original_observer_graph_and_native_budget(self):
        callback=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_callback_custody")
        for token in ["validateOwners", "request.contractVersion>8u", "lifecycle.contractVersion!=OriginalTypedNativeColdContract::generalLifecycleVersion(request.contractVersion)",
                      "request.maxRetainedOwners>commandCeiling", "request.maxRetainedOwners>admissionCeiling",
                      "rootOwners>source.maxGraphOwners", "owners>UINT32_MAX"]:
            self.assertIn(token,callback)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        inspect=native.split("const char* WebGpuCanonicalCallbackCustody::inspectTypedOriginal",1)[1].split("prepareTypedOriginal",1)[0]
        self.assertIn("C::validateOwners",inspect)
        self.assertNotIn("request.maxRetainedOwners>source.maxGraphOwners",inspect)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["submissionPressure.measureSingleUse", "callbackPressure.measureSingleUse",
                      "forgedCallback", "retiredCallback->cancelOriginal()", "retiredRoot->journalEmpty()",
                      "TypedIssuer::prepareOriginal(callbackCustody", "BINDLESS_RHI_GENERAL_NATIVE_CALLBACK6_OK"]:
            self.assertIn(token,fixture)
        self.assertIn("originalGeneralNativeCallbackPassed:generalCallback",
                      self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html"))
        self.assertIn('result.get("originalGeneralNativeCallbackPassed") is True',
                      self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py"))

    def test_general_pending_reads_bind_original_completed_content_and_recording_journal(self):
        model=self.read_source("src/rhi/frontend/command_model.h")
        for token in ["ImmutableUniformRead2", "version==2u && use==Use::ImmutableUniformRead2", "validateUse(version,use)"]:
            self.assertIn(token,model)
        for token in ["ImmutableShaderBufferRead3", "version==3u && use==Use::ImmutableShaderBufferRead3",
                      "finiteGeneral && !originalGeneral && contentVersion==5u"]:
            self.assertIn(token,model)
        root=self.read_source("src/rhi/frontend/command_state.h")
        self.assertIn("OriginalInitializedBufferPendingContract::validateRootProfile",root)
        texture=self.read_source("src/rhi/frontend/resource_texture_validation.h")
        self.assertIn("version==2u && finiteGeneralProfile && !originalGeneralProfile && !bindlessRootProfile && contentPlanVersion==5u",texture)
        seal=self.read_source("src/rhi/frontend/command_recording.h")
        for token in ["budget.contractVersion==4u", "contentVersion==5u", "RHI-ORIGINAL-NATIVE-LIFECYCLE-FINITE-ROOT-PROFILE",
                      "RHI-ORIGINAL-INITIALIZED-SHADER-BUFFER-PENDING-LIFECYCLE",
                      "RHI-COMPLETED-SHADER-TEXTURE-PENDING-LIFECYCLE"]:
            self.assertIn(token,seal)
        pending=root.split("const char* validateCompletedTextureReadPending(",1)[1].split("struct OriginalContentUpload",1)[0]
        for token in ["!recordingSealed_", "status_!=Status::Pending", "journal_.empty()", "object.validateOriginalContent(completed)",
                      "completed.registration_.matchesIdentity", "content->initial.value==completed.value_.value",
                      "content->initial.revision!=completed.value_.revision", "transitions!=1u", "initial.range()!=whole",
                      "cell->normalized->matchesPending(*cell->texturePending,journal_)",
                      "cell->scalar->matchesPending(*cell->scalarPending,journal_)", "Contract::validatePendingStates"]:
            self.assertIn(token,pending)
        self.assertNotIn("makeShared",pending)
        self.assertNotIn("makeUnique",pending)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        body=native.split("const char* WebGpuNativeCommandPreparation::validateTypedAdmissionRetained(",1)[1].split("const char* WebGpuNativeCommandPreparation::validateTypedRetained(",1)[0]
        for token in ["closure->closureContractVersion()==8u", "source.initializedReadyFacts()", "ImmutableShaderBufferRead3",
                      "seal->validateCompletedTextureReadPending", "closure->validateImmutableBindings(uniformReady,textureRead)"]:
            self.assertIn(token,body)
        recording=native.split("const char* WebGpuOrdinaryRecording::recordTyped(",1)[1].split("const char* WebGpuOriginalTypedIssuerPreparation::recordOriginal(",1)[0]
        self.assertLess(recording.index("seal->applyOriginalRecordingPlan()"),recording.rindex("validateTypedRetainedState(state)"))
        self.assertLess(recording.rindex("validateTypedRetainedState(state)"),recording.index("pushScopes"))
        self.assertIn("seal->rollbackOriginalRecordingPlan()",recording)
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        contents=closure.split("template<class At,class TextureRead> static const char* validateShaderTextureContents(",1)[1].split("template<class At",1)[0]
        self.assertLess(contents.index("validateCompletedTextureInput(content,*view,intent)"),contents.index("textureRead(content,*view)"))
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["Scopes::holdScopes(true)", "Scopes::waitHeldScopes()", "Scopes::releaseScopes()", "recordingDiagnostic()",
                      "validateTypedAdmissionRetained(pendingAdmission)", "retainedInitialized->readyDiagnostic()",
                      "BINDLESS_RHI_GENERAL_PENDING_READ_RECORDING6_OK"]:
            self.assertIn(token,fixture)
        self.assertIn("originalGeneralPendingReadRecordingPassed:generalPendingRead",self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html"))
        self.assertIn('result.get("originalGeneralPendingReadRecordingPassed") is True',self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py"))

    def test_general_submission_is_independent_and_frozen_before_native_recording(self):
        contract=self.read_source("src/rhi/frontend/command_shader_validation.h").split("struct OriginalTypedNativeExecutionRequest",1)[1]
        for token in ["struct GeneralSubmissionIntent", "GeneralSubmissionIntent generalSubmission",
                      "request.contractVersion != 6u", "request.contractVersion != 7u", "request.contractVersion != 8u",
                      "request.generalSubmission.contractVersion != (request.contractVersion == 8u ? 3u : request.contractVersion == 7u ? 2u : 1u)",
                      "!request.generalSubmission.prototypeOptIn", "validateRecordedGeneralSubmission",
                      "RHI-TYPED-NATIVE-GENERAL-SUBMISSION-RECORDED-PROFILE"]:
            self.assertIn(token,contract)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        record=native.split("const char* WebGpuOriginalTypedIssuerPreparation::recordOriginal(",1)[1].split("const char* WebGpuOriginalTypedIssuerPreparation::submitOriginal(",1)[0]
        self.assertLess(record.index("recordedGeneralSubmissionVersion = request.generalSubmission.contractVersion"),record.index("WebGpuOrdinaryRecording::recordTyped"))
        submit=native.split("const char* WebGpuOriginalTypedIssuerPreparation::submitOriginal(",1)[1].split("bool WebGpuOriginalTypedIssuerPreparation::hasOriginalRecordedCommand",1)[0]
        self.assertLess(submit.index("C::validateRecordedGeneralSubmission"),submit.index("WebGpuOrdinarySubmission::submitTyped"))
        self.assertIn("request.readbackCompletion.contractVersion, request.generalSubmission.contractVersion",submit)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["project(*c,true)", "SubmissionScopes::holdScopes(true)", "SubmissionScopes::waitHeldScopes()",
                      "SubmissionScopes::releaseScopes()", "hasOriginalCompletedSubmission()",
                      "pendingRoot->status()==Root::Status::Committed && pendingRoot->journalEmpty()",
                      "BINDLESS_RHI_GENERAL_SUBMISSION1_OK"]:
            self.assertIn(token,fixture)
        self.assertIn("originalGeneralSubmissionPassed:generalSubmission",self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html"))
        self.assertIn('result.get("originalGeneralSubmissionPassed") is True',self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py"))

    def test_general_output_snapshot_authenticates_existing_issuer_root_and_output(self):
        contract=self.read_source("src/rhi/frontend/command_transfer_validation.h").split("class OriginalGeneralOutputSnapshotContract final",1)[1]
        for token in ["QueueOrderedSnapshot", "maxJoinVisits", "maxSourceBytes", "maxCpuMetadataBytes",
                      "TextureUsage::Storage | TextureUsage::CopySrc", "issuerOwners == UINT32_MAX",
                      "defaultTextureSubresourceRange(desc)"]:
            self.assertIn(token,contract)
        header=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_issuer_preparation.h")
        self.assertIn("std::weak_ptr<const rhi::OriginalCommandStateTransaction> originalRoot_",header)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        association=native.split("WebGpuOriginalGeneralOutputSnapshot::prepareOriginal",1)[1].split("const char* WebGpuOriginalTypedIssuerPreparation::recordingDiagnostic",1)[0]
        for token in ["validateGeneralOutputIssuer(issuer, root, false, budget.contractVersion)", "Projection::visitStateBound",
                      "core::SharedOwnerProjection::sameOwner(*parent, output)", "matches != 1u",
                      "core::CpuMemoryQuotaScope scope(quota)", "native_.matches()",
                      "observeOriginalCommittedState(self->budget_.range)"]:
            self.assertIn(token,association)
        issuer=native.split("WebGpuOriginalTypedIssuerPreparation::validateGeneralOutputIssuer",1)[1].split("WebGpuOriginalGeneralOutputSnapshot::prepareOriginal",1)[0]
        for token in ["self->originalRoot_.lock()", "state->recordedGeneralSubmissionVersion != outputVersion",
                      "state->queueNativeReceiptReceived", "state->foreignCallbacksDrainedExceptLoss()",
                      "Status::Committed", "!root->journalEmpty()"]:
            self.assertIn(token,issuer)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["snapshotPressure.measureRetained", "snapshotPressure.verify", "forgedRoot", "forgedOutput",
                      "OutputSnapshot::validateCompletedOriginal(outputSnapshot)", "BINDLESS_RHI_GENERAL_OUTPUT_SNAPSHOT1_OK"]:
            self.assertIn(token,fixture)
        self.assertIn('result.get("originalGeneralOutputSnapshotPassed") is True',self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py"))

    def test_general_output_readback_join_requires_separate_retained_transfer_profile(self):
        contract=self.read_validation_section("src/rhi/frontend/command_transfer_validation.h", "original_command_readback_join")
        for token in ["GeneralOutputIntent", "validateGeneralOutputIntent", "b.contractVersion!=4u",
                      "b.generalOutput.contractVersion!=1u", "!b.generalOutput.prototypeOptIn",
                      "b.stream.contractVersion!=1u || b.result.contractVersion!=1u",
                      "stateVersion==1u && inputsVersion==1u", "contentVersion==0u && sourceVersion==2u",
                      "inspectGeneralOutput", "OriginalCommandStateTransaction::validateOriginal(root)",
                      "retainedReadbackCorrespondence().validate()", "OriginalCommandReadbackResultContract::inspect",
                      "RHI-ORIGINAL-READBACK-JOIN-GENERAL-OUTPUT-PROFILE"]:
            self.assertIn(token,contract)
        for forbidden in ["wgpu", "makeUnique", "makeShared", "MutationJournal", "commitOriginal"]:
            self.assertNotIn(forbidden,contract)
        lowering=self.read_validation_section("src/rhi/semantics/command_lowering.h", "original_command_readback_join")
        for backend in ["NULL-VALIDATOR-ONLY", "VULKAN-JOIN-MISSING", "METAL-JOIN-MISSING", "WEBGPU-EXECUTION-MISSING"]:
            self.assertIn("RHI-GENERAL-OUTPUT-READBACK-"+backend,lowering)
        fixture=self.read_source("tests/fixtures/original_command_readback_result_contract_fixture.h")
        self.assertIn("verifyGeneralOutputJoin(join)",fixture)
        self.assertIn("BINDLESS_RHI_GENERAL_OUTPUT_READBACK_JOIN4_CONTRACT_OK",fixture)
        inputs=self.read_source("tests/fixtures/original_general_output_readback_inputs_fixture.h")
        for token in ["Snapshot::validateCompletedOriginal(snapshot)", "prepareRetainedOriginal(output,request",
                      "Source", "Readback::prepareOriginal", "ShaderWrite", "ResourceState::CopySrc",
                      "Join::inspectGeneralOutput(root,b)", "observation->revision()",
                      "verifyHotNoAllocation", "sourceQuota->liveBytes()==sourceBaseline",
                      "BINDLESS_RHI_GENERAL_OUTPUT_READBACK_INPUTS4_OK"]:
            self.assertIn(token,inputs)
        for forbidden in ["wgpuQueue", "wgpuCommandEncoder", "wgpuBufferMap", "WebGpuNativeReadbackSeedFixture"]:
            self.assertNotIn(forbidden,inputs)
        self.assertIn('result.get("originalGeneralOutputReadbackInputsPassed") is True',self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py"))
        dependency=self.read_source("src/rhi/frontend/resource_state_sequence.h").split("class OriginalGeneralOutputReadbackDependencyContract final",1)[1]
        for token in ["validateBarrier(desc,current)", "outputVersion<1u || outputVersion>3u",
                      "const bool graphics=outputVersion==3u",
                      "graphics ? ResourceState::ColorAttachment : ResourceState::ShaderWrite",
                      "graphics ? PipelineStage::ColorAttachmentOutput : PipelineStage::ComputeShader",
                      "graphics ? ResourceAccess::ColorAttachmentRead | ResourceAccess::ColorAttachmentWrite : ResourceAccess::ShaderWrite",
                      "desc.before==source", "desc.sourceStages==stages", "desc.sourceAccess==access",
                      "desc.after==ResourceState::CopySrc", "PipelineStage::ComputeShader",
                      "ResourceAccess::ShaderWrite", "PipelineStage::Transfer", "ResourceAccess::TransferRead"]:
            self.assertIn(token,dependency)
        mapping=self.read_validation_section("src/rhi/semantics/command_lowering.h", "original_general_output_readback_dependency")
        self.assertIn("OriginalGeneralOutputReadbackDependencyContract::validate(desc,current,wholeRange,version)",mapping)
        self.assertIn("TransferDependencyLowering::Operation::ImplicitOrderedUsage",mapping)

    def test_general_output_native_join_retains_authentic_completed_revision_without_execution(self):
        header=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_readback_joins.h")
        for token in ["prepareOriginalGeneralOutput", "WebGpuOriginalGeneralOutputSnapshot", "generalOutputStates_",
                      "BoundedSequence<rhi::GpuTexture::CommittedStateObservation>"]:
            self.assertIn(token,header)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        binding=native.split("const char* WebGpuCanonicalReadbackJoins::validateGeneralOutputBinding",1)[1].split("const char* WebGpuCanonicalReadbackJoins::validateRetained",1)[0]
        for token in ["Snapshot::validateCompletedOriginal(input.generalOutput)", "sameOwner(output,input.source->originalSource()->originalLease()->originalTextureOwner())",
                      "validateOriginalCommittedState(*frozen)", "entry->observation.revision()!=observed->revision()",
                      "validateGeneralOutputCommand", "validateFrozenEntries(nullptr,false)"]:
            self.assertIn(token,binding)
        for forbidden in ["wgpu", "commitOriginal", "makeUnique", "makeShared", "MutationJournal"]:
            self.assertNotIn(forbidden,binding)
        fixture=self.read_source("tests/fixtures/original_general_output_readback_inputs_fixture.h")
        for token in ["verifyNativeJoin", "measureRetained(make)", "pressure.verify(peak,make)", "verifyNoHotQueries",
                      "generalOutput.reset()", "BINDLESS_RHI_GENERAL_OUTPUT_NATIVE_JOIN4_OK"]:
            self.assertIn(token,fixture)
        cold=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_cold_admission.h")
        self.assertIn("readbacks->joinContractVersion()!=1u",cold)
        self.assertIn('result.get("originalGeneralOutputNativeJoinPassed") is True',self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py"))

    def test_general_output_cold_keeps_default_factories_closed_and_checks_upstream_quota_domains(self):
        contract=self.read_validation_section("src/rhi/frontend/command_recording.h", "original_command_native_cold")
        for token in ["GeneralOutputIntent", "validateGeneralOutputIntent", "b.contractVersion!=5u",
                      "b.generalOutput.contractVersion!=1u", "!b.generalOutput.prototypeOptIn"]:
            self.assertIn(token,contract)
        cold=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_cold_admission.h")
        for token in ["prepareOriginalGeneralOutput", "RHI-WEBGPU-CANONICAL-COLD-GENERAL-OUTPUT-FACTORY",
                      "budget.generalOutput.contractVersion ? readbacks->joinContractVersion()!=4u",
                      "OriginalGeneralOutputExecutionLowering::lowering", "validateGeneralOutputSource", "validateGeneralOutputCommand"]:
            self.assertIn(token,cold)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for token in ["entry.generalOutput->overlapsCpuOwner(quota)", "root->overlapsCpuOwner(quota)",
                      "closure->overlapsNativeOwner(quota)", "custody->overlapsCpuOwner(quota)",
                      "results->originalResultQuota()", "issuer_->custody_->overlapsCpuOwner(quota)"]:
            self.assertIn(token,native)
        fixture=self.read_source("tests/fixtures/original_general_output_readback_inputs_fixture.h")
        for token in ["verifyCold", "Cold::prepareOriginalGeneralOutput", "pressure.verify(peak,make)",
                      "originalReservedSubmissionCount()", "BINDLESS_RHI_GENERAL_OUTPUT_COLD1_OK"]:
            self.assertIn(token,fixture)
        for forbidden in ["wgpuQueueSubmit", "wgpuCommandEncoderCopy", "wgpuBufferMapAsync", "WebGpuNativeReadbackSeedFixture"]:
            self.assertNotIn(forbidden,fixture)
        self.assertIn('result.get("originalGeneralOutputColdPassed") is True',self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py"))
        state=self.read_validation_section("src/rhi/frontend/command_state.h", "original_command_state_transaction")
        handoff=state.split("const char* validateGeneralOutputReadPending",1)[1].split("const char* validatePlannedFinals()",1)[0]
        for token in ["validate()", "upstream.validateRetainedPlan(outputVersion==3u ? &object : nullptr)",
                      "if(outputVersion==3u)", "!content || content->mutations", "cell.written || !(cell.initial==cell.proposed)",
                      "cell.initial.state!=Content::State::Completed", "cells!=1u", "upstream.graph_->validateGraph(false)",
                      "upstream.validatePlannedFinalCell(cell)", "initial.revision()!=frozen.revision()",
                      "cell.textureCommitted->revision!=frozen.revision()", "OriginalGeneralOutputReadbackDependencyContract::validatePendingStates",
                      "OriginalGeneralOutputReadbackDependencyContract::validate", "delegated==1u"]:
            self.assertIn(token,handoff)
        # Handoff is private; only the seal and completed-output association are authorized.
        self.assertEqual(re.findall(r"friend class ([^;]+);",state),["OriginalCommandRecordingSeal","OriginalGeneralOutputSnapshotContract","OriginalCommandStateTransaction"])
        seal=self.read_validation_section("src/rhi/frontend/command_recording.h", "original_command_recording_seal")
        pending=seal.split("const char* validateGeneralOutputReadPending",1)[1].split("const char* validateInitializedTexturePending",1)[0]
        for token in ["hasQuotaBoundSharedOwner(self)", "validateRetainedOwnership()", "!lifecycleReserved_", "!nativeColdCommandClaimed_"]:
            self.assertIn(token,pending)
        for token in ["forgedSeal", "forgedSnapshot", "holdScopes(true)", "releaseScopes()", "analyzeStridedImageChannels",
                      "actual==expected[channel]", "BINDLESS_RHI_GENERAL_OUTPUT_NUMERICAL1_OK"]:
            self.assertIn(token,fixture)

    def test_general_issuer6_uses_original_receipts_and_independent_native_budget(self):
        issuer=self.read_validation_section("src/rhi/frontend/command_shader_validation.h","original_typed_issuer_preparation")
        for token in ["request.contractVersion>8u", "validateOwners", "lifecycle.contractVersion!=OriginalTypedNativeColdContract::generalLifecycleVersion(request.contractVersion)",
                      "request.maxRetainedOwners>callbackCeiling", "request.maxRetainedOwners>commandCeiling",
                      "request.maxRetainedOwners>admissionCeiling", "rootOwners>source.maxGraphOwners"]:
            self.assertIn(token,issuer)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        inspect=native.split("const char* WebGpuCanonicalCallbackCustody::inspectTypedIssuers",1)[1].split("prepareTypedIssuers",1)[0]
        self.assertIn("C::validateOwners(state.admittedOwners",inspect)
        self.assertIn("owners=0u",inspect)
        self.assertNotIn("request.maxRetainedOwners>source.maxGraphOwners",inspect)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["+rhi::OriginalTypedIssuerPreparationContract::additionalOwners", "OriginalTypedIssuerPreparationFixture::verifyGeneral",
                      "typedIssuers->validate()", "retainedCallback.expired()", "verifyGeneralReceiptsRetired",
                      "BINDLESS_RHI_GENERAL_NATIVE_ISSUER6_OK"]:
            self.assertIn(token,fixture)
        self.assertLess(fixture.index("retiredCallback->cancelOriginal()"),fixture.index("typedIssuers.reset();assert"))
        self.assertLess(fixture.index("typedIssuers.reset();assert"),fixture.index("verifyGeneralReceiptsRetired();"))
        helper=self.read_source("tests/fixtures/original_typed_issuer_preparation_fixture.h")
        self.assertIn("verifyCold(live,true)",helper)
        self.assertIn("arpGeneralIssuerStates.length===7",helper)
        self.assertIn("originalGeneralNativeIssuerPassed:generalIssuer",
                      self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html"))
        self.assertIn('result.get("originalGeneralNativeIssuerPassed") is True',
                      self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py"))

    def test_general_payload6_reuses_records_and_independent_native_owner_bound(self):
        payload=self.read_source("src/rhi/backends/webgpu/webgpu_original_typed_native_payload.h")
        for token in ["request.contractVersion==6u", "decision->originalRequest().maxRetainedOwners", "C::additionalOwners",
                      "request.maxRetainedOwners>ownerCeiling", "source->originalBudget().maxGraphOwners",
                      "core::BoundedSequence<Record> records_", "core::BoundedSequence<Pass> passes_",
                      "D::validateOriginal(decision)", "validateImmutableRecords()"]:
            self.assertIn(token,payload)

    def test_general_payload6_browser_checks_exact_records_canonical_owners_and_retirement(self):
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["requestFor(frozenDecision,6u)", "payloadBudget.maxWork.commands==10u",
                      "payloadBudget.maxWork.implicitAccesses==4u", "payload->compareOriginalDecision(peerFreeze)",
                      "record.binding->sourceIndex==7u", "record.pipeline->sourceIndex==6u",
                      "retainedFreeze.expired() && retainedClosure.expired() && retained.expired()",
                      "retainedFreeze.reset();retainedClosure.reset();retained.reset()", "GENERAL_NATIVE_PAYLOAD6_OK"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("originalGeneralNativePayloadPassed:generalPayload",runner)
        smoke=self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py")
        self.assertIn('result.get("originalGeneralNativePayloadPassed") is True',smoke)

    def test_general_shader_buffer_content_has_versioned_readonly_domain_and_four_lowerings(self):
        contract=self.read_validation_section("src/rhi/frontend/command_transfer_validation.h","original_command_initialized_buffer_staging")
        for token in ["struct ShaderContentIntent", "b.contractVersion<1u || b.contractVersion>4u",
                      "DescriptorType::UniformBuffer", "DescriptorType::ReadOnlyStorageBuffer",
                      "b.shaderContent.bindingBytes>b.maxCpuPayloadBytes", "validateShaderPayload(b,payloadBytes)",
                      "RHI-INITIALIZED-CANONICAL-STAGING-SHADER-SCHEMA"]:
            self.assertIn(token,contract)
        lowering=self.read_validation_section("src/rhi/semantics/command_lowering.h","original_command_initialized_buffer_staging")
        for token in ["version==4u", "RHI-INITIALIZED-SHADER-NULL-VALIDATOR-ONLY",
                      "RHI-INITIALIZED-SHADER-VULKAN-PAYLOAD-MISSING", "RHI-INITIALIZED-SHADER-METAL-PAYLOAD-MISSING",
                      "RHI-INITIALIZED-SHADER-WEBGPU-PRIVATE-PROFILE"]:
            self.assertIn(token,lowering)

    def test_general_shader_buffer_content_reuses_original_initializer_and_canonical_lease(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_upload_preparation.h")
        for token in ["validateInitializedShaderBufferWithReady", "Contract::validateShaderPayload(state.budget,state.payload.size())",
                      "sameOwner(lease,state.lease)", "type!=state.budget.shaderContent.type", "return self->initializedReadyFacts()",
                      "WGPUBufferUsage_Uniform", "originalObject->sealWrites()"]:
            self.assertIn(token,source)
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["using ShaderBufferContentInput=IndexContentInput", "validateShaderBufferContents",
                      "budget.shaderBufferContent", "frozen.declaration", "RHI-WEBGPU-GENERAL-CONTENT-DUPLICATE",
                      "RHI-WEBGPU-GENERAL-CONTENT-COVERAGE", "bytes>intent.maxInitializedBytes-totalBytes"]:
            self.assertIn(token,closure)

    def test_general_shader_buffer_content_browser_requires_completed_same_chain_and_closed_execution(self):
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["Upload::Budget initialize{4u,true", "Initialized::initialize(initialized)", "initialized->callbacksDrained()",
                      "initialized->compareFrozenPayload", "custody.shaderBufferContent={1u,true",
                      "retainedContent.expired()", "GENERAL_SHADER_BUFFER_CONTENT_OK"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("originalGeneralShaderBufferContentPassed:generalContent",runner)
        self.assertIn("nativeExecutionValidated:false",runner)
        smoke=self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py")
        self.assertIn('result.get("originalGeneralShaderBufferContentPassed") is True',smoke)

    def test_general_sampled_content_reuses_same_device_original_upload_and_keeps_execution_closed(self):
        upload=self.read_source("tests/fixtures/webgpu_canonical_texture_upload_source_fixture.h")
        borrowed=upload.split("Context(rhi::GpuDevice& supplied",1)[1].split("rhi::GpuDevice& gpu()",1)[0]
        for forbidden in ["create()", "prepareOriginalDeviceLifetime", "prepareOriginalResourceRegistration", "device.reset()"]:
            self.assertNotIn(forbidden,borrowed)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["BorrowedContext sampledUpload(*c.device,source,resource)", "Issuer::recordOriginal", "Issuer::submitOriginal",
                      "validateOriginalCompletedShaderTextureInput", "K::TextureBarrier,sampledTransition",
                      "sampledTransition.before=rhi::ResourceState::CopyDst", "sampledTransition.after=rhi::ResourceState::ShaderRead",
                      "custody.shaderTextureContent={1u,true", "uploadIssuer->retainedOwnerCount()", "retainedTextureIssuer.expired()",
                      "committedState->state()==rhi::ResourceState::CopyDst"]:
            self.assertIn(token,fixture)
        execution=fixture.split("template<class Context> static auto project",1)[1].split("template<class Context> static void verifyFiniteBindings",1)[0]
        self.assertNotIn("sampled=texture(*c.device,source,resource,false)",execution)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("originalGeneralShaderTextureContentPassed:generalTextureContent",runner)
        self.assertIn("nativeExecutionValidated:false",runner)
        smoke=self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py")
        self.assertIn('result.get("originalGeneralShaderTextureContentPassed") is True',smoke)

    def test_completed_whole_read_plan_preserves_content_stamp_and_reuses_transaction(self):
        source=self.read_source("src/rhi/frontend/command_state.h")
        section=source.split("class OriginalCommandStateTransaction final",1)[1]
        for token in ["inspectCompletedContentReadPlan", "OriginalCompletedTextureReadPlanContract::validateIntent",
                      "OriginalCompletedTextureReadPlanContract::validateObservation", "OriginalCompletedTextureReadPlanContract::isReadOnly(plan.contentBudget.contractVersion)",
                      "cell.control->value_.matches(cell.initial)", "RHI-COMPLETED-TEXTURE-READ-PLAN-STALE",
                      "RHI-COMPLETED-TEXTURE-READ-PLAN-UNSUPPORTED-USE"]:
            self.assertIn(token,section)
        inspector=section.split("ContentCellsInspection inspectCompletedContentReadPlan",1)[1].split("const char* preflightContentPlan",1)[0]
        for forbidden in ["canUpload", "->propose(", "->complete(", "makeShared", "MutationJournal"]:
            self.assertNotIn(forbidden,inspector)
        lowering=self.read_source("src/rhi/semantics/command_state_lowering.h").split("textureCompletedReadPlanLowering",1)[1]
        for backend in ["Null","Vulkan","Metal","WebGPU"]:
            self.assertIn("case Backend::"+backend,lowering)
        self.assertIn("false, true, false",lowering)
        fixture=self.read_source("tests/fixtures/original_general_shader_resources_projection_fixture.h")
        for token in ["preflightOriginalCompletedContentReads", "plannedContentControlCount()==1u",
                      "plannedContentUploadCount()==0u", "completedTexture->diagnostic()", "maxProjectionVisits=readPreflight.projectionVisits"]:
            self.assertIn(token,fixture)
        self.assertIn("inventory[p.viewSourceIndex]",inspector)
        self.assertIn("(*view)->originalParentLease()->originalObject() != object",inspector)
        self.assertIn("TypedIssuer::recordOriginal(typedIssuers,recording6)",fixture)
        self.assertIn("retiredRoot->status()==Root::Status::Abandoned && retiredRoot->journalEmpty()",fixture)
        self.assertIn("committedState->diagnostic() && graph->validate()",fixture)
        self.assertIn("afterState->revision()>committedState->revision()",fixture)
        self.assertIn("afterRead->revision()==contentRevision",fixture)
        lifecycle=self.read_source("src/rhi/frontend/command_recording.h")
        self.assertIn("RHI-ORIGINAL-NATIVE-LIFECYCLE-COMPLETED-READ-PROFILE-UNAVAILABLE",lifecycle)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("completedTextureReadPlanPassed:completedReadPlan",runner)

    def test_completed_shader_texture_input_requires_genuine_original_issuer_and_completed_source(self):
        source=self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        section=source.split("WebGpuCanonicalIssuerPreparation::validateOriginalCompletedShaderTextureInput",1)[1].split("WebGpuCanonicalIssuerPreparation::submissionDiagnostic",1)[0]
        for token in ["hasQuotaBoundSharedOwner", "hasOriginalCompletedSubmission", "foreignCallbacksDrainedExceptLoss",
                      "originalRoot_.lock()", "Status::Committed", "journalEmpty()", "validateCompletedInputFacts",
                      "packing->originalSource()", "packing->originalDestinationLease()", "root->plannedContentUploadCount()"]:
            self.assertIn(token,section)
        self.assertNotIn("makeShared",section)
        self.assertNotIn("makeUnique",section)

    def test_completed_shader_texture_input_four_backend_lowering_never_grants_shader_execution(self):
        source=self.read_source("src/rhi/semantics/command_lowering.h")
        section=source.split("class OriginalCompletedShaderTextureInputLowering final",1)[1].split("} // namespace",1)[0]
        for backend in ["Null","Vulkan","Metal","WebGPU"]:
            self.assertIn("case Backend::"+backend,section)
        self.assertIn("shaderExecutionAvailable = false",section)
        self.assertIn("Disposition::Prototype,true,false",section)

    def test_completed_shader_texture_input_browser_requires_texels_retirement_and_closed_shader_gate(self):
        source=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        for token in ["original-completed-shader-texture-input", "numericalUploadedTextureReadback:pixels",
                      "completedShaderTextureInputRetirementPassed:retired", "nativeShaderExecutionValidated:false"]:
            self.assertIn(token,source)
        smoke=self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py")
        self.assertIn('result.get("completedShaderTextureInputPassed") is True',smoke)
        self.assertNotIn("progressTimes",source)

    def test_completed_shader_texture_view_and_custody_preserve_state_authority_and_staging_budget(self):
        contract=self.read_source("src/rhi/frontend/command_recording.h")
        for token in ["struct ShaderTextureContentCustody", "maxInitializedBytes", "maxUploads", "maxMipLevels", "maxJoinVisits",
                      "RHI-ORIGINAL-SHADER-TEXTURE-CUSTODY-VERSION", "RHI-ORIGINAL-SHADER-TEXTURE-CUSTODY-OPT-IN"]:
            self.assertIn(token,contract)
        validation=self.read_source("src/rhi/frontend/command_transfer_validation.h")
        self.assertIn("OriginalCompletedTextureReadPlanContract::inspectViewBinding",validation)
        validation=self.read_source("src/rhi/frontend/resource_texture_validation.h")
        for token in ["inspectViewBinding", "OriginalTextureStateRange", "RHI-COMPLETED-SHADER-TEXTURE-VIEW-ACCESS",
                      "RHI-COMPLETED-SHADER-TEXTURE-VIEW-RANGE"]:
            self.assertIn(token,validation)
        closure=self.read_source("src/rhi/backends/webgpu/webgpu_canonical_native_closure.h")
        for token in ["validateCompletedTextureInput", "stagingAllocationRecord", "entry.textureContent.emplace(*input)",
                      "validateShaderTextureContents", "RHI-WEBGPU-GENERAL-TEXTURE-SOURCE-CAPACITY"]:
            self.assertIn(token,closure)
        fixture=self.read_source("tests/fixtures/webgpu_canonical_upload_projection_fixture.h")
        for token in ["committed-CopyDst transition-unissued", "observeOriginalCommittedState", "wholeRetired.expired()", "source-quota final-original-retirement",
                      "RHI-COMPLETED-SHADER-TEXTURE-ORIGINAL-ROOT", "RHI-WEBGPU-GENERAL-TEXTURE-OWNER-CAPACITY"]:
            self.assertIn(token,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        self.assertIn("&& view &&",runner)
        self.assertIn("completedShaderTextureViewInputPassed:view",runner)
        smoke=self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py")
        self.assertIn('result.get("completedShaderTextureViewInputPassed") is True',smoke)

    def test_readback_range_fault_proves_actual_native_exception_and_rhi_refusal_without_log_dependency(self):
        source=self.read_source("tests/fixtures/webgpu_map_handoff_fixture.h")
        self.assertIn("c.read.call(this,offset,size+4)",source)
        self.assertIn("arpMappedRangeNativeExceptions++",source)
        fixture=self.read_source("tests/fixtures/webgpu_original_texture_readback_native_mapping_fixture.h")
        self.assertIn("RHI-WEBGPU-READBACK-MAP-RANGE-READ",fixture)
        self.assertIn("actual-native-exception-recorded-once stable-rhi-rejection",fixture)
        self.assertNotIn("return Module.arpExpectedMappedRangeObserved",fixture)

    def test_canonical_read_faults_prove_intercepted_invocation_refusal_and_retirement_without_log_dependency(self):
        probe = self.read_source("tests/fixtures/webgpu_native_map_execution_probe_fixture.h")
        self.assertIn("++p.readFaultInvocations", probe)
        self.assertIn("p.readFaultInvocations===1", probe)
        fixture = self.read_source("tests/fixtures/webgpu_canonical_readback_joins_fixture.h")
        self.assertEqual(fixture.count("NativeProbe::verifyReadFault();"), 2)
        self.assertIn("Module.arpMapReceiptReadFaultInvocations===1", fixture)
        self.assertIn("assert((result==std::array<unsigned char,768>{}));", fixture)
        runner = self.read_source("tests/fixtures/bindless_webgpu_rhi_runner.html")
        for marker in [
            "BINDLESS_RHI_CANONICAL_PUBLICATION_READ_FAULT_OK intercepted-read-once exact-rhi-refusal unpublished callbacks-drained quotas11-zero",
            "BINDLESS_RHI_CANONICAL_MAP_EXECUTION_READ_FAULT_OK intercepted-read-once exact-rhi-refusal unpublished callbacks-drained quotas11-zero",
            "BINDLESS_RHI_NATIVE_MAP_RECEIPT_READ_FAULT_OK intercepted-read-once exact-rhi-refusal output-unchanged native-unmapped quotas8-zero",
        ]:
            self.assertIn(marker, fixture)
            self.assertIn(marker, runner)
        for name in ["arpCanonicalPublicationReadDiagnostics", "arpCanonicalMapExecutionReadDiagnostics", "arpCanonicalMapReadDiagnostics"]:
            self.assertNotIn(f"(Module.{name} || []).length===1", runner)

    def test_original_buffer_readback_request_uses_real_registration_and_shared_result_storage(self):
        request = self.read_source("src/rhi/frontend/transfer_buffer_readback.h")
        for token in ["OriginalCommandBufferLease::validateOriginal(source)",
                      "device.validateOriginalResourceRegistration(*source->originalObject())",
                      "originalResourceRegistration().generation()", "MemoryUsage::GpuToCpu",
                      "containsUnsignedRange", '"rhi/buffer_readback_range.h"']:
            self.assertIn(token, request)
        ranges = self.read_source("src/rhi/buffer_readback_range.h")
        for token in ("offsetAlignment=8u", "sizeAlignment=4u"):
            self.assertIn(token, ranges)
        owned_fields = request.rsplit("private:", 1)[1]
        for token in ["shared_ptr", "UniquePtr", "GpuBuffer*", "TextureReadbackLayout", "ReadbackTextureDesc"]:
            self.assertNotIn(token, owned_fields)
        snapshot = self.read_validation_section("src/rhi/frontend/command_sources.h", "original_command_readback_result_snapshot")
        for token in ["OriginalCommandReadbackResultSnapshotT", "TextureReadbackRequestDeclaration",
                      "BufferReadbackRequestDeclaration", "OriginalCommandReadbackRequestFacts::cpuObjects(*request)",
                      "OriginalCommandReadbackRequestFacts::byteCapacity(*request)"]:
            self.assertIn(token, snapshot)
        lowering = self.read_source("src/rhi/semantics/transfer_lowering.h")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::" + backend + ":", lowering)
        self.assertIn("nativeExecutionImplemented=false", lowering)

    def test_original_buffer_readback_binding_keeps_the_same_graph_copy_and_unpublished_result(self):
        binding = self.read_source("src/rhi/frontend/command_buffer_readback.h")
        for token in ["program->compareOriginalStates(graph)", "validateProfile(budget.contractVersion",
                      "program->originalBudget().bindlessRoots.contractVersion",
                      "version==4u||version==5u ? 6u : version==3u||version==6u ? 5u",
                      "Result::validateOriginalCold(result,request)", "request->matchesOriginalSource(readback)",
                      "request->identity().resourceGeneration!=snapshot->generation()",
                      "sameOwner(*source,latch)", "sameOwner(*destination,faultReadback)",
                      "fields.destinationOffsetBytes!=request->offsetBytes()", "fields.sizeBytes!=request->byteCapacity()",
                      "maxOperationVisits", "RHI-ORIGINAL-BUFFER-READBACK-BINDING-DUPLICATE-WRITE"]:
            self.assertIn(token, binding)
        for forbidden in ["makeUnique<Graph>", "makeShared<Graph>", "MutationJournal", "wgpuBufferMapAsync",
                          "vkMapMemory", "published_=true", "CompletionSignal::complete", "std::vector", "std::make_shared"]:
            self.assertNotIn(forbidden, binding)

    def test_buffer_readback_pending_has_original_journal_custody_and_no_public_state_bypass(self):
        binding = self.read_source("src/rhi/frontend/command_buffer_readback.h")
        for token in ["validateOriginalPending", "transaction->validate()", "transaction->originalPlannedSequenceBudget()",
                      "fault.originalJournalOwned()", "sameOwner(fault.originalDomain(),self->originalSnapshot())"]:
            self.assertIn(token, binding)
        self.assertNotIn("published_=true", binding)
        domain = self.read_source("src/rhi/frontend/bindless_fault_domain.h")
        self.assertIn("shared_ptr<core::AtomicCapacityBudget>submissions_", domain)
        source = self.read_source("src/rhi/frontend/bindless_fault_domain.cpp")
        self.assertIn("if(domain_&&!originalJournal_)", source)
        self.assertIn("RHI-BINDLESS-FAULT-SUBMISSION-ORIGINAL-JOURNAL", source)

    def test_buffer_readback_counts_reuse_original_result_map_and_drain_contracts(self):
        source = self.read_source("src/rhi/frontend/command_buffer_readback.h")
        for token in ["using Common=OriginalCommandReadbackResultContract", "inspected.required={}",
                      "copyCount!=count", "core::SharedIdentityProjection::inspect", "maxVisits",
                      "sameOwner(resultQuota,binding->originalResult()->originalMetadataQuota())",
                      "Common::validateRequirements(pending,budget)"]:
            self.assertIn(token, source)
        fixture = self.read_source("tests/fixtures/original_bindless_root_source_fixture.h")
        for token in ["OriginalCommandReadbackMapContract::deriveRequirements(projectedResults.required",
                      "inventory->originalBudget()", "OriginalCommandReadbackMapDrainContract::inspect(maps,drain)",
                      "!bufferResult->publishedBytes()"]:
            self.assertIn(token, fixture)

    def test_bindless_native_closure_cannot_gain_state_or_completion_authority(self):
        header = self.read_source("src/rhi/backends/vulkan/vulkan_bindless_root.h")
        source = self.read_source("src/rhi/backends/vulkan/vulkan_bindless_root.cpp")
        for token in ["Root::validateOriginal(root, device)", "Sources::validateRegisteredLifetime(inventory, device)",
                      "inventory->originalLeaseIndex(root)", "matchesCanonicalSources", "CpuMemoryQuotaScope",
                      "shareOriginal", "backendVulkanDescriptorElementResource", "backendVulkanTextureViewParentPayload",
                      "backendVulkanOriginalTextureViewDesc",
                      "SharedOwnerProjection::sameOwner", "SharedIdentityProjection::inspect", "cpuOwnerCount",
                      "FaultReadback", "syntheticRuntime()"]:
            self.assertIn(token, source)
        self.assertIn("nativeExecutionProven() const noexcept { return false; }", header)
        for forbidden in ["MutationJournal", "applyOriginalPlan", "commitOriginal",
                          "vkQueueSubmit", "vkCmd", "std::make_shared", "std::vector", "new "]:
            self.assertNotIn(forbidden, source)
        self.assertNotIn("backendVulkanTextureViewDesc()", source)
        descriptors = self.read_source("src/rhi/backends/vulkan/vulkan_descriptors.cpp")
        self.assertIn("backendVulkanOriginalTextureViewDesc()", descriptors)
        self.assertNotIn("backendVulkanTextureViewDesc()", descriptors)
        state = self.read_source("src/rhi/frontend/command_state.h")
        self.assertNotIn("friend class arp::rhi_vulkan::VulkanBindlessRootNativeClosure", state)

    def test_completed_texture_root_fence_preserves_original_pending_content(self):
        native = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_command.cpp")
        for token in ["lifecycle == 8u", "completed->controlVersion()", "validateCompletedTextureReadPending(object, *completed, graphicsDepth?8u:lifecycleVersion()==9u?(seal->originalRoot()->originalBindlessCompletedReadContractVersion()==2u?7u:5u):4u)",
                      "recordValidatedOriginalImageBarrier", "RHI-VULKAN-TEXTURE-FENCE-BARRIER-CLOSURE",
                      "RHI-VULKAN-TEXTURE-FENCE-SHADER-DIMENSION", "RHI-VULKAN-TEXTURE-FENCE-COMPARISON-SAMPLER",
                      "commitOriginalRootSubmission()"]:
            self.assertIn(token, native)
        for forbidden in ["recordTextureUpload", "createTexture", "MutationJournal", "completeOriginalPendingOwnership"]:
            self.assertNotIn(forbidden, native)
        self.assertLess(native.index("case K::TextureBarrier"), native.index("acquireOriginalFaultCustody"))

    def test_completed_texture_root_completion_profile_is_isolated(self):
        lifecycle = self.read_source("src/rhi/frontend/command_recording.h")
        model = self.read_source("src/rhi/frontend/command_model.h")
        for token in ["budget.contractVersion == 8u", "contentVersion == 8u ? nullptr",
                      "b.contractVersion == 8u ? 4u", "RHI-BINDLESS-COMPLETED-TEXTURE-PENDING-LIFECYCLE"]:
            self.assertIn(token, lifecycle)
        self.assertIn("submissionVersion == 5u && bindlessRoot && !wholeGeneral && !finiteGeneral", model)
        self.assertIn("RHI-BINDLESS-COMPLETED-TEXTURE-COMPLETION-PROFILE", model)

    def test_root_completion_uses_sealed_lifecycle_and_authentic_drain(self):
        seal = self.read_source("src/rhi/frontend/command_recording.h")
        fence = seal.split("const char* commitOriginalRootSubmission()", 1)[1].split(
            "// Deferred canonical", 1)[0]
        for token in ["validateRetainedOwnership()",
                      "OriginalBindlessRootCompletionProfile::inspect(budget_.contractVersion)",
                      "if (profile.callbacks)",
                      "commitOriginalSubmission(profile.submissionVersion)"]:
            self.assertIn(token, fence)
        callback = seal.split("const char* commitOriginalRootReadbackSubmission(", 1)[1].split(
            "const char* validateRetainedOwnership()", 1)[0]
        self.assertLess(callback.index("Drain::inspect(required, facts)"),
                        callback.index("OriginalBindlessRootCompletionProfile::inspect"))
        self.assertIn("if (!profile.callbacks)", callback)
        metal = self.read_source("src/rhi/backends/metal/metal_native_commands.mm").split(
            "bool MetalOriginalBindlessCommand::nativeCompleted", 1)[1].split(
            "prepareMetalRhiOrdinaryComputePush", 1)[0]
        self.assertIn("seal->commitOriginalRootSubmission()", metal)
        self.assertNotIn("finalClosure_", metal)
        self.assertNotIn("nextClosure_", metal)

    def test_root_fence_execution_uses_original_seal_and_authentic_native_command(self):
        native = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_command.cpp")
        for token in ["Closure::validateOriginal(closure)", "Reservation::validateOriginal(reservation)",
                      "OriginalBindlessRootReadbackSequence::inspectInput(binding, readbackInput)", "seal->applyOriginalRecordingPlan()",
                      "additionalFaults_.validatePending", "additionalFaults_.acquire",
                      "additionalFaults_.prepare", "nativeColdCommandClaimed_",
                      "syntheticRuntime()", "command_ != &command", "submitFenceCompleted",
                      "backendInvalidateMappedRead", "seal->commitOriginalRootSubmission()", "binding->originalResult()->published_ = true"]:
            self.assertIn(token, native)
        for token in ["BindlessFaultSubmissionLease::acquire(", "recordBindlessFaultLatchClearPreamble",
                      "makeShared<OriginalCommandStateTransaction>", "MutationJournal", "std::vector", "std::make_shared"]:
            self.assertNotIn(token, native)
        self.assertLess(native.index("seal->commitOriginalRootSubmission()"), native.index("binding->originalResult()->published_ = true"))
        sequence = self.read_source("src/rhi/frontend/bindless_root_readback_sequence.h")
        self.assertIn("Binding::validateOriginal(fault)", sequence)
        self.assertIn("OriginalCommandBufferReadbackResultContract::inspect(", sequence)
        self.assertLess(native.index("OriginalBindlessRootReadbackSequence::inspectInput(binding, readbackInput)"),
                        native.index("Binding::acquireOriginalFaultCustody"))
        state = self.read_source("src/rhi/frontend/command_state.h")
        self.assertNotIn("friend class arp::rhi_vulkan::VulkanOriginalBindlessCommand", state)
        snapshot = self.read_validation_section("src/rhi/frontend/command_sources.h", "original_command_readback_result_snapshot")
        self.assertIn("std::is_same_v<Request, BufferReadbackRequestDeclaration>", snapshot)
        self.assertIn("friend VulkanBufferWriter", snapshot)

    def test_native_result_loss_uses_closed_native_results_and_original_domain(self):
        contract = self.read_source("src/rhi/frontend/resource_device_native_loss.h")
        device = self.read_source("src/rhi/frontend/device_device.inl")
        runtime = self.read_source("src/rhi/backends/vulkan/vulkan_runtime.cpp")
        native = self.read_source("src/rhi/backends/vulkan/vulkan_native_commands.cpp")
        binding = self.read_source("src/rhi/backends/vulkan/vulkan_original_device_native_loss.cpp")
        self.assertIn("class OriginalDeviceNativeResultLossContract", contract)
        self.assertIn("maxSourceSubscriptions != sourceSubscriptionCount", contract)
        self.assertIn("prepareOriginalLossBinding(std::move(quota)", device)
        self.assertIn("std::weak_ptr<VulkanOriginalDeviceNativeResultLoss>", runtime)
        capture = runtime.split("void VulkanRhiRuntime::captureOriginalDeviceLoss", 1)[1].split("bool VulkanRhiRuntime::valid", 1)[0]
        self.assertIn("result != VK_ERROR_DEVICE_LOST", capture)
        self.assertLess(capture.index("originalDeviceLost = true"), capture.index("deliverNativeLoss()"))
        for token in ["captureOriginalDeviceLoss(status)", "captureOriginalDeviceLoss(waited)",
                      "captureOriginalDeviceLoss(submitted)", "captureOriginalDeviceLoss(result)"]:
            self.assertIn(token, native)
        header = self.read_source("src/rhi/backends/vulkan/vulkan_runtime.h")
        self.assertIn("captureOriginalDeviceLoss(VkResult)", header.split("private:", 1)[1])
        self.assertIn("receiver_.deliverNativeLoss()", binding)
        self.assertNotIn("OriginalCommandStateTransaction", binding)
        self.assertNotIn("commitOriginal", binding)
        self.assertNotIn("vkDeviceWaitIdle", binding)

    def test_root_fence_queue_invocation_retains_before_submit_and_quarantines_failure(self):
        native = self.read_source("src/rhi/backends/vulkan/vulkan_native_commands.cpp")
        submit = native.split("bool VulkanRhiNativeCommandPayload::submit(const VulkanRhiTimelineSubmitInfo&", 1)[1]
        self.assertLess(submit.index("retainSubmittedCommandForAsyncRetirement(shared_from_this(), diagnostics)",
                                    submit.index("beforeSubmit(*this, diagnostics)")),
                        submit.index("queueSubmit(impl_->device.graphicsQueue()"))
        for token in ["originalSubmissionUnproven = true", "submissionUnproven(*this)",
                      "RHI-VULKAN-ROOT-FENCE-QUEUE-QUARANTINED", "fenceCompleted(*this)"]:
            self.assertIn(token, native)

    def test_bindless_native_closure_has_explicit_four_backend_gate(self):
        lowering = self.read_source("src/rhi/semantics/command_lowering.h").split(
            "class OriginalBindlessRootNativeClosureLowering", 1)[1].split("class OriginalBindlessRootSourceLowering", 1)[0]
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::" + backend + ":", lowering)
        self.assertIn("RHI-BINDLESS-ROOT-NATIVE-CLOSURE-NULL-VALIDATOR-ONLY", lowering)
        metal = lowering.rsplit("case Backend::Metal:", 1)[1].split("case Backend::WebGPU:", 1)[0]
        self.assertIn("return {1u, CapabilityMode::Native, true, false, false, nullptr}", metal)
        self.assertIn("RHI-BINDLESS-ROOT-GEOMETRY-CLOSURE-METAL-UNAVAILABLE", lowering)
        webgpu = lowering.split("case Backend::WebGPU:", 1)[1]
        self.assertIn("return {1u, CapabilityMode::Equivalent, true, false, false, nullptr}", webgpu)
        self.assertIn("executionAvailable = false", lowering)

    def test_webgpu_original_root_preparation_preserves_execution_refusal(self):
        planner = self.read_source("src/rhi/semantics/bindless_lowering.cpp")
        for token in ["request.originalPreparation.contractVersion != 1u",
                      "OriginalBindlessPreparationLowering::capabilityAvailable", "!request.originalPreparation.prototypeOptIn",
                      "graphicsPreparation ? ShaderStage::Fragment : ShaderStage::Compute",
                      "binding.stages != (graphicsPreparation ? ShaderStage::Fragment : ShaderStage::Compute)", "result.executable = !preparationOnly",
                      "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE"]:
            self.assertIn(token, planner)
        device = self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("caps_.execution = rhi::BindlessExecution::DiagnosticFailure", device)
        self.assertIn("caps_.executablePayload = false", device)
        snapshot = self.read_source("src/rhi/frontend/bindless_snapshot.cpp")
        self.assertIn("RHI-BINDLESS-ORIGINAL-PREPARATION-DESCRIPTORS-REQUIRED", snapshot)
        prepared = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_preparation.cpp")
        hot = prepared.split("rhi::ValidationResult validatePayload() const override", 1)[1]
        self.assertNotIn("wgpuBufferGetSize", hot)
        closure = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root.cpp")
        for forbidden in ["wgpuQueueSubmit", "wgpuComputePassEncoderDispatch", "wgpuBufferGetSize",
                          "OriginalCommandStateTransaction::prepare", "MutationJournal"]:
            self.assertNotIn(forbidden, closure)
        self.assertIn("SharedIdentityProjection::inspect", closure)
        self.assertIn("matchesCanonicalSources(*inventory)", closure)
        self.assertIn("OriginalBufferRole::FaultReadback", closure)

    def test_webgpu_graphics_preparation_is_independent_cold_and_uses_shared_physical_correspondence(self):
        lowering = self.read_source("src/rhi/semantics/bindless_original_preparation_lowering.h")
        for token in ("version == 1u && caps.originalPreparationContractVersion == 1u",
                      "version == 2u && caps.originalGraphicsPreparationContractVersion == 2u",
                      "!plan.executable", "!plan.originalExecutionContractVersion"):
            self.assertIn(token, lowering)
        planner = self.read_source("src/rhi/semantics/bindless_lowering.cpp")
        for token in ("append(caps.originalGraphicsPreparationContractVersion)",
                      "request.originalPreparation.contractVersion == 2u",
                      "binding.stages != (graphicsPreparation ? ShaderStage::Fragment : ShaderStage::Compute)",
                      "result.originalPreparationContractVersion = preparationOnly ? request.originalPreparation.contractVersion : 0u"):
            self.assertIn(token, planner)
        prepared = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_preparation.cpp")
        self.assertEqual(prepared.count("initializeWebGpuNativePhysicalDescriptorGroup("), 1)
        self.assertEqual(prepared.count("class WebGpuOriginalPreparedBinding final"), 1)
        self.assertIn("return prepareBinding<WebGpuOriginalPreparedGraphics>", prepared)
        self.assertIn("return prepareBinding<WebGpuOriginalPreparedCompute>", prepared)
        self.assertIn("originalFactoryOwnerMatches(binding)", prepared)
        self.assertIn("observeSuccessfulBindingWrite(", prepared)
        self.assertIn("OriginalDescriptorBufferBindingContract::validateFiniteInterval(", prepared)
        self.assertIn("RHI-WEBGPU-BINDLESS-PREPARED-PUBLICATION-MISMATCH", prepared)
        self.assertIn("entry.offset = range.offsetBytes; entry.size = range.sizeBytes", prepared)
        self.assertIn("authorization.package().artifact()", prepared)
        self.assertIn("std::max(minimum, table.bufferLayout.minBindingSizeBytes)", prepared)
        for forbidden in ("wgpuQueueSubmit", "wgpuRenderPassEncoderDraw", "publishOriginal", "StateTransaction::prepare"):
            self.assertNotIn(forbidden, prepared)
        fixture = self.read_source("tests/fixtures/webgpu_original_bindless_root_preparation_fixture.h")
        self.assertIn("observeRoot<graphics>(root, inventory, device)", fixture)
        self.assertIn("RHI-BINDLESS-ORIGINAL-EXECUTION-SCOPE", fixture)
        self.assertIn("!extension->capabilities().originalGraphicsExecutionContractVersion", fixture)
        runner = self.read_source("tests/fixtures/bindless_webgpu_original_root_runner.html")
        self.assertIn("numeric&&sampledPrepared&&completedPrepared&&completedPublished&&completedContent&&graphicsPrepared&&canonicalColorPublished&&canonicalColorPlanned&&ordinaryColorClosed&&ordinaryColorExecuted&&ordinaryColorNumeric&&graphicsExecuted&&graphicsNumeric&&rangesExecuted&&rangesNumeric&&storageExecuted&&storageNumeric&&storageRetired))))&&diagnostics.length===0", runner)
        self.assertIn("arrayOnly?(arrayExecuted&&arrayNumeric&&arrayOrdinary&&arrayRetired)", runner)
        self.assertIn("sampledExecuted&&sampledNumeric&&sampledOrdinary", runner)
        self.assertIn("originalBindlessRootArrayOrdinaryPassed:arrayOrdinary", runner)
        self.assertIn("originalBindlessRootSampledOrdinaryPassed:sampledOrdinary", runner)
        build = self.read_source("CMakeLists.txt")
        self.assertIn("APPEND PROPERTY LINK_DEPENDS\n        \"${ARP_BINDLESS_WEBGPU_ROOT_PREPARATION_PUBLICATION}\" ${ARP_BINDLESS_WEBGPU_ROOT_GRAPHICS_PUBLICATIONS}", build)

    def test_root_public_execution_reuses_original_issuer_without_native_surface(self):
        interface = self.read_source("src/rhi/frontend/bindless_root_execution.h")
        for token in ["OriginalBindlessRootExecutionInputs", "OriginalCommandResourceSources",
                      "OriginalCommandStateTransaction", "OriginalCommandBufferReadbackBinding",
                      "hasQuotaBoundSharedOwner(self)", "recordExecution(request)", "submitExecution(request)",
                      "lifecycle.contractVersion != Execution::lifecycleVersion(intent.contractVersion)", "command.contractVersion != 2u",
                      "admission.contractVersion != Execution::admissionVersion(intent.contractVersion)",
                      "command.program.contractVersion != Execution::admissionVersion(intent.contractVersion)",
                      "closure.contractVersion != Execution::closureVersion(intent.contractVersion)"]:
            self.assertIn(token, interface)
        for forbidden in ["rhi/backends/", "void*", "WGPU", "Vk", "deliver(", "makeShared", "makeUnique"]:
            self.assertNotIn(forbidden, interface)
        frontend = self.read_source("src/rhi/frontend/bindless_compute_binding.cpp")
        for token in ["budget.validate()", "OriginalBindlessExecutionLowering::lowering",
                      "device.queryInterface(kExtensionBindlessCross) != this", "capabilityAvailable",
                      "faultReadbackContractVersion", "validateProfile(", "prepareOriginalRootExecutionPayload"]:
            self.assertIn(token, frontend)
        backend = self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp").split(
            "prepareOriginalRootExecutionPayload(", 1)[1].split("processorCompletion()", 1)[0]
        for token in ["inputs.transaction", "inputs.inventory", "inputs.readback",
                      "OriginalCommandRecordingSeal::prepareOriginal", "WebGpuBindlessRootIssuerPreparation::prepareOriginal",
                      "return issuers;"]:
            self.assertIn(token, backend)
        for forbidden in ["CompletionRetentionPool", "wgpuQueueSubmit", "deliver(", "StateTransaction::prepare"]:
            self.assertNotIn(forbidden, backend)
        fixture = self.read_source("tests/fixtures/webgpu_original_bindless_root_execution_fixture.h")
        for token in ["extension->prepareOriginalRootExecution", "OriginalBindlessRootExecution",
                      "Issuer::record(", "Issuer::submit(", "public-bindless-cross", "forged.reset()"]:
            self.assertIn(token, fixture)

    def test_root_execution_profile_requires_independent_authority_and_native_completion(self):
        planner = self.read_source("src/rhi/semantics/bindless_lowering.cpp")
        for token in ["a.originalExecutionContractVersion == b.originalExecutionContractVersion",
                      'hash.appendString("OriginalBindlessExecution/1")', "append(caps.originalExecutionContractVersion)",
                      "RHI-BINDLESS-ORIGINAL-PROFILE-CONFLICT", "OriginalBindlessExecutionLowering::capabilityAvailable",
                      "result.originalExecutionContractVersion = originalExecution ? request.originalExecution.contractVersion : 0u",
                      "append(caps.originalFragmentGraphicsExecutionContractVersion)"]:
            self.assertIn(token, planner)
        lowering = self.read_source("src/rhi/semantics/bindless_original_execution_lowering.h")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::" + backend + ":", lowering)
        self.assertIn("case Backend::WebGPU: return {true, true, nullptr}", lowering)
        for path in ["null/null_backend.cpp", "vulkan/vulkan_native_pipelines.cpp",
                     "metal/metal_device.cpp", "webgpu/webgpu_device.cpp"]:
            backend = self.read_source("src/rhi/backends/" + path)
            self.assertIn("if (plan.originalExecutionContractVersion)", backend)
            self.assertIn("OriginalBindlessExecutionLowering::lowering", backend)
            if path.startswith("webgpu/"):
                self.assertIn("caps_.originalExecutionContractVersion = 1u", backend)
            else:
                self.assertNotIn("caps_.originalExecutionContractVersion = 1u", backend)
        snapshot = self.read_source("src/rhi/frontend/bindless_snapshot.cpp")
        self.assertIn("RHI-BINDLESS-ORIGINAL-EXECUTION-DESCRIPTORS-REQUIRED", snapshot)
        device = self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        self.assertIn("!lowering.executionAvailable || !lowering.completionAvailable", device)
        self.assertIn("plan.originalPreparationContractVersion", device)

    def test_root_callback_cold_reuses_canonical_custody_and_keeps_execution_closed(self):
        cold = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root_cold_admission.h")
        for token in ["OriginalCommandLifecycleReservation", "OriginalCommandBufferReadbackBinding",
                      "OriginalCommandBufferReadbackResultContract::inspect",
                      "OriginalCommandReadbackMapContract::deriveRequirements", "Closure::validateOriginal(closure)",
                      "sameOwner(sources, closure->originalInventory())"]:
            self.assertIn(token, cold)
        for forbidden in ["wgpuQueueSubmit", "wgpuBufferMapAsync", "wgpuComputePassEncoderDispatch",
                          "StateTransaction::prepare", "MutationJournal", "commitOriginal", "publishOriginal"]:
            self.assertNotIn(forbidden, cold)
        self.assertIn("nativeExecutionProven() const noexcept { return false; }", cold)
        lifecycle = self.read_source("src/rhi/semantics/command_lowering.h").split(
            "class OriginalCommandNativeLifecycleLowering", 1)[1].split("if (version == 9u)", 1)[0]
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::" + backend + ":", lifecycle)
        cold_profile = lifecycle.split("if (version == 10u)", 1)[1]
        self.assertEqual(cold_profile.count("false, false,"), 4)

    def test_fragment_graphics_execution_requires_typed_attachment_content_and_real_public_completion(self):
        lowering = self.read_source("src/rhi/semantics/bindless_original_execution_lowering.h")
        for token in ["version == 2u", "originalFragmentGraphicsExecutionContractVersion", "originalExecutionContractVersion"]:
            self.assertIn(token, lowering)
        storage = self.read_source("src/rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h")
        for token in ["OriginalBindlessRootCallbackColdContract::matchesCompletionProfile(",
                      "seal->originalBudget().contractVersion", "self->bufferAdmission_->originalBudget().contractVersion",
                      "self->originalRoot()->originalContentPlanContractVersion()"]:
            self.assertIn(token, storage)
        callback = self.read_source("src/rhi/frontend/bindless_root_callback.h")
        for token in ["lifecycle==12u && admission==2u && content==9u",
                      "lifecycle==16u && admission==4u && content==14u"]:
            self.assertIn(token, callback)
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for token in ["wgpuCommandEncoderBeginRenderPass", "wgpuRenderPassEncoderDraw",
                      "pass.finalize(", ".matches(resolveView, passDraws)", "commitOriginalRootReadbackSubmission"]:
            self.assertIn(token, native)
        fixture = self.read_source("tests/fixtures/original_bindless_graphics_stream_fixture.h")
        for token in ["prepareRetained", "prepareOriginalBindlessGraphicsOwnership", "{9u,true",
                      "OriginalCommandBufferReadbackBinding::prepareOriginal", "observe(root,inventory,binding,tx,device)"]:
            self.assertIn(token, fixture)
        runner = self.read_source("tests/fixtures/bindless_webgpu_original_root_runner.html")
        for token in ["length:11", "graphicsExecuted&&graphicsNumeric", "originalBindlessRootGraphicsExecutionPassed",
                      "publicBindlessExecutionAvailable:false"]:
            self.assertIn(token, runner)

    def test_root_callback_cold_and_real_pool_share_native_slot_policy(self):
        layout = self.read_source("src/rhi/backends/webgpu/webgpu_canonical_callback_layout.h")
        cold = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root_cold_admission.h")
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for field in ["recordingFirst", "submissionFirst", "queueIndex", "lossIndex", "slotCount"]:
            self.assertIn("WebGpuCanonicalCallbackLayout::" + field, native)
            self.assertIn(field, layout)
        self.assertIn("WebGpuCanonicalCallbackLayout::slotCount", cold)
        self.assertIn("WebGpuCanonicalCallbackLayout::registrationCount", cold)
        self.assertIn("OriginalDeviceNativeLossContract::callbackCount", layout)

    def test_root_buffer_result_storage_keeps_original_snapshot_and_native_receipt(self):
        storage = self.read_source("src/rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h")
        body = storage.split("prepareOriginalBuffer(", 1)[1].split("prepareOriginal(", 1)[0]
        self.assertIn("matchesOriginalSource(lease, device)", body)
        self.assertNotIn("prepareOriginalCold", body)
        self.assertNotIn("bytes_.resize", body)
        for token in ["entry.snapshot = binding->originalResult()",
                      "sameOwner(entry.snapshot, binding->originalResult())", "sameOwner(entry.buffer, payload)",
                      "entry.handle != entry.buffer->buffer", "entry.device != entry.buffer->ownerDevice"]:
            self.assertIn(token, storage)
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        receipt = native.split("WebGpuCanonicalReadbackResultStorage::validateBufferMapReceipt(", 1)[1].split(
            "WebGpuCanonicalReadbackResultStorage::readOriginalUnpublished(", 1)[0]
        self.assertIn("receipt.validateRetainedRange(entry.handle,entry.mapRange)", receipt)
        self.assertNotIn("receipt.start()", receipt)
        self.assertIn("commitOriginalRootReadbackSubmission", native)
        self.assertIn("validateOriginalPending", native)
        self.assertIn("receipt.readAndUnmap(snapshot->bytes_.data()", native)
        self.assertIn("entry.snapshot->published_ = true", native)
        lowering = self.read_source("src/rhi/semantics/command_lowering.h").split(
            "static constexpr Decision bufferLowering(", 1)[1].split("// endregion original_command_readback_result_storage", 1)[0]
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::" + backend + ":", lowering)
        self.assertIn("case Backend::WebGPU: return {true, false, false,", lowering)

    def test_root_command_shell_claims_same_seal_after_all_fallible_preparation(self):
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        prepare = native.split("WebGpuNativeCommandPreparation::prepareBindlessRoot(", 1)[1].split(
            "WebGpuNativeCommandPreparation::validateBindlessRootUnclaimed(", 1)[0]
        for token in ["inspectBindlessRootColdCommand(admission, results, request)",
                      "command->impl_->canonicalColdAdmission = admission", "pipeline->issuedContract()->nativeDevice()",
                      "validateBindlessRootUnclaimed(*result)", "seal->nativeColdCommandClaimed_ = true"]:
            self.assertIn(token, prepare)
        self.assertLess(prepare.index("validateBindlessRootUnclaimed(*result)"),
                        prepare.index("seal->nativeColdCommandClaimed_ = true"))
        for forbidden in ["wgpuDeviceCreateCommandEncoder", "wgpuQueueSubmit", "wgpuComputePassEncoderDispatch",
                          "applyOriginalRecordingPlan", "StateTransaction::prepare"]:
            self.assertNotIn(forbidden, prepare)
        validate = native.split("WebGpuNativeCommandPreparation::validateBindlessRootUnclaimed(", 1)[1].split(
            "WebGpuNativeCommandPreparation::validateBindlessRoot(", 1)[0]
        self.assertIn("inspectShellIdentity(original.command_", validate)
        self.assertIn("native.encoder || native.commandBuffer || native.computePass || native.renderPass", validate)

    def test_root_native_program_queries_device_limits_only_during_cold_preparation(self):
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        prepare = native.split("WebGpuNativeCommandPreparation::prepareBindlessRootProgram(", 1)[1].split(
            "WebGpuNativeCommandPreparation::validateBindlessRootProgram(", 1)[0]
        self.assertIn("wgpuDeviceGetLimits", prepare)
        self.assertIn("limits.maxComputeWorkgroupsPerDimension", prepare)
        inspect = native.split("WebGpuNativeCommandPreparation::inspectBindlessRootProgram(", 1)[1].split(
            "WebGpuCanonicalReadbackResultStorage::validateBufferMapReceipt(", 1)[0]
        self.assertIn("Contract::validateDispatch", inspect)
        self.assertIn("request.maxOperationVisits", inspect)
        self.assertIn("Contract::validateCounts", inspect)
        self.assertIn("Contract::validateOperation(op.kind, request)", inspect)
        self.assertIn("semantics::CommandRecordingOrder order", inspect)
        for forbidden in ["wgpuDeviceGetLimits", "wgpuDeviceCreateCommandEncoder", "wgpuQueueSubmit",
                          "wgpuComputePassEncoderDispatch", "wgpuBufferMapAsync", "core::makeShared", "core::makeUnique"]:
            self.assertNotIn(forbidden, inspect)

    def test_root_native_program_borrows_from_original_closure_without_replacing_ownership(self):
        program = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root_native_program.h")
        self.assertIn("core::BoundedSequence<Record>", program)
        self.assertIn("OriginalCommandStreamProgram::Operation original", program)
        # Existing resources remain borrowed from the authentic closure. Only
        # the explicitly budgeted push upload payload is owned by this program.
        self.assertEqual(program.count("std::shared_ptr<"), 1)
        self.assertIn("std::shared_ptr<WebGpuNativeBufferPayload> pushBuffer_", program)
        self.assertIn("pushNativeObjects = 3u, pushRetainedOwners = 2u", program)
        self.assertIn("wgpuBindGroupRelease(pushGroup_)", program)
        self.assertIn("wgpuBindGroupLayoutRelease(pushLayout_)", program)
        shell = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root_cold_command.h")
        self.assertIn("std::optional<WebGpuBindlessRootNativeProgram>", shell)
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        prepare = native.split("WebGpuNativeCommandPreparation::prepareBindlessRoot(", 1)[1].split(
            "WebGpuNativeCommandPreparation::validateBindlessRootUnclaimed(", 1)[0]
        self.assertLess(prepare.index("prepareBindlessRootProgram(*candidate)"),
                        prepare.index("seal->nativeColdCommandClaimed_ = true"))
        lowering = self.read_source("src/rhi/semantics/bindless_root_program_lowering.h")
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::" + backend + ":", lowering)
        self.assertIn("case Backend::WebGPU: return {true, false, false, nullptr}", lowering)

    def test_root_push_upload_preflights_before_device_allocation_and_seal_claim(self):
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        prepare = native.split("WebGpuNativeCommandPreparation::prepareBindlessRoot(", 1)[1].split(
            "WebGpuNativeCommandPreparation::validateBindlessRootUnclaimed(", 1)[0]
        self.assertLess(prepare.index("prepareBindlessRootProgram(*candidate)"),
                        prepare.index("prepareBindlessRootPushUpload(*candidate, allocator)"))
        self.assertLess(prepare.index("prepareBindlessRootPushUpload(*candidate, allocator)"),
                        prepare.index("seal->nativeColdCommandClaimed_ = true"))
        upload = native.split("WebGpuNativeCommandPreparation::prepareBindlessRootPushUpload(", 1)[1].split(
            "WebGpuNativeCommandPreparation::validateBindlessRootDepthPending(", 1)[0]
        for token in ["allocator->allocate", "program.pushResidentBytes_", "allocation.releaseToken",
                      "program.pushBuffer_->allocationReleaseToken = allocation.releaseToken",
                      "wgpuRenderPipelineGetBindGroupLayout", "wgpuDeviceCreateBindGroup",
                      "entry.size = program.pushBindingBytes_"]:
            self.assertIn(token, upload)
        inspect = native.split("WebGpuNativeCommandPreparation::inspectBindlessRootProgram(", 1)[1].split(
            "WebGpuCanonicalReadbackResultStorage::validateBufferMapReceipt(", 1)[0]
        for token in ["OriginalCommandPushConsumption pushConsumer", "OriginalCommandPushUploadPlan upload",
                      "pushConsumer.update", "pushConsumer.consume", "upload.append(",
                      "upload.validateComplete", "frozen.pushBuffer_->allocationReleaseToken",
                      "RHI-WEBGPU-ROOT-PUSH-UPLOAD-FROZEN-RANGE"]:
            self.assertIn(token, inspect)
        runtime = self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.h")
        module = runtime.split("createWebGpuNativeBindlessShaderModulePayload(", 1)[1].split(";", 1)[0]
        self.assertIn("bool explicitPush = false", module)
        resources = self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        module = resources.split("createWebGpuNativeBindlessShaderModulePayload(", 1)[1].split(
            "createWebGpuNativeBindlessComputePipelinePayload(", 1)[0]
        self.assertIn("package, entry, stage, explicitPush", module)
        for stage, entry in [("Vertex", "vertexEntry"), ("Fragment", "fragmentEntry")]:
            self.assertIn("package, " + entry + ", rhi::ShaderStage::" + stage + ", diagnostic, bool(push)", resources)
        preparation = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_preparation.cpp")
        self.assertIn("mapping->webGpuGroup == issued.semantics().pushGroup", preparation)
        self.assertIn("issued.semantics().pushGroup >= issued.semantics().groupCount", preparation)
        self.assertIn("issued.semantics().pushBinding == UINT32_MAX", preparation)
        # Frozen uniform bytes are initialized synchronously during cold
        # preparation. Recording must not enqueue an untracked writeBuffer.
        upload = native.split("WebGpuNativeCommandPreparation::prepareBindlessRootPushUpload(", 1)[1].split(
            "WebGpuNativeCommandPreparation::validateBindlessRootDepthPending(", 1)[0]
        self.assertIn("WebGpuNativeBufferInitializer initializer", upload)
        self.assertIn("record.pushSlice.dynamicOffset", upload)
        self.assertIn("record.push.data", upload)
        emit = native.split("WebGpuOrdinaryRecording::emitRoot(", 1)[1].split(
            "WebGpuOrdinaryRecording::", 1)[0]
        self.assertNotIn("wgpuQueueWriteBuffer", emit)
        self.assertIn("wgpuRenderPassEncoderSetBindGroup", emit)
        device = self.read_source("src/rhi/backends/webgpu/webgpu_device.cpp")
        execution = device.split("prepareOriginalRootExecutionPayload(", 1)[1].split(
            "createComputePipeline(", 1)[0]
        self.assertLess(execution.index("preflightBindlessRootPushUpload(inputs, budget)"),
                        execution.index("OriginalCommandRecordingSeal::prepareOriginal"))

    def test_root_callback_custody_reuses_original_runtime_pool_and_retains_native_receipts(self):
        wrapper = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root_callback_custody.h")
        self.assertIn("WebGpuCanonicalCallbackCustody::RuntimeState<WebGpuBindlessRootColdCommand>", wrapper)
        self.assertNotIn("CompletionRetentionPool<", wrapper)
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        prepare = native.split("WebGpuCanonicalCallbackCustody::prepareRootOriginal(", 1)[1].split(
            "WebGpuCanonicalCallbackCustody::validateRootOriginal(", 1)[0]
        for token in ["RuntimeState<WebGpuBindlessRootColdCommand>", "state->pool.reserve(graph)",
                      "state->prepareOriginalMapReceipts(admission", "command->originalResultStorage()",
                      "prepareWebGpuNativeDeviceLossRegistration", "&RootState::consumeNativeLoss",
                      "command->callbackCustodyClaimed_ = true"]:
            self.assertIn(token, prepare)
        self.assertLess(prepare.index("core::makeUnique<WebGpuBindlessRootCallbackCustody>"),
                        prepare.index("prepareWebGpuNativeDeviceLossRegistration"))
        self.assertLess(prepare.index("prepareWebGpuNativeDeviceLossRegistration"),
                        prepare.index("command->callbackCustodyClaimed_ = true"))
        for forbidden in ["wgpuDeviceCreateCommandEncoder", "wgpuQueueSubmit", ".map.start()",
                          "applyOriginalRecordingPlan", "StateTransaction::prepare", "prepareOriginalCold"]:
            self.assertNotIn(forbidden, prepare)
        self.assertIn("results->snapshotCount()", native)
        self.assertIn("results->originalBufferAdmission(), joins", native)
        self.assertIn("nextMap == results->snapshotCount()", native)
        lowering = self.read_source("src/rhi/semantics/command_lowering.h").split(
            "class OriginalBindlessRootCallbackCustodyLowering final", 1)[1].split(
            "class OriginalCommandNativeLifecycleLowering final", 1)[0]
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::" + backend + ":", lowering)
        self.assertIn("case Backend::WebGPU: return {true, false, nullptr}", lowering)

    def test_root_issuers_join_existing_recording_submission_without_publishing_early(self):
        source = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        prepare = source.split("WebGpuCanonicalCallbackCustody::prepareRootIssuers(", 1)[1].split(
            "WebGpuCanonicalCallbackCustody::validateRootIssuers(", 1)[0]
        for token in ["inspectRootIssuers(custody, request, owners)", "allocateRoot(command.command_, custody->state_)",
                      "allocateRoot(recording, custody->state_)", "WebGpuNativeCompletionReceipt::prepare",
                      "receipt.matches(command.device_", "receipts[i].release()", "state.issuersClaimed = true"]:
            self.assertIn(token, prepare)
        self.assertLess(prepare.index("auto result = Wrapper::shareOriginal"), prepare.index("WebGpuNativeCompletionReceipt::prepare"))
        self.assertLess(prepare.index("receipts[i].release()"), prepare.index("state.issuersClaimed = true"))
        for forbidden in ["CompletionRetentionPool", "gate.arm()", "::bind(", "wgpuQueueSubmit",
                          "wgpuDeviceCreateCommandEncoder", "applyOriginalRecordingPlan", "pool.reserve("]:
            self.assertNotIn(forbidden, prepare)
        for issuer in ["Recording", "Submission"]:
            self.assertIn("completePrepared<RootState>(userdata, outcome, &completeRoot)", source)
            self.assertIn("completePrepared<TypedState>(userdata, outcome, &completeTyped)", source)
            callback = source.split("WebGpuOrdinary" + issuer + "::completePrepared(", 1)[1].split(
                "WebGpuOrdinary" + issuer + "::completeTyped(", 1)[0]
            for token in ["matchesDelivery", "slot->gate.beginDelivery()", "slot->gate.finishDelivery()", "slot->owner.reset()", "receiver,"]:
                self.assertIn(token, callback)
        lowering = self.read_source("src/rhi/semantics/command_lowering.h").split(
            "class OriginalBindlessRootIssuerLowering final", 1)[1].split("class OriginalCommandNativeLifecycleLowering final", 1)[0]
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::" + backend + ":", lowering)
        self.assertIn("case Backend::WebGPU: return {true, false, false, nullptr}", lowering)
        header = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root_issuer_preparation.h")
        public = header.split("using Request =", 1)[1].split("private:", 1)[0]
        for forbidden in ["State&", "nativeDevice", "nativeQueue", "deliver(", "record(", "submit("]:
            self.assertNotIn(forbidden, public)

    def test_completed_texture_descriptor_publication_keeps_ordinary_gate_and_authentic_provenance(self):
        source = self.read_source("src/rhi/frontend/descriptor_completed_texture.cpp")
        for token in ["OriginalCommandTextureViewSource::validateOriginal(source)",
                      "texture.validateOriginalContent(content)", "validateControl(content.controlVersion(), publicationVersion)",
                      "State::Completed", "matchesParent(texture.originalResourceRegistration())",
                      "device.validateOriginalResourceRegistration(registration)",
                      "if (result.success) commitContentWrite()"]:
            self.assertIn(token, source)
        backend = self.read_source("src/rhi/backends/vulkan/vulkan_descriptors.cpp")
        ordinary = backend.split("rhi::ValidationResult validateRegisteredDescriptorView(", 1)[1].split("struct VulkanCompletedTexturePublication", 1)[0]
        self.assertIn("backendVulkanOriginalContentExclusive()", ordinary)
        self.assertIn("RHI-TEXTURE-CONTENT-EXCLUSIVE-USE", ordinary)
        for token in ["validateCompletedTexture", "completedTextures(bindingCount",
                      "publication.source", "runtime->syntheticRuntime()",
                      "sameOwner(view, registered)", "view->backendVulkanTextureViewParentPayload()"]:
            self.assertIn(token, backend)

    def test_completed_texture_descriptor_has_four_backend_scope_without_execution_grant(self):
        lowering = self.read_source("src/rhi/semantics/descriptor_lowering.h").split(
            "OriginalCompletedTextureDescriptor_lowering", 1)[1].split("template <class Decision>", 1)[0]
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::" + backend + ":", lowering)
        for token in ["{true, false, false,", "{false, true, false, nullptr}",
                      "DESCRIPTOR-METAL-PROFILE", "DESCRIPTOR-WEBGPU-MISSING"]:
            self.assertIn(token, lowering)
        snapshot = self.read_source("src/rhi/frontend/bindless_snapshot.cpp")
        self.assertIn("RHI-COMPLETED-TEXTURE-DESCRIPTOR-PADDING-DEVICE", snapshot)
        self.assertIn("alias.content.emplace(*publication.content)", snapshot)

    def test_webgpu_completed_texture_publication2_preserves_original_proof_and_atomic_metadata(self):
        proof = self.read_source("src/rhi/backends/webgpu/webgpu_completed_texture_publication.h")
        for token in ["validateSource(", "*publication.content, publication.colorPublicationVersion", "observeOriginalWebGpuResource(view, registered)",
                      "sameOwner(binding.textureView, registered.view())", "sameOwner(parent, registered.backing().texture)",
                      "matchesDescription(view.desc())", "validateOriginalUpload(*parent, texture.desc())"]:
            self.assertIn(token, proof)
        backend = self.read_source("src/rhi/backends/webgpu/webgpu_backend_descriptors.cpp")
        # The typed metadata dispatcher must preserve the completed proof for
        # old publications; cell identity is independently scoped and cannot
        # enter legacy preparation or executable payload exporters.
        self.assertIn("return validateWebGpuCompletedTexturePublication(publication,binding)", proof)
        self.assertIn("publication.cellIdentity || depth == bool(publication.content)", proof)
        for token in ["writeOriginalCompletedTextureImpl", "validateWebGpuOriginalTexturePublication(*completed, candidate)",
                      "if (publication.cellIdentity || publication.colorPublicationVersion == 6u) return nullptr",
                      "candidateCompleted", "completedTextures_.swap(candidateCompleted)", '"RHI-OOM"']:
            self.assertIn(token, backend)
        runtime = self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.h")
        self.assertIn("colorPublicationVersion = 2u", runtime)
        self.assertIn("colorPublicationVersion = other.colorPublicationVersion", runtime)
        self.assertIn("publication.colorPublicationVersion != 2u", proof)
        self.assertIn("publication.colorPublicationVersion != 6u", proof)
        self.assertIn("publication.colorPublicationVersion = request.contractVersion", backend)
        metadata = self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        for token in ["if (!completed) return nullptr", "validateWebGpuOriginalTexturePublication(*found, *element)",
                      "(!allowCells && publication.cellIdentity)",
                      "createWebGpuNativeOriginalTextureCellDescriptorPayload",
                      "payload->completedTexturePublications = *completed"]:
            self.assertIn(token, metadata)

    def test_descriptor_native_loss_registration_is_separate_and_descriptor_only(self):
        contract = self.read_source("src/rhi/frontend/resource_loss_validation.h").split(
            "class OriginalDescriptorNativeLossRegistrationContract", 1)[1].split("} // namespace", 1)[0]
        for token in ["registrationVersion = 9u", "budget.maxPipelineLayouts", "budget.maxComputePipelines",
                      "budget.maxGraphicsPipelines", "budget.samplers.requested()", "budget.maxDescriptorTables"]:
            self.assertIn(token, contract)
        device = self.read_source("src/rhi/frontend/device_device.inl")
        prepare = device.split("GpuDevice::prepareOriginalDescriptorNativeLossRegistration(", 1)[1].split(
            "inline const char* GpuDevice::validateOriginalNativeLossDescriptor", 1)[0]
        for token in ["Contract::validate(request, budget)", "validateOriginalDeviceNativeLoss(loss)",
                      "loss.overlapsMetadataOwner(quota)", "originalLossBinding_"]:
            self.assertIn(token, prepare)
        self.assertIn("validateOriginalNativeLossDescriptor(*candidate)", device)
        lowering = self.read_source("src/rhi/semantics/resource_lowering.h").split(
            "OriginalDescriptorNativeLossRegistration_lowering", 1)[1].split("template <typename Fn>", 1)[0].split(
            "OriginalSamplerNativeLossJoin_lowering", 1)[0]
        for backend in ["Null", "Vulkan", "Metal", "WebGPU"]:
            self.assertIn("case Backend::" + backend + ":", lowering)
        self.assertIn("{false, true, false, nullptr}", lowering)

    def test_completed_webgpu_cold_preparation_retains_authenticated_physical_content(self):
        prepared = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_preparation.cpp")
        for token in ["validateWebGpuCompletedTexturePublication(*completed, *found)",
                      "observeOriginalWebGpuResource(*completed->source->originalObject(), registered)",
                      "physical->completedTexturePublications.push_back(*completed)",
                      "sameOwner(source->source, retained->source)",
                      "validateWebGpuCompletedTexturePublication(*retained, *physical)"]:
            self.assertIn(token, prepared)
        fixture = self.read_source("tests/fixtures/webgpu_original_completed_preparation_fixture.h")
        for token in ["OriginalCompletedSampledInputFixture::prepare(context,quotas)",
                      "prepareOriginalDescriptorNativeLossRegistration({3u,true}",
                      "completedTexturePublicationVersion() noexcept { return 2u; }"]:
            self.assertIn(token, fixture)

    def test_vulkan_descriptor_loss_join_checks_same_runtime_registry_and_real_set(self):
        backend = self.read_source("src/rhi/backends/vulkan/vulkan_descriptors.cpp")
        for token in ["sameOwner(runtime, nativeResources_.runtime())", "sameOwner(registry, resourceRegistry_)",
                      "requireNativeDescriptorAllocation_ && generation_.isActive()",
                      "nativePayload_->backendVulkanDescriptorSetHandle()",
                      "nativePayload_->backendVulkanDescriptorSetLayoutHandle()"]:
            self.assertIn(token, backend)
        integration = self.read_source("tests/fixtures/vulkan_original_texture_upload_fixture.h")
        self.assertIn("prepareOriginalDescriptorNativeLossRegistration({1u,true},registration,q[2],*loss)", integration)
        self.assertIn("writeOriginalCompletedTexture(*foreign,sampled,viewSource,*completed,publication)", integration)

    def test_descriptor_sampler_loss_join_preserves_subscription_and_legacy_gates(self):
        contract = self.read_source("src/rhi/frontend/resource_loss_validation.h")
        self.assertIn("request.contractVersion == 1u && budget.samplers.requested()", contract)
        self.assertIn("request.contractVersion == 2u && !budget.samplers.requested()", contract)
        self.assertIn("OriginalDescriptorNativeLossRegistrationContract::validate({2u, true}, budget)", contract)
        device = self.read_source("src/rhi/frontend/device_device.inl")
        sampler = device.split("using Loss = OriginalSamplerNativeLossJoinContract", 1)[1].split(
            "auto& object =", 1)[0]
        for token in ["Loss::validateRegistration(originalRegistry_->budget_, version)",
                      "Loss::lowering(backend(), version)", "validateOriginalTypedNativeLossJoin(*observed)",
                      "sameOwner(originalRegistry_->loss_.lock(), observed->binding_)",
                      "sameOwner(originalRegistry_->domain_, observed->domain_)"]:
            self.assertIn(token, sampler)
        backend = self.read_source("src/rhi/backends/vulkan/vulkan_device.cpp")
        native = backend.split("validateOriginalNativeLossResource(const rhi::GpuSampler&", 1)[1].split(
            "validateOriginalNativeLossResource(const rhi::GpuTexture&", 1)[0]
        for token in ["runtime->valid()", "!runtime->syntheticRuntime()", "backendVulkanSamplerHandle()",
                      "backendVulkanDeviceHandle() == runtime->logicalDevice()",
                      "sameOwner(payload, resourceRegistry_->payload(sampler.id()))"]:
            self.assertIn(token, native)
        for forbidden in ["makeShared", "makeUnique", "getDeviceProcAddr", "Callback"]:
            self.assertNotIn(forbidden, native)

    def test_bindless_texture_projection_preserves_the_exact_original_view_source(self):
        root = self.read_source("src/rhi/frontend/bindless_command_source.cpp")
        read = root.split("OriginalCommandBindlessRootSource::readOriginalDeclaration", 1)[1].split(
            "OriginalCommandBindlessRootSource::matchesCanonicalSources", 1)[0]
        self.assertIn("declaration = element.declaration.sourceIndex", read)
        for forbidden in ["makeShared", "makeUnique", "push_back", "getDeviceProcAddr"]:
            self.assertNotIn(forbidden, read)
        projection = self.read_source("src/rhi/frontend/command_texture_ranges.h")
        self.assertIn("root.readOriginalDeclaration(accessIndex, access, declaration)", projection)
        state = self.read_source("src/rhi/frontend/command_state.h")
        self.assertIn("access.viewSourceIndex, access.rootSourceIndex", state)
        self.assertIn("sameOwner(view->originalParentLease(), parent)", self.read_source(
            "tests/fixtures/original_bindless_root_source_fixture.h"))

    def test_completed_bindless_texture_plan_is_read_only_and_has_no_native_issuer(self):
        state = self.read_source("src/rhi/frontend/command_state.h")
        self.assertIn("hasOriginalBindlessRootShaderResourceProfile()", state)
        self.assertIn("sources(*graph_).assemblyVersion_ == 6u", state)
        self.assertIn("RHI-BINDLESS-COMPLETED-TEXTURE-READ-CONSUMPTION-MISSING", state)
        self.assertIn("p.origin == Projection::Point::Origin::BindlessRoot", state)
        self.assertIn("plan_->contentBudget.contractVersion", state.split(
            "if (completedContentIsReadOnly())", 1)[1])
        lifecycle = self.read_source("src/rhi/frontend/command_recording.h")
        self.assertIn("if (contentVersion == 8u)", lifecycle)
        self.assertIn("RHI-BINDLESS-COMPLETED-TEXTURE-NATIVE-LIFECYCLE-UNAVAILABLE", lifecycle)
        content = self.read_source("src/rhi/frontend/resource_texture_validation.h")
        self.assertIn("(planVersion == 8u || planVersion == 17u) && controlVersion == 3u", content)
        self.assertIn("pendingRevision == capturedRevision", content)

    def test_original_native_attachment_creation_preserves_legacy_execution_gates(self):
        contract = self.read_source("src/rhi/frontend/resource_texture_validation.h")
        self.assertIn("return version == 2u || version == 4u", contract)
        self.assertIn("return version == 3u || version == 4u", contract)
        device = self.read_source("src/rhi/frontend/device_device.inl")
        query = device.split("GpuDevice::queryOriginalContentTextureAllocation(", 1)[1].split(
            "GpuDevice::queryOriginalContentTextureAllocationPayload", 1)[0]
        for token in ["hasNativeAllocationBudget(creationVersion)", "inspectCellLayout(desc, UINT64_MAX, texels, creationVersion)",
                      "lowering(backend(), creationVersion)", "queryOriginalContentTextureAllocationPayload(desc, diagnostic)"]:
            self.assertIn(token, query)
        self.assertNotIn("return texels", query)
        control = self.read_source("src/rhi/frontend/resource_texture_content_control.h")
        for method, end in [("canProposeCells(", "proposeCell("), ("proposeCell(", "uniformRange("),
                            ("completeCell(", "const OriginalTextureContentCreationBudget")]:
            self.assertIn("cellControlMatchesPlan(budget_.contractVersion, planVersion)",
                          control.split(method, 1)[1].split(end, 1)[0])
        state = self.read_source("src/rhi/frontend/command_state.h")
        cells = state.split("ContentCellsInspection inspectRangeContentPlan(", 1)[1].split(
            "ContentCellsInspection inspectCompletedContentReadPlan(", 1)[0]
        self.assertIn("!OriginalTextureContentCreationContract::isBindlessCellCreation(control->contractVersion())", cells)
        self.assertIn("control->contractVersion() != (objectStorage ? OriginalStorageTextureContentPlanContract::controlVersion(objectPlan) "
                      ": graphicsDepth ? (b.contractVersion == 19u ? 2u : 4u) : objectDepthRead ? OriginalCompletedDepthRootReadPlanContract::controlVersion(objectPlan) : 2u)", cells)
        depth_contract = contract.split("class OriginalCompletedDepthRootReadPlanContract", 1)[1].split(
            "class OriginalUndefinedTextureUploadPlanContract", 1)[0]
        self.assertIn("v == version ? 2u : v == nativeVersion ? 4u : 0u", depth_contract)
        self.assertIn("static constexpr std::uint32_t nativeVersion = 12u", depth_contract)
        self.assertIn("lifecycleVersion == 13u && contentVersion == version", depth_contract)
        self.assertIn("!selected || b.contractVersion != 2u", cells)
        self.assertIn("geometryRoot?hasOriginalBindlessRootGeometryStateProfile()||hasOriginalBindlessProducedGeometryStateProfile():hasOriginalBindlessRootShaderResourceProfile()", cells)
        self.assertIn("bool geometryRoot = false", cells)
        root = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_command.cpp")
        self.assertIn("RHI-VULKAN-ROOT-FENCE-COMPUTE-ROOT", root)
        fixture = self.read_source("tests/fixtures/vulkan_original_texture_creation_fixture.h")
        self.assertIn("verifyAttachments(", fixture)
        self.assertIn("CpuQuotaFactoryFixture pressure(q[index])", fixture)
        self.assertIn("recordRenderPassBegin(pass,colors", fixture)
        self.assertIn('"RHI-TEXTURE-CONTENT-EXCLUSIVE-USE"', fixture)

    def test_canonical_depth_composition_reuses_original_control_index_and_readonly_gate(self):
        state = self.read_source("src/rhi/frontend/command_state.h")
        helper = state.split("const char* appendRangeCell(", 1)[1].split("enum class Status", 1)[0]
        for token in ["rangeContents.findIf", "c.object == &object", "rangeContents.append",
                      "control - rangeContents.values().data()", "++c.mutations",
                      "{controlIndex, index, initial, initial, timeline.stored, written}"]:
            self.assertIn(token, helper)
        for begin, end in [("const char* materializeOriginalGraphicsPlan(", "ContentCellsInspection inspectUndefinedTextureUploads("),
                           ("const char* populateContentPlan(", "bool completedContentIsReadOnly()")]:
            planner = state.split(begin, 1)[1].split(end, 1)[0]
            self.assertIn("plan.appendRangeCell(object, stamp, index, initial, timeline)", planner)
            self.assertNotIn("rangeContents.append", planner)
        control = self.read_source("src/rhi/frontend/resource_texture_content_control.h")
        readonly = control.split("if (planVersion == 19u && budget_.contractVersion == 2u)", 1)[1].split("const auto depthVersion", 1)[0]
        self.assertIn("if (count || !cells_)", readonly)
        self.assertIn("return diagnostic()", readonly)
        self.assertIn('"RHI-BINDLESS-GRAPHICS-DEPTH-CANONICAL-COMPLETION-UNAVAILABLE"', state)
        contract = self.read_source("src/rhi/frontend/bindless_graphics_depth_content.h")
        self.assertIn("canonicalVersion = 19u", contract)
        self.assertIn("b.graphics.bindlessCompletedReadContractVersion != 3u", contract)
        self.assertIn("view.mipLevelCount != texture.mipLevelCount", contract)

    def test_bindless_graphics_content_composes_original_root_without_native_completion(self):
        state = self.read_source("src/rhi/frontend/command_state.h")
        compose = state.split("prepareOriginalBindlessGraphicsOwnership(", 1)[1].split(
            "static std::shared_ptr<OriginalCommandStateTransaction> prepareOriginalOwner(", 1)[0]
        for token in ["std::is_same_v<Stream, OriginalCommandStreamProgram>",
                      "stream->compareOriginalStates(graph)", "sequence.bindlessRootStateVersion",
                      "sequence.bindlessRootStateOptIn", "OriginalCommandBindlessRootSource::validateOriginal",
                      "BindlessPreparedGraphicsBinding", "OriginalCommandBindlessRootAccesses::visit",
                      "access.rootSourceIndex != rootIndex", "hasIndependentOwners",
                      "materializeOriginalGraphicsPlan", "validateGraphicsStream"]:
            self.assertIn(token, compose)
        complete = state.split("const char* completeOriginalNativeOwnership(", 1)[1].split(
            "const char* cpuMutationDiagnostic", 1)[0]
        self.assertLess(complete.index("mixedBudget.contractVersion == 9u"), complete.index("completeCell("))
        self.assertIn("RHI-BINDLESS-GRAPHICS-CONTENT-NATIVE-COMPLETION-UNAVAILABLE", complete)
        contract = self.read_source("src/rhi/frontend/finite_graphics_content_validation.h")
        for token in ["class OriginalBindlessGraphicsContentPlanContract", "c.renderBindingReads",
                      "!OriginalTextureContentCreationContract::isBindlessCellCreation(creation.contractVersion)", "sources == 6u", "roots==rootStateVersion(geometryVersion)"]:
            self.assertIn(token, contract)
        fixture = CppSource(self.read_source("tests/fixtures/vulkan_original_bindless_graphics_content_fixture.h"))
        for token in ["probe.verify(", "verifyHotNoAllocation", "commitOriginalPendingOwnership",
                      "abandonOriginalPendingOwnership", "preflightOriginalContentCells",
                      "validateOriginalContent(completed)"]:
            self.assertIn(token, fixture)

    def test_original_graphics_array_gradients_dominate_index_and_discard(self):
        shader = self.read_source("tests/fixtures/bindless_original_graphics_array.slang")
        for derivative in ["ddx(uv)", "ddy(uv)"]:
            self.assertLess(shader.index(derivative), shader.index("uint index"))
            self.assertLess(shader.index(derivative), shader.index("if (x =="))
        for token in ["Images_sampleGrad", "Images_load", "Images_query", "Images_sampleLevel",
                      "int4(0, 0, 2, 0)", "int4(0, 0, int(layer), 2)", "Images_sampleGrad(index, 2u"]:
            self.assertIn(token, shader)
        fixture = self.read_source("tests/integration/rhi_vulkan_bindless_compute_tests.cpp")
        self.assertIn("uploaded.arrayTextures=true", fixture)
        self.assertIn("graphics.arraySampling=true", fixture)

    def test_vulkan_fragment_buffers_require_enabled_features_and_real_root_execution(self):
        source = self.read_source("src/rhi/backends/vulkan/vulkan_native_pipelines.cpp")
        fragment = source.split("if (enabled.fragmentStoresAndAtomics) {", 1)[1].split("caps_.execution", 1)[0]
        for token in ["publish(fragmentStage, 3u, Op::Load, true)",
                      "publish(fragmentStage, 4u, readOnlyBufferOps, true)",
                      "publish(fragmentStage, 5u, writableBufferOps, true)"]:
            self.assertIn(token, fragment)
        self.assertNotIn("publish(fragmentStage, 2u", fragment)
        shader = self.read_source("tests/fixtures/bindless_original_graphics_buffers.slang")
        for token in ["Globals_load", "Inputs_load", "Inputs_query", "Counters_query",
                      "Counters_atomicAdd", "Counters_store", "Counters_load", "counterCount, 999u"]:
            self.assertIn(token, shader)
        fixture = self.read_source("tests/fixtures/vulkan_original_graphics_buffer_data_fixture.h")
        for token in ["backendFlushMappedWrite", "backendInvalidateMappedRead", "100u*(index+1u)+2u", "actual==expected"]:
            self.assertIn(token, fixture)

    def test_bindless_graphics_native_issuer_retains_cold_pass_until_real_completion(self):
        native = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_command.cpp")
        record = native.split("bool VulkanOriginalBindlessCommand::record(", 1)[1].split(
            "bool VulkanOriginalBindlessCommand::beforeSubmit", 1)[0]
        for token in ["graphics->native_.renderPass", "graphics->native_.framebuffer",
                      "graphics=graphics_.get()", "pass->commandIndex()==op.commandIndex",
                      "originalRenderPassBorrowed=true", "recordGraphicsPipelineBind", "recordDraw(",
                      "vkCmdEndRenderPass", "activeRenderPass={}"]:
            self.assertIn(token, record)
        for forbidden in ["createNativeObjects", "createTexture", "recordRenderPassBegin", "MutationJournal"]:
            self.assertNotIn(forbidden, record)
        complete = native.split("void VulkanOriginalBindlessCommand::fenceCompleted(", 1)[1]
        self.assertLess(complete.index("!command.impl_->submitFenceCompleted"), complete.index("commitOriginalRootSubmission"))
        self.assertLess(complete.index("commitOriginalRootSubmission"), complete.index("binding->originalResult()->published_ = true"))
        state = self.read_source("src/rhi/frontend/command_state.h")
        self.assertIn("generalSubmissionVersion != (hasOriginalBindlessProducedGeometryProfile() ? 15u : plan_->contentBudget.contractVersion == 19u ? 14u : plan_->contentBudget.contractVersion == 18u ? 13u : 6u) || readbackProven", state)
        self.assertIn("plan_->contentBudget.contractVersion == 19u && (generalSubmissionVersion != 14u || readbackProven)", state)
        fixture = CppSource(self.read_source("tests/fixtures/vulkan_original_bindless_graphics_content_fixture.h"))
        for token in ["OriginalCommandLifecycleReservation::prepareOriginal(seal,{5u", "waitSubmittedFenceUntil",
                      "retained.expired()", "contentGeneration()==initialGeneration+attempt+1u", "StateTransaction::Status::Abandoned",
                      "VulkanOriginalRootTerminalFixture::verify", "terminal.uncertainSubmit || terminal.uncertainFence",
                      "&& !terminalCases",
                      "OriginalCommandStateTransaction::Status::Invalidated && !result->publishedBytes()"]:
            self.assertIn(token, fixture)

    def test_bindless_graphics_pass_is_cold_budgeted_and_tied_to_original_stream(self):
        native = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_graphics_pass.cpp")
        header = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_graphics_pass.h")
        contract = self.read_source("src/rhi/frontend/bindless_graphics_pass_preparation.h")
        for token in ["Closure::validateOriginal", "Transaction::validateOriginal", "Stream::validateOriginal",
                      "compareOriginalBindlessGraphicsStream", "compareOriginalStates", "overlapsCpuOwner",
                      "maxNativePayloadObjects", "AtomicCapacityLease", "SharedIdentityProjection::inspect",
                      "backendVulkanSwapchainFrameSyncAvailable", "RHI-BINDLESS-GRAPHICS-PASS-ATTACHMENT-ALIAS",
                      "destroyVulkanRhiNativeRenderPassResources", "createNativeObjects"]:
            self.assertIn(token, native)
        factory = native.split("VulkanOriginalBindlessGraphicsPass::prepareOriginal(", 1)[1]
        self.assertLess(factory.index("captureAttachments()"), factory.index("createNativeObjects()"))
        self.assertLess(factory.index("inspectOwners("), factory.index("createNativeObjects()"))
        self.assertLess(factory.index("AtomicCapacityLease"), factory.index("createNativeObjects()"))
        for token in ["vkCmdBeginRenderPass", "vkCmdDraw", "vkQueueSubmit", "commitOriginalSubmission",
                      "applyOriginalPrototypePlan"]:
            self.assertNotIn(token, native)
        self.assertIn("bool nativeExecutionProven() const noexcept { return false; }", header)
        pipeline = self.read_source("src/rhi/backends/vulkan/vulkan_pipeline.cpp")
        self.assertIn("OriginalBindlessGraphicsPassContract::capturePipeline(desc)", pipeline)
        self.assertIn("dynamic_cast<const VulkanRhiRenderPipeline*>", pipeline)
        self.assertIn("vulkanRhiBindlessGraphicsPassPipelineState", native)
        for token in ["maxNativeObjects", "maxRetainedOwners", "maxCpuMetadataBytes",
                      "effectiveColorTargetCount", "effectiveDepthStencilFormat", "LoadOp::Clear", "StoreOp::Store"]:
            self.assertIn(token, contract)

    def test_bindless_root_geometry_does_not_borrow_pipeline_registration_or_legacy_execution(self):
        source = self.read_source("src/rhi/frontend/command_usage.h")
        geometry = source[source.index("class OriginalCommandGeometryBindingContract"):source.index("// endregion original_command_geometry_binding")]
        self.assertIn("preparedGeneration != root->originalSnapshot()->generation()", geometry)
        self.assertIn("binding.pipelineRegistrationGeneration ||", geometry)
        self.assertIn("pipeline.bindlessVertexBuffers(1u)", geometry)
        executor = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_command.cpp")
        self.assertIn("closure->closureContractVersion() != (produced ? OriginalBindlessRootNativeClosureContract::producedGeometryVersion : graphicsDepth ? 16u : mixed ? 14u : storage ? 11u : depthReads ? 9u : geometryDraw ? 3u : 1u)", executor)
        self.assertIn("program->originalBudget().bindlessRoots.contractVersion != (produced ? 6u : geometryDraw ? 5u : 4u)", executor)
        self.assertNotIn("ensureScratchIndexBuffer", executor)

    def test_bindless_root_geometry_native_closure_remains_cold_and_canonical(self):
        source = self.read_source("src/rhi/backends/vulkan/vulkan_bindless_root.cpp")
        for forbidden in ("vkCmdBindVertexBuffers", "vkCmdBindIndexBuffer", "vkCmdDrawIndexed", "vkQueueSubmit", "commitOriginalSubmission"):
            self.assertNotIn(forbidden, source)
        for required in ("captureGeometryEntry", "OriginalCommandBufferLease::validateOriginal", "registration.matchesIdentity",
                         "isExtraGeometrySource", "result->inspectOwners", "sources()[index]", "Observation::capture"):
            self.assertIn(required, source)
        lowering = self.read_source("src/rhi/semantics/command_lowering.h")
        self.assertIn("RHI-BINDLESS-ROOT-GEOMETRY-CLOSURE-METAL-UNAVAILABLE", lowering)
        self.assertIn("RHI-BINDLESS-ROOT-GEOMETRY-CLOSURE-WEBGPU-UNAVAILABLE", lowering)

    def test_bindless_geometry_content_composition_is_opted_in_exact_and_cold(self):
        contract = self.read_source("src/rhi/frontend/finite_graphics_content_validation.h")
        for token in ("bindlessGeometryContractVersion", "bindlessGeometryOptIn", "RHI-BINDLESS-GRAPHICS-CONTENT-GEOMETRY-OPT-IN",
                      "geometryVersion==2u?8u:geometryVersion?7u:1u", "geometry==2u?6u:geometry?5u:4u"):
            self.assertIn(token, contract)
        state = self.read_source("src/rhi/frontend/command_state.h")
        for token in ("hasOriginalBindlessRootGeometryStateProfile", "hasOriginalBindlessGraphicsGeometryProfile",
                      "OriginalBindlessGraphicsContentPlanContract::consumes"):
            self.assertIn(token, state)
        lowering = self.read_source("src/rhi/semantics/command_state_lowering.h")
        for backend in ("NULL", "VULKAN", "METAL", "WEBGPU"):
            self.assertIn("RHI-BINDLESS-GRAPHICS-GEOMETRY-"+backend, lowering)
        fixture = self.read_source("tests/fixtures/vulkan_original_bindless_geometry_fixture.h")
        self.assertIn("BINDLESS_VULKAN_ORIGINAL_ROOT_GEOMETRY_CONTENT9_1", fixture)
        self.assertIn("tx->journalEmpty()", fixture)

    def test_initialized_bindless_geometry_closure_retains_exact_actors_without_execution_authority(self):
        source = self.read_source("src/rhi/backends/vulkan/vulkan_bindless_root.cpp")
        for token in ("GeometryContent::validateOriginal(actor)", "inventory->originalLeaseIndex(actor->originalLease())",
                      "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-COVERAGE",
                      "RHI-VULKAN-BINDLESS-ROOT-INITIALIZED-GEOMETRY-DUPLICATE", "inspectInitializedGeometry",
                      "GeometryContent::exclusiveCpuQuotaSlots", "actor->exclusiveCpuQuota", "maxInitializedGeometryBytes-bytes"):
            self.assertIn(token, source)
        contract = self.read_source("src/rhi/frontend/command_sources.h")
        for token in ("maxInitializedGeometryActors", "maxInitializedGeometryBytes", "RHI-BINDLESS-ROOT-INITIALIZED-GEOMETRY-SCHEMA"):
            self.assertIn(token, contract)
        for forbidden in ("vkQueueSubmit", "vkCmdDrawIndexed", "MutationJournal", "commitOriginalSubmission"):
            self.assertNotIn(forbidden, source)
        fixture = self.read_source("tests/fixtures/vulkan_original_bindless_geometry_fixture.h")
        for token in ("BINDLESS_VULKAN_ORIGINAL_ROOT_INITIALIZED_CLOSURE3", "initialized->actors[i].reset()",
                      "initialized->retired[i].expired()", "--smaller.maxInitializedGeometryBytes", "pressure.charged()"):
            self.assertIn(token, fixture)

    def test_host_geometry_content_requires_authentic_initialization_and_keeps_execution_closed(self):
        source = self.read_source("src/rhi/backends/vulkan/vulkan_original_geometry_content.cpp")
        for token in ("runtime->syntheticRuntime()", "queryBufferAllocationBytes", "nativeBytes>budget.maxRetainedBufferBytes",
                      "device.writeBuffer(*object,frozen.data(),frozen.size())", "object->sealWrites()", "Lease::prepareOriginal",
                      "hasQuotaBoundSharedOwner", "validateOriginalCommittedState(*current)", "sameOwner(lease,self->lease_)"):
            self.assertIn(token, source)
        for forbidden in ("vkQueueSubmit", "vkCmdDrawIndexed", "MutationJournal", "commitOriginalSubmission", "initialized=true"):
            self.assertNotIn(forbidden, source)
        contract = self.read_source("src/rhi/frontend/command_geometry_content.h")
        self.assertIn("std::variant<Staging::IndexContentIntent,Staging::VertexContentIntent,Staging::ShaderContentIntent>", contract)
        self.assertIn("bytes!=desc.sizeBytes", contract)
        lowering = self.read_source("src/rhi/semantics/geometry_content_lowering.h")
        self.assertIn("RHI-HOST-GEOMETRY-CONTENT-NULL", lowering)
        self.assertIn("CapabilityMode::Native,false,true,false,nullptr", lowering)
        self.assertIn("CapabilityMode::Equivalent,false,true,false,nullptr", lowering)

    def test_metal_initialized_geometry_is_real_and_retained_through_original_pending(self):
        for backend in ("vulkan", "metal"):
            graphics_pass = self.read_source("src/rhi/backends/" + backend + "/" + backend + "_original_bindless_graphics_pass.cpp")
            self.assertIn("transaction->hasOriginalBindlessGraphicsGeometryProfile()!=(closure->closureContractVersion()==3u)", graphics_pass)
        actor = self.read_source("src/rhi/backends/metal/metal_original_geometry_content.cpp")
        for token in ("Contract::validateQuotas", "device.createBuffer(desc)",
                      "observeMetalRhiNativeBufferAllocation", "device.writeBuffer(*object,frozen.data(),frozen.size())",
                      "object->sealWrites()", "Lease::prepareOriginal", "nativeIdentity_.matches",
                      "validateInitializedBufferPending", "ImmutableBindlessGeometryRead5"):
            self.assertIn(token, actor)
        closure = self.read_source("src/rhi/backends/metal/metal_bindless_root.cpp")
        for token in ("GeometryContent::validateOriginalPending", "entry.initializedGeometryState.emplace",
                      "maxInitializedGeometryBytes-initializedBytes", "actor->exclusiveCpuQuota",
                      "RHI-METAL-BINDLESS-ROOT-GEOMETRY-COVERAGE", "captureGeometryEntry"):
            self.assertIn(token, closure)
        command = self.read_source("src/rhi/backends/metal/metal_original_bindless_command.cpp")
        self.assertIn("Closure::validateOriginalPending(selected,reservation_->originalSeal())", command)
        self.assertIn("std::dynamic_pointer_cast<const MetalOriginalGeometryContent>", command)
        native = self.read_source("src/rhi/backends/metal/metal_native_commands.mm")
        for token in ("validateGeometryFetch(closure,program,geometryFetch)", "case K::DrawIndexed:",
                      "graphicsClosure=selected", "graphicsClosure->initializedGeometry(index)",
                      "RHI-METAL-ROOT-GEOMETRY-PIPELINE-NOT-BOUND", "command.recordVertexBufferBind",
                      "command.recordIndexBufferBind", "command.recordDrawIndexed"):
            self.assertIn(token, native)
        allocator = self.read_source("src/rhi/backends/metal/metal_device.cpp")
        create = allocator.split("createBuffer(const rhi::BufferDesc& bufferDesc)",1)[1].split("CommandValidationResult writeBuffer",1)[0]
        self.assertLess(create.index("observeMetalRhiNativeBufferAllocation"), create.index("allocator_.allocate"))
        self.assertIn("allocationBytes,bufferDesc.memory", create)
        payload = self.read_source("src/rhi/backends/metal/metal_native_resources.mm")
        self.assertIn("buffer.allocatedSize", payload)

    def test_webgpu_initialized_geometry_uses_mapped_creation_and_original_custody(self):
        actor = self.read_source("src/rhi/backends/webgpu/webgpu_original_geometry_content.cpp")
        for token in ("Contract::validateQuotas", "createWebGpuRhiOriginalGeometryBuffer",
                      "WebGpuNativeBufferInitializer", "std::memcpy(mapped,bytes.data(),bytes.size())",
                      "object->sealWrites()", "captureWebGpuRhiOriginalGeometryBuffer",
                      "Lease::prepareOriginal", "nativeIdentity_.matches()",
                      "validateInitializedBufferPending", "ImmutableBindlessGeometryRead5"):
            self.assertIn(token, actor)
        for forbidden in ("wgpuQueueSubmit", "wgpuQueueWriteBuffer", "commitOriginalSubmission",
                          "MutationJournal", "signalFromHost", "CpuReplay"):
            self.assertNotIn(forbidden, actor)
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        initializer = native.split("createWebGpuNativeBufferPayload(",1)[1].split(
            "WebGpuNativeBufferCreation::prepareReadbackOriginal",1)[0]
        self.assertLess(initializer.index("initializer.initialize(initializer.context,mapped,desc.sizeBytes)"),
                        initializer.index("wgpuBufferUnmap(payload->buffer)"))
        closure = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root.cpp")
        for token in ("GeometryContent::validateOriginalPending", "entry.initializedGeometryState.emplace",
                      "maxInitializedGeometryBytes-initializedBytes", "actor->exclusiveCpuQuota",
                      "RHI-WEBGPU-BINDLESS-ROOT-GEOMETRY-COVERAGE", "captureGeometryEntry"):
            self.assertIn(token, closure)

    def test_public_root_sequence_checks_original_stream_order_before_native_preparation(self):
        contract = self.read_source("src/rhi/frontend/bindless_root_sequence.h")
        for token in ("OriginalCommandStreamProgram::validateOriginal(stream)",
                      "Root::validateOriginal(root)", "matchesCanonicalSources(*inventory)",
                      "originalSnapshot()->generation()!=budget.generation", "op.referenceCount!=1u",
                      "sameOwner(*root,expected)", "RHI-BINDLESS-ROOT-SEQUENCE-UNCONSUMED",
                      "RHI-BINDLESS-ROOT-SEQUENCE-VISIT-CAPACITY"):
            self.assertIn(token, contract)
        for file in ("bindless_compute_binding.cpp", "bindless_root_fence_execution.cpp",
                     "bindless_graphics_binding.cpp"):
            source = self.read_source("src/rhi/frontend/" + file)
            inspector = "inspectOriginal" if file == "bindless_graphics_binding.cpp" else "inspectPublicInput"
            self.assertLess(source.index("OriginalBindlessRootSequenceContract::" + inspector),
                            source.index("return prepareOriginal"))

    def test_scoped_root_initializers_are_selected_and_authenticated_before_native_closure(self):
        contract = self.read_source("src/rhi/frontend/bindless_root_sequence.h")
        for token in ("OriginalBindlessInitializedSources initialized", "initializedSources(",
                      "OriginalBindlessInitializedSourcesContract::validatePublicInputs(",
                      "RHI-BINDLESS-ROOT-SEQUENCE-INITIALIZED-INPUT-SCHEMA"):
            self.assertIn(token, contract)
        actors = self.read_source("src/rhi/frontend/bindless_root_initialized_shader.h")
        for token in ("OriginalBindlessInitializedSourcesContract", "Content::validateOriginal(actor)",
                      "dynamic_pointer_cast<const Content>", "budget.maxInitializedGeometryBytes-bytes"):
            self.assertIn(token, actors)
        for file in ("src/rhi/backends/vulkan/vulkan_original_bindless_compute_execution.cpp",
                     "src/rhi/backends/metal/metal_original_bindless_command.cpp",
                     "src/rhi/backends/webgpu/webgpu_device.cpp"):
            source = self.read_source(file)
            start = source.index("const auto prepareClosure=")
            end = source.index("using Sequence=", start)
            adapter = source[start:end]
            self.assertLess(adapter.index("inputs.roots.initializedSources"),
                            adapter.index("OriginalBindlessInitializedSourcesContract::project"))
            self.assertLess(adapter.index("OriginalBindlessInitializedSourcesContract::project"),
                            adapter.index("NativeClosure::prepareOriginal"))
            self.assertIn("Sequence::prepareHistoricalInput(", source[end:])

    def test_original_buffer_readback_native_mapping_preserves_finite_request_interval(self):
        contract = self.read_source("src/rhi/buffer_readback_range.h")
        for token in ("offsetAlignment=8u", "sizeAlignment=4u",
                      "sizeBytes<=allocationBytes-offsetBytes", "offsetBytes==other.offsetBytes"):
            self.assertIn(token, contract)
        sequence = self.read_source("src/rhi/frontend/bindless_root_readback_sequence.h")
        for token in ("inspectMapDestinations", "range.diagnostic()",
                      "RHI-BINDLESS-ROOT-READBACK-SEQUENCE-DESTINATION-ALIAS"):
            self.assertIn(token, sequence)
        admission = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root_cold_admission.h")
        self.assertIn("Readbacks::inspectMapDestinations(binding,readbackInput)", admission)
        runtime = self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.cpp")
        for token in ("prepareColdRange", "validateRetainedRange", "range_.matches(range)",
                      "wgpuBufferGetSize(native)!=range.allocationBytes",
                      "WGPUMapMode_Read,self.range_.offsetBytes",
                      "static_cast<WGPUBuffer>(self.buffer_),self.range_.offsetBytes,self.destination_",
                      "bytes!=range_.sizeBytes"):
            self.assertIn(token, runtime)
        storage = self.read_source("src/rhi/backends/webgpu/webgpu_canonical_readback_result_storage.h")
        for token in ("binding->originalRequest()->offsetBytes()", "entry.mapRange.matches(",
                      "entry.mapRange.sizeBytes>SIZE_MAX"):
            self.assertIn(token, storage)
        recorder = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for token in ("receipt.validateRetainedRange(entry.handle,entry.mapRange)",
                      "map.map.prepareColdRange(buffer,range", "map.map.validateRetainedRange(buffer,range)"):
            self.assertIn(token, recorder)

    def test_public_graphics_adapter_uses_original_issuer_and_real_completion(self):
        public = self.read_source("src/rhi/frontend/bindless_graphics_execution.h")
        for token in ("OriginalBindlessGraphicsExecutionIntent", "OriginalInitializedGeometrySource",
                      "hasQuotaBoundSharedOwner(self)", "OriginalCommandNativeLifecycleContract::validateCapacityRequest"):
            self.assertIn(token, public)
        common = self.read_source("src/rhi/frontend/bindless_graphics_binding.cpp")
        for token in ("originalGraphicsExecutionContractVersion", "kExtensionBindlessCross",
                      "RHI-BINDLESS-GRAPHICS-EXECUTION-QUOTA-DOMAIN", "issuedBy(this,inputs.root->authorization())"):
            self.assertIn(token, common)
        native = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_command.cpp")
        for token in ("VulkanOriginalBindlessGraphicsExecutionView", "submittedFenceCompleted()",
                      "retireCompletedVulkanRhiSubmittedCommands()", "custody->record(*command_,diagnostic_)",
                      "command_->submit(diagnostic_)", "validateGeometryFetch(closure,inputs.readback->originalProgram(),budget.fetch)",
                      "budget.fetch,true", "transaction_->status()==OriginalCommandStateTransaction::Status::Committed",
                      "result_->publishedBytes()"):
            self.assertIn(token, native)
        lowering = self.read_source("src/rhi/semantics/bindless_graphics_execution_lowering.h")
        for backend in ("NULL", "WEBGPU"):
            self.assertIn("RHI-BINDLESS-GRAPHICS-EXECUTION-" + backend, lowering)
        self.assertIn("case Backend::Vulkan:return {true,true,nullptr};", lowering)
        self.assertIn("case Backend::Metal:return {true,true,nullptr};", lowering)
        metal = self.read_source("src/rhi/backends/metal/metal_original_bindless_command.cpp")
        view = metal[metal.index("class MetalOriginalBindlessGraphicsExecutionView"):]
        for token in ("MetalOriginalBindlessGraphicsPass::prepareOriginal", "MetalOriginalBindlessCommand::prepareOriginal",
                      "custody->record(*command_,diagnostic_)", "command_->submit(queue_,done_",
                      "done_->isComplete()", "transaction_->status()==OriginalCommandStateTransaction::Status::Committed"):
            self.assertIn(token, view)
        for forbidden in ("commitOriginalSubmission", "published_=true", "addCompletedHandler", "waitUntilCompleted"):
            self.assertNotIn(forbidden, view)
        for forbidden in ("commitOriginalSubmission", "published_=true", "vkQueueSubmit", "vkCreateFence"):
            view=native[native.index("class VulkanOriginalBindlessGraphicsExecutionView"):native.index("VulkanOriginalBindlessCommand::VulkanOriginalBindlessCommand")]
            self.assertNotIn(forbidden,view)

    def test_webgpu_original_finite_bindless_ranges_keep_generic_writes_closed(self):
        generic = self.read_source("src/rhi/frontend/descriptor_buffer_binding_range.h")
        self.assertIn("RHI-DESCRIPTOR-BUFFER-SUBRANGE-LOWERING-MISSING", generic)
        publication = self.read_source("src/rhi/backends/webgpu/webgpu_backend_descriptors.cpp")
        for token in ("writeOriginalBufferRangeImpl", "sameOwner(native, registered->buffer)",
                      "OriginalDescriptorBufferBindingContract::validateFiniteInterval", "originalRanges_.swap(candidateRanges)"):
            self.assertIn(token, publication)
        prepared = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_preparation.cpp")
        self.assertLess(prepared.index("observeSuccessfulBindingWrite("), prepared.index("initializeWebGpuNativePhysicalDescriptorGroup("))
        self.assertIn("entry.offset = range.offsetBytes; entry.size = range.sizeBytes", prepared)
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        for token in ("if (!publications)", "OriginalCommandBufferLease::validateOriginal(publication->source)",
                      "createDescriptorMetadata(layout, bindings, nullptr)"):
            self.assertIn(token, native)
        fixture = self.read_source("tests/fixtures/webgpu_original_bindless_root_execution_fixture.h")
        for token in ("outside-canaries-unchanged", "snapshotRangeCount", "finiteRanges ? 0xCDu : 0u"):
            self.assertIn(token, fixture)

    def test_bindless_geometry_pending_uses_exact_seal_and_initialized_cold_fetch(self):
        closure = self.read_source("src/rhi/backends/vulkan/vulkan_bindless_root.cpp")
        for token in ("entry.initializedGeometryState.emplace(*state)", "GeometryContent::validateOriginalPending",
                      "validateOriginalCommittedState(*entry.initializedGeometryState)"):
            self.assertIn(token, closure)
        actor = self.read_source("src/rhi/backends/vulkan/vulkan_original_geometry_content.cpp")
        for token in ("hasQuotaBoundSharedOwner(seal)", "self->validateIdentity()", "captured.resourceGeneration()",
                      "ImmutableBindlessGeometryRead5,self->budget_.contractVersion==2u?6u:5u"):
            self.assertIn(token, actor)
        issuer = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_command.cpp")
        for token in ("fetch.contractVersion!=2u", "inspectInitializedGeometry", "!command.impl_->indexBufferBound",
                      "recordDrawIndexed", "Closure::validateOriginalPending(selected,seal)"):
            self.assertIn(token, issuer)
        self.assertLess(issuer.index("inspectInitializedGeometry"), issuer.index("acquireOriginalFaultCustody"))
        stream = self.read_source("src/rhi/frontend/command_stream_program.h")
        for token in ("inspectInitializedGeometry", "analyzeInitializedIndices", "validateInitializedVertices",
                      "visitInitializedIndexedDrawFetch"):
            self.assertIn(token, stream)
        self.assertNotIn("ensureScratchIndexBuffer", issuer)
        lowering = self.read_source("src/rhi/semantics/geometry_content_lowering.h")
        for backend in ("NULL", "WEBGPU"):
            self.assertIn("RHI-BINDLESS-GEOMETRY-EXECUTION-" + backend, lowering)
        fixture = self.read_source("tests/fixtures/vulkan_original_bindless_graphics_content_fixture.h")
        self.assertIn("diagnostic,nativePass,fetch", fixture)
        self.assertIn("indexed?3u:2u", fixture)
        for mode in ("vertex", "instance"):
            shader = self.read_source("tests/fixtures/bindless_original_geometry_" + mode + ".slang")
            self.assertIn("float4 position : POSITION", shader)
            self.assertIn("Images_sampleGrad", shader)

    def test_webgpu_initialized_geometry_reuses_original_graph_and_pending_seal(self):
        contract = self.read_source("src/rhi/frontend/bindless_root_execution.h")
        for token in ("OriginalInitializedGeometrySource", "GEOMETRY-CORRESPONDENCE"):
            self.assertIn(token, contract)
        closure = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root.cpp")
        for token in ("entry.initializedGeometryState.emplace(*state)",
                      "GeometryContent::validateOriginalPending", "inspectInitializedGeometry",
                      "inspectGeometryOwners", "validateGeometryBackend"):
            self.assertIn(token, closure)
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for token in ("WebGpuBindlessRootNativeClosure::validateOriginalPending",
                      "wgpuRenderPassEncoderSetVertexBuffer", "wgpuRenderPassEncoderSetIndexBuffer",
                      "wgpuRenderPassEncoderDrawIndexed", "initializedGeometryContractVersion"):
            self.assertIn(token, native)
        resources = self.read_source("src/rhi/backends/webgpu/webgpu_native_resources.cpp")
        for token in ("class WebGpuNativeVertexLayouts", "validateReflectedVertexInput",
                      "wgpuDeviceGetLimits", "RHI-BINDLESS-WEBGPU-VERTEX-DEVICE-LIMIT"):
            self.assertIn(token, resources)
        fixture = self.read_source("tests/integration/rhi_webgpu_original_bindless_root_browser_smoke.cpp")
        for token in ("original-root-geometry-execution-only", "execute.instanceRate = instance",
                      "Execute::Scenario::RetiredLifetime", "q.payload->liveBytes()"):
            self.assertIn(token, fixture)

    def test_repeated_bindless_attachment_clear_is_explicit_and_uses_original_content_cells(self):
        contract=self.read_source("src/rhi/frontend/bindless_attachment_clear.h")
        for token in ("OriginalBindlessAttachmentClearIntent", "controlVersion != 4u",
                      "intent.contractVersion == 1u && !uninitialized && completed && generation"):
            self.assertIn(token,contract)
        state=self.read_source("src/rhi/frontend/command_state.h")
        self.assertIn("validateObservation(b.attachmentClear",state)
        self.assertIn("initial.state == Content::State::Completed",state)
        self.assertIn("version != 4u && version != 5u",state)
        native=self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_graphics_pass.cpp")
        self.assertIn("OriginalBindlessAttachmentClearContract::same(*intent,budget.attachmentClear)",native)
        self.assertIn("validateObservation(budget_.attachmentClear",native)
        lowering=self.read_source("src/rhi/semantics/bindless_attachment_clear_lowering.h")
        for backend in ("NULL","METAL","WEBGPU"):
            self.assertIn("RHI-BINDLESS-ATTACHMENT-CLEAR-"+backend,lowering)
        for forbidden in ("MutationJournal", "cells_->", "vkQueueSubmit", "valueAt", "resetContent"):
            self.assertNotIn(forbidden,contract)
        fixture=self.read_source("tests/fixtures/vulkan_original_bindless_graphics_content_fixture.h")
        self.assertIn("BINDLESS_VULKAN_ROOT_ATTACHMENT_CLEAR1",fixture)
        self.assertIn("old.attachmentClear={}",fixture)

    def test_original_sampled_graphics_cold_preparation_scope_and_owners(self):
        lowering=self.read_source("src/rhi/semantics/bindless_original_preparation_lowering.h")
        self.assertIn("originalSampledGraphicsPreparationContractVersion == 3u",lowering)
        self.assertIn("RHI-BINDLESS-ORIGINAL-PREPARATION-METAL-UNAVAILABLE",lowering)
        self.assertIn("executionAvailable = false",lowering)
        native=self.read_source("src/rhi/backends/webgpu/webgpu_bindless_preparation.cpp")
        for evidence in ("observeSuccessfulBindingWrite", "creationKey", "parentTexture", "same(frozen.parent,now.parent)",
                         "RHI-WEBGPU-BINDLESS-PREPARED-SAMPLED-PROFILE", "hasPlan(authorization.plan(), 3u)",
                         "wgpuTextureGetMipLevelCount", "wgpuTextureGetDepthOrArrayLayers"):
            self.assertIn(evidence,native)
        for forbidden in ("wgpuQueueSubmit", "wgpuRenderPassEncoderDraw", "wgpuBufferMapAsync"):
            self.assertNotIn(forbidden,native)
        fixture=self.read_source("tests/fixtures/webgpu_original_sampled_preparation_fixture.h")
        for evidence in ("original-parents same-inventory", "allocation.verify(peak, factory)",
                         "verifyHotNoAllocation", "RHI-BINDLESS-ORIGINAL-PREPARATION-NOT-EXECUTABLE"):
            self.assertIn(evidence,fixture)

    def test_original_webgpu_completed_root_content_reuses_canonical_transaction(self):
        content=self.read_source("src/rhi/frontend/finite_graphics_content_validation.h")
        for evidence in ("bindlessCompletedReadContractVersion = 0u", "RHI-BINDLESS-GRAPHICS-COMPLETED-READ-SCHEMA",
                         "bindlessCompletedReadPlanLowering"):
            self.assertIn(evidence,content)
        contract=self.read_source("src/rhi/frontend/resource_texture_validation.h")
        for evidence in ("(planVersion == 10u || planVersion == 18u) && controlVersion == 1u", "(planVersion == 8u || planVersion == 17u) && controlVersion == 3u",
                         "version == 6u && rootCompletedReadVersion == 1u"):
            self.assertIn(evidence,contract)
        state=self.read_source("src/rhi/frontend/command_state.h")
        self.assertIn("OriginalCompletedTextureReadPlanContract::graphicsReadVersion(b.bindlessCompletedReadContractVersion)",state)
        fixture=self.read_source("tests/fixtures/webgpu_original_completed_content_fixture.h")
        for evidence in ("counts->completedControls==2u", "counts->drawReads==2u",
                         "object->state()==ResourceState::CopyDst", "verifyHotNoAllocation", "no-execution"):
            self.assertIn(evidence,fixture)
        runner=self.read_source("tests/fixtures/bindless_webgpu_original_root_runner.html")
        self.assertIn("completedPublished&&completedContent&&graphicsPrepared",runner)
        self.assertIn("originalCompletedTextureRootContentPassed:completedContent",runner)
        driver=self.read_source("tests/contract/bindless_webgpu_rhi_smoke.py")
        self.assertIn('"sampledExecutionValidated": False',driver)
        self.assertIn('and result.get("originalCompletedTextureRootContentPassed") is True',driver)

    def test_vulkan_depth_publication4_preserves_typed_proof_without_execution_grant(self):
        source = self.read_source("src/rhi/frontend/descriptor_completed_texture.cpp")
        self.assertIn("geometry->cellCount() != 1u || cellCount != 1u", source)
        self.assertIn("RHI-COMPLETED-DEPTH-READ-INPUT-SOURCE-PROFILE", source)
        self.assertIn("RHI-COMPLETED-TEXTURE-DESCRIPTOR-DEPTH-PROFILE", source)
        backend = self.read_source("src/rhi/backends/vulkan/vulkan_descriptors.cpp")
        for token in ("depthContent", "publication.depthContent, 4u",
                      "binding.depthTexture != bool(publication.depthContent)",
                      "original.get() != publication.source.get()",
                      "retained.depthContent = publication.depthContent",
                      "sameOwner(prior.depthContent, source)"):
            self.assertIn(token, backend)
        fixture = self.read_source("tests/fixtures/vulkan_original_completed_depth_publication_fixture.h")
        for token in ("producer(root,inventory,device)", "producer.attachmentClearOptIn=true",
                      "proof->validate() && !set->validateBindingElement",
                      "fresh->cells().front().contentGeneration()>oldGeneration",
                      "publication-alone-no-execution-grant", "quota->liveBytes()==baseline"):
            self.assertIn(token, fixture)

    def test_native_depth_public_execution_reuses_original_issuer_and_fence(self):
        public = self.read_source("src/rhi/frontend/bindless_root_fence_execution.h")
        for forbidden in ("CallbackCustody", "CallbackColdBudget", "CallbackIssuer"):
            self.assertNotIn(forbidden, public)
        native = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_compute_execution.cpp")
        for token in ("VulkanOriginalBindlessCommand::prepareOriginal", "custody->record(*command_",
                      "command_->submit(diagnostic_)", "command_->submittedFenceCompleted()",
                      "transaction_->status()==OriginalCommandStateTransaction::Status::Committed",
                      "result_->publishedBytes()", "retireCompletedVulkanRhiSubmittedCommands"):
            self.assertIn(token, native)
        for forbidden in ("vkQueueSubmit", "vkCmdDispatch", "commitOriginalNativeOwnership", "published_ = true"):
            self.assertNotIn(forbidden, native)
        original = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_command.cpp")
        self.assertIn("validateCompletedDepthReadPending(", original)
        self.assertIn("{entry.completedDepth,ResourceState::ShaderRead,PipelineStage::ComputeShader,ResourceAccess::ShaderRead},2u)", original)

    def test_initialized_shader_custody_uses_canonical_entries_original_pending_and_all_backends(self):
        contract = self.read_source("src/rhi/frontend/bindless_root_initialized_shader.h")
        for token in ("validatePublicInputs", "validateInputs", "Content::visitInitializedShader",
                      "inventory.originalLeaseIndex(actor->originalLease())", "ReadOnlyStorageBuffer",
                      "RHI-BINDLESS-INITIALIZED-SHADER-READONLY", "RHI-BINDLESS-INITIALIZED-SHADER-UNBOUND",
                      "Content::validateOriginalPending(actor,seal,*entry.initializedShaderState)",
                      "validateOriginalCommittedState(*entry.initializedShaderState)",
                      "RHI-BINDLESS-INITIALIZED-SHADER-VISIT-CAPACITY"):
            self.assertIn(token, contract)
        for forbidden in ("commitOriginalNativeOwnership", "queueSubmit", "MutationJournal", "mapAsync"):
            self.assertNotIn(forbidden, contract)
        for backend in ("vulkan", "metal", "webgpu"):
            source = self.read_source(f"src/rhi/backends/{backend}/{backend}_bindless_root.cpp")
            for token in ("OriginalBindlessInitializedShaderContract::validateInputs",
                          "OriginalBindlessInitializedShaderContract::capture",
                          "OriginalBindlessInitializedShaderContract::validateRetained<GeometryContent>",
                          "initializedActor()"):
                self.assertIn(token, source)
        fixture = self.read_source("tests/fixtures/original_initialized_shader_data_fixture.h")
        for token in ("Custody::capture", "Custody::validateRetained<Content>",
                      "CpuQuotaFactoryFixture::verifyHotNoAllocation", "entries[1u].sourceIndex=SIZE_MAX"):
            self.assertIn(token, fixture)

    def test_original_native_bindings_freeze_selected_root_and_indirect_payloads_stay_scoped(self):
        vulkan = self.read_source("src/rhi/backends/vulkan/vulkan_original_bindless_command.cpp")
        metal = self.read_source("src/rhi/backends/metal/metal_native_commands.mm")
        for source in (vulkan, metal):
            self.assertIn("case K::DrawIndexedIndirect:", source)
            self.assertIn("recordDrawIndexedIndirect(", source)
            self.assertIn("roots_.select(closure_->inventory_", source)
            self.assertIn("selected->pipeline_", source)
        web = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        for token in ("record.closure=selected", "record.renderPipeline=pipeline->pipeline",
                      "retained.closure!=record.closure", "retained.renderPipeline!=record.renderPipeline",
                      "record.closure->ordinaryGroups_", "wgpuRenderPassEncoderDrawIndexedIndirect",
                      "record.indirect.offset+std::uint64_t(i)*record.indirect.stride"):
            self.assertIn(token, web)
        stream = self.read_source("src/rhi/frontend/command_stream_program.h")
        self.assertIn("!budget_.bindlessRoots.geometryJoined()", stream)
        fixture = self.read_source("tests/contract/original_bindless_indirect_access_tests.cpp")
        self.assertIn("Program::validateOperation(CommandUsageKind::DrawIndexedIndirect,legacy)", fixture)

    def test_produced_geometry_native_factories_inspect_actual_closures_before_sealing(self):
        for path in (
                "src/rhi/backends/vulkan/vulkan_original_bindless_compute_execution.cpp",
                "src/rhi/backends/metal/metal_original_bindless_command.cpp",
                "src/rhi/backends/webgpu/webgpu_device.cpp"):
            native = self.read_source(path)
            join = native.index("inputs.producedGeometry.inspectNative(")
            self.assertLess(native.index("Sequence::preparePublicInput("), join)
            self.assertLess(join, native.index("OriginalCommandRecordingSeal::prepareOriginal(", join))
            for token in ("closures.data()", "closures.size()", "inputs.transaction->originalGraph()",
                          "inputs.readback->originalProgram()", "inputs.readbacks", "indirectFirstInstance"):
                self.assertIn(token, native[join:join + 700])
        common = self.read_source("src/rhi/frontend/bindless_produced_geometry.h")
        for token in ("Closure::validateOriginal(closures[i])", "closures[i]->initializedGeometry(source)",
                      "closures[i]->resourceEntryCount()", "cost>maxContentVisits-visits",
                      "sameOwner(result,actor)", "GpuVisibilityRootGeometryContract::inspect(",
                      "readbacks.empty()?&fault:readbacks.bindings", "INPUTS-REQUIRED"):
            self.assertIn(token, common)
        for forbidden in ("pcg/", "new ", "makeShared", "makeUnique", "commitOriginal", "wgpu", "VkBuffer"):
            self.assertNotIn(forbidden, common)
        runtime = self.read_source("src/rhi/backends/webgpu/webgpu_native_runtime.cpp")
        self.assertIn("device.limits.maxComputeWorkgroupsPerDimension", runtime)

    def test_produced_graphics_passes_are_created_cold_and_retained_by_original_command(self):
        sequence = self.read_source("src/rhi/frontend/bindless_graphics_pass_sequence.h")
        for token in ("OriginalBindlessRootSequenceContract::selectClosure", "Pass::validateOriginal(pass)",
                      "output.swap(prepared)", "input.maxCommandVisits-visits",
                      "pass->originalTransaction()", "pass->originalStream()",
                      "RHI-BINDLESS-GRAPHICS-SEQUENCE-PASS-COVERAGE"):
            self.assertIn(token, sequence)
        for backend, prefix, factory_file, record_file in (
            ("vulkan", "Vulkan", "vulkan_original_bindless_compute_execution.cpp", "vulkan_original_bindless_command.cpp"),
            ("metal", "Metal", "metal_original_bindless_command.cpp", "metal_native_commands.mm")):
            base = "src/rhi/backends/" + backend + "/"
            factory = self.read_source(base + factory_file)
            cold = factory.split("inputs.producedGeometry.inspectNative", 1)[1]
            self.assertLess(cold.index("OriginalBindlessGraphicsPassSequenceContract::prepare"),
                            cold.index("OriginalCommandRecordingSeal::prepareOriginal"))
            self.assertIn("{passes.data(),passes.size()}", cold)
            custody = self.read_source(base + backend + "_original_bindless_command.cpp")
            self.assertIn("producedPasses_.push_back(passInput.passes[i])", custody)
            self.assertIn("pass->validateNativeOwners(", custody)
            record = self.read_source(base + record_file).split("bool " + prefix + "OriginalBindlessCommand::record(", 1)[1]
            self.assertIn("producedPasses_", record)
            for forbidden in ("GraphicsPass::prepareOriginal", "createNativeObjects", "makeShared<"):
                self.assertNotIn(forbidden, record.split("bool " + prefix + "OriginalBindlessCommand::nativeCompleted", 1)[0]
                                 .split("bool " + prefix + "OriginalBindlessCommand::beforeSubmit", 1)[0])

    def test_webgpu_produced_workload_admission_preserves_selected_root_and_callback_domain(self):
        admission = self.read_source("src/rhi/backends/webgpu/webgpu_bindless_root_cold_admission.h")
        for token in ("produced=budget.contractVersion==7u", "budget.initializedGeometry.contractVersion!=3u",
                      "rootInput.count<5u", "passes-renderPasses!=4u", "dispatches!=4u",
                      "binds!=rootInput.count", "budget.contractVersion==7u",
                      "seal->originalBudget().contractVersion==22u",
                      "next->initializedGeometryOwnerCount()", "final->initializedGeometryOwnerCount()"):
            self.assertIn(token, admission)
        native = self.read_source("src/rhi/backends/webgpu/webgpu_native_command_preparation.cpp")
        root = native.split("const bool produced=request.contractVersion==7u", 1)[1]
        for token in ("selected->attachments_", "graphicsRootSource=source",
                      "sourceIndex!=graphicsRootSource", "retained.closure!=record.closure"):
            self.assertIn(token, root)
        lowering = self.read_source("src/rhi/semantics/bindless_root_program_lowering.h")
        self.assertIn("RHI-BINDLESS-PRODUCED-GEOMETRY-NATIVE-ADMISSION-REQUIRED", lowering)

    def test_produced_program_admission_is_shared_cold_and_bounded(self):
        program = self.read_source("src/rhi/frontend/bindless_produced_program.h")
        for token in ("OriginalBindlessRootNativeSequence<Closure>::inspectInput", "ReadbackSequence::inspectInput",
                      "budget.maxCommandVisits", "roots.budget.maxResourceVisits-visits",
                      "retainsOriginalBufferSource(source)", "binding->originalCopySource()",
                      "binding->originalReadbackSource()", "pushConsumer.update(", "pushConsumer.consume(",
                      "RHI-BINDLESS-PRODUCED-PROGRAM-FAULT-CLEAR", "RHI-BINDLESS-PRODUCED-PROGRAM-CONSUMER-ORDER",
                      "RHI-BINDLESS-PRODUCED-PROGRAM-PUSH-ROOT", "RHI-BINDLESS-PRODUCED-PROGRAM-FOREIGN-COPY"):
            self.assertIn(token, program)
        for backend, factory_name, command_name in (
            ("vulkan", "vulkan_original_bindless_compute_execution.cpp", "vulkan_original_bindless_command.cpp"),
            ("metal", "metal_original_bindless_command.cpp", "metal_native_commands.mm"),
            ("webgpu", "webgpu_device.cpp", "webgpu_bindless_root_cold_admission.h")):
            base = "src/rhi/backends/" + backend + "/"
            factory = self.read_source(base + factory_name).split("inputs.producedGeometry.inspectNative", 1)[1]
            self.assertLess(factory.index("OriginalBindlessProducedProgramContract::inspectNative"),
                            factory.index("OriginalCommandRecordingSeal::prepareOriginal"))
            command = self.read_source(base + command_name)
            self.assertIn("OriginalBindlessProducedProgramContract::inspectNative<Closure>", command)
            if backend != "webgpu":
                self.assertIn("Roots::additionalOwnerBound(rootInput,produced?1u:3u)", command)
                self.assertIn("OriginalBindlessProducedProgramContract::inspectNativePushRecording<Closure>", command)
        for forbidden in ("commitOriginal", "applyOriginalRecordingPlan", "publish", "vkCmd", "MTLCommand"):
            self.assertNotIn(forbidden, program)

    def test_visibility_shader_abi_contracts_have_no_reverse_pcg_dependency(self):
        for name in ("layout", "frozen_inputs", "root_inputs", "root_producer", "root_consumer",
                     "root_geometry", "indirect_fetch", "root_readback"):
            name = "gpu_visibility_" + name + ".h"
            contract = self.read_source("src/rhi/frontend/visibility/" + name)
            self.assertNotIn('"pcg/', contract)
            self.assertIn("namespace arp::rhi::visibility", contract)
            compatibility = self.read_source("src/pcg/" + name)
            self.assertIn('"rhi/frontend/visibility/' + name + '"', compatibility)
            self.assertIn("using rhi::visibility::", compatibility)

if __name__ == "__main__":
    unittest.main()
