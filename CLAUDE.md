# CLAUDE.md — binding rules for every agent in this repo

Read docs/COLLAB.md before doing anything. Then follow these rules exactly.

1. **Token discipline.** Usage limits are real (the owner has hit theirs).
   Never read huge files whole (`sandbox/src/main.cpp`, `assets/scripts/scene.lua`, `docs/DECISIONS.md`): grep, then read a range.
   Keep command output short (pipe through `head`/`grep`). At most 2 subagents at a time.
   Cheap models (Haiku/Sonnet) for spec'd or routine work, docs and mechanical edits; the strongest model only for design, review and hard debugging.
2. **Scope.** Do exactly the lane assigned in docs/COLLAB.md. No drive-by refactors.
   Never edit files another lane owns; ask in docs/COLLAB_LOG.md instead. (Parallel lanes only merge cleanly if files do not overlap.)
3. **Honesty.** Report test counts as actually run, say what you did not verify, never claim something works unseen. (The other side plans around your word.)
4. **Assets.** CC0 only: Kenney and KayKit are approved; Quaternius is avoided (conflicting licence pages).
   Every added asset gets a row in docs/ASSET_SOURCES.md. Never recreate an existing commercial game (names, characters, levels, look).
5. **Privacy.** No personal names, emails or people's machine paths in commits, docs, or requests to outside services.
   Say "the owner" / "the collaborator".
6. **Git.**
   - Collaborator side: never commit to `main` or directly to `upgrade-pass`; work on your lane branch `collab/<lane>-<short-name>`.
   - Small commits. `git pull --rebase` before every push. Never force-push.
   - Never commit build output, `settings.ini`, `*.bak`, screenshots dumps or logs.
   - Commit/push only when your human says so. The collaborator's human may pre-authorise commits and pushes to their lane branch: ask once at session start and remember the answer.
7. **Shaders.** SPIR-V is checked in (ADR-011). If you edit a `.vert`/`.frag` you must recompile with `glslc` and commit the `.spv` too, or the engine will not see the change.
8. **Engine conventions.** Architectural change means a new ADR in docs/DECISIONS.md (append-only, take the next number, announce it in the log).
   Capability gaps go in docs/ENGINE_GAPS.md. Scripting API is `forge.*`, bound in `engine/src/scripting/ScriptEngine.cpp`, host hooks implemented in `sandbox/src/main.cpp`.
9. **Verification gate before any push of code.** Build is clean (no new warnings); both test sequencers pass at the documented counts
   (currently 51/51 `tools/dev/test_all.lua` and 126/126 `tools/dev/test_surface.lua`, 111/111 `tools/dev/test_spillway.lua` (about 5 minutes on the owner machine); plus 16/16 `tools/dev/test_mesh.lua` when physics or spawn code changed); no Vulkan validation messages (`vuid`) in the log.
   Docs-only changes skip the gate. Run the tests via the harness in `tools/dev/` (see COLLAB.md section 2).
10. **Blocked by your own machine or permissions** (missing SDK, sandbox denies a command, no glslc, no Vulkan 1.3 GPU, MCP unavailable):
    do NOT work around it destructively, and do not ask the other side's agent to perform a denied action for you.
    Append to docs/COLLAB_LOG.md: the command, the exact error, what you need. Tell your human. The owner's side will do that piece on their machine, or the collaborator's human will change the setting.
11. **Runtime hygiene.** Run the exe from the repo root. `settings.ini` at the root is a real save file: back it up before automated runs and restore after.
    Never run two game instances at once unless `FORGE_TITLE` differs.
