#include "RFMiniatureBase.h"
#include "Net/UnrealNetwork.h"
#include "Components/DecalComponent.h"
#include "Components/WidgetComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"

ARFMiniatureBase::ARFMiniatureBase()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    bAlwaysRelevant = true;

    MiniMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MiniMesh"));
    RootComponent = MiniMesh;
    MiniMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

    SelectionRing = CreateDefaultSubobject<UDecalComponent>(TEXT("SelectionRing"));
    SelectionRing->SetupAttachment(RootComponent);
    SelectionRing->SetRelativeRotation(FRotator(-90.0f, 0, 0));
    SelectionRing->SetVisibility(false);

    MovementRing = CreateDefaultSubobject<UDecalComponent>(TEXT("MovementRing"));
    MovementRing->SetupAttachment(RootComponent);
    MovementRing->SetRelativeRotation(FRotator(-90.0f, 0, 0));
    MovementRing->SetVisibility(false);

    StatusWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("StatusWidget"));
    StatusWidget->SetupAttachment(RootComponent);
    StatusWidget->SetRelativeLocation(FVector(0, 0, 150.0f));
    StatusWidget->SetWidgetSpace(EWidgetSpace::Screen);
}

void ARFMiniatureBase::BeginPlay()
{
    Super::BeginPlay();

    // Scale mesh to match creature size
    float Scale = GetSizeInFeet() / 5.0f;
    MiniMesh->SetRelativeScale3D(FVector(Scale));

    // Apply token color
    if (MiniMesh->GetMaterial(0))
    {
        UMaterialInstanceDynamic* DynMat = MiniMesh->CreateAndSetMaterialInstanceDynamic(0);
        DynMat->SetVectorParameterValue(TEXT("TokenColor"), TokenColor);
    }

    // Selection ring color matches token
    if (SelectionRing->GetMaterial(0))
    {
        UMaterialInstanceDynamic* RingMat = SelectionRing->CreateAndSetMaterialInstanceDynamic(0);
        RingMat->SetVectorParameterValue(TEXT("RingColor"), TokenColor);
    }
}

void ARFMiniatureBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    // Smooth interpolated movement handled by Replication/GAS
}

// ─── HP System ──────────────────────────────────────────────────────────────

void ARFMiniatureBase::ApplyDamage(int32 Amount, const FString& DamageType)
{
    if (!HasAuthority()) return;

    int32 RemainingDamage = Amount;

    // Temp HP absorbs first
    if (Stats.TempHP > 0)
    {
        int32 TempAbsorbed = FMath::Min(Stats.TempHP, RemainingDamage);
        Stats.TempHP -= TempAbsorbed;
        RemainingDamage -= TempAbsorbed;
    }

    Stats.HP = FMath::Max(0, Stats.HP - RemainingDamage);

    FVector ImpactPoint = GetActorLocation() + FVector(0, 0, 80.0f);
    Multicast_PlayHitFX(ImpactPoint, DamageType);

    OnHPChanged.Broadcast(Stats.HP, Stats.MaxHP);

    if (Stats.HP <= 0)
    {
        Multicast_PlayDeathFX();
        OnMiniatureDefeated.Broadcast();
    }
}

void ARFMiniatureBase::Heal(int32 Amount)
{
    if (!HasAuthority()) return;
    Stats.HP = FMath::Clamp(Stats.HP + Amount, 0, Stats.MaxHP);
    OnHPChanged.Broadcast(Stats.HP, Stats.MaxHP);
}

void ARFMiniatureBase::AddTempHP(int32 Amount)
{
    if (!HasAuthority()) return;
    // Temp HP doesn't stack — take the higher value
    Stats.TempHP = FMath::Max(Stats.TempHP, Amount);
}

void ARFMiniatureBase::SetHP(int32 NewHP)
{
    if (!HasAuthority()) return;
    Stats.HP = FMath::Clamp(NewHP, 0, Stats.MaxHP);
    OnHPChanged.Broadcast(Stats.HP, Stats.MaxHP);
}

float ARFMiniatureBase::GetHPPercent() const
{
    if (Stats.MaxHP <= 0) return 0.0f;
    return (float)Stats.HP / (float)Stats.MaxHP;
}

// ─── Movement ───────────────────────────────────────────────────────────────

bool ARFMiniatureBase::MoveTo(FVector TargetLocation, bool bSnap)
{
    if (!HasAuthority())
    {
        Server_MoveTo(TargetLocation);
        return true;
    }

    FVector OldLocation = GetActorLocation();
    FVector FinalLocation = bSnap ? SnapToGrid(TargetLocation) : TargetLocation;

    float Distance = GetDistanceTo(FinalLocation);
    float DistanceFeet = Distance / 100.0f * 5.0f;  // Convert UU to feet (100UU = 5ft)

    if (DistanceFeet > Stats.RemainingMovement)
    {
        UE_LOG(LogTemp, Warning, TEXT("[RF|Mini] %s: Not enough movement (%.0f ft needed, %d remaining)"),
            *CharacterName, DistanceFeet, Stats.RemainingMovement);
        return false;
    }

    Stats.RemainingMovement -= FMath::RoundToInt(DistanceFeet);
    SetActorLocation(FinalLocation);
    OnMoved.Broadcast(OldLocation, FinalLocation);
    return true;
}

void ARFMiniatureBase::ShowMovementRange(float Radius)
{
    // Scale the movement decal to show reach
    MovementRing->SetVisibility(true);
    float DecalSize = (Radius / 5.0f) * 100.0f;  // feet to UU
    MovementRing->DecalSize = FVector(16.0f, DecalSize, DecalSize);
}

