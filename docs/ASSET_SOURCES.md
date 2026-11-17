# Free asset sources survey

Date of survey: 2026-11-17. "Verified" means I fetched the licence/pack page during this survey. Pack pages usually do not state glTF scene structure, so the "needs" column is an estimate until a file is opened in Blender.

## 1. How the source list was obtained

I could NOT read the Reddit post. Both `old.reddit.com` and `www.reddit.com/.json` were blocked by the fetch tool, and a web search for the title did not find the post. This survey is built from well-known free-asset sites. I fetched each site's own pack and licence pages. Nothing here comes from the Reddit post or its comments. A web search only surfaced generic roundups, such as the itch.io "giant list of free assets" thread, which I did not rely on.

## 2. Shortlist

| # | Source / URL | Offers | Licence (verified?) | Formats | Fit |
|---|---|---|---|---|---|
| 1 | Kenney, https://kenney.nl/assets/category:3D | Many coherent low-poly 3D kits (space, factory, cave, nature, characters) | CC0 (verified on pack pages; https://kenney.nl/support says all assets CC0, attribution optional). The support page does not explicitly address redistribution, but CC0 permits it. | Pack pages did not list formats. In my experience GLB/FBX/OBJ are bundled, unverified | Best fit: single shared colour-map atlas per kit, so mostly drop-in |
| 2 | Quaternius, https://quaternius.com/ | Large sci-fi, nature and animated-character packs | Pack pages say CC0. BUT https://quaternius.com/license.html ("Quaternius Asset License v1.0") says no resale or redistribution of the assets themselves, even free or bundled. See Section 6. | OBJ/FBX/glTF (Standard tier, free), Blend (Source tier) | Great, but do not commit raw files to a public repo until the contradiction is resolved |
| 3 | Poly Haven, https://polyhaven.com/license | HDRIs, PBR textures, models | CC0, redistribution explicitly allowed (verified). Bulk scraping prohibited by ToS. | HDRI, textures, glTF models | HDRIs need skybox support. Textures usable as PNG albedo |
| 4 | ambientCG, https://docs.ambientcg.com/license/ | PBR materials, HDRIs | CC0, can include raw files in a game, attribution not required (verified) | PNG/JPG texture sets | Good for 8-bit PNG albedo (use the Color map only) |
| 5 | Kenney audio, https://kenney.nl/assets/category:Audio | Sci-fi sounds, impact sounds, interface sounds | CC0 (Sci-fi Sounds verified) | Formats not stated on page (unverified) | Drop-in (if WAV/OGG; check, because the engine takes WAV/MP3/FLAC) |
| 6 | Kenney UI/fonts, https://kenney.nl/assets/category:2D | UI Pack Sci-Fi, fonts, input prompts | CC0 (verified) | PNG, TTF-style | Drop-in |
| 7 | Freesound, https://freesound.org/help/faq/ | Ambience and SFX, per-sound licences | CC0, CC-BY or CC-BY-NC per sound (verified). Filter to CC0/CC-BY only. | WAV/FLAC/MP3 | Good for ambience. Needs a per-sound ledger entry |
| 8 | Incompetech (Kevin MacLeod), https://incompetech.com/music/royalty-free/faq.html | Music | CC-BY 4.0, credit required and easy to find, e.g. credits screen (verified) | MP3 | Drop-in. Attribution needed. Redistribution in repo: check each track page (unverified) |
| 9 | Mixamo, https://www.mixamo.com/ | Humanoid characters and animations | Custom Adobe terms. The page would not load, so unverified. | FBX only, Adobe account required | Needs Blender FBX to glTF conversion and an engine skinning feature. Redistribution of files in a repo is probably not allowed (unverified) |
| 10 | OpenGameArt, https://opengameart.org/ | Mixed 2D/3D/audio | Mixed per item (CC0, CC-BY, CC-BY-SA, GPL). My fetch of the FAQ returned 403, so unverified. | Varies | Use only after per-item licence check. Styles are inconsistent |

I excluded sites I did not fetch (Sketchfab, itch.io, Sonniss, CGTrader, etc.). They may be useful, but I did not verify them.

## 3. Specific recommended packs

All "verified" facts below come from the pack's own page. "Convert" notes are my estimate.

### 3a. Modular sci-fi / industrial interiors (existing facility)

