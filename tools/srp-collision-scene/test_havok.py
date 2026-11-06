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
