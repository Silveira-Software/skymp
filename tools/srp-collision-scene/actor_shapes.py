"""Fail-closed actor collision proxies from real, pinned Skyrim NIF Havok data.

An operator manifest is required: a visual mesh is NOT assumed to describe
actor physics. Only a model linked to a verified NPC_/RACE MODL record and
containing completely decoded Havok triangle shapes is accepted. This does
not establish engine pose parity, ragdoll parity, or exterior terrain coverage.
"""
import hashlib
import json
import math
import os
import re
import struct
import actor_controller

DESC = re.compile(r'^([0-9a-fA-F]{1,6}):([^:/\\]+\.(?:esm|esp|esl))$', re.I)
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]
PLAYER_TRANSFORMATION_RACES = {
    'cdd84:skyrim.esm': 'WerewolfBeastRace',
    '283a:dawnguard.esm': 'DLC1VampireBeastRace',
}


def _record(order, descriptor, signature):
    match = DESC.fullmatch(descriptor)
    if not match:
        raise ValueError('actor proxy requires local FormDesc hex:plugin: ' + descriptor)
    local = int(match.group(1), 16)
    names = [name.lower() for name in order.names]
    name = match.group(2).lower()
    if names.count(name) != 1:
        raise ValueError('actor proxy plugin not in unique load order: ' + descriptor)
    source_index, record = order.resolve_with_source((names.index(name) << 24) | local)
    if record is None or record.sig != signature:
        raise ValueError('actor proxy record absent or wrong type: ' + descriptor)
    plugin = order.load(source_index)
    return plugin, record


def _traits(order, descriptor):
    """Only deterministic NPC_ trait templates; leveled choices stay unknown."""
    visited = set()
    while descriptor.lower() not in visited:
        visited.add(descriptor.lower())
        plugin, actor = _record(order, descriptor, b'NPC_')
        acbs = actor.field(b'ACBS')
        if acbs is None or len(acbs) < 24:
            raise ValueError('actor proxy ACBS incomplete')
        template = actor.field(b'TPLT')
        if template is not None and len(template) != 4:
            raise ValueError('actor proxy trait template invalid')
        if not struct.unpack_from('<H', acbs, 18)[0] & 1 or not template or not int.from_bytes(template, 'little'):
            return plugin, actor, bool(struct.unpack_from('<I', acbs)[0] & 1)
        if len(template) != 4 or len(visited) > 32:
            raise ValueError('actor proxy trait template invalid')
        value = int.from_bytes(template, 'little')
        references = list(plugin.masters) + [plugin.name]
        if value >> 24 >= len(references):
            raise ValueError('actor proxy trait template master invalid')
        descriptor = f'{value & 0xffffff:x}:{references[value >> 24]}'
    raise ValueError('actor proxy cyclic trait template')


def _skeleton(race, female):
    # xEdit RACE: MNAM/ANAM male, FNAM/ANAM female, before NAM2.
    sex, models = None, {}
    for tag, raw in race.subrecords():
        if tag == b'NAM2':
            break
        if tag in (b'MNAM', b'FNAM'):
            sex = tag == b'FNAM'
        elif tag == b'ANAM':
            if sex is None or sex in models or not raw.endswith(b'\0') or b'\0' in raw[:-1]:
                raise ValueError('actor proxy ambiguous skeletal model')
            models[sex] = raw[:-1].decode('latin-1')
    if female not in models:
        raise ValueError('actor proxy skeletal model for sex missing')
    return models[female]


