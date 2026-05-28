-- ============================================================
-- RealmForge Engine — Example Mod: "Burning Hands Spell"
-- Demonstrates the Mod SDK API
-- ============================================================

local mod = {}
mod.name    = "Burning Hands"
mod.version = "1.0.0"
mod.author  = "ExampleModder"

-- ─── 1. Register a custom spell asset ────────────────────────
RF.RegisterAsset({
    id          = "spell_burning_hands",
    displayName = "Burning Hands",
    category    = "spell",
    iconPath    = "mods/burning_hands/icons/burning_hands.png",
    meshPath    = "mods/burning_hands/fx/cone_fire.uasset",
})

-- ─── 2. Register a chat command ──────────────────────────────
RF.RegisterChatCommand("/burninghands", function(args, caster)
    local range  = 15   -- feet
    local damage = RF.RollDice("3d6")

    -- Notify the table
    RF.AddChatMessage(
        string.format("🔥 %s casts Burning Hands! Cone of fire, %d ft — %dd6 = %d fire damage",
            caster, range, 3, damage),
        "spell"
    )

    -- Apply to targets in cone
    local targets = RF.GetMiniaturesInCone(caster, range, 90)
    for _, target in ipairs(targets) do
        local saved = RF.RollDice("1d20") + RF.GetStatMod(target, "DEX") >= 13
        local dmg   = saved and math.floor(damage / 2) or damage

        RF.ApplyDamage(target, dmg, "fire")
        RF.AddChatMessage(
            string.format("  → %s takes %d fire damage%s",
                target, dmg, saved and " (saved, half)" or ""),
            "damage"
        )
    end
end)

-- ─── 3. React to round events ────────────────────────────────
RF.OnRoundStart(function(round)
    if round == 1 then
        RF.AddChatMessage("⚡ Burning Hands mod loaded — /burninghands to cast!", "system")
    end
end)

-- ─── 4. Register a custom creature ───────────────────────────
RF.RegisterAsset({
    id          = "creature_fire_elemental",
    displayName = "Fire Elemental",
    category    = "creature",
    iconPath    = "mods/burning_hands/icons/fire_elemental.png",
    meshPath    = "mods/burning_hands/meshes/fire_elemental.uasset",
    stats = {
        hp       = 102,
        ac       = 13,
        speed    = 50,
        str      = 10,
        dex      = 17,
        con      = 16,
        int      = 6,
        wis      = 10,
        cha      = 7,
        size     = "Large",
        cr       = 5,
        immunities = { "fire", "poison" },
        vulnerabilities = { "cold" },
    }
})

-- ─── 5. Custom UI panel (registered as sidebar widget) ────────
RF.RegisterUIPanel({
    id    = "burning_hands_panel",
    title = "🔥 Fire Spells",
    icon  = "mods/burning_hands/icons/panel_icon.png",
    render = function()
        RF.UI.Label("Burning Hands (1st level)", { bold = true })
        RF.UI.Label("Cone: 15ft | Damage: 3d6 fire")
        RF.UI.Separator()
        RF.UI.Button("Cast Burning Hands", function()
            RF.ExecuteCommand("/burninghands")
        end)
        RF.UI.Button("Roll Fire Damage", function()
            local dmg = RF.RollDice("3d6")
            RF.AddChatMessage("🎲 Fire damage: " .. dmg)
        end)
    end
})

return mod
