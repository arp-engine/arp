"""Exact rational reference for the authored constant-mip scene diagnostic.

Independent of shader generation and the C++ observer. This proves the finite
material case, not general sampling/lighting or public texture-copy capability.
"""
from fractions import Fraction as Q


def texel(image, layer=0):
    return tuple(Q(value, 255) for value in
                 (16 + 7 * image + 3 * layer, 64 + 3 * image + 5 * layer,
                  128 + image + 7 * layer, 255))


def expected_color(terrain, world_y):
    if not terrain:
        base = texel(5)
        return tuple(float(value * factor) for value, factor in
                     zip(base, (Q(1, 4), Q(1, 2), Q(3, 4), Q(1))))
    a, b = texel(0, 1), texel(1, 1)
    weights = list(a) + list(b[:2])
    total = sum(weights)
    weights = [w / total for w in weights]
    snow = b[2] * min(max(Q(world_y) - Q(1, 4), 0), 1) * Q(7, 10)
    weights[5] += snow
    weights[0] *= 1 - snow
    weights[4] *= 1 - snow * Q(4, 5)
    weights[2] += b[3] * Q(3, 10)
    weights[0] *= 1 - b[3] * Q(3, 20)
    total = sum(weights)
    weights = [w / total for w in weights]
    macro = texel(24)[0]
    gain = (Q(23, 25) + Q(4, 25) * macro) * (1 - Q(21, 100) * b[3])
    tint = (macro - Q(1, 2)) * Q(1, 2) * weights[0]
    color = [sum(texel(layer * 4)[c] * weights[layer] for layer in range(6)
                 if weights[layer] > Q(3, 1000)) * gain for c in range(3)]
    return tuple(float(value * (1 + shift * tint)) for value, shift in
                 zip(color, (Q(7, 100), Q(3, 100), Q(-14, 100)))) + (1.,)
