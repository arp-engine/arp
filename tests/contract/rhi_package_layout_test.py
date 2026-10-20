"""RHI packages express architectural boundaries, not tiny domain folders."""
from pathlib import Path
import re
import unittest
from graphlib import TopologicalSorter


class RhiPackageLayoutTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def test_packages_are_flat_and_backend_roots_are_explicit(self):
        rhi = self.ROOT / "src/rhi"
        self.assertEqual({p.name for p in rhi.iterdir() if p.is_dir()},
                         {"frontend", "semantics", "backends", "conformance"})
        for name in ("frontend", "semantics", "conformance"):
            self.assertFalse(any(p.is_dir() for p in (rhi / name).iterdir()), name)
        self.assertEqual({p.name for p in (rhi / "backends").iterdir() if p.is_dir()},
                         {"null", "vulkan", "metal", "webgpu"})
        for backend in (rhi / "backends").iterdir():
            self.assertFalse(any(p.is_dir() for p in backend.iterdir()), backend.name)

    def test_semantics_does_not_own_runtime_objects_or_include_native_headers(self):
        for path in (self.ROOT / "src/rhi/semantics").glob("*"):
            if path.suffix not in (".h", ".cpp", ".inl"):
                continue
            source = path.read_text(encoding="utf-8-sig")
            source = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
            self.assertIsNone(re.search(r"core::(?:makeUnique|makeShared)|std::shared_ptr|QuotaBoundSharedObjectIdentity", source), str(path))
            self.assertIsNone(re.search(r'#include\s+[<"](?:rhi/backends/|renderer/|platform/|vulkan/|Metal/|webgpu/)', source), str(path))

    def test_internal_imports_resolve_and_rhi_headers_are_acyclic(self):
        graph={}
        for path in (self.ROOT / "src/rhi").rglob("*"):
            if path.suffix not in (".h", ".inl", ".cpp", ".mm"):
                continue
            source=path.read_text(encoding="utf-8-sig")
            dependencies=[]
            for name in re.findall(r'^#include\s+"([^"\n]+)"',source,re.M):
                if name == "bindless_zero_base_identity.h":
                    cmake=(self.ROOT / "CMakeLists.txt").read_text(encoding="utf-8-sig")
                    self.assertIn("generated/rhi/bindless_zero_base_identity.h",cmake)
                    continue
                if name.startswith("rhi/"):
                    dependency=self.ROOT / "src" / name
                elif (self.ROOT / "src" / name).is_file():
                    dependency=self.ROOT / "src" / name
                else:
                    dependency=path.parent / name
                if "rhi" not in dependency.parts:
                    continue
                self.assertTrue(dependency.is_file(), f"{path}: {name}")
                if dependency.suffix in (".h", ".inl"):
                    dependencies.append(dependency.resolve())
            if path.suffix in (".h", ".inl"):
                graph[path.resolve()]=dependencies
        # Core/Shader value headers do not import RHI; direct RHI cycles are forbidden.
        list(TopologicalSorter(graph).static_order())

    def test_frontend_does_not_define_backend_dispatch_tables(self):
        for path in (self.ROOT / "src/rhi/frontend").glob("*"):
            if path.suffix not in (".h", ".cpp", ".inl"):
                continue
            source = path.read_text(encoding="utf-8-sig")
            self.assertIsNone(re.search(r"case\s+(?:rhi::)?Backend::", source), str(path))
            self.assertIsNone(re.search(r'#include\s+"rhi/backends/', source), str(path))


if __name__ == "__main__":
    unittest.main()
