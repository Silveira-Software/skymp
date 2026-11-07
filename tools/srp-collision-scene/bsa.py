"""Bethesda BSA (v105, Skyrim Special Edition) reader.

Layout verified against Skyrim - Meshes0.bsa:
  header (36 bytes)
  one 24-byte folder record per folder (file count at +8)
  per folder, interleaved:
      folder name (u8 length including the NUL, then the NUL-terminated name)
      one 16-byte file record per file: hash u64, size u32, offset u32
  file name block: total_file_name_length bytes of NUL-terminated names, in the
      same order as the file records (the block ends with a NUL)
  file data

Compression is declared by the archive flags, not by the per-file size bit: an
SSE entry is [u32 uncompressed size][LZ4 frame] unless the size has bit 30 set,
which marks the entry as stored verbatim.
"""
import importlib.util
import os
import struct

MAGIC = b'BSA\x00'
FOLDER_RECORD = 24
FILE_RECORD = 16
FLAG_INCLUDE_DIR_NAMES = 0x1
FLAG_INCLUDE_FILE_NAMES = 0x2
FLAG_COMPRESSED = 0x4
FLAG_EMBED_FILE_NAMES = 0x100
SIZE_STORED = 0x40000000


def _load_lz4():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location('srp_lz4', os.path.join(here, 'lz4.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


lz4 = _load_lz4()


class BsaError(RuntimeError):
    pass


class BsaArchive:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as f:
            head = f.read(36)
            if len(head) != 36 or head[:4] != MAGIC:
                raise BsaError('%s: not a BSA archive' % path)
            (_, self.version, self.offset, self.archive_flags, self.folder_count,
             self.file_count, self.total_folder_name_length,
             self.total_file_name_length, self.file_flags) = struct.unpack('<4sIIIIIIII', head)
            if self.version != 105:
                raise BsaError('%s: unsupported BSA version %d' % (path, self.version))
            self._entries = self._read_index(f)

    def _read_index(self, f):
        f.seek(self.offset)
        recs = f.read(FOLDER_RECORD * self.folder_count)
        if len(recs) != FOLDER_RECORD * self.folder_count:
            raise BsaError('%s: truncated folder records' % self.path)
        counts = [struct.unpack_from('<I', recs, FOLDER_RECORD * i + 8)[0]
                  for i in range(self.folder_count)]
        if sum(counts) != self.file_count:
            raise BsaError('%s: folder counts sum to %d, header says %d' %
                           (self.path, sum(counts), self.file_count))
        entries = []
        for count in counts:
            length = f.read(1)
            if not length:
                raise BsaError('%s: truncated folder name' % self.path)
            name = f.read(length[0])[:-1].decode('latin-1') if length[0] else ''
            block = f.read(FILE_RECORD * count)
            if len(block) != FILE_RECORD * count:
                raise BsaError('%s: truncated file records for %s' % (self.path, name))
            for k in range(count):
                _, size, offset = struct.unpack_from('<QII', block, FILE_RECORD * k)
                entries.append([name, None, size, offset])
        names = f.read(self.total_file_name_length)
        if len(names) != self.total_file_name_length:
            raise BsaError('%s: truncated file name block' % self.path)
        cursor = f.tell()
        parts = names.split(b'\x00')
        if parts and parts[-1] == b'':
            parts.pop()
        if len(parts) != len(entries):
            raise BsaError('%s: %d file names for %d file records' %
                           (self.path, len(parts), len(entries)))
        for entry, raw in zip(entries, parts):
            entry[1] = raw.decode('latin-1')
        self._data_start = cursor
        if entries and min(e[3] for e in entries) < cursor:
            raise BsaError('%s: file data overlaps the index' % self.path)
        return entries

    def entries(self):
        return list(self._entries)

    def find(self, wanted):
        key = wanted.replace('/', '\\').lower().lstrip('\\')
        for entry in self._entries:
            if ('%s\\%s' % (entry[0], entry[1])).lower() == key:
                return entry
        return None

    def read_entry(self, entry):
        folder, name, size, offset = entry
        compressed = bool(self.archive_flags & FLAG_COMPRESSED) and not (size & SIZE_STORED)
        stored = size & ~SIZE_STORED
        with open(self.path, 'rb') as f:
            f.seek(offset)
            data = f.read(stored)
        if len(data) != stored:
            raise BsaError('%s: truncated entry %s\\%s' % (self.path, folder, name))
        if not compressed:
            return data
        if len(data) < 8:
            raise BsaError('%s: compressed entry too short: %s\\%s' % (self.path, folder, name))
        original = struct.unpack_from('<I', data, 0)[0]
        out = lz4.decompress_frame(data[4:])
        if len(out) != original:
            raise BsaError('%s: size mismatch for %s\\%s (declared %d, got %d)' %
                           (self.path, folder, name, original, len(out)))
        return out

    def read(self, wanted):
        entry = self.find(wanted)
        if entry is None:
            raise BsaError('%s: entry not found: %s' % (self.path, wanted))
        return self.read_entry(entry)
