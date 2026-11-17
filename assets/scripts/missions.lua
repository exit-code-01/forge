-- assets/scripts/missions.lua — the mission + interact layer (ADR-031).
-- Included by scene.lua (forge.include) after it builds the VAULT kit.
--
-- A mission is DATA: id, title, objectives (each either a predicate `done()`
-- polled every frame, or completed explicitly with Missions.complete), an
-- `ordered` flag, optional callbacks. Objectives latch: once done they stay
-- done until the run resets. `optional` objectives never block completion.
--
-- State lives in MISSION_MEM (a global, so a hot reload keeps it) and is
-- persisted to the forge.game number store as mis_<id>_st (0 idle,
-- 1 active, 2 complete) and mis_<id>_obj (objective bitmask). Every change
-- saves; Missions.load() is called by the location that owns a checkpoint
-- (Continue). Predicate objectives re-derive on their own.

MISSION_MEM = MISSION_MEM or {}

Missions = { list = {}, byId = {}, tracked = nil, holdT = 0, hudKey = nil }

local function mem(id)
    local m = MISSION_MEM[id]
    if not m then
        m = { st = 0, done = {} }
        MISSION_MEM[id] = m
    end
    return m
end

function Missions.define(def)
    def.objIndex = {}
    for i, o in ipairs(def.objectives) do
        o.bit = i
        def.objIndex[o.id] = o
    end
    Missions.list[#Missions.list + 1] = def
    Missions.byId[def.id] = def
    return def
end

function Missions.status(id) return mem(id).st end
function Missions.active(id) return mem(id).st == 1 end
function Missions.isDone(id, oid) return mem(id).done[oid] == true end

-- ---- persistence ----------------------------------------------------------
local function bits(def)
    local m, b = mem(def.id), 0
    for _, o in ipairs(def.objectives) do
        if m.done[o.id] then b = b + 2 ^ (o.bit - 1) end
    end
    return b
end

function Missions.save()
    for _, def in ipairs(Missions.list) do
        forge.game.save("mis_" .. def.id .. "_st", mem(def.id).st)
        forge.game.save("mis_" .. def.id .. "_obj", bits(def))
    end
end

function Missions.load()
    for _, def in ipairs(Missions.list) do
        local m = mem(def.id)
        m.st = math.floor(forge.game.load("mis_" .. def.id .. "_st") or 0)
        local b = math.floor(forge.game.load("mis_" .. def.id .. "_obj") or 0)
        m.done = {}
        for _, o in ipairs(def.objectives) do
            if math.floor(b / 2 ^ (o.bit - 1)) % 2 == 1 then m.done[o.id] = true end
        end
        if m.st == 1 then Missions.tracked = def.id end
    end
    Missions.hudKey = nil
    forge.log("missions: loaded from save")
end

-- ---- HUD ------------------------------------------------------------------
local function textOf(o)
    return type(o.text) == "function" and o.text() or o.text
end

local function pushHud()
    local def = Missions.tracked and Missions.byId[Missions.tracked]
    if not def then
        if Missions.hudKey ~= "" then
            Missions.hudKey = ""
            forge.hud.set_mission("", {})
        end
        return
    end
    local m = mem(def.id)
    local lines = {}
    for _, o in ipairs(def.objectives) do
        if not (o.hidden and o.hidden()) then
            lines[#lines + 1] = (m.done[o.id] and "[x] " or "[ ] ") .. textOf(o)
        end
    end
    local title = def.title .. (def.sub and ("  -  " .. def.sub) or "")
    if m.st == 2 then title = title .. "  (done)" end
    local key = title .. "|" .. table.concat(lines, "|")
    if key ~= Missions.hudKey then -- push only on change
        Missions.hudKey = key
        forge.hud.set_mission(title, lines)
    end
end

-- ---- progress -----------------------------------------------------------
local function finished(def)
    local m = mem(def.id)
    for _, o in ipairs(def.objectives) do
        if not o.optional and not m.done[o.id] then return false end
    end
    return true
end

local function markDone(def, o, quiet)
    local m = mem(def.id)
    if m.done[o.id] then return false end
    m.done[o.id] = true
    forge.log("mission " .. def.id .. ": objective " .. o.id .. " done")
    if not quiet then
        forge.hud.toast(def.title .. ":  " .. textOf(o), 3.0)
        forge.audio.play("assets/sounds/chime.wav", 0.7, 1.2)
    end
    if def.onObjective then def.onObjective(o.id, quiet) end
    if m.st == 1 and finished(def) then
        m.st = 2
        Missions.holdT = 6.0 -- the finished panel lingers, then gives way
        forge.log("mission " .. def.id .. ": COMPLETE")
        if not quiet then
            forge.hud.toast("MISSION COMPLETE:  " .. def.title, 4.0)
            forge.audio.play("assets/sounds/secret.wav", 0.8)
        end
        if def.onComplete then def.onComplete(quiet) end
    end
    return true
end

-- Ordered missions poll only their first open objective (plus optional
-- ones); unordered missions poll every open one.
local function poll(def, quiet)
    local m = mem(def.id)
    if m.st ~= 1 then return false end
    local changed = false
    for _, o in ipairs(def.objectives) do
        if not m.done[o.id] then
            if o.done and o.done() then
                changed = markDone(def, o, quiet) or changed
            elseif def.ordered and not o.optional then
                break
            end
        end
    end
    return changed
end

function Missions.start(id, quiet)
    local def = Missions.byId[id]
    local m = mem(id)
    if not def or m.st ~= 0 then return false end
    m.st = 1
    Missions.tracked = id
    Missions.holdT = 0
    forge.log("mission " .. id .. ": started")
    if not quiet then
        forge.hud.toast("NEW MISSION:  " .. def.title .. (def.sub and (" - " .. def.sub) or ""), 4.0)
        forge.audio.play("assets/sounds/power_up.wav", 0.5, 1.3)
    end
    poll(def, true)
    Missions.save()
    pushHud()
    return true
end

-- Explicit completion (talk-to, use-this). Ordered missions refuse an
-- objective whose predecessors are open.
function Missions.complete(id, oid, quiet)
    local def = Missions.byId[id]
    if not def or mem(id).st ~= 1 then return false end
    local o = def.objIndex[oid]
    if not o then return false end
    if def.ordered and not o.optional then
        for _, prev in ipairs(def.objectives) do
            if prev == o then break end
            if not prev.optional and not mem(id).done[prev.id] then return false end
        end
    end
    local ok = markDone(def, o, quiet)
    if ok then
        Missions.save()
        pushHud()
    end
    return ok
end

function Missions.update(dt)
    local changed = false
    for _, def in ipairs(Missions.list) do
        changed = poll(def, false) or changed
    end
    if changed then Missions.save() end
    -- a finished tracked mission lingers, then the next active one shows
    local t = Missions.tracked and Missions.byId[Missions.tracked]
    if t and mem(t.id).st == 2 then
        Missions.holdT = Missions.holdT - dt
        if Missions.holdT <= 0 then
            Missions.tracked = nil
            for _, def in ipairs(Missions.list) do
                if mem(def.id).st == 1 then Missions.tracked = def.id end
            end
        end
    end
    pushHud()
end

-- Re-derive now, silently (Continue lands deep: predicates catch up).
function Missions.refresh()
    for _, def in ipairs(Missions.list) do poll(def, true) end
    Missions.save()
    Missions.hudKey = nil
    pushHud()
end

-- A run restart: every mission back to idle, autoStart ones begin again.
function Missions.reset()
    for _, def in ipairs(Missions.list) do
        MISSION_MEM[def.id] = { st = 0, done = {} }
    end
    Missions.tracked = nil
    for _, def in ipairs(Missions.list) do
        if def.autoStart then Missions.start(def.id, true) end
    end
    Missions.save()
    Missions.hudKey = nil
    pushHud()
end

-- Process start: memory only (the title menu has not chosen Play/Continue
-- yet, so the save is left alone). Hot reload: MISSION_MEM survives.
function Missions.onStart()
    if not MISSION_BOOTED then
        MISSION_BOOTED = true
        for _, def in ipairs(Missions.list) do
            MISSION_MEM[def.id] = { st = def.autoStart and 1 or 0, done = {} }
        end
    end
    Missions.tracked = nil
    for _, def in ipairs(Missions.list) do
        if mem(def.id).st == 1 then Missions.tracked = def.id end
    end
    Missions.hudKey = nil
    pushHud()
end

-- =========================================================================
-- Interact: the E verb. Interactables are {id, pos (vec3 or fn), radius,
-- prompt (string or fn), enabled (fn, optional), use (fn), body (optional:
-- the name of its own collider, which never blocks it)}. The nearest enabled
-- one in reach that the player is roughly facing AND can see owns the prompt;
-- E (scene.lua) calls Interact.press(), which is also the path tests call.
-- =========================================================================
Interact = { list = {}, cur = nil, shown = nil }

function Interact.add(def)
    Interact.list[#Interact.list + 1] = def
    return def
end

local function posOf(it) return type(it.pos) == "function" and it.pos() or it.pos end

-- Line of sight (O3): a prompt must not show through terrain. Only STATIC
-- world geometry blocks it (the Surface's "Srf ..." boxes and the dressing's
-- colliders). The interactable's own collider (def.body), SPARK, cells and
-- anything carried never do, so a held cell cannot hide the prompt.
local function isWall(name)
    return name:sub(1, 4) == "Srf " or name:sub(1, 10) == "Dress Col "
end
local function blocked(it, eye, ex, ey, ez, el)
    if el < 0.6 then return false end
    local id, hd = forge.physics.raycast(eye, vec3(ex / el, ey / el, ez / el), el)
    if not id or id == 0 or (hd and hd > el - 0.2) then return false end
    local name = forge.scene.bodyName(id) or ""
    return name ~= it.body and isWall(name)
end

function Interact.update(dt, pp)
    local eye, dir = forge.player.look()
    local best, bestD = nil, 1e9
    for _, it in ipairs(Interact.list) do
        if not it.enabled or it.enabled() then
            local at = posOf(it)
            local dx, dy, dz = at.x - pp.x, at.y - pp.y, at.z - pp.z
            local d = math.sqrt(dx * dx + dz * dz)
            if d <= (it.radius or 2.5) and math.abs(dy) < 2.5 then
                -- facing test on the eye ray (close enough always counts)
                local ex, ey, ez = at.x - eye.x, at.y - eye.y, at.z - eye.z
                local el = math.sqrt(ex * ex + ey * ey + ez * ez)
                local facing = el < 1.2 or (ex * dir.x + ey * dir.y + ez * dir.z) / el > 0.55
                if facing and d < bestD and not blocked(it, eye, ex, ey, ez, el) then
                    best, bestD = it, d
                end
            end
        end
    end
    Interact.cur = best
    local text = ""
    if best then
        text = type(best.prompt) == "function" and best.prompt() or best.prompt
    end
    if text ~= Interact.shown then
        Interact.shown = text
        forge.hud.set_prompt(text)
    end
end

-- Returns true when something was used (E was consumed).
function Interact.press()
    local it = Interact.cur
    if not it then return false end
    forge.log("interact: " .. it.id)
    it.use()
    return true
end

-- =========================================================================
-- ASCEND (retro-fitted onto the facility): one objective per zone, driven
-- by the checkpoint ladder that already exists (VAULT.cp(): 6 = R5's door,
-- 12 = R11's, 16 = R15's; past the facility's last = on the Surface).
-- =========================================================================
local function cp() return VAULT.cp() end
Missions.define({
    id = "ascend", title = "ASCEND", sub = "reach the surface", ordered = true, autoStart = true,
    objectives = {
        { id = "storage",  text = "Storage  (R1 - R4)",       done = function() return cp() >= 6 end },
        { id = "reactor",  text = "Reactor wing  (R5 - R10)", done = function() return cp() >= 12 end },
        { id = "launch",   text = "Launch pass  (R11 - R14)", done = function() return cp() >= 16 end },
        { id = "assembly", text = "Assembly  (R15 - R17)",
          done = function() return VAULT.state.ascending == true or cp() > VAULT.facilityCps end },
        { id = "surface",  text = "Ride the freight lift up",
          done = function() return cp() > VAULT.facilityCps end },
    },
})
