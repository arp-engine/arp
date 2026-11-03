"""Reviewable first-party IO references; lexical evidence, not call-graph proof."""
import argparse
from collections import Counter
import hashlib
import io
from pathlib import Path
import re
import subprocess
import sys
import tokenize

import yaml

from cpp_source_evidence import tokens
from unified_io_semantic_review import ReviewDocumentError, SemanticIoReviews


class UnifiedIoInventory:
    roots = {'src', 'editor', 'tools', 'modules', 'examples', 'samples', 'games', 'Libs', 'scripts'}
    suffixes = {'.h', '.hpp', '.hh', '.c', '.cc', '.cpp', '.cxx', '.m', '.mm', '.inl',
                '.arp', '.py', '.js', '.mjs'}
    excluded = {'3rd', 'third_party', 'vendor', 'build', 'generated', 'node_modules', '.git'}
    symbols = {
        'read': {'ifstream', 'fstream', 'fopen', '_wfopen', 'fopen_s', '_wfopen_s',
                 'fread', 'fgets', 'open', 'openat', '_open', '_wopen', 'read', 'pread', 'readTextBytes', 'readBinaryBytes',
                 'CreateFileW', 'CreateFileA', 'createReadStream', 'readTextFile',
                 'readBinaryFile', 'readText', 'readResourceSource', 'readResourceText',
                 'ZipFile', 'loadHost', 'acquireUiFontSource', 'loadDefaultUiFontFace',
                 'makeGraphicsShaderPackage', 'hydrateShaderPackageReflectionFromArtifact',
                 'hydrateEditorSceneViewportShaderPackage',
                 'importAudioFile', 'decodeAudioFileToRuntimePcm', 'ma_decoder_init_file',
                 'read_text', 'read_bytes', 'read_binary_local', 'readBinaryLocal', 'readFile', 'readFileSync',
                 'ReadFile', 'readAsync', 'readResolved', 'NtCreateFile', 'digestFile'},
        'metadata': {'canonical', 'weakly_canonical', 'exists', 'is_regular_file',
                     'is_directory', 'symlink_status', 'file_size', 'last_write_time',
                     'directory_iterator', 'recursive_directory_iterator', 'stat', 'lstat',
                     'fstat', 'glob', 'rglob', 'iterdir', 'GetFileAttributesW', 'GetFileSizeEx',
                     'GetFileInformationByHandle', 'GetFileInformationByHandleEx', 'query', 'queryAsync'},
        'write': {'ofstream', 'fwrite', 'write_text', 'write_bytes', 'writeFile', 'writeFileSync',
                  'WriteFile', 'replaceFileAtomically', 'replaceIfUnchanged', 'create_directory',
                  'create_directories', 'remove', 'remove_all', 'rename', 'copy_file',
                  'copy2', 'copyfile', 'copytree',
                  'save', 'saveAsync', 'prepareFileSave', 'prepareSourceSave', 'writeAndFlush',
                  'FlushFileBuffers', 'MoveFileExW', 'DeleteFileW',
                  'NtSetInformationFile', 'SetFileInformationByHandle'},
        'transport': {'fetch', 'emscripten_fetch'},
        'receipt': {'receipt', 'receiptAsync'},
    }
    boundaries = {
        'src/core/io/blocking_file_reader.cpp', 'src/core/io/atomic_file_service.cpp',
        'src/core/io/staged_directory.cpp', 'src/core/io/filesystem_path.h',
        'src/core/io/detail/native_directory_access.cpp',
    }
    unified_entries = {
        'src/core/io/file_io.cpp': 'LegacyLocalReadAdapter',
        'src/core/io/file_io.h': 'LegacyLocalReadInterface',
        'src/core/io/file_service.cpp': 'UnifiedIoDispatch',
        'src/core/io/file_service.h': 'UnifiedIoDispatchInterface',
        'src/core/io/file_backend.h': 'UnifiedIoBackendInterface',
        'src/core/io/thread_pool_file_backend.cpp': 'LocalWorkAdapter',
        'src/core/io/thread_pool_file_backend.h': 'LocalWorkAdapterInterface',
        'src/core/io/memory_file_backend.cpp': 'RegisteredMemoryBackendWithExplicitBoundedSaves',
        'src/core/io/memory_file_backend.h': 'RegisteredMemoryBackendInterface',
        'src/core/io/mounted_file_backend.cpp': 'GenerationMountRouter',
        'src/core/io/mounted_file_backend.h': 'GenerationMountRouterInterface',
        'src/core/io/native_directory_file_backend.cpp': 'OwnedNativeDirectoryWorker',
        'src/core/io/native_directory_file_backend.h': 'OwnedNativeDirectoryWorkerInterface',
        'src/core/io/http_file_backend.cpp': 'HttpFileBudgetVersionAdapter',
    }

    def __init__(self, root):
        self.root = Path(root).resolve()

    @classmethod
    def references(cls, source, suffix):
        # Reuse the repository lexer, preserving offsets/line numbers. Comments
        # and string contents cannot manufacture an IO symbol reference.
        if suffix in {'.py', '.arp'}:
            # Both use # comments and // floor division. The shared C++
            # lexer would invent IO inside comments and hide real RHS calls
            # after //; reuse Python's tokenizer for these lexical shapes.
            # This inventories identifiers, not ArpScript parse validity.
            return [dict(line=token.start[0], symbol=token.string, operation=kind)
                    for token in tokenize.generate_tokens(io.StringIO(source).readline)
                    if token.type == tokenize.NAME
                    for kind, names in cls.symbols.items() if token.string in names]
        lexical = []
        for value, start, end in tokens(source):
            if value.startswith(('//', '/*')) or re.match(r'^(?:u8|u|U|L)?(?:R?"|\')', value):
                continue
            lexical.append((value, start, end))
        results = []
        for index, (value, start, _) in enumerate(lexical):
            operation = next((kind for kind, names in cls.symbols.items() if value in names), None)
            if operation is None:
                continue
            if operation == 'receipt':
                # Receipt also names ordinary result fields. Inventory the
                # new callable/qualified entry points, including member-pointer
                # aliases, rather than treating every value access as IO.
                called = index + 1 < len(lexical) and lexical[index + 1][0] == '('
                qualified = index > 0 and lexical[index - 1][0] == '::'
                if not (called or qualified):
                    continue
            # Include constructions, declarations and aliases as candidates.
            # Their semantic classification is reviewed during the named batch.
            results.append(dict(line=source.count('\n', 0, start) + 1,
                                symbol=value, operation=operation))
        return results

    @classmethod
    def classification(cls, path):
        if path in {'src/audio/audio_importer.h', 'src/audio/audio_importer.cpp',
                    'src/audio/miniaudio_backend.h', 'src/audio/miniaudio_backend.cpp'}:
            return 'audio', 'M5', 'ExplicitWaveMemoryDecodeAndCoreAllocatorHooksWithRetainedLegacyColdFileEntry', 'ReviewedPartialMigration'
        network_io = {
            'src/network/http/byte_read_transport.cpp': 'EngineHttpClientByteTransport',
            'src/network/http/fetch_byte_transfer.cpp': 'BrowserFetchByteTransport',
            'src/network/http/byte_transfer_executor.cpp': 'BoundedSharedNativeFetchProducerAndCompletionLifecycle',
            'src/core/io/http_file_backend.cpp': 'HttpFileBudgetVersionAdapter',
        }
        if path in network_io:
            owner = 'core/io' if path.startswith('src/core/') else 'network/http'
            return owner, 'I2', network_io[path], 'ReviewedIoInfrastructure'
        if path in {'src/core/io/workspace_file_backend.cpp', 'src/core/io/workspace_file_backend.h',
                    'src/core/io/workspace_file_wire.h', 'src/core/io/detail/http_transport_status.h'}:
            return 'core/io', 'I6', 'BoundedAuthenticatedWorkspaceFileAndReceiptClientOverSharedTransport', 'ReviewedIoInfrastructure'
        if path == 'src/core/io/detail/atomic_file_writer.h':
            return 'core/io', 'I5', 'SharedOwnedHandleWriteFlushAndExclusiveTemporaryNaming', 'ExplicitBoundary'
        if path in {'src/core/io/file_save_receipt.h', 'src/core/io/file_save_receipt_contract.h'}:
            return 'core/io', 'I6', 'MountedSaveReceiptIdentityValuesAndAdmissionContract', 'ReviewedIoInfrastructure'
        if path in cls.boundaries:
            return 'core/io', 'I1', 'LocalBackendBoundary', 'ExplicitBoundary'
        if path in cls.unified_entries:
            return 'core/io', 'I1', cls.unified_entries[path], 'ReviewedIoInfrastructure'
        if path == 'src/resource/resource_graph_executor.cpp':
            return 'resource', 'M1', 'BoundedMountedGraphOwnerAdmissionAndGenerationPinnedSingleReload', 'ReviewedOwnerAdmission'
        if path == 'src/resource/internal_asset_package_loader.cpp':
            return 'resource', 'M1', 'ManifestFirstBoundedPackageAcquisition', 'ReviewedAcquisitionOnly'
        if path == 'src/resource/shader_artifact_resource.cpp':
            return 'resource', 'M2', 'ShaderArtifactGraphSourceProjection', 'ReviewedAcquisitionOnly'
        if path == 'src/ui/ui_document_resource.cpp':
            return 'ui', 'M2', 'SourceOnlyDocumentAndPreparedMount', 'ReviewedSourceMigration'
        if path == 'src/ui/ui_theme_resource.cpp':
            return 'ui', 'M2', 'SourceOnlyThemeAtlasMetadataAndExplicitBinding', 'ReviewedSourceMigration'
        if path == 'src/ui/ui_atlas_image_resource.cpp':
            return 'ui', 'M2', 'BoundedImageSourceAndMetadataPath', 'ReviewedSourceMigration'
        if path == 'src/ui/ui_font_source.cpp':
            return 'ui', 'M2', 'ImmutableFontBytesWithExplicitColdHostAdapter', 'ReviewedSourceMigration'
        if path == 'src/ui/ui_font_face_cache.cpp':
            return 'ui', 'M2', 'SourceKeyedFaceCacheAndHostSnapshotEntry', 'ReviewedSourceMigration'
        if path == 'src/ui/ui_font.cpp':
            return 'ui', 'M2', 'SourceDrivenRasterizerAndLegacyDefaultFontProfile', 'ReviewedSourceMigration'
        if path == 'src/ui/ui_font_set_resource.cpp':
            return 'ui', 'M2', 'BoundedFontSetByteAcquisition', 'ReviewedSourceMigration'
        if path == 'src/renderer/ui/ui_text_layout.h':
            return 'renderer/ui', 'M2', 'PreparedFaceMetricsAndKerningWithoutDefaultFontDiscovery', 'ReviewedSourceMigration'
        if path == 'src/ui/ui_document.cpp':
            return 'ui', 'M2', 'PreparedRuntimeMountWithRemainingLocalLoaders', 'ReviewedPartialMigration'
        if path in {'src/shader/shader_artifact_files.h',
                    'src/shader/shader_reflection_cache.cpp',
                    'src/shader/single_entry_shader_artifact.cpp',
                    'src/shader/graphics_shader_module_artifact.cpp',
                    'src/shader/webgl_shader_program_artifact.cpp',
                    'src/shader/original_depth_table_expansion_artifact.cpp'}:
            return 'shader', 'M2', 'AcquiredArtifactInputWithLegacyLocalEntry', 'ReviewedPartialMigration'
        if path == 'src/resource/asset_package_closure.cpp':
            return 'resource', 'M1', 'SourceOnlyInspectAndPreparedOutputsBesideLegacyNativeProducerAndPublication', 'ReviewedPartialMigration'
        if path == 'src/resource/internal_asset_package.cpp':
            return 'resource', 'M1', 'SourceOnlyReaderAndPreparedMetadataBesideLegacyNativeFileWriter', 'ReviewedPartialMigration'
        if path == 'editor/project/project_scene_authoring.cpp':
            return 'editor/project', 'M4', 'SourceOnlyPackagePreparationAndFileServiceSavesWithNativeDirectoryPublication', 'ReviewedPartialMigration'
        if path in {'src/resource/terrain_asset_resource.cpp',
                    'src/resource/vegetation_catalog_cooked.cpp',
                    'src/resource/asset_package_payload.cpp'}:
            return 'resource', 'M1', 'StagedReadsWithRemainingColdHostIo', 'ReviewedPartialMigration'
        if path == 'src/resource/prefab_asset_resources.cpp':
            return 'resource', 'M1', 'RuntimeBlobSourcesWithRemainingAuthoringHostIo', 'ReviewedPartialMigration'
        if path == 'src/scene/scene_asset_package.cpp':
            return 'scene', 'M3', 'AcquiredPackageSourcesWithColdHostEntry', 'ReviewedPartialMigration'
        if path == 'src/scene/scene_asset_resource.cpp':
            return 'scene', 'M3', 'OriginBoundSceneDocumentTextAndFingerprint', 'ReviewedSourceMigration'
        if path == 'src/scriptvm/plugin/script_source_snapshot.cpp':
            return 'scriptvm/plugin', 'M3', 'SealedBoundedSourceBytesAndDecodedText', 'ReviewedSourceMigration'
        if path == 'src/scriptvm/aot/aot_package.cpp':
            return 'scriptvm/aot', 'M3', 'SourceOnlyCompilerWithExplicitHostInputOutputBoundary', 'ReviewedPartialMigration'
        if path == 'src/scriptvm/plugin/cpp_plugin_importer.cpp':
            return 'scriptvm/plugin', 'M3', 'ExplicitSourceBindingsWithLegacyDiscoveryAndNativeToolIo', 'ReviewedPartialMigration'
        if path == 'src/scriptvm/plugin/detail/cpp_plugin_source_reader.h':
            return 'scriptvm/plugin', 'M3', 'BoundedNativeSnapshotInputWithExplicitLegacyReader', 'ReviewedPartialMigration'
        if path in {'src/scriptvm/plugin/plugin_host_text_reader.cpp',
                    'src/scriptvm/plugin/plugin_host_text_reader.h'}:
            return 'scriptvm/plugin', 'M3', 'BoundedNativeGeneratedTextAndEscapedLogPrefixStillUsingBlockingAdapter', 'ReviewedPartialMigration'
        if path in {'src/scriptvm/plugin/immutable_build_input_snapshot.cpp',
                    'src/scriptvm/plugin/immutable_build_input_snapshot.h'}:
            return 'scriptvm/plugin', 'M3', 'NativeCompilerStagingCopyAndVerificationWithRemainingUnboundedVersionText', 'ReviewedPartialMigration'
        if path in {'src/scriptvm/plugin/compiler_toolchain_layout.cpp',
                    'src/scriptvm/plugin/compiler_toolchain_layout.h'}:
            return 'scriptvm/plugin', 'M3', 'ExplicitHostInstallationAndSdkCaptureWithRemainingVersionTextDependencies', 'ReviewedPartialMigration'
        if path == 'src/scriptvm/plugin/detail/apple_clang_toolchain.h':
            return 'scriptvm/plugin', 'M3', 'ExplicitAppleSdkPhysicalQueriesWithRemainingUnboundedQueryText', 'ReviewedPartialMigration'
        if path == 'src/scriptvm/plugin/aot_dynamic_package.cpp':
            return 'scriptvm/plugin', 'M3', 'BoundedHostTextWithRemainingToolchainBinaryAndOutputIo', 'ReviewedPartialMigration'
        if path == 'tools/asset_importer/script_runner.cpp':
            return 'tools/asset_importer', 'M6', 'SealedSourceClosureConsumptionWithRemainingPluginRootQueriesAndAotHostIo', 'ReviewedPartialMigration'
        if path in {'tools/io/cold_tool_files.cpp', 'tools/io/cold_tool_files.h'}:
            return 'tools/io', 'M6', 'ExplicitMountedOrNativeHostFileServiceProfilesWithNoWasmBlocking', 'ReviewedSourceMigration'
        if path in {'tools/io/native_tool_outputs.cpp', 'tools/io/native_tool_outputs.h'}:
            return 'tools/io', 'M6', 'BoundedCandidateAndBaselinePreflightWithIndividualFileServiceSavesAndExplicitNativeDirectoryPreparation', 'ReviewedPartialMigration'
        if path in {'tools/reflect_codegen/reflection_tool_io.cpp', 'tools/reflect_codegen/reflection_tool_io.h'}:
            return 'tools/reflect_codegen', 'M6', 'ExplicitNativeOrHttpBoundedHeaderInputsWithRetirementAndNoDiscovery', 'ReviewedSourceMigration'
        if path == 'tools/reflect_codegen/main.cpp':
            return 'tools/reflect_codegen', 'M6', 'OriginalReflectionSchemaAndIdentityFromPreparedTextWithSharedNativeOutputPolicy', 'ReviewedSourceMigration'
        if path in {'tools/asset_importer/document_import.cpp', 'tools/asset_importer/document_import.h'}:
            return 'tools/asset_importer', 'M6', 'BoundedNativeOrHttpDocumentTextWithFormalUiPublicationGated', 'ReviewedPartialMigration'
        if path == 'tools/asset_importer/asset_importer.h':
            return 'tools/asset_importer', 'M6', 'ExplicitDocumentSourceDispatchWithRemainingScriptAndPluginHostIo', 'ReviewedPartialMigration'
        if path in {'tools/ai_asset_generator/ai_asset_generator.h',
                    'tools/ai_asset_generator/prompt_manifest.cpp',
                    'tools/ai_asset_generator/prompt_manifest_application.cpp'}:
            return 'tools/ai_asset_generator', 'M6', 'OfflineVersionedYamlWithFileServiceBaselinePublicationNotNetworkGeneration', 'ReviewedSourceMigration'
        if path in {'tools/shader/fragment_atomic_write_verifier.cpp',
                    'tools/shader/spirv_verifier_source.cpp', 'tools/shader/spirv_verifier_source.h'}:
            return 'tools/shader', 'M6', 'BoundedOriginalSpirvBytesFromNativeOrEngineHttpWithoutChangingProofOrExecutionAuthority', 'ReviewedSourceMigration'
        if path == 'tools/script_aotc/main.cpp':
            return 'tools/script_aotc', 'M6', 'NativeHostFileServiceSourcesWithExactIdentityAndRemainingLegacyOutput', 'ReviewedPartialMigration'
        if path in {'tools/script_aotc/aot_cli_sources.cpp', 'tools/script_aotc/aot_cli_sources.h'}:
            return 'tools/script_aotc', 'M6', 'OriginalAotSnapshotFromNativeHostFileServiceWithRetiredInputOwner', 'ReviewedSourceMigration'
        if path == 'tools/cluster_previewer/cluster_preview_asset.cpp':
            return 'tools/cluster_previewer', 'M6', 'IndirectLegacyGlbFileReadAndNonOptInProceduralFallbackRequireExplicitSourceContract', 'ReviewedPartialMigration'
        if path in {'editor/tools/migration_io.cpp', 'editor/tools/migration_io.h'}:
            return 'editor/tools', 'M6', 'BoundedFileServiceTextAndMustBeMissingSaveForOfflineConverters', 'ReviewedSourceMigration'
        if path in {'editor/tools/project_migrate.cpp', 'editor/tools/scene_migrate.cpp'}:
            return 'editor/tools', 'M6', 'FileServiceConversionWithExplicitNativeDirectoryPreparationAndExclusivePublication', 'ReviewedPartialMigration'
        if path in {'examples/survivors/codegen_io.cpp', 'examples/survivors/codegen_io.h'}:
            return 'examples/survivors', 'M6', 'RetiredGameplayContentSourcesAndSharedIndividualSavesWithExplicitNativeDirectoryPreparation', 'ReviewedPartialMigration'
        if path == 'examples/survivors/script_codegen.cpp':
            return 'examples/survivors', 'M6', 'PreparedOfflineCodegenInputsAndOutputsWithOriginalAlgorithmAndBytes', 'ReviewedSourceMigration'
        if path == 'examples/build_a_queen/portable_renderer.cpp':
            return 'examples/build_a_queen', 'M7', 'ReviewedRenderGraphReadDeclarationsNotFileIo', 'ReviewedNonIoMatches'
        if path == 'examples/volumetric_cloud/prepared_offscreen.cpp':
            return 'examples/volumetric_cloud', 'M7', 'ReviewedRenderGraphReadAndGpuQuerySourceIdentifiersNotFileIo', 'ReviewedNonIoMatches'
        if path == 'examples/survivors/assets/scripts/combat.arp':
            return 'examples/survivors', 'M7', 'ReviewedProjectileRemovalMethodNotFileIo', 'ReviewedNonIoMatches'
        if path in {'tools/asset_importer/tool_script_sources.cpp',
                    'tools/asset_importer/tool_script_sources.h'}:
            return 'tools/asset_importer', 'M6', 'BoundedFlatModuleClosureFromFileServiceWithMissingOnlyAbsence', 'ReviewedSourceMigration'
        if path in {'tools/asset_importer/tool_script_source_host.cpp',
                    'tools/asset_importer/tool_script_source_host.h'}:
            return 'tools/asset_importer', 'M6', 'ExplicitNativeColdMountsAndRetiredImmutableSourceOwner', 'ReviewedSourceMigration'
        if path in {'modules/io/io_module.cpp', 'modules/io/io_module.h'}:
            return 'modules/io', 'M6', 'BoundedImmutableLocalBytesWithLegacyTextListsQueriesAndWrites', 'ReviewedPartialMigration'
        if path == 'scripts/build_survivors_assets.py':
            return 'tools/survivors', 'M6', 'OwnedFileServicePngBytesAndProvenanceWithLegacyAuthoringOutputs', 'ReviewedPartialMigration'
        if path == 'scripts/package_survivors.py':
            return 'tools/survivors', 'M6', 'FileServiceInventoryWithLegacyCopyArchiveAndHostMetadata', 'ReviewedPartialMigration'
        if path == 'scripts/compile_shaders.py':
            return 'tools/shader', 'M6', 'ExplicitNativeMainFileServiceInputsWithRetainedLegacyHelpersAndCompilerProcessFileBridge', 'ReviewedPartialMigration'
        if path == 'scripts/bindless_codegen.py':
            return 'tools/shader', 'M6', 'ScopedFileServiceAuthoredLibrarySchemaArtifactAndProvenanceInputsWithLegacyOutputs', 'ReviewedPartialMigration'
        if path == 'scripts/webgl_shader_artifact.py':
            return 'tools/shader', 'M6', 'OwnedVerifiedSpirvSharedByLayoutAndCrossWithFileServiceProvenanceAndLegacyOutputs', 'ReviewedPartialMigration'
        if path == 'scripts/spirv_cross_api.py':
            return 'tools/shader', 'M6', 'ExplicitOwnedSpirvBytesWithRetainedLegacyPathAndNativeLibraryBoundary', 'ReviewedPartialMigration'
        if path == 'scripts/gpu_visibility_codegen.py':
            return 'tools/shader', 'M6', 'ExplicitFileServiceClosedSchemaAndAuthoredSourceWithRetainedLegacyPathInterface', 'ReviewedPartialMigration'
        if path == 'scripts/shader_file_inputs.py':
            return 'tools/shader', 'M6', 'OwnedFileServiceBytesWithOriginalUtf8BomAndUniversalNewlinePolicy', 'ReviewedSourceMigration'
        if path in {'scripts/shader_bindings_codegen.py', 'scripts/spirv_layout.py'}:
            return 'tools/shader', 'M6', 'ExplicitOwnedSpirvInputWithRetainedLegacyPathInterfaceAndHeaderOutputs', 'ReviewedPartialMigration'
        if path == 'scripts/tool_file_inventory.py':
            return 'tools/io', 'M6', 'SharedBoundedNativeFileServiceProtocolWithRemainingHostPathObservations', 'ReviewedPartialMigration'
        if path == 'scripts/survivors_package_inventory.py':
            return 'tools/survivors', 'M6', 'ExplicitSurvivorsWireIdentityAdapterToSharedFileServiceClient', 'ReviewedSourceMigration'
        if path in {'tools/io/file_inventory.cpp', 'tools/io/file_inventory.h'}:
            return 'tools/io', 'M6', 'SharedBoundedOrderedNativeHostFileServiceReceiptsAndExplicitPayloads', 'ReviewedSourceMigration'
        if path in {'tools/io/file_inventory_cli.cpp', 'tools/io/file_inventory_main.cpp'}:
            return 'tools/io', 'M6', 'BoundedYamlStdinStdoutProtocolWithNativeFileServiceConsumer', 'ReviewedProtocolTransport'
        if path in {'tools/survivors/package_file_inventory.cpp', 'tools/survivors/package_file_inventory.h'}:
            return 'tools/survivors', 'M6', 'ExplicitSurvivorsWireIdentityAdapterToSharedFileInventory', 'ReviewedSourceMigration'
        if path == 'tools/survivors/package_inventory_main.cpp':
            return 'tools/survivors', 'M6', 'ExplicitSurvivorsCliIdentityWithSharedProtocolTransport', 'ReviewedProtocolTransport'
        if path in {'modules/asset/asset_module.cpp', 'modules/asset/asset_module.h'}:
            return 'modules/asset', 'M6', 'SourceOnlyAnimatedBytesWithLegacyGlbFbxImageAndPackageHostEntries', 'ReviewedPartialMigration'
        if path == 'tools/unity_import/glb_importer.arp':
            return 'tools/unity_import', 'M6', 'DeclaredToolProfileAndBoundedImmutableLocalByteAcquisition', 'ReviewedSourceMigration'
        if path in {'tools/cloud_asset/cloud_cook_session.cpp',
                    'tools/cloud_asset/cloud_cook_session.h'}:
            return 'tools/cloud_asset', 'M6', 'FileServiceTextAndObservedBaselinePublicationWithoutDomainRecookDuplication', 'ReviewedSourceMigration'
        if path in {'tools/cloud_asset/main.cpp', 'tools/cloud_asset/cloud_cook_application.cpp',
                    'tools/cloud_asset/cloud_cook_application.h'}:
            return 'tools/cloud_asset', 'M6', 'ExplicitNativeOrHttpColdHostWindowsSaveValidatedOtherPlatformsUnsupported', 'ReviewedPartialMigration'
        if path in {'src/resource/mesh_asset_resources.cpp', 'src/resource/mesh_asset_resources.h'}:
            return 'resource', 'M5', 'ExplicitBoundedGlbSourcesWithLegacyColdNativeOverload', 'ReviewedPartialMigration'
        if path in {'src/resource/static_mesh_shadow_lod_resource.cpp',
                    'src/resource/static_mesh_shadow_lod_resource.h'}:
            return 'resource', 'M5', 'StagedShadowSourcesWithExplicitLegacyColdNativePath', 'ReviewedPartialMigration'
        if path in {'src/resource/vegetation_catalog_build_service.cpp',
                    'src/resource/vegetation_catalog_build_service.h'}:
            return 'resource', 'M5', 'SourceOnlyCatalogPreparationWithRemainingNativePackagePublication', 'ReviewedPartialMigration'
        if path in {'src/assets/glb_importer_common.cpp', 'src/assets/glb_importer_common.h'}:
            return 'assets/mesh', 'M6', 'BoundedUtf8JsonWithRemainingLegacyFileReaders', 'ReviewedPartialMigration'
        if path in {'src/assets/glb_static_mesh_importer.cpp', 'src/assets/glb_static_mesh_importer.h'}:
            return 'assets/mesh', 'M6', 'TypedBoundedStaticSceneAndNamedSourcesWithRemainingLegacyFileImporters', 'ReviewedPartialMigration'
        if path == 'src/assets/mesh/glb_accessor_table.h':
            return 'assets/mesh', 'M6', 'BorrowedBoundedGltfAccessorDataNoFileIo', 'ReviewedCpuPreparation'
        if path == 'src/assets/mesh/glb_animated_node_graph.h':
            return 'assets/mesh', 'M6', 'BoundedGltfForestSkinAndInstancePreparationNoFileIo', 'ReviewedCpuPreparation'
        if path == 'src/assets/mesh/glb_animation_track_table.h':
            return 'assets/mesh', 'M6', 'BoundedGltfLinearTrackAndKeyValidationNoFileIo', 'ReviewedCpuPreparation'
        if path == 'src/assets/mesh/glb_skeletal_source.h':
            return 'assets/mesh', 'M6', 'QuotaBoundedNodeSkinPaletteAndClipDecodeNoFileIo', 'ReviewedCpuPreparation'
        if path in {'src/assets/glb_animated_mesh_importer.cpp',
                    'src/assets/glb_animated_mesh_importer.h'}:
            return 'assets/mesh', 'M6', 'ExplicitLegacyFileImporterRetainedBesideBoundedAnimatedByteSource', 'ReviewedPartialMigration'
        if path in {'src/assets/animated_mesh_source_import.cpp',
                    'src/assets/animated_mesh_source_import.h'}:
            return 'assets/mesh', 'M6', 'QuotaBoundedAnimatedByteDecodeNoFileIo', 'ReviewedSourceMigration'
        if path == 'examples/build_a_queen/scene_content.cpp':
            return 'examples/build_a_queen', 'M7', 'PreparedManifestAndCharacterBytesWithExplicitColdHostEntryAndLegacyStaticProjection', 'ReviewedPartialMigration'
        if path in {'examples/build_a_queen/character_sources.h',
                    'examples/build_a_queen/character_sources.cpp'}:
            return 'examples/build_a_queen', 'M7', 'DeclaredTwoCharacterMountedBindingsAndOwnedPreparedBytesNoFileIo', 'ReviewedCpuPreparation'
        if path in {'examples/build_a_queen/character_source_task.h',
                    'examples/build_a_queen/character_source_task.cpp'}:
            return 'examples/build_a_queen', 'M7', 'SequentialMountedManifestAndDeclaredCharacterReadsWithCancellationAndRetainedCallbackState', 'ReviewedSourceMigration'
        if path == 'examples/build_a_queen/main.cpp':
            return 'examples/build_a_queen', 'M7', 'ExplicitNativeOrHttpCharacterShaderAndDeclaredHudStartupBeforeGpu', 'ReviewedSourceMigration'
        if path == 'examples/survivors/main.cpp':
            return 'examples/survivors', 'M7', 'ExplicitNativeOrHttpGameplayContentAndMountedRecordSessionWithRemainingColdShader', 'ReviewedPartialMigration'
        if path in {'examples/common/native_startup_files.h',
                    'examples/common/native_startup_files.cpp'}:
            return 'examples/common', 'M7', 'SharedNativeDirectoryOrEngineHttpProfileWithExplicitLocalRecordSaveAdmission', 'ReviewedSourceMigration'
        if path == 'examples/common/null_smoke_shader_package.h':
            return 'examples/common', 'M7', 'ExplicitCompilerPublicationForColdNullValidatorFixtureOnly', 'ExplicitBoundary'
        if path in {'editor/viewport/editor_scene_viewport_environment.cpp',
                    'editor/viewport/editor_scene_viewport_shader_package.cpp',
                    'editor/viewport/editor_scene_viewport_shader_package.h',
                    'editor/viewport/editor_scene_viewport_sprite_renderer.cpp'}:
            return 'editor/viewport', 'M4', 'NativeViewportPreparedOwnersAndStrictInputValidationWithExplicitLegacyColdEntry', 'ReviewedPartialMigration'
        if path in {'editor/app/editor_webgl_application.cpp', 'editor/web/browser_rendering.h'}:
            return 'editor/web', 'M4', 'AsyncPreparedUiAndOriginalEs100SourcesWithDefaultWorkspaceAndFixturePreloadRetainedProductExecutionUnvalidated', 'ReviewedPartialMigration'
        if path in {'editor/web/editor_web_startup_sources.cpp', 'editor/web/editor_web_startup_sources.h'}:
            return 'editor/web', 'M4', 'SequentialOriginalProgramAndModuleSourceClosureWithExplicitWebFontAliasesAndImmutableProjection', 'ReviewedSourceMigration'
        if path in {'editor/app/editor_web_startup_host.cpp', 'editor/app/editor_web_startup_host.h'}:
            return 'editor/app', 'M4', 'BoundedAsyncStartupMountsAndCallbackRetirementWithoutBrowserBusyWait', 'ReviewedSourceMigration'
        if path in {'editor/ui/editor_ui_content.cpp', 'editor/ui/editor_ui_content.h',
                    'editor/ui/editor_ui_content_preparation.h'}:
            return 'editor/ui', 'M4', 'SharedBoundedUiAtlasAndFontBytePreparationWithExplicitNativeOrWebProfileSemantics', 'ReviewedCpuPreparation'
        if path in {'editor/ui/editor_ui_sources.cpp', 'editor/ui/editor_ui_sources.h'}:
            return 'editor/ui', 'M4', 'DeclaredMountedUiDocumentAtlasFourFontSourcesAndGuiOwnerWithoutColdReads', 'ReviewedSourceMigration'
        if path in {'editor/ui/editor_ui_assets.cpp', 'editor/ui/editor_ui_assets.h'}:
            return 'editor/ui', 'M4', 'FileServiceLegacyAtlasBytesAndPreparedInputsWithRemainingLegacyFontHostIo', 'ReviewedPartialMigration'
        if path == 'editor/tests/legacy_atlas_io_tests.cpp':
            return 'editor/tests', 'M4', 'FormalLegacyAtlasBuilderCpuBytesAndNativeOrMemfsFileOracle', 'ReviewedTestFixture'
        if path == 'editor/cli/main.cpp':
            return 'editor/cli', 'M4', 'NativeBootstrapFileServiceBytesAndConsoleOutputWithOriginalInterpreter', 'ReviewedSourceMigration'
        if path in {'editor/cli/cli_source.h', 'editor/cli/cli_source.cpp'}:
            return 'editor/cli', 'M4', 'BoundedNativeOnlyBootstrapWithRetiredFileServiceAndByteExactScriptVmInput', 'ReviewedSourceMigration'
        if path in {'editor/workspace/editor_shell.cpp', 'editor/workspace/editor_shell.h'}:
            return 'editor/workspace', 'M4', 'ExplicitPreparedUiTextAndSharedInitializationWithRemainingLegacyWorkspaceMetadata', 'ReviewedPartialMigration'
        if path in {'editor/app/editor_startup_resource_host.cpp', 'editor/app/editor_startup_resource_host.h',
                    'editor/app/editor_ui_source_host.cpp', 'editor/app/editor_ui_source_host.h',
                    'editor/app/editor_viewport_shader_host.cpp', 'editor/app/editor_viewport_shader_host.h'}:
            return 'editor/app', 'M4', 'OneShotDeclaredNativeOrHttpStartupMountsAndRetiredGraphCallbacksBeforePublication', 'ReviewedSourceMigration'
        if path in {'editor/tests/ui_sources_tests.cpp', 'editor/tests/ui_sources_http_smoke.py',
                    'editor/tests/ui_source_host_http_smoke.py', 'editor/tests/fixtures/ui_source_files.py',
                    'editor/tests/ui_source_identity.py', 'editor/tests/ui_source_lifecycle.cpp',
                    'editor/tests/ui_sources_browser_smoke.py'}:
            return 'editor/tests', 'M4', 'ExplicitOriginalUiFontAndShaderBytesOracleWithIsolatedHttpFaultsAndColdComparison', 'ReviewedTestFixture'
        if path == 'examples/build_a_queen/webgl_main.cpp':
            return 'examples/build_a_queen', 'M7', 'ExplicitFetchCharacterShaderAndFontStartupBeforeGpuNoMemfsPreload', 'ReviewedPartialMigration'
        if path in {'examples/build_a_queen/native_shader_source_task.h',
                    'examples/build_a_queen/native_shader_source_task.cpp'}:
            return 'examples/build_a_queen', 'M7', 'SixDeclaredNativeShaderResourcesThroughStartupGraphAndImmutableGeneratedOwners', 'ReviewedSourceMigration'
        if path in {'examples/build_a_queen/native_hud_source_task.h',
                    'examples/build_a_queen/native_hud_source_task.cpp'}:
            return 'examples/build_a_queen', 'M7', 'DeclaredAtlasFontAndImageThroughOriginalResourceGraphWithOwnedPreparedFace', 'ReviewedSourceMigration'
        if path == 'src/renderer/renderer_common.h':
            return 'renderer', 'M2', 'PureGraphicsShaderIntentAndExplicitPreparedOwnerWithRetainedConventionalColdEntry', 'ReviewedPartialMigration'
        if path in {'src/renderer/shader_package_reflection.h',
                    'src/renderer/shader_package_reflection.cpp'}:
            return 'renderer', 'M2', 'ImmutableOwnerOrAcquiredSourcePreparationWithExplicitConventionalColdOverloads', 'ReviewedPartialMigration'
        if path in {'src/renderer/postprocess/ssr_renderer.h',
                    'src/renderer/postprocess/ssr_temporal_renderer.h',
                    'src/renderer/gi/ddgi_renderer.h',
                    'src/renderer/visibility/depth_pyramid_renderer.cpp',
                    'src/renderer/lighting/forward_plus_feature.cpp',
                    'src/renderer/rhi_production_frame.cpp'}:
            return 'renderer', 'M2', 'ExplicitConventionalColdShaderHydrationDuringPipelineCreation', 'ReviewedPartialMigration'
        if path == 'src/renderer/ui/gui_renderer.h':
            return 'renderer/ui', 'M2', 'PreparedShaderPublicationOwnerGateBeforeGpuWithExplicitConventionalColdEntry', 'ReviewedPartialMigration'
        if path == 'src/renderer/scene_2d_renderer.cpp':
            return 'renderer', 'M2', 'PreparedNativeCreateValidatesOwnerAndPushAbiBeforeGpuWithRetainedColdCreate', 'ReviewedPartialMigration'
        if path in {'src/renderer/postprocess/taa_renderer.h',
                    'src/renderer/postprocess/fxaa_renderer.h',
                    'src/renderer/postprocess/ssao_renderer.h',
                    'src/renderer/postprocess/display_transfer_renderer.h'}:
            return 'renderer/postprocess', 'M2', 'PreparedCreateValidatesImmutableShaderAndPushAbiBeforeGpuWithRetainedColdCreate', 'ReviewedPartialMigration'
        if path == 'src/renderer/particles/particle_renderer.h':
            return 'renderer/particles', 'M2', 'CreatePreparesSpriteTrailAndOptionalMeshUsingConventionalColdShaderHydration', 'ReviewedPartialMigration'
        if path in {'src/renderer/volumetric_fog/volumetric_fog_feature.cpp',
                    'src/renderer/volumetric_cloud/volumetric_cloud_feature.cpp'}:
            return 'renderer', 'M2', 'NativePipelinePreparationRetainsConventionalColdGraphicsAndComputeHydration', 'ReviewedPartialMigration'
        if path == 'src/gui/gui.cpp':
            return 'gui', 'M2', 'DebugFontAtlasPlanStillDiscoversSystemFontThroughLegacyDefaultEntry', 'ReviewedPartialMigration'
        if path == 'examples/build_a_queen/hud_renderer.cpp':
            return 'examples/build_a_queen', 'M7', 'ImmutableShaderFaceAndAtlasInputsWithoutLocalOrSystemFontFallback', 'ReviewedSourceMigration'
        if path == 'examples/build_a_queen/scene_renderer.cpp':
            return 'examples/build_a_queen', 'M7', 'PreparedShaderAndCharacterHostPathWithRetainedExplicitColdCreateOverloads', 'ReviewedPartialMigration'
        if path in {'examples/volumetric_fog/main.cpp', 'examples/volumetric_cloud/main.cpp'}:
            return 'examples', 'M7', 'ConventionalNativeDemoPipelineCreationRetainsColdShaderHydration', 'ReviewedPartialMigration'
        if path in {'examples/build_a_queen/web_font_source_task.h',
                    'examples/build_a_queen/web_font_source_task.cpp'}:
            return 'examples/build_a_queen', 'M7', 'ExplicitBitmapFontBindingThroughResourceGraphAndOriginalSourceOnlyCpuDecoders', 'ReviewedSourceMigration'
        if path == 'examples/build_a_queen/startup_resource_task.h':
            return 'examples/build_a_queen', 'M7', 'SharedExampleStartupAliasesWithOriginalQueenDiagnosticPolicy', 'ReviewedSourceMigration'
        if path in {'examples/common/startup_resource_task.h',
                    'examples/common/startup_resource_task.cpp'}:
            return 'examples/common', 'M7', 'BoundedExampleStartupGraphOwnerAndRetainedCallbacksWithExplicitHostDiagnostics', 'ReviewedSourceMigration'
        if path in {'examples/survivors/content_source_task.h', 'examples/survivors/content_source_task.cpp'}:
            return 'examples/survivors', 'M7', 'DeclaredTenContentInputsThroughSourceOnlyGraphAndRetainedTypedRegistryOwner', 'ReviewedSourceMigration'
        if path in {'examples/survivors/content.h', 'examples/survivors/content.cpp'}:
            return 'examples/survivors', 'M7', 'SharedOriginalTypedContentParserWithPreparedAndExplicitLegacyColdEntries', 'ReviewedPartialMigration'
        if path == 'examples/common/native_startup_wait.h':
            return 'examples/common', 'M7', 'BoundedColdTaskWaitAndIoRetirementWithoutDomainParsing', 'ReviewedSourceMigration'
        if path in {'examples/survivors/record_session.h', 'examples/survivors/record_session.cpp'}:
            return 'examples/survivors', 'M7', 'OriginalRecordSchemaWithMountedFileServiceAndObservedByteBaselineNoRemoteReceiptProfile', 'ReviewedSourceMigration'
        if path in {'examples/survivors/gameplay_source_task.h',
                    'examples/survivors/gameplay_source_task.cpp'}:
            return 'examples/survivors', 'M7', 'ExplicitGameplayYamlAndOrderedScriptsThroughBoundedSourceOnlyStartupGraph', 'ReviewedSourceMigration'
        if path in {'examples/survivors/gameplay_text.h',
                    'examples/survivors/gameplay_text.cpp'}:
            return 'examples/survivors', 'M7', 'PureOriginalGameplayDecodersWithExplicitLegacyColdOfflineEntry', 'ReviewedPartialMigration'
        if path in {'examples/survivors/shader_source_task.h',
                    'examples/survivors/shader_source_task.cpp'}:
            return 'examples/survivors', 'M7', 'SingleDeclaredSpriteShaderThroughOriginalResourceGraphWithRetiredIoCustody', 'ReviewedSourceMigration'
        if path in {'examples/build_a_queen/web_shader_source_task.h',
                    'examples/build_a_queen/web_shader_source_task.cpp'}:
            return 'examples/build_a_queen', 'M7', 'OriginalShaderResourcePlansThroughResourceGraphAndOwnedAcquiredSourceSnapshot', 'ReviewedSourceMigration'
        if path in {'src/resource/vegetation_catalog_resource.cpp',
                    'src/resource/vegetation_catalog_resource.h'}:
            return 'resource', 'M5', 'CookedAndStagedV2CatalogSourcesWithExplicitLegacyNativeEntry', 'ReviewedPartialMigration'
        if path in {'src/resource/environment_asset_resources.cpp',
                    'src/resource/environment_asset_resources.h'}:
            return 'resource', 'M5', 'NamedMeshAndExplicitPlantRockLodSourcesWithLegacyTerrainAndColdHostIo', 'ReviewedPartialMigration'
        if path in {'src/resource/environment_lod_sources.cpp',
                    'src/resource/environment_lod_sources.h'}:
            return 'resource', 'M5', 'VersionPinnedSourceOnlyLodPrefixPreparation', 'ReviewedSourceMigration'
        if path in {'src/2d/sprite_resources.cpp', 'src/2d/tile_resources.cpp'}:
            return '2d', 'M5', 'ExplicitOriginTextAndPngSourcesWithNativeColdDefault', 'ReviewedSourceMigration'
        if path in {'src/3d/bvh.cpp', 'src/3d/bvh.h',
                    'src/3d/terrain/terrain_surface_instance.h',
                    'src/3d/terrain/terrain_surface_physics_adapter.h'}:
            return '3d', 'M5', 'ReviewedGeometricQueryIdentifiersNotFileIo', 'ReviewedNonIoMatches'
        if path == 'src/audio/audio_system.cpp':
            return 'audio', 'M5', 'ReviewedAtomicRingReadIndexNotFileIo', 'ReviewedNonIoMatches'
        cold_editor_boundaries = {
            'editor/workspace/editor_recovery_preparation.cpp': 'FileServiceFormalObservationsWithOriginalResourceAndScriptColdChecks',
            'editor/workspace/editor_recovery_region.cpp': 'FileServiceMetadataBytesWithNativeEnumerationAndAtomicCreationRemaining',
            'editor/workspace/editor_recovery_session.cpp': 'FileServiceDiscardBaselineWithNativeAtomicStoreAndRemovalRemaining',
            'editor/workspace/editor_scene_publication_preparation.cpp': 'FileServiceSnapshotsWithOriginalResourceClosureInspectionRemaining',
            'editor/workspace/editor_shell_scene_persistence.cpp': 'FileServiceManifestGateWithOriginalNativeStoreRemaining',
            'editor/workspace/editor_shell_project.cpp': 'FileServiceManifestGateWithNativeCreationAndAtomicSaveRemaining',
        }
        if path in cold_editor_boundaries:
            return 'editor/workspace', 'M4', cold_editor_boundaries[path], 'ReviewedPartialMigration'
        if path == 'editor/project/project_input_snapshot.cpp':
            return 'editor/project', 'M4', 'LogicalContentIdentityAndFileServiceColdHostBytesWithRemainingNativeRootChecks', 'ReviewedPartialMigration'
        if path in {'editor/io/editor_cold_host_read.cpp', 'editor/io/editor_cold_host_read.h'}:
            return 'editor/io', 'M4', 'SingleInlineHostFileServiceRequestAndRetiredCompletionWithoutNativeAsyncOrNetworkClaim', 'ReviewedSourceMigration'
        if path in {'editor/project/project_source_session.cpp', 'editor/project/project_source_session.h'}:
            return 'editor/project', 'M4', 'EpochBoundAcquisitionAndPreparedSaveReceiptPublication', 'ReviewedPartialMigration'
        if path in {'editor/project/project_file_read_task.cpp', 'editor/project/project_file_read_task.h'}:
            return 'editor/project', 'M4', 'BoundedOptionalLogicalReadWithActualMissingReceipt', 'ReviewedAcquisitionOnly'
        if path in {'editor/project/project_file_save_task.cpp', 'editor/project/project_file_save_task.h'}:
            return 'editor/project', 'M4', 'SingleSaveOrExplicitOriginalReceiptTaskWithBoundedQueryRelease', 'ReviewedSourceMigration'
        if path == 'editor/workspace/editor_shell_source_receipt.cpp':
            return 'editor/workspace', 'I6', 'OwnerDispatchedOpaqueReceiptObservationWithoutAuthoringPublication', 'ReviewedIoInfrastructure'
        if path in {'editor/remote/editor_file_receipt_wire.h',
                    'editor/app/editor_workspace_connection_profile.cpp',
                    'editor/app/editor_workspace_connection_profile.h'}:
            return 'editor', 'I6', 'ExplicitConnectionAndReceiptValuesWithoutTransportOrFileDiscovery', 'ReviewedIoInfrastructure'
        if path in {'editor/workspace/editor_scene_source_save_task.cpp', 'editor/workspace/editor_scene_source_save_task.h'}:
            return 'editor/workspace', 'M4', 'VerifiedSceneDestinationAcquisitionAndPreparedSave', 'ReviewedSourceMigration'
        if path in {'editor/workspace/editor_project_source_save_task.cpp',
                    'editor/workspace/editor_project_source_save_task.h',
                    'editor/workspace/editor_shell_project_source_save.cpp'}:
            return 'editor/workspace', 'M4', 'PreparedProjectSettingsSaveWithAllOwnerProvenancePublication', 'ReviewedSourceMigration'
        if path == 'editor/workspace/editor_shell_scene_sources.cpp':
            return 'editor/workspace', 'M4', 'MountedSceneLoadAndSaveWithOwnerAndReceiptGates', 'ReviewedSourceMigration'
        if path in {'editor/workspace/editor_scene_script_file_preparation.cpp', 'editor/workspace/editor_scene_script_file_preparation.h'}:
            return 'editor/workspace', 'M4', 'ExplicitNativeOrMountedColdSceneSavePreparation', 'ReviewedPartialMigration'
        if path in {'editor/workspace/script_document_session.cpp', 'editor/workspace/script_document_session.h'}:
            return 'editor/workspace', 'M4', 'MountedScriptSaveAndColdHostBytesWithNativePathAndAtomicStoreRemaining', 'ReviewedPartialMigration'
        if path == 'editor/web/browser_workspace.cpp':
            return 'editor/web', 'M4', 'FileServiceColdArchiveBytesWithNativeEnumerationAndAtomicStagingRemaining', 'ReviewedPartialMigration'
        if path in {'editor/workspace/editor_project_io_task.cpp', 'editor/workspace/editor_project_io_task.h',
                    'editor/workspace/editor_shell_project_io.cpp'}:
            return 'editor/workspace', 'M4', 'DeferredMountedScriptAndProjectSaveWithRetainedCommitReceipt', 'ReviewedPartialMigration'
        if path in {'editor/project/project_source_verification.cpp', 'editor/project/project_source_verification.h'}:
            return 'editor/project', 'M4', 'FiniteShaPinnedSourceRevalidation', 'ReviewedSourceVerification'
        if path == 'editor/workspace/editor_script_modules.cpp':
            return 'editor/workspace', 'M4', 'SharedColdPreparationWithNativeAndMountedInputs', 'ReviewedPartialMigration'
        if path in {'editor/project/project_resource_sources.cpp',
                    'editor/project/project_resource_locations_sources.cpp',
                    'editor/workspace/editor_project_resource_sources.cpp'}:
            return 'editor', 'M4', 'PreloadedProjectResourceCandidateWithEpochMountGates', 'ReviewedSourceMigration'
        if path == 'editor/workspace/editor_project_resources.cpp':
            return 'editor/workspace', 'M4', 'FileServiceColdManifestMetadataAndModuleBytesWithRemainingNativePackageAndPathBoundaries', 'ReviewedPartialMigration'
        if path == 'editor/project/project_package_snapshot.cpp':
            return 'editor/project', 'M4', 'FileServiceNativeFingerprintBytesWithRemainingResourceClosureAndPathChecks', 'ReviewedPartialMigration'
        if path == 'editor/project/project_resource_locations.cpp':
            return 'editor', 'M4', 'SharedNativeAndAcquiredResourceProvenance', 'ReviewedPartialMigration'
        if path == 'editor/workspace/editor_terrain_preview.cpp':
            return 'editor/workspace', 'M4', 'SharedBakerAndFileServiceLegacyCatalogWithExplicitNativeImporterRemaining', 'ReviewedPartialMigration'
        if path == 'editor/workspace/editor_terrain_build.cpp':
            return 'editor/workspace', 'M4', 'FileServiceColdTerrainManifestBytesWithNativeBuildOutputAndCommitRemaining', 'ReviewedPartialMigration'
        if path == 'editor/workspace/editor_project_resource_planner.cpp':
            return 'editor/workspace', 'M4', 'IncrementalDeclaredReadsOverAcquiredSourcesOnly', 'ReviewedSourcePlanning'
        if path == 'editor/workspace/editor_source_recovery_preparation.cpp':
            return 'editor/workspace', 'M4', 'VerifiedRecoveryInputsAndPassiveCandidateWithoutPublication', 'ReviewedAcquisitionOnly'
        if path in {'editor/workspace/editor_source_recovery_workspace.cpp', 'editor/workspace/editor_source_recovery_workspace.h'}:
            return 'editor/workspace', 'M4', 'CompleteColdSourcePacketWithFormalBytesAndDirectoryRevalidationWithoutLivePublication', 'ReviewedAcquisitionOnly'
        if path == 'editor/workspace/editor_source_recovery_capture.cpp':
            return 'editor/workspace', 'M4', 'OwnedAuthoringSnapshotWithFiniteSourceVerificationWithoutPublication', 'ReviewedAcquisitionOnly'
        if path == 'editor/workspace/editor_source_recovery_store.cpp':
            return 'editor/workspace', 'M4', 'AdmittedPerSessionTwoSlotSourceStoreWithoutGlobalRegionLease', 'ReviewedPartialMigration'
        if path == 'editor/workspace/editor_source_recovery_files.cpp':
            return 'editor/workspace', 'M4', 'BoundedFormalFileObservationsWithExplicitRecoveryConflicts', 'ReviewedAcquisitionOnly'
        if path == 'editor/workspace/editor_source_recovery_documents.cpp':
            return 'editor/workspace', 'M4', 'ObservedSourceTabsWithPinnedReacquisitionAndExplicitMissingBinding', 'ReviewedAcquisitionOnly'
        if path == 'editor/workspace/script_document_source_recovery.cpp':
            return 'editor/workspace', 'M4', 'SourceDocumentBindingsAndExplicitRecoveryAdmission', 'ReviewedSourceMigration'
        if path == 'editor/project/project_directory_session.cpp':
            return 'editor/project', 'M4', 'PagedDirectoryCandidateWithReceiptEpochAndMountGates', 'ReviewedAcquisitionOnly'
        if path == 'editor/workspace/project_browser_model.cpp':
            return 'editor/workspace', 'M4', 'PreparedMountedDirectoryModelWithRemainingNativeBrowsing', 'ReviewedPartialMigration'
        if path in {'editor/remote/editor_workspace_protocol.cpp', 'editor/remote/editor_workspace_protocol.h'}:
            return 'editor/remote', 'I6', 'AuthenticatedBoundedRpcProjectionOverExistingFileService', 'ReviewedIoInfrastructure'
        if path in {'editor/remote/editor_workspace_server.cpp', 'editor/remote/editor_workspace_server.h',
                    'editor/tools/workspace_service_main.cpp'}:
            return 'editor/remote', 'I6', 'ExplicitLoopbackRootHostUsingEngineHttpAndNativeDirectoryBackend', 'ReviewedIoInfrastructure'
        if path in {'editor/tests/workspace_service_cli_smoke.py',
                    'editor/tests/workspace_service_budget_tests.py',
                    'editor/tests/fixtures/workspace_service_fixture.h',
                    'editor/tests/fixtures/workspace_wire_host.h',
                    'editor/tests/workspace_backend_tests.cpp',
                    'editor/tests/workspace_source_host_tests.cpp',
                    'editor/tests/workspace_receipt_command_tests.cpp',
                    'editor/tests/workspace_connection_profile_tests.cpp',
                    'editor/tests/source_receipt_cli_tests.py'}:
            return 'editor/tests', 'I6', 'IsolatedActualWorkspaceServiceFixture', 'ReviewedTestFixture'
        if path in {'tests/fixtures/workspace_fault_proxy.py',
                    'tests/fixtures/editor_workspace_receipt_workflow.py',
                    'tests/fixtures/editor_workspace_connection_profile.py',
                    'tests/fixtures/editor_webgl_workspace_smoke.js',
                    'tests/contract/core_workspace_file_browser.py',
                    'tests/contract/editor_web_workspace_smoke.py'}:
            return 'tests/editor-workspace', 'I6', 'ActualBrowserHostIoOracleAndDeliveryFaultFixture', 'ReviewedTestFixture'
        if path == 'editor/tests/wocc_workspace_cli_smoke.py':
            return 'editor/tests', 'I7', 'ExplicitAdoptedWorkspaceNativeCliDiskHashOracle', 'ReviewedTestFixture'
        parts = Path(path).parts
        owner = '/'.join(parts[:2]) if parts[0] == 'src' else parts[0]
        if parts[0] in {'scripts', 'tools'}:
            return owner, 'M6', 'OfflineTooling', 'PendingMigration'
        if parts[0] in {'samples', 'examples', 'games'}:
            return owner, 'M7', 'HostOrRuntime', 'PendingMigration'
        batch = {'resource': 'M1', 'shader': 'M2', 'ui': 'M2', 'renderer': 'M2',
                 'scene': 'M3', 'scriptvm': 'M3', 'assets': 'M6', '2d': 'M5',
                 '3d': 'M5', 'audio': 'M5', 'core': 'I1'}.get(parts[1], 'M7')
        if parts[0] == 'editor':
            batch = 'M4'
        return owner, batch, 'ReviewCallerContext', 'PendingMigration'

    def collect(self):
        listed = subprocess.check_output(
            ['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'], cwd=self.root)
        files, diagnostics, scanned = [], [], 0
        reviews = None
        review_path = self.root / SemanticIoReviews.relative_path
        if review_path.is_file():
            try:
                reviews = SemanticIoReviews(review_path.read_text(encoding='utf-8'))
            except (ReviewDocumentError, UnicodeError, OSError) as error:
                diagnostics.append(dict(path=SemanticIoReviews.relative_path,
                                        diagnostic='IO-INVENTORY-REVIEW-DOCUMENT', detail=str(error)))
        visited = set()
        for name in sorted(set(listed.decode('utf-8').split('\0')) - {''}):
            path = Path(name)
            if (path.parts[0] not in self.roots or path.suffix not in self.suffixes
                    or self.excluded.intersection(path.parts) or not (self.root / path).is_file()):
                continue
            scanned += 1
            raw = (self.root / path).read_bytes()
            try:
                source = raw.decode('utf-8-sig')
            except UnicodeDecodeError as error:
                diagnostics.append(dict(path=name, diagnostic='IO-INVENTORY-SOURCE-ENCODING',
                                        byteOffset=error.start))
                continue
            references = self.references(source, path.suffix)
            review, issue = (reviews.check(name, raw, references) if reviews else (None, None))
            visited.add(name)
            if issue:
                diagnostics.append(dict(path=name, diagnostic=issue))
            if not references:
                continue
            owner, batch, context, status = self.classification(name)
            entry = dict(path=name, sha256=hashlib.sha256(raw).hexdigest(), owner=owner,
                         batch=batch, context=context, status=status, references=references)
            if review:
                entry.update({key: review[key] for key in ('owner', 'batch', 'context', 'status')})
                entry['semanticReview'] = dict(contractVersion=1, document=SemanticIoReviews.relative_path,
                                               scope='ExactSourceAndListedReferencesOnly',
                                               sourcePolicy=reviews.source_policy, sha256=review['sha256'],
                                               evidence=review['evidence'])
            files.append(entry)
        if reviews:
            diagnostics.extend(dict(path=name, diagnostic='IO-INVENTORY-REVIEW-SOURCE-MISSING')
                               for name in sorted(set(reviews.entries) - visited))
        return dict(type='UnifiedIoCallsiteInventory', contractVersion=1,
                    evidenceKind='LexicalReferencesRequireDomainReview',
                    snapshotKind='PerFileReadNoGlobalAtomicity', scanComplete=not diagnostics,
                    roots=sorted(self.roots), suffixes=sorted(self.suffixes),
                    counts=dict(scannedFiles=scanned, matchingFiles=len(files),
                                references=sum(len(f['references']) for f in files)),
                    diagnostics=diagnostics, files=files)

    @staticmethod
    def signature(document):
        return Counter((entry['path'], ref['symbol'], ref['operation'])
                       for entry in document['files'] for ref in entry['references'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path)
    parser.add_argument('--check', type=Path)
    args = parser.parse_args()
    inventory = UnifiedIoInventory(args.root)
    report = inventory.collect()
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(yaml.safe_dump(report, sort_keys=False), encoding='utf-8')
    if args.check:
        baseline = yaml.safe_load(args.check.read_text(encoding='utf-8'))
        added = inventory.signature(report) - inventory.signature(baseline)
        if added:
            print('IO-INVENTORY-UNREVIEWED-REFERENCES:', list(added.items()), file=sys.stderr)
            return 1
        reviewed = {entry['path'] for entry in baseline['files'] if entry.get('semanticReview')}
        current = {entry['path'] for entry in report['files'] if entry.get('semanticReview')}
        if reviewed - current:
            print('IO-INVENTORY-REVIEW-REGRESSION:', sorted(reviewed - current), file=sys.stderr)
            return 1
    print(yaml.safe_dump(report['counts'], sort_keys=False).strip())
    if not report['scanComplete']:
        print(yaml.safe_dump(report['diagnostics']), file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
