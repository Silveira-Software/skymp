"""Pinned Skyrim NIF physical graph reader.

Layouts: niftools/nifxml, bhkRigidBodyCInfo2010 / NiAVObject.
Havok-to-Skyrim scale agrees with PyNifly V30.2.0 (69.99125).
Unsupported shapes, controllers, graph links or transforms refuse the model.
Convex planes include their radius as an OUTER envelope: safe LOS denial, never
an actor hit representation or a claim of exact rounded-corner parity.
"""
import math
import struct
import nif

SCALE = 69.99125
IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.)
NODE_TYPES = {'NiNode', 'BSFadeNode', 'NiSwitchNode', 'BSMultiBoundNode',
              'BSLeafAnimNode', 'BSOrderedNode', 'BSRangeNode'}


def point(m, p):
    return tuple(sum(m[r*4+c]*p[c] for c in range(3))+m[r*4+3] for r in range(3))


def compose(a, b):
    return tuple(sum(a[r*4+k]*b[k*4+c] for k in range(3)) +
                 (a[r*4+3] if c == 3 else 0) for r in range(3) for c in range(4))


def uniform_scale(m):
    columns = [[m[r*4+c] for r in range(3)] for c in range(3)]
    norms = [math.sqrt(sum(x*x for x in col)) for col in columns]
    if not all(math.isfinite(x) for x in m) or not 1e-6 < norms[0] < 1e4 or any(abs(n-norms[0]) > 1e-4 for n in norms):
        raise nif.NifError('nonuniform physical transform')
    if any(abs(sum(columns[i][k]*columns[j][k] for k in range(3))) > 1e-4*norms[0]**2 for i in range(3) for j in range(i)):
        raise nif.NifError('sheared physical transform')
    return norms[0]


def convex(block):
    c = nif.Cursor(block)
    c.u32()
    radius = c.f32()
    c.u32s(6)  # two bhkWorldObjCInfoProperty arrays
    n = c.u32()
    if not 4 <= n <= 4096:
        raise nif.NifError('convex vertex count')
    vertices = [c.f32s(4)[:3] for _ in range(n)]
    n = c.u32()
    if not 4 <= n <= 4096:
        raise nif.NifError('convex plane count')
    planes = [c.f32s(4) for _ in range(n)]
    if c.o != len(block) or not math.isfinite(radius) or not 0 <= radius <= 100:
        raise nif.NifError('convex payload/radius')
    normalized = []
    for p in planes:
        length = math.sqrt(sum(v*v for v in p[:3]))
        if not .99 < length < 1.01 or not all(math.isfinite(v) for v in p):
            raise nif.NifError('convex plane invalid')
        p = [v/length for v in p]
        # Some shipped normals are shrink/optimisation planes. GJK's support
        # shape is the vertices, so derive the outer support plane from those
        # vertices instead of treating the cached plane distance as a bound.
        support = max(sum(p[k]*v[k] for k in range(3)) for v in vertices)
        normalized.append([*p[:3], -support-radius-.002])
    return vertices, normalized, radius+.002


