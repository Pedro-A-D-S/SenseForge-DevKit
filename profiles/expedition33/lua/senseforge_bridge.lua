-- SenseForgeBridge — SF-12b fase 2.12: eventos de gameplay do E33 → runtime.
--
-- v2.15 (debrief 19:53 PID 29776): speaker FUNCIONA (bip de teste e ding
--  ouvidos apos o fix do relatorio HID); ding saiu baixo. ui_tick agora vem de
--  WBP_BaseButton_C:BP_OnFocusReceived; ainda sem validacao no jogo.
--
-- v2.14 (debrief 19:42 PID 30472): os 4 hooks Up/Down/Left/RightPressed
--  registraram mas NUNCA dispararam ao navegar em menus, entao o ui_tick segue
--  sem fonte. Hooks so de diagnostico (ui/diag_*) nos candidatos de foco e
--  ativacao; o primeiro que disparar ao mover o cursor vira o novo ui_tick.
--
-- v2.13 (SF-12b speaker, 25/10): eventos so de speaker, sem lightbar/motores.
--  * item_pickup: AC_jRPG_InventoryManager_C:AddItemToInventory e
--    AddItemInstanceToInventory (item/Lumina que entra no inventario).
--  * ui_tick: BP_UINavigationComponent_C:Up/Down/Left/RightPressed (cursor de
--    menu), debounce proprio de 80ms. Nomes vindos do ObjectDump de 24/10;
--    NAO validados no jogo: o diag registra os primeiros disparos de cada hook
--    para conferir se item_pickup nao dispara ao carregar o save.
--
-- v2.12 (SF-15/SF-16, 25/10):
--  * Dumps de objetos/UHT/atores no boot e F7 so com [Dev] ue4ss_dumps=1 no
--    senseforge_user.ini: no pacote publico eles causavam stutter no boot
--    (ObjectDump de ~51 MB) e acumulavam CSVs de atores a cada abertura.
--  * Estudo SF-16 (cor do elemento no QTE perfeito), SO diagnostico, sem
--    efeito novo: BP_Battle_SkillScript_C:ExecuteSkill registra a skill, o
--    personagem e SkillState.ElementalType; BP_QTEManager_C:
--    OnSkillComboAction(IsSuccess, TrackIndex, IsPerfect) registra cada
--    acao de QTE com o ultimo elemento visto.
--
-- v2.11 (debrief 17:44 PID 27536: L2 com resistencia ao mirar, mas o runtime
-- ficava em state=none — o mod nunca emitia e33_free_aim):
--  * Estado e33_free_aim (sf_state) a partir do ObjectDump de 24/10:
--    exploracao BP_FreeAim_C:EnterFreeAim/ExitFreeAim; combate
--    BP_FreeAimShooterBattleComponent_C:EnableFreeAim -> 1 e
--    EndFreeAim/CancelRegularFreeAim/Internal_DisableFreeAim -> 0.
--  * Watchdog de 1s le IsInFreeAimMode (exploracao) / IsAiming (combate) e
--    zera o estado se o jogo sair da mira sem chamar Exit/End. So arma depois
--    de ler true uma vez (nao sabemos ainda em que ponto o BP liga a flag).
--  * BattleStart/BattleStop e troca de mapa zeram e33_free_aim.
--
-- v2.10 (debrief 14:35 PID 29512: parry/dodge/free_aim/hit ponta-a-ponta,
-- gatilhos adaptativos sentidos; dourado aparecia no QTE perfeito do ataque):
--  * FFE_QTE_Perfect -> qte_perfect (efeito proprio no manifesto), nao parry.
--  * hit_taken: OnCharacterReceivedDamages filtrado por
--    TargetCharacterStats:GetOuter().IsPlayerControlledCharacter. FFE_Hit_Base
--    saiu do mapa (toca tambem quando o jogador acerta o inimigo).
--
-- v2.9 (debrief 14:19 PID 21136: parry 3/3 e dodge 2/2 ponta-a-ponta;
-- K2_ClientPlayForceFeedback 20x com args "userdata"):
--  * Causa-raiz do ffe=nil desde a v2.6: UObject do UE4SS nao tem GetName().
--    name_of() agora usa GetFName():ToString() (fallback GetFullName()).
--  * describe() inclui a classe do objeto (Nome<Classe>) para identificar quem
--    tomou dano em OnCharacterReceivedDamages.
--
-- v2.8 (debrief 14:02 PID 17044: parry validado ponta-a-ponta 5/5, flash->HID
-- <=11ms; esquivas e golpes reais sem evento):
--  * Hooks no AC_jRPG_BattleManager_C (dono dos delegates de batalha):
--    OnDodgeSuccess -> dodge, OnGradientCounterSuccess -> gradient,
--    OnParrySuccess -> parry (redundante), e diagnosticos sem emit:
--    OnDodgeAction / OnParryAction (apertou o botao, acertando ou nao) e
--    OnCharacterReceivedDamages (quem tomou dano).
--  * K2_ClientPlayForceFeedback DISPARA em combate (5x com ffe=nil): agora o
--    callback loga o nome de cada argumento (primeiros 20 disparos) e mapeia
--    por nome exato em FFE_MAP, sem exigir o prefixo FFE_.
--
-- v2.7 (debrief da sessão 13:37 PID 27008: parries/golpes/free aim reais e
-- ZERO disparos logados):
--  * Fonte principal agora é a interface BPI_BattleListener, implementada por
--    BP_BattleSummaryStatsManager_C (conta TODO parry/esquiva da batalha) e
--    BP_InteractiveBattleMusicController_C (backup). Os hooks v2.6
--    (PerfectionMechanic / BattleBuffInstance) só disparam com o sistema de
--    Perfection ativo ou um buff reativo a parry — ficam como auxiliares.
--    O parry real nasce no delegate AC_jRPG_BattleManager_C:OnParrySuccessful
--    (MulticastInlineDelegate) e é distribuído aos listeners.
--  * Callbacks do UE4SS recebem RemoteUnrealParam: é preciso :get() antes de
--    GetName()/propriedades. A v2.6 não desembrulhava → o hook do notify de
--    FFE retornava em silêncio. Corrigido em unwrap().
--  * Diagnóstico: as 5 primeiras execuções de cada hook são logadas
--    ("fire #n"), e cada FFE não mapeado é logado na primeira ocorrência.
--  * Debounce por evento (150ms): vários listeners recebem o mesmo parry.
--  * Estado e33_in_combat (sf_state) via BPI_OnBattleStart/OnBattleStop
--    → o state engine aplica o perfil "combat" (gatilho direito rígido).
--
-- Transporte: NDJSON append em <Win64>/senseforge_events.ndjson (o sandbox
-- Lua desta build do UE4SS não tem LuaSocket); o runtime taila o arquivo.
-- Classes BP de batalha podem carregar só no combate: hooks pendentes são
-- re-tentados a cada 15s.

