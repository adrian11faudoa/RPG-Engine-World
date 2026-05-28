#include "RFDiceSystem.h"
#include "Kismet/KismetMathLibrary.h"
#include "Net/UnrealNetwork.h"

ARFDiceManager::ARFDiceManager()
{
    bReplicates = true;
    PrimaryActorTick.bCanEverTick = false;
}

void ARFDiceManager::BeginPlay()
{
    Super::BeginPlay();
}

FRFRollResult ARFDiceManager::Roll(const FRFDiceFormula& Formula)
{
    FRFRollResult Result;
    Result.Formula = FormulaToString(Formula);
    Result.Label = Formula.RollLabel;
    Result.Modifier = Formula.Modifier;
    Result.Visibility = Formula.Visibility;
    Result.Timestamp = FDateTime::Now();

    int32 Count = FMath::Max(1, Formula.Count);

    if (Formula.Advantage != ERFAdvantageMode::Normal)
    {
        // Advantage/disadvantage: roll twice, pick higher/lower
        int32 Roll1 = RollSingleDie(Formula.DieType, Formula.CustomFaces);
        int32 Roll2 = RollSingleDie(Formula.DieType, Formula.CustomFaces);
        int32 Chosen = (Formula.Advantage == ERFAdvantageMode::Advantage)
            ? FMath::Max(Roll1, Roll2)
            : FMath::Min(Roll1, Roll2);
        Result.IndividualResults.Add(Roll1);
        Result.IndividualResults.Add(Roll2);
        Result.Total = Chosen + Formula.Modifier;
    }
    else
    {
        int32 Sum = 0;
        for (int32 i = 0; i < Count; i++)
        {
            int32 Val = RollSingleDie(Formula.DieType, Formula.CustomFaces);
            Result.IndividualResults.Add(Val);
            Sum += Val;
        }
        Result.Total = Sum + Formula.Modifier;
    }

    // Critical/fumble for d20
    if (Formula.DieType == ERFDieType::D20 && Formula.Count == 1)
    {
        int32 NaturalRoll = Result.IndividualResults[0];
        if (Formula.Advantage == ERFAdvantageMode::Advantage && Result.IndividualResults.Num() > 1)
        {
            NaturalRoll = FMath::Max(Result.IndividualResults[0], Result.IndividualResults[1]);
        }
        Result.bIsCritical = (NaturalRoll == 20);
        Result.bIsFumble   = (NaturalRoll == 1);
    }

    RollHistory.Add(Result);
    if (RollHistory.Num() > 200) RollHistory.RemoveAt(0);

    // Network dispatch
    if (HasAuthority())
    {
        if (Formula.Visibility == ERFRollVisibility::Public)
        {
            Multicast_BroadcastRoll(Result, Result.RollerName);
        }
        else if (Formula.Visibility == ERFRollVisibility::GMOnly)
        {
            // Send to GM client only — handled in GM controller
            OnDiceRolled.Broadcast(Result, Result.RollerName);
        }
        else
        {
            Client_ReceivePrivateRoll(Result);
        }
    }

    return Result;
}

FRFRollResult ARFDiceManager::RollFromString(const FString& FormulaString,
    ERFRollVisibility Visibility)
{
    FRFDiceFormula Formula;
    Formula.Visibility = Visibility;

    if (!ParseFormulaString(FormulaString, Formula))
    {
        UE_LOG(LogTemp, Warning, TEXT("[RF|Dice] Invalid formula: %s"), *FormulaString);
    }

    return Roll(Formula);
}

