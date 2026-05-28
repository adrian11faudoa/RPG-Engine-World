# RealmForge Engine — Asset Import Guide

---

## Supported Formats

| Type | Formats | Notes |
|------|---------|-------|
| 3D Meshes | FBX, GLTF, GLB, OBJ | FBX preferred for rigged characters |
| Textures | PNG, TGA, JPG, EXR | PNG for most, EXR for HDR |
| Audio | WAV, OGG, MP3 | WAV for SFX, OGG for music |
| Animations | FBX (embedded) | Separate anim FBX or embedded |

---

## Miniature / Token Import

### Mesh Requirements

| Property | Requirement |
|----------|-------------|
| Poly budget | ≤15,000 tris (LOD0) |
| Scale | 1 UU = 1 cm (Medium = ~180cm tall) |
| Origin | Base of feet, centered X/Y |
| Normals | Smooth groups set correctly |
| UV channels | UV0 = diffuse, UV1 = lightmap |
| Skeleton | Optional for animated miniatures |

### Texture Maps

| Map | Size | Format |
|-----|------|--------|
| Albedo/Base Color | 1024×1024 | PNG |
| Normal | 1024×1024 | PNG (DirectX) |
| ORM (Occlusion/Roughness/Metal) | 1024×1024 | PNG (packed) |
| Emissive | 512×512 | PNG (optional) |

### Import Steps (Unreal Editor)

1. **Drag-drop** your FBX into `Content/Miniatures/Custom/`
2. Import dialog settings:
   - ☑ Import Mesh
   - ☑ Import Normals & Tangents
   - ☐ Import Materials (we use RF material)
   - ☑ Import Textures
   - ☑ Import Animations (if applicable)
   - Skeleton: assign `SK_RealmForge_Base` if compatible
3. **Apply RF Material:**
   - Open imported mesh
   - Slot 0: assign `M_MiniatureBase`
   - Slot 1 (if present): `M_MiniatureBase_Transparent`
4. **Set Material parameters:**
   - `TokenColor`: Set per-character color (overridden at runtime)
   - `HasEmissive`: 1.0 if glowing eyes/magic

### Register in Mod or Default Asset Pack

Via Lua mod:
```lua
RF.RegisterAsset({
  id = "mini_my_character",
  displayName = "My Custom Character",
  category = "creature",
  meshPath = "Content/Miniatures/Custom/SK_MyCharacter.uasset",
  iconPath = "Content/Miniatures/Custom/T_MyCharacter_Icon.png",
})
```

Via Blueprint (in-editor):
1. Open `BP_AssetRegistry`
2. Find `DefaultMiniatures` array
3. Add entry with mesh reference + display name

---

## Prop / Tile Import

### Mesh Requirements

| Property | Value |
|----------|-------|
| Tile-snap size | 100×100 UU = 5ft tile |
| Origin | Bottom center of bounding box |
| Max tris | Wall: 500 | Furniture: 2000 | Building: 8000 |
| Collision | Simple box or custom UCX_ mesh |

### Import Steps

1. Drag into `Content/Props/Custom/[Category]/`
2. Import settings:
   - ☑ Generate Lightmap UVs
   - ☑ Build Nanite (for high-poly hero props)
   - Set LOD group: `SmallProp` / `LargeProp`
3. Assign material from `Content/Materials/Props/`:
   - `M_Stone` — stone/masonry
   - `M_Wood` — wooden surfaces
   - `M_Metal` — iron, steel
   - `M_Fabric` — cloth, rugs
   - Or create custom: duplicate `M_PropBase` and set parameters

### Collision Setup

For walkable props or doors:
```
In Static Mesh Editor:
  Collision → Add Box Simplified Collision
  Or: Collision → Auto Convex Collision (for complex shapes)
  
  For doors: Add custom UCX_DoorFrame and UCX_Door meshes in your FBX
```

---

## Audio Import

### SFX Requirements

| Property | Value |
|----------|-------|
| Format | WAV, 44.1kHz, 16-bit |
| Length | SFX: ≤5s | Ambient loops: ≤60s |
| Channels | Mono for positional, Stereo for music/UI |
| Normalization | -3 dBFS peak |

### Import Steps

1. Drag WAV/OGG into `Content/Audio/Custom/`
2. Set in Sound Cue editor:
   - For looping ambient: add **Looping** node
   - For random variations: add **Random** node with multiple waves
   - For positional 3D: enable **Spatialization** + set attenuation asset

### Register Ambient Zone

```cpp
// In your map or via Blueprint
URFAmbientZone* Zone = NewObject<URFAmbientZone>(this);
Zone->AmbientTrack = LoadObject<USoundCue>(nullptr, TEXT("/Game/Audio/Custom/MyAmbience"));
Zone->TriggerRadius = 1000.0f;
Zone->Volume = 0.7f;
```

---

## Tileset / Theme Pack Import

A theme pack is a folder of related tiles that share a visual style.

### Folder Structure
```
Content/Tilesets/MyThemePack/
├── Floors/
│   ├── SM_Floor_Stone_01.uasset
│   ├── SM_Floor_Stone_02.uasset
│   └── SM_Floor_Mossy.uasset
├── Walls/
│   ├── SM_Wall_Full.uasset
│   ├── SM_Wall_Half.uasset
│   └── SM_Wall_Corner.uasset
├── Doors/
│   ├── SM_Door_Iron.uasset
│   └── SM_Door_Wood.uasset
├── Details/
│   └── SM_Torch_Wall.uasset
└── TilePackManifest.json
```

### TilePackManifest.json
```json
{
  "packID":      "my_theme_pack",
  "displayName": "My Theme Pack",
  "theme":       "Crypt",
  "author":      "Your Name",
  "tiles": [
    { "id": "floor_stone_01",  "type": "Floor",  "mesh": "Floors/SM_Floor_Stone_01" },
    { "id": "wall_full",       "type": "Wall",   "mesh": "Walls/SM_Wall_Full" },
    { "id": "door_iron",       "type": "Door",   "mesh": "Doors/SM_Door_Iron", "isInteractable": true }
  ]
}
```

---

## VFX / Particle Import

1. Import source textures into `Content/VFX/Custom/Textures/`
2. Create Niagara System: right-click → **Niagara System** → **New System from Template**
3. Reference your textures in the Renderer module
4. Register via Lua:
```lua
RF.RegisterAsset({
  id = "vfx_my_spell",
  displayName = "My Spell Effect",
  category = "spell",
  vfxPath = "Content/VFX/Custom/NS_MySpell.uasset",
  iconPath = "Content/VFX/Custom/Icons/T_MySpell_Icon.png",
})
```

---

## Performance Guidelines

| Asset Type | LOD0 | LOD1 | LOD2 |
|-----------|------|------|------|
| Miniature | 15k tris | 5k | 1k |
| Wall tile | 500 | 100 | 50 |
| Furniture | 2k | 500 | 150 |
| Tree | 5k | 1.5k | 300 |
| Building | 8k | 3k | 800 |

- Use **Nanite** for hero props viewed up close
- Use **Instanced Static Meshes** for repeated tiles (RFTileSystem does this automatically)
- Keep texture atlases to 2048×2048 max
- Pack ORM channels: R=Occlusion, G=Roughness, B=Metallic

---

*RealmForge Engine — Asset Import Guide v1.0*