def read(data):
    doc = nif.Nif(data)
    if doc.bsver not in (83, 100):
        raise nif.NifError('physical graph requires Skyrim BS version 83 or 100')
    doc.has_movable_body = False
    blocks = [(name, data[o:o+n]) for name, o, n in doc.blocks]
    physical = [i for i, (name, _) in enumerate(blocks) if name.startswith('bhk')]
    if not physical:
        return [], [], 0., doc
    nodes, parents = {}, {}
    def node(i):
        if i in nodes:
            return nodes[i]
        name, raw = blocks[i]
        if name not in NODE_TYPES:
            raise nif.NifError('unsupported collision target node '+name)
        c = nif.Cursor(raw)
        c.u32(); extras = c.u32(); c.u32s(extras)
        controller = c.u32()
        if controller != 0xffffffff:
            if controller >= len(blocks) or blocks[controller][0] not in (
                    'NiTransformController', 'NiControllerManager', 'NiMultiTargetTransformController'):
                raise nif.NifError('unsupported physical node controller')
        c.u32()  # NiAVObject flags
        t, r, scale = c.f32s(3), c.f32s(9), c.f32()
        matrix = tuple(r[y*3+x]*scale if x < 3 else t[y] for y in range(3) for x in range(4))
        uniform_scale(matrix)
        collision = c.u32()
        children = c.u32s(c.u32())
        for child in children:
            if child != 0xffffffff:
                if child >= len(blocks) or child in parents:
                    raise nif.NifError('ambiguous NIF node parent')
                parents[child] = i
        nodes[i] = (matrix, collision, controller)
        return nodes[i]
    for i, (name, _) in enumerate(blocks):
        if name in NODE_TYPES:
            node(i)
    def global_matrix(i, seen=()):
        if i in seen:
            raise nif.NifError('cyclic NIF nodes')
        m, _, _ = node(i)
        if i in parents:
            return compose(global_matrix(parents[i], seen+(i,)), m)
        if i not in doc.roots:
            raise nif.NifError('unrooted physical target')
        return m
    animated = any(v[2] != 0xffffffff for v in nodes.values())
    animation_translation, animation_scale = 0., 0.
    if animated:
        key_data = [raw for name, raw in blocks if name == 'NiTransformData']
        if not key_data:
            raise nif.NifError('animated collision has no bounded transform keys')
        for raw in key_data:
            translation, scale = animation_bounds(raw)
            animation_translation = max(animation_translation, translation)
            animation_scale = max(animation_scale, scale)
        for name, raw in blocks:
            if name != 'NiTransformInterpolator':
                continue
            if len(raw) != 36:
                raise nif.NifError('transform interpolator payload')
            values = struct.unpack_from('<8f', raw)
            valid_t = [math.isfinite(v) and abs(v)<1e20 for v in values[:3]]
            if any(valid_t) and not all(valid_t):
                raise nif.NifError('partial translation interpolator defaults')
            if all(valid_t):
                animation_translation = max(animation_translation,math.sqrt(sum(v*v for v in values[:3])))
            if math.isfinite(values[7]) and 0 < values[7] < 1e20:
                animation_scale = max(animation_scale,values[7])
    triangles, volumes, used, margins = [], [], set(), []
    def block(i):
        if i < 0 or i >= len(blocks):
            raise nif.NifError('physical link out of range')
        used.add(i)
        return blocks[i]
    def shape(i, matrix, seen=()):
        if i in seen:
            raise nif.NifError('cyclic physical graph')
        name, raw = block(i)
        sf = uniform_scale(matrix)
        if name == 'bhkMoppBvTreeShape':
            return shape(struct.unpack_from('<I', raw)[0], matrix, seen+(i,))
        if name == 'bhkCompressedMeshShape':
            if len(raw) != 56 or tuple(struct.unpack_from('<3f', raw, 16)) != (1., 1., 1.):
                raise nif.NifError('unsupported compressed shape transform')
            data_id = struct.unpack_from('<I', raw, 52)[0]
            kind, payload = block(data_id)
            if kind != 'bhkCompressedMeshShapeData':
                raise nif.NifError('compressed shape missing data')
            mesh = nif.decode_compressed_mesh_shape_data(payload)
            for tri in mesh.triangles:
                triangles.append(tuple(point(matrix, tuple(v*SCALE for v in p)) for p in tri))
            margins.append(struct.unpack_from('<f', raw, 8)[0]*SCALE*sf)
        elif name in ('bhkConvexVerticesShape', 'bhkBoxShape', 'bhkSphereShape'):
            if name == 'bhkConvexVerticesShape':
                vertices, planes, bound_margin = convex(raw)
            elif name == 'bhkBoxShape':
                nif.decode_box_shape(raw)  # validates exact layout and dimensions
                radius = struct.unpack_from('<f', raw, 4)[0]
                dims = struct.unpack_from('<3f', raw, 16)
                planes = [[*(s if j == axis else 0. for j in range(3)), -abs(dims[axis])-radius] for axis in range(3) for s in (-1., 1.)]
                vertices = [[x, y, z] for x in (-dims[0], dims[0]) for y in (-dims[1], dims[1]) for z in (-dims[2], dims[2])]
                bound_margin = radius
            else:
                if len(raw) != 8:
                    raise nif.NifError('sphere payload')
                radius = struct.unpack_from('<f', raw, 4)[0]
                if not math.isfinite(radius) or not 0 < radius < 1e4:
                    raise nif.NifError('sphere radius')
                volumes.append({'kind':'sphere', 'center':point(matrix, (0,0,0)), 'radius':radius*SCALE*sf})
                return
            transformed = []
            for p in planes:
                normal = [sum(matrix[y*4+x]*p[x] for x in range(3))/sf for y in range(3)]
                transformed.append([*normal, p[3]*SCALE*sf-sum(normal[y]*matrix[y*4+3] for y in range(3))])
            volumes.append({'kind':'convex-envelope', 'planes':transformed,
                            'boundingMargin':bound_margin*SCALE*sf,
                            'vertices':[point(matrix, [x*SCALE for x in v]) for v in vertices]})
        elif name == 'bhkListShape':
            c = nif.Cursor(raw)
            children = c.u32s(c.u32())
            if not 1 <= len(children) <= 256:
                raise nif.NifError('invalid physical list')
            for child in children:
                shape(child, matrix, seen+(i,))
        elif name in ('bhkConvexTransformShape', 'bhkTransformShape'):
            if len(raw) != 84:
                raise nif.NifError('physical shape transform payload')
            values = struct.unpack_from('<16f', raw, 20)
            if any(abs(values[k]) > .0001 for k in (3,7,11)) or min(abs(values[15]),abs(values[15]-1)) > .0001:
                raise nif.NifError('physical shape projective transform')
            child_matrix = tuple(values[x*4+y] if x < 3 else values[12+y]*SCALE for y in range(3) for x in range(4))
            shape(struct.unpack_from('<I',raw)[0], compose(matrix,child_matrix), seen+(i,))
        else:
            raise nif.NifError('unsupported physical shape '+name)
    animated_radius = 0.
    for i, (name, raw) in enumerate(blocks):
        if name != 'bhkCollisionObject':
            continue
        if len(raw) != 10:
            raise nif.NifError('collision object payload')
        used.add(i)
        target, flags, body_id = struct.unpack('<IHI', raw)
        matrix = IDENTITY if animated else global_matrix(target)
        if node(target)[1] != i:
            raise nif.NifError('collision target backlink mismatch')
        name, body = block(body_id)
        if name not in ('bhkRigidBody', 'bhkRigidBodyT') or len(body) != 250:
            raise nif.NifError('unsupported physical body '+name)
        mass = struct.unpack_from('<f', body, 180)[0]
        if not math.isfinite(mass) or mass < 0:
            raise nif.NifError('invalid rigid body mass')
        # Fixed bodies and massless static-layer bodies cannot move under
        # local Havok forces. Other bodies need a verified runtime transform.
        doc.has_movable_body |= body[224] != 7 and (body[4] != 1 or mass != 0)
        if name == 'bhkRigidBodyT':
            t, q = struct.unpack_from('<4f', body, 52), struct.unpack_from('<4f', body, 68)
            if abs(sum(v*v for v in q)-1) > .001:
                raise nif.NifError('invalid rigid body quaternion')
            columns = [nif._quat_rotate(q, v) for v in [(1,0,0), (0,1,0), (0,0,1)]]
            rigid = tuple(columns[x][y] if x < 3 else t[y]*SCALE for y in range(3) for x in range(4))
            matrix = compose(matrix, rigid)
        start_t, start_v = len(triangles), len(volumes)
        shape(struct.unpack_from('<I', body)[0], matrix)
        if animated:
            radius = max([math.sqrt(sum(x*x for x in p)) for tri in triangles[start_t:] for p in tri]+[0.])
            for v in volumes[start_v:]:
                if v['kind'] == 'sphere':
                    radius = max(radius, math.sqrt(sum(x*x for x in v['center']))+v['radius'])
                else:
                    radius = max(radius, max(math.sqrt(sum(x*x for x in p)) for p in v['vertices'])+v['boundingMargin'])
            radius += max(margins,default=0.)
            visited = set()
            current = target
            while True:
                if current in visited:
                    raise nif.NifError('cyclic animated physical parent')
                visited.add(current)
                nm = node(current)[0]
                translation = math.sqrt(sum(nm[k]**2 for k in (3,7,11)))
                radius = max(translation,animation_translation)+max(uniform_scale(nm),animation_scale)*radius
                if current not in parents:
                    if current not in doc.roots:
                        raise nif.NifError('unrooted animated physical parent')
                    break
                current = parents[current]
            animated_radius = max(animated_radius,radius)
            del triangles[start_t:]
            del volumes[start_v:]
    if any(i not in used for i in physical):
        raise nif.NifError('unconsumed physical graph block')
    margin = max(margins, default=0.)
    if not math.isfinite(margin) or not 0 <= margin <= 1000:
        raise nif.NifError('invalid physical mesh radius')
    if animated:
        if not math.isfinite(animated_radius) or not 0 < animated_radius < 100000:
            raise nif.NifError('animated physical envelope invalid')
        return [], [{'kind':'sphere','center':(0,0,0),'radius':animated_radius,
                     'policy':'ALL_TRANSFORM_KEYS_OUTER_BOUND'}], 0., doc
    return triangles, volumes, margin, doc


