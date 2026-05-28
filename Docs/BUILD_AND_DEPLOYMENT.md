# RealmForge Engine — Complete Build & Deployment Guide

---

## Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| Unreal Engine 5 | 5.3+ | Install via Epic Games Launcher |
| Visual Studio | 2022 | With C++ Game Development workload |
| Git LFS | Latest | For large asset tracking |
| CMake | 3.25+ | Optional, for Lua plugin build |
| Node.js | 18+ | For companion web tools |

---

## 1. Project Setup

```bash
# Clone the repository
git clone https://github.com/your-org/RealmForgeEngine.git
cd RealmForgeEngine
git lfs pull

# Generate Visual Studio project files
"C:/Program Files/Epic Games/UE_5.3/Engine/Build/BatchFiles/GenerateProjectFiles.bat" \
  RealmForgeEngine.uproject -Game -Engine
```

Open `RealmForgeEngine.sln` in Visual Studio 2022.

---

## 2. Third-Party Dependencies

### Lua 5.4 (for Modding)
```bash
# Included as a Source plugin in Plugins/LuaPlugin/
# No separate installation needed — compiles with the project
```

### LuaBridge3 (C++ ↔ Lua bindings)
```bash
# Place in Source/ThirdParty/LuaBridge/
# Header-only library, no compilation step
```

### OnlineSubsystem (for Multiplayer)
In `Config/DefaultEngine.ini`:
```ini
[OnlineSubsystem]
DefaultPlatformService=NULL   ; Change to Steam for production

[OnlineSubsystemSteam]
bEnabled=true
SteamDevAppId=480              ; Replace with your Steam App ID
bInitServerOnClient=true

[OnlineSubsystemNull]
bEnabled=true                  ; For LAN/offline testing
```

---

## 3. Build Configurations

| Config | Use Case |
|--------|----------|
| `Debug Game` | Full debug symbols, slow |
| `Development` | Iterating on gameplay |
| `Shipping` | Final release build |

```bash
# Command-line build (CI/CD)
"C:/Program Files/Epic Games/UE_5.3/Engine/Build/BatchFiles/Build.bat" \
  RealmForgeGame Win64 Development \
  "D:/Projects/RealmForgeEngine/RealmForgeEngine.uproject" \
  -waitmutex
```

---

## 4. Multiplayer Deployment

### Option A: Listen Server (GM hosts)
No dedicated server needed. The GM's machine runs as host.

In `RFNetworkManager.cpp`, `HostSession()` calls `World->ServerTravel(...?listen)` automatically.

### Option B: Dedicated Server

**Build dedicated server:**
```bash
"C:/Program Files/Epic Games/UE_5.3/Engine/Build/BatchFiles/Build.bat" \
  RealmForgeServer Win64 Development \
  "D:/Projects/RealmForgeEngine/RealmForgeEngine.uproject"
```

**Launch server:**
```bash
RealmForgeServer.exe /Game/Maps/DungeonOfShadows \
  -server -log -port=7777 \
  -MaxPlayers=8 \
  -CampaignName="The Sunken Keep"
```

**Launch client:**
```bash
RealmForgeGame.exe 192.168.1.100:7777
```

### Docker Deployment
```dockerfile
FROM ghcr.io/epicgames/unreal-engine:dev-server-5.3

WORKDIR /realmforge
COPY LinuxServer/ .

EXPOSE 7777/udp
EXPOSE 7778/udp

CMD ["./RealmForgeServer.sh", \
     "-log", "-port=7777", \
     "-MaxPlayers=8"]
```

```yaml
# docker-compose.yml
version: '3.8'
services:
  realmforge-server:
    build: .
    ports:
      - "7777:7777/udp"
      - "7778:7778/udp"
    volumes:
      - ./Campaigns:/realmforge/Saved/Campaigns
    environment:
      - RF_MAX_PLAYERS=8
      - RF_AUTO_SAVE_INTERVAL=300
    restart: unless-stopped
```

---

## 5. Content & Asset Organization

