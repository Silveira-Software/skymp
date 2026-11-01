"""NIF 20.2.0.7 (Skyrim SE) reader limited to what collision extraction needs.

Block sizes are validated by requiring that all blocks tile the payload
exactly; a file whose sizes do not tile is rejected rather than guessed at,
because a wrong offset would silently produce wrong geometry.
"""
import struct

HEADER_SUFFIX = '20.2.0.7'
TYPE_INDEX_MASK = 0x7FFF


class NifError(RuntimeError):
    pass


def _u8(d, o):
    return d[o], o + 1


def _u16(d, o):
    return struct.unpack_from('<H', d, o)[0], o + 2


def _u32(d, o):
    return struct.unpack_from('<I', d, o)[0], o + 4


def _sized_string(d, o):
    n, o = _u32(d, o)
    if o + n > len(d):
        raise NifError('string past end')
    return d[o:o + n].decode('latin-1'), o + n


def _export_string(d, o):
    n, o = _u8(d, o)
    if n == 0:
        return '', o
    if o + n > len(d):
        raise NifError('export string past end')
    return d[o:o + n - 1].decode('latin-1'), o + n


class Nif:
    def __init__(self, data):
        self.types = []
        self.blocks = []  # (type_name, offset, size)
        self._parse(data)

    def _parse(self, d):
        newline = d.find(b'\n')
        if newline < 0 or newline > 80:
            raise NifError('missing NIF header line')
        line = d[:newline].decode('latin-1')
        if not line.startswith('Gamebryo File Format, Version '):
            raise NifError('not a NIF: %r' % line[:40])
        if not line.endswith(HEADER_SUFFIX):
            raise NifError('unsupported NIF version: %r' % line)
        o = newline + 1
        version, o = _u32(d, o)
        if version != 0x14020007:
            raise NifError('unexpected version word 0x%08x' % version)
        endian, o = _u8(d, o)
        if endian != 1:
            raise NifError('big-endian NIF is not supported')
        user, o = _u32(d, o)
        num_blocks, o = _u32(d, o)
        if not (0 < num_blocks <= 65535):
            raise NifError('implausible block count %d' % num_blocks)
        # Bethesda stream header: bsver then three u8-prefixed export strings.
        self.bsver, o = _u32(d, o)
        self.author, o = _export_string(d, o)
        self.process_script, o = _export_string(d, o)
        self.export_script, o = _export_string(d, o)
        num_types, o = _u16(d, o)
        if not (0 < num_types <= 4096):
            raise NifError('implausible block type count %d' % num_types)
        for _ in range(num_types):
            name, o = _sized_string(d, o)
            self.types.append(name)
        indices = []
        for _ in range(num_blocks):
            idx, o = _u16(d, o)
            indices.append(idx & TYPE_INDEX_MASK)
        sizes = []
        for _ in range(num_blocks):
            size, o = _u32(d, o)
            sizes.append(size)
        num_strings, o = _u32(d, o)
        max_string_length, o = _u32(d, o)
        for _ in range(num_strings):
            _, o = _sized_string(d, o)
        num_groups, o = _u32(d, o)
        for _ in range(num_groups):
            _, o = _u32(d, o)
        data_start = o
        blocks_end = data_start + sum(sizes)
        # A trailing footer lists the root blocks (normally one). Requiring the
        # blocks plus that footer to fill the payload exactly is what makes the
        # size table trustworthy for random access.
        if blocks_end + 4 > len(d):
            raise NifError('block sizes exceed the payload')
        num_roots = struct.unpack_from('<I', d, blocks_end)[0]
        if num_roots > num_blocks or blocks_end + 4 + 4 * num_roots != len(d):
            raise NifError('block sizes do not tile the payload (%d + %d + %d != %d)' %
                           (data_start, sum(sizes), 4 + 4 * num_roots, len(d)))
        self.num_roots = num_roots
        offset = data_start
        for i, size in enumerate(sizes):
            if indices[i] >= len(self.types):
                raise NifError('block %d has out-of-range type index %d' % (i, indices[i]))
            self.blocks.append((self.types[indices[i]], offset, size))
            offset += size

    def blocks_of_type(self, wanted):
        return [(off, size) for name, off, size in self.blocks if name == wanted]

    def read_block(self, data, off, size):
        return data[off:off + size]


# bhkCompressedMeshShapeData -------------------------------------------------

class Cursor:
    def __init__(self, data):
        self.d = data
        self.o = 0

    def u8(self):
        v = self.d[self.o]
        self.o += 1
        return v

    def u16(self):
        v = struct.unpack_from('<H', self.d, self.o)[0]
        self.o += 2
        return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.o)[0]
        self.o += 4
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.d, self.o)[0]
        self.o += 4
        return v

    def f32s(self, n):
        v = list(struct.unpack_from('<%df' % n, self.d, self.o))
        self.o += 4 * n
        return v

    def u16s(self, n):
        v = list(struct.unpack_from('<%dH' % n, self.d, self.o))
        self.o += 2 * n
        return v

    def u32s(self, n):
        v = list(struct.unpack_from('<%dI' % n, self.d, self.o))
        self.o += 4 * n
        return v


