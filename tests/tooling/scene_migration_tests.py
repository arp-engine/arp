"""Offline scene migration contracts and boundary checks."""
import copy
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import migrate_scene_v1 as migration
import yaml


class SceneMigrationTests(unittest.TestCase):
    def setUp(self):
        self.old = yaml.load((ROOT / 'tests/fixtures/scene_migration/orbit.prefab.v1.yaml')
                             .read_text(encoding='utf-8'), Loader=migration.StrictLoader)

    def test_orbit_semantics_and_determinism(self):
        migrated = migration.convert(self.old)
        self.assertEqual(migrated, migration.convert(copy.deepcopy(self.old)))
        self.assertEqual(len(migrated['objects']), 9)
        self.assertEqual(migrated, yaml.safe_load((ROOT / 'examples/survivors/assets/orbit.prefab.yaml').read_text(encoding='utf-8')))
        for old, new in zip(self.old['objects'], migrated['objects']):
            self.assertEqual((old['id'], old['parent'], old['name'], old['active']),
                             (new['id'], new['parent'], new['properties']['name'], new['properties']['active']))
            self.assertEqual(new['transform']['scale']['fields']['x'], old['transform']['sx'])
            if new['components']:
                self.assertEqual(new['components'][0]['fields']['spriteResourceId'], old['components'][0]['spriteId'])
                self.assertEqual(new['components'][0]['fields']['tint']['fields'], dict.fromkeys('xyzw', 1.0))

    def test_invalid_versions_fields_and_types(self):
        for key, value in [('contractVersion', 2), ('contractVersion', True), ('resourceId', 'bad')]:
            old = copy.deepcopy(self.old); old[key] = value
            with self.assertRaises(migration.MigrationError): migration.convert(old)
        for key, value in [('flipX', 1), ('sortingLayer', 2**31), ('spriteId', 'bad'), ('r', float('nan'))]:
            old = copy.deepcopy(self.old); old['objects'][1]['components'][0][key] = value
            with self.assertRaises(migration.MigrationError): migration.convert(old)
        old = copy.deepcopy(self.old); old['objects'][0]['unexpected'] = 1
        with self.assertRaises(migration.MigrationError): migration.convert(old)

    def test_graph_failures(self):
        for parent in [999, 1, -1, True]:
            old = copy.deepcopy(self.old); old['objects'][0]['parent'] = parent
            with self.assertRaises(migration.MigrationError): migration.convert(old)
        old = copy.deepcopy(self.old); old['objects'][1]['id'] = 1
        with self.assertRaises(migration.MigrationError): migration.convert(old)

    def test_duplicate_yaml_keys(self):
        with self.assertRaises(migration.MigrationError):
            yaml.load('type: a\ntype: b\n', Loader=migration.StrictLoader)

    def test_native_mappings(self):
        point = dict(x=0.0, y=0.0)
        body = dict(type='arp.rigid-body2d/1', bodyType='dynamic', velocity=point,
                    angularVelocity=0., gravityScale=1., linearDamping=0., angularDamping=0., bullet=False, fixedRotation=False)
        collider = dict(type='arp.collider2d/1', shapeType='circle', center=point, halfSize=point,
                        pointA=point, pointB=point, points=[], category='8000000000000000', mask='ffffffffffffffff',
                        group=-2, radius=.5, density=1., friction=.6, restitution=0., sensor=False, loop=False)
        joint = dict(type='arp.joint2d/1', jointType='distance', bodyA=1, bodyB=2,
                     localAnchorA=point, localAnchorB=point, localAxisA=dict(x=1., y=0.), length=1., referenceAngle=0.,
                     lowerLimit=0., upperLimit=1., motorSpeed=0., maxMotorForce=0., enableLimit=False,
                     enableMotor=False, collideConnected=False)
        identifier = self.old['resourceId']
        cases = [body, collider, joint,
                 dict(type='arp.sprite-animation/1', animationId=identifier, speed=1.234567890123, paused=True, eventBudget=8),
                 dict(type='arp.tile-map/1', tileMapId=identifier, maxResidentChunks=4, maxVisibleCells=32, firstObjectId=2**53+1),
                 dict(type='arp.object-links/1', links=[dict(name='target', target=2)]),
                 dict(type='arp.script-component/1', scriptResourceId=identifier, schemaVersion=1, scriptType='Link', enabled=False, fields={'value':12})]
        for case in cases:
            result = migration.component(case, '$.component')
            self.assertEqual(result['type'], case['type'])
            self.assertEqual(result['schemaVersion'], 1)
        result = migration.component(collider, '$')
        self.assertEqual(result['fields']['filter']['fields']['mask'], str(2**64-1))
        self.assertEqual(migration.component(cases[3], '$')['fields']['speed'], cases[3]['speed'])
        self.assertEqual(migration.component(cases[4], '$')['fields']['firstObjectId'], str(2**53+1))
        old = copy.deepcopy(self.old)
        old['objects'][0]['components'] = [joint]
        with self.assertRaises(migration.MigrationError): migration.convert(old)

    def test_editor_document_conversion(self):
        old = dict(type='arp.editor-scene', contractVersion=1, name='Editor', selected=1, objects=[
            dict(id=1,parent=0,childIndex=0,name='object',active=False,
                 transform=dict(translation=dict.fromkeys('xyz',0.),scale=dict.fromkeys('xyz',1.),rotation=dict(x=0.,y=0.,z=0.,w=1.)),
                 components=[dict(type='SpriteAnimationComponent',properties={
                     'enabled':dict(kind='boolean',value=False), 'animationResourceId':dict(kind='resource',value={'empty':True}),
                     'speed':dict(kind='number',value=1.234567890123), 'paused':dict(kind='boolean',value=True),
                     'eventBudget':dict(kind='unsigned',value=8)})])])
        new = migration.convert(old)
        self.assertEqual(new['contractVersion'],2)
        properties = new['objects'][0]['components'][0]['properties']
        self.assertEqual(properties['speed']['value'],1.234567890123)
        self.assertEqual(properties['eventBudget']['value'],'8')
        self.assertEqual(properties['animationResourceId']['value'],'')
        self.assertEqual(new['objects'][0]['transform']['rotation']['type'],'Quat')
        with self.assertRaises(migration.MigrationError):
            migration.editor_property(dict(kind='string',value={'empty':1}),'$')

    def test_editor_declared_kind_mappings(self):
        cases = [('string', {'empty':True}, ''), ('string', {'text':'abc'}, 'abc'),
                 ('unsigned', 42, '42'), ('vector2', {'x':1.,'y':2.}, migration.typed('Vec2', {'x':1.,'y':2.})),
                 ('color4', dict(r=1.,g=.5,b=.2,a=1.), migration.typed('Vec4', dict(x=1.,y=.5,z=.2,w=1.)))]
        for kind, old, expected in cases:
            self.assertEqual(migration.editor_property(dict(kind=kind,value=old),'$')['value'], expected)
        with self.assertRaises(migration.MigrationError):
            migration.editor_property(dict(kind='guess',value={}), '$')


if __name__ == '__main__': unittest.main()
