"""Attach a new authored ScriptComponent without rebuilding the game host."""
import argparse
import hashlib
import re
import shutil
import subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--build', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
build, out = a.build.resolve(), a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
exe = build / 'arp_survivors.exe'
before = hashlib.sha256(exe.read_bytes()).hexdigest()
assets = out / 'extension-assets'
shutil.copytree(build / 'assets', assets, dirs_exist_ok=True)
script = assets / 'scripts' / 'entrypoints.arp'
s = script.read_text(encoding='utf-8')
needle = '    s.setup()\n'
assert s.count(needle) == 1
s = s.replace(needle, needle + '''    var obj: GameObject = scene.create_object_ref("authored-burst")
    var extension: ScriptOnlyBurst = obj.add_component("ScriptOnlyBurst")
    extension.setup(s)
''')
s += '''
class ScriptOnlyBurst(ScriptComponent):
    flow: RunFlow
    timer: TimerComponent
    def setup(self, flow: RunFlow):
        self.flow = flow
        self.timer = self.game_object.add_component("TimerComponent")
        require(self.timer.start_timer(0.3, true, "burst"))
        var id: int = flow.enemy(0, false, 6.0, 0.0)
        flow.enemies[id].hp = 1000.0
        flow.grant_experience((settings["xpPerUpgrade"] as int) * 3)
    def on_event(self, name: string, payload: any):
        if name == "burst":
            for i in range(settings["extensionBurstCount"] as int):
                var angle: float = i * 2.0 * PI / settings["extensionBurstCount"]
                self.flow.bolt(self.flow.x, self.flow.y, math.cos(angle) * settings["projectileSpeed"], math.sin(angle) * settings["projectileSpeed"], 18, -1, 0, 0)
'''
script.write_text(s, encoding='utf-8')
y = (assets / 'game.yaml').read_text(encoding='utf-8')
y = y.replace('spawnPerSecond: 3', 'spawnPerSecond: 0')
(assets / 'game.yaml').write_text(y + '\nextensionBurstCount: 8\n', encoding='utf-8')
args = [str(exe), '--simulate', '--autoplay', '--frames', '90', '--mute',
        '--assets', str(assets), '--save-dir', str(out / 'save')]
rejected = subprocess.run(args + ['--script-aot'], capture_output=True, text=True, timeout=60)
assert rejected.returncode != 0 and 'SOURCE-MISMATCH' in rejected.stderr, rejected.stderr
run = subprocess.run(args, capture_output=True, text=True, timeout=120)
(out / 'extension.log').write_text(run.stdout + run.stderr, encoding='utf-8')
assert run.returncode == 0, run.stdout + run.stderr
assert int(re.search(r'^projectiles: (\d+)', run.stdout, re.M)[1]) >= 8, run.stdout
assert int(re.search(r'^upgrades: (\d+)', run.stdout, re.M)[1]) >= 3, run.stdout
assert hashlib.sha256(exe.read_bytes()).hexdigest() == before
(out / 'extension-proof.yaml').write_text(
    'type: arp.script-component-extension-proof\ncontractVersion: 1\nhostSha256: ' + before +
    '\nhostRebuilt: false\ncomponent: ScriptOnlyBurst\nfourthWeapon: eight-way-burst\n'
    'newEnemy: scripted-elite\nupgradeQueue: 3\nexecution: DefaultInterpreter\n'
    'aotSourceMismatchRejected: true\n', encoding='utf-8')
print('PASS new attached ScriptComponent: timer/events/burst/elite/upgrade queue; unchanged executable')
