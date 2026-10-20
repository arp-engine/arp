"""Reproducible product CPU observations; never an RHI admission source."""
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import shutil
from uuid import uuid4

import yaml

from browser_contract_runner import BrowserContractRunner
from webgl1_runtime_build import WebGl1RuntimeBuilder
from webgl_visual_validation import WebGlVisualValidation


class ProductPerformanceBrowser(BrowserContractRunner):
    def browser_command(self, profile, url):
        command = super().browser_command(profile, url)
        if Path(self.browser).stem.lower() != 'firefox':
            command.insert(1, '--enable-logging=stderr')
        (self.output/'browser-command.json').write_text(json.dumps(command, indent=2), encoding='utf8')
        return command


class WebGlProductPerformance:
    profiles = {
        'scene2d': ('scene2dOnly', 'Scene2DOrderedProductSmoke', 3),
        'gui': ('guiOnly', 'GuiOrderedProductSmoke', 4),
        'basic-forward': ('basicForwardOnly', 'BasicForwardProductSmoke', 9),
    }
    stages = ('total', 'inputs', 'build', 'compile', 'prepare', 'record', 'submit', 'presentRetire')
    counters = ('nativeCalls', 'draws', 'uniformCalls', 'uniformBytes', 'bufferUpdateCalls',
                'bufferUpdateBytes', 'textureUpdateCalls', 'textureUpdateBytes')

    def __init__(self, root, manifest, config, browser, output):
        self.root = Path(root).resolve()
        self.manifest = Path(manifest).resolve()
        self.config = Path(config).resolve()
        self.browser = Path(browser).resolve()
        self.output = Path(output).resolve()

    @staticmethod
    def plan(document):
        expected = {'type', 'contractVersion', 'warmupFrames', 'sampleFrames', 'repetitions',
                    'timeoutSeconds', 'cpuBudgetMs', 'budgetDomain', 'profiles', 'canvasExtents', 'resourceScale'}
        if (not isinstance(document, dict) or set(document) != expected
                or document['type'] != 'WebGlProductPerformancePlan' or document['contractVersion'] != 1
                or document['profiles'] != list(WebGlProductPerformance.profiles)
                or document['canvasExtents'] != [[64, 64], [48, 32]]
                or not document['budgetDomain'] or not document['resourceScale']):
            raise ValueError('PRODUCT-PERF-PLAN')
        for key, maximum in (('warmupFrames', 60), ('sampleFrames', 240),
                             ('repetitions', 10), ('timeoutSeconds', 600)):
            if type(document[key]) is not int or not 1 <= document[key] <= maximum:
                raise ValueError('PRODUCT-PERF-CAPACITY: '+key)
        if (type(document['cpuBudgetMs']) not in (int, float)
                or not math.isfinite(document['cpuBudgetMs']) or document['cpuBudgetMs'] <= 0):
            raise ValueError('PRODUCT-PERF-BUDGET')
        return document

    @staticmethod
    def measurement(m):
        if (not isinstance(m, dict) or m.get('observed') is not True
                or type(m.get('cpuMs')) not in (int, float) or not math.isfinite(m['cpuMs']) or m['cpuMs'] < 0):
            raise ValueError('PRODUCT-PERF-TIME')
        for key in ('allocations', 'deallocations', 'liveBytesBefore', 'liveBytesAfter', 'processPeakBytes'):
            if type(m.get(key)) is not int or m[key] < 0:
                raise ValueError('PRODUCT-PERF-ALLOCATOR')
        if m['processPeakBytes'] < max(m['liveBytesBefore'], m['liveBytesAfter']):
            raise ValueError('PRODUCT-PERF-PEAK-DOMAIN')

    @classmethod
    def validate_report(cls, result, profile, plan):
        _, kind, draws = cls.profiles[profile]
        p = result.get('performance')
        if (result.get('type') != kind or result.get('contractVersion') != 1
                or any(result.get(k) is not True for k in ('success', 'subscriptionsRetired', 'webGpuUnavailable'))
                or result.get('gpuCompletionClaimed') is not False or result.get('productionReady') is not False
                or not isinstance(p, dict) or p.get('type') != 'WebGlProductPerformance'
                or p.get('contractVersion') != 1 or p.get('gpuTimeMs') is not None
                or any(p.get(k) is not False for k in ('gpuCompletionClaimed', 'readinessPromoted', 'productionReady'))
                or not p.get('environment') or not p.get('timeDomain') or not p.get('peakDomain')):
            raise ValueError('PRODUCT-PERF-RESULT-IDENTITY')
        setup = p.get('setup', {})
        if (len(setup.get('stages', [])) != 1 or setup['stages'][0].get('stage') != 'setup'
                or not setup.get('native') or not setup.get('navigation') or not setup.get('resources')):
            raise ValueError('PRODUCT-PERF-COLD-MISSING')
        cls.measurement(setup['stages'][0])
        frames = p.get('frames', [])
        if len(frames) != 8+2*(plan['warmupFrames']+plan['sampleFrames']) or result.get('frames') != len(frames):
            raise ValueError('PRODUCT-PERF-FRAME-COVERAGE')
        expected = [(64, 64, 'cold-frame', 0)]*4
        for width, height in plan['canvasExtents']:
            if width == 48:
                expected.append((width, height, 'cold-frame', 0))
            for phase, count in (('warmup', plan['warmupFrames']), ('steady', plan['sampleFrames'])):
                expected.extend((width, height, phase, i) for i in range(count))
            expected.append((width, height, 'cold-frame', 0))
        expected.append((48, 32, 'cold-frame', 0))
        if [(f.get('width'), f.get('height'), f.get('phase'), f.get('ordinal')) for f in frames] != expected:
            raise ValueError('PRODUCT-PERF-SAMPLE-ORDER')
        for f in frames:
            if f['phase'] in ('steady', 'warmup') and f.get('fixtureValidationChecks') is not False:
                raise ValueError('PRODUCT-PERF-NEGATIVE-FIXTURE-IN-STEADY')
            stages = f.get('stages', [])
            if [m.get('stage') for m in stages] != list(cls.stages):
                raise ValueError('PRODUCT-PERF-STAGE-COVERAGE')
            for m in stages:
                cls.measurement(m)
            native, telemetry = f.get('native', {}), f.get('telemetry', {})
            if (telemetry.get('complete') is not True or telemetry.get('accepted') != 1
                    or telemetry.get('failed') != 0 or telemetry.get('lost') != 0
                    or native.get('draws') != draws or not isinstance(native.get('forbidden'), list)):
                raise ValueError('PRODUCT-PERF-EXECUTION')
            for key in cls.counters:
                expected_counter = native.get(key)
                if type(expected_counter) is not int or expected_counter < 0 or type(telemetry.get(key)) is not int:
                    raise ValueError('PRODUCT-PERF-COUNTER-TYPE: '+key)
                if key == 'nativeCalls':
                    deletes = native.get('nativeDeleteCalls')
                    preparation = native.get('preparationNativeCalls')
                    if (type(deletes) is not int or type(preparation) is not int
                            or deletes < 0 or preparation < 0 or deletes + preparation > expected_counter):
                        raise ValueError('PRODUCT-PERF-LIFECYCLE-COUNTER')
                    expected_counter -= deletes + preparation
                if (type(native.get(key)) is not int or native[key] < 0
                        or telemetry.get(key) != expected_counter):
                    raise ValueError('PRODUCT-PERF-COUNTER-MISMATCH: '+key)
        # Only steady frames compare lifetimes: explicit final renderer destruction is a cold event.
        for extent in plan['canvasExtents']:
            steady = [f for f in frames if f['phase'] == 'steady' and [f['width'], f['height']] == extent]
            for f in steady[1:]:
                if (f['live'] != steady[0]['live']
                        or f['stages'][0]['liveBytesAfter'] != steady[0]['stages'][0]['liveBytesAfter']):
                    raise ValueError('PRODUCT-PERF-LIFETIME-DRIFT')
        return p

    @staticmethod
    def distribution(values):
        ordered = sorted(values)
        if not ordered or any(not math.isfinite(n) or n < 0 for n in ordered):
            raise ValueError('PRODUCT-PERF-DISTRIBUTION')
        middle = len(ordered)//2
        return dict(count=len(ordered), median=(ordered[middle] if len(ordered)%2 else
                    (ordered[middle-1]+ordered[middle])/2),
                    p95=ordered[math.ceil(.95*len(ordered))-1], maximum=ordered[-1], minimum=ordered[0])

    @classmethod
    def summarize(cls, p, plan):
        result = []
        for width, height in plan['canvasExtents']:
            frames = [f for f in p['frames'] if f['phase'] == 'steady' and f['width'] == width and f['height'] == height]
            stages = {name: cls.distribution([f['stages'][i]['cpuMs'] for f in frames])
                      for i, name in enumerate(cls.stages)}
            result.append(dict(canvas=[width, height], stageCpuMs=stages,
                cpuBudgetMs=plan['cpuBudgetMs'], overBudgetOrdinals=[f['ordinal'] for f in frames
                    if f['stages'][0]['cpuMs'] > plan['cpuBudgetMs']],
                cpuBudgetAllSamplesWithin=all(f['stages'][0]['cpuMs'] <= plan['cpuBudgetMs'] for f in frames),
                hostAllocations=cls.distribution([f['stages'][0]['allocations'] for f in frames]),
                processCorePeakBytes=max(f['stages'][0]['processPeakBytes'] for f in frames),
                quotaPeaks=frames[-1]['quotaPeaks'], live=frames[-1]['live'],
                nativeCounters={k: cls.distribution([f['native'][k] for f in frames]) for k in cls.counters},
                hotPathFindings=sorted({name for f in frames for name in f['native']['forbidden']}),
                preparationNativeCalls=cls.distribution([f['native']['preparationNativeCalls'] for f in frames]),
                nativeDeleteCalls=cls.distribution([f['native']['nativeDeleteCalls'] for f in frames]),
                zeroCoreAllocation=all(f['stages'][0]['allocations'] == 0 for f in frames),
                gpuTimeMs=None, gpuFrameBudgetValidated=False))
        return result

    def run(self):
        plan = self.plan(yaml.safe_load(self.config.read_text(encoding='utf8')))
        if not self.browser.is_file():
            raise ValueError('PRODUCT-PERF-BROWSER-MISSING')
        # Reuse frozen build/program validation; no capture tool is called here.
        custody = WebGlVisualValidation(self.root, self.manifest, self.output, self.browser,
                                        browser=self.browser)
        document, files = custody.validate()
        if document.get('buildType') != 'Release':
            raise ValueError('PRODUCT-PERF-RELEASE-REQUIRED')
        self.output.mkdir(parents=True, exist_ok=True)
        out = self.output/(datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')+'-'+uuid4().hex[:8])
        out.mkdir(exist_ok=False)
        bundle = out/'bundle'; bundle.mkdir()
        for name in files:
            shutil.copyfile(self.manifest.parent/name, bundle/name)
        if custody.hashes(bundle) != files:
            raise ValueError('PRODUCT-PERF-FROZEN-COPY')
        programs = custody.programs(bundle)
        tool_files = [Path(__file__), Path(__file__).with_name('browser_contract_runner.py'),
                      Path(__file__).with_name('webgl_visual_validation.py'),
                      Path(__file__).with_name('emscripten_preload_package.py')]
        tool_inputs = {str(p.resolve()):WebGl1RuntimeBuilder.sha(p) for p in tool_files}
        report = dict(type='WebGlProductPerformanceCohort', contractVersion=1, status='Running',
            rootManifest=str(self.manifest), files=files, programs=programs, plan=plan,
            planSha256=WebGl1RuntimeBuilder.sha(self.config),
            toolInputs=tool_inputs,
            browser=dict(path=str(self.browser), sha256=WebGl1RuntimeBuilder.sha(self.browser)),
            runs=[], productionReady=False, readinessPromoted=False, gpuFrameBudgetValidated=False)
        path = out/'performance-manifest.yaml'
        def save():
            path.write_text(yaml.safe_dump(report, sort_keys=False), encoding='utf8')
        save()
        try:
            for repetition in range(plan['repetitions']):
                for profile, (query, _, _) in self.profiles.items():
                    entry = f'pipeline.html?{query}=1&productPerf=1&warmup={plan["warmupFrames"]}&samples={plan["sampleFrames"]}'
                    folder = out/f'{profile}-{repetition+1}'
                    print(f'[product-performance] {profile} repetition {repetition+1}', flush=True)
                    raw = ProductPerformanceBrowser(self.browser, bundle, entry, folder, plan['timeoutSeconds']).run()
                    p = self.validate_report(raw, profile, plan)
                    report['runs'].append(dict(profile=profile, repetition=repetition+1,
                        report=str(folder/'report.json'), reportSha256=WebGl1RuntimeBuilder.sha(folder/'report.json'),
                        summary=self.summarize(p, plan), setup=p['setup'],
                        productEnteredAtMs=p['productEnteredAtMs'], setupFinishedAtMs=p['setupFinishedAtMs'],
                        wasmCompileMs=p['wasmCompileMs'], environment=p['environment']))
                    save()
            report['status'] = 'Observed'
            if custody.hashes(bundle) != files:
                raise ValueError('PRODUCT-PERF-BUNDLE-MUTATED')
            if tool_inputs != {str(p.resolve()):WebGl1RuntimeBuilder.sha(p) for p in tool_files}:
                raise ValueError('PRODUCT-PERF-TOOL-MUTATED')
        except Exception as error:
            report['status'] = 'Failed'
            report['diagnostic'] = str(error)
            raise
        finally:
            report['frozenBundleUnchanged'] = custody.hashes(bundle) == files
            save()
        return path
