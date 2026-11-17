# Surface asset manifest

Models for the outdoor "Surface" location. 42 self-contained GLB files in `assets/models/`, all stems prefixed `srf_`. Sources: Kenney Nature Kit 2.1, Space Kit 2.0 and Survival Kit 2.0, all CC0 (see the ledger in `docs/ASSET_SOURCES.md`).

**Nothing here was loaded in the engine.** Sizes and positions come from parsed GLB accessor bounds with node transforms applied (`tools/glb_bounds.py`), multiplied by the `scale` in `assets/models/import.ini`. Orientation notes (which way a face points) are from reading the bounds only, not from looking at the models; treat them as unverified.

## How the files were made (`tools/srf_build.py`)
- Source files are untouched. Each output wraps the scene in a root node that moves the pivot: base-centre for props, and for grid pieces (cliffs, ground) only a lift of the kit's -0.05 ground offset so the base sits at y=0 and the pieces still tile.
- Kenney's Nature and Space kits ship flat colour factors tinted teal and orange. They were remapped by material name to an overgrown/derelict palette (olive grass, dark-green pine, grey stone, brown wood, weathered metal). Metallic factor set to 0. The Survival kit's `colormap.png` was embedded into each GLB that uses it; its colours are untouched.
- `srf_tree_dead` is Kenney `tree_thin` with the foliage primitive removed.
- No alpha, normal maps or vertex colours are used. Foliage is solid geometry.
- Scales are uniform and were chosen for a 1.8 m player: trees 5.5 to 7.3 m, boulders 1 to 3 m, 6 m grid for cliff and ground tiles. Kenney cliff pieces on a 1.0 unit grid become 6 m cells; all cliff and ground pieces share scale 6 so they tile with each other.
- Axes: Y up. "Collider" is a suggested single box (half-extents, with centre relative to the model origin); the engine only has boxes. Rocks use 85% of the bounds so the box does not stick out.

