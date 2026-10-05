#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace RC {
    namespace LuaMadeSimple {
        class Lua {
        public:
            __declspec(dllimport) void execute_string(std::string_view) const;
        };
    }

    class CppUserModBase {
    protected:
        std::vector<std::shared_ptr<void>> GUITabs{};

    public:
        std::wstring ModName{};
        std::wstring ModVersion{};
        std::wstring ModDescription{};
        std::wstring ModAuthors{};
        std::wstring ModIntendedSDKVersion{};

    public:
        __declspec(dllimport) CppUserModBase();
        __declspec(dllimport) virtual ~CppUserModBase(); // Slot 0 (0x00)

        virtual void on_update() {} // Slot 1 (0x08)
        virtual void on_unreal_init() {} // Slot 2 (0x10)
        virtual void on_ui_init() {} // Slot 3 (0x18)
        virtual void on_program_start() {} // Slot 4 (0x20)

        // Slot 5 (0x28): on_lua_start without mod_name
        virtual void vslot5_on_lua_start(LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua*) {}
        // Slot 6 (0x30): on_lua_start with mod_name (called by UE4SS at [rax+30h]!)
        virtual void vslot6_on_lua_start(std::wstring_view, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua*) {}
        // Slot 7 (0x38): on_lua_start vector without mod_name
        virtual void vslot7_on_lua_start_vec(LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, std::vector<LuaMadeSimple::Lua*>&) {}
        // Slot 8 (0x40): on_lua_start vector with mod_name
        virtual void vslot8_on_lua_start_vec(std::wstring_view, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, std::vector<LuaMadeSimple::Lua*>&) {}

        // Slot 9 (0x48): on_lua_stop without mod_name
        virtual void vslot9_on_lua_stop(LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua*) {}
        // Slot 10 (0x50): on_lua_stop with mod_name
        virtual void vslot10_on_lua_stop(std::wstring_view, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua*) {}
        // Slot 11 (0x58): on_lua_stop vector without mod_name
        virtual void vslot11_on_lua_stop_vec(LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, std::vector<LuaMadeSimple::Lua*>&) {}
        // Slot 12 (0x60): on_lua_stop vector with mod_name
        virtual void vslot12_on_lua_stop_vec(std::wstring_view, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, LuaMadeSimple::Lua&, std::vector<LuaMadeSimple::Lua*>&) {}

        virtual void on_dll_load(std::wstring_view) {} // Slot 13 (0x68)
        virtual void render_tab() {} // Slot 14 (0x70)
        virtual void on_cpp_mods_loaded() {} // Slot 15 (0x78)
    };
}

// Internal engine script embedded and executed in memory (protected from plain-text viewing)
static const char* LOOTYIELD_ENGINE_PAYLOAD = R"LUA(
if _G.__LOOTYIELD_LOADED then return end
_G.__LOOTYIELD_LOADED = true

local TAG = "[LootYield] "
local function log(msg) print(TAG .. tostring(msg) .. "\n") end

local function valid(o)
    if not o then return false end
    local ok, v = pcall(function() return o:IsValid() end)
    return ok and v
end

local function fname(o)
    local ok, n = pcall(function() return o:GetFName():ToString() end)
    return ok and tostring(n) or "?"
end

--------------------------------------------------------------------------------
-- 1. Configuration: YieldSliders & LootSliders
--------------------------------------------------------------------------------
local yieldCfg = {
    Multiplier       = 2.0,
    WindowSeconds    = 3.0,
    JoinGraceSeconds = 10.0,
    Verbose          = false,
    PerSkill         = {},
    Station          = {},
}

local lootCfg = {
    Multiplier       = 1.0,
    ChanceMultiplier = 1.0,
    SkipQuestItems   = true,
    IncludeMounts    = false,
    IncludeVestiges  = false,
    Verbose          = false,
}

local SKILL_KEYS = {
    mining = "SKILL_Mining", woodcutting = "SKILL_Woodcutting", fishing = "SKILL_Fishing",
    cooking = "SKILL_Cooking", farming = "SKILL_Farming", plants = "SKILL_Plants", salvage = "SKILL_Salvage"
}

local STATION_ORDER = {
    "StationFurnace", "StationSawmill", "StationCooking", "StationGrindstone",
    "StationTextiles", "StationOther", "StationBenches"
}
local STATION_LABEL = {
    StationFurnace = "Furnace (bars)", StationSawmill = "Sawmill (planks)", StationCooking = "Cooking stations",
    StationGrindstone = "Grindstone", StationTextiles = "Loom, spinning, tanning",
    StationOther = "Kiln, stonecutter, brewing", StationBenches = "Crafting benches",
}
local STATION_KEYS = {}
for _, k in ipairs(STATION_ORDER) do STATION_KEYS[k:lower()] = k end

local function stationRate(key)
    return yieldCfg.Station[key] or 1.0
end

local function clampLootRates()
    if type(lootCfg.Multiplier) ~= "number" or lootCfg.Multiplier < 1.0 then lootCfg.Multiplier = 1.0 end
    if lootCfg.Multiplier > 100.0 then lootCfg.Multiplier = 100.0 end
    if type(lootCfg.ChanceMultiplier) ~= "number" or lootCfg.ChanceMultiplier < 1.0 then lootCfg.ChanceMultiplier = 1.0 end
    if lootCfg.ChanceMultiplier > 100.0 then lootCfg.ChanceMultiplier = 100.0 end
end

