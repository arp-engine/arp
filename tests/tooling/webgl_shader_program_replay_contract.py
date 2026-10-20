"""Fixture replay oracle: a native Present can contain multiple rAF workloads."""


def validate_workload_draws(draws, samples):
    if not draws or len(draws) % 3:
        raise RuntimeError('WEBGL1-CAPTURE-DRAW-COUNT: ' + repr(draws))
    validate_capture_trace(draws, samples)
    return len(draws) // 3


def validate_capture_trace(draws, samples):
    """Validate every draw across consecutive native frames, including a cut tail.

    Native capture ends independently of the JS workload. A tail is admitted
    only after a complete workload, and every available state/sample is checked.
    It is reported separately and never counted as a completed logical workload.
    """
    if len(draws) < 3:
        raise RuntimeError('WEBGL1-CAPTURE-DRAW-COUNT: ' + repr(draws))
    for index, draw in enumerate(draws):
        phase = index % 3
        first = draws[index - phase]
        if phase == 0:
            valid = draw['redPixels'] > 0 and draw['greenPixels'] == 0 and draw['blendEnabled'] is True
            expected = [samples[0], [0, 0, 0], samples[2]]
        else:
            valid = (draw['redPixels'] == first['redPixels'] and draw['greenPixels'] > 0
                     and draw['blendEnabled'] is (phase == 1))
            if phase == 2:
                valid = valid and draw['greenPixels'] == draws[index - 1]['greenPixels']
            expected = samples
        if not valid:
            raise RuntimeError('WEBGL1-CAPTURE-DRAW-STATE: ' + repr(draw))
        if draw['samples'] != expected:
            raise RuntimeError('WEBGL1-CAPTURE-NUMERICAL: ' + repr(draw))
    return dict(completedIterations=len(draws) // 3, validatedTailDraws=len(draws) % 3,
                validatedDraws=len(draws))
