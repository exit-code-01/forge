# COLLAB_LOG.md — shared channel between the owner side and the collaborator side

Append-only. Newest entry at the bottom. Never edit or delete another side's entry.
Entry format (header line, then at most 6 lines):

    ### HH:MM side/lane — STATUS|BLOCKER|QUESTION|HANDOFF

`side` is `owner` or `collab`; `lane` is O1..O3 / C0..C3 (docs/COLLAB.md). Use local 24h time of the writer and state the timezone once in your first entry.
Commit and push log entries promptly on your own branch, and tell your human so they can relay anything urgent.
Machine or permission blockers go here (rule 10 in CLAUDE.md).

---

### HH:MM owner/O1-O3 — STATUS
- Landed on `upgrade-pass`: the Surface (outdoor location) with the mission system, NPC quest-giver, 42 CC0 props, E (Interact) verb.
- Owner machine: test_all 51/51, test_surface 122/122, clean build, no validation messages.
- Starting now: O1 triangle-mesh colliders, O2 Surface art via Blender, O3 Surface playtest fixes. Files listed in COLLAB.md section 3.
- Collaborator: begin with C0 (setup report), then C1 (outdoor shadows). Session ends at HH:MM.

### 11:22 owner/O2 — STATUS (times IST, UTC+05:30; written by a second owner-side agent)
- The owner-side agent on O1 was interrupted mid-edit at about 10:37. Its seven files are untouched by me: PhysicsWorld.hpp/.cpp, Model.hpp/.cpp, ScriptEngine.hpp/.cpp, sandbox/src/main.cpp (ADR-032 mesh colliders, not yet built or tested, ADR text not yet in DECISIONS.md).
- Heads-up for C1: O1 edited engine/src/renderer/Model.cpp and Model.hpp (CPU triangle copy for colliders). Those two files sit inside C1's folder; please leave them to O1.
- O2 added: tools/gen_terrain.py and three generated models (srf_terr_cliffs, srf_terr_ravine, srf_terr_outcrops): rock skins over the Surface's box terrain, no colliders. Edited: assets/scripts/surface_dress.lua (spawns them; the 60 cliff_wall tiles become the fallback), plus rows in docs/ASSET_SOURCES.md and docs/SURFACE_ASSETS.md.
- NOT verified in the engine: this agent has no shell on the owner machine today, so no build, no test run, no screenshot. Checked offline only (GLB structure, winding, no skin inside a box, software preview). The gate (51/51, 122/122) still has to be run before any push.
- Details and how to revert: docs/O2_TERRAIN_SKINS.md.

### 11:45 owner/O3 — STATUS
- missions.lua: the E prompt now needs line of sight (static `Srf `/`Dress Col ` bodies block; the target's own collider, SPARK and carried props do not). surface.lua: `body` set on both interactables, three area hints reworded (old text kept in docs/O2_TERRAIN_SKINS.md).
- NOT run in the engine. Stub-tested and syntax-checked only. Head-house lighting not touched.

### 11:45 owner/O1 — STATUS (IST; a third owner-side agent resumed the interrupted O1 work, with a shell)
- O1's seven files were complete as left: built clean, no edits needed. ADR-032 (mesh colliders + spawn yaw) is now in DECISIONS.md; ENGINE_GAPS updated.
- New test `tools/dev/test_mesh.lua` (a new file in tools/dev, C0's folder: say if you want it moved): 12/12. Run with `rundev.sh 25`.
- Gate run on the owner machine with O1 + O2 skins + O3 Lua edits all in: test_all 51/51, test_surface 122/122, clean build, no validation messages. The three srf_terr models import; dressing places 143 entities, none skipped.
- Nothing committed or pushed. Not yet done: hands-on walk on a mesh collider; nothing in the game uses one yet.

### 11:55 owner/O3 — STATUS
- Head-house was confirmed near-black on a real run (one light at 5.0, radius 9). surface.lua now sets two ceiling lights (`srf_hh`, `srf_hh2`); screenshot checked, the room reads.
- O2 skins looked at in the engine from the arrival point and the ledge: faceted rock, no holes seen from those two views. The bell-from-ledge and pad-from-lookout sightlines were NOT looked at by eye (the sequencer steps that depend on them pass).
- After this edit: test_surface re-run, 122/122, no validation messages. test_all was NOT re-run after it (last run 51/51, before the light change).

### 12:30 owner/O1-O3 — STATUS
- Counts changed: test_surface is now 126 (4 new checks for the E prompt behind a wall), test_mesh 16 (ramp walk, non-uniform scale). CLAUDE.md rule 9 and COLLAB.md section 2 updated; C0, tools/dev/test_surface.lua was edited by the owner side (one block after "E prompt is Wren").
- First use of ADR-032: the seven ground rocks in surface_dress.lua use `collider = "mesh"` (named "Dress Col N"). Dressing count is 136.
- Gate as run on the owner machine with everything in: 51/51, 126/126, 16/16, clean build, no validation messages. Surface frame 42.8 ms avg (debug + validation).
- Looked at by screenshot: bell from the ledge and pad from the lookout are both clear of the rock skins. Not done: keyboard walk on the rocks; terrain is still boxes. Nothing committed.

### 13:10 owner/O1-O3 — STATUS
- The three terrain skins are now mesh colliders (ADR-032; cliffs 10,592 triangles) in front of the boxes, which stay as backing. Keyboard-walked with a real W key: stops at rocks and cliffs, slides 11 m along both cliff walls with no snag.
- Shadowed outdoor faces were near-black: renderer ambient is fixed at about 1/28 of the sun. Worked round in Lua with a small emissive "shade fill" on tinted terrain and the skins (`SHADE_FILL`, surface.lua). REQUEST to C1 (renderer lane), not urgent: an optional `forge.render.set_ambient(sky, ground)`; details in ENGINE_GAPS.md. No renderer file was touched by the owner side for this.
- Surface 126/126 and mesh 16/16 as run on this state, no validation messages, Surface frame 43.8 ms avg. test_all on this state: 51/51, no validation messages.
- Still open on O2: the quest-giver is still the knight model (no robot asset on disk); floors, stairs and ledges are still boxes. Nothing committed.
