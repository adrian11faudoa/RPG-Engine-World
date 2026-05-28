#include "RFInitiativeTracker.h"
#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

ARFInitiativeTracker::ARFInitiativeTracker()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    PrimaryActorTick.bCanEverTick = false;
}

void ARFInitiativeTracker::BeginPlay()
{
    Super::BeginPlay();

    // Find DiceManager in world
    for (TActorIterator<ARFDiceManager> It(GetWorld()); It; ++It)
    {
        DiceManager = *It;
        break;
    }
}

// ─── Combat Lifecycle ────────────────────────────────────────────────────────

void ARFInitiativeTracker::StartCombat()
{
    if (!HasAuthority()) return;

    bCombatActive = true;
    CurrentRound = 1;
    CurrentTurnIndex = 0;

    SortInitiative();
    Multicast_UpdateInitiative(InitiativeOrder);

    OnRoundChanged.Broadcast(CurrentRound);

    if (InitiativeOrder.Num() > 0)
    {
        BeginTurnForCurrent();
        Multicast_TurnChanged(CurrentTurnIndex, CurrentRound);
        OnTurnChanged.Broadcast(GetCurrentTurn(), CurrentRound);
    }

    UE_LOG(LogTemp, Log, TEXT("[RF|Combat] Combat started — Round 1, %d combatants"),
        InitiativeOrder.Num());
}

void ARFInitiativeTracker::EndCombat()
{
    if (!HasAuthority()) return;

    // Reset all miniature turn resources
    for (FRFInitiativeEntry& Entry : InitiativeOrder)
    {
        if (Entry.Miniature.IsValid())
        {
            Entry.Miniature->ResetTurnResources();
            Entry.Miniature->HideMovementRange();
        }
    }

    bCombatActive = false;
    CurrentRound = 0;
    CurrentTurnIndex = 0;

    OnCombatEnded.Broadcast();
    UE_LOG(LogTemp, Log, TEXT("[RF|Combat] Combat ended"));
}

void ARFInitiativeTracker::NextTurn()
{
    if (!HasAuthority() || !bCombatActive) return;

    EndTurnForCurrent();

    CurrentTurnIndex++;
    if (CurrentTurnIndex >= InitiativeOrder.Num())
    {
        CurrentTurnIndex = 0;
        CurrentRound++;
        OnRoundChanged.Broadcast(CurrentRound);

        // Tick status effects at top of round
        for (FRFInitiativeEntry& Entry : InitiativeOrder)
        {
            if (Entry.Miniature.IsValid())
                Entry.Miniature->TickStatusEffects();
        }
    }

    AdvanceToNextLivingCombatant();
    BeginTurnForCurrent();

    Multicast_TurnChanged(CurrentTurnIndex, CurrentRound);
    OnTurnChanged.Broadcast(GetCurrentTurn(), CurrentRound);
}

void ARFInitiativeTracker::PreviousTurn()
{
    if (!HasAuthority() || !bCombatActive) return;

    EndTurnForCurrent();
    CurrentTurnIndex = FMath::Max(0, CurrentTurnIndex - 1);

    Multicast_TurnChanged(CurrentTurnIndex, CurrentRound);
    OnTurnChanged.Broadcast(GetCurrentTurn(), CurrentRound);
}

// ─── Initiative Management ───────────────────────────────────────────────────

void ARFInitiativeTracker::AddToInitiative(ARFMiniatureBase* Mini, int32 DexMod)
{
    if (!HasAuthority() || !Mini) return;

    // Check for duplicate
    for (const FRFInitiativeEntry& Existing : InitiativeOrder)
    {
        if (Existing.CharacterName == Mini->CharacterName) return;
    }

    FRFInitiativeEntry Entry;
    Entry.CharacterName  = Mini->CharacterName;
    Entry.TieBreaker     = DexMod;
    Entry.TokenType      = Mini->MiniType;
    Entry.Miniature      = Mini;

    // Auto-roll initiative
    if (DiceManager)
    {
        FRFRollResult Roll = DiceManager->RollInitiative(DexMod, Mini->CharacterName);
        Entry.InitiativeScore = Roll.Total;
    }
    else
    {
        Entry.InitiativeScore = FMath::RandRange(1, 20) + DexMod;
    }

    InitiativeOrder.Add(Entry);
    SortInitiative();
    Multicast_UpdateInitiative(InitiativeOrder);
}

void ARFInitiativeTracker::AddManual(const FString& Name, int32 Score, ERFMiniatureType Type)
{
    if (!HasAuthority()) return;

    FRFInitiativeEntry Entry;
    Entry.CharacterName   = Name;
    Entry.InitiativeScore = Score;
    Entry.TokenType       = Type;

    InitiativeOrder.Add(Entry);
    SortInitiative();
    Multicast_UpdateInitiative(InitiativeOrder);
}

void ARFInitiativeTracker::RemoveFromInitiative(const FString& CharacterName)
{
    if (!HasAuthority()) return;

    int32 RemovedIdx = INDEX_NONE;
    for (int32 i = 0; i < InitiativeOrder.Num(); i++)
    {
        if (InitiativeOrder[i].CharacterName == CharacterName)
        {
            RemovedIdx = i;
            break;
        }
    }

    if (RemovedIdx == INDEX_NONE) return;

    InitiativeOrder.RemoveAt(RemovedIdx);

    // Adjust current turn index
    if (RemovedIdx < CurrentTurnIndex)
        CurrentTurnIndex = FMath::Max(0, CurrentTurnIndex - 1);
    else if (CurrentTurnIndex >= InitiativeOrder.Num())
        CurrentTurnIndex = 0;

    Multicast_UpdateInitiative(InitiativeOrder);
}

