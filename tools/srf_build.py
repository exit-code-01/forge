#!/usr/bin/env python3
"""Build the 'Surface' (srf_*) model set from extracted Kenney packs (CC0).

Usage: srf_build.py <packs_dir> <out_dir>
  packs_dir holds nature-kit/, space-kit/, survival-kit/ (each as extracted from the
  Kenney zip).  For each piece this copies the source GLB to <out_dir>/srf_<name>.glb,
  with these edits (source files are never modified):
    - wrap the scene in a root node that moves the pivot (base-centre, or 'nat' = keep
      XZ and only lift the kit's -0.05 ground offset to y=0, so grid tiles still tile)
    - optionally drop primitives by material name (bare dead tree)
    - recolour flat-colour materials (Kenney's Nature/Space colour factors are tinted
      teal/orange; mapped here to an overgrown/derelict palette)
    - embed external images into the GLB (self-contained)
Prints the [stem] scale lines for assets/models/import.ini and a manifest table.
Stdlib only; reads untrusted archive content as data.
"""
import sys, os, glob, json, struct, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import glb_bounds as g

PAL = {  # material name -> RGB factor
    'leafsDark': (0.14, 0.30, 0.18), 'leafsGreen': (0.28, 0.42, 0.18),
    'woodBark': (0.36, 0.26, 0.18), 'woodBarkDark': (0.30, 0.21, 0.15),
    'wood': (0.50, 0.38, 0.26), 'woodDark': (0.35, 0.26, 0.18), 'woodInner': (0.70, 0.58, 0.42),
    'stone': (0.50, 0.52, 0.50), 'stoneDark': (0.38, 0.40, 0.40),
    'dirt': (0.36, 0.28, 0.20), 'dirtDark': (0.28, 0.21, 0.15), 'grass': (0.30, 0.42, 0.20),
    'metal': (0.66, 0.68, 0.66), 'metalDark': (0.45, 0.47, 0.46), 'metalRed': (0.55, 0.30, 0.18),
    'dark': (0.20, 0.22, 0.22),
}
# (stem suffix, kit, source, pivot, scale, drop-materials)
P = [
 ('tree_pine_a', 'nature-kit', 'tree_pineTallA', 'base', 4.0, ()),
 ('tree_pine_b', 'nature-kit', 'tree_pineTallD', 'base', 3.5, ()),
 ('tree_oak', 'nature-kit', 'tree_default', 'base', 4.0, ()),
 ('tree_tall', 'nature-kit', 'tree_tall', 'base', 4.0, ()),
 ('tree_dead', 'nature-kit', 'tree_thin', 'base', 6.0, ('leafsGreen',)),
 ('bush_a', 'nature-kit', 'plant_bush', 'base', 4.0, ()),
 ('bush_large', 'nature-kit', 'plant_bushLarge', 'base', 4.5, ()),
 ('bush_detailed', 'nature-kit', 'plant_bushDetailed', 'base', 3.5, ()),
 ('plant_flat', 'nature-kit', 'plant_flatTall', 'base', 3.0, ()),
 ('grass_tuft', 'nature-kit', 'grass_large', 'base', 3.0, ()),
 ('grass_leafs', 'nature-kit', 'grass_leafs', 'base', 3.0, ()),
 ('rock_large_a', 'nature-kit', 'rock_largeA', 'base', 3.0, ()),
 ('rock_large_c', 'nature-kit', 'rock_largeC', 'base', 3.0, ()),
 ('rock_tall_b', 'nature-kit', 'rock_tallB', 'base', 3.0, ()),
 ('stone_large_b', 'nature-kit', 'stone_largeB', 'base', 3.0, ()),
 ('stone_tall_a', 'nature-kit', 'stone_tallA', 'base', 3.0, ()),
 ('stone_small_c', 'nature-kit', 'stone_smallC', 'base', 3.0, ()),
 ('ground_grass', 'nature-kit', 'ground_grass', 'nat', 6.0, ()),
 ('cliff_block', 'nature-kit', 'cliff_block_stone', 'nat', 6.0, ()),
 ('cliff_wall', 'nature-kit', 'cliff_stone', 'nat', 6.0, ()),
 ('cliff_large', 'nature-kit', 'cliff_large_stone', 'nat', 6.0, ()),
 ('cliff_corner', 'nature-kit', 'cliff_cornerLarge_stone', 'nat', 6.0, ()),
 ('cliff_corner_inner', 'nature-kit', 'cliff_cornerInner_stone', 'nat', 6.0, ()),
 ('cliff_steps', 'nature-kit', 'cliff_steps_stone', 'nat', 6.0, ()),
 ('cliff_slope', 'nature-kit', 'cliff_blockSlope_stone', 'nat', 6.0, ()),
 ('log_large', 'nature-kit', 'log_large', 'base', 3.0, ()),
 ('stump_old', 'nature-kit', 'stump_old', 'base', 3.0, ()),
 ('stump_tall', 'nature-kit', 'stump_oldTall', 'base', 3.0, ()),
 ('sign', 'nature-kit', 'sign', 'base', 4.0, ()),
 ('fence', 'survival-kit', 'fence', 'base', 3.0, ()),
 ('fence_fortified', 'survival-kit', 'fence-fortified', 'base', 3.0, ()),
 ('crate', 'survival-kit', 'box', 'base', 3.0, ()),
 ('crate_large', 'survival-kit', 'box-large', 'base', 3.0, ()),
 ('barrel', 'survival-kit', 'barrel', 'base', 2.7, ()),
 ('barrels', 'space-kit', 'barrels', 'base', 3.0, ()),
 ('generator', 'space-kit', 'machine_generator', 'base', 4.0, ()),
 ('relay_unit', 'space-kit', 'machine_wireless', 'base', 4.0, ()),
 ('dish', 'space-kit', 'satelliteDish_large', 'base', 5.0, ()),
 ('mast', 'space-kit', 'chimney', 'base', 3.5, ()),
 ('shed', 'space-kit', 'hangar_smallA', 'base', 4.0, ()),
 ('module', 'space-kit', 'corridor', 'base', 4.0, ()),
 ('container', 'space-kit', 'monorail_trainBox', 'base', 3.5, ()),
]

