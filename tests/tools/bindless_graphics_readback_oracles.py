"""Independent authored input oracles and explicit direct-shader parity checks."""
import struct
import math


def verify_numeric_readback(oracle, data):
    values = list(struct.unpack('<' + str(len(oracle.expected)) + 'f', data))
    words = struct.unpack('<' + str(len(oracle.expected)) + 'I', data)
    metadata = getattr(oracle, 'metadata_indices', ())
    tolerance = getattr(oracle, 'tolerance', 0)
    exact = getattr(oracle, 'exact_indices', ())
    for lane, (value, expected) in enumerate(zip(values, oracle.expected)):
        if lane in metadata:
            expected_word = struct.unpack('<I', struct.pack('<f', expected))[0]
            if words[lane] != expected_word:
                raise RuntimeError('Metadata bits differ at lane ' + str(lane))
            values[lane] = None  # Raw metadata is reported through outputBits.
        elif not math.isfinite(value) or (value != expected if lane in exact
                else abs(value - expected) > tolerance * (1 + abs(expected))):
            raise RuntimeError('Numeric output differs at lane ' + str(lane))
    return values, list(words)


class OrdinaryColorGiOracle:
    package_name = 'GraphicsColorGi'
    scope = 'public Graphics ordinary Cube/2D/2DArray independent sampler Vertex/Fragment numerical76'
    tolerance = 2e-5

    def __init__(self):
        self.ordinary_buffers = {16: struct.pack('<4f', 17, 0, 0, 0)}
        lanes = [17 + value / 255 for face in range(6)
                 for value in (40 + 8 * face, 32 + 8 * face)]
        lanes += [17 + value / 255 for value in (32, 64, 48, 96, 72, 120, 40)]
        self.expected = lanes * 4
        self.color_pixels = [255, 0, 0, 255, 0, 255, 0, 255] * 2
        self.texture_layers = []
        for index, layers in ((0, 6), (1, 1), (2, 2)):
            self.texture_layers.append(tuple((
                b''.join(bytes(((index + 1) * (16 * (texel + 1) + 8 * layer), 0, 0, 255))
                         for texel in range(4)),
                bytes(((index + 1) * (32 + 8 * layer), 0, 0, 255)))
                for layer in range(layers)))


