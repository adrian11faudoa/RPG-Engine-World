#include "RFFogOfWar.h"
#include "Net/UnrealNetwork.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

URFFogOfWar::URFFogOfWar()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetIsReplicatedByDefault(true);
}

void URFFogOfWar::BeginPlay()
{
    Super::BeginPlay();
    InitializeFog();
}

void URFFogOfWar::InitializeFog()
{
    const int32 TotalCells = GridWidth * GridHeight;
    FogGrid.SetNum(TotalCells);
    TargetAlpha.SetNum(TotalCells, 0.0f);

    for (FRFFogCell& Cell : FogGrid)
    {
        Cell.State = ERFFogState::Hidden;
        Cell.ExploreAlpha = 0.0f;
    }

    // Create fog overlay texture
    FogTexture = UTexture2D::CreateTransient(GridWidth, GridHeight, PF_B8G8R8A8);
    FogTexture->AddToRoot();
    FogTexture->Filter = TF_Bilinear;

    FogPixels.SetNum(TotalCells);
    for (FColor& Px : FogPixels) Px = FColor(0, 0, 0, 255);

    RebuildTexture();

    UE_LOG(LogTemp, Log, TEXT("[RF|Fog] Initialized %dx%d fog grid"), GridWidth, GridHeight);
}

void URFFogOfWar::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bSmoothReveal) return;

    bool bChanged = false;
    for (int32 i = 0; i < FogGrid.Num(); i++)
    {
        float Target = TargetAlpha[i];
        float& Current = FogGrid[i].ExploreAlpha;
        if (!FMath::IsNearlyEqual(Current, Target, 0.01f))
        {
            Current = FMath::FInterpTo(Current, Target, DeltaTime, RevealSpeed);
            bChanged = true;
        }
    }

    if (bChanged)
    {
        bTextureDirty = true;
        RebuildTexture();
    }
}

void URFFogOfWar::RevealRadius(FVector2D CenterCell, float RadiusInCells)
{
    TArray<FVector2D> Cells = GetCellsInRadius(CenterCell, RadiusInCells);
    TArray<FVector2D> NewlyRevealed;

    for (const FVector2D& Cell : Cells)
    {
        int32 X = FMath::RoundToInt(Cell.X);
        int32 Y = FMath::RoundToInt(Cell.Y);
        if (!IsValidCell(X, Y)) continue;

        // LOS check from center
        if (!HasLineOfSight(CenterCell, Cell)) continue;

        int32 Idx = CellIndex(X, Y);
        FogGrid[Idx].State = ERFFogState::Visible;
        TargetAlpha[Idx] = 1.0f;
        if (!bSmoothReveal) FogGrid[Idx].ExploreAlpha = 1.0f;
        NewlyRevealed.Add(Cell);
    }

    if (GetOwner()->HasAuthority() && NewlyRevealed.Num() > 0)
    {
        Multicast_RevealCells(NewlyRevealed);
    }

    bTextureDirty = true;
    RebuildTexture();
}

void URFFogOfWar::HideRadius(FVector2D CenterCell, float RadiusInCells)
{
    TArray<FVector2D> Cells = GetCellsInRadius(CenterCell, RadiusInCells);

    for (const FVector2D& Cell : Cells)
    {
        int32 X = FMath::RoundToInt(Cell.X);
        int32 Y = FMath::RoundToInt(Cell.Y);
        if (!IsValidCell(X, Y)) continue;

        int32 Idx = CellIndex(X, Y);
        // Cells become "explored" (grey) when leaving LOS, not hidden again
        if (FogGrid[Idx].State == ERFFogState::Visible)
        {
            FogGrid[Idx].State = ERFFogState::Explored;
            TargetAlpha[Idx] = ExploredDarkenAmount;
        }
    }

    bTextureDirty = true;
    RebuildTexture();
}

void URFFogOfWar::PaintReveal(FVector WorldLocation, float BrushRadius, bool bReveal)
{
    FVector2D Center = WorldToCell(WorldLocation);
    float CellRadius = BrushRadius / TileSize;

    if (bReveal) RevealRadius(Center, CellRadius);
    else
    {
        TArray<FVector2D> Cells = GetCellsInRadius(Center, CellRadius);
        for (const FVector2D& Cell : Cells)
        {
            int32 X = FMath::RoundToInt(Cell.X);
            int32 Y = FMath::RoundToInt(Cell.Y);
            if (!IsValidCell(X, Y)) continue;
            int32 Idx = CellIndex(X, Y);
            FogGrid[Idx].State = ERFFogState::Hidden;
            TargetAlpha[Idx] = 0.0f;
            if (!bSmoothReveal) FogGrid[Idx].ExploreAlpha = 0.0f;
        }
        bTextureDirty = true;
        RebuildTexture();
    }
}

void URFFogOfWar::FloodFillReveal(FVector2D StartCell)
{
    // Iterative flood fill using BFS
    TQueue<FVector2D> Queue;
    TSet<FVector2D> Visited;

    Queue.Enqueue(StartCell);
    Visited.Add(StartCell);

    const TArray<FVector2D> Directions = {
        {1,0}, {-1,0}, {0,1}, {0,-1}
    };

    while (!Queue.IsEmpty())
    {
        FVector2D Current;
        Queue.Dequeue(Current);

        int32 X = FMath::RoundToInt(Current.X);
        int32 Y = FMath::RoundToInt(Current.Y);
        if (!IsValidCell(X, Y)) continue;

        int32 Idx = CellIndex(X, Y);
        FogGrid[Idx].State = ERFFogState::Visible;
        TargetAlpha[Idx] = 1.0f;

        for (const FVector2D& Dir : Directions)
        {
            FVector2D Next = Current + Dir;
            if (!Visited.Contains(Next))
            {
                Visited.Add(Next);
                Queue.Enqueue(Next);
            }
        }
    }

    bTextureDirty = true;
    RebuildTexture();
}

