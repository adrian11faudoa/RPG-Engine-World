# RealmForge Engine 🏰
### A Next-Generation 3D Virtual Tabletop RPG Platform

> *"Where every session becomes a legend."*

---

## Overview

RealmForge Engine is a full-featured, moddable 3D virtual tabletop RPG platform built on Unreal Engine 5. It combines the immersive 3D world of TaleSpire, the GM toolset of Foundry VTT, and the production quality of a commercial game — delivered as an open, extensible platform.

---

## Feature Matrix

| Feature | Status | Notes |
|---------|--------|-------|
| 3D Map Editor | ✅ Core | Grid-based, multi-layer |
| Fog of War | ✅ Core | Bresenham LOS, smooth reveal |
| Miniatures | ✅ Core | HP, status, movement, selection rings |
| Dice System | ✅ Core | Physics d4–d20, formulas, chat commands |
| Initiative Tracker | ✅ Core | Auto-sort, delay/ready, round counter |
| Multiplayer | ✅ Core | UE5 OnlineSubsystem, LAN + Online |
| GM Controller | ✅ Core | Hidden layers, notes, scene switch |
| Campaign Save | ✅ Core | Binary + JSON, auto-save |
| Procedural Dungeon | ✅ Core | BSP rooms, corridors, themes |
| Lua Modding | ✅ Core | Sandboxed per-mod Lua states |
| AI NPCs (Ollama) | ✅ Optional | Local LLM, fallback responses |
| Dynamic Lighting | ✅ UE5 Lumen | Torch radius, day/night |
| Volumetric Fog | ✅ UE5 | Weather system integration |
| Voice Chat | 🔄 Planned | EOS overlay or Vivox |
| VR Support | 🔄 Planned | OpenXR plugin |
| Mobile Companion | 🔄 Planned | React Native app |
| Steam Workshop | 🔄 Planned | Mod distribution |

---

## Architecture

```
RealmForgeEngine/
│
├── Source/RealmForge/
│   ├── Core/           GameInstance, SaveGame, Settings
│   ├── Networking/     Session hosting, roles, reconnection
│   ├── World/          Map editor, tiles, fog, lighting, weather, proc dungeon
│   ├── Miniatures/     Token system, HP, movement, status effects
│   ├── Combat/         Dice, initiative, spell templates, status effects
│   ├── GM/             GM controller, notes, encounter builder, scene director
│   ├── UI/             HUD, all panel widgets, radial menus
│   ├── Campaign/       Save/load, quests, journal, NPC database
│   ├── Audio/          Ambient zones, dynamic music, SFX manager
│   ├── Modding/        Lua loader, asset registry, SDK
│   └── AI/             Ollama HTTP client, NPC dialogue, generation
│
├── Content/            UE5 assets (maps, meshes, VFX, UI, audio)
├── Mods/SDK/           Example mod + Lua API reference
├── Config/             Engine, game, scalability INI files
└── Docs/               Build guide, networking, modding SDK, asset import
```

---

## Core Systems Deep-Dive

### Fog of War (`RFFogOfWar`)
- **64×64 default grid** (configurable up to 256×256)
- **Bresenham line-of-sight** raycasting per miniature movement
- **Three states:** Hidden (black) → Explored (dimmed) → Visible (full)
- **Smooth transitions** via per-frame alpha interpolation
- **Texture output:** UTexture2D updated every frame, drives a decal material
- **GM paint tool:** brush-based reveal/hide at any radius
- **Flood-fill reveal:** for GM "reveal this room" workflow
- **Multicast RPC:** cell changes propagated to all clients efficiently

### Dice System (`ARFDiceManager`)
- **Formula parser:** `"2d8+3"`, `"d20 adv"`, `"4d6kh3"` (keep highest)
- **Chat commands:** `/roll`, `/gmroll` (GM-only), `/initiative`
- **Advantage/disadvantage:** roll-twice, pick high/low
- **Critical detection:** nat-20 on d20 doubles damage dice
- **Visibility tiers:** Public / GM-Only / Private (Client RPC)
- **Roll history:** last 200 rolls in memory, filterable
- **Physics dice:** Blueprint-driven 3D dice throw in dice tray

### Miniature System (`ARFMiniatureBase`)
- **Creature sizes:** Tiny–Gargantuan (auto-scales mesh + selection ring)
- **Turn resources:** Action, Bonus Action, Reaction, Movement (reset each turn)
- **Movement validation:** server-side, remaining-movement check, anti-cheat distance clamp
- **Status effects:** named, colored, round-countdown, auto-expire
- **HP layers:** Temp HP absorbs before regular HP
- **Replicated stats:** full DOREPLIFETIME on Stats struct, OnRep callbacks

### Multiplayer (`URFNetworkManager`)
- **UE5 OnlineSubsystem** abstraction (NULL for LAN, Steam for release)
- **Session metadata:** CampaignName, GMName stored in session settings
- **Role system:** GameMaster, AssistantGM, Player, Spectator
- **Reconnection:** auto-retry on ConnectionLost/Timeout failure
- **Permission enforcement:** Server RPCs validate GM role before sensitive ops

