"""Fixtures for reviewable first-party IO inventory evidence."""
import unittest

from unified_io_inventory import UnifiedIoInventory


class UnifiedIoInventoryTests(unittest.TestCase):
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

    def test_native_low_level_and_python_file_open(self):
        self.assertEqual(self.symbols('int fd = openat(root, path, flags);\nfstat(fd, &info);', '.cpp'),
                         [(1, 'openat'), (2, 'fstat')])
        self.assertEqual(self.symbols('with open(path) as file:\n    value = file.read()', '.py'),
                         [(1, 'open'), (2, 'read')])

    def test_text_service_and_current_package_ownership(self):
        self.assertEqual(self.symbols('service.readText(request, callback);', '.cpp'), [(1, 'readText')])
        self.assertEqual(self.symbols('files.query(request, completion);', '.cpp'), [(1, 'query')])
        self.assertEqual(UnifiedIoInventory.classification('src/core/io/file_service.cpp')[0], 'core/io')
        self.assertEqual(UnifiedIoInventory.classification('src/network/http/fetch_byte_transfer.cpp')[:2], ('network/http', 'I2'))
        self.assertEqual(UnifiedIoInventory.classification('src/resource/resource_graph_executor.cpp'),
                         ('resource', 'M1', 'BoundedMountedGraphOwnerAdmission', 'ReviewedOwnerAdmission'))
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
        self.assertEqual(UnifiedIoInventory.classification('src/renderer/ui/ui_text_layout.h')[-1], 'ReviewedPartialMigration')

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


if __name__ == '__main__':
    unittest.main()