| Pack | Licence | Format / style | Conversion |
|---|---|---|---|
| Kenney Modular Space Kit, https://kenney.nl/assets/modular-space-kit | CC0 | 40 files, low-poly, flat colour atlas, has variations and animation | Likely drop-in per piece. Animated doors need rigid-part animation |
| Kenney Factory Kit, https://kenney.nl/assets/factory-kit | CC0 | 140 files, conveyors, industrial/warehouse tags, colour variations | Likely drop-in per piece |
| Quaternius Modular Sci-Fi Megakit, https://quaternius.com/packs/modularscifimegakit.html | CC0 on page (see licence caveat) | 277 models, FBX/OBJ/glTF, textured. About 60-70% is in the free tier | Textures/materials unknown. May need merge + atlas bake |
| Quaternius Ultimate Modular Sci-Fi Pack, https://quaternius.com/packs/ultimatemodularscifi.html | CC0 on page (caveat) | 46 models, FBX/OBJ/Blend (no glTF listed). **Not textured** | Needs colour/texture work. Lower value |
| Quaternius Sci-Fi Essentials Kit, https://quaternius.com/packs/scifiessentialskit.html | CC0 on page (caveat) | 65 models, robots, guns, screens, crates. Textured, FBX/OBJ/glTF | Good props. Check texture count |

Style note: Kenney and Quaternius are different hands. Pick one as the base. Kenney is the safest for single-atlas import.

### 3b. Candidate new locations