def produce_actor_shapes(manifest_path, order, assets, assets_root, collision_triangles):
    """Return actorShapes, pinned asset bindings and audit entries.

    Asset SHA is pinned by the manifest and every triangle comes from decoded
    Havok data. Unsupported supplemental Havok shapes cause a hard rejection.
    """
    if not manifest_path:
        return [], {}, []
    with open(manifest_path, encoding='utf-8') as stream:
        spec = json.load(stream)
    if not isinstance(spec, dict) or spec.get('schemaVersion') not in (1, 2) or not isinstance(spec.get('actors'), list):
        raise ValueError('actor proxy manifest schema invalid')
    controllers = spec['schemaVersion'] == 2
    if len(spec['actors']) > 512:
        raise ValueError('actor proxy manifest limit exceeded')
    shapes, used, audit, seen = [], {}, [], set()
    for row in spec['actors']:
        keys = {'baseDesc', 'raceDesc', 'model', 'sha256'} | ({'sex'} if controllers else set())
        if not isinstance(row, dict) or set(row) != keys:
            raise ValueError('actor proxy entry must specify baseDesc/raceDesc/model/sha256')
        base, race = row['baseDesc'], row['raceDesc']
        if not isinstance(base, str) or not isinstance(race, str):
            raise ValueError('actor proxy descriptors must be strings')
        if controllers and (type(row['sex']) is not int or row['sex'] not in (0, 1)):
            raise ValueError('actor proxy sex invalid')
        key = (base.lower(), race.lower(), row.get('sex'))
        if key in seen:
            raise ValueError('actor proxy duplicate base/race: ' + base)
        seen.add(key)
        base_plugin, actor, female = _traits(order, base)
        _, race_record = _record(order, race, b'RACE')
        race_field = actor.field(b'RNAM')
        if race_field is None or len(race_field) != 4:
            raise ValueError('actor proxy NPC_ has no explicit RNAM: ' + base)
        # RNAM indexes this plugin's masters, not the server's runtime order.
        race_idx = int.from_bytes(race_field, 'little') >> 24
        race_local = int.from_bytes(race_field, 'little') & 0xFFFFFF
        references = list(base_plugin.masters) + [base_plugin.name]
        if race_idx >= len(references):
            raise ValueError('actor proxy NPC_ race index invalid: ' + base)
        declared_race = f'{race_local:x}:{references[race_idx]}'.lower()
        player = base.lower() == '7:skyrim.esm'
        if declared_race != race.lower() and not (controllers and player):
            raise ValueError('actor proxy race mismatches NPC_.RNAM: ' + base)
        if controllers:
            race_data = race_record.field(b'DATA')
            if race_data is None or len(race_data) < 36:
                raise ValueError('actor proxy race DATA incomplete')
            if player:
                playable = bool(struct.unpack_from('<I', race_data, 32)[0] & 1)
                transformation = PLAYER_TRANSFORMATION_RACES.get(race.lower())
                if not playable and (not transformation or race_record.zstring(b'EDID') != transformation):
                    raise ValueError('actor proxy player race not playable')
                female = bool(row['sex'])
            elif female != bool(row['sex']):
                raise ValueError('actor proxy sex mismatches NPC_ traits')
        model = row['model']
        if not isinstance(model, str) or not model.strip():
            raise ValueError('actor proxy model invalid')
        canonical = model.replace('\\', '/').lstrip('/')
        if (canonical != model.replace('\\', '/') or '..' in canonical.split('/') or
                ':' in canonical or '\x00' in canonical or not canonical.lower().endswith('.nif')):
            raise ValueError('actor proxy model path unsafe')
        known_models = [actor.zstring(b'MODL'), race_record.zstring(b'MODL')]
        if controllers:
            known_models = [_skeleton(race_record, female)]
        known_models = [m.replace('\\', '/').lower() for m in known_models if m]
        if canonical.lower() not in known_models:
            raise ValueError('actor proxy NIF is not linked to NPC_/RACE MODL: ' + base)
        expected = row['sha256']
        if not isinstance(expected, str) or re.fullmatch('[0-9a-fA-F]{64}', expected) is None:
            raise ValueError('actor proxy SHA-256 invalid')
        payload = assets.read(model)
        if payload is None or hashlib.sha256(payload).hexdigest() != expected.lower():
            raise ValueError('actor proxy pinned NIF missing or hash mismatch: ' + model)
        if controllers:
            triangles, controller_sources = actor_controller.read(payload)
            height = actor.field(b'NAM6')
            if height is None or len(height) != 4:
                raise ValueError('actor proxy NPC_ height missing')
            scale = struct.unpack('<f', height)[0] * struct.unpack_from('<f', race_data, 20 if female else 16)[0]
            if not math.isfinite(scale) or not .01 < scale < 100:
                raise ValueError('actor proxy height invalid')
            unsupported = False
        else:
            triangles, _, unsupported = collision_triangles(payload)
        if unsupported or not triangles or len(triangles) > 100000:
            raise ValueError('actor proxy unsupported/incomplete Havok collision: ' + model)
        coords = [p for tri in triangles for p in tri]
        if any(len(p) != 3 or not all(math.isfinite(v) and abs(v) < 1000000 for v in p) for p in coords):
            raise ValueError('actor proxy invalid geometry coordinates')
        for a, b, c in triangles:
            ab = [b[k] - a[k] for k in range(3)]
            ac = [c[k] - a[k] for k in range(3)]
            cross = [ab[1]*ac[2]-ab[2]*ac[1], ab[2]*ac[0]-ab[0]*ac[2], ab[0]*ac[1]-ab[1]*ac[0]]
            if sum(n*n for n in cross) < 1e-16:
                raise ValueError('actor proxy degenerate triangle: ' + model)
        canonical = canonical.lower()
        output = os.path.join(assets_root, *canonical.split('/'))
        os.makedirs(os.path.dirname(output), exist_ok=True)
        if os.path.isfile(output):
            with open(output, 'rb') as existing:
                if existing.read() != payload:
                    raise ValueError('actor proxy output asset conflicts with pinned NIF')
        else:
            with open(output, 'wb') as dest:
                dest.write(payload)
        count = len(triangles)
        shape = {'baseDesc': base, 'raceDesc': race,
                       'policy': 'SERVER_MESH_PROXY_V1', 'representation': 'havok-triangles',
                       'asset': canonical, 'localTransform': IDENTITY,
                       'vertices': [[round(value, 5) for value in point] for point in coords],
                       'triangles': [[3*i, 3*i + 1, 3*i + 2] for i in range(count)]}
        if controllers:
            shape.update(sex=int(female), representation='havok-controller-inscribed-v1',
                         localTransform=[scale,0,0,0,0,scale,0,0,0,0,scale,0],
                         vertices=[list(point) for point in coords],
                         blockingBounds={
                             'min':[min(s['blockingBounds']['min'][k] for s in controller_sources) for k in range(3)],
                             'max':[max(s['blockingBounds']['max'][k] for s in controller_sources) for k in range(3)]})
        shapes.append(shape)
        used[canonical] = expected.lower()
        entry = {'baseDesc': base, 'raceDesc': race, 'asset': canonical,
                      'sha256': expected.lower(), 'triangles': count,
                      'enginePoseParity': False, 'dynamicActorCollisionCertified': False}
        if controllers:
            entry.update(sex=int(female), controllerSources=controller_sources,
                         heightScale=scale, representation=shape['representation'])
        audit.append(entry)
    return shapes, used, audit
