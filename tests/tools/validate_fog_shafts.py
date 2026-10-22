"""Independent analytic slit-shadow single-scattering reference.

The reference integrates the exact ray intervals inside the extruded box shadows.
It does not sample the GPU shadow map or use the production froxel quadrature.
Like the runtime fog model, it omits medium self-shadowing and multiple scattering.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

SPANS = [(-32, -5.175), (-4.825, -0.5), (0.5, 4), (6, 32)]


def shadow_interval(origin, ray, maximum, left, right, slope=0.35):
    lo = np.zeros_like(maximum)
    hi = maximum.copy()
    # Convex shadow of a finite box extruded away from a directional light.
    planes = [((1, 0, 0), right), ((-1, 0, 0), -left),
              ((0, 0, 1), 20.025), ((0, 1, 0), 32),
              ((0, -1, slope), 16 + slope * 20.025),
              ((0, 1, -slope), 32 - slope * 19.975)]
    for normal, bound in planes:
        normal = np.array(normal)
        speed = ray @ normal
        remaining = bound - origin @ normal
        t = remaining / np.where(abs(speed) > 1e-12, speed, 1)
        hi = np.where(speed > 1e-12, np.minimum(hi, t), hi)
        lo = np.where(speed < -1e-12, np.maximum(lo, t), lo)
        hi = np.where((abs(speed) <= 1e-12) & (remaining < 0), -1, hi)
    return lo, hi


def visible_weight(origin, ray, maximum, sigma):
    weight = -np.expm1(-sigma * maximum)
    for left, right in SPANS:
        lo, hi = shadow_interval(origin, ray, maximum, left, right)
        weight -= np.where(hi > lo, np.exp(-sigma * lo) - np.exp(-sigma * np.maximum(lo, hi)), 0)
    return np.maximum(weight, 0)


def phase(cosine, g):
    return (1 - g * g) / (4 * np.pi * (1 + g * g - 2 * g * cosine) ** 1.5)


def self_test():
    # Known opaque/clear intervals, then verify against dense independent marching.
    origins = np.array([[0., 4., -18.], [-5., 4., -18.], [2., 4., -18.],
                        [24., 4., 0.], [24., 4., 0.]])
    rays = np.array([[0., 0., 1.], [0., 0., 1.], [0., 0., 1.],
                     [-1., 0., 0.], [-0.9, -0.1, 0.3]])
    rays /= np.linalg.norm(rays, axis=1)[:, None]
    maximum = np.full(5, 35.)
    exact = visible_weight(origins, rays, maximum, .025)
    assert exact[0] > .5 and exact[1] > .5 and exact[2] < 1e-12
    steps = 200000
    t = (np.arange(steps) + .5) * 35 / steps
    for i in range(5):
        points = origins[i] + t[:, None] * rays[i]
        z_enter = np.maximum(0, 19.975 - points[:, 2])
        z_exit = 20.025 - points[:, 2]
        intersects = ((z_exit >= z_enter) &
                      (points[:, 1] + .35 * z_exit >= -16) &
                      (points[:, 1] + .35 * z_enter <= 32))
        opaque_x = np.zeros(steps, dtype=bool)
        for left, right in SPANS:
            opaque_x |= (points[:, 0] >= left) & (points[:, 0] <= right)
        lit = ~(intersects & opaque_x)
        numeric = np.sum(lit * np.exp(-.025 * t) * .025 * 35 / steps)
        assert abs(numeric - exact[i]) < 2e-5, (i, numeric, exact[i])
    print('analytic shadow interval / dense 200000-step integration: PASS')


def validate(folder):
    folder = Path(folder)
    p = np.fromfile(folder / 'uniform.bin', '<f4').astype(np.float64)
    grid = np.frombuffer((folder / 'uniform.bin').read_bytes(), '<u4', 4, 592)[:3].astype(int)
    gx, gy, gz = grid
    meta = json.loads((folder / 'report.json').read_text())
    width, height = (meta['textures']['FogDepth'][k] for k in ('width', 'height'))
    assert p[161] == 0 and np.allclose(p[192:195], 0), 'reference requires homogeneous medium, zero ambient'
    assert abs(p[168]) < 1e-7 and abs(p[169] / p[170] - .35) < 1e-6
    depth = np.fromfile(folder / 'FogDepth.bin', '<f4').reshape(height, width)
    y, x = np.mgrid[:height, :width]
    uv = np.stack([(x + .5) / width, (y + .5) / height], -1).reshape(-1, 2)
    inv, view = p[:16].reshape(4, 4), p[16:32].reshape(4, 4)

    def world(z):
        clip = np.c_[uv * 2 - 1, np.broadcast_to(z, len(uv)), np.ones(len(uv))]
        w = clip @ inv.T
        return w[:, :3] / w[:, 3:4]

    a, b = world(0), world(1)
    az = abs(np.c_[a, np.ones(len(a))] @ view[2])
    bz = abs(np.c_[b, np.ones(len(b))] @ view[2])
    scale = np.linalg.norm(b - a, axis=1) / (bz - az)
    ray = (b - a) / np.linalg.norm(b - a, axis=1)[:, None]
    near, far, sigma, g = p[152], p[153], p[160], p[167]
    origin = a + (b - a) * ((near - az) / (bz - az))[:, None]
    surface = world(depth.reshape(-1))
    view_depth = abs(np.c_[surface, np.ones(len(surface))] @ view[2])
    view_depth = np.clip(view_depth, near, far)
    maximum = (view_depth - near) * scale
    reference = (visible_weight(origin, ray, maximum, sigma) * phase(ray @ -p[168:171], g))[:, None]
    reference = reference * (p[164:167] * p[172:175] * p[171])
    integrated = np.fromfile(folder / 'FogIntegrated.bin', '<f4').reshape(gz, gy, gx, 4)
    xy = np.clip(uv * grid[:2] - .5, 0, grid[:2] - 1)
    base = xy.astype(int)
    fraction = xy - base
    slice_f = np.clip(np.log(view_depth / near) / np.log(far / near) * gz, 0, gz)
    z = np.minimum(slice_f.astype(int), gz - 1)
    za, zb = near * (far / near) ** (z / gz), near * (far / near) ** ((z + 1) / gz)
    t = np.clip((view_depth - za) / (zb - za), 0, 1)
    gpu = np.zeros((len(uv), 4))
    for dy in range(2):
        for dx in range(2):
            cx, cy = np.minimum(base[:, 0] + dx, gx - 1), np.minimum(base[:, 1] + dy, gy - 1)
            low = integrated[np.maximum(z - 1, 0), cy, cx].astype(float)
            low[z == 0] = [0, 0, 0, 1]
            high = integrated[z, cy, cx].astype(float)
            tr = high[:, 3] / np.maximum(low[:, 3], 1e-20)
            partial = np.maximum(tr, 1e-20) ** t
            f = np.where(abs(1 - tr) < .0001, t, (1 - partial) / np.maximum(1 - tr, 1e-20))
            w = (fraction[:, 0] if dx else 1 - fraction[:, 0]) * (fraction[:, 1] if dy else 1 - fraction[:, 1])
            gpu[:, :3] += (low[:, :3] + (high[:, :3] - low[:, :3]) * f[:, None]) * w[:, None]
            gpu[:, 3] += low[:, 3] * partial * w
    direct = gpu[:, :3]
    ref_l, actual_l = reference.mean(1), direct.mean(1)
    peak = np.percentile(ref_l, 99)
    lit, dark = ref_l > peak * .5, ref_l < peak * .001
    error = abs(actual_l - ref_l)
    metrics = {'grid': grid.tolist(), 'reference': 'analytic finite-panel shadow ray intervals',
               'peakP99': float(peak), 'meanAbsoluteError': float(error.mean()),
               'normalizedRmse': float(np.sqrt(np.mean(error ** 2)) / peak),
               'litMedianRatio': float(np.median(actual_l[lit] / ref_l[lit])),
               'darkPixelCount': int(dark.sum()),
               'darkP99OverPeak': float(np.percentile(actual_l[dark], 99) / peak) if dark.any() else None,
               'maxTransmittanceError': float(abs(gpu[:, 3] - np.exp(-sigma * maximum)).max())}
    # Source cross-section: toward preset has parallel x rows at fixed world z.
    # This catches sub-grid apertures that whole-frame RMSE can hide.
    row = gy // 2
    z_values = near * (far / near) ** ((np.arange(gz) + .5) / gz)
    u = (np.arange(gx) + .5) / gx
    v = np.full(gx, (row + .5) / gy)
    def grid_world(ndc_z):
        c = np.c_[u * 2 - 1, v * 2 - 1, np.full(gx, ndc_z), np.ones(gx)] @ inv.T
        return c[:, :3] / c[:, 3:4]
    ga, gb = grid_world(0), grid_world(1)
    gaz = abs(np.c_[ga, np.ones(gx)] @ view[2])
    gbz = abs(np.c_[gb, np.ones(gx)] @ view[2])
    center_z = ga[gx // 2, 2] + (gb[gx // 2, 2] - ga[gx // 2, 2]) * ((z_values - gaz[gx // 2]) / (gbz[gx // 2] - gaz[gx // 2]))
    zi = int(np.argmin(abs(center_z - 10)))
    positions = ga + (gb - ga) * ((z_values[zi] - gaz) / (gbz - gaz))[:, None]
    if np.ptp(positions[:, 2]) < .01 and abs(p[156]) < .001:
        records = np.fromfile(folder / 'FogScattering.bin', '<f4')
        stride = len(records) // (gx * gy * gz)
        records = records.reshape(gz, gy, gx, stride)
        if stride == 8:
            visibility = records[zi, row, :, 4] / (sigma * p[164] * p[172] * p[171])
        else:
            gr = gb - ga
            gr /= np.linalg.norm(gr, axis=1)[:, None]
            visibility = records[zi, row, :, 0] / (sigma * p[164] * p[172] * p[171] * phase(gr @ -p[168:171], g))
        dx = float(abs(np.diff(positions[:, 0])).mean())
        widths = []
        for center, aperture_width in [(-5., .35), (0., 1.), (5., 2.)]:
            region = abs(positions[:, 0] - center) < 2.3
            widths.append({'apertureMeters': aperture_width,
                           'integratedVisibilityWidthMeters': float(visibility[region].sum() * dx),
                           'peakVisibility': float(visibility[region].max()),
                           'cellsAboveHalfVisibility': int((visibility[region] >= .5).sum())})
        metrics['sourceCrossSection'] = {'worldZ': float(positions[0, 2]), 'cellWidthMeters': dx, 'apertures': widths}
    np.save(folder / 'reference.npy', reference.reshape(height, width, 3))
    np.save(folder / 'gpu-direct.npy', direct.reshape(height, width, 3))
    def picture(rgb):
        return Image.fromarray(np.uint8(np.clip((1 - np.exp(-rgb.reshape(height, width, 3))) * 255, 0, 255)))
    picture(reference).save(folder / 'reference.png')
    picture(direct).save(folder / 'gpu-direct.png')
    comparison = Image.new('RGB', (width * 2, height + 24))
    comparison.paste(picture(reference), (0, 24))
    comparison.paste(picture(direct), (width, 24))
    ImageDraw.Draw(comparison).text((8, 5), 'Analytic single-scattering reference | GPU direct scattering (same display mapping)', fill='white')
    comparison.save(folder / 'comparison.png')
    (folder / 'metrics.json').write_text(json.dumps(metrics, indent=2))
    print(folder.name, json.dumps(metrics))
    return metrics


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('folder', nargs='?')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    if args.folder:
        validate(args.folder)
