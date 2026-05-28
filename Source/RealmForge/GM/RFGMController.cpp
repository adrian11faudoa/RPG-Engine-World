#include "RFGMController.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Misc/Guid.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SpectatorPawn.h"
#include "Camera/CameraComponent.h"

ARFGMController::ARFGMController()
{
    bReplicates = true;
}

void ARFGMController::BeginPlay()
{
    Super::BeginPlay();

    for (TActorIterator<ARFInitiativeTracker> It(GetWorld()); It; ++It)
    {
        InitiativeTracker = *It;
        break;
    }

    if (IsLocalController())
    {
        UE_LOG(LogTemp, Log, TEXT("[RF|GM] GM Controller ready"));
    }
}

void ARFGMController::SetupInputComponent()
{
    Super::SetupInputComponent();
    if (!InputComponent) return;

    InputComponent->BindAction("GM_ToggleFog",   IE_Pressed, this, &ARFGMController::OnHotkeyToggleFog);
    InputComponent->BindAction("GM_NextTurn",    IE_Pressed, this, &ARFGMController::OnHotkeyNextTurn);
    InputComponent->BindAction("GM_ToggleView",  IE_Pressed, this, &ARFGMController::OnHotkeyToggleGMView);
}

// ─── GM Mode ─────────────────────────────────────────────────────────────────

void ARFGMController::SetGMMode(bool bEnabled)
{
    bIsGMMode = bEnabled;

    if (bEnabled)
    {
        // Switch to top-down / GM camera
        SetCinematicMode(false);
        UE_LOG(LogTemp, Log, TEXT("[RF|GM] GM mode ENABLED"));
    }
}

// ─── Visibility ──────────────────────────────────────────────────────────────

void ARFGMController::SetObjectHidden(AActor* Object, bool bHidden)
{
    if (!Object || !HasAuthority()) return;

    Object->SetActorHiddenInGame(bHidden);

    // If it's a miniature, update its visibility flag
    if (ARFMiniatureBase* Mini = Cast<ARFMiniatureBase>(Object))
    {
        Mini->bIsVisibleToPlayers = !bHidden;
    }
}

void ARFGMController::RevealObjectToPlayers(AActor* Object)
{
    SetObjectHidden(Object, false);
}

void ARFGMController::SetLayerVisible(const FString& LayerName, bool bVisible)
{
    // Iterate tagged actors
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        AActor* Actor = *It;
        if (Actor->Tags.Contains(FName(*LayerName)))
        {
            Actor->SetActorHiddenInGame(!bVisible);
        }
    }
}

void ARFGMController::ToggleFogOfWarForAll()
{
    // Broadcasts to all FogOfWar components
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        URFFogOfWar* Fog = (*It)->FindComponentByClass<URFFogOfWar>();
        if (Fog)
        {
            // Toggle reveal all / restore fog
            static bool bAllRevealed = false;
            bAllRevealed = !bAllRevealed;
            bAllRevealed ? Fog->RevealAllForGM() : Fog->HideAll();
        }
    }
}

// ─── Secret Notes ────────────────────────────────────────────────────────────

FString ARFGMController::AddSecretNote(const FString& Title, const FString& Body,
    FVector WorldLocation)
{
    FRFSecretNote Note;
    Note.NoteID    = GenerateNoteID();
    Note.Title     = Title;
    Note.Body      = Body;
    Note.CreatedAt = FDateTime::Now();
    Note.WorldLocation = WorldLocation;
    Note.bIsAttachedToLocation = !WorldLocation.IsZero();

    SecretNotes.Add(Note);
    OnGMNoteAdded.Broadcast(Note);

    UE_LOG(LogTemp, Log, TEXT("[RF|GM] Note added: '%s'"), *Title);
    return Note.NoteID;
}

void ARFGMController::EditNote(const FString& NoteID, const FString& NewBody)
{
    for (FRFSecretNote& Note : SecretNotes)
    {
        if (Note.NoteID == NoteID)
        {
            Note.Body = NewBody;
            return;
        }
    }
}

void ARFGMController::DeleteNote(const FString& NoteID)
{
    SecretNotes.RemoveAll([&](const FRFSecretNote& N){ return N.NoteID == NoteID; });
}

