"""Verify the collision asset readers against real Skyrim SE data.

Run with the meshes archive available:

    python tools/srp-collision-scene/selfcheck.py [--bsa PATH]

Exits 0 when every check passes, 2 when the game data is not available (so the
check can be skipped on machines without Skyrim installed), 1 on a real failure.
Nothing here is synthetic: every triangle that is decoded comes from a shipped
Bethesda mesh.
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bsa  # noqa: E402
import nif  # noqa: E402

DEFAULT_BSA = r'D:\STEAM-DEPOTS\app_489830\depot_489831\Data\Skyrim - Meshes0.bsa'

# Real meshes with a compressed-mesh collision body. Sizes are deliberately not
# pinned; the point is that decoding is self-consistent and geometric.
SAMPLE_MESHES = [
    'wrwallshelf01.nif',
    'wrwallwatergrate01.nif',
    'wrwallruinsfree01entance01.nif',
    'wrintcastlepiece01.nif',
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bsa', default=os.environ.get('SRP_MESHES_BSA', DEFAULT_BSA))
    parser.add_argument('--samples', type=int, default=40,
                        help='additional arbitrary meshes to parse for robustness')
    args = parser.parse_args()

    if not os.path.exists(args.bsa):
        print('SKIP: meshes archive not found at %s' % args.bsa)
        return 2

    archive = bsa.BsaArchive(args.bsa)
    entries = archive.entries()
    nifs = [e for e in entries if e[1].lower().endswith('.nif')]
    print('archive: %d files, %d nif, flags=0x%x' %
          (len(entries), len(nifs), archive.archive_flags))
    if len(nifs) < 1000:
        raise SystemExit('unexpectedly few nif entries')

    by_name = {}
    for entry in nifs:
        by_name.setdefault(entry[1].lower(), entry)

    decoded = 0
    for name in SAMPLE_MESHES:
        entry = by_name.get(name)
        if entry is None:
            print('SKIP sample %s (not in this archive)' % name)
            continue
        data = archive.read_entry(entry)
        if not data.startswith(b'Gamebryo File Format'):
            raise SystemExit('%s: decompressed payload is not a NIF' % name)
        doc = nif.Nif(data)
        blocks = doc.blocks_of_type('bhkCompressedMeshShapeData')
        if not blocks:
            raise SystemExit('%s: no compressed-mesh collision' % name)
        triangles = []
        for offset, size in blocks:
            shape = nif.decode_compressed_mesh_shape_data(data[offset:offset + size])
            triangles.extend(shape.triangles)
        if not triangles:
            raise SystemExit('%s: decoded zero triangles' % name)
        xs = [v[0] for t in triangles for v in t]
        ys = [v[1] for t in triangles for v in t]
        zs = [v[2] for t in triangles for v in t]
        for value in xs + ys + zs:
            if value != value or abs(value) > 1e6:
                raise SystemExit('%s: non-finite or absurd coordinate' % name)
        print('  %-34s tris=%5d x=[%.1f,%.1f] y=[%.1f,%.1f] z=[%.1f,%.1f]' %
              (name, len(triangles), min(xs), max(xs), min(ys), max(ys), min(zs), max(zs)))
        decoded += 1

    parsed = failed = 0
    for entry in nifs[::max(1, len(nifs) // args.samples)][:args.samples]:
        try:
            nif.Nif(archive.read_entry(entry))
            parsed += 1
        except nif.NifError as ex:
            failed += 1
            print('  parse failure %-40s %s' % (entry[1][:38], ex))
    print('arbitrary meshes parsed: %d ok, %d rejected' % (parsed, failed))
    if failed > args.samples // 4:
        raise SystemExit('too many meshes rejected; the block-size check may be wrong')
    if decoded < 2:
        raise SystemExit('fewer than two collision meshes could be decoded')
    print('PASS: %d collision meshes decoded from real data' % decoded)
    return 0


if __name__ == '__main__':
    sys.exit(main())
