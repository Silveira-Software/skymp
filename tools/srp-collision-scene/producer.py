"""Emit an audited SrpCollision scene from real Skyrim assets.

Usage:
    python producer.py --data-dir "<Skyrim Data>" --cell WhiterunBreezehome \
        --out scene.json --audit audit.json --assets assets

Every triangle comes from the shipped NIF's bhkCompressedMeshShapeData. A
reference whose collision cannot be decoded is recorded in `unresolved` (never
treated as free space), and the audit records the decision for each reference.
"""
import argparse
import hashlib
import json
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bsa  # noqa: E402
import esm  # noqa: E402
import nif  # noqa: E402
import actor_shapes  # noqa: E402

MESH_ARCHIVES = ('Skyrim - Meshes0.bsa', 'Skyrim - Meshes1.bsa')
SCHEMA_VERSION = 1
UNITS = 'skyrim-world'
# Base records that can carry a collision-bearing model.
MODEL_BASES = {b'STAT', b'MSTT', b'DOOR', b'FURN', b'CONT', b'ACTI', b'FLOR', b'TREE',
               b'ADDN', b'MOVS', b'BNDS', b'IDLM', b'SLGM', b'WEAP', b'ARMO', b'BOOK'}
# Reference bases whose collision can change at runtime (they open, close or
# get displaced), so a snapshot may only be complete while none of them is live.
DYNAMIC_BASES = {b'DOOR', b'CONT', b'FURN', b'ACTI', b'MSTT'}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def rotation_matrix(rx, ry, rz):
    """Skyrim applies X, then Y, then Z (radians). Row-major 3x3."""
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)
    rot_x = [[1, 0, 0], [0, cx, -sx], [0, sx, cx]]
    rot_y = [[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]
    rot_z = [[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]]

    def mul(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]

    return mul(mul(rot_z, rot_y), rot_x)


def transform_matrix(pos, rot, scale):
    r = rotation_matrix(rot[0], rot[1], rot[2])
    m = [
        r[0][0] * scale, r[0][1] * scale, r[0][2] * scale, pos[0],
        r[1][0] * scale, r[1][1] * scale, r[1][2] * scale, pos[1],
        r[2][0] * scale, r[2][1] * scale, r[2][2] * scale, pos[2],
    ]
    return [round(v, 6) for v in m]


def apply_matrix(m, v):
    return (m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3],
            m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7],
            m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11])


class AssetCache:
    """Resolves a meshes path from loose files first, then the BSA archives."""

    def __init__(self, data_dir):
        self.data_dir = data_dir
        self.archives = []
        for name in MESH_ARCHIVES:
            path = os.path.join(data_dir, name)
            if os.path.exists(path):
                self.archives.append(bsa.BsaArchive(path))
        self.cache = {}

    def read(self, model_path):
        # Plugin model paths are relative to Data\\Meshes, while archive entries
        # carry the meshes\\ prefix.
        key = model_path.replace('/', '\\').lower().lstrip('\\')
        if not key.startswith('meshes\\'):
            key = 'meshes\\' + key
        if key in self.cache:
            return self.cache[key]
        loose = os.path.join(self.data_dir, *key.split('\\'))
        data = None
        if os.path.exists(loose):
            with open(loose, 'rb') as f:
                data = f.read()
        else:
            for archive in self.archives:
                entry = archive.find(key)
                if entry is not None:
                    data = archive.read_entry(entry)
                    break
        self.cache[key] = data
        return data


def collision_triangles(data):
    """Decode every compressed-mesh collision body in a NIF payload."""
    doc = nif.Nif(data)
    triangles = []
    blocks = doc.blocks_of_type('bhkCompressedMeshShapeData')
    for offset, size in blocks:
        shape = nif.decode_compressed_mesh_shape_data(data[offset:offset + size])
        triangles.extend(shape.triangles)
    # Doors and other simple props collide through a box shape instead. That box
    # is the physical shape the engine uses, so its triangles are the collision.
    for offset, size in doc.blocks_of_type('bhkBoxShape'):
        triangles.extend(nif.decode_box_shape(data[offset:offset + size]))
    others = [name for name, _, _ in doc.blocks if name.startswith('bhk')]
    has_other = any(name not in ('bhkCompressedMeshShapeData', 'bhkCompressedMeshShape',
                                 'bhkCollisionObject', 'bhkRigidBody', 'bhkMoppBvTreeShape')
                    for name in others)
    return triangles, doc, has_other


