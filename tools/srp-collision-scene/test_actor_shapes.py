"""Actor identity, override/template provenance and skeleton-model regressions."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
import actor_shapes
import esm
from test_load_order import FixturePlugin
from test_actor_controller import controller_fixture


def field(tag, raw):
    return tag + struct.pack('<H',len(raw)) + raw


class ActorShapesTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.raw = controller_fixture()
        self.model = 'Actors/Test/skeleton.nif'
        data = bytearray(36)
        struct.pack_into('<2f', data, 16, 1.1, .9)
        struct.pack_into('<I', data, 32, 1)
        race = esm.Record(b'RACE',0x10,0,field(b'DATA',data)+
            field(b'MNAM',b'')+field(b'ANAM',self.model.encode()+b'\0')+
            field(b'FNAM',b'')+field(b'ANAM',b'Actors/Test/female.nif\0')+
            field(b'NAM2',b'')+field(b'MODL',b'Actors/Test/behavior.hkx\0'))
        acbs = bytes(24)
        npc = esm.Record(b'NPC_',0x20,0,field(b'ACBS',acbs)+
            field(b'RNAM',struct.pack('<I',0x10))+field(b'NAM6',struct.pack('<f',1.2)))
        player = esm.Record(b'NPC_',7,0,npc.data)
        self.order = esm.LoadOrder('', ['Skyrim.esm'])
        self.order.plugins = {0:FixturePlugin('Skyrim.esm',[],{0x10:race,0x20:npc,7:player})}

    def produce(self, row=None, schema=2):
        row = row or {'baseDesc':'20:Skyrim.esm','raceDesc':'10:Skyrim.esm',
            'model':self.model,'sha256':hashlib.sha256(self.raw).hexdigest(),'sex':0}
        path = self.root/'manifest.json'
        path.write_text(json.dumps({'schemaVersion':schema,'actors':[row]}))
        raw = self.raw
        class Assets:
            def read(self, path):
                return raw
        # Legacy callback models an already decoded physical triangle.
        return actor_shapes.produce_actor_shapes(str(path),self.order,Assets(),str(self.root/'assets'),
            lambda _: ([((0,0,0),(0,1,0),(1,0,0))],None,False))

    def test_valid_formdesc_and_skeletal_anam_reach_the_physical_reader(self):
        shapes,bindings,audit = self.produce()
        self.assertEqual(len(shapes),1)
        self.assertEqual(shapes[0]['representation'],'havok-controller-inscribed-v1')
        self.assertEqual(shapes[0]['sex'],0)
        self.assertAlmostEqual(shapes[0]['localTransform'][0],1.32,places=6)
        self.assertEqual(bindings[self.model.lower()],hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(shapes[0]['asset'],self.model.lower())
        self.assertTrue((self.root/'assets'/self.model.lower()).is_file())
        self.assertEqual(audit[0]['controllerSources'][0]['shapeBlock'],3)
        self.assertFalse(audit[0]['enginePoseParity'])

    def test_player_race_and_sex_require_a_playable_real_record(self):
        shapes,_,_ = self.produce({'baseDesc':'7:Skyrim.esm','raceDesc':'10:Skyrim.esm',
            'model':'Actors/Test/female.nif','sha256':hashlib.sha256(self.raw).hexdigest(),'sex':1})
        self.assertEqual(shapes[0]['sex'],1)
        self.assertAlmostEqual(shapes[0]['localTransform'][0],1.08,places=6)
        race = self.order.plugins[0].records[0x10]
        raw = bytearray(race.data)
        # DATA begins after its six-byte subrecord header.
        struct.pack_into('<I',raw,6+32,0)
        race.data = bytes(raw)
        with self.assertRaisesRegex(ValueError,'not playable'):
            self.produce({'baseDesc':'7:Skyrim.esm','raceDesc':'10:Skyrim.esm',
                'model':self.model,'sha256':hashlib.sha256(self.raw).hexdigest(),'sex':0})

    def test_wrong_sex_unlinked_asset_and_unsafe_formdesc_are_rejected(self):
        for overrides, message in [({'sex':1},'sex mismatches'),
                ({'baseDesc':'20:Skyrim/esm'},'FormDesc'),
                ({'model':'Actors/Test/behavior.hkx'},'path unsafe'),
                ({'model':'Actors/Test/not-linked.nif'},'not linked'),
                ({'sha256':'0'*64},'hash mismatch')]:
            row = {'baseDesc':'20:Skyrim.esm','raceDesc':'10:Skyrim.esm',
                'model':self.model,'sha256':hashlib.sha256(self.raw).hexdigest(),'sex':0}
            row.update(overrides)
            with self.assertRaisesRegex(ValueError,message):
                self.produce(row)

    def test_only_original_transformation_races_extend_player_controller_admission(self):
        records = self.order.plugins[0].records
        template = bytearray(records[0x10].data)
        struct.pack_into('<I', template, 6 + 32, 0)
        self.order.names.append('Dawnguard.esm')
        dawnguard = FixturePlugin('Dawnguard.esm', ['Skyrim.esm'], {})
        self.order.plugins[1] = dawnguard
        for plugin_records, form, descriptor, editor_id in (
            (records, 0xcdd84, 'cdd84:Skyrim.esm', 'WerewolfBeastRace'),
            (dawnguard.records, 0x0100283a, '283a:Dawnguard.esm', 'DLC1VampireBeastRace'),
        ):
            with self.subTest(race=descriptor):
                race = esm.Record(b'RACE', form, 0, bytes(template) +
                                  field(b'EDID', editor_id.encode() + b'\0'))
                plugin_records[form] = race
                row = {'baseDesc': '7:Skyrim.esm', 'raceDesc': descriptor, 'model': self.model,
                       'sha256': hashlib.sha256(self.raw).hexdigest(), 'sex': 0}
                shapes, _, _ = self.produce(row)
                self.assertEqual(shapes[0]['raceDesc'], descriptor)
                race.data = bytes(template) + field(b'EDID', b'OtherRace\0')
                with self.assertRaisesRegex(ValueError, 'not playable'):
                    self.produce(row)

    def test_deterministic_traits_are_followed_but_cycles_are_not_guessed(self):
        plugin = self.order.plugins[0]
        npc = plugin.records[0x20]
        acbs = bytearray(24);struct.pack_into('<H',acbs,18,1)
        child = esm.Record(b'NPC_',0x21,0,field(b'ACBS',acbs)+field(b'TPLT',struct.pack('<I',0x20)))
        plugin.records[0x21] = child
        shapes,_,_ = self.produce({'baseDesc':'21:Skyrim.esm','raceDesc':'10:Skyrim.esm',
            'model':self.model,'sha256':hashlib.sha256(self.raw).hexdigest(),'sex':0})
        self.assertEqual(shapes[0]['baseDesc'],'21:Skyrim.esm')
        npc.data = field(b'ACBS',acbs)+field(b'TPLT',struct.pack('<I',0x21))
        with self.assertRaisesRegex(ValueError,'cyclic'):
            self.produce({'baseDesc':'21:Skyrim.esm','raceDesc':'10:Skyrim.esm',
                'model':self.model,'sha256':hashlib.sha256(self.raw).hexdigest(),'sex':0})

    def test_legacy_triangle_manifest_remains_readable(self):
        npc = self.order.plugins[0].records[0x20]
        npc.data += field(b'MODL',self.model.encode()+b'\0')
        shapes,_,_ = self.produce({'baseDesc':'20:Skyrim.esm','raceDesc':'10:Skyrim.esm',
            'model':self.model,'sha256':hashlib.sha256(self.raw).hexdigest()},schema=1)
        self.assertEqual(shapes[0]['representation'],'havok-triangles')
        self.assertNotIn('sex',shapes[0])


if __name__=='__main__':
    unittest.main()
