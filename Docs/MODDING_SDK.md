# RealmForge Engine — Modding SDK Reference
### Lua Scripting API v1.0

---

## Getting Started

### 1. Create Your Mod Folder

```
Mods/
└── my_awesome_mod/
    ├── manifest.json
    ├── main.lua
    ├── assets/
    │   ├── meshes/
    │   ├── icons/
    │   └── audio/
    └── README.md
```

### 2. Write Your Manifest

```json
{
  "modID":       "my_awesome_mod",
  "displayName": "My Awesome Mod",
  "version":     "1.0.0",
  "author":      "Your Name",
  "description": "What this mod does",
  "entryScript": "main.lua",
  "dependencies": []
}
```

### 3. Write Your Entry Script (`main.lua`)

```lua
local mod = {}

-- Your mod code here

return mod
```

---

## Full API Reference

### Asset Registration

```lua
RF.RegisterAsset({
  id          = "my_asset_id",      -- unique, no spaces
  displayName = "Display Name",
  category    = "creature",          -- creature | prop | spell | tile | weapon | armor
  meshPath    = "assets/meshes/my_mesh.uasset",
  iconPath    = "assets/icons/my_icon.png",

  -- Optional: creature stats
  stats = {
    hp     = 50,   maxHp  = 50,
    ac     = 14,   speed  = 30,
    str=10, dex=14, con=12, int=8, wis=10, cha=6,
    size   = "Medium",   -- Tiny|Small|Medium|Large|Huge|Gargantuan
    cr     = 2,
    type   = "undead",
    immunities     = { "poison", "necrotic" },
    resistances    = { "cold" },
    vulnerabilities = { "radiant" },
    attacks = {
      { name="Claw", hit=4, damage="1d6+2", type="slashing" },
      { name="Bite", hit=4, damage="1d8+2", type="piercing" },
    }
  }
})
```

---

### Dice & Rolls

```lua
-- Roll and get result (integer)
local result = RF.RollDice("2d6+3")
local attack  = RF.RollDice("1d20")
local damage  = RF.RollDice("3d8")
local adv     = RF.RollDice("d20 adv")      -- advantage
local dis     = RF.RollDice("d20 dis")      -- disadvantage

-- Roll with label (shows in chat)
local result = RF.RollDiceLabeled("1d20", "Stealth Check")

-- Roll saving throw (returns { roll, total, success })
local save = RF.RollSave("DEX", 13, "Fireball Save")

-- Roll attack (returns { roll, total, isHit, isCrit })
local atk = RF.RollAttack(5, "normal", "Shortsword Attack")
```

---

### World & Miniatures

```lua
-- Get miniatures
local all         = RF.GetAllMiniatures()
local players     = RF.GetMiniaturesByType("player")
local monsters    = RF.GetMiniaturesByType("monster")
local selected    = RF.GetSelectedMiniature()

-- Spatial queries
local inRadius    = RF.GetMiniaturesInRadius(location, 30)   -- 30ft radius
local inCone      = RF.GetMiniaturesInCone(caster, 15, 90)   -- 15ft, 90deg cone
local inLine      = RF.GetMiniaturesInLine(start, direction, 60, 5)  -- line

-- Stats
local hp    = RF.GetHP(miniName)
local maxHp = RF.GetMaxHP(miniName)
local mod   = RF.GetStatMod(miniName, "STR")   -- returns modifier integer
local ac    = RF.GetAC(miniName)
local speed = RF.GetSpeed(miniName)

-- Modify
RF.ApplyDamage(miniName, amount, "fire")
RF.Heal(miniName, amount)
RF.AddTempHP(miniName, amount)
RF.SetHP(miniName, newHP)
RF.Kill(miniName)
RF.AddStatusEffect(miniName, "Stunned", 1)     -- name, rounds (-1=permanent)
RF.RemoveStatusEffect(miniName, "Stunned")
RF.HasStatusEffect(miniName, "Prone")          -- returns bool

-- Movement
RF.MoveMiniature(miniName, {x=100, y=200})
RF.TeleportMiniature(miniName, {x=100, y=200})
RF.GetLocation(miniName)                        -- returns {x, y, z}
RF.GetDistanceFeet(nameA, nameB)                -- returns number

-- Spawn & remove
RF.SpawnMiniature(assetId, {x=100, y=200})
RF.DespawnMiniature(miniName)
```

---

### Chat & Communication

```lua
-- Add messages to chat log
RF.AddChatMessage("Hello world!")
RF.AddChatMessage("🔥 Spell cast!", "spell")    -- styles: normal|roll|damage|spell|system|gm
RF.AddGMOnlyMessage("Only the GM sees this")

-- Register chat commands
RF.RegisterChatCommand("/myspell", function(args, playerName)
  -- args is a table of strings after the command
  -- playerName is who typed it
  local target = args[1] or "nearest"
  RF.AddChatMessage(playerName .. " casts on " .. target)
end)

-- Ping the map
RF.PingLocation({x=300, y=200}, "Look here!", "yellow")
```

---

### Campaign & World State

```lua
-- Simple key-value flags
RF.SetWorldFlag("dragon_killed", "true")
RF.SetWorldFlag("gold_found", "500")
local killed = RF.GetWorldFlag("dragon_killed")   -- returns string
local gold   = tonumber(RF.GetWorldFlag("gold_found", "0"))

-- Boolean helper
if RF.GetWorldFlagBool("castle_unlocked") then
  RF.AddChatMessage("The castle gates open!")
end

-- Journal
RF.AddJournalEntry("The Battle of Ironhold", "The party defeated the lich lord at last.", "GM")

-- Quest tracking
RF.CompleteObjective("main_quest", "defeat_boss")
```

