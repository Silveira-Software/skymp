"""Minimal Skyrim ESM/ESP reader for collision scene production.

Only what the producer needs: walk GRUPs, read one CELL's references, resolve
each reference's base record and take the model path from that base.

The index stores offsets, not record bytes, because Skyrim.esm is ~238 MB. Every
parse is self-checked (signatures must be printable 4CC, sizes must stay inside
their parent) so a wrong record-header width cannot silently shift reads.
"""
import struct
import zlib

# Records whose data is zlib-compressed in form version 43+ (Skyrim SE).
RECORD_COMPRESSED = 0x00040000


def _inflate(data, name, pos):
    """Compressed records carry a u32 original size, then the zlib stream."""
    for payload in (data[4:], data):
        try:
            out = zlib.decompress(payload)
        except zlib.error:
            continue
        if 0 < len(out) <= 16 * 1024 * 1024:
            return out
    raise EsmError('%s: cannot inflate compressed record at %d' % (name, pos))


class EsmError(RuntimeError):
    pass


def _sig_ok(raw):
    return len(raw) == 4 and all(32 <= b < 127 for b in raw)


class Record:
    __slots__ = ('sig', 'form_id', 'flags', 'data')

    def __init__(self, sig, form_id, flags, data):
        self.sig = sig
        self.form_id = form_id
        self.flags = flags
        self.data = data

    def subrecords(self):
        o = 0
        n = len(self.data)
        while o + 6 <= n:
            sig = self.data[o:o + 4]
            if not _sig_ok(sig):
                return
            size = struct.unpack_from('<H', self.data, o + 4)[0]
            o += 6
            if o + size > n:
                return
            yield sig, self.data[o:o + size]
            o += size

    def field(self, wanted):
        for sig, payload in self.subrecords():
            if sig == wanted:
                return payload
        return None

    def fields(self, wanted):
        return [payload for sig, payload in self.subrecords() if sig == wanted]

    def zstring(self, wanted):
        raw = self.field(wanted)
        if raw is None:
            return None
        return raw.split(b'\x00', 1)[0].decode('latin-1')


class Plugin:
    def __init__(self, path, name):
        self.path = path
        self.name = name
        with open(path, 'rb') as f:
            self.raw = f.read()
        if len(self.raw) < 40:
            raise EsmError('%s: too short' % name)
        if self.raw[0:4] not in (b'TES4', b'TES5'):
            raise EsmError('%s: not a plugin (sig %r)' % (name, self.raw[0:4]))
        # Record headers are 20 bytes, or 24 when the file carries extra version
        # fields; decide once from where the first record ends.
        self.header_size = 20
        tes4_size = struct.unpack_from('<I', self.raw, 4)[0]
        for size in (20, 24):
            pos = size + tes4_size
            nxt = self.raw[pos:pos + 4]
            if nxt == b'GRUP' or _sig_ok(nxt):
                self.header_size = size
                break
        self.index = {}
        self.cell_ids = {}
        self.edid_to_cell = {}
        tes4 = self.record_at(0)
        self.masters = [m.split(b'\x00', 1)[0].decode('latin-1') for m in tes4.fields(b'MAST')]
        self._walk(self.header_size + len(tes4.data), len(self.raw))

    # -- low level ---------------------------------------------------------
    def record_at(self, pos):
        size = self.header_size
        data_size = struct.unpack_from('<I', self.raw, pos + 4)[0]
        flags = struct.unpack_from('<I', self.raw, pos + 8)[0]
        form_id = struct.unpack_from('<I', self.raw, pos + 12)[0]
        start = pos + size
        end = start + data_size
        if end > len(self.raw):
            raise EsmError('%s: record at %d overruns the file' % (self.name, pos))
        data = self.raw[start:end]
        if flags & RECORD_COMPRESSED:
            data = _inflate(data, self.name, pos)
        return Record(self.raw[pos:pos + 4], form_id, flags, data)

    def record(self, form_id):
        entry = self.index.get(form_id)
        if entry is None:
            return None
        return self.record_at(entry)

    def _walk(self, start, end):
        pos = start
        while pos + 24 <= end:
            sig = self.raw[pos:pos + 4]
            if sig == b'GRUP':
                size = struct.unpack_from('<I', self.raw, pos + 4)[0]
                if size < 24 or pos + size > end:
                    raise EsmError('%s: group at %d has bad size %d' % (self.name, pos, size))
                self._walk(pos + 24, pos + size)
                pos += size
                continue
            if not _sig_ok(sig):
                raise EsmError('%s: bad signature %r at %d' % (self.name, sig, pos))
            data_size = struct.unpack_from('<I', self.raw, pos + 4)[0]
            form_id = struct.unpack_from('<I', self.raw, pos + 12)[0]
            self.index.setdefault(form_id, pos)
            if sig == b'CELL':
                self.cell_ids[form_id] = pos
            pos += self.header_size + data_size

    # -- cell level --------------------------------------------------------
    def cell_edids(self):
        for form_id, pos in self.cell_ids.items():
            edid = self.record_at(pos).zstring(b'EDID')
            if edid:
                self.edid_to_cell[edid.lower()] = form_id
        return self.edid_to_cell

    def find_cell(self, value):
        """Accepts a form id ('0x000165a4', '165a4') or an editor id."""
        text = str(value)
        if text.lower().startswith('0x'):
            return int(text, 16)
        try:
            return int(text, 16)
        except ValueError:
            pass
        return self.cell_edids().get(text.lower())

    def references(self, cell_form_id):
        """Materialise the REFR records of one cell (its child group)."""
        found = []
        self._collect(self.header_size + len(self.record_at(0).data), len(self.raw),
                      cell_form_id, False, found)
        return found

    def _collect(self, start, end, cell_form_id, inside, found):
        pos = start
        while pos + 24 <= end:
            sig = self.raw[pos:pos + 4]
            if sig == b'GRUP':
                size = struct.unpack_from('<I', self.raw, pos + 4)[0]
                label = self.raw[pos + 8:pos + 12]
                group_type = struct.unpack_from('<i', self.raw, pos + 12)[0]
                if size < 24 or pos + size > end:
                    raise EsmError('%s: group at %d has bad size %d' % (self.name, pos, size))
                # Cell children live in a type-6 group labelled with the cell id,
                # nested inside the interior/exterior cell group.
                enters = group_type == 6 and struct.unpack('<I', label)[0] == cell_form_id
                self._collect(pos + 24, pos + size, cell_form_id, inside or enters, found)
                pos += size
                continue
            data_size = struct.unpack_from('<I', self.raw, pos + 4)[0]
            if inside and sig == b'REFR':
                found.append(self.record_at(pos))
            pos += self.header_size + data_size


