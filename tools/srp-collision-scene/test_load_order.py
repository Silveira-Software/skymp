"""Regression for mod-local master slots and winning collision references."""
import unittest
import esm
import producer
import struct


class FixturePlugin:
    def __init__(self, name, masters, records, references=()):
        self.name, self.masters, self.records = name, masters, records
        self.refs = references

    def record(self, form_id):
        return self.records.get(form_id)

    def references(self, cell_id):
        return self.refs if cell_id == 0x3A else []


class LoadOrderTest(unittest.TestCase):
    def order(self):
        base = esm.Record(b'STAT', 0x7A, 0, b'')
        overridden = esm.Record(b'STAT', 0x7A, 0, b'override')
        new = esm.Record(b'STAT', 0x0100007A, 0, b'new')
        ref = esm.Record(b'REFR', 0x7B, 0, b'')
        deleted = esm.Record(b'REFR', 0x7B, 0x20, b'')
        added = esm.Record(b'REFR', 0x0100007B, 0, b'')
        order = esm.LoadOrder('', ['Skyrim.esm', 'Unused.esm', 'Pack.esp'])
        order.plugins = {
            0: FixturePlugin('Skyrim.esm', [], {0x7A: base}, [ref]),
            1: FixturePlugin('Unused.esm', ['Skyrim.esm'], {}),
            2: FixturePlugin('Pack.esp', ['Skyrim.esm'], {0x7A: overridden, 0x0100007A: new}, [deleted, added]),
        }
        return order

    def test_mod_own_slot_uses_its_masters_and_winning_override(self):
        order = self.order()
        self.assertEqual(order.to_global(2, 0x0100007A), 0x0200007A)
        self.assertEqual(order.resolve(0x0200007A).data, b'new')
        self.assertEqual(order.resolve(0x7A).data, b'override')
        self.assertEqual(order.resolve_with_source(0x7A)[0], 2)

    def test_deleted_override_is_excluded_and_new_reference_keeps_identity(self):
        refs = self.order().cell_references(0x3A)
        self.assertEqual(len(refs), 1)
        self.assertEqual(refs[0][:2], (0x0200007B, 2))

    def test_missing_master_slot_is_never_guessed(self):
        with self.assertRaises(esm.EsmError):
            self.order().to_global(2, 0x0400007A)

    def test_unknown_geometry_denies_its_transformed_full_bounds(self):
        bounds = struct.pack('<6h', -100, -20, -50, 100, 20, 50)
        record = esm.Record(b'STAT', 0x7A, 0, b'OBND' + struct.pack('<H', len(bounds)) + bounds)
        matrix = producer.transform_matrix([1000, 2000, 3000], [0, 0, 0], 2)
        self.assertEqual(producer.unresolved_bounds(record, matrix), ([800, 1960, 2900], [1200, 2040, 3100]))
        self.assertIsNone(producer.unresolved_bounds(None, matrix))


if __name__ == '__main__':
    unittest.main()
