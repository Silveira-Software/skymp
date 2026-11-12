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
import havok  # noqa: E402

MESH_ARCHIVES = ('Skyrim - Meshes0.bsa', 'Skyrim - Meshes1.bsa')
SCHEMA_VERSION = 1
UNITS = 'skyrim-world'
# Base records that can carry a collision-bearing model.
MODEL_BASES = {b'STAT', b'MSTT', b'DOOR', b'FURN', b'CONT', b'ACTI', b'FLOR', b'TREE',
               b'ADDN', b'MOVS', b'BNDS', b'IDLM', b'SLGM', b'WEAP', b'ARMO', b'BOOK',
               b'MISC', b'KEYM', b'INGR', b'ALCH', b'AMMO', b'LIGH'}
# Reference bases whose collision can change at runtime (they open, close or
# get displaced), so a snapshot may only be complete while none of them is live.
DYNAMIC_BASES = {b'DOOR', b'CONT', b'FURN', b'ACTI', b'MSTT', b'MISC', b'KEYM', b'INGR',
                 b'ALCH', b'AMMO', b'WEAP', b'ARMO', b'BOOK', b'SLGM'}


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

    def __init__(self, data_dir, plugins=(), loose_overrides=()):
        self.loose_roots = [*loose_overrides, data_dir]
        self.archives = []
        # Later active plugins' matching archives win over base archives.
        names = [os.path.splitext(plugin)[0] + '.bsa' for plugin in reversed(plugins)]
        for name in dict.fromkeys([*names, *MESH_ARCHIVES]):
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
        loose = next((path for root in self.loose_roots
                      if os.path.isfile(path := os.path.join(root, *key.split('\\')))), None)
        data = None
        if loose is not None:
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
    """Actor proxies require exact triangles, never outer LOS envelopes."""
    triangles, volumes, margin, doc = havok.read(data)
    return triangles, doc, bool(volumes) or margin > 0


def bounds_of(points):
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    zs = [p[2] for p in points]
    return [min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)]


def unresolved_bounds(record, matrix):
    """OBND may deny a trace through unknown geometry, never prove a hit."""
    raw = record.field(b'OBND') if record is not None else None
    if raw is None or len(raw) != 12:
        return None
    values = struct.unpack('<6h', raw)
    if any(values[index] >= values[index + 3] for index in range(3)):
        return None
    corners = [apply_matrix(matrix, [x, y, z])
               for x in (values[0], values[3])
               for y in (values[1], values[4])
               for z in (values[2], values[5])]
    return bounds_of(corners)


def require_interior_cell(order, cell_id):
    record = order.resolve(cell_id)
    data = record.field(b'DATA') if record and record.sig == b'CELL' else None
    if not data or record.flags & 0x20 or not data[0] & 1:
        raise ValueError('collision coverage requires a winning interior CELL')