1. **Overgrown surface exterior.** Kenney Nature Kit (https://kenney.nl/assets/nature-kit, CC0, 330 files, trees/rocks/foliage). Alternative: Quaternius Stylized Nature MegaKit (https://quaternius.com/packs/stylizednaturemegakit.html, CC0 on page, 116 textured models, Ghibli-like style). The Quaternius kit is stylistically softer than the industrial kits. Needs a skybox (see 3f).
2. **Cavern / flooded level.** Kenney Modular Cave Kit (https://kenney.nl/assets/modular-cave-kit, CC0, 40 files, with variations). Water needs an engine shader or plane. Textures from ambientCG for rock look are optional.
3. **Reactor / industrial exterior.** Kenney Factory Kit plus Kenney City Kit (Industrial) (https://kenney.nl/assets/city-kit-industrial). I saw the City Kit (Industrial) name in the listing but did not fetch its page, so its licence is unverified (likely CC0, unconfirmed).
4. **Orbital / space section.** Kenney Space Kit (https://kenney.nl/assets/space-kit, CC0, 150 files, planets/ships). Quaternius Ultimate Space Kit (https://quaternius.com/packs/ultimatespacekit.html, CC0 on page, 92 textured models including animated ones). Pair with Modular Space Kit corridors. Needs a space skybox.

### 3c. Characters and animation

| Option | Licence | Format / animation | Needs |
|---|---|---|---|
| Quaternius Animated Mech Pack, https://quaternius.com/packs/animatedmech.html | CC0 on page (caveat) | 4 mechs, FBX/OBJ/Blend/glTF, textured, skeletal animations | Skeletal animation. Could serve as a robot NPC |
| Quaternius Animated Robot Pack, https://quaternius.com/packs/animatedrobot.html | CC0 on page (caveat) | 1 cute robot, FBX/OBJ/Blend, **not textured** | Skinning and texture work. No glTF listed |
| Quaternius Universal Base Characters, https://quaternius.com/packs/universalbasecharacters.html | CC0 on page (caveat) | Humanoid rig, ~13k tris each, FBX/OBJ/glTF. No animations included | Skinning. Pair with the animation library |
| Quaternius Universal Animation Library, https://quaternius.com/packs/universalanimationlibrary.html | CC0 on page (caveat) | 120+ animations: jog, sprint, push, crawl, swim, sit, emotes. GLB/FBX/Blend. Retargetable | Skinning plus a retargeting/clip system. Best format for a future glTF skinning feature |
| Quaternius Universal Animation Library 2, https://quaternius.com/packs/universalanimationlibrary2.html | CC0 on page (caveat) | 130+ animations: combat combos, parkour, farming, zombie locomotion | Same as above |
| Kenney Mini Characters, https://kenney.nl/assets/mini-characters | CC0 | 40 files, listed as animated, formats not stated | Unverified details. Skinning probable |
| Mixamo, https://www.mixamo.com/ | Unverified (Adobe terms) | FBX only, retargetable auto-rig | Blender conversion plus skinning. Licence uncertain for committing files |

Idle/walk/run/jump/interact: the Universal Animation Library page names jog, sprint and push explicitly. I could not confirm idle, jump or interact clips from the page. Check the downloaded clip list.

For a first-person game with a visible robot only in shadows/reflections, the rigid multi-part robot route (no skinning) works today.

### 3d. Props, textures, skyboxes

- Props: Quaternius Sci-Fi Essentials Kit (above), Kenney Factory Kit (crates/conveyors), Kenney Modular Space Kit.
- Textures/materials: ambientCG, https://docs.ambientcg.com/license/ (CC0). Poly Haven, https://polyhaven.com/ (CC0).
- Skyboxes/HDRIs (needs engine feature): Poly Haven HDRIs, https://polyhaven.com/hdris (CC0; 12 categories; format/resolution not confirmed from the page).

### 3e. Audio and music

- SFX: Kenney Sci-fi Sounds https://kenney.nl/assets/sci-fi-sounds (CC0, 70 files); Impact Sounds https://kenney.nl/assets/impact-sounds; Interface Sounds https://kenney.nl/assets/interface-sounds (both listed, licences not individually fetched).
- Ambience: Freesound (CC0/CC-BY filter). No ambience pack found on Kenney.
- Music: Incompetech (CC-BY 4.0).

### 3f. UI

- Kenney UI Pack Sci-Fi, https://kenney.nl/assets/ui-pack-sci-fi (CC0, 130 files, buttons/panels/sliders).
- Kenney Fonts, https://kenney.nl/assets/kenney-fonts (CC0, 11 files, tagged pixel fonts).
- Kenney Input Prompts, https://kenney.nl/assets/input-prompts (seen in listing, licence not fetched).

## 4. Attribution ledger template

Keep as `docs/ASSET_LEDGER.md`. Fill in for every file committed, even CC0 ones.

| Asset (file/pack) | Author | Source URL | Licence (+ link) | Date fetched | Modified? | Redistribution OK in repo? |
|---|---|---|---|---|---|---|
| assets/models/knight.glb (KayKit Character Pack: Adventurers 1.0, `Characters/gltf/Knight.glb`; 3.6 MB, 1 embedded 1024x1024 atlas, 41-joint rig, 76 clips) | Kay Lousberg (KayKit) | https://github.com/KayKit-Game-Assets/KayKit-Character-Pack-Adventures-1.0 (the author's GitHub account, linked from https://kaylousberg.itch.io/kaykit-adventurers) | CC0 1.0 Universal: the repo's LICENSE.txt ("License: (Creative Commons Zero, CC0)", created/distributed by Kay Lousberg), and the itch page lists "Creative Commons Zero v1.0 Universal". Attribution optional | 2026-11-17 | Renamed Knight.glb to knight.glb; bytes unmodified. Accessory variants are hidden at import (assets/models/import.ini), not edited out | Yes (CC0) |
| assets/models/srf_tree_*, srf_bush_*, srf_plant_flat, srf_grass_*, srf_rock_*, srf_stone_*, srf_ground_grass, srf_cliff_*, srf_log_large, srf_stump_*, srf_sign (27 GLB; Kenney Nature Kit 2.1) | Kenney (kenney.nl) | https://kenney.nl/assets/nature-kit | CC0 1.0, https://creativecommons.org/publicdomain/zero/1.0/ (shown on the pack page and in the archive License.txt) | 2026-11-17 | Yes: pivot moved, flat colours remapped, images embedded (tools/srf_build.py); source files untouched | Yes (CC0) |
| assets/models/srf_barrels, srf_generator, srf_relay_unit, srf_dish, srf_mast, srf_shed, srf_module, srf_container (8 GLB; Kenney Space Kit 2.0) | Kenney (kenney.nl) | https://kenney.nl/assets/space-kit | CC0 1.0, https://creativecommons.org/publicdomain/zero/1.0/ (shown on the pack page and in the archive License.txt) | 2026-11-17 | Yes: pivot moved, flat colours remapped, images embedded (tools/srf_build.py); source files untouched | Yes (CC0) |
| assets/models/srf_fence, srf_fence_fortified, srf_crate, srf_crate_large, srf_barrel (5 GLB; Kenney Survival Kit 2.0; colormap.png embedded) | Kenney (kenney.nl) | https://kenney.nl/assets/survival-kit | CC0 1.0, https://creativecommons.org/publicdomain/zero/1.0/ (shown on the pack page and in the archive License.txt) | 2026-11-17 | Yes: pivot moved, flat colours remapped, images embedded (tools/srf_build.py); source files untouched | Yes (CC0) |
| assets/models/srf_terr_cliffs, srf_terr_ravine, srf_terr_outcrops (3 GLB; rock skins over the Surface's box terrain) | Generated in this repo by `tools/gen_terrain.py` (procedural, stdlib Python, no external input) | none (not downloaded) | Project's own work, same provenance rule as `tools/gen_models.py`; no third-party content | 2026-11-17 (generated) | n/a: regenerate with `python tools/gen_terrain.py` | Yes |
| assets/models/wren_bot.glb (the Surface quest-giver: original robot, 2,040 triangles, 15 joints, clips Idle / Interact / Cheer, 5 flat-colour materials, no textures) | Generated in this repo by `tools/gen_wren.py` (Blender primitives built and animated by script; no external geometry, textures or motion data) | none (not downloaded). A third-party character library was looked at for the general jointed-mannequin idea only; nothing was downloaded or traced | Project's own work, same provenance rule as `tools/gen_terrain.py`; no third-party content | 2026-11-17 (generated) | n/a: regenerate with `blender -b --factory-startup -P tools/gen_wren.py` | Yes |

Licences to avoid:
- CC-BY-NC, CC-BY-ND, "free for personal use", and "non-commercial only". These block later release or sale.
- CC-BY-SA and GPL for art: share-alike can propagate to your game assets.
- AI-generated packs with unclear training/rights.
- Ripped game assets and fan assets.
- Mixamo or Adobe files, unless you confirm the terms allow committing them to a public repo.
- Freesound CC-BY-NC sounds.
- CC-BY is fine, but needs a visible credits screen entry.

## 5. Engine work, ranked by assets unlocked

1. **Multi-mesh + per-mesh material import (embedded or multiple textures).** Unlocks most Quaternius multi-material kits without Blender baking. *Landed (ADR-030): base colour only.*
2. **Skeletal animation from glTF (skinning, clip playback).** Unlocks Universal Animation Library, Universal Base Characters, Animated Mech/Robot, Kenney Mini Characters, and a visible player model. *Landed (ADR-030).*
3. **Skybox / HDRI environment.** Unlocks surface and space locations. Poly Haven HDRIs. *Landed (ADR-030) for 8-bit equirect PNGs and a procedural sky; .hdr still open.*
4. **Normal/roughness maps and vertex colours.** Unlocks ambientCG/Poly Haven PBR sets and untextured colour-only kits.
5. **Rigid node-hierarchy animation (doors, fans).** Cheap, uses animated Kenney pieces. *Covered by ADR-030's rigid parts (any animated glTF), but not yet tried on a real pack file.*
6. **Water shader / transparency.** For a flooded level.

## 6. Open questions / not verified

- Reddit post: unread (blocked). Its 50 sites are unknown to me.
- **Quaternius licence contradiction.** Pack pages say CC0, but https://quaternius.com/license.html (v1.0) forbids redistributing the assets themselves, even free. If the repo is public, either contact the author or keep Quaternius files out of the repo (git-ignore plus a fetch script) until clarified.
- Kenney: licence confirmed CC0, but file formats and per-model material layout are not shown on the pack pages. Download one pack manually to confirm.
- OpenGameArt: FAQ blocked (403). Mixamo: terms page would not load.
- Poly Haven HDRI resolutions/formats unconfirmed. Kenney audio file formats unconfirmed (engine reads WAV/MP3/FLAC; OGG would need conversion).
- Which Quaternius files are in the free "Standard" tier vs paid "Pro" (about 60-70% free by the page). Exact prices were not readable.
- Not looked at: Sketchfab, itch.io, Sonniss GDC audio, Kay Lousberg, Synty, Unity/Unreal marketplaces. Their licences vary and would need checking.
- Idle/walk/run/jump/interact coverage of each animation set is not confirmed.
