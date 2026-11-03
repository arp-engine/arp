"""Fixtures for reviewable first-party IO inventory evidence."""
import unittest
import contextlib
import copy
import hashlib
import io
from pathlib import Path
import tempfile
from unittest import mock

import yaml

from unified_io_inventory import UnifiedIoInventory, main
from unified_io_semantic_review import ReviewDocumentError, SemanticIoReviews


class UnifiedIoInventoryTests(unittest.TestCase):
    def test_plugin_compiler_audit_does_not_turn_physical_boundaries_into_io_exemptions(self):
        host = UnifiedIoInventory.classification('src/scriptvm/plugin/plugin_host_text_reader.cpp')
        self.assertEqual(host[-1], 'ReviewedPartialMigration')
        self.assertIn('BlockingAdapter', host[2])
        snapshot = UnifiedIoInventory.classification('src/scriptvm/plugin/immutable_build_input_snapshot.cpp')
        self.assertEqual(snapshot[:2], ('scriptvm/plugin', 'M3'))
        self.assertEqual(snapshot[-1], 'ReviewedPartialMigration')
        self.assertIn('UnboundedVersionText', snapshot[2])
        apple = UnifiedIoInventory.classification('src/scriptvm/plugin/detail/apple_clang_toolchain.h')
        self.assertEqual(apple[:2], ('scriptvm/plugin', 'M3'))
        self.assertEqual(apple[-1], 'ReviewedPartialMigration')
        self.assertIn('UnboundedQueryText', apple[2])

    def test_reflection_tool_does_not_hide_native_directory_or_transaction_limits(self):
        for name in ('main.cpp', 'reflection_tool_io.cpp'):
            scope = UnifiedIoInventory.classification('tools/reflect_codegen/' + name)
            self.assertEqual(scope[:2], ('tools/reflect_codegen', 'M6'))
            self.assertEqual(scope[-1], 'ReviewedSourceMigration')
        outputs = UnifiedIoInventory.classification('tools/io/native_tool_outputs.cpp')
        self.assertEqual(outputs[-1], 'ReviewedPartialMigration')
        self.assertIn('IndividualFileServiceSaves', outputs[2])
        self.assertIn('NativeDirectoryPreparation', outputs[2])

    def test_arp_comments_strings_and_floor_division_keep_real_rhs_io(self):
        source = ('# io.read_text("comment") query\r\n'
                  'var text: str = "# read_bytes // write_text"\r\n'
                  "var other: str = '# read_text' # exists()\r\n"
                  'var n = 8 // io.read_text(path) # write_text(hidden)\r\n'
                  'io.write_text(path, text)\r\n')
        self.assertEqual(self.symbols(source, '.arp'), [(4, 'read_text'), (5, 'write_text')])

    def test_reviewed_example_nonio_matches_stay_within_the_reviewed_shapes(self):
        root = Path(__file__).resolve().parents[2]
        paths = {
            'examples/build_a_queen/portable_renderer.cpp': ['read', 'read'],
            'examples/volumetric_cloud/prepared_offscreen.cpp': ['query', 'query', 'read'],
            'examples/survivors/assets/scripts/combat.arp': ['remove', 'remove', 'remove'],
        }
        for path, expected in paths.items():
            with self.subTest(path=path):
                self.assertEqual(UnifiedIoInventory.classification(path)[-1], 'ReviewedNonIoMatches')
                source = (root / path).read_text(encoding='utf-8')
                self.assertEqual([r['symbol'] for r in UnifiedIoInventory.references(source, Path(path).suffix)],
                                 expected)
        weapons = root / 'examples/survivors/assets/scripts/weapons.arp'
        self.assertEqual(UnifiedIoInventory.references(weapons.read_text(encoding='utf-8'), '.arp'), [])

    def test_survivors_codegen_preserves_namespace_and_save_transaction_boundaries(self):
        source = UnifiedIoInventory.classification('examples/survivors/script_codegen.cpp')
        self.assertEqual(source[-1], 'ReviewedSourceMigration')
        host = UnifiedIoInventory.classification('examples/survivors/codegen_io.cpp')
        self.assertEqual(host[-1], 'ReviewedPartialMigration')
        self.assertIn('IndividualSaves', host[2])
        self.assertIn('NativeDirectoryPreparation', host[2])

    def test_cloud_cook_source_and_host_platform_limits_stay_distinct(self):
        source = UnifiedIoInventory.classification('tools/cloud_asset/cloud_cook_session.cpp')
        self.assertEqual(source[:2], ('tools/cloud_asset', 'M6'))
        self.assertEqual(source[-1], 'ReviewedSourceMigration')
        host = UnifiedIoInventory.classification('tools/cloud_asset/cloud_cook_application.cpp')
        self.assertEqual(host[-1], 'ReviewedPartialMigration')
        self.assertIn('OtherPlatformsUnsupported', host[2])

    def test_tool_closure_does_not_hide_remaining_business_or_plugin_io(self):
        runner = UnifiedIoInventory.classification('tools/asset_importer/script_runner.cpp')
        self.assertEqual(runner[-1], 'ReviewedPartialMigration')
        self.assertIn('SealedSourceClosure', runner[2])
        self.assertIn('PluginRootQueries', runner[2])
        for name in ('tool_script_sources.cpp', 'tool_script_source_host.cpp'):
            owner = UnifiedIoInventory.classification('tools/asset_importer/' + name)
            self.assertEqual(owner[:2], ('tools/asset_importer', 'M6'))
            self.assertEqual(owner[-1], 'ReviewedSourceMigration')
        self.assertEqual(UnifiedIoInventory.classification('modules/io/io_module.cpp')[-1],
                         'ReviewedPartialMigration')

    def test_editor_shader_wrapper_preserves_visible_legacy_boundary(self):
        source = ('// hydrateEditorSceneViewportShaderPackage(fake);\n'
                  'const char* text = "hydrateEditorSceneViewportShaderPackage";\n'
                  'hydrateEditorSceneViewportShaderPackage(package, artifacts, diagnostic);\n'
                  'auto alias = &hydrateEditorSceneViewportShaderPackage;\n')
        self.assertEqual(self.symbols(source, '.cpp'),
                         [(3, 'hydrateEditorSceneViewportShaderPackage'),
                          (4, 'hydrateEditorSceneViewportShaderPackage')])
        context = UnifiedIoInventory.classification('editor/viewport/editor_scene_viewport_shader_package.cpp')
        self.assertEqual(context[-1], 'ReviewedPartialMigration')
        self.assertIn('PreparedOwners', context[2])

    def symbols(self, source, suffix):
        return [(item['line'], item['symbol'])
                for item in UnifiedIoInventory.references(source, suffix)]

    def test_cpp_literals_comments_and_real_operations(self):
        source = '''// ifstream fake;
auto literal = R"tag(readTextFile("none"))tag";
/* std::filesystem::exists(path); */
std::ifstream file(path);
auto size = std::filesystem::file_size(path);
'''
        self.assertEqual(self.symbols(source, '.cpp'), [(4, 'ifstream'), (5, 'file_size')])

    def test_python_inline_comments_and_multiline_literals(self):
        source = '''path.read_text() # read_bytes() exists()
text = """read_bytes()
read_text()"""
# fetch()
value = path.read_bytes()
'''
        self.assertEqual(self.symbols(source, '.py'), [(1, 'read_text'), (5, 'read_bytes')])

    def test_javascript_comment_and_string_contents(self):
        source = '''const name = "fetch('elsewhere')";
// readFile()
const response = await fetch(url);
'''
        self.assertEqual(self.symbols(source, '.js'), [(3, 'fetch')])

    def test_python_native_copy_and_archive_references_remain_visible(self):
        source = ("from shutil import copy2 as stage_file\n"
                  "shutil.copytree(source, output)\n"
                  "shutil.copyfile(source, output)\n"
                  "with zipfile.ZipFile(path, 'w') as archive: pass\n")
        self.assertEqual(self.symbols(source, '.py'),
                         [(1, 'copy2'), (2, 'copytree'), (3, 'copyfile'), (4, 'ZipFile')])
        refs = UnifiedIoInventory.references(source, '.py')
        self.assertEqual([r['operation'] for r in refs], ['write', 'write', 'write', 'read'])

    def test_copy_archive_names_in_literals_and_comments_do_not_create_io(self):
        source = ('# shutil.copy2(source, target)\n'
                  'description = "ZipFile copytree copyfile"\n'
                  'shutil.copy2(source, target)\n')
        self.assertEqual(self.symbols(source, '.py'), [(3, 'copy2')])
        before = {'files': [{'path': 'scripts/package.py', 'references': []}]}
        after = {'files': [{'path': 'scripts/package.py', 'references':
                            UnifiedIoInventory.references(source, '.py')}]}
        self.assertTrue(UnifiedIoInventory.signature(after) - UnifiedIoInventory.signature(before))

    def test_native_low_level_and_python_file_open(self):
        self.assertEqual(self.symbols('int fd = openat(root, path, flags);\nfstat(fd, &info);', '.cpp'),
                         [(1, 'openat'), (2, 'fstat')])
        self.assertEqual(self.symbols('with open(path) as file:\n    value = file.read()', '.py'),
                         [(1, 'open'), (2, 'read')])

    def test_save_receipt_entries_include_aliases_without_plain_value_fields(self):
        source = ('files.receipt(request, completion);\n'
                  'backend.receiptAsync(request, completion, queue);\n'
                  'auto method = &FileService::receipt;\n'
                  'FileSaveReceipt receipt;\n'
                  'result.receipt = receipt;\n')
        self.assertEqual(self.symbols(source, '.cpp'),
                         [(1, 'receipt'), (2, 'receiptAsync'), (3, 'receipt')])

    def test_text_service_and_current_package_ownership(self):
        self.assertEqual(self.symbols('service.readText(request, callback);', '.cpp'), [(1, 'readText')])
        self.assertEqual(self.symbols('files.query(request, completion);', '.cpp'), [(1, 'query')])
        self.assertEqual(UnifiedIoInventory.classification('src/core/io/file_service.cpp')[0], 'core/io')
        self.assertEqual(UnifiedIoInventory.classification('src/network/http/fetch_byte_transfer.cpp')[:2], ('network/http', 'I2'))
        self.assertEqual(UnifiedIoInventory.classification('src/resource/resource_graph_executor.cpp'),
                         ('resource', 'M1', 'BoundedMountedGraphOwnerAdmissionAndGenerationPinnedSingleReload', 'ReviewedOwnerAdmission'))
        self.assertEqual(UnifiedIoInventory.classification('src/resource/internal_asset_package_loader.cpp'),
                         ('resource', 'M1', 'ManifestFirstBoundedPackageAcquisition', 'ReviewedAcquisitionOnly'))
        self.assertEqual(UnifiedIoInventory.classification('src/resource/shader_artifact_resource.cpp'),
                         ('resource', 'M2', 'ShaderArtifactGraphSourceProjection', 'ReviewedAcquisitionOnly'))
        self.assertEqual(UnifiedIoInventory.classification('src/shader/shader_artifact_files.h')[-1], 'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification('src/ui/ui_document_resource.cpp'),
                         ('ui', 'M2', 'SourceOnlyDocumentAndPreparedMount', 'ReviewedSourceMigration'))
        self.assertEqual(UnifiedIoInventory.classification('src/ui/ui_document.cpp')[-1], 'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification('src/ui/ui_theme_resource.cpp'),
                         ('ui', 'M2', 'SourceOnlyThemeAtlasMetadataAndExplicitBinding', 'ReviewedSourceMigration'))
        self.assertEqual(UnifiedIoInventory.classification('src/ui/ui_atlas_image_resource.cpp'),
                         ('ui', 'M2', 'BoundedImageSourceAndMetadataPath', 'ReviewedSourceMigration'))
        self.assertEqual(UnifiedIoInventory.classification('src/ui/ui_font_source.cpp'),
                         ('ui', 'M2', 'ImmutableFontBytesWithExplicitColdHostAdapter', 'ReviewedSourceMigration'))
        self.assertEqual(UnifiedIoInventory.classification('src/renderer/ui/ui_text_layout.h'),
                         ('renderer/ui', 'M2', 'PreparedFaceMetricsAndKerningWithoutDefaultFontDiscovery',
                          'ReviewedSourceMigration'))

    def test_shader_inputs_retain_explicit_unmigrated_boundaries(self):
        for name in ('compile_shaders.py', 'shader_bindings_codegen.py', 'spirv_layout.py'):
            entry = UnifiedIoInventory.classification('scripts/' + name)
            self.assertEqual(entry[0], 'tools/shader')
            self.assertEqual(entry[-1], 'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification('scripts/shader_file_inputs.py')[-1],
                         'ReviewedSourceMigration')
        for name in ('bindless_codegen.py', 'webgl_shader_artifact.py', 'spirv_cross_api.py', 'gpu_visibility_codegen.py'):
            self.assertEqual(UnifiedIoInventory.classification('scripts/' + name)[-1],
                             'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification('scripts/webgl_shader_codegen.py')[-1],
                         'PendingMigration')

    def test_package_inventory_does_not_hide_remaining_native_copy_archive_io(self):
        self.assertEqual(UnifiedIoInventory.classification('scripts/package_survivors.py'),
                         ('tools/survivors', 'M6', 'FileServiceInventoryWithLegacyCopyArchiveAndHostMetadata',
                          'ReviewedPartialMigration'))
        self.assertEqual(UnifiedIoInventory.classification('scripts/tool_file_inventory.py')[-1],
                         'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification('scripts/survivors_package_inventory.py')[-1],
                         'ReviewedSourceMigration')
        self.assertEqual(UnifiedIoInventory.classification('tools/io/file_inventory.cpp')[0], 'tools/io')
        self.assertEqual(UnifiedIoInventory.classification('tools/io/file_inventory_cli.cpp')[-1],
                         'ReviewedProtocolTransport')
        self.assertEqual(UnifiedIoInventory.classification('tools/survivors/package_file_inventory.cpp')[-1],
                         'ReviewedSourceMigration')
        self.assertEqual(UnifiedIoInventory.classification('tools/survivors/package_inventory_main.cpp')[-1],
                         'ReviewedProtocolTransport')
        self.assertIn((1, 'rglob'), self.symbols("for path in root.rglob('*'): pass", '.py'))
        self.assertEqual(self.symbols('fread(bytes, 1, size, stdin);', '.cpp'), [(1, 'fread')])

    def test_authoring_input_migration_retains_image_wave_and_yaml_output_boundaries(self):
        entry = UnifiedIoInventory.classification('scripts/build_survivors_assets.py')
        self.assertEqual(entry, ('tools/survivors', 'M6',
                                'OwnedFileServicePngBytesAndProvenanceWithLegacyAuthoringOutputs',
                                'ReviewedPartialMigration'))
        self.assertEqual(self.symbols('Image.open(BytesIO(source.data))\nimage.save(output)\n'
                                      'config.write_text(yaml)\nwave.open(path, "wb")', '.py'),
                         [(1, 'open'), (2, 'save'), (3, 'write_text'), (4, 'open')])

    def test_baseline_counts_reject_growth_not_line_movement(self):
        def doc(count, line):
            return {'files': [{'path': 'src/domain/reader.cpp', 'references': [
                {'symbol': 'ifstream', 'operation': 'read', 'line': line}
            ] * count}]}
        before = UnifiedIoInventory.signature(doc(1, 4))
        self.assertFalse(UnifiedIoInventory.signature(doc(1, 9)) - before)
        self.assertTrue(UnifiedIoInventory.signature(doc(2, 9)) - before)

    def test_resource_wrappers_are_visible_and_partial_migrations_stay_partial(self):
        self.assertEqual(self.symbols(
            'readResourceSource(path, context, budget);\nreadResourceText(path, context, budget);',
            '.cpp'), [(1, 'readResourceSource'), (2, 'readResourceText')])
        self.assertEqual(self.symbols(
            'UiFontSource::loadHost(path, cap, diagnostic);\nacquireUiFontSource(desc, diagnostic);\nloadDefaultUiFontFace();',
            '.cpp'), [(1, 'loadHost'), (2, 'acquireUiFontSource'), (3, 'loadDefaultUiFontFace')])
        for path in ('src/resource/terrain_asset_resource.cpp',
                     'src/resource/vegetation_catalog_cooked.cpp',
                     'src/resource/internal_asset_package.cpp',
                     'src/resource/asset_package_closure.cpp',
                     'src/resource/asset_package_payload.cpp',
                     'src/resource/prefab_asset_resources.cpp',
                     'src/scene/scene_asset_package.cpp'):
            self.assertEqual(UnifiedIoInventory.classification(path)[-1], 'ReviewedPartialMigration')

    def test_cold_shader_wrappers_and_aliases_cannot_disappear_from_inventory(self):
        source = ('// makeGraphicsShaderPackage("fake", path, 0);\n'
                  'const char* text = "makeGraphicsShaderPackage";\n'
                  'auto package = detail::makeGraphicsShaderPackage(name, path, bytes);\n'
                  'auto cold = &renderer::detail::makeGraphicsShaderPackage;\n'
                  'prepareGraphicsShaderPackage(output, name, path, bytes, prepared);\n')
        self.assertEqual(self.symbols(source, '.cpp'),
                         [(3, 'makeGraphicsShaderPackage'), (4, 'makeGraphicsShaderPackage')])
        for path in ('src/renderer/scene_2d_renderer.cpp',
                     'src/renderer/postprocess/taa_renderer.h',
                     'src/renderer/postprocess/fxaa_renderer.h',
                     'src/renderer/postprocess/ssao_renderer.h',
                     'src/renderer/postprocess/display_transfer_renderer.h',
                     'src/renderer/particles/particle_renderer.h',
                     'src/renderer/volumetric_fog/volumetric_fog_feature.cpp',
                     'src/renderer/volumetric_cloud/volumetric_cloud_feature.cpp',
                     'src/gui/gui.cpp'):
            self.assertEqual(UnifiedIoInventory.classification(path)[-1], 'ReviewedPartialMigration')

    def test_hydration_overloads_remain_candidates_until_context_review(self):
        source = ('// hydrateShaderPackageReflectionFromArtifact(fake);\n'
                  'const char* text = "hydrateShaderPackageReflectionFromArtifact";\n'
                  'hydrateShaderPackageReflectionFromArtifact(package);\n'
                  'auto cold = &renderer::hydrateShaderPackageReflectionFromArtifact;\n'
                  'hydrateShaderPackageReflectionFromArtifact(package, prepared, diagnostic);\n')
        # The lexical inventory must retain both overloads. Only domain review
        # distinguishes immutable-owner consumption from the cold local profile.
        self.assertEqual(self.symbols(source, '.cpp'),
                         [(3, 'hydrateShaderPackageReflectionFromArtifact'),
                          (4, 'hydrateShaderPackageReflectionFromArtifact'),
                          (5, 'hydrateShaderPackageReflectionFromArtifact')])
        for path in ('src/renderer/shader_package_reflection.cpp',
                     'src/renderer/postprocess/ssr_renderer.h',
                     'src/renderer/postprocess/ssr_temporal_renderer.h',
                     'src/renderer/gi/ddgi_renderer.h',
                     'src/renderer/visibility/depth_pyramid_renderer.cpp',
                     'src/renderer/lighting/forward_plus_feature.cpp',
                     'src/renderer/rhi_production_frame.cpp',
                     'examples/survivors/main.cpp'):
            self.assertEqual(UnifiedIoInventory.classification(path)[-1], 'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification(
            'examples/common/null_smoke_shader_package.h')[-1], 'ExplicitBoundary')

    def test_editor_prepared_ui_does_not_close_cold_or_web_paths(self):
        for path in ('editor/ui/editor_ui_assets.cpp', 'editor/workspace/editor_shell.cpp'):
            self.assertEqual(UnifiedIoInventory.classification(path)[-1], 'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification('editor/ui/editor_ui_sources.cpp')[-1],
                         'ReviewedSourceMigration')
        self.assertEqual(UnifiedIoInventory.classification('editor/tests/ui_sources_tests.cpp')[-1],
                         'ReviewedTestFixture')
        self.assertEqual(self.symbols('readResourceText(path, context, budget);', '.cpp'),
                         [(1, 'readResourceText')])

    def test_memory_audio_does_not_hide_the_legacy_file_import_boundary(self):
        source = ('// importAudioFile("fake", cap);\n'
                  'importAudioMemory(source);\n'
                  'auto old = &audio::importAudioFile;\n'
                  'decodeAudioFileToRuntimePcm(path, budget);\n'
                  'ma_decoder_init_file(path, config, decoder);\n')
        self.assertEqual(self.symbols(source, '.cpp'),
                         [(3, 'importAudioFile'), (4, 'decodeAudioFileToRuntimePcm'),
                          (5, 'ma_decoder_init_file')])
        self.assertEqual(UnifiedIoInventory.classification('src/audio/miniaudio_backend.cpp')[-1],
                         'ReviewedPartialMigration')

    def test_animated_byte_decode_does_not_close_legacy_or_sample_migration(self):
        self.assertEqual(UnifiedIoInventory.classification('src/assets/animated_mesh_source_import.cpp'),
                         ('assets/mesh', 'M6', 'QuotaBoundedAnimatedByteDecodeNoFileIo', 'ReviewedSourceMigration'))
        self.assertEqual(UnifiedIoInventory.classification('src/assets/glb_animated_mesh_importer.cpp')[-1],
                         'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification('examples/build_a_queen/scene_content.cpp')[-1],
                         'ReviewedPartialMigration')
        self.assertEqual(self.symbols('io.read_binary_local(path, cap, allocation);', '.arp'),
                         [(1, 'read_binary_local')])
        self.assertEqual(UnifiedIoInventory.classification('modules/io/io_module.cpp')[-1],
                         'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification('modules/asset/asset_module.cpp')[-1],
                         'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification('tools/unity_import/glb_importer.arp')[-1],
                         'ReviewedSourceMigration')

    def test_tool_cold_host_does_not_imply_formal_ui_publication(self):
        self.assertEqual(UnifiedIoInventory.classification('tools/io/cold_tool_files.cpp')[-1],
                         'ReviewedSourceMigration')
        for path in ('tools/asset_importer/document_import.cpp',
                     'tools/asset_importer/asset_importer.h'):
            self.assertEqual(UnifiedIoInventory.classification(path)[-1], 'ReviewedPartialMigration')

    def test_remaining_tool_review_does_not_claim_runtime_migration(self):
        writer = UnifiedIoInventory.classification('tools/ai_asset_generator/ai_asset_generator.h')
        self.assertEqual(writer[-1], 'ReviewedSourceMigration')
        self.assertIn('NotNetworkGeneration', writer[2])
        preview = UnifiedIoInventory.classification('tools/cluster_previewer/cluster_preview_asset.cpp')
        self.assertEqual(preview[-1], 'ReviewedPartialMigration')
        self.assertIn('IndirectLegacyGlbFileRead', preview[2])

    def test_shader_tool_acquisition_does_not_mint_execution_authority(self):
        for path in ('tools/shader/fragment_atomic_write_verifier.cpp',
                     'tools/shader/spirv_verifier_source.cpp'):
            entry = UnifiedIoInventory.classification(path)
            self.assertEqual(entry[-1], 'ReviewedSourceMigration')
            self.assertIn('WithoutChangingProofOrExecutionAuthority', entry[2])

    def test_aot_cli_review_preserves_multi_output_and_host_identity_gaps(self):
        entry = UnifiedIoInventory.classification('tools/script_aotc/main.cpp')
        self.assertEqual(entry[-1], 'ReviewedPartialMigration')
        self.assertIn('NativeHostFileServiceSources', entry[2])
        self.assertIn('RemainingLegacyOutput', entry[2])
        self.assertEqual(UnifiedIoInventory.classification('tools/script_aotc/aot_cli_sources.cpp')[-1],
                         'ReviewedSourceMigration')

    def test_editor_converter_file_io_does_not_hide_native_namespace_boundary(self):
        self.assertEqual(UnifiedIoInventory.classification('editor/tools/migration_io.cpp')[-1],
                         'ReviewedSourceMigration')
        for path in ('editor/tools/project_migrate.cpp', 'editor/tools/scene_migrate.cpp'):
            entry = UnifiedIoInventory.classification(path)
            self.assertEqual(entry[-1], 'ReviewedPartialMigration')
            self.assertIn('NativeDirectoryPreparationAndExclusivePublication', entry[2])

    def test_cli_bootstrap_migration_does_not_exempt_other_cli_files(self):
        for path in ('editor/cli/main.cpp', 'editor/cli/cli_source.cpp'):
            self.assertEqual(UnifiedIoInventory.classification(path)[-1], 'ReviewedSourceMigration')
        self.assertNotEqual(UnifiedIoInventory.classification('editor/cli/other.cpp')[-1],
                            'ReviewedSourceMigration')
        root = Path(__file__).resolve().parents[2]
        references = self.symbols((root / 'editor/cli/main.cpp').read_text(encoding='utf-8'), '.cpp')
        self.assertEqual([symbol for _, symbol in references], ['read', 'fwrite'])

    def test_editor_scene_package_saves_do_not_close_legacy_resource_producers(self):
        entry = UnifiedIoInventory.classification('editor/project/project_scene_authoring.cpp')
        self.assertEqual(entry[-1], 'ReviewedPartialMigration')
        self.assertIn('SourceOnlyPackagePreparationAndFileServiceSaves', entry[2])
        self.assertIn('NativeDirectoryPublication', entry[2])
        for path in ('src/resource/internal_asset_package.cpp', 'src/resource/asset_package_closure.cpp'):
            self.assertEqual(UnifiedIoInventory.classification(path)[-1], 'ReviewedPartialMigration')

    def test_legacy_atlas_read_migration_keeps_font_host_io_and_fixture_scope_visible(self):
        entry = UnifiedIoInventory.classification('editor/ui/editor_ui_assets.cpp')
        self.assertEqual(entry[-1], 'ReviewedPartialMigration')
        self.assertIn('RemainingLegacyFontHostIo', entry[2])
        self.assertEqual(UnifiedIoInventory.classification('editor/tests/legacy_atlas_io_tests.cpp')[-1],
                         'ReviewedTestFixture')
        self.assertNotEqual(UnifiedIoInventory.classification('editor/tests/other_atlas.cpp')[-1],
                            'ReviewedTestFixture')

    def test_editor_cold_read_does_not_hide_native_metadata_or_package_io(self):
        self.assertEqual(self.symbols('reader.readTextBytes(path, budget);', '.cpp'),
                         [(1, 'readTextBytes')])
        self.assertEqual(self.symbols('reader.readBinaryBytes(path, budget);', '.cpp'),
                         [(1, 'readBinaryBytes')])
        self.assertEqual(UnifiedIoInventory.classification('editor/io/editor_cold_host_read.cpp')[-1],
                         'ReviewedSourceMigration')
        for path, boundary in (
                ('editor/project/project_input_snapshot.cpp', 'RemainingNativeRootChecks'),
                ('editor/workspace/editor_project_resources.cpp', 'RemainingNativePackageAndPathBoundaries')):
            entry = UnifiedIoInventory.classification(path)
            self.assertEqual(entry[-1], 'ReviewedPartialMigration')
            self.assertIn(boundary, entry[2])
        package = UnifiedIoInventory.classification('editor/project/project_package_snapshot.cpp')
        self.assertEqual(package[-1], 'ReviewedPartialMigration')
        self.assertIn('RemainingResourceClosureAndPathChecks', package[2])

    def test_recovery_read_migration_keeps_native_store_boundaries_visible(self):
        root = Path(__file__).resolve().parents[2]
        for name in ('editor_recovery_preparation.cpp', 'editor_recovery_region.cpp',
                     'editor_recovery_session.cpp', 'editor_scene_publication_preparation.cpp',
                     'editor_shell_scene_persistence.cpp', 'editor_shell_project.cpp'):
            path = 'editor/workspace/' + name
            entry = UnifiedIoInventory.classification(path)
            self.assertEqual(entry[-1], 'ReviewedPartialMigration')
            symbols = {symbol for _, symbol in self.symbols((root / path).read_text(encoding='utf-8'), '.cpp')}
            self.assertTrue(symbols.isdisjoint({'readTextFile', 'readBinaryFile'}), path)
        session = UnifiedIoInventory.classification('editor/workspace/editor_recovery_session.cpp')
        self.assertIn('NativeAtomicStoreAndRemovalRemaining', session[2])

    def test_script_document_bytes_keep_path_and_atomic_store_boundaries(self):
        path = 'editor/workspace/script_document_session.cpp'
        entry = UnifiedIoInventory.classification(path)
        self.assertEqual(entry[-1], 'ReviewedPartialMigration')
        self.assertIn('NativePathAndAtomicStoreRemaining', entry[2])
        root = Path(__file__).resolve().parents[2]
        symbols = {symbol for _, symbol in self.symbols((root / path).read_text(encoding='utf-8'), '.cpp')}
        self.assertTrue({'readTextBytes', 'readBinaryBytes'}.issubset(symbols))
        self.assertTrue(symbols.isdisjoint({'readTextFile', 'readBinaryFile'}))

    def test_browser_archive_read_does_not_hide_directory_or_import_storage(self):
        path = 'editor/web/browser_workspace.cpp'
        entry = UnifiedIoInventory.classification(path)
        self.assertEqual(entry[-1], 'ReviewedPartialMigration')
        self.assertIn('NativeEnumerationAndAtomicStagingRemaining', entry[2])
        root = Path(__file__).resolve().parents[2]
        symbols = {symbol for _, symbol in self.symbols((root / path).read_text(encoding='utf-8'), '.cpp')}
        self.assertIn('readBinaryBytes', symbols)
        self.assertNotIn('readBinaryFile', symbols)

    def test_terrain_manifest_read_does_not_close_native_producer_or_commit(self):
        path = 'editor/workspace/editor_terrain_build.cpp'
        entry = UnifiedIoInventory.classification(path)
        self.assertEqual(entry[-1], 'ReviewedPartialMigration')
        self.assertIn('NativeBuildOutputAndCommitRemaining', entry[2])
        root = Path(__file__).resolve().parents[2]
        symbols = {symbol for _, symbol in self.symbols((root / path).read_text(encoding='utf-8'), '.cpp')}
        self.assertIn('readTextBytes', symbols)
        self.assertTrue(symbols.isdisjoint({'readBinaryFile', 'readTextFile'}))

    def test_legacy_terrain_catalog_read_keeps_importer_and_source_gate_visible(self):
        path = 'editor/workspace/editor_terrain_preview.cpp'
        entry = UnifiedIoInventory.classification(path)
        self.assertEqual(entry[-1], 'ReviewedPartialMigration')
        self.assertIn('FileServiceLegacyCatalog', entry[2])
        self.assertIn('ExplicitNativeImporterRemaining', entry[2])
        root = Path(__file__).resolve().parents[2]
        source = (root / path).read_text(encoding='utf-8')
        symbols = {symbol for _, symbol in self.symbols(source, '.cpp')}
        self.assertIn('readTextBytes', symbols)
        self.assertTrue(symbols.isdisjoint({'readBinaryFile', 'readTextFile'}))
        self.assertIn('EDITOR-TERRAIN-PREVIEW-LEGACY-VEGETATION-SOURCE-UNSUPPORTED', source)

    def test_routed_saves_and_native_commit_boundaries_remain_visible(self):
        self.assertEqual(UnifiedIoInventory.classification('editor/workspace/editor_source_recovery_files.cpp'),
                         ('editor/workspace', 'M4', 'BoundedFormalFileObservationsWithExplicitRecoveryConflicts',
                          'ReviewedAcquisitionOnly'))
        self.assertEqual(UnifiedIoInventory.classification('editor/workspace/editor_source_recovery_preparation.cpp'),
                         ('editor/workspace', 'M4', 'VerifiedRecoveryInputsAndPassiveCandidateWithoutPublication',
                          'ReviewedAcquisitionOnly'))
        self.assertEqual(self.symbols('files.save(request, cb); backend.saveAsync(request, cb, q);', '.cpp'),
                         [(1, 'save'), (1, 'saveAsync')])
        self.assertEqual(self.symbols('NtSetInformationFile(handle, io, data, size, kind);', '.cpp'),
                         [(1, 'NtSetInformationFile')])
        self.assertEqual(UnifiedIoInventory.classification('src/core/io/detail/atomic_file_writer.h')[-1],
                         'ExplicitBoundary')
        for path in ('editor/project/project_source_session.cpp',
                     'editor/workspace/script_document_session.cpp'):
            self.assertEqual(UnifiedIoInventory.classification(path)[-1], 'ReviewedPartialMigration')
        self.assertEqual(UnifiedIoInventory.classification('editor/workspace/editor_project_io_task.cpp'),
                         ('editor/workspace', 'M4', 'DeferredMountedScriptAndProjectSaveWithRetainedCommitReceipt',
                          'ReviewedPartialMigration'))
        self.assertEqual(UnifiedIoInventory.classification('editor/workspace/editor_scene_source_save_task.cpp'),
                         ('editor/workspace', 'M4', 'VerifiedSceneDestinationAcquisitionAndPreparedSave',
                          'ReviewedSourceMigration'))


