import zlib, struct, math, os

def png(path, w, h, pix):
    raw = b''.join(b'\x00' + bytes(pix[y]) for y in range(h))
    def chunk(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    hdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', hdr) + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    open(path, 'wb').write(data)

def make_tex(path, base, pattern):
    w = h = 16
    pix = []
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b = base
            if pattern == 'checker' and ((x // 4 + y // 4) % 2 == 0):
                r, g, b = min(255, r + 40), min(255, g + 40), min(255, b + 40)
            if pattern == 'grid' and (x % 4 == 0 or y % 4 == 0):
                r, g, b = r // 2, g // 2, b // 2
            row += [r, g, b, 255]
        pix.append(row)
    png(path, w, h, pix)

d = '/home/user/luanti/games/bench/mods/bench/textures/'
os.makedirs(d, exist_ok=True)
make_tex(d + 'bench_node.png', (180, 90, 60), 'checker')
make_tex(d + 'bench_node2.png', (80, 150, 90), 'grid')
make_tex(d + 'bench_model.png', (200, 200, 210), 'grid')

def write_obj(path, verts, faces):
    with open(path, 'w') as f:
        for v in verts:
            f.write("v %.6f %.6f %.6f\n" % v)
        for v in verts:
            # crude spherical-ish normal, normalized position
            l = math.sqrt(sum(c*c for c in v)) or 1.0
            f.write("vn %.6f %.6f %.6f\n" % (v[0]/l, v[1]/l, v[2]/l))
        for v in verts:
            f.write("vt %.6f %.6f\n" % (v[0]*0.5+0.5, v[1]*0.5+0.5))
        for (a, b, c) in faces:
            # OBJ indices are 1-based
            a, b, c = a + 1, b + 1, c + 1
            f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (a,a,a,b,b,b,c,c,c))

def icosphere(subdiv, scale=0.5):
    t = (1.0 + 5.0 ** 0.5) / 2.0
    verts = [(-1,t,0),(1,t,0),(-1,-t,0),(1,-t,0),(0,-1,t),(0,1,t),(0,-1,-t),(0,1,-t),(t,0,-1),(t,0,1),(-t,0,-1),(-t,0,1)]
    verts = [tuple(x / math.sqrt(3) * scale for x in v) for v in verts]
    faces = [(0,11,5),(0,5,1),(0,1,7),(0,7,10),(0,10,11),(1,5,9),(5,11,4),(11,10,2),(10,7,6),(7,1,8),
             (3,9,4),(3,4,2),(3,2,6),(3,6,8),(3,8,9),(4,9,5),(2,4,11),(6,2,10),(8,6,7),(9,8,1)]
    for _ in range(subdiv):
        cache = {}
        nf = []
        def mid(a, b):
            k = (min(a,b), max(a,b))
            if k in cache: return cache[k]
            va, vb = verts[a], verts[b]
            m = tuple((va[i]+vb[i])/2 for i in range(3))
            l = math.sqrt(sum(c*c for c in m)) or 1
            m = tuple(c/l*scale for c in m)
            verts.append(m)
            cache[k] = len(verts)-1
            return cache[k]
        for (a,b,c) in faces:
            ab, bc, ca = mid(a,b), mid(b,c), mid(c,a)
            nf += [(a,ab,ca),(b,bc,ab),(c,ca,bc),(ab,bc,ca)]
        faces = nf
    return verts, faces

def uvsphere(nu, nv, scale=0.5):
    verts = []
    for i in range(1, nv):
        phi = math.pi * i / nv
        for j in range(nu):
            th = 2*math.pi*j/nu
            verts.append((math.sin(phi)*math.cos(th)*scale, math.cos(phi)*scale, math.sin(phi)*math.sin(th)*scale))
    faces = []
    for i in range(nv-2):
        for j in range(nu):
            a = i*nu + j; b = i*nu + (j+1)%nu; c = (i+1)*nu + j; dd = (i+1)*nu + (j+1)%nu
            faces.append((a+1, c+1, b+1)); faces.append((b+1, c+1, dd+1))
    return verts, faces

m = '/home/user/luanti/games/bench/mods/bench/models/'
os.makedirs(m, exist_ok=True)
v, f = icosphere(1)          # 80 tris
write_obj(m + 'bench_low.obj', v, f)
v, f = icosphere(3)          # 1280 tris
write_obj(m + 'bench_mid.obj', v, f)
v, f = icosphere(4)          # 5120 tris
write_obj(m + 'bench_hi.obj', v, f)
for n in ['bench_low','bench_mid','bench_hi']:
    f2 = m + n + '.obj'
    lines = open(f2).read().count('\nf ')
    print(n, lines, 'tris', os.path.getsize(f2))