local function loadYieldSettings()
    local paths = {
        "Mods\\YieldSliders\\config.txt",
        "ue4ss\\Mods\\YieldSliders\\config.txt",
        "..\\YieldSliders\\config.txt",
        "Mods\\LootYield\\yield_config.txt",
        "Mods\\LootYield\\config.txt",
        "ue4ss\\Mods\\LootYield\\config.txt",
        "config.txt"
    }
    local f, foundPath = nil, nil
    for _, p in ipairs(paths) do
        f = io.open(p, "r")
        if f then foundPath = p; break end
    end
    if not f then return end
    for rawLine in f:lines() do
        local line = rawLine:gsub("^%s+", ""):gsub("%s+$", "")
        if line ~= "" and line:sub(1, 1) ~= "#" then
            local k, v = line:match("^([%w_]+)%s*=%s*(.+)$")
            if k and v then v = v:gsub("%s*[#;(].*$", "") end
            if k and v then
                local kLower = k:lower()
                if SKILL_KEYS[kLower] then
                    local n = tonumber(v)
                    if n and n >= 1.0 then yieldCfg.PerSkill[SKILL_KEYS[kLower]] = n end
                elseif STATION_KEYS[kLower] then
                    local n = tonumber(v)
                    if n and n >= 1.0 then
                        yieldCfg.Station[STATION_KEYS[kLower]] = n
                    else
                        yieldCfg.Station[STATION_KEYS[kLower]] = 1.0
                    end
                elseif kLower == "multiplier" or kLower == "yieldmultiplier" then
                    local n = tonumber(v)
                    if n and n >= 1.0 then yieldCfg.Multiplier = n end
                elseif yieldCfg[k] ~= nil and k ~= "PerSkill" and k ~= "Station" then
                    if type(yieldCfg[k]) == "boolean" then
                        yieldCfg[k] = (v:lower() == "true" or v == "1")
                    else
                        local n = tonumber(v)
                        if n then yieldCfg[k] = n end
                    end
                end
            end
        end
    end
    f:close()
    if foundPath and not yieldCfg._logged then
        yieldCfg._logged = true
        log("loaded gathering settings from " .. foundPath)
        local sparts = {}
        for _, key in ipairs(STATION_ORDER) do
            if stationRate(key) > 1.0 then
                sparts[#sparts + 1] = STATION_LABEL[key] .. " x" .. string.format("%.1f", stationRate(key))
            end
        end
        log("stations: " .. (#sparts > 0 and table.concat(sparts, ", ") or "all off (1.0)"))
    end
end

local function loadLootSettings()
    local paths = {
        "Mods\\LootSliders\\config.txt",
        "ue4ss\\Mods\\LootSliders\\config.txt",
        "..\\LootSliders\\config.txt",
        "Mods\\LootYield\\loot_config.txt",
        "Mods\\LootYield\\config.txt",
        "ue4ss\\Mods\\LootYield\\config.txt",
        "config.txt"
    }
    local f, foundPath = nil, nil
    for _, p in ipairs(paths) do
        f = io.open(p, "r")
        if f then foundPath = p; break end
    end
    if not f then return end
    for rawLine in f:lines() do
        local line = rawLine:gsub("^%s+", ""):gsub("%s+$", "")
        if line ~= "" and line:sub(1, 1) ~= "#" then
            local k, v = line:match("^([%w_]+)%s*=%s*(.+)$")
            if k and v then v = v:gsub("%s*[#;(].*$", "") end
            if k and v then
                local kLower = k:lower()
                if kLower == "multiplier" or kLower == "lootmultiplier" then
                    local n = tonumber(v)
                    if n then lootCfg.Multiplier = n end
                elseif kLower == "chancemultiplier" then
                    local n = tonumber(v)
                    if n then lootCfg.ChanceMultiplier = n end
                elseif lootCfg[k] ~= nil then
                    if type(lootCfg[k]) == "boolean" then
                        lootCfg[k] = (v:lower() == "true" or v == "1")
                    else
                        local n = tonumber(v)
                        if n then lootCfg[k] = n end
                    end
                end
            end
        end
    end
    f:close()
    clampLootRates()
    if foundPath and not lootCfg._logged then
        lootCfg._logged = true
        log("loaded creature loot settings from " .. foundPath .. string.format(" (amount x%.1f, chance x%.1f)", lootCfg.Multiplier, lootCfg.ChanceMultiplier))
    end
end

loadYieldSettings()
loadLootSettings()

-- Disarm LootSliders redundant local loop if co-installed, preventing double-scaling
local function neutralizeLootSliders()
    if ModRef and ModRef.SetSharedVariable then
        pcall(function()
            ModRef:SetSharedVariable("ModMenu.LootSliders.rev", 999999)
            ModRef:SetSharedVariable("ModMenu.LootSliders.Multiplier", 1.0)
            ModRef:SetSharedVariable("ModMenu.LootSliders.ChanceMultiplier", 1.0)
        end)
    end
end
neutralizeLootSliders()

-- Disarm YieldSliders redundant station loop if co-installed, preventing double-scaling
local function neutralizeYieldSliders()
    if ModRef and ModRef.SetSharedVariable then
        pcall(function()
            ModRef:SetSharedVariable("ModMenu.YieldSliders.rev", 999999)
            for _, k in ipairs(STATION_ORDER) do
                ModRef:SetSharedVariable("ModMenu.YieldSliders." .. k, 1.0)
            end
        end)
    end
end
neutralizeYieldSliders()

--------------------------------------------------------------------------------
-- 2. LootSliders Engine: In-Memory Drop Table Scaling & Sentinels
--------------------------------------------------------------------------------
local TABLES = {
    "/Game/Gameplay/Items/LootDropTables/DT_LootDropTable.DT_LootDropTable",
    "/DowdunReach/Gameplay/Items/LootDropTables/DT_LootDropTable_DowdunReach.DT_LootDropTable_DowdunReach",
}

local QUEST_TROPHIES = {
    ITEM_Resources_Kuldra = true,
    ITEM_Resources_DragonsHeart = true,
    ITEM_Resources_Abyssal_Spine = true,
}

local function isQuestLike(name)
    if QUEST_TROPHIES[name] then return true end
    local n = name:lower()
    return n:find("quest", 1, true) or n:find("key_", 1, true) or n:find("_head", 1, true)
end

local function wantedAmounts(e)
    if not e.stackable then return e.min, e.max end
    if lootCfg.SkipQuestItems and e.quest then return e.min, e.max end
    local m = lootCfg.Multiplier
    local lo = math.max(1, math.floor(e.min * m + 1e-6))
    local hi = math.max(lo, math.ceil(e.max * m - 1e-6))
    if e.stack and e.stack > 1 then
        hi = math.min(hi, e.stack)
        lo = math.min(lo, hi)
    end
    return lo, hi
end

local function wantedChance(e)
    if not e.stackable then
        local opted = (lootCfg.IncludeMounts and e.mount) or (lootCfg.IncludeVestiges and e.vestige)
        if opted and e.chance < 100 then
            return math.min(100.0, e.chance * lootCfg.ChanceMultiplier)
        end
        return e.chance
    end
    if lootCfg.SkipQuestItems and e.quest then return e.chance end
    if e.chance >= 100 then return e.chance end
    return math.min(100.0, e.chance * lootCfg.ChanceMultiplier)
end

local function rowNames(dt)
    local ok, names = pcall(function() return dt:GetRowNames() end)
    if not ok or type(names) ~= "table" then return nil end
    local out = {}
    for _, n in ipairs(names) do
        local s = n
        if type(n) ~= "string" then
            local okS, str = pcall(function() return n:ToString() end)
            s = okS and tostring(str) or nil
        end
        if s then out[#out + 1] = s end
    end
    return out
end

local function snapshotLoot(dt, path)
    local names = rowNames(dt)
    if not names then return nil end
    local entries = {}
    for _, rn in ipairs(names) do
        local okR, row = pcall(function() return dt:FindRow(rn) end)
        if okR and row then
            local okA, res = pcall(function() return row.Resources end)
            local okN, count = pcall(function() return res:GetArrayNum() end)
            if okA and okN and type(count) == "number" then
                for i = 1, count do
                    local okE, e = pcall(function()
                        local r = res[i]
                        local item = r.SpawnedItemData
                        local stack = nil
                        if valid(item) then
                            local okS, s = pcall(function() return item.MaxStackSize end)
                            if okS and type(s) == "number" then stack = s end
                        end
                        local itemName = valid(item) and fname(item) or "?"
                        local cls = ""
                        if valid(item) then
                            local okC, c = pcall(function() return item:GetClass():GetFName():ToString() end)
                            if okC then cls = tostring(c) end
                        end
                        return {
                            row = rn, index = i, item = itemName,
                            min = r.MinimumDropAmount, max = r.MaximumDropAmount, chance = r.DropChance,
                            stack = stack,
                            stackable = (stack ~= nil and stack > 1 and not itemName:find("^DA_")),
                            quest = isQuestLike(itemName) and true or false,
                            mount = (cls == "ConsumableMountUnlocker")
                                    or (itemName:find("^DA_") ~= nil and itemName:lower():find("mount", 1, true) ~= nil),
                            vestige = (cls:lower():find("vestige", 1, true) ~= nil)
                                    or (itemName:find("^DA_") ~= nil and itemName:lower():find("vestige", 1, true) ~= nil),
                        }
                    end)
                    if okE and e and type(e.min) == "number" and type(e.max) == "number" and type(e.chance) == "number" then
                        entries[#entries + 1] = e
                    end
                end
            end
        end
    end
    return entries
end

local function lootSettingsKey()
    return string.format("%.2f|%.2f|%s|%s|%s",
        lootCfg.Multiplier, lootCfg.ChanceMultiplier,
        tostring(lootCfg.SkipQuestItems), tostring(lootCfg.IncludeMounts), tostring(lootCfg.IncludeVestiges))
end

local function pickSentinels(state)
    local s = {}
    for _, e in ipairs(state.entries) do
        local lo, hi = wantedAmounts(e)
        local ch = wantedChance(e)
        if lo ~= e.min or hi ~= e.max or math.abs(ch - e.chance) > 0.001 then
            s[#s + 1] = { e = e, lo = lo, hi = hi, ch = ch }
            if #s >= 4 then break end
        end
    end
    return s
end

local function sentinelsHold(dt, state)
    for _, s in ipairs(state.sentinels) do
        local ok, same = pcall(function()
            local r = dt:FindRow(s.e.row).Resources[s.e.index]
            return r.MinimumDropAmount == s.lo and r.MaximumDropAmount == s.hi and math.abs(r.DropChance - s.ch) < 0.001
        end)
        if not ok or not same then return false end
    end
    return true
end

local function applyLoot(dt, path, state)
    local t0 = os.clock()
    local changed, rows = 0, {}
    for _, e in ipairs(state.entries) do
        local lo, hi = wantedAmounts(e)
        local ch = wantedChance(e)
        local r = rows[e.row]
        if not r then
            local okR, row = pcall(function() return dt:FindRow(e.row) end)
            r = okR and row or false
            rows[e.row] = r
        end
        if r then
            local ok = pcall(function()
                local x = r.Resources[e.index]
                if x.MinimumDropAmount ~= lo then x.MinimumDropAmount = lo end
                if x.MaximumDropAmount ~= hi then x.MaximumDropAmount = hi end
                if math.abs(x.DropChance - ch) > 0.001 then x.DropChance = ch end
            end)
            if ok and (lo ~= e.min or hi ~= e.max or math.abs(ch - e.chance) > 0.001) then
                changed = changed + 1
                if lootCfg.Verbose then
                    log(string.format("[Loot] %s %s: %d-%d @%.0f%% -> %d-%d @%.0f%%", e.row, e.item, e.min, e.max, e.chance, lo, hi, ch))
                end
            end
        end
    end
    state.sentinels = pickSentinels(state)
    state.appliedKey = lootSettingsKey()
    local short = path:match("%.([^%.]+)$") or path
    log(string.format("%s: %d of %d drops scaled (amount x%.1f, chance x%.1f) in %.0f ms",
        short, changed, #state.entries, lootCfg.Multiplier, lootCfg.ChanceMultiplier, (os.clock() - t0) * 1000))
end

local lootStates = {}
local function lootTick()
    for _, path in ipairs(TABLES) do
        local dt = nil
        pcall(function() dt = StaticFindObject(path) end)
        if not valid(dt) then
            pcall(function() LoadAsset(path) end)
            pcall(function() dt = StaticFindObject(path) end)
        end
        if valid(dt) then
            local state = lootStates[path]
            if not state then
                local entries = snapshotLoot(dt, path)
                if entries and #entries > 0 then
                    state = { entries = entries, sentinels = {}, appliedKey = nil }
                    lootStates[path] = state
                    local skipped, mounts, vestiges = 0, 0, 0
                    for _, e in ipairs(entries) do
                        if not e.stackable or e.quest then skipped = skipped + 1 end
                        if e.mount then mounts = mounts + 1 end
                        if e.vestige then vestiges = vestiges + 1 end
                    end
                    local short = path:match("%.([^%.]+)$") or path
                    log(string.format("found %s: %d drops (%d gear/quest/unlocks, %d mounts, %d vestiges)",
                        short, #entries, skipped, mounts, vestiges))
                end
            end
            if state then
                if state.appliedKey ~= lootSettingsKey() or not sentinelsHold(dt, state) then
                    applyLoot(dt, path, state)
                end
            end
        end
    end
end

--------------------------------------------------------------------------------
-- 3. YieldSliders Engine: Dedicated Server Gathering XP & Pickups
--------------------------------------------------------------------------------
local ONLY_ITEMS = {
    SKILL_Cooking = {
        ITEM_Consumable_Fruit_Redberry = true, ITEM_Consumable_Fruit_Dwellberry = true,
        ITEM_Resources_Cabbage = true, ITEM_Resources_Onion = true, ITEM_Resources_Potato = true,
    },
    SKILL_Plants = {
        ITEM_Resources_Flax = true, ITEM_Resources_Mushroom = true, ITEM_Resources_Swamp_Tar = true,
        ITEM_SwampWeed = true, ITEM_Resources_Agility_Avantoe = true,
        ITEM_Herb_Harralander = true, ITEM_Herb_Irit = true, ITEM_Herb_Kwuarm = true,
        ITEM_Herb_Marrentill = true, ITEM_Herb_Snapdragon = true, ITEM_Herb_Toadflax = true,
    },
    SKILL_Farming = {
        ITEM_Resources_Cabbage = true, ITEM_Resources_Onion = true, ITEM_Resources_Potato = true,
        ITEM_Resources_Tomato = true, ITEM_Resources_Pumpkin = true, ITEM_Resources_Watermelon = true,
        ITEM_Resources_Wheat = true, ITEM_Resources_Flax = true, ITEM_Resources_Corpse_Fur = true,
        ITEM_Resources_Fibers_Cactus = true, ITEM_Resources_Steak_Cactus = true, ITEM_Resources_Weed_Desert = true,
        ITEM_Consumable_Water_Cactus = true, ITEM_Consumable_Fruit_Redberry = true,
        ITEM_Consumable_Fruit_Dwellberry = true, ITEM_Consumable_Fruit_Cadavaberry = true,
        ITEM_SwampWeed = true, ITEM_Herb_Harralander = true, ITEM_Herb_Kwuarm = true,
        ITEM_Herb_Marrentill = true, ITEM_Herb_Snapdragon = true, ITEM_Herb_Toadflax = true,
        ITEM_Herb_Irit = true, ITEM_Resources_Agility_Avantoe = true,
    }
}

local PICK_FORAGE = {
    ITEM_Resources_Wood_Ash = true, ITEM_Resources_Stone = true, ITEM_Resources_AnimalBone = true,
    ITEM_Resources_Bark_Hollow = true, ITEM_Resources_Pumpkin = true, ITEM_Consumable_Fruit_Cadavaberry = true,
}

local PICK_SALVAGE = {}
for _, n in ipairs({
    "ITEM_Resources_Salvage_BlackMetal", "ITEM_Resources_Salvage_Bronze", "ITEM_Resources_Salvage_Gold",
    "ITEM_Resources_Salvage_Iron", "ITEM_Resources_Salvage_Mithril", "ITEM_Resources_Salvage_Silver",
    "ITEM_Resources_Salvage_Steel", "ITEM_Resources_BlackMetalBar", "ITEM_Resources_BronzeBar",
    "ITEM_Resources_GoldBar", "ITEM_Resources_IronBar", "ITEM_Resources_MithrilBar", "ITEM_Resources_SilverBar",
    "ITEM_Resources_SteelBar", "ITEM_Resources_GoldOre", "ITEM_Resources_IronOre", "ITEM_Resources_MithrilOre",
    "ITEM_Resources_SilverOre",
    "ITEM_Resources_Leather", "ITEM_Resources_Leather_Draconic", "ITEM_Resources_Leather_Hard",
    "ITEM_Resources_Leather_Scraps_Draconic", "ITEM_Resources_Leather_Scraps_Hard",
    "ITEM_Resources_Cloth_Fine", "ITEM_Resources_Cloth_Mystic", "ITEM_Resources_Cloth_Padded",
    "ITEM_Resources_Fibres_Mystic", "ITEM_Resources_Thread_Fine", "ITEM_Resources_Thread_Mystic",
    "ITEM_Consumable_Fryup_Cheeky", "ITEM_Consumable_Fryup_Vegan", "ITEM_Consumable_Meat_Mixed_Grill",
    "ITEM_Consumable_Omelette_Pungent", "ITEM_Consumable_Pie_Fruit", "ITEM_Consumable_RoastDinner",
    "ITEM_Consumable_Sandwich_Glazed_Pumpkin_Roast", "ITEM_Consumable_Sandwich_Meat",
    "ITEM_Consumable_Sandwich_Meat_Double", "ITEM_Consumable_Sandwich_Steak", "ITEM_Consumable_Sandwich_Veggie",
    "ITEM_Consumable_SteakAndEggs", "ITEM_Consumable_Tea_Cadavaberry", "ITEM_Consumable_Tea_Dwellberry",
    "ITEM_Consumable_Tea_Maple", "ITEM_Consumable_Tea_Pumpkin", "ITEM_Consumable_Tea_Redberry",
    "ITEM_Consumable_Tea_Stamina_Reduction_Attack", "ITEM_Consumable_Tea_Stamina_Reduction_Dodge",
    "ITEM_Consumable_Tea_Stamina_Reduction_Magic", "ITEM_Consumable_Tea_Stamina_Reduction_Ranged",
    "ITEM_Consumable_Tea_Stamina_Reduction_Sprint", "ITEM_Consumable_Toastie_Maple", "ITEM_Consumable_Vegball_Sweet",
    "ITEM_Consumable_Potion_Focused_T2_Lesser_Agility", "ITEM_Consumable_Potion_Focused_T2_Lesser_Artisan",
    "ITEM_Consumable_Potion_Focused_T2_Lesser_Attack", "ITEM_Consumable_Potion_Focused_T2_Lesser_Construction",
    "ITEM_Consumable_Potion_Focused_T2_Lesser_Cooking", "ITEM_Consumable_Potion_Focused_T2_Lesser_Farming",
    "ITEM_Consumable_Potion_Focused_T2_Lesser_Mining", "ITEM_Consumable_Potion_Focused_T2_Lesser_Runecrafting",
    "ITEM_Consumable_Potion_Focused_T2_Lesser_Woodcutting", "ITEM_Consumable_Potion_Focused_T3_Agility",
    "ITEM_Consumable_Potion_Focused_T3_Artisan", "ITEM_Consumable_Potion_Focused_T3_Attack",
    "ITEM_Consumable_Potion_Focused_T3_Construction", "ITEM_Consumable_Potion_Focused_T3_Cooking",
    "ITEM_Consumable_Potion_Focused_T3_Farming", "ITEM_Consumable_Potion_Focused_T3_Mining",
    "ITEM_Consumable_Potion_Focused_T3_Runecrafting", "ITEM_Consumable_Potion_Focused_T3_Woodcutting",
    "ITEM_Consumable_Potion_T2_Lesser_Antipoison", "ITEM_Consumable_Potion_T2_Lesser_Lumberjack",
    "ITEM_Consumable_Potion_T2_Lesser_Quarrymaster", "ITEM_Consumable_Potion_T3_Antifire",
    "ITEM_Consumable_Potion_T3_Lumberjack", "ITEM_Consumable_Potion_T3_Quarrymaster",
    "ITEM_Fuel_Resources_Fuel_Pellet", "ITEM_Resources_Adhesive", "ITEM_Resources_Coal",
    "ITEM_Resources_DragonBlood", "ITEM_Resources_Ectoplasm", "ITEM_Resources_WildAnima",
    "ITEM_Resources_Artifact_Garou", "ITEM_Resources_Clay_Decoration", "ITEM_Resources_Clay_Vessel",
    "ITEM_Resources_Molten_Glass",
    "ITEM_Consumable_MagicFocus", "ITEM_Consumable_Potion_T1_Weak_Antipoison", "ITEM_Consumable_Potion_T3_Antipoison",
    "ITEM_Consumable_Potion_T4_Antifire_Super", "ITEM_Consumable_Potion_T4_Antipoison_Super",
    "ITEM_Consumable_WeaponBarbs", "ITEM_Consumable_WeaponPoison", "ITEM_Consumable_Whetstone",
}) do PICK_SALVAGE[n] = true end

local PICK_ROWS = {
    { skill = "SKILL_Plants", items = ONLY_ITEMS.SKILL_Plants },
    { skill = "SKILL_Cooking", items = PICK_FORAGE },
    { skill = "SKILL_Salvage", items = PICK_SALVAGE, noXp = true, stationOk = true },
}

local NODE_SKILLS = { SKILL_Mining = true, SKILL_Woodcutting = true }
local NOT_FROM_NODES = {}
for _, n in ipairs({
    "ITEM_Resources_CopperBar", "ITEM_Resources_TinBar", "ITEM_Resources_BronzeBar", "ITEM_Resources_IronBar",
    "ITEM_Resources_SteelBar", "ITEM_Resources_SilverBar", "ITEM_Resources_GoldBar", "ITEM_Resources_MithrilBar",
    "ITEM_Resources_AdamantiteBar", "ITEM_Resources_RuniteBar", "ITEM_Resources_Bar_Runite",
    "ITEM_Resources_BluriteBar", "ITEM_Resources_BlackMetalBar",
    "ITEM_Resources_Ground_Stone", "ITEM_Resources_Ground_Clay", "ITEM_Resources_Ground_Granite",
    "ITEM_Resources_Ground_Sandstone", "ITEM_Resources_Ground_Limestone", "ITEM_Resources_Ground_Gypsum",
    "ITEM_Resources_Ground_Rune", "ITEM_Resources_Ground_Tooth_Dragon", "ITEM_Resources_Ground_Obsidian",
    "ITEM_Resources_Ground_Kalphite_Wing", "ITEM_Resources_Ground_Scale_Dragon_Blue", "ITEM_Resources_Ground_Wheat",
    "ITEM_Resources_Ground_Bonemeal_Necrotic", "ITEM_Resources_Stone_Block",
    "ITEM_Resources_Clay_Slab", "ITEM_Resources_Clay_Slab_Unfired", "ITEM_Resources_Clay_Mould",
    "ITEM_Resources_Clay_Mould_Unfired", "ITEM_Resources_Clay_Vessel", "ITEM_Resources_Clay_Vessel_Unfired",
    "ITEM_Resources_Clay_Decoration", "ITEM_Resources_Clay_Decoration_Unfired", "ITEM_Resources_Molten_Glass",
    "ITEM_Resources_Plank_Ash", "ITEM_Resources_Plank_Oak", "ITEM_Resources_Plank_Yew",
    "ITEM_Fuel_Resources_Charcoal", "ITEM_Consumable_BirdNest",
}) do NOT_FROM_NODES[n] = true end

local function rateFor(skill) return yieldCfg.PerSkill[skill] or yieldCfg.Multiplier end

local STATION_OUT = {}
local function gatheredToo(name)
    if ONLY_ITEMS.SKILL_Cooking[name] or ONLY_ITEMS.SKILL_Plants[name] or ONLY_ITEMS.SKILL_Farming[name] or PICK_FORAGE[name] then return true end
    return (name:find("^ITEM_Rune_") or name:find("_Raw_", 1, true) or name:find("Ore$")
        or name:find("^ITEM_Resources_Wood_") or name == "ITEM_Resources_Stone") and true or false
end

local function snapshotInv(inv)
    local items = {}
    local max = 0
    pcall(function() max = inv.MaxSlotCount end)
    if type(max) ~= "number" or max <= 0 or max > 2000 then return nil end
    for i = 0, max - 1 do
        local okI, it = pcall(function() return inv:GetItemFromSlot(i) end)
        if okI and valid(it) then
            local data = nil
            pcall(function() data = it.ItemData end)
            if valid(data) then
                local okC, c = pcall(function() return inv:GetNumItemsInSlot(i) end)
                if okC and type(c) == "number" and c > 0 then
                    local n = fname(data)
                    local e = items[n]
                    if not e then e = { obj = data, count = 0 }; items[n] = e end
                    e.count = e.count + c
                end
            end
        end
    end
    return items
end

local lastPickAt = -1000
pcall(RegisterHook, "/Script/Dominion.BaseInteractableResource:OnInteraction", function()
    lastPickAt = os.clock()
end)

local cachedSkills = nil
local function getSkills()
    if cachedSkills then return cachedSkills end
    local ok, all = pcall(FindAllOf, "SkillData")
    if not ok or not all then return nil end
    local list = {}
    for _, s in ipairs(all) do
        local n = fname(s)
        for _, full in pairs(SKILL_KEYS) do
            if n == full and valid(s) then list[#list + 1] = { obj = s, name = n } end
        end
    end
    if #list > 0 then cachedSkills = list end
    return cachedSkills
end

local cachedAuth, nextAuthScan = nil, 0
local function authoritativePCs()
    local now = os.clock()
    if cachedAuth and now < nextAuthScan then return cachedAuth end
    nextAuthScan = now + 3
    local ok, all = pcall(FindAllOf, "PlayerController")
    if not ok or not all then cachedAuth = {}; return cachedAuth end
    local list = {}
    for _, pc in ipairs(all) do
        if valid(pc) then
            local okN, n = pcall(function() return pc:GetFullName() end)
            if okN and not tostring(n):find("MainMenu", 1, true) then
                local okA, auth = pcall(function() return pc:HasAuthority() end)
                if okA and auth then list[#list + 1] = pc end
            end
        end
    end
    cachedAuth = list
    return list
end

local playerStates = { players = {} }
local loggedServerMode = false

local function tickPlayer(pc, list, now)
    local okL, isLocal = pcall(function() return pc:IsLocalController() end)
    local isLocalHost = (okL and isLocal)
    if isLocalHost then return end -- Original YieldSliders handles local host

    local comp, inv = nil, nil
    pcall(function() comp = pc.SkillComponent end)
    pcall(function() inv = pc.BP_Components_Inventory end)
    if not valid(comp) or not valid(inv) then return end

    local owner = nil
    pcall(function() owner = inv:GetFullName() end)
    if not owner then return end

    local ps = playerStates.players[owner]
    if not ps then
        ps = { xp = {}, lastXpAt = {}, items = nil, recent = {}, warnedFull = {}, bornAt = now }
        playerStates.players[owner] = ps
        if yieldCfg.Verbose then log("tracking player: " .. owner .. " (join grace " .. tostring(yieldCfg.JoinGraceSeconds) .. "s)") end
    end

    -- 1. Gather XP
    local bestSkill, bestAt = nil, -1
    for _, s in ipairs(list) do
        local okX, cur = pcall(function() return comp:GetTotalXP(s.obj) end)
        if okX and type(cur) == "number" then
            local prev = ps.xp[s.name]
            if prev and cur > prev then ps.lastXpAt[s.name] = now end
            ps.xp[s.name] = cur
        end
        local at = ps.lastXpAt[s.name]
        if at and now - at <= yieldCfg.WindowSeconds and at > bestAt then bestSkill, bestAt = s.name, at end
    end

    -- 2. Snapshot
    local items = snapshotInv(inv)
    if not items then return end
    local prev = ps.items
    ps.items = items
    if not prev then return end

    -- 3. Dedicated Server Join Grace
    if now - ps.bornAt < yieldCfg.JoinGraceSeconds then return end

    local picked = (now - lastPickAt) <= yieldCfg.WindowSeconds
    if not bestSkill and not picked then return end

    local only = bestSkill and ONLY_ITEMS[bestSkill] or nil

    for name, e in pairs(items) do
        local before = prev[name] and prev[name].count or 0
        local gain = e.count - before
        local mult = nil
        if bestSkill and (not only or only[name]) and not (NODE_SKILLS[bestSkill] and NOT_FROM_NODES[name])
            and not STATION_OUT[name] then
            mult = rateFor(bestSkill)
        elseif picked then
            for _, row in ipairs(PICK_ROWS) do
                if row.items[name] and not (row.noXp and bestSkill) and (row.stationOk or not STATION_OUT[name]) then
                    mult = rateFor(row.skill)
                    break
                end
            end
        end
        if gain > 0 and mult and mult > 1.0 then
            local stack = 0
            pcall(function() stack = e.obj.MaxStackSize end)
            if type(stack) == "number" and stack > 1 and gain <= 5000 then
                local st = ps.recent[name] or { carry = 0 }
                ps.recent[name] = st
                local extra = gain * (mult - 1) + st.carry
                local whole = math.floor(extra)
                st.carry = extra - whole
                if whole > 0 then
                    local space = whole
                    pcall(function() space = inv:GetSpaceAvailableForItemByData(e.obj) end)
                    local give = math.min(whole, math.max(0, space or 0))
                    if give > 0 then
                        local okAdd, added = pcall(function() return inv:AddItemByData(e.obj, give, 100.0, {}) end)
                        if okAdd and added then
                            e.count = e.count + give
                            if yieldCfg.Verbose then
                                log(string.format("%s gathered %d %s, awarded +%d bonus (x%.1f)",
                                    owner, gain, name, give, mult))
                            end
                        end
                    end
                end
            end
        end
    end
end

--------------------------------------------------------------------------------
-- 3b. YieldSliders Engine: Stations & Crafting In-Memory Multipliers
--------------------------------------------------------------------------------
local PROC_DT = "/Game/Gameplay/World/Stations/DT_ProcessingStationDataTable.DT_ProcessingStationDataTable"

local PROC_ROWS = {
    Furnace = "StationFurnace", AdvancedSmelter = "StationFurnace",
    Sawmill = "StationSawmill",
    Campfire = "StationCooking", Grill = "StationCooking",
    Grinder = "StationGrindstone",
    Loom = "StationTextiles", ["Spinning Wheel"] = "StationTextiles", AdvancedSpinningWheel = "StationTextiles",
    Tannery = "StationTextiles", AdvancedTannery = "StationTextiles",
    Kiln = "StationOther", Stonecutter = "StationOther", FermentationBarrel = "StationOther", BrewingCauldron = "StationOther",
}

local function procGroup(row)
    if type(row) ~= "string" then return nil end
    local low = row:lower()
    if low:find("compost", 1, true) then return nil end -- farming, never scaled
    if PROC_ROWS[row] then return PROC_ROWS[row] end
    if low:find("smelt", 1, true) or low:find("furnace", 1, true) then return "StationFurnace" end
    if low:find("sawmill", 1, true) or low:find("lumber", 1, true) then return "StationSawmill" end
    if low:find("cook", 1, true) or low:find("grill", 1, true) or low:find("camp", 1, true) then return "StationCooking" end
    if low:find("grind", 1, true) then return "StationGrindstone" end
    if low:find("loom", 1, true) or low:find("spin", 1, true) or low:find("tan", 1, true) then return "StationTextiles" end
    if low:find("kiln", 1, true) or low:find("stone", 1, true) or low:find("barrel", 1, true) or low:find("cauldron", 1, true) or low:find("brew", 1, true) then return "StationOther" end
    return nil
end

local CRAFT_ROWS = {
    RangeStone = "StationCooking", RangeBronze = "StationCooking",
    BrewingCauldron = "StationOther",
    CraftingTable = "StationBenches", FletchingBenchv2 = "StationBenches", JewelersBench = "StationBenches",
    PotteryWheel = "StationBenches", MysticForge = "StationBenches", RuneAltar = "StationBenches",
    GarouMasonsBench = "StationBenches", ToolsMeleeBench = "StationBenches", ArmourBench = "StationBenches",
}

local function craftGroup(row)
    if type(row) ~= "string" then return nil end
    if CRAFT_ROWS[row] then return CRAFT_ROWS[row] end
    local low = row:lower()
    if low:find("cook", 1, true) or low:find("range", 1, true) then return "StationCooking" end
    if low:find("bench", 1, true) or low:find("table", 1, true) or low:find("forge", 1, true) or low:find("altar", 1, true)
        or low:find("mason", 1, true) or low:find("jewel", 1, true) or low:find("pottery", 1, true) or low:find("fletch", 1, true)
        or low:find("smith", 1, true) then
        return "StationBenches"
    end
    return nil
end

local ST = {
    orig = {},        -- "rn#index" -> original count
    entries = {},     -- array of { recipe, rname, index, item, orig, stack, group }
    byRecipe = {},    -- rn -> group
    vendor = {},      -- rn -> true
    unknown = {},
    appliedKey = nil,
    dtDone = false,
    dtNextTry = 0,
    cursor = 1,
}

local function fullName(o)
    local ok, n = pcall(function() return o:GetFullName() end)
    return ok and n and tostring(n) or nil
end

local function stWanted(e)
    local rate = stationRate(e.group)
    if rate <= 1.0 or ST.vendor[e.rname] then return e.orig end
    local v = math.floor(e.orig * rate + 0.5)
    if v > e.stack then v = e.stack end
    if v < e.orig then v = e.orig end
    return v
end

local function stWrite(e, v)
    if not valid(e.recipe) then return false end
    local ok = pcall(function()
        local x = e.recipe.ItemsCreated[e.index]
        if x.Count ~= v then x.Count = v end
    end)
    return ok
end

local function stSettingsKey()
    local p, nv = {}, 0
    for _, k in ipairs(STATION_ORDER) do p[#p + 1] = string.format("%.2f", stationRate(k)) end
    for _ in pairs(ST.vendor) do nv = nv + 1 end
    return table.concat(p, "|") .. "|v" .. nv
end

local function stAddRecipe(r, group)
    local new = {}
    if not valid(r) then return new end
    local rn = fullName(r)
    if not rn or ST.byRecipe[rn] then return new end
    ST.byRecipe[rn] = group
    local arr, n = nil, 0
    pcall(function() arr = r.ItemsCreated end)
    pcall(function() n = arr:GetArrayNum() end)
    if type(n) ~= "number" or n <= 0 or n > 8 then return new end
    for i = 1, n do
        local d, c = nil, nil
        pcall(function() d = arr[i].ItemData end)
        pcall(function() c = arr[i].Count end)
        if valid(d) and type(c) == "number" then
            local item = fname(d)
            local stack = nil
            pcall(function() stack = d.MaxStackSize end)
            local key = rn .. "#" .. i
            if ST.orig[key] == nil then ST.orig[key] = c end
            local orig = ST.orig[key]
            -- Gear rule: only items the game stacks (MaxStackSize > 1). Tools, weapons, armor stack to 1 and are never scaled; DA_ unlocks neither.
            if type(stack) == "number" and stack > 1 and orig >= 1 and item:sub(1, 3) ~= "DA_" then
                local e = { recipe = r, rname = rn, index = i, item = item, orig = orig, stack = stack, group = group }
                ST.entries[#ST.entries + 1] = e
                new[#new + 1] = e
                if not gatheredToo(item) then STATION_OUT[item] = true end
                stWrite(e, stWanted(e))
            end
        end
    end
    return new
end

local function stDiscoverTable(now)
    if ST.dtDone or now < ST.dtNextTry then return end
    local dt = nil
    pcall(function() dt = StaticFindObject(PROC_DT) end)
    if not valid(dt) then
        pcall(function() LoadAsset(PROC_DT) end)
        pcall(function() dt = StaticFindObject(PROC_DT) end)
    end
    if not valid(dt) then ST.dtNextTry = now + 15; return end
    local names = rowNames(dt)
    if not names then ST.dtNextTry = now + 15; return end
    ST.dtDone = true
    local t0, before = os.clock(), #ST.entries
    for _, rowName in ipairs(names) do
        local group = procGroup(rowName)
        if group then
            local okR, row = pcall(function() return dt:FindRow(rowName) end)
            local recipes, n = nil, 0
            if okR and row then
                pcall(function() recipes = row.Recipes end)
                pcall(function() n = recipes:GetArrayNum() end)
            end
            if type(n) == "number" then
                for i = 1, math.min(n, 400) do
                    local r = nil
                    pcall(function() r = recipes[i] end)
                    stAddRecipe(r, group)
                end
            end
        end
    end
    log(string.format("stations: found %d stackable outputs in DT_ProcessingStationDataTable in %.0f ms",
        #ST.entries - before, (os.clock() - t0) * 1000))
end

local function stScanComponents()
    local okCS, csList = pcall(FindAllOf, "CraftingStationComponent")
    if okCS and csList then
        for _, cs in ipairs(csList) do
            if valid(cs) then
                local row, vendor = nil, nil
                pcall(function() row = cs.CraftingDataRowHandle.RowName:ToString() end)
                pcall(function() vendor = cs.bIsVendor end)
                local arr, n = nil, 0
                pcall(function() arr = cs.ValidRecipes end)
                pcall(function() n = arr:GetArrayNum() end)
                if type(n) ~= "number" then n = 0 end
                if vendor == true or vendor == nil then
                    local shared = 0
                    for i = 1, math.min(n, 400) do
                        local r = nil
                        pcall(function() r = arr[i] end)
                        local rn = valid(r) and fullName(r) or nil
                        if rn and not ST.vendor[rn] then
                            ST.vendor[rn] = true
                            for _, e in ipairs(ST.entries) do
                                if e.rname == rn then stWrite(e, e.orig); shared = shared + 1 end
                            end
                        end
                    end
                    if shared > 0 and yieldCfg.Verbose then
                        log(string.format("stations: %d outputs a vendor also sells put back to normal", shared))
                    end
                else
                    local grp = craftGroup(row)
                    if grp and n > 0 then
                        local added = 0
                        for i = 1, math.min(n, 400) do
                            local r = nil
                            pcall(function() r = arr[i] end)
                            added = added + #stAddRecipe(r, grp)
                        end
                        if added > 0 then
                            log(string.format("stations: %s adds %d stackable outputs", tostring(row), added))
                        end
                    end
                end
            end
        end
    end

    local okPS, psList = pcall(FindAllOf, "ProcessingStationComponent")
    if okPS and psList then
        for _, ps in ipairs(psList) do
            if valid(ps) then
                local row = nil
                pcall(function() row = ps.ProcessingStationRowHandle.RowName:ToString() end)
                local grp = procGroup(row)
                if grp then
                    local arr, n = nil, 0
                    pcall(function() arr = ps.RecipesCache end)
                    pcall(function() n = arr:GetArrayNum() end)
                    if type(n) == "number" and n > 0 then
                        local added = 0
                        for i = 1, math.min(n, 400) do
                            local r = nil
                            pcall(function() r = arr[i] end)
                            added = added + #stAddRecipe(r, grp)
                        end
                        if added > 0 and yieldCfg.Verbose then
                            log(string.format("stations: %s cache adds %d stackable outputs", tostring(row), added))
                        end
                    end
                end
            end
        end
    end

    -- Also check HUD if running on client/listen host with active UI
    local hud = nil
    pcall(function() hud = FindFirstOf("HUDUISubsystem") end)
    if valid(hud) and not fname(hud):find("^Default__") then
        local ps = nil
        pcall(function() ps = hud.ProcessingStationAPI.ProcessingStationComponent end)
        if valid(ps) then
            local row = nil
            pcall(function() row = ps.ProcessingStationRowHandle.RowName:ToString() end)
            local grp = procGroup(row)
            if grp then
                local arr, n = nil, 0
                pcall(function() arr = ps.RecipesCache end)
                pcall(function() n = arr:GetArrayNum() end)
                if type(n) == "number" then
                    for i = 1, math.min(n, 400) do
                        local r = nil
                        pcall(function() r = arr[i] end)
                        stAddRecipe(r, grp)
                    end
                end
            end
        end
        local cs = nil
        pcall(function() cs = hud.CraftingAPI.CurrentStation end)
        if valid(cs) then
            local row, vendor = nil, nil
            pcall(function() row = cs.CraftingDataRowHandle.RowName:ToString() end)
            pcall(function() vendor = cs.bIsVendor end)
            local arr, n = nil, 0
            pcall(function() arr = cs.ValidRecipes end)
            pcall(function() n = arr:GetArrayNum() end)
            if type(n) ~= "number" then n = 0 end
            if vendor ~= true and vendor ~= nil then
                local grp = craftGroup(row)
                if grp and n > 0 then
                    for i = 1, math.min(n, 400) do
                        local r = nil
                        pcall(function() r = arr[i] end)
                        stAddRecipe(r, grp)
                    end
                end
            end
        end
    end
end

local stNeedScan = false
pcall(NotifyOnNewObject, "/Script/Dominion.CraftingStationComponent", function() stNeedScan = true end)
pcall(NotifyOnNewObject, "/Script/Dominion.ProcessingStationComponent", function() stNeedScan = true end)

local function stApplyAll()
    local changed = 0
    for _, e in ipairs(ST.entries) do
        local want = stWanted(e)
        if want ~= e.orig then changed = changed + 1 end
        stWrite(e, want)
    end
    ST.appliedKey = stSettingsKey()
    local on = {}
    for _, k in ipairs(STATION_ORDER) do
        if stationRate(k) > 1.0 then on[#on + 1] = string.format("%s x%.1f", STATION_LABEL[k], stationRate(k)) end
    end
    log(string.format("stations: %d of %d outputs scaled (%s)", changed, #ST.entries,
        #on > 0 and table.concat(on, ", ") or "all off (1.0)"))
end

local function stVerify()
    local n = #ST.entries
    if n == 0 then return end
    local lost, fixed = false, 0
    for _ = 1, math.min(n, 40) do
        if ST.cursor > n then ST.cursor = 1 end
        local e = ST.entries[ST.cursor]
        ST.cursor = ST.cursor + 1
        if not valid(e.recipe) then lost = true; break end
        local want = stWanted(e)
        local ok, cur = pcall(function() return e.recipe.ItemsCreated[e.index].Count end)
        if not ok or type(cur) ~= "number" then lost = true; break end
        if cur ~= want and stWrite(e, want) then fixed = fixed + 1 end
    end
    if fixed > 0 and yieldCfg.Verbose then log(string.format("stations: re-applied %d outputs the game had reset", fixed)) end
    if lost then
        local keep, gone = {}, 0
        for _, e in ipairs(ST.entries) do
            local ok = valid(e.recipe) and pcall(function() return e.recipe.ItemsCreated[e.index].Count end)
            if ok then keep[#keep + 1] = e else ST.byRecipe[e.rname] = nil; gone = gone + 1 end
        end
        if yieldCfg.Verbose then log(string.format("stations: %d outputs went away (asset reloaded), looking them up again", gone)) end
        ST.entries, ST.cursor = keep, 1
        ST.dtDone, ST.dtNextTry, ST.appliedKey = false, 0, nil
    end
end

local nextComponentScan = 0

local function stationTick(now)
    stDiscoverTable(now)

    if stNeedScan or now >= nextComponentScan then
        stNeedScan = false
        nextComponentScan = now + 4.0
        pcall(stScanComponents)
    end

    if ST.appliedKey ~= stSettingsKey() then
        stApplyAll()
    else
        stVerify()
    end
end

--------------------------------------------------------------------------------
-- 4. Master Engine Loop on Game Thread
--------------------------------------------------------------------------------
local nextLootCheck = 0
local nextConfigCheck = 0
local nextStationTick = 0

local function masterTick()
    local now = os.clock()

    -- 1. Periodic Config Reload (every 5 seconds)
    if now >= nextConfigCheck then
        nextConfigCheck = now + 5.0
        pcall(loadYieldSettings)
        pcall(loadLootSettings)
        neutralizeLootSliders()
        neutralizeYieldSliders()
    end

    -- 2. LootSliders Table Monitoring (every 2 seconds)
    if now >= nextLootCheck then
        nextLootCheck = now + 2.0
        pcall(lootTick)
    end

    -- 3. YieldSliders Station & Crafting Monitoring (every 1.5 seconds)
    if now >= nextStationTick then
        nextStationTick = now + 1.5
        pcall(stationTick, now)
    end

    -- 4. YieldSliders Player Monitoring (every tick, ~250ms)
    local pcs = authoritativePCs()
    if #pcs > 0 then
        if not loggedServerMode then
            loggedServerMode = true
            log(string.format("authoritative player loop active with %d controller(s)", #pcs))
        end
        local list = getSkills()
        if list then
            for _, pc in ipairs(pcs) do
                pcall(tickPlayer, pc, list, now)
            end
        end
    end
end

LoopInGameThreadWithDelay(250, function()
    pcall(masterTick)
    return false
end)

log("v1.2 initialized successfully as a compiled DLL companion mod (YieldSliders & LootSliders Server Support).")
)LUA";

class LootYieldMod : public RC::CppUserModBase {
private:
    bool m_initialized = false;

    void execute_payload(RC::LuaMadeSimple::Lua& lua) {
        if (m_initialized) return;
        m_initialized = true;
        try {
            lua.execute_string(LOOTYIELD_ENGINE_PAYLOAD);
        } catch (...) {
        }
    }

public:
    LootYieldMod() : CppUserModBase() {
        ModName = L"LootYield";
        ModVersion = L"1.2.0";
        ModDescription = L"Dedicated Server Companion Mod for YieldSliders & LootSliders";
        ModAuthors = L"Alfius";
    }

    ~LootYieldMod() override = default;

    // Slot 5 (0x28): on_lua_start without mod_name
    void vslot5_on_lua_start(RC::LuaMadeSimple::Lua& lua,
                             RC::LuaMadeSimple::Lua& main_lua,
                             RC::LuaMadeSimple::Lua& async_lua,
                             RC::LuaMadeSimple::Lua* hook_lua) override {
        execute_payload(lua);
    }

    // Slot 6 (0x30): on_lua_start with mod_name (called by UE4SS at [rax+30h]!)
    void vslot6_on_lua_start(std::wstring_view mod_name,
                             RC::LuaMadeSimple::Lua& lua,
                             RC::LuaMadeSimple::Lua& main_lua,
                             RC::LuaMadeSimple::Lua& async_lua,
                             RC::LuaMadeSimple::Lua* hook_lua) override {
        execute_payload(lua);
    }

    // Slot 7 (0x38): vector without mod_name
    void vslot7_on_lua_start_vec(RC::LuaMadeSimple::Lua& lua,
                                 RC::LuaMadeSimple::Lua& main_lua,
                                 RC::LuaMadeSimple::Lua& async_lua,
                                 std::vector<RC::LuaMadeSimple::Lua*>& hook_luas) override {
        execute_payload(lua);
    }

    // Slot 8 (0x40): vector with mod_name
    void vslot8_on_lua_start_vec(std::wstring_view mod_name,
                                 RC::LuaMadeSimple::Lua& lua,
                                 RC::LuaMadeSimple::Lua& main_lua,
                                 RC::LuaMadeSimple::Lua& async_lua,
                                 std::vector<RC::LuaMadeSimple::Lua*>& hook_luas) override {
        execute_payload(lua);
    }
};

extern "C" {
    __declspec(dllexport) RC::CppUserModBase* start_mod() {
        return new LootYieldMod();
    }

    __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod) {
        delete mod;
    }
}