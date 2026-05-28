#include "RFProceduralDungeon.h"

ARFProceduralDungeon::ARFProceduralDungeon()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
}

FRFGeneratedDungeon ARFProceduralDungeon::GenerateDungeon(const FRFDungeonConfig& Config)
{
    float StartTime = FPlatformTime::Seconds() * 1000.0f;
    LastConfig = Config;

    int32 Seed = (Config.Seed == 0) ? FMath::Rand() : Config.Seed;
    FMath::RandInit(Seed);

    FRFGeneratedDungeon Result;
    Result.Config = Config;

    InitGrid(Config.MaxWidth, Config.MaxHeight);

    // ─── 1. Place rooms using BSP-inspired placement ──────────────────
    TArray<FRFRoom> PlacedRooms;
    int32 Attempts = 0;
    int32 MaxAttempts = Config.RoomCount * 20;

    while (PlacedRooms.Num() < Config.RoomCount && Attempts < MaxAttempts)
    {
        Attempts++;

        FRFRoom NewRoom;
        NewRoom.RoomID = PlacedRooms.Num();

        // Randomize room size based on type budget
        float RollType = FMath::FRand();
        if      (RollType < 0.4f)  NewRoom.Size = FIntPoint(FMath::RandRange(3,5), FMath::RandRange(3,5));
        else if (RollType < 0.7f)  NewRoom.Size = FIntPoint(FMath::RandRange(5,8), FMath::RandRange(5,8));
        else                        NewRoom.Size = FIntPoint(FMath::RandRange(8,14),FMath::RandRange(8,14));

        // Random position within grid bounds
        NewRoom.Position.X = FMath::RandRange(2, Config.MaxWidth  - NewRoom.Size.X - 2);
        NewRoom.Position.Y = FMath::RandRange(2, Config.MaxHeight - NewRoom.Size.Y - 2);

        // Check for overlaps
        bool bOverlaps = false;
        for (const FRFRoom& Existing : PlacedRooms)
        {
            if (RoomsOverlap(NewRoom, Existing, 2))
            {
                bOverlaps = true;
                break;
            }
        }

        if (!bOverlaps && PlaceRoom(NewRoom))
        {
            PlacedRooms.Add(NewRoom);
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[RF|Dungeon] Placed %d/%d rooms (%d attempts)"),
        PlacedRooms.Num(), Config.RoomCount, Attempts);

    // ─── 2. Connect rooms with corridors ─────────────────────────────
    for (int32 i = 1; i < PlacedRooms.Num(); i++)
    {
        ConnectRooms(PlacedRooms[i - 1], PlacedRooms[i]);
        PlacedRooms[i - 1].ConnectedRoomIDs.Add(i);
        PlacedRooms[i].ConnectedRoomIDs.Add(i - 1);

        // Occasionally add a second connection (loops)
        if (FMath::FRand() < 0.15f && i >= 2)
        {
            int32 AltIdx = FMath::RandRange(0, i - 2);
            ConnectRooms(PlacedRooms[i], PlacedRooms[AltIdx]);
        }
    }

    // ─── 3. Assign room types ─────────────────────────────────────────
    AssignRoomTypes(PlacedRooms, Config);

    // ─── 4. Scatter features ──────────────────────────────────────────
    ScatterTrapsAndTreasure(PlacedRooms, Config);

    // ─── 5. Find entrance / exit / boss ──────────────────────────────
    for (const FRFRoom& Room : PlacedRooms)
    {
        if (Room.Type == ERFRoomType::Entrance) Result.EntranceRoomID = Room.RoomID;
        if (Room.Type == ERFRoomType::Exit)     Result.ExitRoomID     = Room.RoomID;
        if (Room.Type == ERFRoomType::Boss)     Result.BossRoomID     = Room.RoomID;
    }

    // ─── 6. Generate descriptions ─────────────────────────────────────
    for (FRFRoom& Room : PlacedRooms)
    {
        Room.Description = GenerateRoomDescription(Room, Config.Theme);
    }

    Result.Rooms = PlacedRooms;
    Result.TileGrid = Grid;
    Result.GenerationTimeMs = (FPlatformTime::Seconds() * 1000.0f) - StartTime;

    LastGenerated = Result;
    OnDungeonGenerated.Broadcast(Result);

    UE_LOG(LogTemp, Log, TEXT("[RF|Dungeon] Generated in %.1fms — %d rooms, seed %d"),
        Result.GenerationTimeMs, PlacedRooms.Num(), Seed);

    return Result;
}

void ARFProceduralDungeon::SpawnDungeonInWorld(const FRFGeneratedDungeon& Dungeon,
    FVector WorldOrigin)
{
    const float TileSize = 100.0f;  // UU per tile

    for (int32 Y = 0; Y < Dungeon.TileGrid.Num(); Y++)
    {
        for (int32 X = 0; X < Dungeon.TileGrid[Y].Num(); X++)
        {
            int32 TileType = Dungeon.TileGrid[Y][X];
            if (TileType == TILE_EMPTY) continue;

            FVector TileLocation = WorldOrigin + FVector(X * TileSize, Y * TileSize, 0.0f);
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride =
                ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            // Spawn floor tile
            if (TileType == TILE_FLOOR || TileType == TILE_DOOR ||
                TileType == TILE_TRAP  || TileType == TILE_CHEST)
            {
                // Get theme-appropriate tile from RFTileSystem
                // GetWorld()->SpawnActor<ARFTile>(FloorClass, TileLocation, ...);
            }

            // Spawn walls on TILE_WALL
            if (TileType == TILE_WALL)
            {
                // GetWorld()->SpawnActor<ARFTile>(WallClass, TileLocation, ...);
            }

            // Spawn stair mesh
            if (TileType == TILE_STAIRS)
            {
                // GetWorld()->SpawnActor<ARFTile>(StairsClass, TileLocation, ...);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[RF|Dungeon] Spawned dungeon at %s"), *WorldOrigin.ToString());
}

void ARFProceduralDungeon::ClearGeneratedDungeon()
{
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if ((*It)->Tags.Contains(FName("ProceduralTile")))
            (*It)->Destroy();
    }
}

FRFGeneratedDungeon ARFProceduralDungeon::Regenerate()
{
    LastConfig.Seed = 0;  // New random seed
    return GenerateDungeon(LastConfig);
}

// ─── Private: Grid Operations ────────────────────────────────────────────────

void ARFProceduralDungeon::InitGrid(int32 W, int32 H)
{
    Grid.SetNum(H);
    for (TArray<int32>& Row : Grid)
    {
        Row.SetNum(W);
        for (int32& Cell : Row) Cell = TILE_EMPTY;
    }
}

bool ARFProceduralDungeon::PlaceRoom(FRFRoom& Room)
{
    int32 PX = Room.Position.X, PY = Room.Position.Y;
    int32 SW = Room.Size.X,     SH = Room.Size.Y;

    // Bounds check
    if (PX + SW >= Grid[0].Num() || PY + SH >= Grid.Num()) return false;

    // Carve floor
    for (int32 Y = PY; Y < PY + SH; Y++)
        for (int32 X = PX; X < PX + SW; X++)
            Grid[Y][X] = TILE_FLOOR;

    // Carve walls around perimeter
    for (int32 X = PX - 1; X <= PX + SW; X++)
    {
        if (X >= 0 && X < Grid[0].Num())
        {
            if (PY - 1 >= 0 && Grid[PY - 1][X] == TILE_EMPTY)   Grid[PY - 1][X] = TILE_WALL;
            if (PY + SH < Grid.Num() && Grid[PY + SH][X] == TILE_EMPTY) Grid[PY + SH][X] = TILE_WALL;
        }
    }
    for (int32 Y = PY - 1; Y <= PY + SH; Y++)
    {
        if (Y >= 0 && Y < Grid.Num())
        {
            if (PX - 1 >= 0 && Grid[Y][PX - 1] == TILE_EMPTY)   Grid[Y][PX - 1] = TILE_WALL;
            if (PX + SW < Grid[0].Num() && Grid[Y][PX + SW] == TILE_EMPTY) Grid[Y][PX + SW] = TILE_WALL;
        }
    }

    return true;
}

bool ARFProceduralDungeon::RoomsOverlap(const FRFRoom& A, const FRFRoom& B, int32 Pad) const
{
    return !(A.Position.X + A.Size.X + Pad <= B.Position.X ||
             B.Position.X + B.Size.X + Pad <= A.Position.X ||
             A.Position.Y + A.Size.Y + Pad <= B.Position.Y ||
             B.Position.Y + B.Size.Y + Pad <= A.Position.Y);
}

void ARFProceduralDungeon::ConnectRooms(FRFRoom& A, FRFRoom& B)
{
    FIntPoint CenterA = GetRoomCenter(A);
    FIntPoint CenterB = GetRoomCenter(B);

    // L-shaped corridor: horizontal then vertical (or vice versa)
    if (FMath::FRand() < 0.5f)
    {
        CarveHorizontalCorridor(CenterA.X, CenterB.X, CenterA.Y, 1);
        CarveVerticalCorridor(CenterB.X, CenterA.Y, CenterB.Y, 1);
    }
    else
    {
        CarveVerticalCorridor(CenterA.X, CenterA.Y, CenterB.Y, 1);
        CarveHorizontalCorridor(CenterA.X, CenterB.X, CenterB.Y, 1);
    }
}

void ARFProceduralDungeon::CarveHorizontalCorridor(int32 X1, int32 X2, int32 Y, int32 Width)
{
    int32 StartX = FMath::Min(X1, X2), EndX = FMath::Max(X1, X2);
    for (int32 X = StartX; X <= EndX; X++)
    {
        for (int32 W = 0; W < Width; W++)
        {
            int32 TY = Y + W;
            if (TY >= 0 && TY < Grid.Num() && X >= 0 && X < Grid[0].Num())
            {
                Grid[TY][X] = TILE_FLOOR;
            }
        }
    }
}

void ARFProceduralDungeon::CarveVerticalCorridor(int32 X, int32 Y1, int32 Y2, int32 Width)
{
    int32 StartY = FMath::Min(Y1, Y2), EndY = FMath::Max(Y1, Y2);
    for (int32 Y = StartY; Y <= EndY; Y++)
    {
        for (int32 W = 0; W < Width; W++)
        {
            int32 TX = X + W;
            if (Y >= 0 && Y < Grid.Num() && TX >= 0 && TX < Grid[0].Num())
            {
                Grid[Y][TX] = TILE_FLOOR;
            }
        }
    }
}

FIntPoint ARFProceduralDungeon::GetRoomCenter(const FRFRoom& Room) const
{
    return FIntPoint(
        Room.Position.X + Room.Size.X / 2,
        Room.Position.Y + Room.Size.Y / 2);
}

void ARFProceduralDungeon::AssignRoomTypes(TArray<FRFRoom>& Rooms,
    const FRFDungeonConfig& Config)
{
    if (Rooms.Num() == 0) return;

    // First room = entrance
    Rooms[0].Type = ERFRoomType::Entrance;

    // Last room = exit
    Rooms.Last().Type = ERFRoomType::Exit;

    // Largest remaining = boss (if enabled)
    if (Config.bGuaranteeBossRoom && Rooms.Num() > 2)
    {
        int32 BossIdx = 1;
        int32 MaxArea = 0;
        for (int32 i = 1; i < Rooms.Num() - 1; i++)
        {
            int32 Area = Rooms[i].Size.X * Rooms[i].Size.Y;
            if (Area > MaxArea) { MaxArea = Area; BossIdx = i; }
        }
        Rooms[BossIdx].Type = ERFRoomType::Boss;
    }

    // Remaining: assign by size
    for (FRFRoom& Room : Rooms)
    {
        if (Room.Type != ERFRoomType::Entrance &&
            Room.Type != ERFRoomType::Exit     &&
            Room.Type != ERFRoomType::Boss)
        {
            int32 Area = Room.Size.X * Room.Size.Y;
            if      (Area <= 12)  Room.Type = ERFRoomType::Small;
            else if (Area <= 35)  Room.Type = ERFRoomType::Medium;
            else                  Room.Type = ERFRoomType::Large;
        }
    }
}

void ARFProceduralDungeon::ScatterTrapsAndTreasure(TArray<FRFRoom>& Rooms,
    const FRFDungeonConfig& Config)
{
    for (FRFRoom& Room : Rooms)
    {
        if (Room.Type == ERFRoomType::Entrance || Room.Type == ERFRoomType::Exit) continue;

        if (FMath::FRand() < Config.TrapDensity)
        {
            Room.bHasTrap = true;
            // Mark a random tile in the room as a trap
            int32 TX = Room.Position.X + FMath::RandRange(1, Room.Size.X - 2);
            int32 TY = Room.Position.Y + FMath::RandRange(1, Room.Size.Y - 2);
            if (TY < Grid.Num() && TX < Grid[0].Num())
                Grid[TY][TX] = TILE_TRAP;
        }

        if (FMath::FRand() < Config.TreasureDensity || Room.Type == ERFRoomType::Boss)
        {
            Room.bHasTreasure = true;
            int32 TX = Room.Position.X + Room.Size.X / 2;
            int32 TY = Room.Position.Y + Room.Size.Y / 2;
            if (TY < Grid.Num() && TX < Grid[0].Num())
                Grid[TY][TX] = TILE_CHEST;
        }
    }
}

FString ARFProceduralDungeon::GenerateRoomDescription(const FRFRoom& Room,
    ERFDungeonTheme Theme) const
{
    TArray<FString> SmallDescs = {
        TEXT("A cramped stone chamber, its walls slick with moisture."),
        TEXT("A small alcove with old torch sconces on the walls."),
        TEXT("A narrow room that smells of mildew and old stone.")
    };
    TArray<FString> MedDescs = {
        TEXT("A vaulted chamber with crumbling columns along the walls."),
        TEXT("A rectangular room with faded murals of forgotten battles."),
        TEXT("A dusty hall littered with broken furniture and old bones.")
    };
    TArray<FString> LargeDescs = {
        TEXT("A vast cavern, its ceiling lost in darkness above."),
        TEXT("A grand hall with shattered stained glass windows."),
        TEXT("An enormous chamber that was once a great meeting hall.")
    };

    FString Base;
    int32 Area = Room.Size.X * Room.Size.Y;
    TArray<FString>& Pool = (Area <= 12) ? SmallDescs : (Area <= 35) ? MedDescs : LargeDescs;
    Base = Pool[FMath::RandRange(0, Pool.Num() - 1)];

    if (Room.bHasTrap)    Base += TEXT(" The floor is suspiciously clean in one spot.");
    if (Room.bHasTreasure) Base += TEXT(" A locked chest sits against the far wall.");
    if (Room.Type == ERFRoomType::Boss)
        Base += TEXT(" Something powerful makes its lair here.");

    return Base;
}