FRFRollResult ARFDiceManager::RollCommand(const FString& Command, const FString& RollerName)
{
    // Handles: /roll 2d6+3, /gmroll d20, /initiative
    FString Cmd = Command.ToLower().TrimStartAndEnd();
    ERFRollVisibility Vis = ERFRollVisibility::Public;

    if (Cmd.StartsWith(TEXT("/gmroll")))
    {
        Vis = ERFRollVisibility::GMOnly;
        Cmd = Cmd.Mid(7).TrimStart();
    }
    else if (Cmd.StartsWith(TEXT("/roll")))
    {
        Cmd = Cmd.Mid(5).TrimStart();
    }
    else if (Cmd.StartsWith(TEXT("/initiative")))
    {
        return RollInitiative(0, RollerName);
    }

    FRFRollResult Result = RollFromString(Cmd, Vis);
    Result.RollerName = RollerName;
    return Result;
}

FRFRollResult ARFDiceManager::RollInitiative(int32 DexModifier, const FString& CharacterName)
{
    FRFDiceFormula Formula;
    Formula.DieType = ERFDieType::D20;
    Formula.Modifier = DexModifier;
    Formula.RollLabel = FString::Printf(TEXT("%s — Initiative"), *CharacterName);
    Formula.Visibility = ERFRollVisibility::Public;
    return Roll(Formula);
}

FRFRollResult ARFDiceManager::RollAttack(int32 AttackBonus, ERFAdvantageMode Mode,
    const FString& AttackerName, const FString& WeaponName)
{
    FRFDiceFormula Formula;
    Formula.DieType = ERFDieType::D20;
    Formula.Modifier = AttackBonus;
    Formula.Advantage = Mode;
    Formula.RollLabel = FString::Printf(TEXT("%s attacks with %s"), *AttackerName, *WeaponName);
    return Roll(Formula);
}

FRFRollResult ARFDiceManager::RollDamage(int32 DiceCount, ERFDieType DieType,
    int32 DamageBonus, bool bCritical)
{
    FRFDiceFormula Formula;
    Formula.Count = bCritical ? DiceCount * 2 : DiceCount;  // Crit doubles dice
    Formula.DieType = DieType;
    Formula.Modifier = DamageBonus;
    Formula.RollLabel = bCritical ? TEXT("CRITICAL DAMAGE!") : TEXT("Damage");
    return Roll(Formula);
}

FRFRollResult ARFDiceManager::RollSavingThrow(int32 SaveBonus, ERFAdvantageMode Mode,
    const FString& CharacterName, const FString& SaveType)
{
    FRFDiceFormula Formula;
    Formula.DieType = ERFDieType::D20;
    Formula.Modifier = SaveBonus;
    Formula.Advantage = Mode;
    Formula.RollLabel = FString::Printf(TEXT("%s — %s Save"), *CharacterName, *SaveType);
    return Roll(Formula);
}

int32 ARFDiceManager::RollSingleDie(ERFDieType DieType, int32 CustomFaces) const
{
    int32 Faces = GetDieFaces(DieType);
    if (DieType == ERFDieType::Custom) Faces = FMath::Max(2, CustomFaces);
    return FMath::RandRange(1, Faces);
}

int32 ARFDiceManager::GetDieFaces(ERFDieType DieType) const
{
    switch (DieType)
    {
        case ERFDieType::D4:   return 4;
        case ERFDieType::D6:   return 6;
        case ERFDieType::D8:   return 8;
        case ERFDieType::D10:  return 10;
        case ERFDieType::D12:  return 12;
        case ERFDieType::D20:  return 20;
        case ERFDieType::D100: return 100;
        default:               return 6;
    }
}

FString ARFDiceManager::FormulaToString(const FRFDiceFormula& Formula) const
{
    FString S = FString::Printf(TEXT("%dd%d"), Formula.Count, GetDieFaces(Formula.DieType));
    if (Formula.Modifier > 0) S += FString::Printf(TEXT("+%d"), Formula.Modifier);
    else if (Formula.Modifier < 0) S += FString::Printf(TEXT("%d"), Formula.Modifier);
    if (Formula.Advantage == ERFAdvantageMode::Advantage) S += TEXT(" (Adv)");
    else if (Formula.Advantage == ERFAdvantageMode::Disadvantage) S += TEXT(" (Dis)");
    return S;
}

