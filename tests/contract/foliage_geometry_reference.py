"""Independent float64 reference for the frozen production wind inputs.

The reference separates displacement components and scales their sum by height
and flexibility. Its inputs are authored CPU values, never shader/readback data.
"""
import math

CURRENT_WIND = (.6, 0, .8, 4.5, 1.25, .45, .7, .3, 7, .6, 64, .9, 0, 0, 0, 0)
PREVIOUS_WIND = (-.8, 0, .6, 2, .75, .1, .5, .2, 3, .1, 32, .6, 3, 0, 7, 0)
WIND_WEIGHTS_HEIGHT = (.7, .4, .2, .3)


def deform(world, instance_origin, phase, lod, wind, height=.5, stiffness=.2):
    if lod >= 3 or stiffness == 1:
        return list(world[:3]) + [1]
    direction = wind[:3]
    if math.hypot(direction[0], direction[2]) > .001:
        norm = math.sqrt(sum(x*x for x in direction))
        direction = tuple(x/norm for x in direction)
    else:
        direction = (.8, 0, .6)
    cross = (-direction[2], 0, direction[0])
    speed, time = max(wind[3], 3), wind[4] + phase
    along = lambda p: p[0]*direction[0] + p[2]*direction[2]
    spatial = along(instance_origin)*.7
    primary, flutter = WIND_WEIGHTS_HEIGHT[:2]
    amplitude = primary*(.05 + speed*.04)
    longitudinal = math.sin(phase*6.2831853 + spatial + wind[4]*1.65)*amplitude
    lateral = math.cos(phase*6.2831853 + spatial*1.37 + wind[4]*1.18)*amplitude*.28
    vertical = 0
    if lod <= 1:
        gust = math.sin(along(world)*wind[6] + time*wind[6]*2)*max(wind[5], .25)*speed*.075
        longitudinal += gust
        lateral += gust*(.45 + .25*primary)
    if lod <= 0:
        lateral += (math.sin(world[0]*1.3 + time*1.7)
                    * math.cos(world[2]*1.1 + time*1.3)*wind[7]*speed*.08)
        wave = math.sin(time*max(wind[8], 5) + phase*6.28318)
        lateral += wave*flutter*max(wind[9], .35)*.12
        vertical = abs(wave)*flutter*.018
    displacement = [direction[i]*longitudinal + cross[i]*lateral for i in range(3)]
    displacement[1] += vertical
    away = (world[0]-wind[12], 0, world[2]-wind[14])
    distance = math.hypot(away[0], away[2])
    if wind[10] > .001 and distance < wind[10]:
        push = (1-distance/wind[10])**2*wind[11]*.3/max(distance, .001)
        displacement = [d+a*push for d, a in zip(displacement, away)]
    weight = min(1, abs(WIND_WEIGHTS_HEIGHT[3])/height)*(1-stiffness)
    return [float(w)+weight*d for w, d in zip(world[:3], displacement)] + [1]