void URFFogOfWar::RevealAllForGM()
{
    for (int32 i = 0; i < FogGrid.Num(); i++)
    {
        FogGrid[i].State = ERFFogState::Visible;
        FogGrid[i].ExploreAlpha = 1.0f;
        TargetAlpha[i] = 1.0f;
    }
    bTextureDirty = true;
    RebuildTexture();
}

void URFFogOfWar::HideAll()
{
    for (int32 i = 0; i < FogGrid.Num(); i++)
    {
        FogGrid[i].State = ERFFogState::Hidden;
        FogGrid[i].ExploreAlpha = 0.0f;
        TargetAlpha[i] = 0.0f;
    }
    bTextureDirty = true;
    RebuildTexture();
}

ERFFogState URFFogOfWar::GetCellState(int32 X, int32 Y) const
{
    if (!IsValidCell(X, Y)) return ERFFogState::Hidden;
    return FogGrid[CellIndex(X, Y)].State;
}

bool URFFogOfWar::IsCellVisible(FVector WorldLocation) const
{
    FVector2D Cell = WorldToCell(WorldLocation);
    return GetCellState(FMath::RoundToInt(Cell.X), FMath::RoundToInt(Cell.Y))
        == ERFFogState::Visible;
}

FVector2D URFFogOfWar::WorldToCell(FVector WorldLocation) const
{
    return FVector2D(WorldLocation.X / TileSize, WorldLocation.Y / TileSize);
}

FVector URFFogOfWar::CellToWorld(FVector2D Cell) const
{
    return FVector(Cell.X * TileSize, Cell.Y * TileSize, 0.0f);
}

void URFFogOfWar::RebuildTexture()
{
    if (!FogTexture) return;

    for (int32 i = 0; i < FogGrid.Num(); i++)
    {
        uint8 Alpha = FMath::Clamp(FMath::RoundToInt((1.0f - FogGrid[i].ExploreAlpha) * 255), 0, 255);
        FogPixels[i] = FColor(0, 0, 0, Alpha);
    }

    void* TextureData = FogTexture->GetPlatformData()->Mips[0].BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(TextureData, FogPixels.GetData(), FogPixels.Num() * sizeof(FColor));
    FogTexture->GetPlatformData()->Mips[0].BulkData.Unlock();
    FogTexture->UpdateResource();

    bTextureDirty = false;
}

bool URFFogOfWar::IsValidCell(int32 X, int32 Y) const
{
    return X >= 0 && X < GridWidth && Y >= 0 && Y < GridHeight;
}

int32 URFFogOfWar::CellIndex(int32 X, int32 Y) const
{
    return Y * GridWidth + X;
}

bool URFFogOfWar::HasLineOfSight(FVector2D From, FVector2D To) const
{
    // Bresenham line algorithm for grid-based LOS
    int32 X0 = FMath::RoundToInt(From.X), Y0 = FMath::RoundToInt(From.Y);
    int32 X1 = FMath::RoundToInt(To.X),   Y1 = FMath::RoundToInt(To.Y);

    int32 DX = FMath::Abs(X1 - X0), SX = (X0 < X1) ? 1 : -1;
    int32 DY = FMath::Abs(Y1 - Y0), SY = (Y0 < Y1) ? 1 : -1;
    int32 Err = DX - DY;

    while (true)
    {
        if (X0 == X1 && Y0 == Y1) return true;
        // TODO: check wall tiles here for occlusion
        // if (IsWallAt(X0, Y0) && !(X0==X0Start && Y0==Y0Start)) return false;

        int32 E2 = 2 * Err;
        if (E2 > -DY) { Err -= DY; X0 += SX; }
        if (E2 <  DX) { Err += DX; Y0 += SY; }
    }
}

TArray<FVector2D> URFFogOfWar::GetCellsInRadius(FVector2D Center, float Radius) const
{
    TArray<FVector2D> Result;
    int32 R = FMath::CeilToInt(Radius);
    int32 CX = FMath::RoundToInt(Center.X);
    int32 CY = FMath::RoundToInt(Center.Y);

    for (int32 Y = CY - R; Y <= CY + R; Y++)
    {
        for (int32 X = CX - R; X <= CX + R; X++)
        {
            if (FVector2D::Distance(FVector2D(X, Y), Center) <= Radius)
            {
                Result.Add(FVector2D(X, Y));
            }
        }
    }
    return Result;
}

void URFFogOfWar::Multicast_RevealCells_Implementation(const TArray<FVector2D>& Cells)
{
    for (const FVector2D& Cell : Cells)
    {
        int32 X = FMath::RoundToInt(Cell.X);
        int32 Y = FMath::RoundToInt(Cell.Y);
        if (!IsValidCell(X, Y)) continue;
        int32 Idx = CellIndex(X, Y);
        FogGrid[Idx].State = ERFFogState::Visible;
        TargetAlpha[Idx] = 1.0f;
    }
    bTextureDirty = true;
    RebuildTexture();
}

void URFFogOfWar::Multicast_HideCells_Implementation(const TArray<FVector2D>& Cells)
{
    for (const FVector2D& Cell : Cells)
    {
        int32 X = FMath::RoundToInt(Cell.X);
        int32 Y = FMath::RoundToInt(Cell.Y);
        if (!IsValidCell(X, Y)) continue;
        int32 Idx = CellIndex(X, Y);
        FogGrid[Idx].State = ERFFogState::Explored;
        TargetAlpha[Idx] = ExploredDarkenAmount;
    }
    bTextureDirty = true;
    RebuildTexture();
}

void URFFogOfWar::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    // Grid replicated via multicast RPCs, not raw property replication (too large)
}