class OrdinaryWoccIblOracle:
    package_name = 'GraphicsWoccIbl'
    scope = 'public Graphics actual-size WOCC horizon/zenith Cube64 mip7 and BRDF LUT128 numerical424'

    def __init__(self, directory):
        # Cold core-baked input files are inputs, never expected GPU output.
        from pathlib import Path
        import hashlib
        directory = Path(directory)
        self.ordinary_buffers = {16: struct.pack('<4f', 17, 0, 0, 0)}
        self.color_pixels = [255, 0, 0, 255, 0, 255, 0, 255] * 2
        self.input_hashes = {}
        self.texture_chains = []
        lanes = []
        cubes = []
        for name in ('horizon.rgba16f', 'zenith.rgba16f'):
            raw = (directory / name).read_bytes()
            if len(raw) != sum((64 >> mip)**2 * 6 * 8 for mip in range(7)):
                raise ValueError('WOCC IBL exact Cube64 mip7 input size')
            self.input_hashes[name] = hashlib.sha256(raw).hexdigest()
            offset, layers = 0, [[] for _ in range(6)]
            centers = []
            for mip in range(7):
                size = 64 >> mip
                face_bytes = size * size * 8
                center = []
                for face in range(6):
                    data = raw[offset + face * face_bytes:offset + (face+1) * face_bytes]
                    layers[face].append(data)
                    low, high = (size-1)//2, size//2
                    center.append(sum(struct.unpack_from('<e', data, (y*size+x)*8)[0]
                                      for y in (low, high) for x in (low, high)) / 4)
                centers.append(center)
                offset += face_bytes * 6
            cubes.append(centers)
            for face in range(6):
                lanes.extend(17 + centers[mip][face] for mip in range(7))
                lanes.append(17 + (centers[2][face]+centers[3][face])/2)
            self.texture_chains.append((64, 64, 7, 6, tuple(tuple(chain) for chain in layers)))
        lut = (directory / 'brdf.rgba16f').read_bytes()
        if len(lut) != 128*128*8:
            raise ValueError('WOCC BRDF exact LUT128 input size')
        self.input_hashes['brdf.rgba16f'] = hashlib.sha256(lut).hexdigest()
        self.texture_chains.append((128, 128, 1, 1, ((lut,),)))
        for x, y in ((7, 15), (64, 64), (120, 96), (127, 0)):
            lanes.extend(17 + value for value in struct.unpack_from('<2e', lut, (y*128+x)*8))
        lanes.extend((17 + (centers[2][0]+centers[3][0])/2 for centers in cubes))
        if len(lanes) != 106:
            raise ValueError('WOCC IBL numerical footprint')
        self.expected = lanes * 4

    def verify_direct_reference(self, data):
        """Validate independent sampling without inventing a filtering tolerance.

        Only single-texel Cube mip6 and LUT texel-center probes are compared
        against CPU arithmetic. Other lanes retain analytical deltas as evidence;
        they are accepted only by exact parity with a separately authenticated
        shader, with its inputs independently verified by the replay caller.
        """
        if len(data) != len(self.expected) * 4:
            raise RuntimeError('Wrong direct IBL reference byte count')
        values = list(struct.unpack('<424f', data))
        exact = 0
        for index, value in enumerate(values):
            if not math.isfinite(value):
                raise RuntimeError('Nonfinite direct IBL reference at lane ' + str(index))
            lane = index % 106
            if (lane < 96 and lane % 8 == 6) or 96 <= lane < 104:
                if struct.pack('<f', value) != struct.pack('<f', self.expected[index]):
                    raise RuntimeError('Unfiltered CPU IBL reference differs at lane ' + str(index))
                exact += 1
        return dict(cpuExactLanes=exact,
                    maxCpuAnalyticalDelta=max(abs(v-e) for v, e in zip(values, self.expected)),
                    cpuFilteredAnalyticalParityClaimed=False)

    def verify_direct_parity(self, data, reference):
        self.verify_direct_reference(reference)
        if len(data) != len(reference) or data != reference:
            raise RuntimeError('IBL output differs from independent direct shader reference')
        return list(struct.unpack('<424f', data)), list(struct.unpack('<424I', data))


class OrdinaryHdrGiOracle:
    package_name = 'GraphicsHdrGi'
    scope = 'public Graphics ordinary RGBA16F Cube/2D/Array fractional LOD numerical112'
    tolerance = 2e-5

    def __init__(self):
        self.ordinary_buffers = {16: struct.pack('<4f', 17, 0, 0, 0)}
        lanes = [17 + value for face in range(6)
                 for value in (2.5 + 2 * face, 8 + 2 * face, 5.25 + 2 * face)]
        lanes += [19, 33, 26]
        lanes += [17 + value for layer in range(2)
                  for value in (3 * (1 + 2 * layer), 3 * (8 + 2 * layer),
                                3 * (2.75 + 2 * layer))]
        self.expected = (lanes + [22.25]) * 4
        self.color_pixels = [255, 0, 0, 255, 0, 255, 0, 255] * 2
        self.texture_layers = []
        def rgba(value):
            return struct.pack('<4e', value, value * .5, -value, 1)
        for index, layers in ((0, 6), (1, 1), (2, 2)):
            self.texture_layers.append(tuple((
                b''.join(rgba((index + 1) * (texel + 1 + 2 * layer))
                         for texel in range(4)),
                rgba((index + 1) * (8 + 2 * layer))) for layer in range(layers)))


