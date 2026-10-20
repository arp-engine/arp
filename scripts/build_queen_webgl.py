"""Build the real Queen game with the original ordered WebGL1 Prototype."""
import argparse
from pathlib import Path
import subprocess
import sys
import yaml
from cmake_build_cache import CMakeCacheDocument
from webgl1_runtime_build import WebGl1RuntimeBuilder


class QueenWebGlBuilder:
    modules=('queen_portable_sky','queen_portable_shadow','queen_portable','gui_bitmap')

    def __init__(self,args):
        self.args=args
        self.root=Path(__file__).resolve().parents[1]
        self.output=Path(args.output).resolve()
        self.programs=Path(args.programs).resolve()

    def run(self,command):
        subprocess.run([str(p) for p in command],cwd=self.root,check=True)

    def build(self):
        toolchain=Path(self.args.emsdk)/'upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake'
        if not toolchain.is_file():raise ValueError('QUEEN-EMSCRIPTEN-TOOLCHAIN-MISSING')
        for directory in (self.output,self.programs):
            if directory==self.root or any(self.root/n==directory or self.root/n in directory.parents
                    for n in ('src','scripts','examples','shaders','3rd','tests','cmake','.git')):
                raise ValueError('QUEEN-BUILD-OUTPUT-SOURCE')
        if self.output==self.programs or self.programs in self.output.parents:
            raise ValueError('QUEEN-BUILD-PROGRAM-OUTPUT-OVERLAP')
        for module in self.modules:
            self.run([sys.executable,'-X','utf8',self.root/'scripts/compile_shaders.py',
                '--slangc',self.args.slangc,'--spirv-reflect',self.args.reflect,
                '--spirv-cross-library',self.args.spirv_cross,'--out-dir',self.programs,
                '--module',module,'--graphics-module','--glsl-es-100'])
        self.run(['cmake','-S',self.root,'-B',self.output,'-G','Ninja',
            '-DCMAKE_TOOLCHAIN_FILE='+str(toolchain),'-DCMAKE_MAKE_PROGRAM='+self.args.ninja,
            '-DCMAKE_BUILD_TYPE=Release','-DARP_WEB_RHI=webgl1',
            '-DARP_WEBGL1_PROTOTYPE_PRODUCT=ON','-DARP_WEBGL1_QUEEN_GAME=ON',
            '-DARP_WEBGL_PROGRAM_DIRECTORY='+str(self.programs)])
        cache=CMakeCacheDocument.load(self.output/'CMakeCache.txt')
        cache.require_locations(self.root,self.output)
        for key,value in (('ARP_WEB_RHI','webgl1'),('ARP_WEBGL1_QUEEN_GAME','ON'),
                ('ARP_WEBGL1_PROTOTYPE_PRODUCT','ON'),('CMAKE_BUILD_TYPE','Release')):
            if cache.value(key)!=value:raise ValueError('QUEEN-CACHE-SELECTION: '+key)
        self.run(['cmake','--build',self.output,'--target','arp_build_a_queen_webgl','arp_webgl_platform_smoke',
            '--parallel',str(self.args.jobs)])
        link=(self.output/'build.ninja').read_text()+(self.output/'CMakeFiles/rules.ninja').read_text()
        if any(x in link for x in ('emdawnwebgpu','USE_WEBGPU','libarp_rhi_webgpu','libarp_rhi_vulkan','libarp_rhi_metal')):
            raise ValueError('QUEEN-FOREIGN-BACKEND-LINK')
        if not all(x in link for x in ('-sMIN_WEBGL_VERSION=1','-sMAX_WEBGL_VERSION=1')):
            raise ValueError('QUEEN-WEBGL1-CONTEXT-SELECTION')
        manifest=dict(type='QueenWebGlBuild',contractVersion=1,status='Prototype Complete',
            backend='WebGL1',gameplayValidated=False,productionReady=False,
            artifacts={p.name:WebGl1RuntimeBuilder.sha(p) for suffix in ('html','js','wasm','data')
                for p in [self.output/('arp_build_a_queen_webgl.'+suffix)]},
            programs={name:WebGl1RuntimeBuilder.sha(self.programs/(name+'.webgl-program.yaml'))
                for name in self.modules})
        (self.output/'queen-build.yaml').write_text(yaml.safe_dump(manifest,sort_keys=False),encoding='utf8')
        print('Serve: python -m http.server 8092 --bind 127.0.0.1 --directory "{}"'.format(self.output))
        print('Open: http://127.0.0.1:8092/arp_build_a_queen_webgl.html')


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',default='build/queen-webgl1')
    parser.add_argument('--programs',default='build/queen-webgl-programs')
    parser.add_argument('--emsdk',default='C:/emsdk')
    parser.add_argument('--ninja',default='C:/mingw64/bin/ninja.exe')
    parser.add_argument('--slangc',default='C:/VulkanSDK/1.4.350.0/Bin/slangc.exe')
    parser.add_argument('--reflect',default='C:/VulkanSDK/1.4.350.0/Bin/spirv-reflect.exe')
    parser.add_argument('--spirv-cross',default='C:/VulkanSDK/1.4.350.0/Bin/spirv-cross-c-shared.dll')
    parser.add_argument('--jobs',type=int,default=6)
    QueenWebGlBuilder(parser.parse_args()).build()