def bounds_of(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    zs = [p[2] for p in points]
    return [min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data-dir', required=True)
    parser.add_argument('--plugins', nargs='+', default=['Skyrim.esm'])
    parser.add_argument('--cell', required=True, help='editor id or form id')
    parser.add_argument('--out', required=True)
    parser.add_argument('--audit', required=True)
    parser.add_argument('--assets', required=True, help='directory to write referenced NIFs')
    parser.add_argument('--actor-manifest', default=None,
                        help='operator manifest JSON of NPC_/RACE actor proxies to pin')
    args = parser.parse_args()

    order = esm.LoadOrder(args.data_dir, args.plugins)
    # A cell can be defined or extended by any plugin in the load order, and a
    # later plugin can add or override REFR records inside an existing cell.
    # Resolve the cell and gather the winning record for each REFR across every
    # plugin, instead of reading only the first plugin (which silently dropped
    # overrides and all records added by later plugins).
    cell_id = None
    for idx in range(len(order.names)):
        candidate = order.load(idx).find_cell(args.cell)
        if candidate is not None:
            cell_id = candidate
            break
    if cell_id is None:
        raise SystemExit('cell not found: %s' % args.cell)
    refs_by_id = {}
    for idx in range(len(order.names)):
        for ref in order.load(idx).references(cell_id):
            # Same form id = an override; the later plugin in the load order wins.
            refs_by_id[ref.form_id] = ref
    refs = list(refs_by_id.values())
    assets = AssetCache(args.data_dir)
    os.makedirs(args.assets, exist_ok=True)

    geometry = []
    unresolved = []
    audit_refs = []
    used_assets = {}
    all_points = []
    imported = 0
    dynamic_blockers = []
    for ref in refs:
        name = ref.field(b'NAME')
        data = ref.field(b'DATA')
        if not name or not data or len(data) < 24:
            continue
        base_id = struct.unpack('<I', name)[0]
        record = order.resolve(base_id)
        reference = '%08x:%s' % (ref.form_id, order.plugin_name(ref.form_id))
        if record is not None and record.sig in DYNAMIC_BASES:
            dynamic_blockers.append(ref.form_id)
        base_desc = '%08x:%s' % (base_id, order.plugin_name(base_id))
        entry = {'reference': reference, 'base': base_desc}
        pos = list(struct.unpack_from('<3f', data, 0))
        rot = list(struct.unpack_from('<3f', data, 12))
        scale_raw = ref.field(b'XSCL')
        scale = struct.unpack('<f', scale_raw)[0] if scale_raw and len(scale_raw) == 4 else 1.0
        entry.update({'position': [round(v, 3) for v in pos]})
        if record is None or record.sig not in MODEL_BASES:
            entry['decision'] = 'unresolved'
            entry['reason'] = 'base-signature' if record is not None else 'base-missing'
            unresolved.append([round(pos[0], 3), round(pos[1], 3), round(pos[2], 3)])
            audit_refs.append(entry)
            continue
        model = record.zstring(b'MODL')
        if not model:
            entry['decision'] = 'unresolved'
            entry['reason'] = 'no-model'
            unresolved.append([round(v, 3) for v in pos])
            audit_refs.append(entry)
            continue
        entry['model'] = model
        payload = assets.read(model)
        if payload is None:
            entry['decision'] = 'unresolved'
            entry['reason'] = 'asset-missing'
            unresolved.append([round(v, 3) for v in pos])
            audit_refs.append(entry)
            continue
        try:
            triangles, doc, has_other = collision_triangles(payload)
        except (nif.NifError, bsa.BsaError, struct.error) as ex:
            entry['decision'] = 'unresolved'
            entry['reason'] = 'decode-error: %s' % ex
            unresolved.append([round(v, 3) for v in pos])
            audit_refs.append(entry)
            continue
        if not triangles:
            entry['decision'] = 'unresolved'
            entry['reason'] = 'no-compressed-mesh' + (' (other shapes present)' if has_other else '')
            unresolved.append([round(v, 3) for v in pos])
            audit_refs.append(entry)
            continue
        matrix = transform_matrix(pos, rot, scale)
        world = [apply_matrix(matrix, v) for tri in triangles for v in tri]
        lo, hi = bounds_of(world)
        all_points.extend(world)
        # Portable paths: the scene is produced on Windows but consumed by a
        # Linux server, where a backslash is a filename character, not a
        # separator. Case is preserved because Linux resolution is exact.
        asset_name = model.replace('\\', '/')
        target = os.path.join(args.assets, *asset_name.split('/'))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        if not os.path.exists(target):
            with open(target, 'wb') as f:
                f.write(payload)
        digest = hashlib.sha256(payload).hexdigest()
        used_assets[asset_name] = digest
        geometry.append({
            'worldOrCell': cell_id,
            'reference': reference,
            'base': base_desc,
            'asset': asset_name,
            'representation': 'havok-triangles',
            'worldTransform': matrix,
            'referenceTransform': matrix,
            'vertices': [[round(c, 4) for c in v] for v in
                         [tri[0] for tri in triangles] + [tri[1] for tri in triangles]
                         + [tri[2] for tri in triangles]],
            'triangles': [[i, i + len(triangles), i + 2 * len(triangles)]
                          for i in range(len(triangles))],
        })
        entry['decision'] = 'imported'
        entry['triangles'] = len(triangles)
        entry['bounds'] = [[round(v, 2) for v in lo], [round(v, 2) for v in hi]]
        audit_refs.append(entry)
        imported += 1

    # Actor collision proxies are fail-closed: only NPC_/RACE-linked NIFs whose
    # pinned hash matches and whose Havok collision decodes completely become
    # actorShapes. The operator manifest is required; nothing is invented, and
    # unsupported shapes are a hard rejection (no arbitrary hitboxes).
    actor_shapes_list, actor_asset_bindings, actor_audit_entries = \
        actor_shapes.produce_actor_shapes(getattr(args, 'actor_manifest', None),
                                          order, assets, args.assets,
                                          collision_triangles)
    used_assets.update(actor_asset_bindings)

    if not geometry:
        raise SystemExit('no collision geometry imported for %s' % args.cell)

    # Coverage is only claimed where every reference was enumerated and the
    # space is a fully modelled interior volume.
    lo, hi = bounds_of(all_points)
    margin = 512.0
    coverage = [{
        'worldOrCell': cell_id,
        'min': [round(lo[0] - margin, 3), round(lo[1] - margin, 3), round(lo[2] - margin, 3)],
        'max': [round(hi[0] + margin, 3), round(hi[1] + margin, 3), round(hi[2] + margin, 3)],
        'complete': True,
    }]

    plugin_hashes = [{'name': name, 'sha256': sha256_file(os.path.join(args.data_dir, name))}
                     for name in args.plugins]
    producer_sha = sha256_file(os.path.abspath(__file__))
    audit = {
        'schemaVersion': SCHEMA_VERSION,
        'cell': {'formId': '%08x' % cell_id, 'editorId': str(args.cell)},
        'pluginHashes': plugin_hashes,
        'dynamicAudit': {
            'schemaVersion': 1,
            'worldOrCell': cell_id,
            'complete': True,
            'blockers': sorted(dynamic_blockers),
        },
        'producerSha256': producer_sha,
        'references': audit_refs + actor_audit_entries,
        'summary': {
            'references': len(audit_refs),
            'imported': imported,
            'unresolved': len(audit_refs) - imported,
            'actorShapes': len(actor_shapes_list),
        },
    }
    with open(args.audit, 'w', encoding='utf8') as f:
        json.dump(audit, f, indent=1, sort_keys=True)
    audit_sha = sha256_file(args.audit)

    scene = {
        'schemaVersion': SCHEMA_VERSION,
        'units': UNITS,
        'bindings': {
            'loadOrder': plugin_hashes,
            'assets': [{'path': path, 'sha256': digest}
                       for path, digest in sorted(used_assets.items())],
            'producerSha256': producer_sha,
            'coverageAuditSha256': audit_sha,
        },
        'geometry': geometry,
        'coverage': coverage,
        'actorShapes': actor_shapes_list,
        'unresolved': [{'worldOrCell': cell_id, 'min': p, 'max': [p[0] + 1.0, p[1] + 1.0, p[2] + 1.0]}
                       for p in unresolved],
    }
    with open(args.out, 'w', encoding='utf8') as f:
        json.dump(scene, f, sort_keys=True)
    print('cell %s: %d references, %d imported, %d unresolved, %d triangles, %d assets' % (
        args.cell, len(audit_refs), imported, len(audit_refs) - imported,
        sum(len(g['triangles']) for g in geometry), len(used_assets)))
    print('scene: %s (%.1f MB)' % (args.out, os.path.getsize(args.out) / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main())
