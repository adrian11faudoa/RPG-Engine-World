#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RFMiniatureBase.h"
#include "RFDiceSystem.h"
#include "RFInitiativeTracker.generated.h"

USTRUCT(BlueprintType)
struct FRFInitiativeEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) int32 InitiativeScore = 0;
    UPROPERTY(BlueprintReadWrite) int32 TieBreaker = 0;    // DEX mod for ties
    UPROPERTY(BlueprintReadWrite) FString CharacterName;
    UPROPERTY(BlueprintReadWrite) FString OwnerPlayerName;
    UPROPERTY(BlueprintReadWrite) ERFMiniatureType TokenType = ERFMiniatureType::NPC;
    UPROPERTY(BlueprintReadWrite) TWeakObjectPtr<ARFMiniatureBase> Miniature;
    UPROPERTY(BlueprintReadWrite) bool bIsDelayed = false;
    UPROPERTY(BlueprintReadWrite) bool bReadied = false;
    UPROPERTY(BlueprintReadWrite) FString ReadyTrigger;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTurnChanged,
    const FRFInitiativeEntry&, Current, int32, Round);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoundChanged, int32, NewRound);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCombatEnded);

UCLASS()
class REALMFORGE_API ARFInitiativeTracker : public AActor
{
    GENERATED_BODY()

public:
    ARFInitiativeTracker();

    // ─── Combat Lifecycle ─────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void StartCombat();

    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void EndCombat();

    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void NextTurn();

    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void PreviousTurn();  // For GM corrections

    // ─── Initiative Management ────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void AddToInitiative(ARFMiniatureBase* Mini, int32 DexMod = 0);

    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void AddManual(const FString& Name, int32 Score, ERFMiniatureType Type);

    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void RemoveFromInitiative(const FString& CharacterName);

    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void RollInitiativeForAll();

    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void SetInitiativeScore(const FString& CharacterName, int32 Score);

    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void DelayTurn(const FString& CharacterName);

    UFUNCTION(BlueprintCallable, Category = "RF|Combat")
    void ReadyAction(const FString& CharacterName, const FString& Trigger);

    // ─── Queries ──────────────────────────────────────────────────────
    UFUNCTION(BlueprintPure, Category = "RF|Combat")
    TArray<FRFInitiativeEntry> GetOrderedInitiative() const { return InitiativeOrder; }

    UFUNCTION(BlueprintPure, Category = "RF|Combat")
    FRFInitiativeEntry GetCurrentTurn() const;

    UFUNCTION(BlueprintPure, Category = "RF|Combat")
    int32 GetCurrentRound() const { return CurrentRound; }

    UFUNCTION(BlueprintPure, Category = "RF|Combat")
    bool IsInCombat() const { return bCombatActive; }

    UFUNCTION(BlueprintPure, Category = "RF|Combat")
    int32 GetTurnIndex() const { return CurrentTurnIndex; }

    // ─── Events ───────────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnTurnChanged OnTurnChanged;
    UPROPERTY(BlueprintAssignable) FOnRoundChanged OnRoundChanged;
    UPROPERTY(BlueprintAssignable) FOnCombatEnded OnCombatEnded;

protected:
    virtual void BeginPlay() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_UpdateInitiative(const TArray<FRFInitiativeEntry>& NewOrder);

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_TurnChanged(int32 TurnIndex, int32 Round);

private:
    UPROPERTY(Replicated)
    TArray<FRFInitiativeEntry> InitiativeOrder;

    UPROPERTY(Replicated)
    int32 CurrentTurnIndex = 0;

    UPROPERTY(Replicated)
    int32 CurrentRound = 0;

    UPROPERTY(Replicated)
    bool bCombatActive = false;

    UPROPERTY()
    ARFDiceManager* DiceManager = nullptr;

    void SortInitiative();
    void AdvanceToNextLivingCombatant();
    void BeginTurnForCurrent();
    void EndTurnForCurrent();
};
