#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RFMiniatureBase.h"
#include "RFInitiativeTracker.h"
#include "RFGMController.generated.h"

UENUM(BlueprintType)
enum class ERFSceneTransition : uint8
{
    Cut         UMETA(DisplayName = "Cut"),
    Fade        UMETA(DisplayName = "Fade to Black"),
    Wipe        UMETA(DisplayName = "Wipe"),
    Cinematic   UMETA(DisplayName = "Cinematic Sequence")
};

USTRUCT(BlueprintType)
struct FRFSecretNote
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString NoteID;
    UPROPERTY(BlueprintReadWrite) FString Title;
    UPROPERTY(BlueprintReadWrite) FString Body;
    UPROPERTY(BlueprintReadWrite) FVector WorldLocation = FVector::ZeroVector;
    UPROPERTY(BlueprintReadWrite) bool bIsAttachedToLocation = false;
    UPROPERTY(BlueprintReadWrite) FDateTime CreatedAt;
};

USTRUCT(BlueprintType)
struct FRFEncounterTemplate
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString EncounterID;
    UPROPERTY(BlueprintReadWrite) FString Name;
    UPROPERTY(BlueprintReadWrite) FString Description;
    UPROPERTY(BlueprintReadWrite) int32 CRRating = 1;
    UPROPERTY(BlueprintReadWrite) TArray<FString> MonsterIDs;    // Asset IDs
    UPROPERTY(BlueprintReadWrite) TArray<int32> MonsterCounts;
    UPROPERTY(BlueprintReadWrite) TArray<FVector> SpawnLocations;
    UPROPERTY(BlueprintReadWrite) FString Terrain;
    UPROPERTY(BlueprintReadWrite) FString SpecialConditions;
};

USTRUCT(BlueprintType)
struct FRFCameraBookmark
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString BookmarkName;
    UPROPERTY(BlueprintReadWrite) FVector Location;
    UPROPERTY(BlueprintReadWrite) FRotator Rotation;
    UPROPERTY(BlueprintReadWrite) float FOV = 70.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSceneSwitched,
    const FString&, NewMapName, ERFSceneTransition, Transition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGMNoteAdded, const FRFSecretNote&, Note);

UCLASS()
class REALMFORGE_API ARFGMController : public APlayerController
{
    GENERATED_BODY()

public:
    ARFGMController();

    // ─── GM Mode ──────────────────────────────────────────────────────
    UPROPERTY(BlueprintReadOnly, Replicated)
    bool bIsGMMode = false;

    UFUNCTION(BlueprintCallable, Category = "RF|GM")
    void SetGMMode(bool bEnabled);

    // ─── Hidden Objects & Layers ──────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|GM|Visibility")
    void SetObjectHidden(AActor* Object, bool bHidden);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Visibility")
    void RevealObjectToPlayers(AActor* Object);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Visibility")
    void SetLayerVisible(const FString& LayerName, bool bVisible);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Visibility")
    void ToggleFogOfWarForAll();

    // ─── Secret Notes ─────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|GM|Notes")
    FString AddSecretNote(const FString& Title, const FString& Body,
        FVector WorldLocation = FVector::ZeroVector);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Notes")
    void EditNote(const FString& NoteID, const FString& NewBody);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Notes")
    void DeleteNote(const FString& NoteID);

    UFUNCTION(BlueprintPure, Category = "RF|GM|Notes")
    TArray<FRFSecretNote> GetAllNotes() const { return SecretNotes; }

    UFUNCTION(BlueprintPure, Category = "RF|GM|Notes")
    TArray<FRFSecretNote> GetNotesNearLocation(FVector Location, float Radius) const;

    // ─── Monster Spawning ─────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|GM|Spawning")
    ARFMiniatureBase* SpawnMonster(TSubclassOf<ARFMiniatureBase> MonsterClass,
        FVector Location, const FString& CustomName = TEXT(""));

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Spawning")
    void SpawnEncounter(const FRFEncounterTemplate& Encounter);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Spawning")
    void DespawnAllMonsters();

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Spawning")
    void DespawnMiniature(ARFMiniatureBase* Mini);

    // ─── Encounter Builder ────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|GM|Encounters")
    void SaveEncounterTemplate(const FRFEncounterTemplate& Template);

    UFUNCTION(BlueprintPure, Category = "RF|GM|Encounters")
    TArray<FRFEncounterTemplate> GetEncounterTemplates() const { return SavedEncounters; }

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Encounters")
    int32 CalculateCR(const TArray<FString>& MonsterIDs) const;

    // ─── Scene Management ─────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|GM|Scene")
    void SwitchScene(const FString& MapName,
        ERFSceneTransition Transition = ERFSceneTransition::Fade);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Scene")
    void SetCinematicMode(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Scene")
    void SetSpectatorMode(bool bEnabled);

    // ─── Camera Control ───────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|GM|Camera")
    void SaveCameraBookmark(const FString& BookmarkName);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Camera")
    void GoToCameraBookmark(const FString& BookmarkName);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Camera")
    void FocusOnMiniature(ARFMiniatureBase* Mini, float Duration = 1.5f);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Camera")
    void FocusOnLocation(FVector Location, float Duration = 1.0f);

    // ─── Player Permissions ───────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|GM|Permissions")
    void SetPlayerCanMoveMiniature(const FString& PlayerName, bool bCanMove);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Permissions")
    void SetPlayerCanEditMap(const FString& PlayerName, bool bCanEdit);

    UFUNCTION(BlueprintCallable, Category = "RF|GM|Permissions")
    void PingLocation(FVector WorldLocation, const FString& Message, FLinearColor Color);

    // ─── Events ───────────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnSceneSwitched OnSceneSwitched;
    UPROPERTY(BlueprintAssignable) FOnGMNoteAdded   OnGMNoteAdded;

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // GM hotkeys
    UFUNCTION() void OnHotkeyToggleFog();
    UFUNCTION() void OnHotkeyNextTurn();
    UFUNCTION() void OnHotkeyToggleGMView();

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_SwitchScene(const FString& MapName, ERFSceneTransition Transition);

    UFUNCTION(Server, Reliable, WithValidation)
    void Server_SpawnMonster(TSubclassOf<ARFMiniatureBase> Class, FVector Location,
        const FString& CustomName);

    UFUNCTION(NetMulticast, Reliable)
    void Multicast_PingLocation(FVector WorldLocation, const FString& Message,
        FLinearColor Color);

    UFUNCTION(Client, Reliable)
    void Client_BeginSceneTransition(ERFSceneTransition Transition);

private:
    TArray<FRFSecretNote>       SecretNotes;
    TArray<FRFEncounterTemplate> SavedEncounters;
    TArray<FRFCameraBookmark>   CameraBookmarks;

    UPROPERTY()
    ARFInitiativeTracker* InitiativeTracker = nullptr;

    TMap<FString, bool>         PlayerMovePermissions;
    TMap<FString, bool>         PlayerEditPermissions;

    FString GenerateNoteID() const;
};