class LoadOrder:
    """Resolves a form id through the plugin selected by its high byte."""

    def __init__(self, data_dir, names):
        self.data_dir = data_dir
        self.names = list(names)
        self.plugins = {}

    def load(self, index):
        import os
        if index >= len(self.names):
            raise EsmError('plugin index %d is beyond the load order (%d files)' %
                           (index, len(self.names)))
        if index not in self.plugins:
            name = self.names[index]
            self.plugins[index] = Plugin(os.path.join(self.data_dir, name), name)
        return self.plugins[index]

    def resolve(self, form_id):
        _, record = self.resolve_with_source(form_id)
        return record

    def to_global(self, plugin_index, local_id):
        if not local_id:
            return 0
        plugin = self.load(plugin_index)
        names = plugin.masters + [plugin.name]
        slot = local_id >> 24
        if slot >= len(names) or names[slot] not in self.names:
            raise EsmError('unresolved local FormID %08x in %s' % (local_id, plugin.name))
        return (self.names.index(names[slot]) << 24) | (local_id & 0xFFFFFF)

    def to_local(self, plugin_index, global_id):
        owner = self.plugin_name(global_id)
        plugin = self.load(plugin_index)
        names = plugin.masters + [plugin.name]
        if owner not in names:
            return None
        return (names.index(owner) << 24) | (global_id & 0xFFFFFF)

    def resolve_with_source(self, form_id):
        # Overrides encode the original owner's slot in the overriding file's
        # master list. That slot is independent of the server's load order.
        for index in reversed(range(len(self.names))):
            local = self.to_local(index, form_id)
            if local is not None:
                record = self.load(index).record(local)
                if record is not None:
                    return index, record
        return None, None

    def cell_references(self, cell_id):
        winners = {}
        for index in range(len(self.names)):
            local = self.to_local(index, cell_id)
            if local is None:
                continue
            for record in self.load(index).references(local):
                winners[self.to_global(index, record.form_id)] = (index, record)
        return [(form_id, index, record) for form_id, (index, record) in winners.items()
                if not record.flags & 0x20 and not record.flags & 0x800]

    def plugin_name(self, form_id):
        return self.names[(form_id >> 24) & 0xFF]
