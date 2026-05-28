#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RFCampaignManager.generated.h"

USTRUCT(BlueprintType)
struct FRFQuestEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString QuestID;
    UPROPERTY(BlueprintReadWrite) FString Title;
    UPROPERTY(BlueprintReadWrite) FString Description;
    UPROPERTY(BlueprintReadWrite) bool bIsComplete = false;
    UPROPERTY(BlueprintReadWrite) TArray<FString> ObjectiveIDs;
    UPROPERTY(BlueprintReadWrite) TMap<FString, bool> ObjectiveStates;
};

USTRUCT(BlueprintType)
struct FRFJournalEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString EntryID;
    UPROPERTY(BlueprintReadWrite) FString Title;
    UPROPERTY(BlueprintReadWrite) FString Body;
    UPROPERTY(BlueprintReadWrite) FString Author;     // "GM" or player name
    UPROPERTY(BlueprintReadWrite) FDateTime CreatedAt;
    UPROPERTY(BlueprintReadWrite) bool bIsGMOnly = false;
    UPROPERTY(BlueprintReadWrite) FString SessionDate;
};

USTRUCT(BlueprintType)
struct FRFNPCRecord
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString NPCID;
    UPROPERTY(BlueprintReadWrite) FString Name;
    UPROPERTY(BlueprintReadWrite) FString Description;
    UPROPERTY(BlueprintReadWrite) FString Location;
    UPROPERTY(BlueprintReadWrite) FString Attitude;   // Friendly/Neutral/Hostile
    UPROPERTY(BlueprintReadWrite) TMap<FString, FString> Notes;  // Key=topic, Value=note
    UPROPERTY(BlueprintReadWrite) bool bIsAlive = true;
};

USTRUCT(BlueprintType)
struct FRFCampaignData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString CampaignID;
    UPROPERTY(BlueprintReadWrite) FString Title;
    UPROPERTY(BlueprintReadWrite) FString Description;
    UPROPERTY(BlueprintReadWrite) FString GMName;
    UPROPERTY(BlueprintReadWrite) FDateTime CreatedAt;
    UPROPERTY(BlueprintReadWrite) FDateTime LastPlayedAt;
    UPROPERTY(BlueprintReadWrite) int32 SessionCount = 0;
    UPROPERTY(BlueprintReadWrite) FString ActiveMapName;
    UPROPERTY(BlueprintReadWrite) TArray<FRFQuestEntry> Quests;
    UPROPERTY(BlueprintReadWrite) TArray<FRFJournalEntry> Journal;
    UPROPERTY(BlueprintReadWrite) TArray<FRFNPCRecord> NPCs;
    UPROPERTY(BlueprintReadWrite) TMap<FString, FString> WorldState; // Flexible KV store
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCampaignLoaded, const FRFCampaignData&, Campaign);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCampaignSaved, const FString&, CampaignID);

UCLASS()
class REALMFORGE_API URFCampaignManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // ─── Campaign Lifecycle ───────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Campaign")
    FString CreateCampaign(const FString& Title, const FString& GMName);

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign")
    bool SaveCampaign(const FRFCampaignData& Data);

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign")
    bool LoadCampaign(const FString& CampaignID, FRFCampaignData& OutData);

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign")
    bool DeleteCampaign(const FString& CampaignID);

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign")
    TArray<FRFCampaignData> GetAllCampaigns();

    UFUNCTION(BlueprintPure, Category = "RF|Campaign")
    FRFCampaignData GetActiveCampaign() const { return ActiveCampaign; }

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign")
    void SetActiveCampaign(const FString& CampaignID);

    // ─── Quick Save ───────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Campaign")
    void AutoSave();

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign")
    void StartAutoSaveTimer(float IntervalSeconds = 300.0f);

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign")
    void StopAutoSaveTimer();

    // ─── Quests ───────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Campaign|Quests")
    void AddQuest(const FRFQuestEntry& Quest);

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign|Quests")
    void CompleteObjective(const FString& QuestID, const FString& ObjectiveID);

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign|Quests")
    void CompleteQuest(const FString& QuestID);

    UFUNCTION(BlueprintPure, Category = "RF|Campaign|Quests")
    TArray<FRFQuestEntry> GetActiveQuests() const;

    // ─── Journal ──────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Campaign|Journal")
    FString AddJournalEntry(const FString& Title, const FString& Body,
        const FString& Author, bool bGMOnly = false);

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign|Journal")
    void EditJournalEntry(const FString& EntryID, const FString& NewBody);

    UFUNCTION(BlueprintPure, Category = "RF|Campaign|Journal")
    TArray<FRFJournalEntry> GetJournalEntries(bool bIncludeGMOnly = false) const;

    // ─── NPC Database ─────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Campaign|NPCs")
    FString RegisterNPC(const FRFNPCRecord& NPC);

    UFUNCTION(BlueprintCallable, Category = "RF|Campaign|NPCs")
    void UpdateNPCNote(const FString& NPCID, const FString& Topic, const FString& Note);

    UFUNCTION(BlueprintPure, Category = "RF|Campaign|NPCs")
    FRFNPCRecord GetNPC(const FString& NPCID) const;

    // ─── World State ──────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Campaign|State")
    void SetWorldFlag(const FString& Key, const FString& Value);

    UFUNCTION(BlueprintPure, Category = "RF|Campaign|State")
    FString GetWorldFlag(const FString& Key, const FString& Default = TEXT("")) const;

    UFUNCTION(BlueprintPure, Category = "RF|Campaign|State")
    bool GetWorldFlagBool(const FString& Key) const;

    // ─── Events ───────────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnCampaignLoaded OnCampaignLoaded;
    UPROPERTY(BlueprintAssignable) FOnCampaignSaved OnCampaignSaved;

private:
    FRFCampaignData ActiveCampaign;
    FTimerHandle AutoSaveTimer;

    FString GetSavePath(const FString& CampaignID) const;
    FString GetCampaignDirectory() const;
    bool SerializeCampaign(const FRFCampaignData& Data, TArray<uint8>& OutBytes) const;
    bool DeserializeCampaign(const TArray<uint8>& Bytes, FRFCampaignData& OutData) const;
};
