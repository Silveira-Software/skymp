"""Synthetic graph regressions, separate from the real-asset acceptance run."""
import math
import struct
import unittest
import havok
import nif


def pack(fmt, *values):
    return struct.pack('<'+fmt, *values)


def file(blocks):
    names=list(dict.fromkeys(n for n,_ in blocks))
    data=b'Gamebryo File Format, Version 20.2.0.7\n'+pack('IBIII',0x14020007,1,12,len(blocks),100)+b'\0\0\0'+pack('H',len(names))
    for name in names:
        data+=pack('I',len(name))+name.encode()
    data+=pack('H'*len(blocks),*(names.index(n) for n,_ in blocks))
    data+=pack('I'*len(blocks),*(len(b) for _,b in blocks))+pack('III',0,0,0)
    data+=b''.join(b for _,b in blocks)+pack('II',1,0)
    return data


def node(collision=2, translation=(0,0,0), controller=0xffffffff):
    return pack('IIII',0xffffffff,0,controller,0)+pack('3f',*translation)+pack('9f',1,0,0,0,1,0,0,0,1)+pack('fII',1,collision,0)


def sphere(radius=1, body_type='bhkRigidBody', translation=(0,0,0)):
    body=bytearray(250)
    struct.pack_into('<I',body,0,3)
    struct.pack_into('<4f',body,52,*translation,0)
    struct.pack_into('<4f',body,68,0,0,0,1)
    return file([('NiNode',node()),(body_type,bytes(body)),
                 ('bhkCollisionObject',pack('IHI',0,0x81,1)),
                 ('bhkSphereShape',pack('If',0,radius))])


