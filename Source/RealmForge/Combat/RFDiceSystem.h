#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RFDiceSystem.generated.h"

UENUM(BlueprintType)
enum class ERFDieType : uint8
{
    D4   UMETA(DisplayName = "d4"),
    D6   UMETA(DisplayName = "d6"),
    D8   UMETA(DisplayName = "d8"),
    D10  UMETA(DisplayName = "d10"),
    D12  UMETA(DisplayName = "d12"),
    D20  UMETA(DisplayName = "d20"),
    D100 UMETA(DisplayName = "d100"),
    Custom UMETA(DisplayName = "Custom")
};

UENUM(BlueprintType)
enum class ERFAdvantageMode : uint8
{
    Normal      UMETA(DisplayName = "Normal"),
    Advantage   UMETA(DisplayName = "Advantage"),
    Disadvantage UMETA(DisplayName = "Disadvantage")
};

UENUM(BlueprintType)
enum class ERFRollVisibility : uint8
{
    Public      UMETA(DisplayName = "Public"),    // Everyone sees
    GMOnly      UMETA(DisplayName = "GM Only"),   // Only GM sees
    Private     UMETA(DisplayName = "Private")    // Only roller sees
};

USTRUCT(BlueprintType)
struct FRFDiceFormula
{
    GENERATED_BODY()

    // e.g. "2d8+3" or "1d20+5"
    UPROPERTY(BlueprintReadWrite) int32 Count = 1;
    UPROPERTY(BlueprintReadWrite) ERFDieType DieType = ERFDieType::D20;
    UPROPERTY(BlueprintReadWrite) int32 Modifier = 0;
    UPROPERTY(BlueprintReadWrite) ERFAdvantageMode Advantage = ERFAdvantageMode::Normal;
    UPROPERTY(BlueprintReadWrite) ERFRollVisibility Visibility = ERFRollVisibility::Public;
    UPROPERTY(BlueprintReadWrite) FString RollLabel;         // e.g. "Attack Roll"
    UPROPERTY(BlueprintReadWrite) int32 CustomFaces = 6;     // for Custom die
};

USTRUCT(BlueprintType)
struct FRFRollResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) TArray<int32> IndividualResults;
    UPROPERTY(BlueprintReadOnly) int32 Total = 0;
    UPROPERTY(BlueprintReadOnly) int32 Modifier = 0;
    UPROPERTY(BlueprintReadOnly) FString Formula;
    UPROPERTY(BlueprintReadOnly) FString RollerName;
    UPROPERTY(BlueprintReadOnly) FString Label;
    UPROPERTY(BlueprintReadOnly) bool bIsCritical = false;
    UPROPERTY(BlueprintReadOnly) bool bIsFumble = false;
    UPROPERTY(BlueprintReadOnly) ERFRollVisibility Visibility = ERFRollVisibility::Public;
    UPROPERTY(BlueprintReadOnly) FDateTime Timestamp;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDiceRolled,
    const FRFRollResult&, Result, const FString&, RollerName);

UCLASS()
class REALMFORGE_API ARFDiceManager : public AActor
{
    GENERATED_BODY()

public:
    ARFDiceManager();

    // ─── Roll API ─────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Dice")
    FRFRollResult Roll(const FRFDiceFormula& Formula);

    // Parse a string formula like "2d6+3" or "d20 adv"
    UFUNCTION(BlueprintCallable, Category = "RF|Dice")
    FRFRollResult RollFromString(const FString& FormulaString,
        ERFRollVisibility Visibility = ERFRollVisibility::Public);

    // Chat commands
    UFUNCTION(BlueprintCallable, Category = "RF|Dice")
    FRFRollResult RollCommand(const FString& Command, const FString& RollerName);

    UFUNCTION(BlueprintCallable, Category = "RF|Dice")
    FRFRollResult RollInitiative(int32 DexModifier, const FString& CharacterName);

    UFUNCTION(BlueprintCallable, Category = "RF|Dice")
    FRFRollResult RollAttack(int32 AttackBonus, ERFAdvantageMode Mode,
        const FString& AttackerName, const FString& WeaponName);

    UFUNCTION(BlueprintCallable, Category = "RF|Dice")
    FRFRollResult RollDamage(int32 DiceCount, ERFDieType DieType,
        int32 DamageBonus, bool bCritical = false);

    UFUNCTION(BlueprintCallable, Category = "RF|Dice")
    FRFRollResult RollSavingThrow(int32 SaveBonus, ERFAdvantageMode Mode,
        const FString& CharacterName, const FString& SaveType);

    // ─── Physics Dice ─────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Dice|Physics")
    void SpawnPhysicsDie(ERFDieType DieType, FVector TrayLocation);

    UFUNCTION(BlueprintCallable, Category = "RF|Dice|Physics")
    void ClearDiceTray();

    // ─── History & Persistence ────────────────────────────────────────
    UFUNCTION(BlueprintPure, Category = "RF|Dice")
    TArray<FRFRollResult> GetRollHistory(int32 MaxEntries = 50) const;

    UFUNCTION(BlueprintCallable, Category = "RF|Dice")
    void ClearHistory();

    // ─── Events ──────────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable, Category = "RF|Dice|Events")
    FOnDiceRolled OnDiceRolled;

protected:
    virtual void BeginPlay() override;

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_SubmitRoll(const FRFRollResult& Result, const FString& RollerName);

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_BroadcastRoll(const FRFRollResult& Result, const FString& RollerName);

    UFUNCTION(Client, Reliable)
    void Client_ReceivePrivateRoll(const FRFRollResult& Result);

private:
    UPROPERTY()
    TArray<FRFRollResult> RollHistory;

    int32 RollSingleDie(ERFDieType DieType, int32 CustomFaces = 6) const;
    int32 GetDieFaces(ERFDieType DieType) const;
    FString FormulaToString(const FRFDiceFormula& Formula) const;
    bool ParseFormulaString(const FString& Input, FRFDiceFormula& OutFormula) const;
};