class Instance64Oracle:
    package_name = 'GraphicsInstance64'
    scope = 'public Graphics Instance64 runtime256 probe224 numerical176 metadata-bits wind'
    tolerance = 2e-5
    metadata_indices = tuple(pixel * 44 + lane for pixel in range(4) for lane in range(16, 20))

    def __init__(self):
        import importlib.util
        from pathlib import Path
        reference = Path(__file__).resolve().parents[1] / 'contract' / 'foliage_geometry_reference.py'
        spec = importlib.util.spec_from_file_location('foliage_geometry_reference', reference)
        wind = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(wind)
        positions = [[111, 222, 333, 7], [2, -3, 5, 2], [-8, 4, 12, .5], [16, -2, -6, 1.5]]
        attributes = [[1, 0, 900, 0], [.37, .25, 101, 0], [-.63, .5, 202, 1], [1.12, .75, 303, 2]]
        extra = [[0, 0, 0, 0], [.0005, -.0005, 0, -.75], [.41, -.27, 1.75, -1.25], [.0005, .33, -1, .5]]
        metadata = [[(13+i) | ((31+i) << 8) | ((201-i) << 16) | (3 << 24),
                     i | (2 << 8) | ((0x7fc1+i) << 16), 0x80000000, 1+i] for i in range(4)]
        self.ordinary_buffers = {
            256: b''.join(struct.pack('<12f4I', *positions[i], *attributes[i], *extra[i], *metadata[i]) for i in range(4)),
            224: (struct.pack('<12f8I', 2, -3, 5, 1, 2, 3, -4, 0, -3, 2, 1, -1, 1, 2, 0, 0, 0, 1, 2, 1)
                  + struct.pack('<36f', *wind.WIND_WEIGHTS_HEIGHT, *wind.CURRENT_WIND, *wind.PREVIOUS_WIND)),
        }
        # Independent float64 NumPy Rz @ Rx @ Ry reference, evaluated from
        # float32 authored inputs and rounded once to float32. No engine output.
        vertex = [25.08130645751953, -3.1463842391967773, -5.434933185577393, 1]
        geometry = {
            1: [9.345463752746582, -9.75, 12.876811981201172, 1,
                .07765647768974304, .5570859909057617, -.8268159627914429, 0,
                -2.435366630554199, 2, 2.0171737670898438, -1],
            2: [-10.202445030212402, -2.715489625930786, 12.300265312194824, 1,
                .8878000378608704, .4421533942222595, -.12771634757518768, 0,
                -2.31280517578125, 2.94008469581604, -.08267325907945633, -1],
            3: [*vertex, -.6600517630577087, .36312299966812134, -.6576271057128906, 0,
                -1.0325669050216675, 1.758732795715332, 3.1369833946228027, -1],
        }
        self.expected = []
        for i in (1, 2, 3, 2):
            raw = struct.unpack('<4f', struct.pack('<4I', *metadata[i]))
            self.expected.extend(vertex + geometry[i] + list(raw) + attributes[i])
            self.expected.extend(wind.deform(vertex, positions[3], attributes[3][1],
                                             attributes[3][3], wind.CURRENT_WIND))
            for field, height in ((wind.CURRENT_WIND, .5), (wind.PREVIOUS_WIND, .5),
                                  (wind.CURRENT_WIND, 2)):
                self.expected.extend(wind.deform(geometry[i], positions[i], attributes[i][1],
                                                 attributes[i][3], field, height))
            self.expected.extend(geometry[i][:4])  # Exact rigid rest pose.


