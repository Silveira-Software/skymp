"""Read pinned Skyrim character-controller shapes, not animated ragdolls.

Niftools nif.xml: bhkSPCollisionObject -> bhkSimpleShapePhantom -> bhkCapsuleShape.
The resulting *inscribed* surface is an explicit server hit-proxy policy.
It is not an engine impact report or a proof of the current animation pose.
Ragdoll bodies and constraints are deliberately outside this representation.
"""
import math
import struct
import havok
import nif


def convex_mesh(raw):
    # bhkConvexVerticesShape's support vertices define the convex hull. Its
    # cached planes are not all exact hull planes, so never triangulate them
    # as though they were surfaces (or enlarge positive hits by their radius).
    vertices, _, _ = havok.convex(raw)
    points=list(dict.fromkeys(tuple(v) for v in vertices))
    if len(points)<4 or any(not math.isfinite(x) or abs(x)>1e5 for p in points for x in p):
        raise nif.NifError('controller convex hull has fewer than four vertices')
    sub=lambda a,b:tuple(a[k]-b[k] for k in range(3))
    dot=lambda a,b:sum(a[k]*b[k] for k in range(3))
    cross=lambda a,b:(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
    a=min(range(len(points)),key=lambda i:points[i])
    b=max(range(len(points)),key=lambda i:dot(sub(points[i],points[a]),sub(points[i],points[a])))
    ab=sub(points[b],points[a])
    c=max(range(len(points)),key=lambda i:dot(cross(ab,sub(points[i],points[a])),cross(ab,sub(points[i],points[a]))))
    normal=cross(ab,sub(points[c],points[a]))
    d=max(range(len(points)),key=lambda i:abs(dot(normal,sub(points[i],points[a]))))
    extent=max(math.sqrt(dot(sub(p,points[a]),sub(p,points[a]))) for p in points)
    epsilon=max(1e-10,extent*1e-9)
    if abs(dot(normal,sub(points[d],points[a])))<=epsilon**3 or len({a,b,c,d})!=4:
        raise nif.NifError('controller convex hull degenerate')
    center=tuple(sum(points[i][k] for i in (a,b,c,d))/4 for k in range(3))
    def oriented(face):
        i,j,k=face
        if dot(cross(sub(points[j],points[i]),sub(points[k],points[i])),sub(center,points[i]))>0:
            return i,k,j
        return face
    faces=[oriented(t) for t in ((a,b,c),(a,d,b),(b,d,c),(c,d,a))]
    for i,p in enumerate(points):
        if i in (a,b,c,d):continue
        visible=[]
        for face in faces:
            x,y,z=(points[j] for j in face)
            n=cross(sub(y,x),sub(z,x))
            if dot(n,sub(p,x))>epsilon*math.sqrt(dot(n,n)):
                visible.append(face)
        if not visible:continue
        edges={}
        for x,y,z in visible:
            for edge in ((x,y),(y,z),(z,x)):
                key=tuple(sorted(edge))
                if key in edges:del edges[key]
                else:edges[key]=edge
        if not edges:
            raise nif.NifError('controller convex hull horizon absent')
        faces=[f for f in faces if f not in visible]
        faces.extend(oriented((x,y,i)) for x,y in edges.values())
        if len(faces)>16384:
            raise nif.NifError('controller convex hull face limit')
    # Every emitted face must separate all source vertices and form a closed
    # hull. Reject numeric/topological uncertainty instead of approving it.
    edges={}
    for face in faces:
        x,y,z=(points[i] for i in face)
        n=cross(sub(y,x),sub(z,x));length=math.sqrt(dot(n,n))
        if length<epsilon**2 or any(dot(n,sub(p,x))>epsilon*length*4 for p in points):
            raise nif.NifError('controller convex hull verification')
        for edge in ((face[0],face[1]),(face[1],face[2]),(face[2],face[0])):
            key=tuple(sorted(edge));edges[key]=edges.get(key,0)+1
    if any(n!=2 for n in edges.values()):
        raise nif.NifError('controller convex hull not closed')
    def inward(p):
        return tuple((center[k]+(p[k]-center[k])*(1.-1e-7))*havok.SCALE for k in range(3))
    return [tuple(inward(points[i]) for i in face) for face in faces]


def capsule(raw):
    if len(raw) != 48:
        raise nif.NifError('controller capsule payload')
    radius = struct.unpack_from('<f', raw, 4)[0]
    a = struct.unpack_from('<3f', raw, 16)
    r1 = struct.unpack_from('<f', raw, 28)[0]
    b = struct.unpack_from('<3f', raw, 32)
    r2 = struct.unpack_from('<f', raw, 44)[0]
    if (not all(math.isfinite(x) for x in (*a, *b, radius, r1, r2)) or
            not 0 < radius < 100 or
            abs(r1 - radius) > 1e-6 or abs(r2 - radius) > 1e-6):
        raise nif.NifError('controller capsule radii/coordinates')
    return a, b, radius


def inscribed_capsule(a, b, radius, sides=24, hemispheres=6):
    """Every vertex and face lies inside the convex physical capsule.

    Using an outer collision envelope here could authorize a false hit.
    No rounding is applied to these vertices before serialization.
    """
    delta = tuple(b[k] - a[k] for k in range(3))
    length = math.sqrt(sum(v*v for v in delta))
    axis = tuple(v/length for v in delta) if length > 1e-9 else (0., 0., 1.)
    seed = (1., 0., 0.) if abs(axis[0]) < .8 else (0., 1., 0.)
    u = (axis[1]*seed[2]-axis[2]*seed[1],
         axis[2]*seed[0]-axis[0]*seed[2], axis[0]*seed[1]-axis[1]*seed[0])
    norm = math.sqrt(sum(x*x for x in u))
    u = tuple(x/norm for x in u)
    v = (axis[1]*u[2]-axis[2]*u[1],
         axis[2]*u[0]-axis[0]*u[2], axis[0]*u[1]-axis[1]*u[0])
    # A tiny inward offset also bounds IEEE arithmetic in a positive hit test.
    radius *= 1. - 1e-7
    rings = []
    for half, (center, lo, hi) in enumerate(((a, -math.pi/2, 0.), (b, 0., math.pi/2))):
        for j in range(hemispheres + 1):
            t = lo + (hi-lo)*j/hemispheres
            if half == 1 and length <= 1e-9 and j == 0:
                continue
            ring = [tuple(center[k] + radius * (
                axis[k]*math.sin(t) + math.cos(t)*(
                    u[k]*math.cos(2*math.pi*i/sides) +
                    v[k]*math.sin(2*math.pi*i/sides))) for k in range(3))
                for i in range(sides)]
            rings.append(ring)
    triangles = []
    for lower, upper in zip(rings, rings[1:]):
        for i in range(sides):
            j = (i+1) % sides
            for tri in ((lower[i], upper[i], upper[j]),
                        (lower[i], upper[j], lower[j])):
                e, f = [[tri[n][k]-tri[0][k] for k in range(3)] for n in (1,2)]
                cross = (e[1]*f[2]-e[2]*f[1], e[2]*f[0]-e[0]*f[2], e[0]*f[1]-e[1]*f[0])
                if sum(x*x for x in cross) > 1e-16:
                    triangles.append(tri)
    return triangles


def read(data):
    doc = nif.Nif(data)
    if doc.bsver not in (83, 100):
        raise nif.NifError('controller requires Skyrim BS version')
    blocks = [(name, data[o:o+n]) for name, o, n in doc.blocks]
    nodes, parents = {}, {}
    for i, (name, raw) in enumerate(blocks):
        if name not in havok.NODE_TYPES:
            continue
        c = nif.Cursor(raw)
        c.u32(); c.u32s(c.u32())
        controller = c.u32()
        c.u32()
        t, r, scale = c.f32s(3), c.f32s(9), c.f32()
        matrix = tuple(r[y*3+x]*scale if x < 3 else t[y]
                       for y in range(3) for x in range(4))
        havok.uniform_scale(matrix)
        collision = c.u32()
        for child in c.u32s(c.u32()):
            if child == 0xffffffff:
                continue
            if child >= len(blocks) or child in parents:
                raise nif.NifError('ambiguous controller node parent')
            parents[child] = i
        nodes[i] = (matrix, collision, controller)

    def pose(i, visited=()):
        if i in visited or i not in nodes:
            raise nif.NifError('cyclic/missing controller node')
        matrix, _, controller = nodes[i]
        if controller != 0xffffffff:
            raise nif.NifError('animated controller node requires runtime pose')
        if i in parents:
            return havok.compose(pose(parents[i], visited+(i,)), matrix)
        if i not in doc.roots:
            raise nif.NifError('unrooted controller node')
        return matrix

    triangles, sources = [], []
    def read_shape(i, visited=()):
        if i in visited or len(visited)>32 or not 0<=i<len(blocks):
            raise nif.NifError('controller shape link/cycle')
        kind, raw = blocks[i]
        if kind == 'bhkCapsuleShape':
            a,b,radius=capsule(raw)
            bounds=([min(a[k],b[k])-radius for k in range(3)],
                    [max(a[k],b[k])+radius for k in range(3)])
            return inscribed_capsule(tuple(x*havok.SCALE for x in a),
                tuple(x*havok.SCALE for x in b),radius*havok.SCALE),[i],[(
                    tuple(x*havok.SCALE for x in bounds[0]),
                    tuple(x*havok.SCALE for x in bounds[1]))]
        if kind == 'bhkConvexVerticesShape':
            vertices,_,margin=havok.convex(raw)
            return convex_mesh(raw),[i],[(
                tuple((min(p[k] for p in vertices)-margin)*havok.SCALE for k in range(3)),
                tuple((max(p[k] for p in vertices)+margin)*havok.SCALE for k in range(3)))]
        if kind == 'bhkMoppBvTreeShape':
            # MOPP is an acceleration tree; the linked child defines geometry.
            if len(raw)<41:
                raise nif.NifError('controller MOPP payload')
            count=struct.unpack_from('<I',raw,20)[0]
            if (len(raw)!=41+count or
                    struct.unpack_from('<f',raw,16)[0]!=1. or raw[40] not in (0,1,2)):
                raise nif.NifError('controller MOPP bytecode length')
            return read_shape(struct.unpack_from('<I',raw)[0],visited+(i,))
        if kind == 'bhkListShape':
            if len(raw)<36:
                raise nif.NifError('controller list payload')
            count=struct.unpack_from('<I',raw)[0]
            if not 1<=count<=256 or len(raw)!=36+count*8:
                raise nif.NifError('controller list child count')
            child_filters=struct.unpack_from('<I',raw,32+count*4)[0]
            if child_filters!=count:
                raise nif.NifError('controller list filter count')
            filters=struct.unpack_from('<%dI'%count,raw,36+count*4)
            if any(value not in (0,30) for value in filters):
                raise nif.NifError('controller child filter unsupported')
            mesh,ids,bounds=[],[i],[]
            for child in struct.unpack_from('<%dI'%count,raw,4):
                part,proof,boxes=read_shape(child,visited+(i,))
                mesh.extend(part);ids.extend(proof);bounds.extend(boxes)
            return mesh,ids,bounds
        raise nif.NifError('controller shape unsupported: '+kind)

    for i, (name, raw) in enumerate(blocks):
        if name != 'bhkSPCollisionObject':
            continue
        if len(raw) != 10:
            raise nif.NifError('controller collision object payload')
        target, flags, body_id = struct.unpack('<IHI', raw)
        if target not in nodes or nodes[target][1] != i or not flags & 1:
            raise nif.NifError('controller target/backlink/flags')
        if body_id >= len(blocks):
            raise nif.NifError('controller body link')
        kind, body = blocks[body_id]
        if kind != 'bhkSimpleShapePhantom' or len(body) != 100 or body[4] != 30:
            raise nif.NifError('controller body/filter unsupported')
        shape_id = struct.unpack_from('<I', body)[0]
        mesh,shape_blocks,shape_bounds=read_shape(shape_id)
        values = struct.unpack_from('<16f', body, 36)
        if (not all(math.isfinite(x) for x in values) or
                any(abs(values[k]) > 1e-6 for k in (3,7,11)) or
                min(abs(values[15]), abs(values[15]-1)) > 1e-6):
            raise nif.NifError('controller phantom projective transform')
        phantom = tuple(values[x*4+y] if x < 3 else values[12+y]*havok.SCALE
                        for y in range(3) for x in range(4))
        # Skyrim skeletons duplicate the controller's bind transform in the
        # phantom and its target NiNode. Composing both doubles the offset.
        # Accept only independent agreement; a mismatched/runtime-relative
        # phantom needs a separate engine pose producer and stays unsupported.
        matrix = pose(target)
        if any(abs(matrix[k]-phantom[k]) > (.02 if k in (3,7,11) else 1e-4)
               for k in range(12)):
            raise nif.NifError('controller bind/phantom transform mismatch')
        havok.uniform_scale(matrix)
        triangles.extend(tuple(havok.point(matrix, p) for p in tri) for tri in mesh)
        # The positive hit surface is inscribed. A separate OUTER envelope is
        # needed to certify that a third actor cannot obstruct the segment.
        # Transform all corners before taking extrema; rotating only min/max
        # would lose coverage. nextafter rounds outward, never shrinks a body.
        corners=[havok.point(matrix,(x,y,z)) for lo,hi in shape_bounds
                 for x in (lo[0],hi[0]) for y in (lo[1],hi[1]) for z in (lo[2],hi[2])]
        blocking_bounds={'min':[math.nextafter(min(p[k] for p in corners),-math.inf) for k in range(3)],
                         'max':[math.nextafter(max(p[k] for p in corners),math.inf) for k in range(3)]}
        sources.append({'collisionBlock': i, 'targetNode': target,
                        'phantomBlock': body_id, 'shapeBlock': shape_id,
                        'decodedShapeBlocks': shape_blocks,
                        'filterLayer': 30, 'triangles': len(mesh),
                        'blockingBounds':blocking_bounds})
    if not sources:
        raise nif.NifError('character controller capsule absent')
    return triangles, sources
