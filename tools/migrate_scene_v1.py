"""Offline, strict scene/prefab schema 1 -> 2 conversion. Never used by runtime."""
import argparse
import copy
import math
from pathlib import Path
import re
import sys
import yaml


class MigrationError(ValueError):
    pass


def exact(value, keys, path):
    if not isinstance(value, dict) or set(value) != set(keys):
        raise MigrationError(f"{path}: expected exactly {sorted(keys)}")


def typed(name, fields):
    return {"type": name, "fields": fields}


def vec(value, dimensions=2):
    exact(value, 'xyzw'[:dimensions], 'vector')
    for number in value.values():
        if isinstance(number, bool) or not isinstance(number, (int, float)) or not math.isfinite(number):
            raise MigrationError('vector: expected finite numbers')
    return typed(f"Vec{dimensions}", dict(value))


def enum(value, names, path):
    if value not in names:
        raise MigrationError(f"{path}: unknown enum {value!r}")
    return names.index(value)


def wide(value, path):
    if type(value) is not int or not 0 <= value <= 2**64 - 1:
        raise MigrationError(f"{path}: invalid uint64")
    return str(value)


def scalar(value, kind, path, minimum=None, maximum=None):
    valid = type(value) is kind if kind in (int, bool, str) else type(value) in (int, float)
    if not valid or (kind is float and not math.isfinite(value)):
        raise MigrationError(f'{path}: expected {kind.__name__}')
    if minimum is not None and value < minimum or maximum is not None and value > maximum:
        raise MigrationError(f'{path}: out of range')
    return value


def uuid(value, path, optional=False):
    scalar(value, str, path)
    if optional and value == '': return value
    if not re.fullmatch(r'[0-9a-fA-F]{8}-(?:[0-9a-fA-F]{4}-){3}[0-9a-fA-F]{12}', value):
        raise MigrationError(f'{path}: expected UUID')
    return value


def sequence(value, path, maximum=4096):
    if not isinstance(value, list) or len(value) > maximum:
        raise MigrationError(f'{path}: expected bounded sequence')
    return value


def validate_component(kind, fields, path):
    # Exact legacy schema types; runtime still performs resource readiness and domain validation.
    for key in ('ySort', 'flipX', 'flipY', 'bullet', 'fixedRotation', 'sensor', 'loop',
                'enableLimit', 'enableMotor', 'collideConnected', 'paused', 'enabled'):
        if key in fields: scalar(fields[key], bool, path + '.' + key)
    for key in ('sortingLayer', 'sortingOrder', 'group'):
        if key in fields: scalar(fields[key], int, path + '.' + key, -(2**31), 2**31-1)
    for key in ('bodyA', 'bodyB', 'firstObjectId'):
        if key in fields: scalar(fields[key], int, path + '.' + key, 1, 2**63-1)
    for key in ('eventBudget', 'maxResidentChunks', 'maxVisibleCells', 'schemaVersion'):
        if key in fields: scalar(fields[key], int, path + '.' + key, 1, 2**32-1)
    for key in ('spriteId', 'animationId', 'tileMapId', 'scriptResourceId'):
        if key in fields: uuid(fields[key], path + '.' + key, optional=key == 'animationId')
    for key in ('r','g','b','a','angularVelocity','gravityScale','linearDamping','angularDamping',
                'radius','density','friction','restitution','length','referenceAngle','lowerLimit',
                'upperLimit','motorSpeed','maxMotorForce','speed'):
        if key in fields:
            scalar(fields[key], float, path + '.' + key)
            if key not in ('speed',) and abs(fields[key]) > 3.4028234663852886e38:
                raise MigrationError(f'{path}.{key}: float32 overflow')
    for key in ('linearDamping','angularDamping','radius','density','friction','restitution','maxMotorForce'):
        if key in fields and fields[key] < 0: raise MigrationError(f'{path}.{key}: negative domain value')
    if 'points' in fields: sequence(fields['points'], path + '.points')
    if 'scriptType' in fields:
        scalar(fields['scriptType'], str, path + '.scriptType')
        if not fields['scriptType'] or not isinstance(fields['fields'], dict):
            raise MigrationError(f'{path}: invalid script type or fields')