class SemanticIoReviewTests(unittest.TestCase):
    path = 'editor/workspace/review_fixture.cpp'
    raw = b'void show() { modal.open(shell); modal.open(shell); }\n'

    def document(self):
        entry = dict(path=self.path, sha256=SemanticIoReviews.source_digest(self.raw),
                     owner='editor/workspace', batch='M4', context='ExplicitFixtureModalMethods',
                     status='ReviewedNonFileReference', evidence='The two open calls address the modal.',
                     references=UnifiedIoInventory.references(self.raw.decode(), '.cpp'))
        return dict(type='UnifiedIoSemanticReviews', contractVersion=1,
                    scope='ExactSourceAndListedReferencesOnly', sourcePolicy=SemanticIoReviews.source_policy,
                    entries=[entry])

    def review(self, document=None):
        return SemanticIoReviews(yaml.safe_dump(self.document() if document is None else document))

    def test_annotation_keeps_every_original_lexical_reference(self):
        document = self.document()
        references = copy.deepcopy(document['entries'][0]['references'])
        original = copy.deepcopy(references)
        review, issue = self.review(document).check(self.path, self.raw, references)
        self.assertIsNone(issue)
        self.assertEqual(review['status'], 'ReviewedNonFileReference')
        self.assertEqual(references, original)
        self.assertEqual(len(references), 2)
        self.assertEqual(UnifiedIoInventory.classification(self.path)[-1], 'PendingMigration')

    def test_source_change_invalidates_review_even_without_new_io_tokens(self):
        entry, issue = self.review().check(self.path, self.raw + b'// changed\n',
                                           self.document()['entries'][0]['references'])
        self.assertIsNone(entry)
        self.assertEqual(issue, 'IO-INVENTORY-REVIEW-SOURCE-CHANGED')

    def test_explicit_crlf_policy_keeps_checkout_eol_from_invalidating_source_review(self):
        entry, issue = self.review().check(self.path, self.raw.replace(b'\n', b'\r\n'),
                                           self.document()['entries'][0]['references'])
        self.assertIsNone(issue)
        self.assertIsNotNone(entry)
        self.assertNotEqual(hashlib.sha256(self.raw).hexdigest(),
                            hashlib.sha256(self.raw.replace(b'\n', b'\r\n')).hexdigest())
        changed = self.raw.replace(b'open', b'open\r')
        self.assertEqual(self.review().check(self.path, changed,
                                             self.document()['entries'][0]['references'])[1],
                         'IO-INVENTORY-REVIEW-SOURCE-CHANGED')

    def test_invalid_utf8_never_receives_a_text_source_review(self):
        entry, issue = self.review().check(self.path, b'\xff', [])
        self.assertIsNone(entry)
        self.assertEqual(issue, 'IO-INVENTORY-REVIEW-SOURCE-ENCODING')

    def test_duplicate_occurrence_or_line_changes_cannot_reuse_a_review(self):
        refs = self.document()['entries'][0]['references']
        for changed in (refs[:1], refs + refs[:1], [dict(ref, line=2) for ref in refs]):
            entry, issue = self.review().check(self.path, self.raw, changed)
            self.assertIsNone(entry)
            self.assertEqual(issue, 'IO-INVENTORY-REVIEW-REFERENCES-CHANGED')

    def test_invalid_metadata_fails_without_granting_readiness(self):
        for mutate in (
                lambda doc: doc.update(contractVersion=True),
                lambda doc: doc.update(contractVersion=2),
                lambda doc: doc.update(scope='WholeProgramProven'),
                lambda doc: doc.update(sourcePolicy='GuessWhitespace'),
                lambda doc: doc['entries'][0].update(path='../escape.cpp'),
                lambda doc: doc['entries'][0].update(status='Production Ready'),
                lambda doc: doc['entries'][0].update(status=[]),
                lambda doc: doc['entries'][0].update(sha256='unknown'),
                lambda doc: doc['entries'][0].update(references=[]),
                lambda doc: doc['entries'][0]['references'][0].update(operation=[]),
                lambda doc: doc['entries'].append(copy.deepcopy(doc['entries'][0]))):
            document = self.document()
            mutate(document)
            with self.assertRaises(ReviewDocumentError):
                self.review(document)
        with self.assertRaises(ReviewDocumentError):
            SemanticIoReviews(yaml.safe_dump(self.document()) + 'contractVersion: 1\n')

    def fixture(self, directory):
        root = Path(directory)
        source = root / self.path
        source.parent.mkdir(parents=True)
        source.write_bytes(self.raw)
        document = root / SemanticIoReviews.relative_path
        document.parent.mkdir(parents=True)
        document.write_text(yaml.safe_dump(self.document()), encoding='utf-8')
        return root, source, document

    def collect(self, root):
        with mock.patch('unified_io_inventory.subprocess.check_output',
                        return_value=(self.path + '\0').encode()):
            return UnifiedIoInventory(root).collect()

    def test_inventory_publishes_only_fresh_exact_source_annotations(self):
        with tempfile.TemporaryDirectory() as directory:
            root, source, _ = self.fixture(directory)
            report = self.collect(root)
            self.assertTrue(report['scanComplete'])
            self.assertEqual(report['files'][0]['references'], self.document()['entries'][0]['references'])
            self.assertEqual(report['files'][0]['semanticReview']['scope'],
                             'ExactSourceAndListedReferencesOnly')
            source.write_bytes(self.raw + b'void readTextFile();\n')
            stale = self.collect(root)
            self.assertFalse(stale['scanComplete'])
            self.assertEqual(stale['files'][0]['status'], 'PendingMigration')
            self.assertNotIn('semanticReview', stale['files'][0])
            self.assertEqual(stale['diagnostics'][0]['diagnostic'],
                             'IO-INVENTORY-REVIEW-SOURCE-CHANGED')

    def test_missing_or_no_longer_referenced_source_requires_explicit_review_update(self):
        with tempfile.TemporaryDirectory() as directory:
            root, source, _ = self.fixture(directory)
            source.unlink()
            report = self.collect(root)
            self.assertFalse(report['scanComplete'])
            self.assertEqual(report['diagnostics'][0]['diagnostic'],
                             'IO-INVENTORY-REVIEW-SOURCE-MISSING')
            source.write_bytes(b'void show() {}\n')
            report = self.collect(root)
            self.assertFalse(report['scanComplete'])
            self.assertEqual(report['files'], [])

    def test_corrupt_or_absent_review_cannot_make_existing_gate_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root, _, document = self.fixture(directory)
            baseline = root / 'baseline.yaml'
            baseline.write_text(yaml.safe_dump(self.collect(root)), encoding='utf-8')
            for corrupt in (True, False):
                if corrupt:
                    document.write_text('type: [\n', encoding='utf-8')
                else:
                    document.unlink()
                with mock.patch('unified_io_inventory.subprocess.check_output',
                                return_value=(self.path + '\0').encode()), \
                        mock.patch('sys.argv', ['inventory', '--root', str(root), '--check', str(baseline)]), \
                        contextlib.redirect_stderr(io.StringIO()) as errors:
                    self.assertEqual(main(), 1)
                    self.assertIn('IO-INVENTORY-REVIEW-REGRESSION', errors.getvalue())

    def test_real_editor_reviews_keep_native_legacy_and_validation_io_visible(self):
        root = Path(__file__).resolve().parents[2]
        reviews = SemanticIoReviews((root / SemanticIoReviews.relative_path).read_text(encoding='utf-8'))
        self.assertEqual(len(reviews.entries), 41)
        for path, expected in (
                ('editor/workspace/editor_scene_persistence.cpp', 'ReviewedPartialMigration'),
                ('editor/workspace/editor_recovery_session.h', 'ReviewedPartialMigration'),
                ('editor/tools/generate_reference_assets.py', 'ReviewedPartialMigration'),
                ('editor/validation/editor_script_input_smoke.cpp', 'ReviewedValidationFixture'),
                ('editor/web/cli_bridge.js', 'ReviewedProtocolTransport'),
                ('editor/viewport/editor_scene_viewport_ordered_renderer.cpp', 'ReviewedNonFileReference')):
            source = (root / path).read_bytes()
            refs = UnifiedIoInventory.references(source.decode('utf-8-sig'), Path(path).suffix)
            entry, issue = reviews.check(path, source, refs)
            self.assertIsNone(issue, path)
            self.assertEqual(entry['status'], expected)


if __name__ == '__main__':
    unittest.main()
