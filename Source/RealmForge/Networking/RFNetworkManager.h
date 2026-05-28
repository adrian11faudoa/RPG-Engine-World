#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "RFNetworkManager.generated.h"

UENUM(BlueprintType)
enum class ERFPlayerRole : uint8
{
    GameMaster      UMETA(DisplayName = "Game Master"),
    AssistantGM     UMETA(DisplayName = "Assistant GM"),
    Player          UMETA(DisplayName = "Player"),
    Spectator       UMETA(DisplayName = "Spectator")
};

USTRUCT(BlueprintType)
struct FRFSessionInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    FString SessionName;

    UPROPERTY(BlueprintReadWrite)
    FString CampaignName;

    UPROPERTY(BlueprintReadWrite)
    int32 MaxPlayers = 8;

    UPROPERTY(BlueprintReadWrite)
    int32 CurrentPlayers = 0;

    UPROPERTY(BlueprintReadWrite)
    bool bIsLAN = false;

    UPROPERTY(BlueprintReadWrite)
    bool bIsPasswordProtected = false;

    UPROPERTY(BlueprintReadWrite)
    FString GMName;
};

USTRUCT(BlueprintType)
struct FRFPlayerInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    FString PlayerName;

    UPROPERTY(BlueprintReadWrite)
    ERFPlayerRole Role = ERFPlayerRole::Player;

    UPROPERTY(BlueprintReadWrite)
    FString CharacterName;

    UPROPERTY(BlueprintReadWrite)
    bool bIsConnected = false;

    UPROPERTY(BlueprintReadWrite)
    FUniqueNetIdRepl NetId;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionCreated, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionJoined, bool, bSuccess);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSessionDestroyed);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerJoined, const FRFPlayerInfo&, PlayerInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerLeft, const FString&, PlayerName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSearchComplete, const TArray<FRFSessionInfo>&, Results, bool, bSuccess);

UCLASS(ClassGroup=(RealmForge), meta=(BlueprintSpawnableComponent))
class REALMFORGE_API URFNetworkManager : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ─── Session Lifecycle ────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Network")
    void HostSession(const FRFSessionInfo& SessionConfig, const FString& MapName);

    UFUNCTION(BlueprintCallable, Category = "RF|Network")
    void FindSessions(bool bIsLAN = false, int32 MaxResults = 20);

    UFUNCTION(BlueprintCallable, Category = "RF|Network")
    void JoinSession(int32 SearchResultIndex);

    UFUNCTION(BlueprintCallable, Category = "RF|Network")
    void DestroySession();

    UFUNCTION(BlueprintCallable, Category = "RF|Network")
    void RequestReconnect(const FString& SessionName);

    // ─── Player Management ────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Network")
    void AssignPlayerRole(const FUniqueNetIdRepl& PlayerId, ERFPlayerRole NewRole);

    UFUNCTION(BlueprintCallable, Category = "RF|Network")
    void KickPlayer(const FUniqueNetIdRepl& PlayerId, const FString& Reason);

    UFUNCTION(BlueprintPure, Category = "RF|Network")
    TArray<FRFPlayerInfo> GetConnectedPlayers() const { return ConnectedPlayers; }

    UFUNCTION(BlueprintPure, Category = "RF|Network")
    bool IsGameMaster() const;

    UFUNCTION(BlueprintPure, Category = "RF|Network")
    ERFPlayerRole GetLocalPlayerRole() const { return LocalPlayerRole; }

    // ─── Delegates ───────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable, Category = "RF|Network|Events")
    FOnSessionCreated OnSessionCreated;

    UPROPERTY(BlueprintAssignable, Category = "RF|Network|Events")
    FOnSessionJoined OnSessionJoined;

    UPROPERTY(BlueprintAssignable, Category = "RF|Network|Events")
    FOnSessionDestroyed OnSessionDestroyed;

    UPROPERTY(BlueprintAssignable, Category = "RF|Network|Events")
    FOnPlayerJoined OnPlayerJoined;

    UPROPERTY(BlueprintAssignable, Category = "RF|Network|Events")
    FOnPlayerLeft OnPlayerLeft;

    UPROPERTY(BlueprintAssignable, Category = "RF|Network|Events")
    FOnSearchComplete OnSearchComplete;

protected:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // OSS Callbacks
    void OnCreateSessionComplete(FName SessionName, bool bSuccess);
    void OnFindSessionsComplete(bool bSuccess);
    void OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
    void OnDestroySessionComplete(FName SessionName, bool bSuccess);
    void OnNetworkFailure(UWorld* World, UNetDriver* Driver, ENetworkFailure::Type FailureType, const FString& Msg);

private:
    IOnlineSessionPtr SessionInterface;
    TSharedPtr<FOnlineSessionSearch> SessionSearch;

    TArray<FRFPlayerInfo> ConnectedPlayers;
    ERFPlayerRole LocalPlayerRole = ERFPlayerRole::Player;

    FRFSessionInfo ActiveSession;

    static const FName SESSION_NAME;
};
