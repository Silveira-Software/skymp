"""Check new outer envelopes against already pinned controller assets."""
from pathlib import Path
import argparse
import hashlib
import json
import actor_controller


def probe(manifest_path, assets):
    manifest = json.loads(manifest_path.read_bytes())
    if manifest.get('schemaVersion') != 2 or not isinstance(manifest.get('actors'), list):
        raise ValueError('controller manifest v2 required')
    models = {}
    for actor in manifest['actors']:
        model = actor['model'].replace('\\', '/').lower()
        if model.startswith('/') or '..' in model.split('/') or ':' in model or '\0' in model:
            raise ValueError('unsafe controller asset path')
        digest = actor['sha256']
        if model in models and models[model] != digest:
            raise ValueError('conflicting controller asset hashes')
        models[model] = digest
    checks = []
    for model, digest in sorted(models.items()):
        data = (assets/model).read_bytes()
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError('controller asset hash mismatch ' + model)
        triangles, sources = actor_controller.read(data)
        if not triangles or not sources:
            raise ValueError('controller geometry missing ' + model)
        lo = [min(s['blockingBounds']['min'][k] for s in sources) for k in range(3)]
        hi = [max(s['blockingBounds']['max'][k] for s in sources) for k in range(3)]
        for triangle in triangles:
            for vertex in triangle:
                if any(not lo[k] <= vertex[k] <= hi[k] for k in range(3)):
                    raise ValueError('controller surface outside outer envelope ' + model)
        checks.append({'model':model, 'sha256':digest, 'controllerParts':len(sources),
                       'triangles':len(triangles), 'blockingBounds':{'min':lo,'max':hi}})
    return {'status':'PASS', 'scope':'PINNED_ACTOR_CONTROLLER_OUTER_ENVELOPES_ONLY',
            'manifestActors':len(manifest['actors']), 'uniqueModels':len(models), 'checks':checks,
            'manifestSha256':hashlib.sha256(manifest_path.read_bytes()).hexdigest(),
            'dynamicBodySimulation':False, 'realSkyrim':False}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    result = probe(args.manifest, args.assets)
    args.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps({k:v for k,v in result.items() if k!='checks'}))