```
Content/
├── Maps/
│   ├── ExampleMaps/
│   │   ├── DungeonOfShadows.umap   ← Main example dungeon
│   │   ├── TavernOfTales.umap      ← Social/RP environment
│   │   └── OpenFieldBattle.umap    ← Outdoor combat map
│   ├── Templates/
│   │   ├── Template_5x5_Indoor.umap
│   │   └── Template_GridOutdoor.umap
│   └── Lobby.umap                  ← Session lobby/waiting room
│
├── Miniatures/
│   ├── Heroes/
│   │   ├── SK_Fighter_01           ← Skeletal mesh
│   │   ├── SK_Rogue_01
│   │   └── SK_Wizard_01
│   ├── Monsters/
│   │   ├── SK_Skeleton
│   │   ├── SK_Dragon_Young
│   │   └── SK_ShadowWraith
│   └── Bases/
│       └── SM_TokenBase_Round      ← Base ring mesh
│
├── Props/
│   ├── Walls/      ← SM_Wall_Stone, SM_Wall_Wood...
│   ├── Floors/     ← SM_Floor_Stone, SM_Floor_Dirt...
│   ├── Furniture/  ← SM_Table, SM_Chair, SM_Barrel...
│   ├── Nature/     ← SM_Tree_Oak, SM_Rock_01...
│   └── Dungeon/    ← SM_Torch, SM_Door_Iron, SM_Chest...
│
├── VFX/
│   ├── Spells/
│   │   ├── NS_Fireball
│   │   ├── NS_LightningBolt
│   │   └── NS_HealingWord
│   ├── Dice/
│   │   └── NS_DiceRoll_Gold
│   └── Ambient/
│       ├── NS_TorchFlame
│       └── NS_DustMotes
│
└── Materials/
    ├── M_FogOfWar              ← Uses fog texture from URFFogOfWar
    ├── M_SelectionRing         ← Animated decal
    ├── M_MovementRange         ← Dashed circle decal
    └── M_TokenBase             ← Per-instance color via MID
```

---

## 6. Save System

Campaigns save to:
- **Windows:** `%LOCALAPPDATA%/RealmForge/Saved/Campaigns/`
- **Linux:** `~/.local/share/RealmForge/Campaigns/`

Format: Binary (FArchive serialization) + JSON sidecar for readability.

Auto-save triggers:
- Every 5 minutes (configurable)
- On scene change
- On session end
- On explicit `/save` command

---

## 7. Modding Setup

1. Create folder: `Mods/YourModName/`
2. Create `manifest.json`:
```json
{
  "modID": "your_mod_name",
  "displayName": "Your Mod Name",
  "version": "1.0.0",
  "author": "You",
  "entryScript": "main.lua",
  "dependencies": []
}
```
3. Write `main.lua` using the RF Lua API (see `Mods/SDK/ExampleMod/`)
4. Place mod folder in game `Mods/` directory
5. Enable via in-game Settings → Mods

---

## 8. Ollama AI Setup (Optional)

```bash
# Install Ollama
curl -fsSL https://ollama.ai/install.sh | sh

# Pull a model (llama3 recommended for RPG dialogue)
ollama pull llama3

# Start server (runs on localhost:11434)
ollama serve
```

In-game: Settings → AI → Enable AI Features → Test Connection.

---

## 9. Performance Tuning

| Setting | Recommended (High-End) | Recommended (Mid) |
|---------|----------------------|-------------------|
| Shadow Quality | Epic | High |
| Lumen GI | Enabled | Software Lumen |
| Nanite | Enabled | Disabled |
| Fog of War Res | 128×128 | 64×64 |
| Max Players | 8 | 6 |
| Auto-LOD | On | On |

Key console variables:
```ini
# Config/DefaultScalability.ini
r.Shadow.MaxResolution=2048
r.Lumen.DiffuseIndirect.Allow=1
r.Nanite.MaxPixelsPerEdge=1
rf.FogTexSize=64          ; Custom CVAR for fog resolution
rf.MaxMiniatures=32       ; Cap miniature count
rf.NetworkTickRate=30     ; Server tick rate
```

---

## 10. Networking Topology

```
┌─────────────────────────────────────────────┐
│              REALMFORGE SESSION              │
│                                             │
│  ┌──────────────┐     TCP/UDP 7777          │
│  │  GM Client   │◄──────────────────────┐  │
│  │ (Host or DS) │                       │  │
│  └──────┬───────┘                       │  │
│         │ ServerTravel / RPC            │  │
│  ┌──────▼───────┐  ┌─────────────┐      │  │
│  │ Player 1     │  │ Player 2    │      │  │
│  │ Client       │  │ Client      │      │  │
│  └──────────────┘  └─────────────┘      │  │
│                                          │  │
│  Replicated Actors: Miniatures, Fog,    │  │
│  Dice, Initiative, Chat, Props           │  │
└─────────────────────────────────────────────┘
```

RPCs by direction:
- `Server_*` — Client → Server (validated)
- `Multicast_*` — Server → All Clients
- `Client_*` — Server → Specific Client (private rolls, GM-only data)

---

## 11. Known Issues & Workarounds

| Issue | Workaround |
|-------|-----------|
| Fog texture flicker on join | Call `InitializeFog()` on `OnRep_` in FoW component |
| Physics dice tunnelling | Set `bCCD=true` on dice mesh component |
| Lua state memory leak | Call `CloseLuaState()` on mod unload |
| Large map streaming stutter | Set `s.LevelStreamingActorsUpdateTimeLimit=2` |

---

*RealmForge Engine — Build Guide v1.0*
*See also: NETWORKING.md, MODDING_SDK.md, ASSET_IMPORT.md*