local VERSION = "2.15"

local function log(fmt, ...)
    print(string.format("[SenseForgeBridge] " .. fmt, ...))
end

local EVENTS_FILE = "senseforge_events.ndjson"
local DEBOUNCE_S = 0.15
local DIAG_FIRES = 5
local DIAG_FIRES_FFE = 20

-- FFE name -> SenseForge event id (manifest event_effects).
local FFE_MAP = {
    -- v2.10: FFE_QTE_Perfect e o QTE perfeito do ATAQUE do jogador (nao parry;
    -- o parry real toca FFE_Test). FFE_Hit_Base toca em QUALQUER acerto,
    -- inclusive quando o jogador acerta o inimigo -> hit_taken agora vem de
    -- OnCharacterReceivedDamages filtrado pelo alvo (ver hook mgr/received_damage).
    FFE_QTE_Perfect       = "qte_perfect",
    FFE_FreeAimShoot      = "free_aim_shot",
    FFE_FreeAimExploShoot = "free_aim_shot",
    FFE_ExplorationAttack = "explore_attack",
}

local function append_line(line)
    local ok, err = pcall(function()
        local f = io.open(EVENTS_FILE, "ab")
        if not f then return end
        f:write(line)
        f:close()
    end)
    if not ok then
        log("append falhou: %s", tostring(err))
    end
