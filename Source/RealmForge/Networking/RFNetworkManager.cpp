#include "RFNetworkManager.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"

const FName URFNetworkManager::SESSION_NAME = FName(TEXT("RealmForgeSession"));

void URFNetworkManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
    if (OSS)
    {
        SessionInterface = OSS->GetSessionInterface();
        if (SessionInterface.IsValid())
        {
            SessionInterface->OnCreateSessionCompleteDelegates.AddUObject(
                this, &URFNetworkManager::OnCreateSessionComplete);
            SessionInterface->OnFindSessionsCompleteDelegates.AddUObject(
                this, &URFNetworkManager::OnFindSessionsComplete);
            SessionInterface->OnJoinSessionCompleteDelegates.AddUObject(
                this, &URFNetworkManager::OnJoinSessionComplete);
            SessionInterface->OnDestroySessionCompleteDelegates.AddUObject(
                this, &URFNetworkManager::OnDestroySessionComplete);
        }
    }

    if (GEngine)
    {
        GEngine->OnNetworkFailure().AddUObject(this, &URFNetworkManager::OnNetworkFailure);
    }
}

void URFNetworkManager::HostSession(const FRFSessionInfo& SessionConfig, const FString& MapName)
{
    if (!SessionInterface.IsValid()) return;

    ActiveSession = SessionConfig;
    LocalPlayerRole = ERFPlayerRole::GameMaster;

    FOnlineSessionSettings Settings;
    Settings.NumPublicConnections = SessionConfig.MaxPlayers;
    Settings.NumPrivateConnections = 0;
    Settings.bIsLANMatch = SessionConfig.bIsLAN;
    Settings.bUsesPresence = !SessionConfig.bIsLAN;
    Settings.bAllowJoinInProgress = true;
    Settings.bAllowInvites = true;
    Settings.bShouldAdvertise = true;
    Settings.bUseLobbiesIfAvailable = true;

    // Store campaign metadata in session settings
    Settings.Set(FName("CAMPAIGN_NAME"),
        SessionConfig.CampaignName, EOnlineDataAdvertisementType::ViaOnlineService);
    Settings.Set(FName("GM_NAME"),
        SessionConfig.GMName, EOnlineDataAdvertisementType::ViaOnlineService);
    Settings.Set(FName("MAP_NAME"),
        MapName, EOnlineDataAdvertisementType::ViaOnlineService);

    SessionInterface->CreateSession(0, SESSION_NAME, Settings);

    UE_LOG(LogTemp, Log, TEXT("[RF] Hosting session: %s | Map: %s | MaxPlayers: %d"),
        *SessionConfig.SessionName, *MapName, SessionConfig.MaxPlayers);
}

void URFNetworkManager::FindSessions(bool bIsLAN, int32 MaxResults)
{
    if (!SessionInterface.IsValid()) return;

    SessionSearch = MakeShareable(new FOnlineSessionSearch());
    SessionSearch->MaxSearchResults = MaxResults;
    SessionSearch->bIsLanQuery = bIsLAN;
    SessionSearch->QuerySettings.Set(SEARCH_PRESENCE, true, EOnlineComparisonOp::Equals);

    SessionInterface->FindSessions(0, SessionSearch.ToSharedRef());

    UE_LOG(LogTemp, Log, TEXT("[RF] Searching for sessions (LAN: %s)"), bIsLAN ? TEXT("Yes") : TEXT("No"));
}

void URFNetworkManager::JoinSession(int32 SearchResultIndex)
{
    if (!SessionInterface.IsValid() || !SessionSearch.IsValid()) return;
    if (!SessionSearch->SearchResults.IsValidIndex(SearchResultIndex)) return;

    LocalPlayerRole = ERFPlayerRole::Player;
    SessionInterface->JoinSession(0, SESSION_NAME, SessionSearch->SearchResults[SearchResultIndex]);
}

void URFNetworkManager::DestroySession()
{
    if (!SessionInterface.IsValid()) return;
    SessionInterface->DestroySession(SESSION_NAME);
}

void URFNetworkManager::RequestReconnect(const FString& SessionName)
{
    // Re-search and attempt to rejoin a specific session by name
    FindSessions(false, 50);
    UE_LOG(LogTemp, Log, TEXT("[RF] Attempting reconnect to: %s"), *SessionName);
}

