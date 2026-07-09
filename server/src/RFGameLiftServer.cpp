#include "RFGameLiftServer.h"
#include "GameLiftServerSDK.h"
#include "Misc/CommandLine.h"

void URFGameLiftServer::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Only run on dedicated server builds
    if (!IsRunningDedicatedServer()) return;

    // Parse port from command line: -port=7777
    FString PortStr;
    if (FParse::Value(FCommandLine::Get(), TEXT("port="), PortStr))
        ServerPort = FCString::Atoi(*PortStr);

    InitGameLiftSDK();
}

void URFGameLiftServer::Deinitialize()
{
    if (bGameLiftActive)
    {
        Aws::GameLift::Server::ProcessEnding();
        Aws::GameLift::Server::Destroy();
    }
    Super::Deinitialize();
}

void URFGameLiftServer::InitGameLiftSDK()
{
    // Initialize SDK
    auto InitOutcome = Aws::GameLift::Server::InitSDK();
    if (!InitOutcome.IsSuccess())
    {
        UE_LOG(LogTemp, Error, TEXT("[GameLift] SDK init failed: %s"),
            UTF8_TO_TCHAR(InitOutcome.GetError().GetErrorMessage().c_str()));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[GameLift] SDK initialized on port %d"), ServerPort);
    bGameLiftActive = true;

    RegisterServerProcess();
}

void URFGameLiftServer::RegisterServerProcess()
{
    // Log file path for GameLift to collect
    FString LogPath = FPaths::ProjectLogDir() / TEXT("RealmForgeServer.log");

    auto ProcessParams = Aws::GameLift::Server::ProcessParameters(
        // OnStartGameSession
        std::bind(&URFGameLiftServer::OnStartGameSession, this, std::placeholders::_1),
        // OnUpdateGameSession
        std::bind(&URFGameLiftServer::OnUpdateGameSession, this, std::placeholders::_1),
        // OnProcessTerminate
        std::bind(&URFGameLiftServer::OnProcessTerminate, this),
        // HealthCheck
        std::bind(&URFGameLiftServer::OnHealthCheck, this),
        // Port
        ServerPort,
        // Log paths
        Aws::GameLift::Server::LogParameters({ TCHAR_TO_UTF8(*LogPath) })
    );

    auto ReadyOutcome = Aws::GameLift::Server::ProcessReady(ProcessParams);
    if (!ReadyOutcome.IsSuccess())
    {
        UE_LOG(LogTemp, Error, TEXT("[GameLift] ProcessReady failed: %s"),
            UTF8_TO_TCHAR(ReadyOutcome.GetError().GetErrorMessage().c_str()));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[GameLift] ProcessReady — waiting for game session"));
}

// ─── SDK Callbacks ────────────────────────────────────────────

void URFGameLiftServer::OnStartGameSession(
    Aws::GameLift::Server::Model::GameSession GameSessionObj)
{
    FString SessionId = UTF8_TO_TCHAR(GameSessionObj.GetGameSessionId().c_str());
    ActiveGameSessionId = SessionId;

    UE_LOG(LogTemp, Log, TEXT("[GameLift] StartGameSession: %s"), *SessionId);

    // Extract campaign ID from game properties
    for (const auto& Prop : GameSessionObj.GetGameProperties())
    {
        FString Key   = UTF8_TO_TCHAR(Prop.GetKey().c_str());
        FString Value = UTF8_TO_TCHAR(Prop.GetValue().c_str());
        UE_LOG(LogTemp, Log, TEXT("[GameLift] Property: %s = %s"), *Key, *Value);
    }

    // Activate the session
    Aws::GameLift::Server::ActivateGameSession();

    // Notify Blueprint/C++ systems
    if (IsInGameThread())
    {
        OnGameSessionStarted.Broadcast(SessionId);
    }
    else
    {
        AsyncTask(ENamedThreads::GameThread, [this, SessionId]()
        {
            OnGameSessionStarted.Broadcast(SessionId);
        });
    }
}

void URFGameLiftServer::OnUpdateGameSession(
    Aws::GameLift::Server::Model::UpdateGameSession UpdatedSession)
{
    UE_LOG(LogTemp, Log, TEXT("[GameLift] UpdateGameSession received"));
}

void URFGameLiftServer::OnProcessTerminate()
{
    UE_LOG(LogTemp, Log, TEXT("[GameLift] ProcessTerminate — shutting down"));

    AsyncTask(ENamedThreads::GameThread, [this]()
    {
        OnGameSessionEnded.Broadcast();

        // Give sessions 5 seconds to save then exit
        FTimerHandle ShutdownTimer;
        GetWorld()->GetTimerManager().SetTimer(ShutdownTimer, []()
        {
            Aws::GameLift::Server::ProcessEnding();
            FGenericPlatformMisc::RequestExit(false);
        }, 5.0f, false);
    });
}

bool URFGameLiftServer::OnHealthCheck()
{
    // Basic health: game thread responsive, no critical errors
    return bGameLiftActive;
}

// ─── Player Session Management ──────────────────────────────

bool URFGameLiftServer::AcceptPlayerSession(const FString& PlayerSessionId)
{
    auto Outcome = Aws::GameLift::Server::AcceptPlayerSession(
        TCHAR_TO_UTF8(*PlayerSessionId));

    if (Outcome.IsSuccess())
    {
        UE_LOG(LogTemp, Log, TEXT("[GameLift] Player session accepted: %s"), *PlayerSessionId);
        OnPlayerAccepted.Broadcast(PlayerSessionId);
        return true;
    }

    UE_LOG(LogTemp, Warning, TEXT("[GameLift] AcceptPlayerSession failed: %s"),
        UTF8_TO_TCHAR(Outcome.GetError().GetErrorMessage().c_str()));
    return false;
}

void URFGameLiftServer::RemovePlayerSession(const FString& PlayerSessionId)
{
    Aws::GameLift::Server::RemovePlayerSession(TCHAR_TO_UTF8(*PlayerSessionId));
    UE_LOG(LogTemp, Log, TEXT("[GameLift] Player session removed: %s"), *PlayerSessionId);
}

void URFGameLiftServer::TerminateGameSession()
{
    if (!bGameLiftActive) return;

    UE_LOG(LogTemp, Log, TEXT("[GameLift] Terminating game session: %s"), *ActiveGameSessionId);
    Aws::GameLift::Server::TerminateGameSession();
    ActiveGameSessionId.Empty();

    OnGameSessionEnded.Broadcast();
}
