#include "RFOllamaIntegration.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Guid.h"

void URFOllamaIntegration::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    CheckOllamaAvailability(FOnAIResponse());
}

FString URFOllamaIntegration::GenerateNPCDialogue(const FRFNPCPersonality& NPC,
    const FString& PlayerInput, const FOnAIResponse& Callback)
{
    if (!IsAvailable())
    {
        FRFAIResponse Fallback;
        Fallback.bSuccess = false;
        Fallback.ErrorMessage = TEXT("AI not available");
        Fallback.ResponseText = TEXT("*The figure stares at you silently.*");
        Callback.ExecuteIfBound(Fallback);
        return FString();
    }

    FString SystemPrompt = BuildNPCSystemPrompt(NPC);
    FString HistoryBlock;
    for (const FString& Line : NPC.ConversationHistory)
        HistoryBlock += Line + TEXT("\n");

    FString FullPrompt = FString::Printf(
        TEXT("%s\n\nConversation so far:\n%s\nPlayer: %s\n%s:"),
        *SystemPrompt, *HistoryBlock, *PlayerInput, *NPC.Name);

    return SubmitRequest(FullPrompt, ERFAITask::NPCDialogue, Callback);
}

FString URFOllamaIntegration::GenerateDungeonDescription(const FString& DungeonTheme,
    int32 RoomCount, const FOnAIResponse& Callback)
{
    FString Prompt = FString::Printf(
        TEXT("You are a creative dungeon master. Describe a %s dungeon with %d rooms. "
             "For each room provide: name, description (2 sentences), notable features, "
             "and one possible encounter or secret. Format as a numbered list. "
             "Keep descriptions evocative and atmospheric."),
        *DungeonTheme, RoomCount);

    return SubmitRequest(Prompt, ERFAITask::DungeonGeneration, Callback);
}

FString URFOllamaIntegration::GenerateQuestHook(const FString& CampaignContext,
    const FString& PlayerLevel, const FOnAIResponse& Callback)
{
    FString Prompt = FString::Printf(
        TEXT("Generate a compelling quest hook for a %s level party in this campaign setting: %s. "
             "Include: a mysterious situation, a compelling NPC questgiver, "
             "a clear initial objective, and a hidden complication. Keep it to 150 words."),
        *PlayerLevel, *CampaignContext);

    return SubmitRequest(Prompt, ERFAITask::QuestGeneration, Callback);
}

FString URFOllamaIntegration::GenerateLore(const FString& Topic,
    const FString& WorldContext, const FOnAIResponse& Callback)
{
    FString Prompt = FString::Printf(
        TEXT("In the world of: %s\n\nWrite 2-3 paragraphs of immersive lore about: %s. "
             "Write it as if from an in-world encyclopedia or ancient tome. "
             "Include history, cultural significance, and one mysterious element."),
        *WorldContext, *Topic);

    return SubmitRequest(Prompt, ERFAITask::LoreGeneration, Callback);
}

FString URFOllamaIntegration::GenerateTavernScene(const FString& TavernName,
    const TArray<FString>& NPCNames, const FOnAIResponse& Callback)
{
    FString NPCList = FString::Join(NPCNames, TEXT(", "));
    FString Prompt = FString::Printf(
        TEXT("Describe a lively scene in '%s' tavern. These NPCs are present: %s. "
             "Write a short atmospheric description of the tavern, then give each NPC "
             "one interesting snippet of conversation the players might overhear. "
             "Keep each snippet to one sentence. Total response under 200 words."),
        *TavernName, *NPCList);

    return SubmitRequest(Prompt, ERFAITask::TavernConversation, Callback);
}

FString URFOllamaIntegration::AskGMAssistant(const FString& Question,
    const FString& CampaignContext, const FOnAIResponse& Callback)
{
    FString Prompt = BuildGMAssistantPrompt(CampaignContext) +
        TEXT("\n\nGM Question: ") + Question;

    return SubmitRequest(Prompt, ERFAITask::GMAssist, Callback);
}

void URFOllamaIntegration::CheckOllamaAvailability(const FOnAIResponse& Callback)
{
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request =
        FHttpModule::Get().CreateRequest();

    Request->SetURL(TEXT("http://localhost:11434/api/tags"));
    Request->SetVerb(TEXT("GET"));
    Request->SetTimeout(3.0f);

    Request->OnProcessRequestComplete().BindLambda(
        [this, Callback](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
        {
            bOllamaAvailable = bSuccess && Resp.IsValid() && Resp->GetResponseCode() == 200;
            UE_LOG(LogTemp, Log, TEXT("[RF|AI] Ollama available: %s"),
                bOllamaAvailable ? TEXT("Yes") : TEXT("No"));

            FRFAIResponse Result;
            Result.bSuccess = bOllamaAvailable;
            Result.ResponseText = bOllamaAvailable ? TEXT("Ollama online") : TEXT("Ollama not running");
            Callback.ExecuteIfBound(Result);
        });

    Request->ProcessRequest();
}

void URFOllamaIntegration::CancelRequest(const FString& RequestID)
{
    if (FHttpRequestPtr* Found = ActiveRequests.Find(RequestID))
    {
        (*Found)->CancelRequest();
        ActiveRequests.Remove(RequestID);
    }
}

// ─── Private ────────────────────────────────────────────────────────────────

FString URFOllamaIntegration::SubmitRequest(const FString& Prompt, ERFAITask TaskType,
    const FOnAIResponse& Callback)
{
    FString RequestID = GenerateRequestID();
    float StartTime = FPlatformTime::Seconds() * 1000.0f;

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
        FHttpModule::Get().CreateRequest();

    HttpRequest->SetURL(OllamaEndpoint);
    HttpRequest->SetVerb(TEXT("POST"));
    HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    // Build JSON payload
    TSharedPtr<FJsonObject> JsonBody = MakeShareable(new FJsonObject);
    JsonBody->SetStringField(TEXT("model"), DefaultModel);
    JsonBody->SetStringField(TEXT("prompt"), Prompt);
    JsonBody->SetBoolField(TEXT("stream"), false);

    TSharedPtr<FJsonObject> Options = MakeShareable(new FJsonObject);
    Options->SetNumberField(TEXT("num_predict"), MaxTokens);
    Options->SetNumberField(TEXT("temperature"), Temperature);
    JsonBody->SetObjectField(TEXT("options"), Options);

    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonBody.ToSharedRef(), Writer);
    HttpRequest->SetContentAsString(JsonString);

    HttpRequest->OnProcessRequestComplete().BindUObject(this,
        &URFOllamaIntegration::OnHttpResponse, RequestID, TaskType, Callback, StartTime);

    HttpRequest->ProcessRequest();
    ActiveRequests.Add(RequestID, HttpRequest);

    UE_LOG(LogTemp, Verbose, TEXT("[RF|AI] Submitted %s request [%s]"),
        *UEnum::GetValueAsString(TaskType), *RequestID);

    return RequestID;
}

