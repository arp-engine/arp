"""Counter reconciliation, sample custody and honest negative timing results."""
import copy
from pathlib import Path
import sys
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'scripts'))
from webgl_product_performance import WebGlProductPerformance as Performance


class ProductPerformanceTests(unittest.TestCase):
    def setUp(self):
        self.plan = Performance.plan(yaml.safe_load((ROOT/'tests/fixtures/webgl1_product_performance.yaml').read_text()))
        self.plan.update(warmupFrames=1, sampleFrames=2)
        m = dict(cpuMs=1., allocations=7, deallocations=7, liveBytesBefore=100,
                 liveBytesAfter=100, processPeakBytes=1000, observed=True)
        expected = [(64, 64, 'cold-frame', 0)]*4
        for w, h in self.plan['canvasExtents']:
            if w == 48:
                expected.append((w, h, 'cold-frame', 0))
            expected.extend([(w,h,'warmup',0), (w,h,'steady',0), (w,h,'steady',1), (w,h,'cold-frame',0)])
        expected.append((48,32,'cold-frame',0))
        native = {k:0 for k in Performance.counters}
        native.update(nativeCalls=20, draws=3, forbidden=[], nativeDeleteCalls=0, preparationNativeCalls=0)
        frames = [dict(width=w, height=h, phase=phase, ordinal=i,
                       fixtureValidationChecks=phase == 'cold-frame',
                       stages=[dict(m, stage=name) for name in Performance.stages],
                       native=copy.deepcopy(native),
                       telemetry=dict(native, complete=True, accepted=1, failed=0, lost=0),
                       live=dict(objects=20), quotaPeaks=dict(frameBytes=1000, commandBytes=2000))
                  for w,h,phase,i in expected]
        self.raw = dict(type='Scene2DOrderedProductSmoke', contractVersion=1, success=True,
                        subscriptionsRetired=True, webGpuUnavailable=True, frames=len(frames),
                        gpuCompletionClaimed=False, productionReady=False,
                        performance=dict(type='WebGlProductPerformance', contractVersion=1,
                            gpuTimeMs=None, gpuCompletionClaimed=False, readinessPromoted=False,
                            productionReady=False, environment={'version':'test-only'},
                            timeDomain='CPU', peakDomain='process core lifetime', frames=frames,
                            setup=dict(stages=[dict(m,stage='setup')],native={'cold':True},
                                       navigation=[{}],resources=[{}])))

    def validate(self, raw=None):
        return Performance.validate_report(raw or self.raw, 'scene2d', self.plan)

    def test_complete_observation_and_negative_budget_are_preserved(self):
        p = self.validate()
        p['frames'][5]['stages'][0]['cpuMs'] = 40.
        p['frames'][6]['stages'][0]['cpuMs'] = 20.
        result = Performance.summarize(self.validate(), self.plan)[0]
        self.assertEqual(result['overBudgetOrdinals'], [0, 1])
        self.assertEqual(result['stageCpuMs']['total']['maximum'], 40.)
        self.assertFalse(result['zeroCoreAllocation'])
        self.assertFalse(result['gpuFrameBudgetValidated'])

    def test_missing_duplicate_or_reordered_samples_reject(self):
        for mutation in ('missing', 'duplicate', 'reordered'):
            raw = copy.deepcopy(self.raw)
            frames = raw['performance']['frames']
            if mutation == 'missing':
                frames.pop()
            elif mutation == 'duplicate':
                frames[6] = copy.deepcopy(frames[5])
            else:
                frames[5], frames[6] = frames[6], frames[5]
            with self.subTest(mutation=mutation), self.assertRaisesRegex(ValueError, 'COVERAGE|ORDER'):
                self.validate(raw)

    def test_native_counter_and_lifetime_tampering_reject(self):
        for field in Performance.counters:
            raw = copy.deepcopy(self.raw)
            raw['performance']['frames'][5]['telemetry'][field] += 1
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, 'COUNTER-MISMATCH'):
                self.validate(raw)
        raw = copy.deepcopy(self.raw)
        raw['performance']['frames'][6]['live']['objects'] += 1
        with self.assertRaisesRegex(ValueError, 'LIFETIME-DRIFT'):
            self.validate(raw)

    def test_bad_cpu_measurement_missing_stage_or_fake_gpu_time_reject(self):
        for key, value in (('cpuMs', float('nan')), ('cpuMs', float('inf')),
                           ('cpuMs', -1), ('allocations', -1), ('processPeakBytes', 1), ('observed', False)):
            raw = copy.deepcopy(self.raw)
            raw['performance']['frames'][5]['stages'][0][key] = value
            with self.subTest(key=key,value=value), self.assertRaises(ValueError):
                self.validate(raw)
        for key,value in (('readinessPromoted', True), ('gpuTimeMs', 1)):
            raw = copy.deepcopy(self.raw)
            raw['performance'][key] = value
            with self.assertRaisesRegex(ValueError, 'IDENTITY'):
                self.validate(raw)
        raw = copy.deepcopy(self.raw)
        raw['performance']['frames'][5]['stages'].pop()
        with self.assertRaisesRegex(ValueError, 'STAGE-COVERAGE'):
            self.validate(raw)

    def test_bounded_explicit_plan(self):
        for key,value in (('sampleFrames',0), ('sampleFrames',241), ('warmupFrames',True),
                          ('repetitions',11), ('cpuBudgetMs',float('inf')), ('canvasExtents',[[1,1]])):
            with self.subTest(key=key), self.assertRaises(ValueError):
                Performance.plan(dict(self.plan, **{key:value}))

    def test_percentile_retains_slow_tail(self):
        r = Performance.distribution(list(range(20)))
        self.assertEqual((r['median'],r['p95'],r['maximum']), (9.5,18,19))

    def test_final_drop_deletes_reconcile_without_relaxing_execution_counts(self):
        final = self.raw['performance']['frames'][-1]
        final['native']['nativeCalls'] += 3
        final['native']['nativeDeleteCalls'] = 3
        self.validate()
        final['native']['nativeDeleteCalls'] = 2
        with self.assertRaisesRegex(ValueError, 'COUNTER-MISMATCH'):
            self.validate()

    def test_native_preparation_calls_observed_without_fabricating_execution_telemetry(self):
        f = self.raw['performance']['frames'][5]
        f['native']['nativeCalls'] += 7
        f['native']['preparationNativeCalls'] = 7
        f['native']['forbidden'] = ['createFramebuffer']
        self.validate()
        self.assertEqual(Performance.summarize(self.validate(), self.plan)[0]['hotPathFindings'], ['createFramebuffer'])
        f['native']['preparationNativeCalls'] = 6
        with self.assertRaisesRegex(ValueError, 'COUNTER-MISMATCH'):
            self.validate()

    def test_negative_fixture_work_is_excluded_from_steady_measurements(self):
        self.raw['performance']['frames'][5]['fixtureValidationChecks'] = True
        with self.assertRaisesRegex(ValueError, 'NEGATIVE-FIXTURE-IN-STEADY'):
            self.validate()


if __name__ == '__main__':
    unittest.main()