def reference_transform_fields(ref):
    name, data = ref.field(b'NAME'), ref.field(b'DATA')
    scale_raw = ref.field(b'XSCL')
    if (name is None or len(name) != 4 or name == b'\0' * 4 or
            data is None or len(data) != 24 or
            (scale_raw is not None and len(scale_raw) != 4)):
        return None
    position, rotation = struct.unpack_from('<3f', data), struct.unpack_from('<3f', data, 12)
    scale = struct.unpack('<f', scale_raw)[0] if scale_raw is not None else 1.0
    if not all(map(math.isfinite, (*position, *rotation, scale))) or scale <= 0:
        return None
    return struct.unpack('<I', name)[0], position, rotation, scale


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--data-dir', required=True)
    parser.add_argument('--asset-data-dir', help='read-only asset tree if plugins are staged separately')
    parser.add_argument('--loose-override', action='append', default=[])
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
            cell_id = order.to_global(idx, candidate)
            break
    if cell_id is None:
        raise SystemExit('cell not found: %s' % args.cell)
    require_interior_cell(order, cell_id)
    refs = order.cell_references(cell_id)
    assets = AssetCache(args.asset_data_dir or args.data_dir, args.plugins, args.loose_override)
    os.makedirs(args.assets, exist_ok=True)

    geometry = []
    audit_refs = []
    used_assets = {}
    all_points = []
    imported = 0
    dynamic_blockers = []
    untracked_physics = []
    for reference_id, source_index, ref in refs:
        reference = '%x:%s' % (reference_id & 0xFFFFFF, order.plugin_name(reference_id))
        fields = reference_transform_fields(ref)
        if fields is None:
            audit_refs.append({'reference': reference, 'decision': 'unresolved',
                               'reason': 'invalid-reference-fields'})
            continue
        local_base, pos, rot, scale = fields
        base_id = order.to_global(source_index, local_base)
        record = order.resolve(base_id)
        if record is not None and record.sig in DYNAMIC_BASES:
            dynamic_blockers.append(reference_id)
        base_desc = '%x:%s' % (base_id & 0xFFFFFF, order.plugin_name(base_id))
        entry = {'reference': reference, 'base': base_desc}
        entry.update({'position': [round(v, 3) for v in pos]})
        matrix = transform_matrix(pos, rot, scale)
        if record is not None and (record.sig in {b'SOUN', b'ASPC', b'TXST'} or
                                   record.sig in {b'STAT', b'IDLM', b'LIGH', b'ACTI'} and not record.zstring(b'MODL')):
            entry['decision'] = 'non-colliding-record'
            entry['reason'] = 'sound-decal-or-model-free-record'
            audit_refs.append(entry)
            continue
        if record is None or record.sig not in MODEL_BASES:
            entry['decision'] = 'unresolved'
            entry['reason'] = 'base-signature' if record is not None else 'base-missing'
            audit_refs.append(entry)
            continue
        model = record.zstring(b'MODL')
        if not model:
            entry['decision'] = 'unresolved'
            entry['reason'] = 'no-model'
            audit_refs.append(entry)
            continue
        entry['model'] = model
        payload = assets.read(model)
        if payload is None:
            entry['decision'] = 'unresolved'
            entry['reason'] = 'asset-missing'
            audit_refs.append(entry)
            continue
        try:
            triangles, volumes, margin, doc = havok.read(payload)
        except (nif.NifError, bsa.BsaError, struct.error) as ex:
            entry['decision'] = 'unresolved'
            entry['reason'] = 'decode-error: %s' % ex
            audit_refs.append(entry)
            continue
        if not triangles and not volumes:
            if not any(name.startswith('bhk') for name, _, _ in doc.blocks):
                entry['decision'] = 'non-colliding-model'
                entry['reason'] = 'parsed-nif-has-no-havok-body'
                audit_refs.append(entry)
                continue
            entry['decision'] = 'unresolved'
            entry['reason'] = 'no-physical-shape'
            audit_refs.append(entry)
            continue
        if doc.has_movable_body:
            untracked_physics.append(reference_id)
        world = [apply_matrix(matrix, v) for tri in triangles for v in tri]
        for volume in volumes:
            if volume['kind'] == 'convex-envelope':
                world.extend(apply_matrix(matrix, v) for v in volume['vertices'])
            else:
                center, radius = volume['center'], volume['radius']
                world.extend(apply_matrix(matrix, [center[k]+s*radius for k in range(3)]) for s in (-1,1))
        lo, hi = bounds_of(world)
        all_points.extend(world)
        # Portable paths: the scene is produced on Windows but consumed by a
        # Linux server, where a backslash is a filename character, not a
        # separator. Skyrim resolves models without case sensitivity, whereas
        # a Windows-created directory cannot preserve two case-only aliases.
        # Use one canonical path in both the scene and the copied asset tree.
        asset_name = model.replace('\\', '/').lower()
        target = os.path.join(args.assets, *asset_name.split('/'))
        os.makedirs(os.path.dirname(target), exist_ok=True)
        if not os.path.exists(target):
            with open(target, 'wb') as f:
                f.write(payload)
        else:
            with open(target, 'rb') as f:
                if f.read() != payload:
                    raise ValueError('collision output conflicts with pinned asset: '+asset_name)
        digest = hashlib.sha256(payload).hexdigest()
        used_assets[asset_name] = digest
        geometry.append({
            'worldOrCell': cell_id,
            'reference': reference,
            'base': base_desc,
            'asset': asset_name,
            'representation': 'havok-physical-v2',
            'worldTransform': matrix,
            'referenceTransform': matrix,
            'vertices': [[round(c, 4) for c in v] for v in
                         [tri[0] for tri in triangles] + [tri[1] for tri in triangles]
                         + [tri[2] for tri in triangles]],
            'triangles': [[i, i + len(triangles), i + 2 * len(triangles)]
                          for i in range(len(triangles))],
            'volumes': volumes, 'margin': margin,
        })
        entry['decision'] = 'imported'
        entry['triangles'] = len(triangles)
        entry['volumes'] = len(volumes)
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

    # An undecoded body is not a point at the reference origin. OBND is useful
    # diagnostic metadata, but does not prove the Havok body is contained in it.
    # Until every physical shape is decoded, the cell cannot prove clear LOS.
    unknown_volumes = []
    ref_sources = {('%x:%s' % (reference_id & 0xFFFFFF, order.plugin_name(reference_id))):
                   (source_index, ref) for reference_id, source_index, ref in refs}
    for entry in audit_refs:
        if entry.get('decision') != 'unresolved':
            continue
        if 'position' in entry:
            source_index, ref = ref_sources[entry['reference']]
            local_base, pos, rot, scale = reference_transform_fields(ref)
            base = order.resolve(order.to_global(source_index, local_base))
            matrix = transform_matrix(pos, rot, scale)
            source_bounds = unresolved_bounds(base, matrix)
            if source_bounds is not None:
                entry['sourceObjectBounds'] = source_bounds
        bounds = (coverage[0]['min'], coverage[0]['max'])
        entry['unknownScope'] = 'ENTIRE_CELL_UNDECODED_PHYSICAL_SHAPE'
        entry['unknownBounds'] = bounds
        unknown_volumes.append({'worldOrCell': cell_id, 'min': bounds[0], 'max': bounds[1]})

    plugin_hashes = [{'name': name, 'sha256': sha256_file(os.path.join(args.data_dir, name))}
                     for name in args.plugins]
    producer_sha = sha256_file(os.path.abspath(__file__))
    producer_sources = [{'path':name,'sha256':sha256_file(os.path.join(os.path.dirname(__file__),name))}
                        for name in ('producer.py','havok.py','nif.py','esm.py','bsa.py','lz4.py','actor_shapes.py','actor_controller.py')]
    audit = {
        'schemaVersion': SCHEMA_VERSION,
        'cell': {'formId': '%08x' % cell_id, 'editorId': str(args.cell)},
        'pluginHashes': plugin_hashes,
        'dynamicAudit': {
            'schemaVersion': 1,
            'worldOrCell': cell_id,
            'complete': True,
            'blockers': sorted(dynamic_blockers),
            'untrackedPhysics': sorted(untracked_physics),
        },
        'producerSha256': producer_sha,
        'producerSources': producer_sources,
        'references': audit_refs + actor_audit_entries,
        'summary': {
            'references': len(audit_refs),
            'imported': imported,
            'unresolved': len(unknown_volumes),
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
            'producerSources': producer_sources,
            'coverageAuditSha256': audit_sha,
        },
        'geometry': geometry,
        'coverage': coverage,
        'actorShapes': actor_shapes_list,
        'unresolved': unknown_volumes,
    }
    with open(args.out, 'w', encoding='utf8') as f:
        json.dump(scene, f, sort_keys=True)
    print('cell %s: %d references, %d imported, %d unresolved, %d triangles, %d assets' % (
        args.cell, len(audit_refs), imported, len(unknown_volumes),
        sum(len(g['triangles']) for g in geometry), len(used_assets)))
    print('scene: %s (%.1f MB)' % (args.out, os.path.getsize(args.out) / 1e6))
    return 0


if __name__ == '__main__':
    sys.exit(main())
