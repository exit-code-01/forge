#!/usr/bin/env python3
"""GLB inspector: validates a .glb and prints world bounds (node transforms applied),
joint counts, and animation clips.  Usage: glb_bounds.py [--scale S] file.glb ...
Stdlib only.  One line per file: stem | size | min | max | joints | clips | issues
"""
import json, struct, sys, os, math

def load(path):
    d = open(path, 'rb').read()
    if len(d) < 20 or d[:4] != b'glTF':
        raise ValueError('bad magic')
    ver, total = struct.unpack_from('<II', d, 4)
    if total > len(d):
        raise ValueError('truncated')
    off, js, binc = 12, None, b''
    while off + 8 <= total:
        n, t = struct.unpack_from('<II', d, off)
        body = d[off + 8: off + 8 + n]
        if t == 0x4E4F534A: js = json.loads(body.decode('utf8'))
        elif t == 0x004E4942: binc = body
        off += 8 + n
    if js is None: raise ValueError('no JSON chunk')
    return js, binc

def mul(a, b):  # column-major 4x4
    return [sum(a[k*4+r] * b[c*4+k] for k in range(4)) for c in range(4) for r in range(4)]

def local(n):
    if 'matrix' in n: return list(n['matrix'])
    t = n.get('translation', [0, 0, 0]); q = n.get('rotation', [0, 0, 0, 1]); s = n.get('scale', [1, 1, 1])
    x, y, z, w = q
    r = [1-2*(y*y+z*z), 2*(x*y+z*w), 2*(x*z-y*w), 0,
         2*(x*y-z*w), 1-2*(x*x+z*z), 2*(y*z+x*w), 0,
         2*(x*z+y*w), 2*(y*z-x*w), 1-2*(x*x+y*y), 0, 0, 0, 0, 1]
    for c in range(3):
        for k in range(3): r[c*4+k] *= s[c]
    r[12], r[13], r[14] = t
    return r

def prim_bounds(js, binc, p):
    """Local bounds of only the vertices a primitive's indices reference (accessors can be
    shared between primitives, so accessor min/max may overstate).  None if unsupported."""
    try:
        acc, bvs = js['accessors'], js['bufferViews']
        ia = acc[p['indices']]; iv = bvs[ia['bufferView']]
        fmt = {5121: 'B', 5123: 'H', 5125: 'I'}[ia['componentType']]
        sz = struct.calcsize(fmt); off = iv.get('byteOffset', 0) + ia.get('byteOffset', 0)
        used = set(struct.unpack_from('<' + fmt, binc, off + i * sz)[0] for i in range(ia['count']))
        pa = acc[p['attributes']['POSITION']]; pv = bvs[pa['bufferView']]
        po = pv.get('byteOffset', 0) + pa.get('byteOffset', 0); st = pv.get('byteStride', 12)
        pts = [struct.unpack_from('<3f', binc, po + i * st) for i in used]
        return [min(q[k] for q in pts) for k in range(3)], [max(q[k] for q in pts) for k in range(3)]
    except Exception:
        return None

def inspect(path):
    js, binc = load(path)
    issues = []
    for i, bv in enumerate(js.get('bufferViews', [])):
        if bv.get('byteOffset', 0) + bv['byteLength'] > len(binc):
            issues.append(f'bufferView {i} out of range')
    for i, im in enumerate(js.get('images', [])):
        if 'bufferView' not in im and not im.get('uri', '').startswith('data:'):
            if not os.path.exists(os.path.join(os.path.dirname(path), im.get('uri', ''))):
                issues.append(f'image {i} missing {im.get("uri")}')
    nodes = js.get('nodes', []); acc = js.get('accessors', [])
    lo = [math.inf]*3; hi = [-math.inf]*3
    def walk(i, parent):
        n = nodes[i]; m = mul(parent, local(n))
        if 'mesh' in n:
            for p in js['meshes'][n['mesh']]['primitives']:
                a = acc[p['attributes']['POSITION']]
                amin, amax = prim_bounds(js, binc, p) or (a['min'], a['max'])
                for cx in (0, 1):
                    for cy in (0, 1):
                        for cz in (0, 1):
                            v = [(amin, amax)[c][k] for k, c in enumerate((cx, cy, cz))]
                            for k in range(3):
                                w = m[k]*v[0] + m[4+k]*v[1] + m[8+k]*v[2] + m[12+k]
                                lo[k] = min(lo[k], w); hi[k] = max(hi[k], w)
        for c in n.get('children', []): walk(c, m)
    ident = [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]
    sc = js.get('scenes', [{}])[js.get('scene', 0)]
    for r in sc.get('nodes', []): walk(r, ident)
    joints = [len(s['joints']) for s in js.get('skins', [])]
    if any(j > 64 for j in joints): issues.append('>64 joints')
    clips = []
    for a in js.get('animations', []):
        dur = max((acc[s['input']].get('max', [0])[0] for s in a['samplers']), default=0)
        clips.append(f"{a.get('name','?')}={dur:.2f}s")
    return lo, hi, joints, clips, issues

if __name__ == '__main__':
    args = sys.argv[1:]; scale = 1.0
    if args and args[0] == '--scale': scale = float(args[1]); args = args[2:]
    for p in args:
        try:
            lo, hi, j, c, iss = inspect(p)
            f = lambda v: '(' + ','.join(f'{x*scale:.3f}' for x in v) + ')'
            print(f"{os.path.splitext(os.path.basename(p))[0]} | size {f([h-l for l,h in zip(lo,hi)])} | min {f(lo)} | max {f(hi)} | joints {j} | clips {','.join(c)} | {'; '.join(iss) or 'ok'}")
        except Exception as e:
            print(f'{p} | ERROR {e}')