def validate_graph(objects):
    ids = {item['id']: item for item in objects}
    for item in objects:
        local = item['id']
        visited = set()
        while local:
            if local in visited or local not in ids:
                raise MigrationError('$.objects.parent: dangling parent or cycle')
            visited.add(local)
            local = ids[local]['parent']
        for component in item['components']:
            path = f"$.objects[id={item['id']}].components[{component['type']}]"
            fields = component['fields']
            if component['type'] == 'arp.joint2d/1':
                for key in ('bodyA','bodyB'):
                    target = ids.get(fields[key])
                    if not target or not any(c['type'] == 'arp.rigid-body2d/1' for c in target['components']):
                        raise MigrationError(f'{path}.{key}: missing rigid body target')
            if component['type'] == 'arp.object-links/1':
                names = set()
                for link in fields['links']:
                    value = link['fields']
                    if value['targetObjectId'] not in ids or value['name'] in names:
                        raise MigrationError(f'{path}.links: duplicate name or dangling target')
                    names.add(value['name'])


def component(old, path):
    if not isinstance(old, dict): raise MigrationError(f'{path}: expected component mapping')
    kind = old.get('type')
    fields = copy.deepcopy(old)
    fields.pop('type', None)
    validate_component(kind, fields, path)
    result = {"type": kind, "schemaVersion": 1, "enabled": True, "configuration": None}
    if kind == 'arp.sprite/1':
        exact(fields, ['spriteId', 'r', 'g', 'b', 'a', 'sortingLayer', 'sortingOrder', 'ySort', 'flipX', 'flipY'], path)
        result['fields'] = {
            'spriteResourceId': fields['spriteId'],
            'tint': vec(dict(zip('xyzw', [fields[k] for k in 'rgba'])), 4),
            'localBounds': typed('Bounds2D', {'center': vec(dict.fromkeys('xyz', 0.0), 3),
                'extents': vec(dict.fromkeys('xyz', 0.0), 3), 'hasExplicitMetadata': False}),
            **{k: fields[k] for k in ['sortingLayer', 'sortingOrder', 'ySort', 'flipX', 'flipY']}}
    elif kind == 'arp.rigid-body2d/1':
        exact(fields, ['bodyType', 'velocity', 'angularVelocity', 'gravityScale', 'linearDamping', 'angularDamping', 'bullet', 'fixedRotation'], path)
        fields['type'] = enum(fields.pop('bodyType'), ['static', 'kinematic', 'dynamic'], path)
        fields['velocity'] = vec(fields['velocity'])
        result['fields'] = fields
    elif kind == 'arp.collider2d/1':
        exact(fields, ['shapeType', 'center', 'halfSize', 'pointA', 'pointB', 'points', 'category', 'mask', 'group', 'radius', 'density', 'friction', 'restitution', 'sensor', 'loop'], path)
        fields['type'] = enum(fields.pop('shapeType'), ['circle', 'capsule', 'box', 'convex', 'static-chain'], path)
        filter_fields = {'group': fields.pop('group')}
        for key in ['category', 'mask']:
            text = fields.pop(key)
            if not isinstance(text, str) or not re.fullmatch(r'[0-9a-fA-F]{16}', text):
                raise MigrationError(f'{path}.{key}: expected exactly 16 hexadecimal digits')
            filter_fields[key] = str(int(text, 16))
        fields['filter'] = typed('CollisionFilter', filter_fields)
        for key in ['center', 'halfSize', 'pointA', 'pointB']: fields[key] = vec(fields[key])
        fields['points'] = [vec(point) for point in fields['points']]
        result['fields'] = fields
    elif kind == 'arp.joint2d/1':
        exact(fields, ['jointType', 'bodyA', 'bodyB', 'localAnchorA', 'localAnchorB', 'localAxisA', 'length', 'referenceAngle', 'lowerLimit', 'upperLimit', 'motorSpeed', 'maxMotorForce', 'enableLimit', 'enableMotor', 'collideConnected'], path)
        fields['type'] = enum(fields.pop('jointType'), ['distance', 'revolute', 'prismatic'], path)
        for key in ['localAnchorA', 'localAnchorB', 'localAxisA']: fields[key] = vec(fields[key])
        result['fields'] = fields
    elif kind == 'arp.sprite-animation/1':
        exact(fields, ['animationId', 'speed', 'paused', 'eventBudget'], path)
        fields['animationResourceId'] = fields.pop('animationId')
        result['fields'] = fields
    elif kind == 'arp.tile-map/1':
        exact(fields, ['tileMapId', 'maxResidentChunks', 'maxVisibleCells', 'firstObjectId'], path)
        fields['tileMapResourceId'] = fields.pop('tileMapId')
        fields['firstObjectId'] = wide(fields['firstObjectId'], path + '.firstObjectId')
        result['fields'] = fields
    elif kind == 'arp.object-links/1':
        exact(fields, ['links'], path)
        links = []
        for link in sequence(fields['links'], path + '.links', 64):
            exact(link, ['name', 'target'], path + '.links')
            scalar(link['name'], str, path + '.links.name')
            if not 1 <= len(link['name']) <= 128: raise MigrationError(f'{path}.links.name: length')
            scalar(link['target'], int, path + '.links.target', 1, 2**63-1)
            links.append(typed('ObjectLinksEditorEntry', {'name': link['name'], 'targetObjectId': link['target']}))
        result['fields'] = {'links': links}
    elif kind == 'arp.script-component/1':
        exact(fields, ['scriptResourceId', 'schemaVersion', 'scriptType', 'enabled', 'fields'], path)
        result['configuration'] = {'scriptResourceId': fields['scriptResourceId'],
            'moduleSchemaVersion': fields['schemaVersion'], 'scriptType': fields['scriptType']}
        result['enabled'] = fields['enabled']
        result['fields'] = fields['fields']
    else:
        raise MigrationError(f'{path}.type: unsupported component {kind!r}; explicit migration required')
    return result


