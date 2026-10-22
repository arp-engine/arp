"""Continuous recorded-medium / native-shadow reference, independent of froxels.

Uses exact authored medium functions and captured CSM. This isolates fog sampling;
it does not certify caster geometry or replace the analytic slit reference.
"""
import argparse
import json
from pathlib import Path
import time

import numpy as np
from PIL import Image, ImageDraw

from inspect_fog_phase_history import sample_volume


def smooth(t):
    t = np.clip(t, 0, 1)
    return t * t * (3 - 2 * t)


def noise(points, seed):
    cell = np.floor(points).astype(np.int32)
    f = smooth(points - np.floor(points))
    value = np.zeros(len(points))
    for z in range(2):
        for y in range(2):
            for x in range(2):
                at = (cell + [x, y, z]).astype(np.uint32)
                h = np.full(len(points), seed, dtype=np.uint32)
                h = (h ^ (at[:, 0] * np.uint32(374761393))) * np.uint32(668265263)
                h = (h ^ (at[:, 1] * np.uint32(1274126177))) * np.uint32(461845907)
                h = (h ^ (at[:, 2] * np.uint32(2246822519))) * np.uint32(3266489917)
                h ^= h >> 13
                h *= np.uint32(1274126177)
                h ^= h >> 16
                w = (f[:, 0] if x else 1-f[:, 0]) * (f[:, 1] if y else 1-f[:, 1]) * (f[:, 2] if z else 1-f[:, 2])
                value += (h >> 8).astype(float) / 16777216 * w
    return value