end

local last_emit = {}

local function emit(event_id, scale, debounce)
    local now = os.clock()
    local prev = last_emit[event_id]
    if prev and (now - prev) < (debounce or DEBOUNCE_S) then
        return false
    end
    last_emit[event_id] = now
    append_line(string.format('{"type":"sf_flash","id":"%s","scale":%.2f}\n',
                              event_id, scale or 1.0))
    return true
end

local function set_state(key, value)
    append_line(string.format('{"type":"sf_state","id":"%s","value":%.2f}\n',
                              key, value))
    log("state %s=%.2f", key, value)
end

-- ---- Desembrulho de parâmetros e extração do FFE ------------------------
local function unwrap(a)
    if type(a) ~= "userdata" then
        return a
    end
    local ok, v = pcall(function() return a:get() end)
    if ok and v ~= nil then
        return v
    end
    return a
end

-- UObject do UE4SS NAO tem GetName(): o nome vem de GetFName():ToString().
-- Fallback: ultimo segmento de GetFullName() ("Classe /Path/Pkg.Nome").
local function name_of(obj)
    local ok, name = pcall(function() return obj:GetFName():ToString() end)
    if ok and type(name) == "string" and name ~= "" and name ~= "None" then
        return name
    end
    local ok2, full = pcall(function() return obj:GetFullName() end)
    if ok2 and type(full) == "string" and full ~= "" then
        return full:match("([^%.:]+)$") or full
    end
    return nil
end

local function class_of(obj)
    local ok, n = pcall(function() return obj:GetClass():GetFName():ToString() end)
    if ok and type(n) == "string" then return n end
    return nil
end

local function ffe_from(obj)
    if type(obj) ~= "userdata" then
        return nil
    end
    local n = name_of(obj)
    if n and (n:sub(1, 4) == "FFE_" or FFE_MAP[n]) then
        return n
    end
    -- notify carregando o FFE como propriedade?
    local ok, ffe = pcall(function()
        return obj["Force Feedback Effect"]
    end)
    if ok and ffe ~= nil then
        n = name_of(unwrap(ffe))
        if n and n:sub(1, 4) == "FFE_" then
            return n
        end
    end
    return nil
end

local function describe(v)
    v = unwrap(v)
    local t = type(v)
    if t ~= "userdata" then
        return t .. ":" .. tostring(v)
    end
    local n = name_of(v)
    if n then
        local c = class_of(v)
        if c and c ~= n then return n .. "<" .. c .. ">" end
        return n
    end
    local ok, s = pcall(function() return v:ToString() end)
    if ok and type(s) == "string" then return "str:" .. s end
    return "userdata"
end

