"""Build the real EditorShell with the explicit ordered WebGL1 profile."""
import os
from pathlib import Path
import re
import subprocess
import sys

from cmake_build_cache import CMakeCacheDocument


class EditorWebGlBuilder:
    def __init__(self, args):
        self.args = args
        self.root = Path(__file__).resolve().parents[1]
        self.output = Path(args.output_dir).resolve()
        self.programs = self.output.parent/(self.output.name+'-programs')

    def run(self, command):
        subprocess.run([str(p) for p in command], cwd=self.root, check=True)

    def build(self):
        args = self.args
        toolchain = Path(args.emsdk)/'upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'
        for required in (toolchain, Path(args.mono_font), Path(args.cjk_font),
                         Path(args.reflect_codegen), Path(args.script_aotc)):
            if not required.is_file():
                raise ValueError('EDITOR-WEB-BUILD-INPUT-MISSING: '+str(required))
        if self.output == self.root or self.root not in self.output.parents:
            raise ValueError('EDITOR-WEB-BUILD-OUTPUT: select a dedicated directory under build/')
        if self.root/'build' not in self.output.parents:
            raise ValueError('EDITOR-WEB-BUILD-OUTPUT: select a dedicated directory under build/')
        cache_path=self.output/'CMakeCache.txt'
        if cache_path.exists():
            cache=CMakeCacheDocument.load(cache_path)
            cache.require_locations(self.root,self.output)
            if cache.value('ARP_EDITOR_WEBGL_PRODUCT')!='ON' or cache.value('ARP_WEB_RHI')!='webgl1':
                raise ValueError('EDITOR-WEB-CACHE-SELECTION: refusing to repurpose another build')
        self.run([sys.executable,'-X','utf8',self.root/'scripts/compile_shaders.py',
            '--slangc',args.slangc,'--spirv-reflect',args.spirv_reflect,
            '--spirv-cross-library',args.spirv_cross,'--out-dir',self.programs,
            '--module','gui_unified','--graphics-module','--glsl-es-100'])
        self.run(['cmake','-S',self.root,'-B',self.output,'-G','Ninja',
            '-DCMAKE_TOOLCHAIN_FILE='+str(toolchain),'-DCMAKE_MAKE_PROGRAM='+args.ninja,
            '-DCMAKE_BUILD_TYPE='+args.build_type,'-DARP_WEB_RHI=webgl1',
            '-DARP_EDITOR_WEBGL_PRODUCT=ON','-DARP_BUILD_EDITOR=ON','-DARP_BUILD_A_QUEEN=OFF',
            '-DARP_ENABLE_VENDORED_ASSIMP=OFF','-DARP_EDITOR_WEB_PROGRAMS='+str(self.programs),
            '-DARP_EDITOR_WEB_MONO_FONT='+args.mono_font,'-DARP_EDITOR_WEB_CJK_FONT='+args.cjk_font,
            '-DARP_NATIVE_REFLECT_CODEGEN='+args.reflect_codegen,'-DARP_SCRIPT_AOTC_HOST='+args.script_aotc])
        self.run(['cmake','--build',self.output,'--target','arp_editor','--parallel',str(args.jobs)])
        ninja=(self.output/'build.ninja').read_text(encoding='utf8')
        link=re.search(r'^build arp_editor\.html:.*?(?=^\S|\Z)',ninja,re.M|re.S)
        if not link or any(x in link[0] for x in ('emdawnwebgpu','USE_WEBGPU','libarp_rhi_webgpu','libarp_rhi_vulkan','libarp_rhi_metal')):
            raise ValueError('EDITOR-WEB-FOREIGN-BACKEND-LINK')
        if not all(x in link[0] for x in ('-sMIN_WEBGL_VERSION=1','-sMAX_WEBGL_VERSION=1','-pthread')):
            raise ValueError('EDITOR-WEB-CONTEXT-SELECTION')
        print('Serve: python scripts/serve_editor_web.py --directory "'+str(self.output)+'"')
        print('Open: http://127.0.0.1:8093/arp_editor.html')


def add_arguments(parser, root):
    parser.add_argument('--output-dir',default=str(Path(root)/'build/editor-webgl'))
    parser.add_argument('--emsdk',default=os.environ.get('EMSDK','C:/emsdk'))
    parser.add_argument('--ninja',default='C:/mingw64/bin/ninja.exe')
    parser.add_argument('--slangc',default='C:/VulkanSDK/1.4.350.0/Bin/slangc.exe')
    parser.add_argument('--spirv-reflect',default='C:/VulkanSDK/1.4.350.0/Bin/spirv-reflect.exe')
    parser.add_argument('--spirv-cross',default='C:/VulkanSDK/1.4.350.0/Bin/spirv-cross-c-shared.dll')
    parser.add_argument('--mono-font',default='C:/Windows/Fonts/consola.ttf')
    parser.add_argument('--cjk-font',default='C:/Windows/Fonts/Deng.ttf')
    parser.add_argument('--reflect-codegen',default=str(Path(root)/'build/win-vulkan-debug/arp_reflect_codegen.exe'))
    parser.add_argument('--script-aotc',default=str(Path(root)/'build/win-vulkan-release/arp_script_aotc.exe'))
    parser.add_argument('--build-type',choices=('Debug','Release'),default='Debug')
    parser.add_argument('--jobs',type=int,default=6)