TArray<FRFSecretNote> ARFGMController::GetNotesNearLocation(FVector Location, float Radius) const
{
    TArray<FRFSecretNote> Result;
    for (const FRFSecretNote& Note : SecretNotes)
    {
        if (Note.bIsAttachedToLocation &&
            FVector::Dist(Note.WorldLocation, Location) <= Radius)
        {
            Result.Add(Note);
        }
    }
    return Result;
}

// ─── Monster Spawning ────────────────────────────────────────────────────────

ARFMiniatureBase* ARFGMController::SpawnMonster(TSubclassOf<ARFMiniatureBase> MonsterClass,
    FVector Location, const FString& CustomName)
{
    if (!HasAuthority())
    {
        Server_SpawnMonster(MonsterClass, Location, CustomName);
        return nullptr;
    }

    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    ARFMiniatureBase* Spawned = GetWorld()->SpawnActor<ARFMiniatureBase>(
        MonsterClass, Location, FRotator::ZeroRotator, Params);

    if (Spawned)
    {
        if (!CustomName.IsEmpty()) Spawned->CharacterName = CustomName;
        Spawned->MiniType = ERFMiniatureType::Monster;

        // Auto-add to initiative if combat is active
        if (InitiativeTracker && InitiativeTracker->IsInCombat())
        {
            InitiativeTracker->AddToInitiative(Spawned, 0);
        }

        UE_LOG(LogTemp, Log, TEXT("[RF|GM] Spawned: %s at %s"),
            *Spawned->CharacterName, *Location.ToString());
    }

    return Spawned;
}

void ARFGMController::SpawnEncounter(const FRFEncounterTemplate& Encounter)
{
    if (!HasAuthority()) return;

    UE_LOG(LogTemp, Log, TEXT("[RF|GM] Spawning encounter: %s (%d monster types)"),
        *Encounter.Name, Encounter.MonsterIDs.Num());

    for (int32 i = 0; i < Encounter.MonsterIDs.Num(); i++)
    {
        int32 Count = Encounter.MonsterCounts.IsValidIndex(i) ? Encounter.MonsterCounts[i] : 1;

        for (int32 j = 0; j < Count; j++)
        {
            FVector SpawnLoc = Encounter.SpawnLocations.IsValidIndex(j)
                ? Encounter.SpawnLocations[j]
                : GetPawn()->GetActorLocation() + FVector(j * 150.0f, 0, 0);

            // Asset lookup by ID handled by AssetRegistry
            // SpawnMonster(ResolveMonsterClass(Encounter.MonsterIDs[i]), SpawnLoc);
        }
    }
}

void ARFGMController::DespawnAllMonsters()
{
    if (!HasAuthority()) return;

    TArray<AActor*> ToDestroy;
    for (TActorIterator<ARFMiniatureBase> It(GetWorld()); It; ++It)
    {
        if ((*It)->MiniType == ERFMiniatureType::Monster)
            ToDestroy.Add(*It);
    }
    for (AActor* Actor : ToDestroy) Actor->Destroy();
}

void ARFGMController::DespawnMiniature(ARFMiniatureBase* Mini)
{
    if (!HasAuthority() || !Mini) return;

    if (InitiativeTracker)
        InitiativeTracker->RemoveFromInitiative(Mini->CharacterName);

    Mini->Destroy();
}

// ─── Scene Management ────────────────────────────────────────────────────────

void ARFGMController::SwitchScene(const FString& MapName, ERFSceneTransition Transition)
{
    if (!IsLocalController())
    {
        Server_SwitchScene(MapName, Transition);
        return;
    }
    Server_SwitchScene(MapName, Transition);
}

void ARFGMController::SetCinematicMode(bool bEnabled)
{
    SetCinematicMode(bEnabled, true, true, true, true);
    UE_LOG(LogTemp, Log, TEXT("[RF|GM] Cinematic mode: %s"), bEnabled ? TEXT("ON") : TEXT("OFF"));
}

void ARFGMController::SetSpectatorMode(bool bEnabled)
{
    if (bEnabled)
    {
        ASpectatorPawn* Spectator = GetWorld()->SpawnActor<ASpectatorPawn>(
            GetActorLocation(), GetControlRotation());
        if (Spectator) Possess(Spectator);
    }
}

// ─── Camera Control ──────────────────────────────────────────────────────────