def convert(old):
    if isinstance(old, dict) and old.get('type') == 'arp.editor-scene': return convert_editor(old)
    exact(old, ['type', 'contractVersion', 'resourceId', 'name', 'objects'], '$')
    if old['type'] not in ['arp.scene-asset', 'arp.scene-prefab'] or type(old['contractVersion']) is not int or old['contractVersion'] != 1:
        raise MigrationError('$.contractVersion: converter accepts scene/prefab version 1 only')
    uuid(old['resourceId'], '$.resourceId')
    scalar(old['name'], str, '$.name')
    sequence(old['objects'], '$.objects')
    result = copy.deepcopy(old)
    result['contractVersion'] = 2
    result['objects'] = []
    ids = set()
    for index, item in enumerate(old['objects']):
        path = f'$.objects[{index}]'
        exact(item, ['id', 'parent', 'name', 'active', 'transform', 'components'], path)
        if type(item['id']) is not int or item['id'] <= 0 or item['id'] in ids:
            raise MigrationError(f'{path}.id: invalid or duplicate local ID')
        scalar(item['parent'], int, path + '.parent', 0, 2**63-1)
        scalar(item['id'], int, path + '.id', 1, 2**63-1)
        scalar(item['name'], str, path + '.name')
        scalar(item['active'], bool, path + '.active')
        sequence(item['components'], path + '.components')
        ids.add(item['id'])
        transform = item['transform']
        exact(transform, ['x', 'y', 'z', 'qx', 'qy', 'qz', 'qw', 'sx', 'sy', 'sz'], path + '.transform')
        q = dict(zip('xyzw', [transform[k] for k in ['qx', 'qy', 'qz', 'qw']]))
        for key, number in q.items(): scalar(number, float, path + '.transform.q' + key)
        if abs(sum(n*n for n in q.values())-1) > 1e-5: raise MigrationError(f'{path}.transform: quaternion must be normalized')
        quaternion = vec(q, 4)
        quaternion['type'] = 'Quat'
        result['objects'].append({'id': item['id'], 'parent': item['parent'],
            'properties': {'name': item['name'], 'active': item['active']},
            'transform': {'translation': vec({k: transform[k] for k in 'xyz'}, 3),
                'scale': vec({k: transform['s' + k] for k in 'xyz'}, 3), 'rotation': quaternion},
            'components': [component(c, path + f'.components[{i}]') for i, c in enumerate(item['components'])]})
    validate_graph(result['objects'])
    return result


