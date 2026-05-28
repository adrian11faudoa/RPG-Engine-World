#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/Texture2D.h"
#include "RFFogOfWar.generated.h"

UENUM(BlueprintType)
enum class ERFFogState : uint8
{
    Hidden      UMETA(DisplayName = "Hidden"),       // Never seen — full black
    Explored    UMETA(DisplayName = "Explored"),     // Seen but not currently visible — dim
    Visible     UMETA(DisplayName = "Visible")       // Currently in sight — fully lit
};

USTRUCT(BlueprintType)
struct FRFFogCell
{
    GENERATED_BODY()

    UPROPERTY()
    ERFFogState State = ERFFogState::Hidden;

    UPROPERTY()
    float ExploreAlpha = 0.0f;  // 0=hidden, 1=fully explored (used for smooth transitions)
};

UCLASS(ClassGroup=(RealmForge), meta=(BlueprintSpawnableComponent))
class REALMFORGE_API URFFogOfWar : public UActorComponent
{
    GENERATED_BODY()

public:
    URFFogOfWar();

    // ─── Configuration ────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|Fog")
    int32 GridWidth = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|Fog")
    int32 GridHeight = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|Fog")
    float TileSize = 100.0f;  // UU per grid cell

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|Fog")
    float ExploredDarkenAmount = 0.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|Fog")
    bool bGMSeesAll = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|Fog")
    bool bSmoothReveal = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|Fog")
    float RevealSpeed = 4.0f;

    // ─── Core API ─────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Fog")
    void InitializeFog();

    UFUNCTION(BlueprintCallable, Category = "RF|Fog")
    void RevealRadius(FVector2D CenterCell, float RadiusInCells);

    UFUNCTION(BlueprintCallable, Category = "RF|Fog")
    void HideRadius(FVector2D CenterCell, float RadiusInCells);

    UFUNCTION(BlueprintCallable, Category = "RF|Fog")
    void RevealAllForGM();

    UFUNCTION(BlueprintCallable, Category = "RF|Fog")
    void HideAll();

    // Paint-brush API for GM manual control
    UFUNCTION(BlueprintCallable, Category = "RF|Fog")
    void PaintReveal(FVector WorldLocation, float BrushRadius, bool bReveal);

    UFUNCTION(BlueprintCallable, Category = "RF|Fog")
    void FloodFillReveal(FVector2D StartCell);

    UFUNCTION(BlueprintPure, Category = "RF|Fog")
    ERFFogState GetCellState(int32 X, int32 Y) const;

    UFUNCTION(BlueprintPure, Category = "RF|Fog")
    bool IsCellVisible(FVector WorldLocation) const;

    UFUNCTION(BlueprintCallable, Category = "RF|Fog")
    FVector2D WorldToCell(FVector WorldLocation) const;

    UFUNCTION(BlueprintCallable, Category = "RF|Fog")
    FVector CellToWorld(FVector2D Cell) const;

    // ─── Texture Output (for decal/material overlay) ──────────────────
    UFUNCTION(BlueprintPure, Category = "RF|Fog")
    UTexture2D* GetFogTexture() const { return FogTexture; }

    // Replication
    UFUNCTION(NetMulticast, Reliable)
    void Multicast_RevealCells(const TArray<FVector2D>& Cells);

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_HideCells(const TArray<FVector2D>& Cells);

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
    // Grid storage
    TArray<FRFFogCell> FogGrid;

    // Texture updated each frame for material use
    UPROPERTY()
    UTexture2D* FogTexture = nullptr;

    TArray<FColor> FogPixels;

    bool bTextureDirty = false;

    void RebuildTexture();
    bool IsValidCell(int32 X, int32 Y) const;
    int32 CellIndex(int32 X, int32 Y) const;

    // Line-of-sight raycasting
    bool HasLineOfSight(FVector2D From, FVector2D To) const;
    TArray<FVector2D> GetCellsInRadius(FVector2D Center, float Radius) const;

    // Smooth transition targets
    TArray<float> TargetAlpha;
};