void ARFInitiativeTracker::RollInitiativeForAll()
{
    if (!HasAuthority()) return;

    for (FRFInitiativeEntry& Entry : InitiativeOrder)
    {
        int32 Roll = FMath::RandRange(1, 20) + Entry.TieBreaker;
        Entry.InitiativeScore = Roll;
    }

    SortInitiative();
    Multicast_UpdateInitiative(InitiativeOrder);
}

void ARFInitiativeTracker::SetInitiativeScore(const FString& CharacterName, int32 Score)
{
    if (!HasAuthority()) return;

    for (FRFInitiativeEntry& Entry : InitiativeOrder)
    {
        if (Entry.CharacterName == CharacterName)
        {
            Entry.InitiativeScore = Score;
            break;
        }
    }

    SortInitiative();
    Multicast_UpdateInitiative(InitiativeOrder);
}

void ARFInitiativeTracker::DelayTurn(const FString& CharacterName)
{
    if (!HasAuthority()) return;

    for (FRFInitiativeEntry& Entry : InitiativeOrder)
    {
        if (Entry.CharacterName == CharacterName)
        {
            Entry.bIsDelayed = true;
            break;
        }
    }

    Multicast_UpdateInitiative(InitiativeOrder);
}

void ARFInitiativeTracker::ReadyAction(const FString& CharacterName, const FString& Trigger)
{
    if (!HasAuthority()) return;

    for (FRFInitiativeEntry& Entry : InitiativeOrder)
    {
        if (Entry.CharacterName == CharacterName)
        {
            Entry.bReadied     = true;
            Entry.ReadyTrigger = Trigger;
            break;
        }
    }

    Multicast_UpdateInitiative(InitiativeOrder);
}

FRFInitiativeEntry ARFInitiativeTracker::GetCurrentTurn() const
{
    if (InitiativeOrder.IsValidIndex(CurrentTurnIndex))
        return InitiativeOrder[CurrentTurnIndex];
    return FRFInitiativeEntry();
}

// ─── Private Helpers ─────────────────────────────────────────────────────────

void ARFInitiativeTracker::SortInitiative()
{
    InitiativeOrder.Sort([](const FRFInitiativeEntry& A, const FRFInitiativeEntry& B)
    {
        if (A.InitiativeScore != B.InitiativeScore)
            return A.InitiativeScore > B.InitiativeScore;
        return A.TieBreaker > B.TieBreaker;   // DEX mod breaks ties
    });
}

void ARFInitiativeTracker::AdvanceToNextLivingCombatant()
{
    // Skip dead or invalid miniatures
    int32 Attempts = 0;
    while (Attempts < InitiativeOrder.Num())
    {
        const FRFInitiativeEntry& Entry = InitiativeOrder[CurrentTurnIndex];
        bool bSkip = false;

        if (Entry.Miniature.IsValid() && !Entry.Miniature->IsAlive())
            bSkip = true;

        if (!bSkip) break;

        CurrentTurnIndex = (CurrentTurnIndex + 1) % InitiativeOrder.Num();
        Attempts++;
    }
}

void ARFInitiativeTracker::BeginTurnForCurrent()
{
    if (!InitiativeOrder.IsValidIndex(CurrentTurnIndex)) return;

    FRFInitiativeEntry& Entry = InitiativeOrder[CurrentTurnIndex];
    Entry.bIsDelayed = false;

    if (Entry.Miniature.IsValid())
    {
        Entry.Miniature->ResetTurnResources();
        Entry.Miniature->SetSelected(true);

        UE_LOG(LogTemp, Log, TEXT("[RF|Combat] Turn: %s (Round %d)"),
            *Entry.CharacterName, CurrentRound);
    }
}

void ARFInitiativeTracker::EndTurnForCurrent()
{
    if (!InitiativeOrder.IsValidIndex(CurrentTurnIndex)) return;

    const FRFInitiativeEntry& Entry = InitiativeOrder[CurrentTurnIndex];
    if (Entry.Miniature.IsValid())
    {
        Entry.Miniature->SetSelected(false);
        Entry.Miniature->HideMovementRange();
    }
}

// ─── Replication ─────────────────────────────────────────────────────────────

void ARFInitiativeTracker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARFInitiativeTracker, InitiativeOrder);
    DOREPLIFETIME(ARFInitiativeTracker, CurrentTurnIndex);
    DOREPLIFETIME(ARFInitiativeTracker, CurrentRound);
    DOREPLIFETIME(ARFInitiativeTracker, bCombatActive);
}

void ARFInitiativeTracker::Multicast_UpdateInitiative_Implementation(
    const TArray<FRFInitiativeEntry>& NewOrder)
{
    InitiativeOrder = NewOrder;
}

void ARFInitiativeTracker::Multicast_TurnChanged_Implementation(int32 TurnIndex, int32 Round)
{
    CurrentTurnIndex = TurnIndex;
    CurrentRound = Round;

    if (InitiativeOrder.IsValidIndex(TurnIndex))
    {
        OnTurnChanged.Broadcast(InitiativeOrder[TurnIndex], Round);
    }
}