void ARFMiniatureBase::HideMovementRange()
{
    MovementRing->SetVisibility(false);
}

void ARFMiniatureBase::ResetTurnResources()
{
    Stats.RemainingMovement = Stats.Speed;
    Stats.bHasAction = true;
    Stats.bHasBonusAction = true;
    Stats.bHasReaction = true;
}

float ARFMiniatureBase::GetDistanceTo(FVector TargetLocation) const
{
    return FVector::Dist(GetActorLocation(), TargetLocation);
}

// ─── Status Effects ─────────────────────────────────────────────────────────

void ARFMiniatureBase::AddStatusEffect(const FRFStatusEffect& Effect)
{
    if (!HasAuthority()) return;

    // Replace if same name
    for (FRFStatusEffect& Existing : ActiveEffects)
    {
        if (Existing.Name == Effect.Name)
        {
            Existing = Effect;
            OnStatusChanged.Broadcast(ActiveEffects);
            return;
        }
    }
    ActiveEffects.Add(Effect);
    OnStatusChanged.Broadcast(ActiveEffects);
}

void ARFMiniatureBase::RemoveStatusEffect(const FString& EffectName)
{
    if (!HasAuthority()) return;
    ActiveEffects.RemoveAll([&](const FRFStatusEffect& E){ return E.Name == EffectName; });
    OnStatusChanged.Broadcast(ActiveEffects);
}

bool ARFMiniatureBase::HasStatusEffect(const FString& EffectName) const
{
    return ActiveEffects.ContainsByPredicate([&](const FRFStatusEffect& E){
        return E.Name == EffectName;
    });
}

void ARFMiniatureBase::TickStatusEffects()
{
    if (!HasAuthority()) return;

    TArray<FString> ToRemove;
    for (FRFStatusEffect& Effect : ActiveEffects)
    {
        if (Effect.RemainingRounds > 0)
        {
            Effect.RemainingRounds--;
            if (Effect.RemainingRounds == 0) ToRemove.Add(Effect.Name);
        }
    }
    for (const FString& Name : ToRemove) RemoveStatusEffect(Name);
}

// ─── Selection ──────────────────────────────────────────────────────────────

void ARFMiniatureBase::SetSelected(bool bSelected)
{
    bIsSelected = bSelected;
    SelectionRing->SetVisibility(bSelected);
    UpdateSelectionRingVisuals();

    if (bSelected)
    {
        ShowMovementRange(Stats.RemainingMovement);
    }
    else
    {
        HideMovementRange();
    }
}

void ARFMiniatureBase::UpdateSelectionRingVisuals()
{
    float SizeFeet = GetSizeInFeet();
    float DecalSize = (SizeFeet / 5.0f) * 60.0f;
    SelectionRing->DecalSize = FVector(16.0f, DecalSize, DecalSize);
}

// ─── Replication ────────────────────────────────────────────────────────────

void ARFMiniatureBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARFMiniatureBase, CharacterName);
    DOREPLIFETIME(ARFMiniatureBase, MiniType);
    DOREPLIFETIME(ARFMiniatureBase, CreatureSize);
    DOREPLIFETIME(ARFMiniatureBase, Stats);
    DOREPLIFETIME(ARFMiniatureBase, ActiveEffects);
    DOREPLIFETIME(ARFMiniatureBase, bIsVisibleToPlayers);
    DOREPLIFETIME(ARFMiniatureBase, bIsHiddenInFog);
    DOREPLIFETIME(ARFMiniatureBase, OwningPlayerName);
}

void ARFMiniatureBase::OnRep_Stats()
{
    OnHPChanged.Broadcast(Stats.HP, Stats.MaxHP);
}

void ARFMiniatureBase::OnRep_ActiveEffects()
{
    OnStatusChanged.Broadcast(ActiveEffects);
}

void ARFMiniatureBase::Multicast_PlayDeathFX_Implementation()
{
    // Trigger death animation/particle system in Blueprint
    UE_LOG(LogTemp, Log, TEXT("[RF|Mini] %s defeated!"), *CharacterName);
}

void ARFMiniatureBase::Multicast_PlayHitFX_Implementation(FVector ImpactPoint, const FString& DamageType)
{
    // Trigger hit flash / particle in Blueprint
}

bool ARFMiniatureBase::Server_MoveTo_Validate(FVector TargetLocation)
{
    // Anti-cheat: validate target is within reasonable range
    float MaxRange = 5000.0f;
    return FVector::Dist(GetActorLocation(), TargetLocation) < MaxRange;
}

void ARFMiniatureBase::Server_MoveTo_Implementation(FVector TargetLocation)
{
    MoveTo(TargetLocation, true);
}

// ─── Private Helpers ────────────────────────────────────────────────────────

float ARFMiniatureBase::GetSizeInFeet() const
{
    switch (CreatureSize)
    {
        case ERFCreatureSize::Tiny:       return 2.5f;
        case ERFCreatureSize::Small:
        case ERFCreatureSize::Medium:     return 5.0f;
        case ERFCreatureSize::Large:      return 10.0f;
        case ERFCreatureSize::Huge:       return 15.0f;
        case ERFCreatureSize::Gargantuan: return 20.0f;
        default: return 5.0f;
    }
}

FVector ARFMiniatureBase::SnapToGrid(FVector Location, float GridSize) const
{
    return FVector(
        FMath::RoundToFloat(Location.X / GridSize) * GridSize,
        FMath::RoundToFloat(Location.Y / GridSize) * GridSize,
        Location.Z
    );
}