local function describe_args(args)
    local parts = {}
    for i = 1, #args do
        parts[#parts + 1] = describe(args[i])
    end
    return table.concat(parts, " | ")
end

local function find_ffe_name(args)
    for i = 1, #args do
        local a = unwrap(args[i])
        if type(a) == "table" then
            for _, v in pairs(a) do
                local n = ffe_from(unwrap(v))
                if n then return n end
            end
        else
            local n = ffe_from(a)
            if n then return n end
        end
    end
    return nil
end

-- ---- Bateria de hooks ----------------------------------------------------
-- kind: "event" (emit direto) | "ffe" (extrai FFE e mapeia) | "state"
local SUMMARY = "/Game/Gameplay/Battle/BattleStats/BP_BattleSummaryStatsManager.BP_BattleSummaryStatsManager_C:"
local MGR = "/Game/jRPGTemplate/Blueprints/Components/AC_jRPG_BattleManager.AC_jRPG_BattleManager_C:"
local MUSIC = "/Game/Gameplay/InteractiveMusic/BP_InteractiveBattleMusicController.BP_InteractiveBattleMusicController_C:"
local FREEAIM = "/Game/Gameplay/FreeAim/BP_FreeAim.BP_FreeAim_C:"
local FREEAIM_BATTLE = "/Game/Gameplay/FreeAim/BP_FreeAimShooterBattleComponent.BP_FreeAimShooterBattleComponent_C:"

local INVENTORY = "/Game/jRPGTemplate/Blueprints/Components/AC_jRPG_InventoryManager.AC_jRPG_InventoryManager_C:"
local BTN = "/Game/UI/Widgets/CommonElements/Buttons/WBP_BaseButton.WBP_BaseButton_C:"
local UINAV = "/Game/Gameplay/UINavigation/BP_UINavigationComponent.BP_UINavigationComponent_C:"

local HOOKS = {
    -- fonte principal (BPI_BattleListener)
    { tag = "summary/parry", path = SUMMARY .. "OnSuccessfulParry",
      kind = "event", event = "parry" },
    { tag = "summary/dodge", path = SUMMARY .. "OnSuccessfulDodge",
      kind = "event", event = "dodge" },
    { tag = "summary/battle_start", path = SUMMARY .. "BPI_OnBattleStart",
      kind = "state", key = "e33_in_combat", value = 1.0, reset_free_aim = true },
    { tag = "summary/battle_stop", path = SUMMARY .. "OnBattleStop",
      kind = "state", key = "e33_in_combat", value = 0.0, reset_free_aim = true },
    -- free aim (v2.11): exploracao e combate sao sistemas separados
    { tag = "freeaim/enter", path = FREEAIM .. "EnterFreeAim",
      kind = "free_aim", value = 1.0, flag = "IsInFreeAimMode" },
    { tag = "freeaim/exit", path = FREEAIM .. "ExitFreeAim",
      kind = "free_aim", value = 0.0 },
    { tag = "freeaim/toggle", path = FREEAIM .. "ToggleFreeAim", kind = "diag" },
    { tag = "freeaim_battle/enable", path = FREEAIM_BATTLE .. "EnableFreeAim",
      kind = "free_aim", value = 1.0, flag = "IsAiming" },
    { tag = "freeaim_battle/end", path = FREEAIM_BATTLE .. "EndFreeAim",
      kind = "free_aim", value = 0.0 },
    { tag = "freeaim_battle/cancel", path = FREEAIM_BATTLE .. "CancelRegularFreeAim",
      kind = "free_aim", value = 0.0 },
    { tag = "freeaim_battle/disable", path = FREEAIM_BATTLE .. "Internal_DisableFreeAim",
      kind = "free_aim", value = 0.0 },
    -- backup (mesmo evento; debounce colapsa duplicatas)
    { tag = "music/parry", path = MUSIC .. "OnSuccessfulParry",
      kind = "event", event = "parry" },
    { tag = "music/dodge", path = MUSIC .. "OnSuccessfulDodge",
      kind = "event", event = "dodge" },
    -- dono dos delegates de batalha (v2.8)
    { tag = "mgr/dodge_success", path = MGR .. "OnDodgeSuccess",
      kind = "event", event = "dodge" },
    { tag = "mgr/gradient_counter", path = MGR .. "OnGradientCounterSuccess",
      kind = "event", event = "gradient" },
    { tag = "mgr/parry_success", path = MGR .. "OnParrySuccess",
      kind = "event", event = "parry" },
    { tag = "mgr/dodge_action", path = MGR .. "OnDodgeAction", kind = "diag" },
    { tag = "mgr/parry_action", path = MGR .. "OnParryAction", kind = "diag" },
    { tag = "mgr/received_damage", path = MGR .. "OnCharacterReceivedDamages",
      kind = "damage" },
    -- speaker (v2.13): eventos sem lightbar/motores
    { tag = "inv/add_item", path = INVENTORY .. "AddItemToInventory",
      kind = "event", event = "item_pickup", debounce = 0.4 },
    { tag = "inv/add_item_instance", path = INVENTORY .. "AddItemInstanceToInventory",
      kind = "event", event = "item_pickup", debounce = 0.4 },
    -- ui_tick (v2.15): botoes do menu recebem foco (BP_OnFocusReceived) -- os
    -- hooks Up/Down/Left/RightPressed e os 5 diag da v2.14 nunca dispararam.
    { tag = "ui/focus_received", path = BTN .. "BP_OnFocusReceived",
      kind = "event", event = "ui_tick", debounce = 0.08 },
    { tag = "ui/diag_hovered", path = BTN .. "BP_OnHovered", kind = "diag" },
    { tag = "ui/diag_common_focus",
      path = "/Script/CommonUI.CommonButtonBase:HandleFocusReceived", kind = "diag" },
    { tag = "ui/diag_items_focus",
      path = "/Game/UI/Widgets/InGame_Menu/Items/WBP_Items_Panel.WBP_Items_Panel_C:OnItemFocused",
      kind = "diag" },
    -- estudo SF-16 (v2.12): so diagnostico
    { tag = "skill/execute",
      path = "/Game/Gameplay/Battle/Skills/BP_Battle_SkillScript.BP_Battle_SkillScript_C:ExecuteSkill",
      kind = "skill_diag" },
    { tag = "qte/combo_action",
      path = "/Game/Gameplay/QTE/BP_QTEManager.BP_QTEManager_C:OnSkillComboAction",
      kind = "qte_diag" },
    -- auxiliares v2.6
    { tag = "parry/perfection",
      path = "/Game/Gameplay/Battle/Perfection/BP_PerfectionMechanicComponent.BP_PerfectionMechanicComponent_C:OnParrySuccessful",
      kind = "event", event = "parry" },
    { tag = "parry/buff",
      path = "/Game/Gameplay/Buffs/BP_BattleBuffInstance.BP_BattleBuffInstance_C:OnSuccessfulParry",
      kind = "event", event = "parry" },
    { tag = "notify/camshake+ffe",
      path = "/Game/Cinematics/CameraShakes/Notify/CamShakeAndForceFeedback_Notify.CamShakeAndForceFeedback_Notify_C:Received_Notify",
      kind = "ffe" },
    { tag = "pc/k2clientplayff",
      path = "/Script/Engine.PlayerController:K2_ClientPlayForceFeedback",
      kind = "ffe" },
    { tag = "pc/playdynamicff",
      path = "/Script/Engine.PlayerController:PlayDynamicForceFeedback",
      kind = "ffe" },
}

local fires = {}
-- SF-16: ultimo elemento/skill vistos em ExecuteSkill (so diagnostico).
sf16_last_element = nil
sf16_last_skill = nil
local unmapped_seen = {}

-- ---- Estado e33_free_aim + watchdog --------------------------------------
local WATCHDOG_MS = 1000
local free_aim_on = false
local free_aim_owner = nil   -- UObject dono da mira (BP_FreeAim / componente)
local free_aim_flag = nil    -- propriedade bool lida pelo watchdog
local free_aim_armed = false -- flag ja foi lida como true nesta mira
local free_aim_gen = 0       -- invalida watchdogs de miras anteriores

local function set_free_aim(on, why)
    free_aim_gen = free_aim_gen + 1
    if not on then
        free_aim_owner, free_aim_flag, free_aim_armed = nil, nil, false
    end
    if on == free_aim_on then return end
    free_aim_on = on
    set_state("e33_free_aim", on and 1.0 or 0.0)
    log("free aim %s (%s)", on and "on" or "off", why)
end

local function read_flag(obj, flag)
    local ok, v = pcall(function()
        if not obj:IsValid() then return nil end
        return obj[flag]
    end)
    if ok and type(v) == "boolean" then return v end
    return nil
end

local function watchdog_tick(gen)
    if gen ~= free_aim_gen or not free_aim_on or free_aim_owner == nil then
        return
    end
    local v = read_flag(free_aim_owner, free_aim_flag)
    if v == true then
        free_aim_armed = true
    elseif free_aim_armed then
        -- false depois de ter sido true, ou objeto invalido (nil)
        set_free_aim(false, "watchdog " .. free_aim_flag .. "=" .. tostring(v))
        return
    end
    ExecuteWithDelay(WATCHDOG_MS, function()
        pcall(ExecuteInGameThread, function() watchdog_tick(gen) end)
    end)
end

local function diag(hook, extra, limit)
    local n = (fires[hook.tag] or 0) + 1
    fires[hook.tag] = n
    if n <= (limit or DIAG_FIRES) then
        log("fire #%d %s%s", n, hook.tag, extra or "")
    end
end

local function make_callback(hook)
    if hook.kind == "event" then
        return function(...)
            diag(hook)
            if emit(hook.event, nil, hook.debounce) then
                log("%s -> %s", hook.tag, hook.event)
            end
        end
    end
    if hook.kind == "diag" then
        return function(...)
            diag(hook)
        end
    end
    if hook.kind == "damage" then
        -- Args: Context, TargetCharacterStats, Damages, SourceCharacterStats,
        -- BattleDamages. O alvo e um componente; o Outer e o personagem
        -- (BP_jRPG_Character_Battle_Base_C, bool IsPlayerControlledCharacter).
        return function(...)
            local args = {...}
            local target = unwrap(args[2])
            local actor, is_player = nil, nil
            pcall(function() actor = target:GetOuter() end)
            if actor ~= nil then
                local ok, v = pcall(function() return actor.IsPlayerControlledCharacter end)
                if ok and type(v) == "boolean" then is_player = v end
            end
            diag(hook, string.format(" alvo=%s player=%s dano=%s fonte=%s",
                 describe(actor), tostring(is_player), describe(args[3]),
                 describe(args[4])), DIAG_FIRES_FFE)
            -- nil (leitura falhou) cai no comportamento antigo: emite.
            if is_player ~= false then
                if emit("hit_taken") then
                    log("%s -> hit_taken (player=%s)", hook.tag, tostring(is_player))
                end
            end
        end
    end
    if hook.kind == "skill_diag" then
        -- self = BP_Battle_SkillScript (subclasse por skill); SkillState e
        -- CurrentCharacter sao propriedades do script.
        return function(self_param, ...)
            local script = unwrap(self_param)
            local skill, element, character = nil, nil, nil
            pcall(function()
                local state = script.SkillState
                if state ~= nil and state:IsValid() then
                    element = state.ElementalType
                    skill = describe(state.SkillDefinition)
                end
            end)
            pcall(function() character = describe(script.CurrentCharacter) end)
            sf16_last_element = element
            sf16_last_skill = skill
            diag(hook, string.format(" script=%s skill=%s elem=%s char=%s",
                 describe(script), tostring(skill), tostring(element),
                 tostring(character)), 200)
        end
    end
    if hook.kind == "qte_diag" then
        return function(self_param, is_success, track_index, is_perfect)
            local ok_s, success = pcall(function() return unwrap(is_success) end)
            local ok_p, perfect = pcall(function() return unwrap(is_perfect) end)
            local ok_t, track = pcall(function() return unwrap(track_index) end)
            diag(hook, string.format(
                 " success=%s perfect=%s track=%s ultimo_elem=%s ultima_skill=%s",
                 tostring(ok_s and success), tostring(ok_p and perfect),
                 tostring(ok_t and track), tostring(sf16_last_element),
                 tostring(sf16_last_skill)), 400)
        end
    end
    if hook.kind == "diag_args" then
        return function(...)
            diag(hook, " args=" .. describe_args({...}), DIAG_FIRES_FFE)
        end
    end
    if hook.kind == "state" then
        return function(...)
            diag(hook)
            set_state(hook.key, hook.value)
            if hook.reset_free_aim then
                set_free_aim(false, hook.tag)
            end
        end
    end
    if hook.kind == "free_aim" then
        -- Primeiro argumento do callback e o self do BP (RemoteUnrealParam).
        return function(self_param, ...)
            local owner = unwrap(self_param)
            local flag_now = hook.flag and read_flag(owner, hook.flag)
            diag(hook, string.format(" self=%s %s=%s", describe(owner),
                 tostring(hook.flag), tostring(flag_now)), DIAG_FIRES_FFE)
            if hook.value > 0 then
                set_free_aim(true, hook.tag)
                free_aim_owner, free_aim_flag = owner, hook.flag
                free_aim_armed = (flag_now == true)
                local gen = free_aim_gen
                ExecuteWithDelay(WATCHDOG_MS, function()
                    pcall(ExecuteInGameThread, function() watchdog_tick(gen) end)
                end)
            else
                set_free_aim(false, hook.tag)
            end
        end
    end
    return function(...)
        local args = {...}
        local ffe_name = find_ffe_name(args)
        diag(hook, " ffe=" .. tostring(ffe_name) .. " args=" .. describe_args(args),
             DIAG_FIRES_FFE)
        if not ffe_name then
            return
        end
        local event_id = FFE_MAP[ffe_name]
        if event_id then
            if emit(event_id) then
                log("%s: FFE %s -> %s", hook.tag, ffe_name, event_id)
            end
        elseif not unmapped_seen[ffe_name] then
            unmapped_seen[ffe_name] = true
            log("%s: FFE nao mapeado %s", hook.tag, ffe_name)
        end
    end
end

local pending = {}
for i = 1, #HOOKS do pending[i] = i end

local function try_register()
    local still = {}
    for _, idx in ipairs(pending) do
        local hook = HOOKS[idx]
        local ok = pcall(function()
            RegisterHook(hook.path, make_callback(hook))
        end)
        if ok then
            log("hook %s registrado", hook.tag)
        else
            still[#still + 1] = idx
        end
    end
    pending = still
    if #pending > 0 then
        log("%d hook(s) pendentes (classes BP carregam no combate); retry 15s",
            #pending)
        ExecuteWithDelay(15000, try_register)
    else
        log("todos os hooks registrados")
    end
end

try_register()

-- Troca de mapa = fora de combate (cobre fuga/derrota sem OnBattleStop).
pcall(function()
    RegisterLoadMapPostHook(function()
        set_state("e33_in_combat", 0.0)
        set_free_aim(false, "load_map")
    end)
end)

-- ---- Dumps: menu aos 8s; F7 re-dump durante o combate --------------------
local function run_all_dumps()
    local ok1 = pcall(DumpAllObjects)
    log("DumpAllObjects: %s", tostring(ok1))
    local ok2 = pcall(GenerateUHTCompatibleHeaders)
    log("GenerateUHTCompatibleHeaders: %s", tostring(ok2))
    local ok3 = pcall(DumpAllActors)
    log("DumpAllActors: %s", tostring(ok3))
end

-- v2.12: [Dev] ue4ss_dumps=1 no senseforge_user.ini (CWD = Win64). Sem a
-- flag (pacote publico) nada e despejado: os dumps travavam o boot.
local function dev_dumps_enabled()
    local ok, enabled = pcall(function()
        local f = io.open("senseforge_user.ini", "r")
        if not f then return false end
        local section, found = nil, false
        for line in f:lines() do
            local s = line:match("^%s*%[([^%]]+)%]")
            if s then
                section = s
            elseif section == "Dev" then
                local v = line:match("^%s*ue4ss_dumps%s*=%s*(%S+)")
                if v then found = (v == "1" or v == "true") end
            end
        end
        f:close()
        return found
    end)
    return ok and enabled
end

if dev_dumps_enabled() then
    ExecuteWithDelay(8000, run_all_dumps)
    local kb_ok = pcall(function()
        RegisterKeyBind(0x77, function()  -- F7
            log("F7: re-dump durante combate")
            run_all_dumps()
        end)
    end)
    log("dumps de dev ligados ([Dev] ue4ss_dumps=1); keybind F7: %s",
        tostring(kb_ok))
else
    log("dumps de dev desligados (pacote publico)")
end

log("fase %s ativa: bateria de hooks, saida %s", VERSION, EVENTS_FILE)
