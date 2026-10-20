"""Reject incomplete or corrupt later iterations, including coalesced Presents."""
import copy
import unittest
from webgl_shader_program_replay_contract import validate_workload_draws, validate_capture_trace


class WorkloadReplayTests(unittest.TestCase):
    samples = [[32, 0, 0], [0, 48, 0], [0, 0, 0]]

    def draws(self):
        return [dict(redPixels=132, greenPixels=0, blendEnabled=True, samples=[self.samples[0], [0, 0, 0], self.samples[2]]),
                dict(redPixels=132, greenPixels=132, blendEnabled=True, samples=self.samples),
                dict(redPixels=132, greenPixels=132, blendEnabled=False, samples=self.samples)]

    def test_every_complete_iteration_is_validated(self):
        for count in (1, 2, 4):
            self.assertEqual(validate_workload_draws(self.draws() * count, self.samples), count)

    def test_empty_and_incomplete_iterations_fail(self):
        for draws in ([], self.draws() + self.draws()[:1]):
            with self.assertRaisesRegex(RuntimeError, 'DRAW-COUNT'):
                validate_workload_draws(draws, self.samples)

    def test_later_iteration_state_is_not_ignored(self):
        for offset, key, value in ((1, 'redPixels', 0), (2, 'blendEnabled', True),
                                   (2, 'redPixels', 0), (2, 'greenPixels', 0)):
            draws = copy.deepcopy(self.draws() * 2)
            draws[3 + offset][key] = value
            with self.assertRaisesRegex(RuntimeError, 'DRAW-STATE'):
                validate_workload_draws(draws, self.samples)

    def test_later_iteration_numerical_output_is_not_ignored(self):
        draws = copy.deepcopy(self.draws() * 2)
        draws[5]['samples'][0][0] = 0
        with self.assertRaisesRegex(RuntimeError, 'NUMERICAL'):
            validate_workload_draws(draws, self.samples)

    def test_native_capture_boundary_tail_is_validated_and_not_counted(self):
        draws = self.draws() * 2 + self.draws()[:2]
        self.assertEqual(validate_capture_trace(draws, self.samples),
                         dict(completedIterations=2, validatedTailDraws=2, validatedDraws=8))
        for index, key, value in ((6, 'samples', []), (7, 'blendEnabled', False),
                                   (7, 'redPixels', 0), (7, 'samples', [])):
            changed = copy.deepcopy(draws)
            changed[index][key] = value
            with self.assertRaises(RuntimeError):
                validate_capture_trace(changed, self.samples)

    def test_contiguous_frame_split_does_not_reset_logical_phase(self):
        draws = self.draws() * 3
        frames = [draws[:8], draws[8:]]
        self.assertEqual(validate_workload_draws(sum(frames, []), self.samples), 3)
        frames[1][0]['blendEnabled'] = True
        with self.assertRaisesRegex(RuntimeError, 'DRAW-STATE'):
            validate_capture_trace(sum(frames, []), self.samples)


if __name__ == '__main__':
    unittest.main()
