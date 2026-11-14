"""Geometry and malformed-input tests; these are not Skyrim runtime tests."""
import math
import random
import struct
import unittest
import actor_controller
import havok
import nif
from test_havok import file, node, pack


def controller_fixture(translation=(0.,0.,0.), radius=1., r2=None, layer=30, flags=0x81):
    shape = pack('If8x3ff3ff', 0, radius, 0,0,0, radius, 0,0,2, radius if r2 is None else r2)
    body = bytearray(100)
    struct.pack_into('<I', body, 0, 3)
    body[4] = layer
    values = (1,0,0,0, 0,1,0,0, 0,0,1,0, *translation,0)
    struct.pack_into('<16f', body, 36, *values)
    translation_world=tuple(x*havok.SCALE for x in translation)
    return file([('NiNode', node(translation=translation_world)), ('bhkSimpleShapePhantom', bytes(body)),
                 ('bhkSPCollisionObject', pack('IHI', 0, flags, 1)),
                 ('bhkCapsuleShape', shape)])


def convex_raw(vertices):
    planes=[(1,0,0,-1),(-1,0,0,-1),(0,1,0,-1),(0,-1,0,-1),(0,0,1,-1),(0,0,-1,-1)]
    return (pack('If6II',0,.1,0,0,0,0,0,0,len(vertices))+
            b''.join(pack('4f',*p,0) for p in vertices)+pack('I',len(planes))+
            b''.join(pack('4f',*p) for p in planes))


class ControllerTest(unittest.TestCase):
    def test_translations_and_havok_units_apply_once(self):
        triangles, sources = actor_controller.read(controller_fixture(translation=(1,2,3)))
        points = [p for tri in triangles for p in tri]
        eps = havok.SCALE*2e-6
        self.assertAlmostEqual(min(p[0] for p in points), 0., delta=eps)
        self.assertAlmostEqual(max(p[0] for p in points), 2*havok.SCALE, delta=eps)
        self.assertAlmostEqual(min(p[2] for p in points), 2*havok.SCALE, delta=eps)
        self.assertAlmostEqual(max(p[2] for p in points), 6*havok.SCALE, delta=eps)
        self.assertEqual(sources[0]['filterLayer'], 30)
        self.assertGreater(len(triangles), 500)
        envelope = sources[0]['blockingBounds']
        for axis, (lo, hi) in enumerate(((0, 2), (1, 3), (2, 6))):
            self.assertAlmostEqual(envelope['min'][axis], lo * havok.SCALE, delta=eps)
            self.assertAlmostEqual(envelope['max'][axis], hi * havok.SCALE, delta=eps)
            self.assertTrue(all(envelope['min'][axis] <= p[axis] <= envelope['max'][axis] for p in points))

    def test_surface_faces_never_leave_physical_capsule(self):
        rng = random.Random(42100)
        for a,b in [((0,0,0),(0,0,10)), ((-2,3,1),(7,-2,4)), ((1,2,3),(1,2,3))]:
            mesh = actor_controller.inscribed_capsule(a,b,2.3)
            delta = tuple(b[k]-a[k] for k in range(3))
            length2 = sum(x*x for x in delta)
            for tri in mesh:
                for _ in range(3):
                    u,v = rng.random(),rng.random()
                    if u+v>1: u,v=1-u,1-v
                    p = tuple(tri[0][k]*(1-u-v)+tri[1][k]*u+tri[2][k]*v for k in range(3))
                    t = max(0.,min(1.,sum((p[k]-a[k])*delta[k] for k in range(3))/length2)) if length2 else 0.
                    distance = math.sqrt(sum((p[k]-a[k]-t*delta[k])**2 for k in range(3)))
                    self.assertLessEqual(distance, 2.3)

    def test_unsupported_and_inactive_bodies_fail_closed(self):
        for raw in [controller_fixture(r2=.9), controller_fixture(radius=float('nan')),
                    controller_fixture(layer=8), controller_fixture(flags=0)]:
            with self.assertRaises(nif.NifError):
                actor_controller.read(raw)
        with self.assertRaises(nif.NifError):
            actor_controller.capsule(bytes(47))

    def test_unrooted_and_wrong_backlink_do_not_produce_a_proxy(self):
        raw = controller_fixture()
        doc = nif.Nif(raw)
        blocks = [(name,raw[o:o+n]) for name,o,n in doc.blocks]
        blocks[0] = ('NiNode',node(collision=0xffffffff))
        with self.assertRaisesRegex(nif.NifError,'backlink'):
            actor_controller.read(file(blocks))

    def test_phantom_cannot_double_or_replace_the_pinned_bind_transform(self):
        raw=controller_fixture(translation=(1,2,3))
        doc=nif.Nif(raw)
        blocks=[(name,raw[o:o+n]) for name,o,n in doc.blocks]
        blocks[0]=('NiNode',node())
        with self.assertRaisesRegex(nif.NifError,'transform mismatch'):
            actor_controller.read(file(blocks))

    def test_compound_and_mopp_use_the_same_physical_children_once(self):
        raw=controller_fixture();doc=nif.Nif(raw)
        blocks=[(n,raw[o:o+l]) for n,o,l in doc.blocks]
        capsule=blocks[3]
        child_list=pack('III',1,4,0)+pack('6I',0,0,0x80000000,0,0,0x80000000)+pack('II',1,0)
        blocks[3]=('bhkListShape',child_list);blocks.append(capsule)
        direct,_=actor_controller.read(file(blocks))
        self.assertEqual(len(direct),576)
        # child + 12 unused + scale + code size + offset4 + build + bytecode.
        mopp=pack('I12xfI4fB',4,1.,0,0,0,0,1,1)
        blocks[3]=('bhkMoppBvTreeShape',mopp)
        through_mopp,_=actor_controller.read(file(blocks))
        self.assertEqual(through_mopp,direct)
        blocks[3]=('bhkMoppBvTreeShape',mopp+b'\0')
        with self.assertRaisesRegex(nif.NifError,'bytecode'):
            actor_controller.read(file(blocks))
        blocks[3]=('bhkListShape',child_list)
        blocks[4]=('bhkListShape',child_list)
        with self.assertRaisesRegex(nif.NifError,'cycle'):
            actor_controller.read(file(blocks))

    def test_convex_support_hull_is_closed_and_strictly_inside_a_cube(self):
        vertices=[(x,y,z) for x in (-1,1) for y in (-1,1) for z in (-1,1)]
        vertices.extend([(0,0,0),(1,0,0),(-1,0,0)])
        mesh=actor_controller.convex_mesh(convex_raw(vertices))
        self.assertEqual(len(mesh),12)
        edges={}
        volume=0.
        for a,b,c in mesh:
            volume+=a[0]*(b[1]*c[2]-b[2]*c[1])+a[1]*(b[2]*c[0]-b[0]*c[2])+a[2]*(b[0]*c[1]-b[1]*c[0])
            for p in (a,b,c):
                self.assertTrue(all(abs(v)<havok.SCALE for v in p))
            for edge in ((a,b),(b,c),(c,a)):
                key=tuple(sorted(edge));edges[key]=edges.get(key,0)+1
        self.assertTrue(all(n==2 for n in edges.values()))
        self.assertAlmostEqual(abs(volume)/6/havok.SCALE**3,8,places=5)
        for points in [[(0,0,0)]*4,[(0,0,0),(1,0,0),(0,1,0),(1,1,0)]]:
            with self.assertRaisesRegex(nif.NifError,'vertices|degenerate'):
                actor_controller.convex_mesh(convex_raw(points))


if __name__ == '__main__':
    unittest.main()