## Models
| Stem | What | Size x y z (m) | Origin | Collider half-extents (box centre rel. origin) | Textured | Notes |
|---|---|---|---|---|---|---|
| `srf_tree_pine_a` | Pine tree, medium | 1.55 x 6.12 x 1.57 | base-centre (y=0 at ground, x/z centred) | trunk only: half (0.3, 3.1, 0.3) at y 3.1 | self-coloured | scale 4; Tiered cone foliage; trunk ~0.4 m wide |
| `srf_tree_pine_b` | Pine tree, tall and slim | 1.68 x 7.26 x 1.94 | base-centre (y=0 at ground, x/z centred) | trunk only: half (0.3, 3.6, 0.3) at y 3.6 | self-coloured | scale 3.5; Tiered cone foliage |
| `srf_tree_oak` | Broadleaf tree with round crown | 3.02 x 6.83 x 2.62 | base-centre (y=0 at ground, x/z centred) | trunk only: half (0.3, 3.4, 0.3) at y 3.4 | self-coloured | scale 4; Wide crown (3 m) |
| `srf_tree_tall` | Tall slim tree (lollipop crown) | 1.60 x 6.75 x 1.85 | base-centre (y=0 at ground, x/z centred) | trunk only: half (0.3, 3.4, 0.3) at y 3.4 | self-coloured | scale 4;  |
| `srf_tree_dead` | Dead / bare forked tree (foliage removed from Kenney tree_thin) | 1.75 x 5.45 x 1.58 | base-centre (y=0 at ground, x/z centred) | trunk only: half (0.3, 2.7, 0.3) at y 2.7 | self-coloured | scale 6; Bare branches are solid geometry; only the trunk should collide |
| `srf_bush_a` | Small bush | 1.58 x 0.98 x 1.58 | base-centre (y=0 at ground, x/z centred) | no collider | self-coloured | scale 4;  |
| `srf_bush_large` | Large bush | 1.68 x 1.09 x 1.51 | base-centre (y=0 at ground, x/z centred) | no collider | self-coloured | scale 4.5;  |
| `srf_bush_detailed` | Detailed bush (more facets) | 2.11 x 1.26 x 2.11 | base-centre (y=0 at ground, x/z centred) | no collider | self-coloured | scale 3.5;  |
| `srf_plant_flat` | Low leafy plant | 0.81 x 0.85 x 0.81 | base-centre (y=0 at ground, x/z centred) | no collider | self-coloured | scale 3;  |
| `srf_grass_tuft` | Grass tuft | 1.23 x 0.76 x 1.22 | base-centre (y=0 at ground, x/z centred) | no collider | self-coloured | scale 3; Solid blades (no alpha) |
| `srf_grass_leafs` | Small leafy ground cover | 0.70 x 0.43 x 0.77 | base-centre (y=0 at ground, x/z centred) | no collider | self-coloured | scale 3;  |
| `srf_rock_large_a` | Boulder, mossy top | 2.35 x 0.78 x 3.05 | base-centre (y=0 at ground, x/z centred) | half (1.00, 0.33, 1.30) at (0.00, 0.39, 0.00) | self-coloured | scale 3; Wide and low |
| `srf_rock_large_c` | Boulder, mossy top, widest | 3.19 x 0.96 x 3.05 | base-centre (y=0 at ground, x/z centred) | half (1.36, 0.41, 1.30) at (0.00, 0.48, 0.00) | self-coloured | scale 3;  |
| `srf_rock_tall_b` | Rock pillar, mossy top | 2.29 x 2.65 x 2.31 | base-centre (y=0 at ground, x/z centred) | half (0.97, 1.13, 0.98) at (0.00, 1.32, 0.00) | self-coloured | scale 3;  |
| `srf_stone_large_b` | Boulder, bare stone | 2.30 x 1.29 x 3.05 | base-centre (y=0 at ground, x/z centred) | half (0.98, 0.55, 1.30) at (0.00, 0.65, 0.00) | self-coloured | scale 3;  |
| `srf_stone_tall_a` | Tall bare stone outcrop | 2.95 x 2.99 x 2.05 | base-centre (y=0 at ground, x/z centred) | half (1.25, 1.27, 0.87) at (0.00, 1.50, 0.00) | self-coloured | scale 3; Largest rock |
| `srf_stone_small_c` | Flat pebble/stone | 1.08 x 0.37 x 1.08 | base-centre (y=0 at ground, x/z centred) | no collider | self-coloured | scale 3;  |
| `srf_ground_grass` | Grass ground tile | 6.00 x 0.00 x 6.00 | cell-centre XZ, base y=0 (kit grid layout, not re-centred) | floor slab half (3.0, 0.1, 3.0) at y -0.1 | self-coloured | scale 6; Flat plane, top face at y=0; tiles on a 6 m grid with cliff pieces |
| `srf_cliff_block` | Solid cliff block with grass top | 6.00 x 6.00 x 6.00 | cell-centre XZ, base y=0 (kit grid layout, not re-centred) | half (3.00, 3.00, 3.00) at (0.00, 3.00, 0.00) | self-coloured | scale 6; 6 m cube, tiles on 6 m grid |
| `srf_cliff_wall` | Thin cliff face (wall only) | 6.00 x 6.00 x 1.01 | cell-centre XZ, base y=0 (kit grid layout, not re-centred) | half (3.00, 3.00, 0.51) at (0.00, 3.00, 2.50) | self-coloured | scale 6; Face at +Z edge of tile cell (z from +2 to +3); tiles on 6 m grid; rotate for other sides |
| `srf_cliff_large` | Cliff wall, thicker | 6.00 x 6.00 x 2.51 | cell-centre XZ, base y=0 (kit grid layout, not re-centred) | half (3.00, 3.00, 1.25) at (0.00, 3.00, 1.75) | self-coloured | scale 6; Wall occupies +Z half-ish of the cell (z +0.5..+3); 6 m grid |
| `srf_cliff_corner` | Cliff outer corner column | 2.51 x 6.00 x 2.51 | cell-centre XZ, base y=0 (kit grid layout, not re-centred) | half (1.25, 3.00, 1.25) at (-1.75, 3.00, 1.75) | self-coloured | scale 6; Occupies the -X/+Z corner of its cell (x -3..-0.5, z +0.5..+3); 6 m grid |
| `srf_cliff_corner_inner` | Cliff inner corner block | 6.00 x 6.00 x 6.00 | cell-centre XZ, base y=0 (kit grid layout, not re-centred) | half (3.00, 3.00, 3.00) at (0.00, 3.00, 0.00) | self-coloured | scale 6; Full 6 m cell; 6 m grid |
| `srf_cliff_steps` | Cliff with ledge steps | 6.00 x 6.00 x 6.30 | cell-centre XZ, base y=0 (kit grid layout, not re-centred) | half (3.00, 3.00, 3.15) at (0.00, 3.00, -0.15) | self-coloured | scale 6; Slightly overshoots cell on -Z by 0.3 m; 6 m grid |
| `srf_cliff_slope` | Cliff block with sloped side | 7.20 x 6.00 x 6.00 | cell-centre XZ, base y=0 (kit grid layout, not re-centred) | no collider (ramp); approximate with stepped boxes or skip | self-coloured | scale 6; Overshoots 0.6 m each side in X (7.2 m wide); a box is a poor fit |
| `srf_log_large` | Fallen log | 3.00 x 1.25 x 1.65 | base-centre (y=0 at ground, x/z centred) | half (1.50, 0.62, 0.82) at (0.00, 0.62, 0.00) | self-coloured | scale 3; Lies along X |
| `srf_stump_old` | Tree stump | 1.07 x 0.80 x 1.11 | base-centre (y=0 at ground, x/z centred) | half (0.54, 0.40, 0.56) at (0.00, 0.40, 0.00) | self-coloured | scale 3;  |
| `srf_stump_tall` | Tall broken stump | 1.07 x 2.00 x 1.11 | base-centre (y=0 at ground, x/z centred) | half (0.54, 1.00, 0.56) at (0.00, 1.00, 0.00) | self-coloured | scale 3;  |
| `srf_sign` | Wooden sign on post | 1.20 x 1.64 x 0.28 | base-centre (y=0 at ground, x/z centred) | no collider | self-coloured | scale 4; Board faces +Z (verify) |
| `srf_fence` | Wooden fence section | 1.50 x 1.55 x 0.13 | base-centre (y=0 at ground, x/z centred) | half (0.75, 0.78, 0.1) at y 0.78 | texture embedded (palette) | scale 3; 1.5 m sections, run along X, tile end to end |
| `srf_fence_fortified` | Fortified fence section (planks, spikes) | 1.50 x 1.55 x 0.16 | base-centre (y=0 at ground, x/z centred) | half (0.75, 0.78, 0.1) at y 0.78 | texture embedded (palette) | scale 3; 1.5 m sections along X |
| `srf_crate` | Wooden crate | 0.75 x 0.75 x 0.75 | base-centre (y=0 at ground, x/z centred) | half (0.38, 0.38, 0.38) at (0.00, 0.38, 0.00) | texture embedded (palette) | scale 3;  |
| `srf_crate_large` | Large wooden crate | 0.75 x 0.75 x 1.50 | base-centre (y=0 at ground, x/z centred) | half (0.38, 0.38, 0.75) at (0.00, 0.38, 0.00) | texture embedded (palette) | scale 3; Long axis Z |
| `srf_barrel` | Barrel | 0.64 x 0.93 x 0.64 | base-centre (y=0 at ground, x/z centred) | half (0.32, 0.47, 0.32) at (0.00, 0.47, 0.00) | texture embedded (palette) | scale 2.7;  |
| `srf_barrels` | Cluster of barrels | 1.65 x 1.05 x 1.49 | base-centre (y=0 at ground, x/z centred) | half (0.82, 0.53, 0.74) at (0.00, 0.53, 0.00) | self-coloured | scale 3;  |
| `srf_generator` | Generator machine | 2.80 x 1.60 x 2.10 | base-centre (y=0 at ground, x/z centred) | half (1.40, 0.80, 1.05) at (0.00, 0.80, 0.00) | self-coloured | scale 4;  |
| `srf_relay_unit` | Wireless relay unit / transmitter cabinet | 3.00 x 2.40 x 2.00 | base-centre (y=0 at ground, x/z centred) | half (1.50, 1.20, 1.00) at (0.00, 1.20, 0.00) | self-coloured | scale 4; Good for the relay station |
| `srf_dish` | Satellite dish | 4.24 x 4.07 x 3.68 | base-centre (y=0 at ground, x/z centred) | half (2.12, 2.04, 1.84) at (0.00, 2.04, 0.00) | self-coloured | scale 5; Dish opening direction: see Notes in-engine; verify |
| `srf_mast` | Tall chimney / antenna mast (stack) | 1.40 x 7.00 x 1.40 | base-centre (y=0 at ground, x/z centred) | half (0.70, 3.50, 0.70) at (0.00, 3.50, 0.00) | self-coloured | scale 3.5; Thin tower |
| `srf_shed` | Small hangar / shed building | 8.00 x 4.00 x 8.00 | base-centre (y=0 at ground, x/z centred) | half (4.00, 2.00, 4.00) at (0.00, 2.00, 0.00) | self-coloured | scale 4; 8 m footprint, 4 m tall; solid, no interior |
| `srf_module` | Corridor module / small enclosure | 4.00 x 4.00 x 4.00 | base-centre (y=0 at ground, x/z centred) | half (2.00, 2.00, 2.00) at (0.00, 2.00, 0.00) | self-coloured | scale 4; Open ends along Z (tube); 4 m cube |
| `srf_container` | Freight pod / container | 1.75 x 2.80 x 3.50 | base-centre (y=0 at ground, x/z centred) | half (0.88, 1.40, 1.75) at (0.00, 1.40, 0.00) | self-coloured | scale 3.5; Long axis Z |
| `srf_terr_cliffs` | Rock skin over the cliff ring (3 tiers, 4 sides, plus its drop into the ravine) | 116 x 53 x 116 | WORLD-authored around (400, 0, 0): spawn at (400, 0, 0), scale 1, no yaw | mesh collider (ADR-032); boxes kept as backing | self-coloured | generated by `tools/gen_terrain.py`, not from a kit; 10,592 triangles; recessed around the cliff bell |
| `srf_terr_ravine` | Rock skin on both ravine walls (x 366 and x 378) | 12 x 27 x 88 | as above | mesh collider (ADR-032); boxes kept as backing | self-coloured | generated; 3,528 triangles; flat where the bridge slides out |
| `srf_terr_outcrops` | Rock skin on the mesa, the ledge, the ore shelf and terrace C (side faces only, tops untouched) | 88 x 6 x 67 | as above | mesh collider (ADR-032); boxes kept as backing | self-coloured | generated; 1,470 triangles; recessed at both stair runs |
| `srf_terr_ground` | Valley floor: flat at y = 0 where things stand, scree banks at the cliff feet, dirt paths | see surface.lua | WORLD-authored around (400, 0, 0): spawn at (400, 0, 0), scale 1, no yaw | mesh collider (ADR-032); surface.lua sinks its ground boxes 6 cm under it as backing | per-triangle colour | generated by `tools/gen_ground.py` (reuses `tools/gen_terrain.py`); 7,890 triangles; if not loaded, the ground boxes stay the floor at y = 0 (surface.lua `gy`) |
| `srf_terr_ramps` | Two walkable ramps over the stair runs | see surface.lua | as above | mesh collider (ADR-032), friction 1.8 (ramps are 23-27 degrees) | see tools/gen_ramps.py | generated by `tools/gen_ramps.py`; 360 triangles; if not loaded, the step boxes are skipped as ramps (`put()` logs it once) |
| `srf_terr_tops` | Walking surfaces of the mesa, lookout, ledge, terrace and ore shelf at their exact heights, with a rim skirt | see surface.lua | as above | mesh collider (ADR-032); boxes sink 6 cm under it as backing | see tools/gen_tops.py | generated by `tools/gen_tops.py` (reuses `tools/gen_terrain.py`); 1,622 triangles; if not loaded, the boxes keep their own tops (surface.lua `TOPS()` = 0) |
| `wren_bot` | Wren, the Surface quest-giver: original relay-keeper robot, 15 joints, clips Idle / Interact / Cheer | see surface.lua | spawn at (394.5, 0, 13), scale 1 | box collider "Wren Body", half (0.35, 0.90, 0.35) at (0.00, 0.90, 0.00); the model itself is not a collider | no textures, 5 flat-colour materials | generated by `tools/gen_wren.py` (Blender primitives, animated by script); 2,040 triangles; if not loaded, surface.lua falls back to `knight`|

## Character / NPC
The Surface quest-giver is `wren_bot.glb`, an original relay-keeper robot generated by `tools/gen_wren.py` (headless Blender; 2,040 triangles, 15 joints, clips Idle / Interact / Cheer). `surface.lua` falls back to the knight (`assets/models/knight.glb`, KayKit, CC0, 41 joints, scale 0.65 in import.ini) if it is missing.