def animation_bounds(raw):
    """Conservative bounds over SSE NiTransformData, never current client pose.

    Linear/quadratic translation keys are bounded by their values and Hermite
    tangents. Cubic's |h10|/|h11| <= 1, so summing tangent norms is conservative.
    TBC and spline controllers require a separate bound and are refused.
    Rotation never changes a sphere radius. Scale uses the same scalar bound.
    """
    c = nif.Cursor(raw)
    def keys(dimension, count=None, quaternion=False):
        count = c.u32() if count is None else count
        if count == 0:
            return 0.
        if count > 100000:
            raise nif.NifError('animation key count')
        kind = c.u32()
        if quaternion and kind == 4:
            for _ in range(3):
                keys(1)
            return 0.
        if kind not in (1,2):
            raise nif.NifError('unsupported animation key interpolation')
        bound, tangent_bound, first = 0., 0., None
        last = -math.inf
        for _ in range(count):
            time = c.f32()
            if not math.isfinite(time) or time < last:
                raise nif.NifError('animation key times')
            last = time
            if first is None:
                first = time
            values = c.f32s(dimension)
            tangents = [] if kind == 1 or quaternion else c.f32s(dimension*2)
            if not all(math.isfinite(v) for v in values+tangents):
                raise nif.NifError('nonfinite animation keys')
            size = math.sqrt(sum(v*v for v in values))
            if tangents:
                tangent_bound = max(tangent_bound,sum(math.sqrt(sum(v*v for v in tangents[i:i+dimension])) for i in (0,dimension)))
            bound = max(bound,size)
        return bound+tangent_bound*max(1.,last-first)
    keys(4,quaternion=True)
    translation = keys(3)
    scale = keys(1)
    if c.o != len(raw):
        raise nif.NifError('animation key payload')
    return translation,scale