def _quat_rotate(q, v):
    x, y, z, w = q
    # Standard quaternion rotation, matching Havok's xyzw order.
    tx = 2.0 * (y * v[2] - z * v[1])
    ty = 2.0 * (z * v[0] - x * v[2])
    tz = 2.0 * (x * v[1] - y * v[0])
    return (v[0] + w * tx + (y * tz - z * ty),
            v[1] + w * ty + (z * tx - x * tz),
            v[2] + w * tz + (x * ty - y * tx))


class CompressedMeshShape:
    """Decoded bhkCompressedMeshShapeData: object-space triangles."""

    def __init__(self, triangles, bounds, chunk_count, big_verts, small_verts):
        self.triangles = triangles
        self.bounds = bounds
        self.chunk_count = chunk_count
        self.big_verts = big_verts
        self.small_verts = small_verts


def decode_compressed_mesh_shape_data(block):
    c = Cursor(block)
    bits_per_index = c.u32()
    bits_per_windex = c.u32()
    mask_windex = c.u32()
    mask_index = c.u32()
    error = c.f32()
    bounds_min = c.f32s(4)
    bounds_max = c.f32s(4)
    welding_type = c.u8()
    material_type = c.u8()
    if bits_per_index > 32 or bits_per_windex > 32:
        raise NifError('implausible CMS bit widths')
    # Same three link arrays for every Skyrim mesh; validated by consuming the
    # whole block at the end.
    n32 = c.u32()
    c.u32s(n32)
    n16 = c.u32()
    c.u32s(n16)
    n8 = c.u32()
    c.u32s(n8)
    num_materials = c.u32()
    for _ in range(num_materials):
        c.u32(); c.u8(); c.u8(); c.u16()
    c.u32()  # number of named materials (no array)
    num_transforms = c.u32()
    transforms = []
    for _ in range(num_transforms):
        translation = c.f32s(4)
        quaternion = c.f32s(4)
        transforms.append((translation, quaternion))
    num_big_verts = c.u32()
    big_verts = [tuple(c.f32s(4)[:3]) for _ in range(num_big_verts)]
    num_big_tris = c.u32()
    big_tris = []
    for _ in range(num_big_tris):
        a, b, cc = c.u16(), c.u16(), c.u16()
        c.u32()  # material
        c.u16()  # welding info
        big_tris.append((a, b, cc))
    num_chunks = c.u32()
    chunks = []
    for _ in range(num_chunks):
        translation = c.f32s(4)
        material = c.u32()
        reference = c.u16()
        transform_index = c.u16()
        count = c.u32()
        if count % 3:
            raise NifError('chunk vertex component count is not a multiple of 3')
        comps = c.u16s(count)
        num_indices = c.u32()
        indices = c.u16s(num_indices)
        num_strips = c.u32()
        strips = c.u16s(num_strips)
        num_welding = c.u32()
        c.u16s(num_welding)
        chunks.append((translation, material, reference, transform_index,
                       comps, indices, strips))
    num_convex = c.u32()
    if c.o != len(block):
        raise NifError('CMS data did not consume its block (%d of %d)' % (c.o, len(block)))
    if num_convex:
        raise NifError('unexpected convex piece array')
    triangles = []
    for a, b, cc in big_tris:
        if max(a, b, cc) >= len(big_verts):
            raise NifError('big triangle index out of range')
        triangles.append((big_verts[a], big_verts[b], big_verts[cc]))
    for translation, material, reference, transform_index, comps, indices, strips in chunks:
        verts = []
        for i in range(0, len(comps), 3):
            x = comps[i] / 1000.0 + translation[0]
            y = comps[i + 1] / 1000.0 + translation[1]
            z = comps[i + 2] / 1000.0 + translation[2]
            if 0 <= transform_index < len(transforms):
                t, q = transforms[transform_index]
                x += t[0]; y += t[1]; z += t[2]
                x, y, z = _quat_rotate(q, (x, y, z))
            verts.append((x, y, z))
        if not verts:
            continue
        # Each strip of length L yields L-2 triangles; leftover indices after the
        # strips are an explicit triangle list.
        pos = 0
        for length in strips:
            if length < 3:
                pos += length
                continue
            for k in range(length - 2):
                a, b, cc = indices[pos + k], indices[pos + k + 1], indices[pos + k + 2]
                if k % 2:
                    b, cc = cc, b
                if max(a, b, cc) >= len(verts):
                    raise NifError('chunk triangle index out of range')
                triangles.append((verts[a], verts[b], verts[cc]))
            pos += length
        rest = len(indices) - pos
        if rest > 0:
            if rest % 3:
                raise NifError('trailing chunk indices are not a triangle list')
            for k in range(0, rest, 3):
                a, b, cc = indices[pos + k], indices[pos + k + 1], indices[pos + k + 2]
                if max(a, b, cc) >= len(verts):
                    raise NifError('chunk triangle index out of range')
                triangles.append((verts[a], verts[b], verts[cc]))
    return CompressedMeshShape(triangles, (tuple(bounds_min[:3]), tuple(bounds_max[:3])),
                               num_chunks, len(big_verts), sum(
                                   len(ch[4]) // 3 for ch in chunks))