class PbrMaterialOracle:
    package_name = 'GraphicsPbrMaterial'
    scope = 'public Graphics PBR five-role sampling material288 selection16 numerical128'
    tolerance = 2e-5

    def __init__(self):
        records = [
            ([9, 8, 7, 6], [9, 8, 7, 6], [0]*4, [0]*4, [9]*4, [9, 1, 0, 0]),
            ([.5, .75, 1, .8], [2, .01, .4, 1], [1.5, 2, 1, .25],
             [.7, .3, .2, .1], [0, 1, 2, 3], [4, 0, 0, 0]),
            ([1, .5, .25, .5], [.25, 2, .4, 2.125], [-.25, .5, -.75, .9],
             [-.5, .6, .7, .8], [5, 6, 7, 8], [9, 1, 0, 0]),
        ]
        self.ordinary_buffers = {
            288: b''.join(struct.pack('<16f8I', *sum(r[:4], []), *sum(r[4:], [])) for r in records),
            16: struct.pack('<4I', 1, 2, 1, 0),
        }
        colors = [(64, 128, 192, 128), (128, 192, 255, 255), (16, 128, 192, 255),
                  (96, 64, 32, 255), (32, 64, 128, 255), (192, 64, 128, 192),
                  (192, 64, 255, 255), (128, 192, 32, 255), (32, 96, 160, 255),
                  (224, 64, 32, 255)]
        self.texture_mips = [(bytes((255-c+17*p) % 256 for p in range(4) for c in color), bytes(color)) for color in colors]
        self.color_pixels = [64, 128, 191, 255, 0, 0, 0, 255] * 2
        self.expected = []
        for pixel in range(4):
            selected = 1 + pixel % 2
            factor, params, extra, environment, textures, ao_sampler = records[selected]
            base, normal, mr, emissive = [[c/255 for c in colors[i]] for i in textures]
            ao = colors[ao_sampler[0]][0]/255
            rgba = [a*b for a, b in zip(factor, base)]
            metallic = min(1, max(0, mr[2]*params[0]))
            roughness = min(1, max(.045, mr[1]*params[1]))
            occlusion = 1 + (ao-1)*min(1, max(0, extra[0]))
            mode, cutoff = params[3], params[2]
            leaf = 2 <= mode <= 3
            covered = not ((.5 < mode < 1.5 or leaf) and rgba[3] < cutoff)
            # HSV saturation is independently expressed as interpolation from V.
            high, low = max(rgba[:3]), min(rgba[:3])
            saturation = min(-environment[0], high/(high-low)) if leaf and high > low else 1
            translucent = [high + (c-high)*saturation for c in rgba[:3]]
            front = pixel < 2
            flip = not front and extra[2] < .5
            mapped = [2*normal[0]-1, 1-2*normal[1], (-1 if flip else 1)*(2*normal[2]-1)]
            length = math.sqrt(sum(c*c for c in mapped))
            # Independent bilinear interpolation in texel coordinates. The
            # nearest sample addresses the bottom-right corner at every pixel.
            u = .5 + (pixel % 2 + .5) * .125
            v = .5 + (pixel // 2 + .5) * .125
            weights = [(1-(u*2-.5))*(1-(v*2-.5)), (u*2-.5)*(1-(v*2-.5)),
                       (1-(u*2-.5))*(v*2-.5), (u*2-.5)*(v*2-.5)]
            probe = []
            for channel in (0, 1):
                corners = [self.texture_mips[0][0][p*4+channel]/255 for p in range(4)]
                probe.extend([corners[3], sum(a*b for a, b in zip(corners, weights))])
            self.expected.extend(records[1][0] + rgba + [metallic, roughness, occlusion, int(covered)]
                                 + [c*extra[1] for c in emissive[:3]] + [mode-2 if leaf else .04]
                                 + translucent + [max(-extra[2], 0) if leaf else 0]
                                 + [c/length for c in mapped] + [int(front)] + probe
                                 + [selected, ao_sampler[1], 1, 1])


class Terrain27Oracle:
    package_name = 'GraphicsTerrain27'
    scope = 'public Graphics authored Terrain27 two arrays runtime96 selection16 numerical128'
    tolerance = 2e-5

    def __init__(self):
        records = [([99, 98, 97, 96], [99, 98, 97, 96]),
                   ([3, 1, 4, 0], [0, 0, 0, 0]),
                   ([1.0005, 1, 0, 0], [4, 0, 0, 0])]
        self.ordinary_buffers = {
            96: b''.join(struct.pack('<4f4I', *a, *b) for a, b in records),
            16: struct.pack('<4I', 1, 2, 0, 0)}
        surfaces = [tuple((17 + image*23 + c*37) % 251 for c in range(4))
                    for image in range(25)]
        pages = [[(30, 40, 50, 60), (70, 20, 40, 50)],
                 [(80, 20, 160, 90), (10, 90, 60, 180)]]
        self.texture_layers = [((bytes(255-c for c in color)*4, bytes(color)),)
                               for color in surfaces]
        self.texture_layers += [tuple((bytes(255-c for c in color)*4, bytes(color))
                                      for color in page) for page in pages]
        self.expected = []
        for pixel in range(4):
            selected, layer = 1 + pixel % 2, pixel // 2
            # Inputs must use the actual authored float32, not Python's decimal.
            params = struct.unpack('<4f', struct.pack('<4f', *records[selected][0]))
            a, b = ([c/255 for c in page[layer]] for page in pages)
            weights = a + b[:2]
            weights = [v/max(sum(weights), .001) for v in weights]
            snow = b[2]*min(1, max(0, (params[0]-params[1])/max(params[2], .001)))
            wet = b[3]*.3
            weights[5] += snow*.7
            weights[0] *= (1-snow*.7)*(1-wet*.5)
            weights[4] *= 1-snow*.56
            weights[2] += wet
            weights = [v/max(sum(weights), .001) for v in weights]
            bases = [(4*i + records[selected][1][0]) % 24 for i in range(6)]
            albedo = [sum(surfaces[base][c]/255*w for base, w in zip(bases, weights))
                      for c in range(3)]
            normal = [sum((surfaces[base+1][c]/127.5-1)*w for base, w in zip(bases, weights))
                      for c in range(3)]
            roughness, height = [sum(surfaces[base+role][0]/255*w
                                    for base, w in zip(bases, weights)) for role in (2, 3)]
            self.expected.extend(a + b + weights + [snow, wet] + albedo
                                 + [surfaces[24][0]/255] + normal + [roughness, height]
                                 + records[1][0][:3] + [selected, layer, 1, 1])


class MatrixOrdinaryOracle:
    package_name = "GraphicsMatrixOrdinary"
    scope = "public Graphics MatrixOrdinary 320+16 Vertex/Fragment numerical32"

    def __init__(self):
        matrices = [
            [2, 3, 0, 7, 0, 5, 1, 11, 1, 0, 4, 13, 2, 1, 3, 1],
            [1, -2, 3, 17, 2, 3, 0, 19, 0, 4, 5, 23, 1, 3, 2, 1],
            [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
            [2, 1, 4, 3, 6, 5, 8, 7, 10, 9, 12, 11, 14, 13, 16, 15],
            [3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59]]
        revision = [2, 4, 8, 16]

        def transform(m, v):
            return [sum(m[r * 4 + c] * v[c] for c in range(4)) for r in range(4)]

        point = [2, 3, 5, 1]
        skin = [a * .25 + b * .75 for a, b in zip(transform(matrices[2], point), transform(matrices[4], point))]
        vertex = [a + b + c for a, b, c in zip(transform(matrices[0], skin), transform(matrices[1], point), revision)]
        expected = []
        for pixel in range(4):
            p = [pixel + 1, 3, 5, 1]
            fragment = [a + b + c for a, b, c in zip(transform(matrices[0], transform(matrices[1], p)), transform(matrices[3], p), revision)]
            expected.extend(vertex + fragment)
        self.expected = expected
        self.ordinary_buffers = {
            320: struct.pack('<80f', *sum(matrices, [])),
            16: struct.pack('<4f', *revision),
        }


class Skinning256Oracle:
    package_name = "GraphicsSkinning256"
    scope = "public Graphics Skinning256 temporal32864 influence32 numerical64 normal tangent"
    tolerance = 2e-5

    def __init__(self):
        palettes = []
        for pose in range(2):
            values = [float(joint * 2 + lane + pose * 3 + 1)
                      for joint in range(256) for lane in range(16)]
            transform = [2, 4, 8, 2, 0, 0, 1, 0] if pose == 0 else [16, 32, 64, .5, 1, 0, 0, 0]
            palettes.append(struct.pack('<4104f4I', *values, *transform,
                                        256 if pose == 0 else 220, 0, 0, 0))
        self.ordinary_buffers = {
            32864: b''.join(palettes),
            32: struct.pack('<8f', 0, 128, 219, 255, .0625, .125, .0625, .25),
        }
        # Closed-form row sums, normalized by 0.5, then exact 180-degree
        # rotations and translations; independent of the C++ oracle/engine.
        vertex = [-8274.5, -8360.5, 8460.5, 2135.125]
        previous = [[1732.75, -1704.75, -1692.75, 1776.75],
                    [1903.625, -1877.625, -1867.625, 1953.625],
                    [2074.5, -2050.5, -2042.5, 2130.5],
                    [2245.375, -2223.375, -2217.375, 2307.375]]
        # Closed row sums are independent of the engine's matrix helpers.
        normal = [-1880.25, -1900.25, 1920.25]
        tangent = [-2., -2., 2.]
        normal = [v / math.sqrt(sum(x*x for x in normal)) for v in normal]
        tangent = [v / math.sqrt(sum(x*x for x in tangent)) for v in tangent]
        self.expected = [value for fragment in previous
                         for value in vertex + fragment + normal + [0.] + tangent + [-1.]]
        self.exact_indices = [i for i in range(64) if i % 16 < 8 or i % 16 in (11, 15)]