def editor_property(data, path):
    exact(data, ['kind', 'value'], path)
    kind, value = data['kind'], data['value']
    if kind in ('vector2', 'vector3'): value = vec(value, int(kind[-1]))
    elif kind == 'color4':
        exact(value, 'rgba', path)
        value = vec(dict(zip('xyzw', [value[k] for k in 'rgba'])), 4)
    elif kind == 'vector2List': value = [vec(point) for point in sequence(value, path)]
    elif kind == 'sceneObjectLinkList':
        links = []
        for link in sequence(value, path, 64):
            exact(link, ['name', 'target'], path)
            scalar(link['name'], str, path + '.name')
            scalar(link['target'], int, path + '.target', 1, 2**63-1)
            links.append(typed('ObjectLinksEditorEntry', {'name': link['name'], 'targetObjectId': link['target']}))
        value = links
    elif kind in ('string', 'resource'):
        if isinstance(value, dict) and set(value) == {'empty'} and value['empty'] is True: value = ''
        else:
            exact(value, ['text'], path)
            value = scalar(value['text'], str, path)
        if kind == 'resource' and value: uuid(value, path)
    elif kind == 'unsigned': value = wide(scalar(value, int, path, 0, 2**32-1), path)
    elif kind == 'sceneObjectReference': scalar(value, int, path, 0, 2**63-1)
    elif kind in ('integer', 'enum'): scalar(value, int, path, -(2**31), 2**31-1)
    elif kind == 'boolean': scalar(value, bool, path)
    elif kind == 'number': scalar(value, float, path)
    else: raise MigrationError(f'{path}.kind: unsupported declared kind {kind!r}')
    return {'kind': kind, 'value': value}


def convert_editor(old):
    exact(old, ['type', 'contractVersion', 'name', 'selected', 'objects'], '$')
    if type(old['contractVersion']) is not int or old['contractVersion'] != 1:
        raise MigrationError('$.contractVersion: expected editor version 1')
    scalar(old['name'], str, '$.name')
    scalar(old['selected'], int, '$.selected', 0, 2**63-1)
    result = copy.deepcopy(old)
    result['contractVersion'] = 2
    ids = set()
    for i, item in enumerate(sequence(result['objects'], '$.objects')):
        path = f'$.objects[{i}]'
        exact(item, ['id','parent','childIndex','name','active','transform','components'], path)
        for key in ('id','parent','childIndex'): scalar(item[key], int, path+'.'+key, 1 if key=='id' else 0, 2**63-1)
        if item['id'] in ids: raise MigrationError(path + '.id: duplicate')
        ids.add(item['id'])
        scalar(item['name'], str, path+'.name'); scalar(item['active'], bool, path+'.active')
        t = item['transform']
        exact(t, ['translation','scale','rotation'], path+'.transform')
        t['translation'] = vec(t['translation'], 3); t['scale'] = vec(t['scale'], 3)
        t['rotation'] = vec(t['rotation'], 4); t['rotation']['type'] = 'Quat'
        for c in sequence(item['components'], path+'.components'):
            exact(c, ['type','properties'], path+'.components')
            scalar(c['type'], str, path+'.components.type')
            if not isinstance(c['properties'],dict): raise MigrationError(path+'.properties: mapping required')
            c['properties'] = {key: editor_property(value,path+'.'+key) for key,value in c['properties'].items()}
            # These fields were absent from the v1 editor view. Preserve the
            # exact v1 constructor defaults, not inferred resource dimensions.
            if c['type'] == 'SpriteRendererComponent':
                for field in ('center','extents'):
                    c['properties']['localBounds.'+field] = {'kind':'vector3','value':vec(dict.fromkeys('xyz',0.0),3)}
                c['properties']['localBounds.hasExplicitMetadata'] = {'kind':'boolean','value':False}
            if c['type'] == 'Collider2DComponent':
                for key,value in [('category','1'),('mask',str(2**64-1))]:
                    c['properties']['filter.'+key] = {'kind':'unsigned','value':value}
                c['properties']['filter.group'] = {'kind':'integer','value':0}
    parents = {item['id']:item['parent'] for item in result['objects']}
    for local in ids:
        visited = set()
        while local:
            if local in visited or local not in parents: raise MigrationError('$.objects.parent: dangling or cycle')
            visited.add(local); local = parents[local]
    if old['selected'] and old['selected'] not in ids: raise MigrationError('$.selected: dangling')
    return result


class StrictLoader(yaml.SafeLoader):
    pass


def unique_mapping(loader, node, deep=False):
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if key in result: raise MigrationError(f'duplicate YAML key: {key}')
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


StrictLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, unique_mapping)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    if args.source.resolve() == args.destination.resolve():
        parser.error('source and destination must differ; validate before replacing the original')
    try:
        value = yaml.load(args.source.read_text(encoding='utf-8-sig'), Loader=StrictLoader)
        output = yaml.safe_dump(convert(value), sort_keys=True, allow_unicode=True)
        args.destination.write_text(output, encoding='utf-8')
    except (MigrationError, ValueError, KeyError, TypeError, OSError, yaml.YAMLError) as error:
        print(f'{args.source}: {error}', file=sys.stderr)
        return 1
    print(f'{args.destination}: migrated schema 1 -> 2; stored body velocities become authored initial conditions')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
