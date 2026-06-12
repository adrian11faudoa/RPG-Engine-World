#pragma once
/**
 * RFGameLiftServer.h
 * Integrates AWS GameLift Server SDK into the Unreal Engine dedicated server.
 * Handles: process ready, health check, game session start/end, player sessions.
 *
 * Requires: GameLift Server SDK 5.x added to Plugins/GameLiftServerSDK/
 */

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameLiftServerSDK.h"
#include "RFGameLiftServer.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGameSessionStarted, const FString&, SessionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGameSessionEnded);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerAccepted, const FString&, PlayerId);

UCLASS()
class REALMFORGE_API URFGameLiftServer : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ─── Session Management ───────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|GameLift")
    bool AcceptPlayerSession(const FString& PlayerSessionId);

    UFUNCTION(BlueprintCallable, Category = "RF|GameLift")
    void RemovePlayerSession(const FString& PlayerSessionId);

    UFUNCTION(BlueprintCallable, Category = "RF|GameLift")
    void TerminateGameSession();

    UFUNCTION(BlueprintPure, Category = "RF|GameLift")
    FString GetGameSessionId() const { return ActiveGameSessionId; }

    UFUNCTION(BlueprintPure, Category = "RF|GameLift")
    bool IsGameLiftActive() const { return bGameLiftActive; }

    // ─── Events ───────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnGameSessionStarted OnGameSessionStarted;
    UPROPERTY(BlueprintAssignable) FOnGameSessionEnded   OnGameSessionEnded;
    UPROPERTY(BlueprintAssignable) FOnPlayerAccepted     OnPlayerAccepted;

private:
    bool bGameLiftActive = false;
    FString ActiveGameSessionId;
    int32 ServerPort = 7777;

    // GameLift SDK callbacks
    void OnStartGameSession(Aws::GameLift::Server::Model::GameSession GameSessionObj);
    void OnUpdateGameSession(Aws::GameLift::Server::Model::UpdateGameSession UpdatedSession);
    void OnProcessTerminate();
    bool OnHealthCheck();

    // Helpers
    void InitGameLiftSDK();
    void RegisterServerProcess();
};
