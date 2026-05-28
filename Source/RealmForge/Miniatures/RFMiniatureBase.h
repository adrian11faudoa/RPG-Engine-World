#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DecalComponent.h"
#include "Components/WidgetComponent.h"
#include "RFMiniatureBase.generated.h"

UENUM(BlueprintType)
enum class ERFMiniatureType : uint8
{
    PlayerCharacter UMETA(DisplayName = "Player Character"),
    NPC             UMETA(DisplayName = "NPC"),
    Monster         UMETA(DisplayName = "Monster"),
    Vehicle         UMETA(DisplayName = "Vehicle"),
    Object          UMETA(DisplayName = "Interactive Object")
};

UENUM(BlueprintType)
enum class ERFCreatureSize : uint8
{
    Tiny       UMETA(DisplayName = "Tiny"),      // 2.5ft
    Small      UMETA(DisplayName = "Small"),     // 5ft
    Medium     UMETA(DisplayName = "Medium"),    // 5ft
    Large      UMETA(DisplayName = "Large"),     // 10ft
    Huge       UMETA(DisplayName = "Huge"),      // 15ft
    Gargantuan UMETA(DisplayName = "Gargantuan") // 20ft+
};

USTRUCT(BlueprintType)
struct FRFStatusEffect
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString Name;
    UPROPERTY(BlueprintReadWrite) FString IconPath;
    UPROPERTY(BlueprintReadWrite) FLinearColor Color = FLinearColor::White;
    UPROPERTY(BlueprintReadWrite) int32 RemainingRounds = -1;  // -1 = permanent
    UPROPERTY(BlueprintReadWrite) bool bShowOnToken = true;
};

USTRUCT(BlueprintType)
struct FRFMiniatureStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int32 HP = 30;
    UPROPERTY(BlueprintReadWrite) int32 MaxHP = 30;
    UPROPERTY(BlueprintReadWrite) int32 TempHP = 0;
    UPROPERTY(BlueprintReadWrite) int32 AC = 10;
    UPROPERTY(BlueprintReadWrite) int32 Speed = 30;        // feet
    UPROPERTY(BlueprintReadWrite) int32 RemainingMovement = 30;
    UPROPERTY(BlueprintReadWrite) bool bHasAction = true;
    UPROPERTY(BlueprintReadWrite) bool bHasBonusAction = true;
    UPROPERTY(BlueprintReadWrite) bool bHasReaction = true;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHPChanged, int32, NewHP, int32, MaxHP);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMoved, FVector, OldLocation, FVector, NewLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnStatusChanged, const TArray<FRFStatusEffect>&, Effects);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMiniatureDefeated);

UCLASS(Abstract, BlueprintType, Blueprintable)
class REALMFORGE_API ARFMiniatureBase : public AActor
{
    GENERATED_BODY()

public:
    ARFMiniatureBase();

    // ─── Components ──────────────────────────────────────────────────
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RF|Miniature")
    UStaticMeshComponent* MiniMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RF|Miniature")
    UDecalComponent* SelectionRing;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RF|Miniature")
    UDecalComponent* MovementRing;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RF|Miniature")
    UWidgetComponent* StatusWidget;  // Floating HP/name overhead

    // ─── Identity ────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "RF|Miniature")
    FString CharacterName = TEXT("Unknown");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "RF|Miniature")
    ERFMiniatureType MiniType = ERFMiniatureType::NPC;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "RF|Miniature")
    ERFCreatureSize CreatureSize = ERFCreatureSize::Medium;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|Miniature")
    FLinearColor TokenColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|Miniature")
    UTexture2D* CharacterPortrait;

    // ─── Stats ───────────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "RF|Miniature")
    FRFMiniatureStats Stats;

    UPROPERTY(Replicated, BlueprintReadOnly, Category = "RF|Miniature")
    TArray<FRFStatusEffect> ActiveEffects;

    // ─── Visibility / Permissions ────────────────────────────────────
    UPROPERTY(Replicated, BlueprintReadWrite, Category = "RF|Miniature")
    bool bIsVisibleToPlayers = true;

    UPROPERTY(Replicated, BlueprintReadWrite, Category = "RF|Miniature")
    bool bIsHiddenInFog = false;

    UPROPERTY(Replicated, BlueprintReadWrite, Category = "RF|Miniature")
    FString OwningPlayerName;   // Which player controls this token

    // ─── HP System ───────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void ApplyDamage(int32 Amount, const FString& DamageType = TEXT("Untyped"));

    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void Heal(int32 Amount);

    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void AddTempHP(int32 Amount);

    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void SetHP(int32 NewHP);

    UFUNCTION(BlueprintPure, Category = "RF|Miniature")
    float GetHPPercent() const;

    UFUNCTION(BlueprintPure, Category = "RF|Miniature")
    bool IsAlive() const { return Stats.HP > 0; }

    // ─── Movement ────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    bool MoveTo(FVector TargetLocation, bool bSnap = true);

    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void ShowMovementRange(float Radius);

    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void HideMovementRange();

    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void ResetTurnResources();

    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    float GetDistanceTo(FVector TargetLocation) const;

    // ─── Status Effects ──────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void AddStatusEffect(const FRFStatusEffect& Effect);

    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void RemoveStatusEffect(const FString& EffectName);

    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    bool HasStatusEffect(const FString& EffectName) const;

    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void TickStatusEffects();  // Call each round

    // ─── Selection ───────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Miniature")
    void SetSelected(bool bSelected);

    UPROPERTY(BlueprintReadOnly, Category = "RF|Miniature")
    bool bIsSelected = false;

    // ─── Delegates ───────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnHPChanged OnHPChanged;
    UPROPERTY(BlueprintAssignable) FOnMoved OnMoved;
    UPROPERTY(BlueprintAssignable) FOnStatusChanged OnStatusChanged;
    UPROPERTY(BlueprintAssignable) FOnMiniatureDefeated OnMiniatureDefeated;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION()
    virtual void OnRep_Stats();

    UFUNCTION()
    virtual void OnRep_ActiveEffects();

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_PlayDeathFX();

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_PlayHitFX(FVector ImpactPoint, const FString& DamageType);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_MoveTo(FVector TargetLocation);

private:
    float GetSizeInFeet() const;
    FVector SnapToGrid(FVector Location, float GridSize = 100.0f) const;
    void UpdateSelectionRingVisuals();
};
