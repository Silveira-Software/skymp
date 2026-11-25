"""Minimal LZ4 frame decoder (standard library only).

SSE BSA entries in the Meshes archives are stored as an LZ4 frame after a u32
uncompressed size, so the collision producer needs this to read a NIF.
"""
import struct

FRAME_MAGIC = 0x184D2204


class Lz4Error(RuntimeError):
    pass


def decompress_block(src, expected_size=None):
    out = bytearray()
    i = 0
    n = len(src)
    while i < n:
        token = src[i]
        i += 1
        literal_len = token >> 4
        if literal_len == 15:
            while True:
                if i >= n:
                    raise Lz4Error('truncated literal length')
                b = src[i]
                i += 1
                literal_len += b
                if b != 255:
                    break
        if i + literal_len > n:
            raise Lz4Error('literal run past end')
        out += src[i:i + literal_len]
        i += literal_len
        if i >= n:
            break
        if i + 2 > n:
            raise Lz4Error('truncated match offset')
        offset = src[i] | (src[i + 1] << 8)
        i += 2
        if offset == 0:
            raise Lz4Error('zero match offset')
        match_len = token & 0x0F
        if match_len == 15:
            while True:
                if i >= n:
                    raise Lz4Error('truncated match length')
                b = src[i]
                i += 1
                match_len += b
                if b != 255:
                    break
        match_len += 4
        start = len(out) - offset
        if start < 0:
            raise Lz4Error('match before start of output')
        for k in range(match_len):
            out.append(out[start + k])
    if expected_size is not None and len(out) != expected_size:
        raise Lz4Error('block size mismatch: %d != %d' % (len(out), expected_size))
    return bytes(out)


def decompress_frame(data):
    if len(data) < 7:
        raise Lz4Error('frame too short')
    magic = struct.unpack_from('<I', data, 0)[0]
    if magic != FRAME_MAGIC:
        raise Lz4Error('bad frame magic 0x%08x' % magic)
    o = 4
    flg = data[o]
    o += 1
    o += 1  # BD byte: block max size
    version = (flg >> 6) & 0x3
    if version != 1:
        raise Lz4Error('unsupported frame version %d' % version)
    block_checksum = bool(flg & 0x10)
    content_size = bool(flg & 0x08)
    content_checksum = bool(flg & 0x04)
    dict_id = bool(flg & 0x01)
    declared = None
    if content_size:
        declared = struct.unpack_from('<Q', data, o)[0]
        o += 8
    if dict_id:
        o += 4
    o += 1  # header checksum
    out = bytearray()
    while True:
        if o + 4 > len(data):
            raise Lz4Error('truncated block header')
        raw = struct.unpack_from('<I', data, o)[0]
        o += 4
        if raw == 0:
            break
        uncompressed = bool(raw & 0x80000000)
        size = raw & 0x7FFFFFFF
        if o + size > len(data):
            raise Lz4Error('truncated block data')
        block = data[o:o + size]
        o += size
        if block_checksum:
            o += 4
        out += block if uncompressed else decompress_block(block)
    if content_checksum:
        o += 4
    if declared is not None and len(out) != declared:
        raise Lz4Error('content size mismatch: %d != %d' % (len(out), declared))
    return bytes(out)
