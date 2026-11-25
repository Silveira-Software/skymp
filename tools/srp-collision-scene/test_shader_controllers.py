"""Shader animation cannot move a Havok body; unknown spatial animation still refuses."""
import math
import os
import struct
import unittest

import havok
import nif
from test_havok import node, pack, sphere


def encode(blocks, strings):
    names = list(dict.fromkeys(n for n, _ in blocks))
    data = b'Gamebryo File Format, Version 20.2.0.7\n'
    data += pack('IBIII', 0x14020007, 1, 12, len(blocks), 100) + b'\0\0\0'
    data += pack('H', len(names))
    for value in names:
        data += pack('I', len(value)) + value.encode()
    data += pack('H' * len(blocks), *(names.index(n) for n, _ in blocks))
    data += pack('I' * len(blocks), *(len(b) for _, b in blocks))
    data += pack('II', len(strings), max(map(len, strings), default=0))
    for value in strings:
        data += pack('I', len(value)) + value.encode()
    return data + pack('I', 0) + b''.join(b for _, b in blocks) + pack('II', 1, 0)


def lamp():
    raw = sphere()
    blocks = [(name, raw[o:o+s]) for name, o, s in nif.Nif(raw).blocks]
    blocks[0] = ('NiNode', node(controller=4))
    time = lambda following, target: pack('IH4fI', following, 76, 1, 0, 0, 1, target)
    blocks.extend([
        ('NiControllerManager', time(5, 0) + pack('BIII', 0, 1, 6, 0xffffffff)),
        ('NiMultiTargetTransformController', time(0xffffffff, 0) + pack('HII', 2, 0xffffffff, 0xffffffff)),
        ('NiControllerSequence', pack('III', 0, 1, 1) +
         pack('IIB5I', 7, 9, 0, 0, 1, 2, 0xffffffff, 0xffffffff) +
         pack('fII3fIIH', 1, 0xffffffff, 0, 1, 0, 1, 4, 0, 0)),
        ('NiFloatInterpolator', pack('fI', 1, 8)),
        ('NiFloatData', pack('II4f', 2, 1, 0, 0, 1, 1)),
        ('BSLightingShaderPropertyFloatController', time(0xffffffff, 10) + pack('II', 11, 11)),
        ('BSLightingShaderProperty', b''),
        ('NiBlendFloatInterpolator', pack('BBff', 1, 2, 0, 1)),
    ])
    return blocks, ['Root', 'BSLightingShaderProperty', 'BSLightingShaderPropertyFloatController']


class ShaderControllerTests(unittest.TestCase):
    def test_shader_sequence_preserves_static_havok_geometry(self):
        blocks, strings = lamp()
        triangles, volumes, margin, doc = havok.read(encode(blocks, strings))
        expected = havok.read(sphere())
        self.assertEqual((triangles, volumes, margin), expected[:3])
        self.assertEqual(doc.has_movable_body, expected[3].has_movable_body)
        self.assertEqual(doc.strings, strings)

    def test_unbound_spatial_targets_and_shader_types_are_required(self):
        cases = []
        blocks, strings = lamp()
        cases.append((5, blocks[5][1][:-4] + pack('I', 0)))
        cases.append((4, blocks[4][1][:-4] + pack('I', 999)))
        cases.append((6, blocks[6][1][:65] + pack('I', 99) + blocks[6][1][69:]))
        cases.append((9, blocks[9][1][:22] + pack('I', 0) + blocks[9][1][26:]))
        cases.append((9, blocks[9][1][:-4] + pack('I', 999)))
        cases.append((7, pack('fI', 1, 999)))
        cases.append((8, pack('II4f', 2, 1, 0, 0, 1, math.nan)))
        cases.append((11, pack('BBff', 0, 2, 0, 1)))
        cases.append((6, blocks[6][1] + b'\0'))
        cases.append((4, blocks[4][1][:31]))
        for index, payload in cases:
            with self.subTest(index=index, payload=payload.hex()):
                changed = blocks.copy()
                changed[index] = (changed[index][0], payload)
                with self.assertRaises(nif.NifError):
                    havok.read(encode(changed, strings))

    def test_transform_or_mislabelled_sequence_is_not_shader_only(self):
        blocks, strings = lamp()
        for index, kind in [(7, 'NiTransformInterpolator'), (9, 'NiTransformController')]:
            with self.subTest(kind=kind):
                changed = blocks.copy()
                changed[index] = (kind, changed[index][1])
                with self.assertRaises(nif.NifError):
                    havok.read(encode(changed, strings))
        strings[2] = 'NiTransformController'
        with self.assertRaises(nif.NifError):
            havok.read(encode(blocks, strings))

    def test_real_pinned_lantern_matches_its_unchanged_static_collision(self):
        path = os.environ.get('SRP_R49_LANTERN_NIF')
        if not path:
            self.skipTest('set SRP_R49_LANTERN_NIF for the shipped-asset acceptance')
        import hashlib
        with open(path, 'rb') as stream:
            raw = stream.read()
        self.assertEqual(hashlib.sha256(raw).hexdigest(),
                         'af936b61381d3771495a547e6f079848b41a84e26cd493dcbf7829cf0ce89f65')
        doc = nif.Nif(raw)
        detached = bytearray(raw)
        # Detach only the root's controller pointer for a static-geometry control.
        offset = doc.blocks[0][1]
        cursor = nif.Cursor(raw[offset:offset + doc.blocks[0][2]])
        cursor.u32()
        cursor.u32s(cursor.u32())
        struct.pack_into('<I', detached, offset + cursor.o, 0xffffffff)
        expected = havok.read(bytes(detached))
        actual = havok.read(raw)
        self.assertEqual(actual[:3], expected[:3])
        self.assertTrue(actual[1])
        self.assertFalse(actual[3].has_movable_body)


if __name__ == '__main__':
    unittest.main()
