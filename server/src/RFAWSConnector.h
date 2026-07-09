#pragma once
/**
 * RFAWSConnector.h
 * Connects the UE5 game client to the RealmForge AWS backend.
 * Handles: login, session creation/joining, asset downloads from CloudFront,
 *          and forwarding WebSocket events to/from game systems.
 *
 * Thread-safe: HTTP calls made on background threads, results
 * dispatched back to game thread via AsyncTask.
 */

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Http.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "RFAWSConnector.generated.h"

USTRUCT(BlueprintType)
struct FRFUserInfo
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString UserID;
    UPROPERTY(BlueprintReadOnly) FString Username;
    UPROPERTY(BlueprintReadOnly) FString Email;
    UPROPERTY(BlueprintReadOnly) FString Role;  // "player" | "gm" | "admin"
};

USTRUCT(BlueprintType)
struct FRFGameSessionInfo
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString GameSessionId;
    UPROPERTY(BlueprintReadOnly) FString PlayerSessionId;
    UPROPERTY(BlueprintReadOnly) FString ServerIP;
    UPROPERTY(BlueprintReadOnly) int32   ServerPort = 7777;
    UPROPERTY(BlueprintReadOnly) FString ServerEndpoint;  // "IP:Port"
};

USTRUCT(BlueprintType)
struct FRFSessionListing
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FString GameSessionId;
    UPROPERTY(BlueprintReadOnly) FString Name;
    UPROPERTY(BlueprintReadOnly) FString CampaignId;
    UPROPERTY(BlueprintReadOnly) FString GMName;
    UPROPERTY(BlueprintReadOnly) int32   Players = 0;
    UPROPERTY(BlueprintReadOnly) int32   MaxPlayers = 8;
};

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLoginComplete,   bool, bSuccess, const FRFUserInfo&, User);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSessionCreated,  bool, bSuccess, const FRFGameSessionInfo&, Session);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSessionJoined,   bool, bSuccess, const FRFGameSessionInfo&, Session);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSessionsListed,  bool, bSuccess, const TArray<FRFSessionListing>&, Sessions);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAssetDownloaded, bool, bSuccess, const FString&, LocalPath);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam (FOnWSMessage,       const FString&, MessageJson);

UCLASS()
class REALMFORGE_API URFAWSConnector : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ─── Configuration ────────────────────────────────────────
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|AWS")
    FString APIBaseURL = TEXT("https://realmforge.gg/api");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|AWS")
    FString CDNBaseURL = TEXT("https://realmforge.gg/assets");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RF|AWS")
    FString WSBaseURL  = TEXT("wss://realmforge.gg/ws");

    // ─── Auth ─────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|AWS|Auth")
    void Login(const FString& Email, const FString& Password);

    UFUNCTION(BlueprintCallable, Category = "RF|AWS|Auth")
    void Register(const FString& Username, const FString& Email, const FString& Password);

    UFUNCTION(BlueprintCallable, Category = "RF|AWS|Auth")
    void Logout();

    UFUNCTION(BlueprintPure, Category = "RF|AWS|Auth")
    bool IsLoggedIn() const { return !AuthToken.IsEmpty(); }

    UFUNCTION(BlueprintPure, Category = "RF|AWS|Auth")
    FRFUserInfo GetCurrentUser() const { return CurrentUser; }

    // ─── Sessions ─────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|AWS|Sessions")
    void CreateGameSession(const FString& CampaignId, int32 MaxPlayers = 8);

    UFUNCTION(BlueprintCallable, Category = "RF|AWS|Sessions")
    void JoinGameSession(const FString& GameSessionId);

    UFUNCTION(BlueprintCallable, Category = "RF|AWS|Sessions")
    void ListActiveSessions();

    // ─── Assets ───────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|AWS|Assets")
    void DownloadAsset(const FString& AssetKey, const FString& LocalSaveDir);

    UFUNCTION(BlueprintPure, Category = "RF|AWS|Assets")
    FString GetCDNUrl(const FString& AssetKey) const;

    // ─── WebSocket Relay ──────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|AWS|WebSocket")
    void ConnectWebSocket(const FString& SessionId);

    UFUNCTION(BlueprintCallable, Category = "RF|AWS|WebSocket")
    void SendWSDiceRoll(const FString& Formula, int32 Total, const FString& Visibility = TEXT("public"));

    UFUNCTION(BlueprintCallable, Category = "RF|AWS|WebSocket")
    void SendWSChat(const FString& Text, const FString& Style = TEXT("normal"));

    UFUNCTION(BlueprintCallable, Category = "RF|AWS|WebSocket")
    void SendWSFogUpdate(const FString& FogDataJson);

    UFUNCTION(BlueprintCallable, Category = "RF|AWS|WebSocket")
    void SendWSInitiative(const FString& InitiativeJson);

    UFUNCTION(BlueprintCallable, Category = "RF|AWS|WebSocket")
    void DisconnectWebSocket();

    // ─── Delegates ────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnLoginComplete   OnLoginComplete;
    UPROPERTY(BlueprintAssignable) FOnSessionCreated  OnSessionCreated;
    UPROPERTY(BlueprintAssignable) FOnSessionJoined   OnSessionJoined;
    UPROPERTY(BlueprintAssignable) FOnSessionsListed  OnSessionsListed;
    UPROPERTY(BlueprintAssignable) FOnAssetDownloaded OnAssetDownloaded;
    UPROPERTY(BlueprintAssignable) FOnWSMessage       OnWSMessage;

private:
    FString AuthToken;
    FRFUserInfo CurrentUser;

    // HTTP helpers
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(
        const FString& Verb, const FString& Path,
        const TSharedPtr<FJsonObject>& Body = nullptr);

    void OnLoginResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSuccess);
    void OnCreateSessionResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSuccess);
    void OnJoinSessionResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSuccess);
    void OnListSessionsResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSuccess);
    void OnDownloadResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bSuccess, FString LocalPath);

    FRFGameSessionInfo ParseSessionInfo(const TSharedPtr<FJsonObject>& Json);

    // WebSocket (using UE5 IWebSocket module)
    TSharedPtr<class IWebSocket> WebSocket;
    FString ActiveSessionId;
    void OnWSConnected();
    void OnWSClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
    void OnWSReceived(const FString& Message);
    void WSSend(const TSharedPtr<FJsonObject>& Payload);

    // Persistent auth token storage
    static const FString TOKEN_SAVE_KEY;
    void SaveToken(const FString& Token);
    FString LoadToken() const;
};