class RecordedScene:
    def __init__(self, folder):
        self.folder = Path(folder)
        raw = (self.folder / 'uniform.bin').read_bytes()
        self.p = np.frombuffer(raw, '<f4').astype(float)
        self.q = np.frombuffer(raw, '<u4')
        self.grid = self.q[148:151].astype(int)
        self.inv = self.p[:16].reshape(4, 4)
        self.view = self.p[16:32].reshape(4, 4)
        self.matrices = self.p[64:128].reshape(4, 4, 4)
        self.rects = self.p[128:144].reshape(4, 4)
        self.info = json.loads((self.folder / 'report.json').read_text())
        w, h = [self.info['textures']['FogShadow'][v] for v in ('width', 'height')]
        self.shadow = np.fromfile(self.folder / 'FogShadow.bin', '<f4').reshape(h, w)
        self.depth_shape = tuple(self.info['textures']['FogDepth'][v] for v in ('height', 'width'))
        self.depth = np.fromfile(self.folder / 'FogDepth.bin', '<f4').reshape(self.depth_shape)
        vraw = (self.folder / 'FogVolumes.bin').read_bytes()
        self.volumes = []
        for i in range(int(self.q[184])):
            v = np.frombuffer(vraw, '<f4', 32, i*128).astype(float)
            seed = np.frombuffer(vraw, '<u4', 1, i*128+112)[0]
            self.volumes.append((v, seed))

    def unproject(self, uv, depth):
        clip = np.c_[uv*2-1, np.broadcast_to(depth, len(uv)), np.ones(len(uv))]
        world = clip @ self.inv.T
        return world[:, :3] / world[:, 3:4]

    def ray_basis(self, uv):
        a, b = self.unproject(uv, 0), self.unproject(uv, 1)
        az = abs(np.c_[a, np.ones(len(a))] @ self.view[2])
        bz = abs(np.c_[b, np.ones(len(b))] @ self.view[2])
        step = (b-a)/(bz-az)[:, None]
        return a-step*az[:, None], step

    def medium(self, world):
        p = self.p
        sigma = p[160]*np.exp(-p[161]*np.maximum(world[:, 1]-p[162], 0)) if self.q[185] else np.zeros(len(world))
        scatter = sigma[:, None]*p[164:167]
        for v, seed in self.volumes:
            local = world @ v[:16].reshape(4, 4)[:3, :3].T + v[:16].reshape(4, 4)[:3, 3]
            radius = np.max(abs(local), axis=1) if v[20] == 0 else np.linalg.norm(local, axis=1)
            edge = smooth((1-radius)/v[21]) if v[21] else (radius < 1).astype(float)
            active = edge > 0
            density = np.zeros(len(world))
            n = noise((world[active]-v[24:27]*p[154])*v[22], seed)**v[27]
            density[active] = v[19]*edge[active]*(1-v[23]+n*v[23])
            sigma += density
            scatter += density[:, None]*v[16:19]
        return scatter, sigma

    def cascade(self, world, index):
        clip = np.c_[world, np.ones(len(world))] @ self.matrices[index].T
        ndc = clip[:, :3]/clip[:, 3:4]
        uv = ndc[:, :2]*.5+.5
        valid = (clip[:, 3] > 0) & (uv >= 0).all(1) & (uv <= 1).all(1) & (ndc[:, 2] >= 0) & (ndc[:, 2] <= 1)
        rect = self.rects[index]
        h, w = self.shadow.shape
        texel = (rect[:2]+uv*rect[2:])*[w, h]-.5
        base = np.floor(texel).astype(int)
        f = texel-base
        lower = (rect[:2]*[w, h]).astype(int)
        upper = ((rect[:2]+rect[2:])*[w, h]).astype(int)-1
        value = np.zeros(len(world))
        for y in range(2):
            for x in range(2):
                pixel = np.clip(base+[x, y], lower, upper)
                lit = ndc[:, 2]-self.p[175] <= self.shadow[pixel[:, 1], pixel[:, 0]]
                value += lit*(f[:, 0] if x else 1-f[:, 0])*(f[:, 1] if y else 1-f[:, 1])
        return np.where(valid, value, 1)

    def visibility(self, world, depth):
        count = int(self.q[186])
        if not count:
            return np.ones(len(world))
        selection = np.linalg.norm(world-self.p[188:191], axis=1) if self.p[191] > .5 else np.broadcast_to(depth, len(world))
        ends = self.p[144:144+count]
        ids = np.searchsorted(ends, selection, side='left')
        value = np.ones(len(world))
        for i in range(count):
            active = ids == i
            if not active.any():
                continue
            begin = 0 if i == 0 else ends[i-1]
            blend = smooth((selection[active]-(begin*.1+ends[i]*.9))/((ends[i]-begin)*.1))
            current = self.cascade(world[active], i)
            nxt = np.ones(active.sum())
            mixed = blend > 0
            if i+1 < count and mixed.any():
                nxt[mixed] = self.cascade(world[active][mixed], i+1)
            value[active] = current*(1-blend)+nxt*blend
        return value

    def validate_recorded(self):
        gx, gy, gz = self.grid
        ids = np.arange(0, gx*gy*gz, 53)
        x, y, z = ids % gx, ids//gx % gy, ids//(gx*gy)
        uv = np.c_[x+self.p[182], y+self.p[183]]/[gx, gy]
        origin, step = self.ray_basis(uv)
        depth = self.p[152]*(self.p[153]/self.p[152])**((z+self.p[159])/gz)
        world = origin+step*depth[:, None]
        scatter, sigma = self.medium(world)
        media = np.fromfile(self.folder/'FogMedia.bin', '<f4').reshape(-1, 8)[ids]
        medium_error = abs(np.c_[scatter, sigma]-media[:, :4]).max()
        assert medium_error < 2e-6, medium_error
        visible = np.zeros(len(ids))
        for dz in range(2):
            for dy in range(2):
                for dx in range(2):
                    uv = np.c_[x+(dx+self.p[182])*.5, y+(dy+self.p[183])*.5]/[gx, gy]
                    origin, step = self.ray_basis(uv)
                    low = self.p[152]*(self.p[153]/self.p[152])**(z/gz)
                    high = self.p[152]*(self.p[153]/self.p[152])**((z+1)/gz)
                    d = low+(high-low)*(dz+self.p[159])*.5
                    visible += self.visibility(origin+step*d[:, None], d)/8
        expected = scatter*self.p[172:175]*self.p[171]*visible[:, None]
        actual = np.fromfile(self.folder/'FogScattering.bin', '<f4').reshape(-1, 8)[ids, 4:7]
        source_error = abs(expected-actual).max()
        unoccluded = media[:, :3]*self.p[172:175]*self.p[171]
        valid = unoccluded > 1e-7
        visibility_error = abs(expected-actual)[valid]/unoccluded[valid]
        metrics = {'recordedMediumMaxError': float(medium_error), 'recordedSunSourceMaxError': float(source_error),
                   'visibilityMaxError': float(visibility_error.max()),
                   'visibilityP99Error': float(np.percentile(visibility_error, 99)), 'checkedCells': len(ids)}
        print('sample precision', metrics, flush=True)
        # CPU double and GPU float positions differ near PCF edges. Bound the
        # dimensionless visibility error, independent of authored light/density.
        assert visibility_error.max() < .001 and np.percentile(visibility_error, 99) < .00025, metrics
        return metrics

    def integrate(self, stride, dz):
        h, w = self.depth_shape
        yy, xx = np.mgrid[stride//2:h:stride, stride//2:w:stride]
        uv = np.c_[(xx.ravel()+.5)/w, (yy.ravel()+.5)/h]
        origin, step = self.ray_basis(uv)
        scale = np.linalg.norm(step, axis=1)
        world = self.unproject(uv, self.depth[yy, xx].ravel())
        stop = np.clip(abs(np.c_[world, np.ones(len(world))] @ self.view[2]), self.p[152], self.p[153])
        ray = step/scale[:, None]
        g = self.p[167]
        cosine = ray @ -self.p[168:171]
        phase = (1-g*g)/(4*np.pi*(1+g*g-2*g*cosine)**1.5)
        light = self.p[172:175]*self.p[171]
        result = np.zeros((len(uv), 3))
        trans = np.ones(len(uv))
        for i, low in enumerate(np.arange(self.p[152], self.p[153], dz)):
            length = np.maximum(np.minimum(low+dz, stop)-low, 0)*scale
            active = length > 0
            if not active.any():
                break
            depth = low+np.minimum(dz, np.maximum(stop[active]-low, 0))*.5
            world = origin[active]+step[active]*depth[:, None]
            scatter, sigma = self.medium(world)
            v = self.visibility(world, depth)
            tau = sigma*length[active]
            segment = np.divide(-np.expm1(-tau), sigma, out=length[active].copy(), where=sigma > 0)
            result[active] += scatter*light*(phase[active]*v*trans[active]*segment)[:, None]
            trans[active] *= np.exp(-tau)
            if i % 100 == 0:
                print(f'depth {low:.1f}/{self.p[153]:.1f}', flush=True)
        return result.reshape(*xx.shape, 3), trans.reshape(xx.shape), uv, stop, xx.shape


def run(args):
    started = time.monotonic()
    scene = RecordedScene(args.folder)
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    report = scene.validate_recorded()
    print('recorded validation', report, flush=True)
    reference, trans, uv, stop, shape = scene.integrate(args.stride, args.step)
    gx, gy, gz = scene.grid
    # GPU direct-only result from the independently verified source decomposition.
    direct_full = np.load(scene.folder/'sun-visible.npy')
    h, w = scene.depth_shape
    yy, xx = np.mgrid[args.stride//2:h:args.stride, args.stride//2:w:args.stride]
    gpu = direct_full[yy, xx]
    integrated = np.fromfile(scene.folder/'FogIntegrated.bin', '<f4').reshape(gz, gy, gx, 4)
    gpu_trans = sample_volume(integrated, uv, stop, scene.p[152], scene.p[153])[:, 3].reshape(shape)
    report.update({'stepViewMeters': args.step, 'pixelStride': args.stride,
                   'shape': list(shape), 'seconds': time.monotonic()-started,
                   'reference': 'continuous authored medium and captured shadow atlas',
                   'sunMeanReference': reference.mean((0, 1)).tolist(),
                   'sunMeanGpu': gpu.mean((0, 1)).tolist(),
                   'sunRmse': float(np.sqrt(np.mean((reference-gpu)**2))),
                   'transmittanceRmse': float(np.sqrt(np.mean((trans-gpu_trans)**2)))})
    np.save(output/'reference-sun.npy', reference)
    np.save(output/'reference-trans.npy', trans)
    def picture(rgb):
        return Image.fromarray(np.uint8(np.clip((1-np.exp(-rgb))*255, 0, 255)))
    comparison = Image.new('RGB', (shape[1]*2, shape[0]+24))
    comparison.paste(picture(reference), (0, 24))
    comparison.paste(picture(gpu), (shape[1], 24))
    ImageDraw.Draw(comparison).text((5, 5), 'Continuous field raymarch | Captured GPU sun (same display mapping)', fill='white')
    comparison.save(output/'comparison.png')
    (output/'report.json').write_text(json.dumps(report, indent=2))
    print(json.dumps(report), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('folder')
    parser.add_argument('--output', required=True)
    parser.add_argument('--stride', type=int, default=4)
    parser.add_argument('--step', type=float, default=.5)
    args = parser.parse_args()
    assert args.stride >= 1 and args.step > 0
    run(args)
