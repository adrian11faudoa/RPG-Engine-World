#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Http.h"
#include "RFOllamaIntegration.generated.h"

UENUM(BlueprintType)
enum class ERFAITask : uint8
{
    NPCDialogue         UMETA(DisplayName = "NPC Dialogue"),
    DungeonGeneration   UMETA(DisplayName = "Dungeon Generation"),
    QuestGeneration     UMETA(DisplayName = "Quest Generation"),
    LoreGeneration      UMETA(DisplayName = "Lore Generation"),
    TavernConversation  UMETA(DisplayName = "Tavern Conversation"),
    GMAssist            UMETA(DisplayName = "GM Assistant")
};

USTRUCT(BlueprintType)
struct FRFNPCPersonality
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString Name;
    UPROPERTY(BlueprintReadWrite) FString Race;
    UPROPERTY(BlueprintReadWrite) FString Occupation;
    UPROPERTY(BlueprintReadWrite) FString Personality;   // e.g. "gruff but kind-hearted"
    UPROPERTY(BlueprintReadWrite) FString Background;
    UPROPERTY(BlueprintReadWrite) FString CurrentMood;
    UPROPERTY(BlueprintReadWrite) FString KnownInfo;     // What this NPC knows
    UPROPERTY(BlueprintReadWrite) TArray<FString> ConversationHistory;
};

USTRUCT(BlueprintType)
struct FRFAIResponse
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FString ResponseText;
    UPROPERTY(BlueprintReadOnly) bool bSuccess = false;
    UPROPERTY(BlueprintReadOnly) FString ErrorMessage;
    UPROPERTY(BlueprintReadOnly) ERFAITask TaskType;
    UPROPERTY(BlueprintReadOnly) float GenerationTimeMs = 0.0f;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnAIResponse, const FRFAIResponse&, Response);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAIResponseReady, const FRFAIResponse&, Response, const FString&, RequestID);

UCLASS()
class REALMFORGE_API URFOllamaIntegration : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // ─── Configuration ────────────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|AI")
    FString OllamaEndpoint = TEXT("http://localhost:11434/api/generate");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|AI")
    FString DefaultModel = TEXT("llama3");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|AI")
    int32 MaxTokens = 256;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|AI")
    float Temperature = 0.8f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|AI")
    bool bEnabled = true;

    // ─── NPC Dialogue ────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|AI|NPC")
    FString GenerateNPCDialogue(const FRFNPCPersonality& NPC,
        const FString& PlayerInput,
        const FOnAIResponse& Callback);

    UFUNCTION(BlueprintCallable, Category = "RF|AI|NPC")
    FString GenerateNPCReaction(const FRFNPCPersonality& NPC,
        const FString& Situation,
        const FOnAIResponse& Callback);

    // ─── Dungeon & Quest Generation ───────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|AI|World")
    FString GenerateDungeonDescription(const FString& DungeonTheme,
        int32 RoomCount, const FOnAIResponse& Callback);

    UFUNCTION(BlueprintCallable, Category = "RF|AI|World")
    FString GenerateQuestHook(const FString& CampaignContext,
        const FString& PlayerLevel, const FOnAIResponse& Callback);

    UFUNCTION(BlueprintCallable, Category = "RF|AI|World")
    FString GenerateLore(const FString& Topic,
        const FString& WorldContext, const FOnAIResponse& Callback);

    UFUNCTION(BlueprintCallable, Category = "RF|AI|World")
    FString GenerateTavernScene(const FString& TavernName,
        const TArray<FString>& NPCNames, const FOnAIResponse& Callback);

    // ─── GM Assistant ─────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|AI|GM")
    FString AskGMAssistant(const FString& Question,
        const FString& CampaignContext, const FOnAIResponse& Callback);

    // ─── Utility ──────────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|AI")
    void CheckOllamaAvailability(const FOnAIResponse& Callback);

    UFUNCTION(BlueprintCallable, Category = "RF|AI")
    void CancelRequest(const FString& RequestID);

    UFUNCTION(BlueprintPure, Category = "RF|AI")
    bool IsAvailable() const { return bOllamaAvailable && bEnabled; }

    UPROPERTY(BlueprintAssignable) FOnAIResponseReady OnAIResponseReady;

private:
    bool bOllamaAvailable = false;
    TMap<FString, FHttpRequestPtr> ActiveRequests;

    FString SubmitRequest(const FString& Prompt, ERFAITask TaskType,
        const FOnAIResponse& Callback);

    void OnHttpResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
        bool bConnectedSuccessfully, FString RequestID,
        ERFAITask TaskType, FOnAIResponse Callback, float StartTime);

    FString BuildNPCSystemPrompt(const FRFNPCPersonality& NPC) const;
    FString BuildGMAssistantPrompt(const FString& Context) const;
    FString SanitizeResponse(const FString& RawResponse) const;
    FString GenerateRequestID() const;
};
