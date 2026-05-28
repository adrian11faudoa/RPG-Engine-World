#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RFModLoader.generated.h"

// Forward declaration for Lua state
struct lua_State;

USTRUCT(BlueprintType)
struct FRFModManifest
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString ModID;
    UPROPERTY(BlueprintReadWrite) FString DisplayName;
    UPROPERTY(BlueprintReadWrite) FString Version;
    UPROPERTY(BlueprintReadWrite) FString Author;
    UPROPERTY(BlueprintReadWrite) FString Description;
    UPROPERTY(BlueprintReadWrite) FString EntryScript;        // e.g. "main.lua"
    UPROPERTY(BlueprintReadWrite) TArray<FString> Dependencies;
    UPROPERTY(BlueprintReadWrite) bool bIsEnabled = true;
    UPROPERTY(BlueprintReadWrite) FString FolderPath;
};

USTRUCT(BlueprintType)
struct FRFModAsset
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite) FString AssetID;
    UPROPERTY(BlueprintReadWrite) FString DisplayName;
    UPROPERTY(BlueprintReadWrite) FString Category;           // "creature", "prop", "spell", etc.
    UPROPERTY(BlueprintReadWrite) FString MeshPath;
    UPROPERTY(BlueprintReadWrite) FString IconPath;
    UPROPERTY(BlueprintReadWrite) FString SourceMod;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnModLoaded, const FRFModManifest&, Manifest);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnModError, const FString&, ErrorMessage);

UCLASS()
class REALMFORGE_API URFModLoader : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ─── Mod Management ───────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Mods")
    void ScanModDirectory();

    UFUNCTION(BlueprintCallable, Category = "RF|Mods")
    bool LoadMod(const FString& ModID);

    UFUNCTION(BlueprintCallable, Category = "RF|Mods")
    void UnloadMod(const FString& ModID);

    UFUNCTION(BlueprintCallable, Category = "RF|Mods")
    void LoadAllEnabledMods();

    UFUNCTION(BlueprintCallable, Category = "RF|Mods")
    void SetModEnabled(const FString& ModID, bool bEnabled);

    UFUNCTION(BlueprintPure, Category = "RF|Mods")
    TArray<FRFModManifest> GetLoadedMods() const { return LoadedMods; }

    UFUNCTION(BlueprintPure, Category = "RF|Mods")
    TArray<FRFModManifest> GetAvailableMods() const { return AvailableMods; }

    UFUNCTION(BlueprintPure, Category = "RF|Mods")
    bool IsModLoaded(const FString& ModID) const;

    // ─── Lua Scripting ────────────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Mods|Lua")
    bool ExecuteLuaScript(const FString& ModID, const FString& ScriptPath);

    UFUNCTION(BlueprintCallable, Category = "RF|Mods|Lua")
    bool CallLuaFunction(const FString& ModID, const FString& FunctionName,
        const TArray<FString>& Args);

    UFUNCTION(BlueprintCallable, Category = "RF|Mods|Lua")
    FString EvaluateLuaExpression(const FString& Expression);

    // ─── Mod Asset Registry ───────────────────────────────────────────
    UFUNCTION(BlueprintCallable, Category = "RF|Mods|Assets")
    void RegisterModAsset(const FRFModAsset& Asset);

    UFUNCTION(BlueprintPure, Category = "RF|Mods|Assets")
    TArray<FRFModAsset> GetModAssetsByCategory(const FString& Category) const;

    UFUNCTION(BlueprintPure, Category = "RF|Mods|Assets")
    FRFModAsset GetModAsset(const FString& AssetID) const;

    // ─── Events ───────────────────────────────────────────────────────
    UPROPERTY(BlueprintAssignable) FOnModLoaded OnModLoaded;
    UPROPERTY(BlueprintAssignable) FOnModError OnModError;

private:
    TArray<FRFModManifest> AvailableMods;
    TArray<FRFModManifest> LoadedMods;
    TArray<FRFModAsset> ModAssets;

    // Lua states per mod (sandbox isolation)
    TMap<FString, lua_State*> LuaStates;

    FString GetModsDirectory() const;
    bool ParseManifest(const FString& ManifestPath, FRFModManifest& OutManifest);
    lua_State* CreateSandboxedLuaState(const FString& ModID);
    void RegisterLuaAPI(lua_State* L, const FString& ModID);
    void CloseLuaState(const FString& ModID);

    // Lua API functions exposed to mods
    static int Lua_RF_RegisterAsset(lua_State* L);
    static int Lua_RF_SpawnMiniature(lua_State* L);
    static int Lua_RF_ShowNotification(lua_State* L);
    static int Lua_RF_RollDice(lua_State* L);
    static int Lua_RF_GetWorldFlag(lua_State* L);
    static int Lua_RF_SetWorldFlag(lua_State* L);
    static int Lua_RF_AddChatMessage(lua_State* L);
    static int Lua_RF_RegisterChatCommand(lua_State* L);
    static int Lua_RF_GetCurrentRound(lua_State* L);
    static int Lua_RF_GetMiniatureStats(lua_State* L);
};