void URFNetworkManager::AssignPlayerRole(const FUniqueNetIdRepl& PlayerId, ERFPlayerRole NewRole)
{
    if (!IsGameMaster()) return;

    for (FRFPlayerInfo& Player : ConnectedPlayers)
    {
        if (Player.NetId == PlayerId)
        {
            Player.Role = NewRole;
            UE_LOG(LogTemp, Log, TEXT("[RF] Assigned role %d to %s"),
                (int32)NewRole, *Player.PlayerName);
            break;
        }
    }
}

void URFNetworkManager::KickPlayer(const FUniqueNetIdRepl& PlayerId, const FString& Reason)
{
    if (!IsGameMaster()) return;
    // Implementation calls APlayerController::ClientWasKicked on server
    UE_LOG(LogTemp, Log, TEXT("[RF] Kicking player. Reason: %s"), *Reason);
}

bool URFNetworkManager::IsGameMaster() const
{
    return LocalPlayerRole == ERFPlayerRole::GameMaster ||
           LocalPlayerRole == ERFPlayerRole::AssistantGM;
}

// ─── OSS Callbacks ──────────────────────────────────────────────────────────

void URFNetworkManager::OnCreateSessionComplete(FName SessionName, bool bSuccess)
{
    UE_LOG(LogTemp, Log, TEXT("[RF] Session created: %s (Success: %s)"),
        *SessionName.ToString(), bSuccess ? TEXT("Yes") : TEXT("No"));

    OnSessionCreated.Broadcast(bSuccess);

    if (bSuccess)
    {
        UWorld* World = GetWorld();
        if (World)
        {
            FString TravelURL = FString::Printf(TEXT("/Game/Maps/%s?listen"), *ActiveSession.CampaignName);
            World->ServerTravel(TravelURL);
        }
    }
}

void URFNetworkManager::OnFindSessionsComplete(bool bSuccess)
{
    TArray<FRFSessionInfo> Results;

    if (bSuccess && SessionSearch.IsValid())
    {
        for (const FOnlineSessionSearchResult& Result : SessionSearch->SearchResults)
        {
            FRFSessionInfo Info;
            Result.Session.SessionSettings.Get(FName("CAMPAIGN_NAME"), Info.CampaignName);
            Result.Session.SessionSettings.Get(FName("GM_NAME"), Info.GMName);
            Info.MaxPlayers = Result.Session.SessionSettings.NumPublicConnections;
            Info.CurrentPlayers = Info.MaxPlayers - Result.Session.NumOpenPublicConnections;
            Info.bIsLAN = Result.Session.SessionSettings.bIsLANMatch;
            Results.Add(Info);
        }
    }

    OnSearchComplete.Broadcast(Results, bSuccess);
}

void URFNetworkManager::OnJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
    bool bSuccess = (Result == EOnJoinSessionCompleteResult::Success);
    OnSessionJoined.Broadcast(bSuccess);

    if (bSuccess && SessionInterface.IsValid())
    {
        FString ConnectInfo;
        SessionInterface->GetResolvedConnectString(SESSION_NAME, ConnectInfo);

        APlayerController* PC = GetGameInstance()->GetFirstLocalPlayerController();
        if (PC)
        {
            PC->ClientTravel(ConnectInfo, TRAVEL_Absolute);
        }
    }
}

void URFNetworkManager::OnDestroySessionComplete(FName SessionName, bool bSuccess)
{
    OnSessionDestroyed.Broadcast();
}

void URFNetworkManager::OnNetworkFailure(UWorld* World, UNetDriver* Driver,
    ENetworkFailure::Type FailureType, const FString& Msg)
{
    UE_LOG(LogTemp, Warning, TEXT("[RF] Network failure: %s — %s"),
        *UEnum::GetValueAsString(FailureType), *Msg);

    // Auto-attempt reconnect on timeout
    if (FailureType == ENetworkFailure::ConnectionTimeout ||
        FailureType == ENetworkFailure::ConnectionLost)
    {
        RequestReconnect(ActiveSession.SessionName);
    }
}