void ARFGMController::SaveCameraBookmark(const FString& BookmarkName)
{
    FRFCameraBookmark Mark;
    Mark.BookmarkName = BookmarkName;

    if (APawn* P = GetPawn())
    {
        Mark.Location = P->GetActorLocation();
        Mark.Rotation = GetControlRotation();
    }
    Mark.FOV = PlayerCameraManager ? PlayerCameraManager->GetFOVAngle() : 70.0f;

    // Replace if exists
    CameraBookmarks.RemoveAll([&](const FRFCameraBookmark& B){ return B.BookmarkName == BookmarkName; });
    CameraBookmarks.Add(Mark);

    UE_LOG(LogTemp, Log, TEXT("[RF|GM] Camera bookmark saved: '%s'"), *BookmarkName);
}

void ARFGMController::GoToCameraBookmark(const FString& BookmarkName)
{
    for (const FRFCameraBookmark& Mark : CameraBookmarks)
    {
        if (Mark.BookmarkName == BookmarkName)
        {
            FocusOnLocation(Mark.Location, 1.0f);
            SetControlRotation(Mark.Rotation);
            return;
        }
    }
}

void ARFGMController::FocusOnMiniature(ARFMiniatureBase* Mini, float Duration)
{
    if (Mini) FocusOnLocation(Mini->GetActorLocation(), Duration);
}

void ARFGMController::FocusOnLocation(FVector Location, float Duration)
{
    // Smooth camera interpolation — implemented in Blueprint camera pawn
    UE_LOG(LogTemp, Verbose, TEXT("[RF|GM] Camera moving to: %s"), *Location.ToString());
}

// ─── Player Permissions ──────────────────────────────────────────────────────

void ARFGMController::SetPlayerCanMoveMiniature(const FString& PlayerName, bool bCanMove)
{
    PlayerMovePermissions.Add(PlayerName, bCanMove);
}

void ARFGMController::SetPlayerCanEditMap(const FString& PlayerName, bool bCanEdit)
{
    PlayerEditPermissions.Add(PlayerName, bCanEdit);
}

void ARFGMController::PingLocation(FVector WorldLocation, const FString& Message, FLinearColor Color)
{
    if (HasAuthority())
        Multicast_PingLocation(WorldLocation, Message, Color);
    else
        ServerRPCWithParams(WorldLocation, Message, Color);  // Implement as needed
}

// ─── Hotkeys ─────────────────────────────────────────────────────────────────

void ARFGMController::OnHotkeyToggleFog()   { ToggleFogOfWarForAll(); }
void ARFGMController::OnHotkeyNextTurn()    { if (InitiativeTracker) InitiativeTracker->NextTurn(); }
void ARFGMController::OnHotkeyToggleGMView(){ SetGMMode(!bIsGMMode); }

// ─── RPCs ────────────────────────────────────────────────────────────────────

bool ARFGMController::Server_SwitchScene_Validate(const FString&, ERFSceneTransition)
{
    return bIsGMMode;  // Only GM can switch scenes
}

void ARFGMController::Server_SwitchScene_Implementation(const FString& MapName,
    ERFSceneTransition Transition)
{
    OnSceneSwitched.Broadcast(MapName, Transition);
    Client_BeginSceneTransition(Transition);

    FTimerHandle TransitionTimer;
    float Delay = (Transition == ERFSceneTransition::Cut) ? 0.0f : 1.5f;

    GetWorldTimerManager().SetTimer(TransitionTimer, [this, MapName]()
    {
        GetWorld()->ServerTravel(FString::Printf(TEXT("/Game/Maps/%s"), *MapName));
    }, Delay, false);
}

bool ARFGMController::Server_SpawnMonster_Validate(TSubclassOf<ARFMiniatureBase>,
    FVector, const FString&)
{
    return bIsGMMode;
}

void ARFGMController::Server_SpawnMonster_Implementation(TSubclassOf<ARFMiniatureBase> Class,
    FVector Location, const FString& CustomName)
{
    SpawnMonster(Class, Location, CustomName);
}

void ARFGMController::Multicast_PingLocation_Implementation(FVector WorldLocation,
    const FString& Message, FLinearColor Color)
{
    // Show ping indicator in world space via HUD
    UE_LOG(LogTemp, Log, TEXT("[RF|GM] Ping at %s: %s"), *WorldLocation.ToString(), *Message);
}

void ARFGMController::Client_BeginSceneTransition_Implementation(ERFSceneTransition Transition)
{
    // Trigger transition widget on client HUD
}

void ARFGMController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ARFGMController, bIsGMMode);
}

FString ARFGMController::GenerateNoteID() const
{
    return FGuid::NewGuid().ToString(EGuidFormats::Short);
}
