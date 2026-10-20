"""Root WebGL1 runtime component Prototype builder; no production promotion."""
from datetime import datetime, timezone
import hashlib
from pathlib import Path
import subprocess
import sys
from uuid import uuid4

import yaml
from cmake_build_cache import CMakeCacheDocument


class WebGl1RuntimeBuilder:
    def __init__(self, root, output, program, cmake, ninja, toolchain, browser=None, build_type='Debug'):
        self.root = Path(root).resolve()
        self.output = Path(output).resolve()
        self.program = Path(program).resolve()
        self.cmake, self.ninja, self.toolchain = (Path(p).resolve() for p in (cmake,ninja,toolchain))
        self.browser = Path(browser).resolve() if browser else None
        if build_type not in ('Debug', 'Release'):
            raise ValueError('RHI-WEBGL1-RUNTIME-BUILD-TYPE')
        self.build_type = build_type

    @staticmethod
    def sha(path):
        with Path(path).open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()

    def validate(self):
        for path in (self.cmake,self.ninja,self.toolchain):
            if not path.is_file():
                raise ValueError('RHI-WEBGL1-RUNTIME-TOOL-MISSING: '+str(path))
        if not (self.program/'webgl1_graphics.webgl-program.yaml').is_file():
            raise ValueError('RHI-WEBGL1-RUNTIME-ORIGINAL-PROGRAM-MISSING')
        if self.browser and not self.browser.is_file():
            raise ValueError('RHI-WEBGL1-RUNTIME-BROWSER-MISSING')
        if self.output == self.root or self.output == self.program or self.program in self.output.parents:
            raise ValueError('RHI-WEBGL1-RUNTIME-OUTPUT-ROOT')
        for name in ('src','scripts','tests','cmake','docs','3rd','generated','shaders','.git'):
            protected=self.root/name
            if self.output==protected or protected in self.output.parents:
                raise ValueError('RHI-WEBGL1-RUNTIME-OUTPUT-SOURCE')

    def inputs(self):
        names=subprocess.check_output(['git','ls-files','-co','--exclude-standard'],cwd=self.root,text=True).splitlines()
        result={str(self.root/n):self.sha(self.root/n) for n in sorted(set(names)) if (self.root/n).is_file()}
        result.update({str(p):self.sha(p) for p in sorted(self.program.rglob('*')) if p.is_file()})
        return result

    def build(self):
        self.validate()
        self.output.mkdir(parents=True,exist_ok=True)
        directory=self.output/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid4().hex[:8])
        directory.mkdir(exist_ok=False)
        before=self.inputs()
        manifest=dict(type='WebGl1RuntimeComponentBuild',contractVersion=1,status='Building',
            evidenceKind='Prototype',buildType=self.build_type,productionReady=False,gpuCompletionClaimed=False,sourceInputs=before,
            tools={str(p):self.sha(p) for p in (self.cmake,self.ninja,self.toolchain)},steps=[])
        path=directory/'build-manifest.yaml'
        def save():
            path.write_text(yaml.safe_dump(manifest,sort_keys=False),encoding='utf8')
        def execute(name,command):
            with (directory/(name+'.log')).open('w',encoding='utf8') as log:
                result=subprocess.run([str(p) for p in command],cwd=self.root,stdout=log,stderr=subprocess.STDOUT)
            manifest['steps'].append(dict(name=name,command=[str(p) for p in command],exitCode=result.returncode))
            save()
            if result.returncode:
                raise RuntimeError('RHI-WEBGL1-RUNTIME-STEP-FAILED: '+str(directory/(name+'.log')))
        save()
        try:
            execute('configure',[self.cmake,'-S',self.root,'-B',directory,'-G','Ninja',
                '-DCMAKE_TOOLCHAIN_FILE='+str(self.toolchain),'-DCMAKE_MAKE_PROGRAM='+str(self.ninja),
                '-DCMAKE_BUILD_TYPE='+self.build_type,'-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                '-DARP_WEB_RHI=webgl1','-DARP_WEBGL1_PROTOTYPE_PRODUCT=ON',
                '-DARP_WEBGL_PROGRAM_DIRECTORY='+str(self.program)]
                + (['-DARP_WEBGL_RUNTIME_BROWSER='+str(self.browser)] if self.browser else []))
            cache=CMakeCacheDocument.load(directory/'CMakeCache.txt')
            cache.require_locations(self.root,directory)
            for key,value in [('ARP_WEB_RHI','webgl1'),('ARP_WEBGL1_PROTOTYPE_PRODUCT','ON'),('CMAKE_BUILD_TYPE',self.build_type)]:
                if cache.value(key)!=value:
                    raise ValueError('RHI-WEBGL1-RUNTIME-CACHE-IDENTITY: '+key)
            execute('build',[self.cmake,'--build',directory,'--target','arp_webgl1_runtime_validation','--parallel','1'])
            link=(directory/'CMakeFiles/rules.ninja').read_text()+(directory/'build.ninja').read_text()
            if any(token in link for token in ('emdawnwebgpu','USE_WEBGPU','libarp_rhi_webgpu','libarp_rhi_vulkan','libarp_rhi_metal')):
                raise ValueError('RHI-WEBGL1-RUNTIME-LINK-FOREIGN-BACKEND')
            if '-sMIN_WEBGL_VERSION=1' not in link or '-sMAX_WEBGL_VERSION=1' not in link:
                raise ValueError('RHI-WEBGL1-RUNTIME-LINK-CONTEXT-VERSION')
            if self.browser:
                execute('browser',[sys.executable,'-X','utf8',self.root/'tests/contract/webgl_pipeline_browser.py',
                    '--directory',directory,'--output',directory/'browser','--browser',self.browser])
                execute('extensions-browser',[sys.executable,'-X','utf8',self.root/'tests/contract/webgl_extension_execution_browser.py',
                    '--directory',directory,'--output',directory/'extensions-browser','--browser',self.browser])
                execute('ordered-browser',[sys.executable,'-X','utf8',self.root/'tests/contract/webgl_ordered_graphics_browser.py',
                    '--directory',directory,'--output',directory/'ordered-browser','--browser',self.browser])
                for profile in ('scene2d', 'gui', 'basic_forward', 'conformance_clear', 'conformance_sampled', 'conformance_original'):
                    execute(profile+'-browser',[sys.executable,'-X','utf8',
                        self.root/('tests/contract/webgl_'+profile+'_browser.py'),
                        '--directory',directory,'--output',directory/(profile+'-browser'),'--browser',self.browser])
            manifest['artifacts']={p.name:self.sha(p) for p in directory.glob('arp_webgl1_runtime_validation.*') if p.is_file()}
            manifest['linkClosure']='Original core/shader/L2/WebGL only; no WebGPU/Dawn/Vulkan/Metal'
            manifest['status']='Prototype Complete'
        except Exception:
            manifest['status']='Failed'
            raise
        finally:
            manifest['sourceInputsAfter']=self.inputs()
            manifest['allNamedInputsMatch']=manifest['sourceInputsAfter']==before
            save()
        if not manifest['allNamedInputsMatch']:
            raise ValueError('RHI-WEBGL1-RUNTIME-BUILD-INPUT-DRIFT')
        return path
