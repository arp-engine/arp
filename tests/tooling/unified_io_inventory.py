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


class UnifiedIoInventory:
    roots = {'src', 'editor', 'tools', 'modules', 'examples', 'samples', 'games', 'Libs', 'scripts'}
    suffixes = {'.h', '.hpp', '.hh', '.c', '.cc', '.cpp', '.cxx', '.m', '.mm', '.inl',
                '.arp', '.py', '.js', '.mjs'}
    excluded = {'3rd', 'third_party', 'vendor', 'build', 'generated', 'node_modules', '.git'}
    symbols = {
        'read': {'ifstream', 'fstream', 'fopen', '_wfopen', 'fopen_s', '_wfopen_s',
                 'fread', 'fgets', 'open', 'openat', '_open', '_wopen', 'read', 'pread',
                 'CreateFileW', 'CreateFileA', 'createReadStream', 'readTextFile',
                 'readBinaryFile', 'readText', 'readResourceSource', 'readResourceText',
                 'loadHost', 'acquireUiFontSource', 'loadDefaultUiFontFace',
                 'read_text', 'read_bytes', 'readFile', 'readFileSync',
                 'ReadFile', 'readAsync', 'readResolved', 'NtCreateFile', 'digestFile'},
        'metadata': {'canonical', 'weakly_canonical', 'exists', 'is_regular_file',
                     'is_directory', 'symlink_status', 'file_size', 'last_write_time',
                     'directory_iterator', 'recursive_directory_iterator', 'stat', 'lstat',
                     'fstat', 'glob', 'rglob', 'iterdir', 'GetFileAttributesW', 'GetFileSizeEx',
                     'GetFileInformationByHandle', 'GetFileInformationByHandleEx', 'query', 'queryAsync'},
        'write': {'ofstream', 'fwrite', 'write_text', 'write_bytes', 'writeFile', 'writeFileSync',
                  'WriteFile', 'replaceFileAtomically', 'replaceIfUnchanged', 'create_directory',
                  'create_directories', 'remove', 'remove_all', 'rename', 'copy_file'},
        'transport': {'fetch', 'emscripten_fetch'},
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
        'src/core/io/memory_file_backend.cpp': 'RegisteredMemoryBackend',
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
        if suffix == '.py':
            # Python comments and multiline strings have different rules than
            # C++. Use its own lexer, retaining the same source line evidence.
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
            # Include constructions, declarations and aliases as candidates.
            # Their semantic classification is reviewed during the named batch.
            results.append(dict(line=source.count('\n', 0, start) + 1,
                                symbol=value, operation=operation))
        return results

    @classmethod
    def classification(cls, path):
        network_io = {
            'src/network/http/byte_read_transport.cpp': 'EngineHttpClientByteTransport',
            'src/network/http/fetch_byte_transfer.cpp': 'BrowserFetchByteTransport',
            'src/core/io/http_file_backend.cpp': 'HttpFileBudgetVersionAdapter',
        }
        if path in network_io:
            owner = 'core/io' if path.startswith('src/core/') else 'network/http'
            return owner, 'I2', network_io[path], 'ReviewedIoInfrastructure'
        if path in cls.boundaries:
            return 'core/io', 'I1', 'LocalBackendBoundary', 'ExplicitBoundary'
        if path in cls.unified_entries:
            return 'core/io', 'I1', cls.unified_entries[path], 'ReviewedIoInfrastructure'
        if path == 'src/resource/resource_graph_executor.cpp':
            return 'resource', 'M1', 'BoundedMountedGraphOwnerAdmission', 'ReviewedOwnerAdmission'
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
            return 'renderer/ui', 'M2', 'PreparedKerningWithRemainingDefaultFontProfile', 'ReviewedPartialMigration'
        if path == 'src/ui/ui_document.cpp':
            return 'ui', 'M2', 'PreparedRuntimeMountWithRemainingLocalLoaders', 'ReviewedPartialMigration'
        if path in {'src/shader/shader_artifact_files.h',
                    'src/shader/shader_reflection_cache.cpp',
                    'src/shader/single_entry_shader_artifact.cpp',
                    'src/shader/graphics_shader_module_artifact.cpp',
                    'src/shader/webgl_shader_program_artifact.cpp',
                    'src/shader/original_depth_table_expansion_artifact.cpp'}:
            return 'shader', 'M2', 'AcquiredArtifactInputWithLegacyLocalEntry', 'ReviewedPartialMigration'
        if path in {'src/resource/terrain_asset_resource.cpp',
                    'src/resource/vegetation_catalog_cooked.cpp',
                    'src/resource/internal_asset_package.cpp',
                    'src/resource/asset_package_closure.cpp',
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
            return 'scriptvm/plugin', 'M3', 'BoundedNativeGeneratedTextAndEscapedLogPrefix', 'ReviewedSourceMigration'
        if path == 'src/scriptvm/plugin/aot_dynamic_package.cpp':
            return 'scriptvm/plugin', 'M3', 'BoundedHostTextWithRemainingToolchainBinaryAndOutputIo', 'ReviewedPartialMigration'
        if path == 'tools/asset_importer/script_runner.cpp':
            return 'tools/asset_importer', 'M6', 'DeclaredResolverAndBuiltinPluginBindingsWithRemainingToolIo', 'ReviewedPartialMigration'
        if path == 'editor/project/project_input_snapshot.cpp':
            return 'editor/project', 'M4', 'LogicalContentIdentityWithBoundedNativeReadAndRootChecks', 'ReviewedPartialMigration'
        if path in {'editor/project/project_source_session.cpp', 'editor/project/project_source_session.h'}:
            return 'editor/project', 'M4', 'EpochBoundMountedProjectAcquisition', 'ReviewedAcquisitionOnly'
        if path == 'editor/workspace/editor_script_modules.cpp':
            return 'editor/workspace', 'M4', 'SharedColdPreparationWithNativeAndMountedInputs', 'ReviewedPartialMigration'
        if path in {'editor/project/project_resource_sources.cpp',
                    'editor/project/project_resource_locations_sources.cpp',
                    'editor/workspace/editor_project_resource_sources.cpp'}:
            return 'editor', 'M4', 'PreloadedProjectResourceCandidateWithEpochMountGates', 'ReviewedSourceMigration'
        if path in {'editor/project/project_package_snapshot.cpp',
                    'editor/project/project_resource_locations.cpp',
                    'editor/workspace/editor_project_resources.cpp'}:
            return 'editor', 'M4', 'SharedNativeAndAcquiredResourceProvenance', 'ReviewedPartialMigration'
        if path == 'editor/workspace/editor_terrain_preview.cpp':
            return 'editor/workspace', 'M4', 'SharedBakerWithAcquiredInputsAndExplicitLegacyImportGate', 'ReviewedPartialMigration'
        if path == 'editor/workspace/editor_project_resource_planner.cpp':
            return 'editor/workspace', 'M4', 'IncrementalDeclaredReadsOverAcquiredSourcesOnly', 'ReviewedSourcePlanning'
        if path == 'editor/project/project_directory_session.cpp':
            return 'editor/project', 'M4', 'PagedDirectoryCandidateWithReceiptEpochAndMountGates', 'ReviewedAcquisitionOnly'
        if path == 'editor/workspace/project_browser_model.cpp':
            return 'editor/workspace', 'M4', 'PreparedMountedDirectoryModelWithRemainingNativeBrowsing', 'ReviewedPartialMigration'
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
            if not references:
                continue
            owner, batch, context, status = self.classification(name)
            files.append(dict(path=name, sha256=hashlib.sha256(raw).hexdigest(), owner=owner,
                              batch=batch, context=context, status=status, references=references))
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
    print(yaml.safe_dump(report['counts'], sort_keys=False).strip())
    if not report['scanComplete']:
        print(yaml.safe_dump(report['diagnostics']), file=sys.stderr)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
