"""Verify FogPhaseHistory/1 from native capture buffers and decompose illumination."""
import json
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw


def sample_volume(volume, uv, view_depth, near, far):
    gz, gy, gx, _ = volume.shape
    xy = np.clip(uv * [gx, gy] - .5, 0, [gx - 1, gy - 1])
    base, f = xy.astype(int), xy - xy.astype(int)
    depth = np.clip(view_depth, near, far)
    z = np.minimum((np.log(depth / near) / np.log(far / near) * gz).astype(int), gz - 1)
    a, b = near * (far / near) ** (z / gz), near * (far / near) ** ((z + 1) / gz)
    t = np.clip((depth - a) / (b - a), 0, 1)
    result = np.zeros((len(uv), 4))
    for dy in range(2):
        for dx in range(2):
            x, y = np.minimum(base[:, 0] + dx, gx - 1), np.minimum(base[:, 1] + dy, gy - 1)
            low, high = volume[np.maximum(z - 1, 0), y, x].copy(), volume[z, y, x]
            low[z == 0] = [0, 0, 0, 1]
            tr = high[:, 3] / np.maximum(low[:, 3], 1e-20)
            partial = np.maximum(tr, 1e-20) ** t
            fraction = np.where(abs(1 - tr) < .0001, t, (1 - partial) / np.maximum(1 - tr, 1e-20))
            weight = (f[:, 0] if dx else 1 - f[:, 0]) * (f[:, 1] if dy else 1 - f[:, 1])
            result[:, :3] += (low[:, :3] + (high[:, :3] - low[:, :3]) * fraction[:, None]) * weight[:, None]
            result[:, 3] += low[:, 3] * partial * weight
    return result


def inspect(folder):
    folder = Path(folder)
    raw = (folder / 'uniform.bin').read_bytes()
    p = np.frombuffer(raw, '<f4').astype(float)
    gx, gy, gz = np.frombuffer(raw, '<u4', 3, 592)
    source = np.fromfile(folder / 'FogScattering.bin', '<f4').reshape(gz, gy, gx, 8)
    # Prove the light control reached executed shader code, not only the UBO.
    if p[163] == 0:
        assert not np.any(source[..., :3]), 'zero ambient gain produced ambient scattering'
    history = np.fromfile(folder / 'FogResolved.bin', '<f4').reshape(gz, gy, gx, 12)
    actual = np.fromfile(folder / 'FogIntegrated.bin', '<f4').reshape(gz, gy, gx, 4)
    assert np.isfinite(history).all() and np.isfinite(actual).all()
    medium = np.fromfile(folder / 'FogMedia.bin', '<f4').reshape(gz, gy, gx, 8)
    unoccluded = medium[..., :3] * (p[172:175] * p[171])
    valid = unoccluded > 1e-9
    visibility = source[..., 4:7][valid] / unoccluded[valid]
    assert visibility.min() > -1e-5 and visibility.max() <= 1.00002
    assert visibility.max() > .95, 'no full-strength phase-free sunlight sample'
    y, x = np.mgrid[:gy, :gx]
    uv = np.c_[(x.ravel() + .5) / gx, (y.ravel() + .5) / gy]
    inv, view = p[:16].reshape(4, 4), p[16:32].reshape(4, 4)

    def unproject(uv, z):
        clip = np.c_[uv * 2 - 1, np.broadcast_to(z, len(uv)), np.ones(len(uv))]
        w = clip @ inv.T
        return w[:, :3] / w[:, 3:4]

    a, b = unproject(uv, 0), unproject(uv, 1)
    ray = b - a
    scale = np.linalg.norm(ray, axis=1) / (abs(np.c_[b, np.ones(len(b))] @ view[2]) - abs(np.c_[a, np.ones(len(a))] @ view[2]))
    ray /= np.linalg.norm(ray, axis=1)[:, None]
    cosine, g = ray @ -p[168:171], p[167]
    phase = ((1 - g * g) / (4 * np.pi * (1 + g * g - 2 * g * cosine) ** 1.5)).reshape(gy, gx)
    distance = np.diff(p[152] * (p[153] / p[152]) ** (np.arange(gz + 1) / gz))[:, None, None] * scale.reshape(gy, gx)
    sigma = history[..., 3]
    tau = sigma * distance
    tr = np.exp(-tau)
    prefix = np.exp(-np.concatenate([np.zeros_like(tau[:1]), np.cumsum(tau, axis=0)[:-1]]))
    length = -np.expm1(-tau) / np.maximum(sigma, 1e-30)
    length = np.where(sigma == 0, distance, length)
    ambient = np.cumsum(history[..., :3] * (prefix * length)[..., None], axis=0)
    direct = np.cumsum(history[..., 8:11] * phase[None, ..., None] * (prefix * length)[..., None], axis=0)
    error = abs(actual[..., :3] - (ambient + direct))
    trans_error = abs(actual[..., 3] - np.cumprod(tr, axis=0))
    assert error.max() < 1e-4 and trans_error.max() < 1e-4, (error.max(), trans_error.max())
    report = {'historyStride': 48, 'scatteringStride': 32,
              'integrationMaxError': float(error.max()), 'transmittanceMaxError': float(trans_error.max()),
              'phaseRange': [float(phase.min()), float(phase.max())],
              'historyHasViewPhase': False, 'grid': [int(gx), int(gy), int(gz)]}
    report['phaseFreeVisibilityRange'] = [float(visibility.min()), float(visibility.max())]
    info = json.loads((folder / 'report.json').read_text())['textures']['FogDepth']
    width, height = info['width'], info['height']
    depth = np.fromfile(folder / 'FogDepth.bin', '<f4')
    yy, xx = np.mgrid[:height, :width]
    full_uv = np.c_[(xx.ravel() + .5) / width, (yy.ravel() + .5) / height]
    world = unproject(full_uv, depth)
    view_depth = abs(np.c_[world, np.ones(len(world))] @ view[2])
    trans = actual[..., 3:4].astype(float)
    images = []
    for name, value in [('ambient', ambient), ('sun', direct)]:
        records = np.concatenate([value, trans], axis=-1)
        sampled = sample_volume(records, full_uv, view_depth, p[152], p[153])[:, :3]
        np.save(folder / (name + '-visible.npy'), sampled.reshape(height, width, 3))
        # Same display curve for both terms; no diagnostic amplification.
        image = Image.fromarray(np.uint8(np.clip((1 - np.exp(-sampled.reshape(height, width, 3))) * 255, 0, 255)))
        image.save(folder / (name + '-visible.png'))
        images.append(image)
        report[name + 'VisibleMean'] = sampled.mean(axis=0).tolist()
    comparison = Image.new('RGB', (width * 2, height + 24))
    comparison.paste(images[0], (0, 24))
    comparison.paste(images[1], (width, 24))
    ImageDraw.Draw(comparison).text((8, 5), 'Isotropic ambient | Shadowed sunlight (same display mapping, no gain)', fill='white')
    comparison.save(folder / 'decomposition.png')
    (folder / 'phase-history.json').write_text(json.dumps(report, indent=2))
    print(folder.name, json.dumps(report))


if __name__ == '__main__':
    inspect(sys.argv[1])
