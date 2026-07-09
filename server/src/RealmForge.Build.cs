// RFGameLiftServer.Build.cs
// Unreal Engine 5 module build rules for GameLift Server SDK integration.
// Place in: Source/RealmForge/

using UnrealBuildTool;
using System.IO;

public class RealmForge : ModuleRules
{
    public RealmForge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "HTTP",
            "Json",
            "JsonUtilities",
            "WebSockets",
            "Networking",
            "Sockets",
            "OnlineSubsystem",
            "OnlineSubsystemUtils",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "UMG",
        });

        // ─── GameLift Server SDK ──────────────────────────────
        // Only link on dedicated server builds
        if (Target.Type == TargetType.Server)
        {
            string GameLiftSDKDir = Path.Combine(PluginDirectory, "GameLiftServerSDK");

            if (!Directory.Exists(GameLiftSDKDir))
            {
                System.Console.WriteLine("[RealmForge] WARNING: GameLift Server SDK not found at " + GameLiftSDKDir);
                System.Console.WriteLine("[RealmForge] Run server/install-gamelift-sdk.sh to install it.");
            }
            else
            {
                // Public include path
                PublicIncludePaths.Add(Path.Combine(GameLiftSDKDir, "include"));

                // Link against the prebuilt library
                string LibDir = Path.Combine(GameLiftSDKDir, "lib");

                if (Target.Platform == UnrealTargetPlatform.Linux)
                {
                    PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libaws-cpp-sdk-gamelift-server.a"));
                    PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libsioclient.a"));
                    PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libboost_system.a"));
                    PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libboost_date_time.a"));
                    PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libssl.a"));
                    PublicAdditionalLibraries.Add(Path.Combine(LibDir, "libcrypto.a"));
                }
                else if (Target.Platform == UnrealTargetPlatform.Win64)
                {
                    PublicAdditionalLibraries.Add(Path.Combine(LibDir, "aws-cpp-sdk-gamelift-server.lib"));
                }

                PublicDefinitions.Add("WITH_GAMELIFT=1");
            }
        }
        else
        {
            // Client build — stub out GameLift calls
            PublicDefinitions.Add("WITH_GAMELIFT=0");
        }
    }
}