---

### Events

```lua
-- Combat events
RF.OnCombatStart(function()
  RF.AddChatMessage("⚔️ Battle begins!", "system")
end)

RF.OnCombatEnd(function()
  RF.AddChatMessage("🏆 Victory!", "system")
end)

RF.OnRoundStart(function(round)
  RF.AddChatMessage("Round " .. round .. " begins", "system")
end)

RF.OnTurnStart(function(miniName, round)
  local hp = RF.GetHP(miniName)
  RF.AddChatMessage(miniName .. "'s turn (HP: " .. hp .. ")", "normal")
end)

-- Miniature events
RF.OnMiniatureDamaged(function(miniName, amount, damageType)
  if amount >= 20 then
    RF.AddChatMessage("💥 MASSIVE DAMAGE on " .. miniName .. "!", "damage")
  end
end)

RF.OnMiniatureDefeated(function(miniName)
  RF.AddChatMessage(miniName .. " falls!", "system")
end)

RF.OnMiniatureMoved(function(miniName, from, to)
  -- from and to are {x, y} tables
end)

-- Map events
RF.OnObjectPlaced(function(assetId, location) end)
RF.OnFogRevealed(function(cells) end)  -- cells = array of {x, y}

-- Session events
RF.OnPlayerJoined(function(playerName) end)
RF.OnPlayerLeft(function(playerName) end)
RF.OnSceneSwitched(function(newMap) end)
```

---

### Custom UI Panels

```lua
RF.RegisterUIPanel({
  id    = "my_panel",
  title = "My Panel",
  icon  = "assets/icons/panel.png",
  render = function()
    -- UI builder functions
    RF.UI.Label("Header Text", { bold = true, color = "gold" })
    RF.UI.Label("Normal text")
    RF.UI.Separator()

    RF.UI.Button("Click Me", function()
      RF.AddChatMessage("Button clicked!")
    end)

    RF.UI.Slider("Damage Bonus", 0, 10, 0, function(value)
      RF.SetWorldFlag("damage_bonus", tostring(value))
    end)

    RF.UI.Dropdown("Element", {"Fire", "Ice", "Lightning"}, function(selected)
      RF.SetWorldFlag("element", selected)
    end)

    RF.UI.Checkbox("Auto-roll damage", false, function(checked)
      RF.SetWorldFlag("auto_damage", tostring(checked))
    end)

    RF.UI.Space(8)
    RF.UI.Image("assets/icons/spell.png", 32, 32)
  end
})
```

---

### Audio

```lua
RF.PlaySound("assets/audio/thunder.wav")
RF.PlaySoundAtLocation("assets/audio/footsteps.wav", {x=100, y=200})
RF.SetAmbientTrack("assets/audio/dungeon_ambient.mp3")
RF.StopAmbientTrack()
```

---

### Utility

```lua
-- Math helpers
RF.FeetToUnrealUnits(30)    -- returns 600 (30ft × 20)
RF.UnrealUnitsToFeet(600)   -- returns 30

-- String helpers
RF.Format("Hello, {1}! You have {2} HP.", playerName, hp)

-- Random
RF.Random(1, 100)           -- integer between 1-100
RF.RandomFloat(0, 1)

-- Logging (shows in mod console)
RF.Log("Debug message")
RF.LogWarning("Something odd")
RF.LogError("Something broke")

-- Timer
local timerId = RF.SetTimer(5.0, function()
  RF.AddChatMessage("5 seconds passed!")
end)
RF.ClearTimer(timerId)

-- Recurring timer
RF.SetInterval(60.0, function()
  RF.AddChatMessage("One minute elapsed in session")
end)
```

---

## Example: Full Spell Mod

```lua
-- Thunderwave spell implementation
local mod = {}

RF.RegisterChatCommand("/thunderwave", function(args, caster)
  local dc = 14
  local range = 15  -- feet, cube

  local targets = RF.GetMiniaturesInCone(
    RF.GetLocation(caster), range, 180)

  RF.AddChatMessage(
    string.format("⚡ %s casts Thunderwave! DC %d CON save", caster, dc), "spell")

  for _, target in ipairs(targets) do
    if target == caster then goto continue end

    local save = RF.RollSave("CON", dc, target .. " CON Save")
    local dmg  = RF.RollDice("2d8")
    local taken = save.success and math.floor(dmg / 2) or dmg

    RF.ApplyDamage(target, taken, "thunder")
    RF.AddChatMessage(string.format(
      "  → %s %s (takes %d thunder damage)",
      target,
      save.success and "saves, half damage" or "FAILS",
      taken), "damage")

    -- Knock back on fail
    if not save.success then
      RF.AddChatMessage("  → " .. target .. " is pushed 10ft!", "normal")
      -- Movement handled by GM/system
    end

    ::continue::
  end
end)

return mod
```

---

## Publishing Your Mod

1. Test locally with the in-game Mod Manager
2. Zip your mod folder: `my_awesome_mod.zip`
3. Upload to the RealmForge Mod Portal (coming soon)
4. *(Future)* Publish to Steam Workshop

---

*RealmForge Engine Modding SDK — v1.0*
