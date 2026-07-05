# VAULT — Looping Puzzle Rooms (week 8 candidates)

Design bank for the next content pass. "Looping" here means the SPACE or the
LOGIC cycles — the player re-traverses ground that has changed, instead of
marching ever north. Every idea below is buildable with the existing
vocabulary (plates, timed plates, crates, lasers, glass, anim clips, drone,
checkpoints/teleport); engine gaps, where any, are called out per idea.

## 1. The Atrium Loop (hub-and-spoke)
A central chamber with three locked wings. Each wing's puzzle ends at a drop
chute that sends its crate back down into the hub. Three hub plates need
those three crates. The player keeps RETURNING to a hub that has visibly
changed (crates accumulate, doors unlock). Loop count: 3 laps minimum.
- Build cost: pure Lua data (rooms are already spawn tables).
- Engine gap: none. Chutes are sloped static boxes; crates slide.

## 2. Figure-8
Two chambers wrapped around a shared centre wall with a balcony. Ground floor
of A -> stairs in B -> balcony OVER A -> throw the crate down onto A's plate
-> drop after it and walk the now-open door back into B. The player crosses
the same airspace twice at different heights.
- Build cost: pure Lua; stairs are step boxes (step-offset already proven).
- Engine gap: none.

## 3. The Carousel
A keyframed platform cycles four stations (loop clip). Ride it around once to
REACH the crate island, then a second lap to carry the crate past a laser
that sweeps the floor — only the platform's height clears the beam. Timing
loop: the platform is the clock.
- Build cost: one anim clip + Lua.
- Engine gap: the noted ADR-020 follow-up — a KINEMATIC platform body that
  pushes riders. Today's teleport-following collider can leave the player
  behind on fast segments. Prototype slow; promote the kinematic body if it
  reads badly.

## 4. One-Way Drops (respawn as mechanic)
Marked floor holes are INTENTIONAL: dropping through one respawns the player
at the room checkpoint, but a crate dropped first LANDS in a lower copy of
the room and stays. Solve by choosing which holes get crates before you
commit yourself. The loop is player-fall -> respawn -> approach again.
- Build cost: Lua only, but needs a small change to updateRespawn: a
  per-region crate policy (crates inside marked drop zones do NOT respawn).
- Engine gap: none.

## 5. The Relay Ring
A beam circles the room through four emitter/receiver relays. Blocking
segment N opens door N but CLOSES door N-1 — one crate, four segments, so the
player ferries it around the ring, looping the room in order and losing each
door behind them until the exit segment.
- Build cost: pure Lua (lasers are already generic + per-door conds).
- Engine gap: none.

## Selection lean (when week 8 starts)
Pick TWO: the Atrium Loop (structural novelty, zero engine risk) and the
Relay Ring (mechanical novelty, zero engine risk). The Carousel waits for the
kinematic body; One-Way Drops slot into either as a garnish; Figure-8 is the
spare if a pick underperforms in grey-box.

## Built (week 8)
- **Relay Ring -> R9**, as a C-shaped variant: the east side is a solid
  block, so the "ring" is S lane -> W lane -> N lane around it. Knee-high
  (y 0.35) beams so a GROUNDED crate cuts them; each gate is held open by
  the beam behind it — place the crate, walk through, glove-pull the crate
  through the open doorway. The gate closing at your heels is the loop.
  Full four-relay ring reserved for an art-pass remix.
- **Atrium Loop -> R10**, as a 16 m hub with three alcove wings (drone-park
  cage / timed body-press cage / glass you spend a retrieved crate on) and a
  three-plate exit row. Wings return crates to the hub by hand rather than
  drop chutes — the chute garnish joins when a vertical room exists.
Still banked: Carousel (waits on the kinematic platform body), One-Way
Drops, Figure-8.