def pad4(b, fill=b'\0'):
    return b + fill * (-len(b) % 4)

def build(src, dst, pivot, drop):
    js, binc = g.load(src)
    lo, hi, _, _, _ = g.inspect(src)
    if pivot == 'base':
        t = [-(lo[0] + hi[0]) / 2, -lo[1], -(lo[2] + hi[2]) / 2]
    else:
        t = [0.0, -lo[1], 0.0]
        t[1] = 0.05 if abs(lo[1] + 0.05) < 1e-3 else -lo[1]
    for m in js.get('materials', []):
        p = m.setdefault('pbrMetallicRoughness', {})
        if m.get('name') in PAL and 'baseColorTexture' not in p:
            p['baseColorFactor'] = list(PAL[m['name']]) + [1.0]
        p['metallicFactor'] = 0.0
    if drop:
        names = [m.get('name') for m in js['materials']]
        for me in js['meshes']:
            me['primitives'] = [pr for pr in me['primitives']
                                if names[pr.get('material', 0)] not in drop]
    for im in js.get('images', []):
        if 'uri' in im and not im['uri'].startswith('data:'):
            data = open(os.path.join(os.path.dirname(src), im['uri']), 'rb').read()
            binc = pad4(binc)
            js['bufferViews'].append({'buffer': 0, 'byteOffset': len(binc), 'byteLength': len(data)})
            binc += data
            im['bufferView'] = len(js['bufferViews']) - 1
            im['mimeType'] = 'image/png'
            del im['uri']
    binc = pad4(binc)
    js['buffers'][0]['byteLength'] = len(binc)
    js['buffers'][0].pop('uri', None)
    sc = js['scenes'][js.get('scene', 0)]
    # Kenney exports list a mesh node in scene.nodes while it is also a child of an orphan
    # 'tmpParent' node (identity transform, not in the scene).  Detach scene roots from any
    # such parent so each node has exactly one parent, then re-parent them under srf_root.
    roots = []
    for r in sc['nodes']:
        if r not in roots:
            roots.append(r)
    for n in js['nodes']:
        if 'children' in n:
            n['children'] = [c for c in n['children'] if c not in roots]
            if not n['children']: del n['children']
    js['nodes'].append({'name': 'srf_root', 'translation': t, 'children': roots})
    sc['nodes'] = [len(js['nodes']) - 1]
    jb = pad4(json.dumps(js, separators=(',', ':')).encode(), b' ')
    out = struct.pack('<III', 0x46546C67, 2, 12 + 8 + len(jb) + 8 + len(binc))
    out += struct.pack('<II', len(jb), 0x4E4F534A) + jb + struct.pack('<II', len(binc), 0x004E4942) + binc
    open(dst, 'wb').write(out)

if __name__ == '__main__':
    packs, outdir = sys.argv[1], sys.argv[2]
    ini, rows = [], []
    for stem, kit, name, pivot, scale, drop in P:
        src = glob.glob(os.path.join(packs, kit, 'Models', 'G*', name + '.glb'))[0]
        dst = os.path.join(outdir, 'srf_' + stem + '.glb')
        if os.path.exists(dst):
            sys.exit('refusing to overwrite ' + dst)
        build(src, dst, pivot, drop)
        lo, hi, _, _, iss = g.inspect(dst)
        ini.append(f'[srf_{stem}]\nscale = {scale:g}\n')
        sz = [(h - l) * scale for l, h in zip(lo, hi)]
        mn = [x * scale for x in lo]; mx = [x * scale for x in hi]
        rows.append(f"srf_{stem}|{kit}/{name}|{pivot}|{scale:g}|" + ' '.join(f'{v:.2f}' for v in sz) +
                    '|' + ' '.join(f'{v:.2f}' for v in mn) + '|' + ' '.join(f'{v:.2f}' for v in mx) +
                    '|' + (';'.join(iss) or 'ok') + f'|{os.path.getsize(dst)}')
    print('\n'.join(rows))
    print('---INI---')
    print('\n'.join(ini))