bool ARFDiceManager::ParseFormulaString(const FString& Input, FRFDiceFormula& OutFormula) const
{
    // Parse "NdM+K" or "NdM-K" or "dM"
    FString Str = Input.ToLower().Replace(TEXT(" "), TEXT(""));

    // Advantage/disadvantage suffixes
    if (Str.EndsWith(TEXT("adv")))  { OutFormula.Advantage = ERFAdvantageMode::Advantage;    Str = Str.LeftChop(3); }
    if (Str.EndsWith(TEXT("dis")))  { OutFormula.Advantage = ERFAdvantageMode::Disadvantage; Str = Str.LeftChop(3); }

    int32 DPos = Str.Find(TEXT("d"));
    if (DPos == INDEX_NONE) return false;

    FString CountStr = Str.Left(DPos);
    OutFormula.Count = CountStr.IsEmpty() ? 1 : FCString::Atoi(*CountStr);

    FString Rest = Str.Mid(DPos + 1);
    int32 PlusPos = Rest.Find(TEXT("+"));
    int32 MinusPos = Rest.Find(TEXT("-"));

    if (PlusPos != INDEX_NONE)
    {
        OutFormula.Modifier = FCString::Atoi(*Rest.Mid(PlusPos + 1));
        Rest = Rest.Left(PlusPos);
    }
    else if (MinusPos != INDEX_NONE)
    {
        OutFormula.Modifier = -FCString::Atoi(*Rest.Mid(MinusPos + 1));
        Rest = Rest.Left(MinusPos);
    }

    int32 Faces = FCString::Atoi(*Rest);
    const TMap<int32, ERFDieType> FaceMap = {
        {4, ERFDieType::D4}, {6, ERFDieType::D6}, {8, ERFDieType::D8},
        {10, ERFDieType::D10}, {12, ERFDieType::D12}, {20, ERFDieType::D20}, {100, ERFDieType::D100}
    };

    const ERFDieType* Found = FaceMap.Find(Faces);
    if (Found) OutFormula.DieType = *Found;
    else { OutFormula.DieType = ERFDieType::Custom; OutFormula.CustomFaces = FMath::Max(2, Faces); }

    return true;
}

TArray<FRFRollResult> ARFDiceManager::GetRollHistory(int32 MaxEntries) const
{
    int32 Start = FMath::Max(0, RollHistory.Num() - MaxEntries);
    return TArray<FRFRollResult>(RollHistory.GetData() + Start, RollHistory.Num() - Start);
}

void ARFDiceManager::ClearHistory() { RollHistory.Empty(); }

void ARFDiceManager::SpawnPhysicsDie(ERFDieType DieType, FVector TrayLocation)
{
    // Physics die spawn — Blueprint implements the mesh & physics sim
    // This fires an event the BP can respond to
    UE_LOG(LogTemp, Log, TEXT("[RF|Dice] Spawning physics d%d at %s"),
        GetDieFaces(DieType), *TrayLocation.ToString());
}

void ARFDiceManager::ClearDiceTray()
{
    UE_LOG(LogTemp, Log, TEXT("[RF|Dice] Clearing dice tray"));
}

bool ARFDiceManager::Server_SubmitRoll_Validate(const FRFRollResult& Result, const FString& RollerName)
{
    return true; // Add anti-cheat validation here
}

void ARFDiceManager::Server_SubmitRoll_Implementation(const FRFRollResult& Result, const FString& RollerName)
{
    if (Result.Visibility == ERFRollVisibility::Public)
        Multicast_BroadcastRoll(Result, RollerName);
}

void ARFDiceManager::Multicast_BroadcastRoll_Implementation(const FRFRollResult& Result,
    const FString& RollerName)
{
    OnDiceRolled.Broadcast(Result, RollerName);
}

void ARFDiceManager::Client_ReceivePrivateRoll_Implementation(const FRFRollResult& Result)
{
    OnDiceRolled.Broadcast(Result, Result.RollerName);
}
