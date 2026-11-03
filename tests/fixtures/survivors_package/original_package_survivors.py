"""Create an offline standalone Windows package from a completed MinGW build."""
import argparse,hashlib,shutil,subprocess,zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
def quoted(value):return "'"+str(value).replace("'","''")+"'"
def main():
    p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();build=args.build.resolve();out=args.output.resolve()
    if out.exists():raise SystemExit('Output must be a new directory: '+str(out))
    exe=build/'arp_survivors.exe'
    imports=subprocess.check_output(['C:/mingw64/bin/objdump.exe','-p',str(exe)],text=True)
    dlls=sorted({line.split('DLL Name:',1)[1].strip() for line in imports.splitlines() if 'DLL Name:' in line})
    # Native Windows/driver libraries are provided by the supported target OS.
    system={'kernel32.dll','user32.dll','gdi32.dll','shell32.dll','advapi32.dll','ole32.dll','oleaut32.dll','winmm.dll','msvcrt.dll','ws2_32.dll','bcrypt.dll','comdlg32.dll','comctl32.dll','shlwapi.dll','setupapi.dll','vulkan-1.dll','dwmapi.dll','imm32.dll','version.dll','uxtheme.dll','ntdll.dll','crypt32.dll','propsys.dll'}
    dependencies=[]
    for dll in dlls:
        if dll.lower() in system or dll.lower().startswith('api-ms-win-'):continue
        source=build/dll
        if not source.is_file():source=Path('C:/mingw64/bin')/dll
        if not source.is_file():raise SystemExit('Unresolved DLL: '+dll)
        dependencies.append(source)
    shaderNames=['scene_2d_sprites'+suffix for suffix in ('.slang','.spv','.metal','.wgsl','.reflection.yaml','.artifact-set.yaml')]
    for name in shaderNames:
        if not (build/'shaders'/name).is_file():raise SystemExit('Missing shader artifact: '+name)
    out.mkdir(parents=True);shutil.copy2(exe,out/exe.name)
    for source in dependencies:shutil.copy2(source,out/source.name)
    shutil.copytree(build/'assets',out/'assets')
    (out/'shaders').mkdir()
    for name in shaderNames:shutil.copy2(build/'shaders'/name,out/'shaders'/name)
    (out/'scripts').mkdir()
    shutil.copy2(build/'generated/survivors-aot/survivors.manifest.yaml',out/'scripts/survivors.manifest.yaml')
    (out/'play-interpreter.cmd').write_text('@echo off\n"%~dp0arp_survivors.exe" --script-interpreter %*\n',encoding='utf-8')
    shutil.copy2(ROOT/'docs/script-components-plan.md',out/'SCRIPT-VALIDATION.md')
    licenses=out/'licenses';licenses.mkdir()
    for source in (ROOT/'3rd').rglob('*'):
        if source.is_file() and (source.name.lower().startswith(('license','copying','copyright')) or source.name.lower() in ('ftl.txt','gplv2.txt')):
            target=licenses/source.relative_to(ROOT/'3rd');target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
    for source in ROOT.glob('LICENSE*'):shutil.copy2(source,licenses/source.name)
    # stb/miniaudio have embedded license text; preserve the exact distributed header.
    for name in ('stb/stb_image.h','miniaudio/miniaudio.h'):
        target=licenses/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/'3rd'/name,target)
    (out/'README.txt').write_text("""Copperlight Survivors - ARP 2D acceptance game
Windows x64 / Vulkan driver required. No SDK, editor, source tree or downloads.
Run arp_survivors.exe. For an explicitly silent run use --mute.
Default execution is Interpreter and reads the included ArpScript source.
Double-click play-interpreter.cmd for the same mode. Current performance limitations
and validation scope are recorded in SCRIPT-VALIDATION.md.
Explicit --script-aot selects DirectNative and validates the source hash;
changed .arp requires a regenerated AOT package in that mode. No silent fallback.
Enter: start; WASD/arrows: move; Escape: pause; 1/2/3: upgrade;
R: new run; -/+: volume. Automatic weapons; survive 5 minutes and defeat Boss.
Upgrade 1 Bolt: extra shots (max 6), pierce every 2 upgrades (max 3),
ricochet every 3 upgrades (max 2). Upgrade 2 Orbit: +1 blade (max 8).
Upgrade 3 Pulse: +15% base radius (max 220%). All also add damage/attack rate.
Upgrade menu shows current / next values. HUD shows actual counts/radius.
1280x720 logical canvas; --width 1920 --height 1080; --integer-scale optional.
Save/settings: user-data/record.yaml beside executable (writable directory).
Automation: --autoplay, --hidden, --seed 42, --restart-check 20.
Stress: --stress --hidden --mute --width 1920 --height 1080 (30+180 seconds).
Licenses and asset provenance are in licenses/ and assets/PROVENANCE.yaml.
Validation scope: Windows/Vulkan only. Not all-backend Production Ready.
""",encoding='utf-8')
    rows=['type: arp.survivors-package','contractVersion: 1','platform: windows-x64','backend: Vulkan','scriptExecutionModes: [DirectNativeV5, Interpreter]','systemDlls:']
    rows+=['  - '+quoted(dll) for dll in dlls if dll.lower() in system or dll.lower().startswith('api-ms-win-')]
    rows+=['files:']
    for source in sorted(out.rglob('*')):
        if source.is_file():rows+=['  - path: '+quoted(source.relative_to(out).as_posix()),'    bytes: '+str(source.stat().st_size),'    sha256: '+quoted(hashlib.sha256(source.read_bytes()).hexdigest())]
    (out/'package.yaml').write_text('\n'.join(rows)+'\n',encoding='utf-8')
    archive=out.with_suffix('.zip')
    with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for source in sorted(out.rglob('*')):
            if source.is_file():z.write(source,out.name+'/'+source.relative_to(out).as_posix())
    print(archive)
if __name__=='__main__':main()
