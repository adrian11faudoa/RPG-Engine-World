#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RFTileSystem.h"
#include "RFProceduralDungeon.generated.h"

UENUM(BlueprintType)
enum class ERFDungeonTheme : uint8
{
    Classic     UMETA(DisplayName = "Classic Dungeon"),
    Cavern      UMETA(DisplayName = "Natural Cavern"),
    Ruins       UMETA(DisplayName = "Ancient Ruins"),
    Sewer       UMETA(DisplayName = "City Sewers"),
    Crypt       UMETA(DisplayName = "Undead Crypt"),
    Prison      UMETA(DisplayName = "Dungeon Prison"),
    Temple      UMETA(DisplayName = "Lost Temple"),
    Maze        UMETA(DisplayName = "Labyrinth"),
    Custom      UMETA(DisplayName = "Custom Tileset")
};

UENUM(BlueprintType)
enum class ERFRoomType : uint8
{
    Entrance    UMETA(DisplayName = "Entrance"),
    Corridor    UMETA(DisplayName = "Corridor"),
    Small       UMETA(DisplayName = "Small Room"),
    Medium      UMETA(DisplayName = "Medium Room"),
    Large       UMETA(DisplayName = "Large Room"),
    Boss        UMETA(DisplayName = "Boss Chamber"),
    Treasure    UMETA(DisplayName = "Treasure Room"),
    Trap        UMETA(DisplayName = "Trap Room"),
    Exit        UMETA(DisplayName = "Exit")
};

USTRUCT(BlueprintType)
struct FRFRoom
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) int32 RoomID = 0;
    UPROPERTY(BlueprintReadOnly) ERFRoomType Type = ERFRoomType::Small;
    UPROPERTY(BlueprintReadOnly) FIntPoint Position  = FIntPoint(0, 0);  // Grid position
    UPROPERTY(BlueprintReadOnly) FIntPoint Size       = FIntPoint(5, 5);  // In tiles
    UPROPERTY(BlueprintReadOnly) TArray<int32> ConnectedRoomIDs;
    UPROPERTY(BlueprintReadOnly) TArray<FIntPoint> DoorPositions;
    UPROPERTY(BlueprintReadOnly) bool bHasTrap        = false;
    UPROPERTY(BlueprintReadOnly) bool bHasTreasure    = false;
    UPROPERTY(BlueprintReadOnly) FString Description;
};

USTRUCT(BlueprintType)
struct FRFDungeonConfig
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) ERFDungeonTheme Theme     = ERFDungeonTheme::Classic;
    UPROPERTY(BlueprintReadWrite) int32 RoomCount           = 10;
    UPROPERTY(BlueprintReadWrite) int32 Seed                = 0;       // 0 = random
    UPROPERTY(BlueprintReadWrite) int32 MaxWidth            = 80;      // in tiles
    UPROPERTY(BlueprintReadWrite) int32 MaxHeight           = 80;
    UPROPERTY(BlueprintReadWrite) float TrapDensity         = 0.15f;   // 0-1
    UPROPERTY(BlueprintReadWrite) float TreasureDensity     = 0.2f;
    UPROPERTY(BlueprintReadWrite) bool bGuaranteeExit       = true;
    UPROPERTY(BlueprintReadWrite) bool bGuaranteeBossRoom   = true;
    UPROPERTY(BlueprintReadWrite) int32 CorridorWidth       = 1;       // tiles
    UPROPERTY(BlueprintReadWrite) bool bSpawnMonsters       = false;   // GM controls manually
};

USTRUCT(BlueprintType)
struct FRFGeneratedDungeon
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FRFDungeonConfig Config;
    UPROPERTY(BlueprintReadOnly) TArray<FRFRoom> Rooms;
    UPROPERTY(BlueprintReadOnly) TArray<TArray<int32>> TileGrid;  // Tile type per cell
    UPROPERTY(BlueprintReadOnly) int32 EntranceRoomID = 0;
    UPROPERTY(BlueprintReadOnly) int32 ExitRoomID     = 0;
    UPROPERTY(BlueprintReadOnly) int32 BossRoomID     = -1;
    UPROPERTY(BlueprintReadOnly) float GenerationTimeMs = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDungeonGenerated, const FRFGeneratedDungeon&, Dungeon);

UCLASS()
class REALMFORGE_API ARFProceduralDungeon : public AActor
{
    GENERATED_BODY()

public:
    ARFProceduralDungeon();

    // ─── Generation ───────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Dungeon")
    FRFGeneratedDungeon GenerateDungeon(const FRFDungeonConfig& Config);

    UFUNCTION(BlueprintCallable, Category = "RF|Dungeon")
    void SpawnDungeonInWorld(const FRFGeneratedDungeon& Dungeon, FVector WorldOrigin);

    UFUNCTION(BlueprintCallable, Category = "RF|Dungeon")
    void ClearGeneratedDungeon();

    UFUNCTION(BlueprintCallable, Category = "RF|Dungeon")
    FRFGeneratedDungeon Regenerate();  // Re-run with last config

    // ─── Tile Mapping ─────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|Dungeon|Tiles")
    TMap<ERFDungeonTheme, FString> ThemeTilePacks;  // Theme -> TilePack ID

    // ─── Events ───────────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable)
    FOnDungeonGenerated OnDungeonGenerated;

    // ─── Last Result ──────────────────────────────────────────────────
    UFUNCTION(BlueprintPure, Category = "RF|Dungeon")
    FRFGeneratedDungeon GetLastGenerated() const { return LastGenerated; }

private:
    FRFGeneratedDungeon LastGenerated;
    FRFDungeonConfig    LastConfig;

    TArray<FRFRoom>      PendingRooms;
    TArray<TArray<int32>> Grid;

    // Generation steps
    void InitGrid(int32 W, int32 H);
    bool PlaceRoom(FRFRoom& Room);
    bool RoomsOverlap(const FRFRoom& A, const FRFRoom& B, int32 Padding = 1) const;
    void ConnectRooms(FRFRoom& A, FRFRoom& B);
    void CarveHorizontalCorridor(int32 X1, int32 X2, int32 Y, int32 Width);
    void CarveVerticalCorridor(int32 X, int32 Y1, int32 Y2, int32 Width);
    FIntPoint GetRoomCenter(const FRFRoom& Room) const;
    void AssignRoomTypes(TArray<FRFRoom>& Rooms, const FRFDungeonConfig& Config);
    void ScatterTrapsAndTreasure(TArray<FRFRoom>& Rooms, const FRFDungeonConfig& Config);
    FString GenerateRoomDescription(const FRFRoom& Room, ERFDungeonTheme Theme) const;

    // Tile IDs
    static const int32 TILE_EMPTY  = 0;
    static const int32 TILE_FLOOR  = 1;
    static const int32 TILE_WALL   = 2;
    static const int32 TILE_DOOR   = 3;
    static const int32 TILE_STAIRS = 4;
    static const int32 TILE_TRAP   = 5;
    static const int32 TILE_CHEST  = 6;
};