void URFOllamaIntegration::OnHttpResponse(FHttpRequestPtr Request, FHttpResponsePtr Response,
    bool bConnectedSuccessfully, FString RequestID, ERFAITask TaskType,
    FOnAIResponse Callback, float StartTime)
{
    ActiveRequests.Remove(RequestID);

    FRFAIResponse Result;
    Result.TaskType = TaskType;
    Result.GenerationTimeMs = (FPlatformTime::Seconds() * 1000.0f) - StartTime;

    if (!bConnectedSuccessfully || !Response.IsValid())
    {
        Result.bSuccess = false;
        Result.ErrorMessage = TEXT("Failed to connect to Ollama");
        Result.ResponseText = GetFallbackResponse(TaskType);
        Callback.ExecuteIfBound(Result);
        OnAIResponseReady.Broadcast(Result, RequestID);
        return;
    }

    if (Response->GetResponseCode() != 200)
    {
        Result.bSuccess = false;
        Result.ErrorMessage = FString::Printf(TEXT("HTTP %d: %s"),
            Response->GetResponseCode(), *Response->GetContentAsString());
        Result.ResponseText = GetFallbackResponse(TaskType);
        Callback.ExecuteIfBound(Result);
        OnAIResponseReady.Broadcast(Result, RequestID);
        return;
    }

    // Parse JSON response
    TSharedPtr<FJsonObject> JsonResponse;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());

    if (FJsonSerializer::Deserialize(Reader, JsonResponse) && JsonResponse.IsValid())
    {
        FString RawText;
        if (JsonResponse->TryGetStringField(TEXT("response"), RawText))
        {
            Result.bSuccess = true;
            Result.ResponseText = SanitizeResponse(RawText);
        }
        else
        {
            Result.bSuccess = false;
            Result.ErrorMessage = TEXT("No 'response' field in JSON");
            Result.ResponseText = GetFallbackResponse(TaskType);
        }
    }
    else
    {
        Result.bSuccess = false;
        Result.ErrorMessage = TEXT("Failed to parse JSON response");
    }

    Callback.ExecuteIfBound(Result);
    OnAIResponseReady.Broadcast(Result, RequestID);
}

FString URFOllamaIntegration::BuildNPCSystemPrompt(const FRFNPCPersonality& NPC) const
{
    return FString::Printf(
        TEXT("You are %s, a %s %s in a fantasy tabletop RPG world.\n"
             "Personality: %s\nBackground: %s\nCurrent mood: %s\n"
             "What you know: %s\n\n"
             "Stay completely in character. Give responses of 1-3 sentences. "
             "Never break the fourth wall. Use period-appropriate language."),
        *NPC.Name, *NPC.Race, *NPC.Occupation,
        *NPC.Personality, *NPC.Background, *NPC.CurrentMood, *NPC.KnownInfo);
}

FString URFOllamaIntegration::BuildGMAssistantPrompt(const FString& Context) const
{
    return FString::Printf(
        TEXT("You are an expert tabletop RPG Game Master assistant. "
             "Campaign context: %s\n"
             "Provide concise, practical advice to help the GM run a better session. "
             "Focus on story, player engagement, and dramatic tension."),
        *Context);
}

FString URFOllamaIntegration::SanitizeResponse(const FString& RawResponse) const
{
    FString Clean = RawResponse.TrimStartAndEnd();
    // Remove any accidentally generated system tokens
    Clean = Clean.Replace(TEXT("<|"), TEXT("")).Replace(TEXT("|>"), TEXT(""));
    return Clean;
}

FString URFOllamaIntegration::GenerateRequestID() const
{
    return FGuid::NewGuid().ToString(EGuidFormats::Short);
}

FString URFOllamaIntegration::GetFallbackResponse(ERFAITask TaskType) const
{
    switch (TaskType)
    {
        case ERFAITask::NPCDialogue:
            return TEXT("*The figure eyes you warily and says nothing.*");
        case ERFAITask::DungeonGeneration:
            return TEXT("A dark dungeon with stone walls and flickering torches.");
        case ERFAITask::QuestGeneration:
            return TEXT("A mysterious stranger needs help with an urgent matter.");
        default:
            return TEXT("[AI unavailable]");
    }
}