class HavokGraphTests(unittest.TestCase):
    def test_hinged_bodies_import_both_shapes_and_require_runtime_pose(self):
        original = sphere()
        blocks = [(name, original[offset:offset+length])
                  for name, offset, length in nif.Nif(original).blocks]
        root = node()[:-4] + pack('2I', 1, 4)
        body = blocks[1][1]
        hinged = body[:244] + pack('2I', 1, 7) + body[248:]
        hinge = pack('4I35fB', 2, 1, 5, 1, *([0.] * 35), 0)
        blocks[0] = ('NiNode', root)
        blocks[1] = ('bhkRigidBody', hinged)
        blocks.extend([('NiNode', node(collision=6, translation=(0, 0, 3))),
                       ('bhkRigidBody', body),
                       ('bhkCollisionObject', pack('IHI', 4, 0x81, 5)),
                       ('bhkLimitedHingeConstraint', hinge)])
        _, volumes, _, doc = havok.read(file(blocks))
        self.assertEqual([v['center'] for v in volumes], [(0, 0, 0), (0, 0, 3)])
        self.assertTrue(doc.has_movable_body)
        blocks[-1] = ('bhkLimitedHingeConstraint', hinge[:8] + pack('I', 4) + hinge[12:])
        with self.assertRaisesRegex(nif.NifError, 'hinge body has no imported collision'):
            havok.read(file(blocks))

    def test_tree_root_applies_parent_transform_to_collision_child(self):
        original = sphere()
        blocks = [(name, original[offset:offset+length])
                  for name, offset, length in nif.Nif(original).blocks]
        child = node(translation=(1, 2, 3))
        tree = node(collision=0xffffffff, translation=(4, 5, 6))[:-4]
        tree += pack('6I', 1, 4, 0, 1, 4, 0)
        blocks[0] = ('BSTreeNode', tree)
        blocks[2] = ('bhkCollisionObject', pack('IHI', 4, 0x81, 1))
        blocks.append(('NiNode', child))
        _, volumes, _, doc = havok.read(file(blocks))
        self.assertEqual(volumes[0]['center'], (5, 7, 9))
        self.assertTrue(doc.has_movable_body)

    def test_capsule_los_envelope_contains_the_whole_real_support_shape(self):
        raw = sphere(body_type='bhkRigidBodyT', translation=(2,-3,4))
        doc = nif.Nif(raw)
        blocks = [(name, raw[offset:offset+length]) for name,offset,length in doc.blocks]
        a, b, radius = (-2.,1.,0.), (3.,-1.,5.), .75
        payload = pack('If8s4f4f',0,radius,b'\0'*8,*a,radius,*b,radius)
        blocks[-1] = ('bhkCapsuleShape',payload)
        triangles, volumes, _, _ = havok.read(file(blocks))
        self.assertEqual(triangles,[])
        self.assertEqual(len(volumes),1)
        self.assertEqual(volumes[0]['kind'],'convex-envelope')
        planes = volumes[0]['planes']
        for end in (a,b):
            for yaw in range(0,360,15):
                for pitch in range(-90,91,15):
                    y,p = math.radians(yaw),math.radians(pitch)
                    direction = (math.cos(p)*math.cos(y),math.cos(p)*math.sin(y),math.sin(p))
                    point = [(end[k]+radius*direction[k]+(2,-3,4)[k])*havok.SCALE for k in range(3)]
                    self.assertTrue(all(sum(plane[k]*point[k] for k in range(3))+plane[3] < 0 for plane in planes))

    def test_capsule_corrupt_payload_and_unequal_radii_remain_unknown(self):
        payload = pack('If8s4f4f',0,1.,b'\0'*8,0.,0.,0.,1.,0.,0.,2.,1.)
        for invalid in [payload[:-1],payload+b'\0',payload[:44]+pack('f',2.),payload[:4]+pack('f',float('nan'))+payload[8:]]:
            with self.assertRaises(nif.NifError):
                havok.capsule(invalid)

    def test_sphere_uses_engine_world_units(self):
        triangles,volumes,margin,_=havok.read(sphere())
        self.assertFalse(triangles)
        self.assertEqual(volumes[0]['radius'],69.99125)
        self.assertEqual(margin,0)

    def test_rigidbody_t_translation_applies_once(self):
        _,volumes,_,_=havok.read(sphere(body_type='bhkRigidBodyT',translation=(1,2,3)))
        self.assertEqual(volumes[0]['center'],(69.99125,139.9825,209.97375))
        _,volumes,_,_=havok.read(sphere(translation=(1,2,3)))
        self.assertEqual(volumes[0]['center'],(0,0,0),'non-T ignores body pose')

    def test_unconsumed_physics_cannot_silently_disappear(self):
        doc=nif.Nif(sphere())
        blocks=[(n,sphere()[o:o+l]) for n,o,l in doc.blocks]
        blocks.append(('bhkSphereShape',pack('If',0,1)))
        with self.assertRaisesRegex(nif.NifError,'unconsumed'):
            havok.read(file(blocks))

    def test_cached_planes_do_not_clip_the_vertex_support_shape(self):
        vertices=[(-1,-1,-1),(1,1,1),(-1,1,-1),(1,-1,1)]
        planes=[(1,0,0,0),(-1,0,0,0),(0,1,0,0),(0,-1,0,0),(0,0,1,0),(0,0,-1,0)]
        raw=pack('If6II',0,.05,0,0,0,0,0,0,len(vertices))
        raw+=b''.join(pack('4f',*v,0)for v in vertices)+pack('I',len(planes))+b''.join(pack('4f',*p)for p in planes)
        vs,ps,radius=havok.convex(raw)
        self.assertGreater(radius,.05)
        for p in ps:
            self.assertLess(max(sum(p[k]*v[k]for k in range(3))+p[3]for v in vs),0)

    def test_bad_radius_and_nonuniform_transform_fail_closed(self):
        with self.assertRaises(nif.NifError):
            havok.read(sphere(float('nan')))
        with self.assertRaises(nif.NifError):
            havok.uniform_scale((2,0,0,0,0,1,0,0,0,0,1,0))

    def test_animation_bound_includes_cubic_tangents_over_key_duration(self):
        # no rotation; quadratic vector translation; no animated scale
        raw=pack('III',0,2,2)
        raw+=pack('10f',0,0,0,0, 10,0,0, 0,0,0)
        raw+=pack('10f',5,1,0,0, 0,0,0, 10,0,0)+pack('I',0)
        translation,scale=havok.animation_bounds(raw)
        self.assertGreaterEqual(translation,51)
        self.assertEqual(scale,0)
        with self.assertRaises(nif.NifError):
            havok.animation_bounds(pack('III',0,2,3))


if __name__=='__main__':
    unittest.main()