### GM Controller (`ARFGMController`)
- **Hidden object layers:** tag-based (`GM_Layer_Secrets`)
- **Secret notes:** world-attached or floating, GM-only visibility
- **Scene switching:** Cut / Fade / Wipe / Cinematic transitions
- **Monster spawning:** with auto-initiative-add if in combat
- **Encounter templates:** save/load CR-rated encounter presets
- **Camera bookmarks:** named save-points with smooth interpolation
- **Ping system:** multicast world-space pings with color + label

### Procedural Dungeon (`ARFProceduralDungeon`)
- **BSP-inspired room placement** with overlap detection + padding
- **L-shaped corridor carving** with configurable width
- **Loop corridors:** ~15% chance of secondary connections
- **Room type assignment:** largest room = boss, first = entrance, last = exit
- **Tile types:** Empty, Floor, Wall, Door, Stairs, Trap, Chest
- **Theme system:** maps to tileset packs per theme
- **Seeded generation:** reproducible dungeons via `Config.Seed`
- **Room descriptions:** procedural atmospheric text per room type

### Campaign Manager (`URFCampaignManager`)
- **Binary save format** (FArchive) + JSON sidecar for tooling
- **Auto-save timer:** configurable interval (default 5 min)
- **World state KV store:** flexible flag system for story tracking
- **Quest system:** objectives, completion tracking, active filter
- **NPC database:** per-NPC notes by topic, attitude tracking
- **Journal:** GM-only and shared entries, timestamps

### Modding (`URFModLoader`)
- **Per-mod sandboxed Lua states** (isolation, no cross-mod leaks)
- **Manifest-driven:** `manifest.json` declares entry, version, deps
- **Lua API exposed:** `RF.RegisterAsset`, `RF.RollDice`, `RF.SpawnMiniature`, `RF.AddChatMessage`, `RF.RegisterChatCommand`, `RF.OnRoundStart`, `RF.RegisterUIPanel`, and more
- **Hot-reload friendly:** unload/reload without restarting

### AI Integration (`URFOllamaIntegration`)
- **Local Ollama** (llama3 default) — fully offline, no cloud dependency
- **NPC dialogue:** personality + history context, 1-3 sentence responses
- **Dungeon generation:** room-by-room atmospheric descriptions
- **Quest hooks:** CR-appropriate, context-aware
- **GM assistant:** rules lookup, encounter balancing, story ideas
- **Graceful fallback:** static responses when Ollama unavailable
- **Request cancellation:** by UUID, prevents stale response handling

---

## Multiplayer Roles & Permissions

```
Game Master
  ├── Full map edit
  ├── All GM tools (fog, layers, notes, scene)
  ├── Spawn/despawn all tokens
  ├── Roll public and GM-only dice
  ├── Assign player permissions
  └── Switch scenes / end session

Assistant GM
  ├── Map edit (if granted)
  ├── Spawn monsters
  └── Roll GM dice

Player
  ├── Move own token
  ├── Roll public dice
  ├── Chat
  └── View non-hidden objects

Spectator
  └── View only, no interaction
```

---

## Modding SDK Quick Reference

```lua
-- Assets
RF.RegisterAsset({ id, displayName, category, meshPath, iconPath, stats })

-- World
RF.GetMiniaturesInCone(origin, range, angleDeg)
RF.GetMiniaturesInRadius(origin, range)
RF.ApplyDamage(targetName, amount, damageType)
RF.SpawnMiniature(assetId, location)

-- Dice
RF.RollDice("2d6+3")           -- Returns integer
RF.RollDice("d20 adv")
RF.RollDice("4d6kh3")          -- Keep highest 3

-- Campaign
RF.GetWorldFlag(key)
RF.SetWorldFlag(key, value)

-- Chat
RF.AddChatMessage(text, style)  -- styles: "normal", "roll", "damage", "spell", "system"
RF.RegisterChatCommand("/cmd", function(args, playerName) end)

-- Events
RF.OnRoundStart(function(round) end)
RF.OnTurnStart(function(miniName, round) end)
RF.OnCombatEnd(function() end)
RF.OnMiniatureDamaged(function(miniName, amount, type) end)

-- UI
RF.RegisterUIPanel({ id, title, icon, render = function() end })
RF.UI.Label(text, opts)
RF.UI.Button(label, onClick)
RF.UI.Separator()
RF.UI.Slider(label, min, max, default, onChange)
```

---

## Performance Targets

| Scenario | Target FPS |
|----------|-----------|
| 64×64 map, 4 players, all features | 60+ FPS |
| 80×80 map, 8 players, 30 tokens | 45+ FPS |
| Large outdoor map (256×256) | 30+ FPS |

---

## Contributing

1. Fork the repo
2. Create feature branch: `git checkout -b feature/my-system`
3. Write tests for new systems
4. Submit PR with system description + screenshots

---

## License

MIT License — free for personal and commercial use.

---

*RealmForge Engine is inspired by TaleSpire, Foundry VTT, The RPG Engine, and the D&D/Pathfinder communities.*
