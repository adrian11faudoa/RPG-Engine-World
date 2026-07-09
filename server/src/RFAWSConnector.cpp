#include "RFAWSConnector.h"
#include "HttpModule.h"
#include "WebSocketsModule.h"
#include "IWebSocket.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "SaveGameSystem.h"

const FString URFAWSConnector::TOKEN_SAVE_KEY = TEXT("RFAuthToken");

void URFAWSConnector::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // Try to restore saved auth token
    FString SavedToken = LoadToken();
    if (!SavedToken.IsEmpty())
    {
        AuthToken = SavedToken;
        UE_LOG(LogTemp, Log, TEXT("[RF|AWS] Restored saved auth token"));
    }

    UE_LOG(LogTemp, Log, TEXT("[RF|AWS] Connector initialized. API: %s"), *APIBaseURL);
}

void URFAWSConnector::Deinitialize()
{
    DisconnectWebSocket();
    Super::Deinitialize();
}

// ─── Auth ─────────────────────────────────────────────────────

void URFAWSConnector::Login(const FString& Email, const FString& Password)
{
    TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
    Body->SetStringField(TEXT("email"),    Email);
    Body->SetStringField(TEXT("password"), Password);

    auto Req = MakeRequest(TEXT("POST"), TEXT("/auth/login"), Body);
    Req->OnProcessRequestComplete().BindUObject(this, &URFAWSConnector::OnLoginResponse);
    Req->ProcessRequest();
}

void URFAWSConnector::Register(const FString& Username, const FString& Email, const FString& Password)
{
    TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
    Body->SetStringField(TEXT("username"), Username);
    Body->SetStringField(TEXT("email"),    Email);
    Body->SetStringField(TEXT("password"), Password);

    auto Req = MakeRequest(TEXT("POST"), TEXT("/auth/register"), Body);
    Req->OnProcessRequestComplete().BindUObject(this, &URFAWSConnector::OnLoginResponse);
    Req->ProcessRequest();
}

void URFAWSConnector::Logout()
{
    AuthToken.Empty();
    CurrentUser = FRFUserInfo();
    SaveToken(TEXT(""));
    DisconnectWebSocket();
    UE_LOG(LogTemp, Log, TEXT("[RF|AWS] Logged out"));
}

void URFAWSConnector::OnLoginResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
{
    bool bSuccess = bConnected && Res.IsValid() && Res->GetResponseCode() == 200 ||
                    Res->GetResponseCode() == 201;

    FRFUserInfo User;

    if (bSuccess)
    {
        TSharedPtr<FJsonObject> Json;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Res->GetContentAsString());
        if (FJsonSerializer::Deserialize(Reader, Json))
        {
            AuthToken = Json->GetStringField(TEXT("token"));
            SaveToken(AuthToken);

            TSharedPtr<FJsonObject> UserObj = Json->GetObjectField(TEXT("user"));
            User.UserID   = UserObj->GetStringField(TEXT("id"));
            User.Username = UserObj->GetStringField(TEXT("username"));
            User.Email    = UserObj->GetStringField(TEXT("email"));
            User.Role     = UserObj->GetStringField(TEXT("role"));
            CurrentUser   = User;

            UE_LOG(LogTemp, Log, TEXT("[RF|AWS] Logged in as %s (%s)"), *User.Username, *User.Role);
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[RF|AWS] Login failed: %s"),
            Res.IsValid() ? *Res->GetContentAsString() : TEXT("No response"));
    }

    AsyncTask(ENamedThreads::GameThread, [this, bSuccess, User]()
    {
        OnLoginComplete.Broadcast(bSuccess, User);
    });
}

// ─── Sessions ─────────────────────────────────────────────────

void URFAWSConnector::CreateGameSession(const FString& CampaignId, int32 MaxPlayers)
{
    TSharedPtr<FJsonObject> Body = MakeShareable(new FJsonObject);
    Body->SetStringField(TEXT("campaignId"), CampaignId);
    Body->SetNumberField(TEXT("maxPlayers"), MaxPlayers);

    auto Req = MakeRequest(TEXT("POST"), TEXT("/sessions/create"), Body);
    Req->OnProcessRequestComplete().BindUObject(this, &URFAWSConnector::OnCreateSessionResponse);
    Req->ProcessRequest();
}

void URFAWSConnector::JoinGameSession(const FString& GameSessionId)
{
    FString Path = FString::Printf(TEXT("/sessions/%s/join"), *GameSessionId);
    auto Req = MakeRequest(TEXT("POST"), Path);
    Req->OnProcessRequestComplete().BindUObject(this, &URFAWSConnector::OnJoinSessionResponse);
    Req->ProcessRequest();
}

void URFAWSConnector::ListActiveSessions()
{
    auto Req = MakeRequest(TEXT("GET"), TEXT("/sessions/active"));
    Req->OnProcessRequestComplete().BindUObject(this, &URFAWSConnector::OnListSessionsResponse);
    Req->ProcessRequest();
}

void URFAWSConnector::OnCreateSessionResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
{
    bool bSuccess = bConnected && Res.IsValid() && (Res->GetResponseCode() == 200 || Res->GetResponseCode() == 201);
    FRFGameSessionInfo Session;

    if (bSuccess)
    {
        TSharedPtr<FJsonObject> Json;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Res->GetContentAsString());
        if (FJsonSerializer::Deserialize(Reader, Json))
        {
            Session = ParseSessionInfo(Json);
            UE_LOG(LogTemp, Log, TEXT("[RF|AWS] Session created: %s → %s"),
                *Session.GameSessionId, *Session.ServerEndpoint);
        }
    }

    AsyncTask(ENamedThreads::GameThread, [this, bSuccess, Session]()
    {
        OnSessionCreated.Broadcast(bSuccess, Session);
    });
}

void URFAWSConnector::OnJoinSessionResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
{
    bool bSuccess = bConnected && Res.IsValid() && Res->GetResponseCode() == 200;
    FRFGameSessionInfo Session;

    if (bSuccess)
    {
        TSharedPtr<FJsonObject> Json;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Res->GetContentAsString());
        if (FJsonSerializer::Deserialize(Reader, Json)) Session = ParseSessionInfo(Json);
    }

    AsyncTask(ENamedThreads::GameThread, [this, bSuccess, Session]()
    {
        OnSessionJoined.Broadcast(bSuccess, Session);
    });
}

void URFAWSConnector::OnListSessionsResponse(FHttpRequestPtr Req, FHttpResponsePtr Res, bool bConnected)
{
    bool bSuccess = bConnected && Res.IsValid() && Res->GetResponseCode() == 200;
    TArray<FRFSessionListing> Listings;

    if (bSuccess)
    {
        TArray<TSharedPtr<FJsonValue>> Array;
        TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Res->GetContentAsString());
        FJsonSerializer::Deserialize(Reader, Array);

        for (const TSharedPtr<FJsonValue>& Val : Array)
        {
            TSharedPtr<FJsonObject> Obj = Val->AsObject();
            FRFSessionListing L;
            L.GameSessionId = Obj->GetStringField(TEXT("id"));
            L.Name          = Obj->GetStringField(TEXT("name"));
            L.CampaignId    = Obj->GetStringField(TEXT("campaignId"));
            L.GMName        = Obj->GetStringField(TEXT("gmName"));
            L.Players       = (int32)Obj->GetNumberField(TEXT("players"));
            L.MaxPlayers    = (int32)Obj->GetNumberField(TEXT("maxPlayers"));
            Listings.Add(L);
        }
    }

    AsyncTask(ENamedThreads::GameThread, [this, bSuccess, Listings]()
    {
        OnSessionsListed.Broadcast(bSuccess, Listings);
    });
}

// ─── Assets ───────────────────────────────────────────────────

FString URFAWSConnector::GetCDNUrl(const FString& AssetKey) const
{
    return FString::Printf(TEXT("%s/%s"), *CDNBaseURL, *AssetKey);
}

void URFAWSConnector::DownloadAsset(const FString& AssetKey, const FString& LocalSaveDir)
{
    FString URL = GetCDNUrl(AssetKey);
    FString LocalPath = FPaths::Combine(LocalSaveDir, FPaths::GetCleanFilename(AssetKey));

    auto Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(URL);
    Req->SetVerb(TEXT("GET"));
    Req->OnProcessRequestComplete().BindUObject(this, &URFAWSConnector::OnDownloadResponse, LocalPath);
    Req->ProcessRequest();

    UE_LOG(LogTemp, Log, TEXT("[RF|AWS] Downloading: %s → %s"), *URL, *LocalPath);
}

void URFAWSConnector::OnDownloadResponse(FHttpRequestPtr Req, FHttpResponsePtr Res,
    bool bConnected, FString LocalPath)
{
    bool bSuccess = bConnected && Res.IsValid() && Res->GetResponseCode() == 200;

    if (bSuccess)
    {
        IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
        FString Dir = FPaths::GetPath(LocalPath);
        if (!PF.DirectoryExists(*Dir)) PF.CreateDirectoryTree(*Dir);
        FFileHelper::SaveArrayToFile(Res->GetContent(), *LocalPath);
        UE_LOG(LogTemp, Log, TEXT("[RF|AWS] Asset saved to: %s"), *LocalPath);
    }

    AsyncTask(ENamedThreads::GameThread, [this, bSuccess, LocalPath]()
    {
        OnAssetDownloaded.Broadcast(bSuccess, LocalPath);
    });
}

// ─── WebSocket ────────────────────────────────────────────────

void URFAWSConnector::ConnectWebSocket(const FString& SessionId)
{
    ActiveSessionId = SessionId;

    if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
        FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));

    WebSocket = FWebSocketsModule::Get().CreateWebSocket(WSBaseURL, TEXT("ws"));

    WebSocket->OnConnected().AddUObject(this, &URFAWSConnector::OnWSConnected);
    WebSocket->OnClosed().AddUObject(this, &URFAWSConnector::OnWSClosed);
    WebSocket->OnMessage().AddUObject(this, &URFAWSConnector::OnWSReceived);

    WebSocket->Connect();
    UE_LOG(LogTemp, Log, TEXT("[RF|AWS] WebSocket connecting to: %s"), *WSBaseURL);
}

void URFAWSConnector::OnWSConnected()
{
    UE_LOG(LogTemp, Log, TEXT("[RF|AWS] WebSocket connected"));

    // Auth
    TSharedPtr<FJsonObject> Auth = MakeShareable(new FJsonObject);
    Auth->SetStringField(TEXT("type"),  TEXT("auth"));
    Auth->SetStringField(TEXT("token"), AuthToken);
    WSSend(Auth);

    // Join session after auth_ok (handled in OnWSReceived)
}

void URFAWSConnector::OnWSClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
    UE_LOG(LogTemp, Log, TEXT("[RF|AWS] WebSocket closed: %d — %s"), StatusCode, *Reason);
}

void URFAWSConnector::OnWSReceived(const FString& Message)
{
    TSharedPtr<FJsonObject> Json;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
    if (!FJsonSerializer::Deserialize(Reader, Json)) return;

    FString Type = Json->GetStringField(TEXT("type"));

    if (Type == TEXT("auth_ok"))
    {
        // Join the session channel
        TSharedPtr<FJsonObject> Join = MakeShareable(new FJsonObject);
        Join->SetStringField(TEXT("type"),      TEXT("join_session"));
        Join->SetStringField(TEXT("sessionId"), ActiveSessionId);
        WSSend(Join);
        UE_LOG(LogTemp, Log, TEXT("[RF|AWS] WebSocket authenticated, joined session %s"), *ActiveSessionId);
    }

    AsyncTask(ENamedThreads::GameThread, [this, Message]()
    {
        OnWSMessage.Broadcast(Message);
    });
}

void URFAWSConnector::SendWSDiceRoll(const FString& Formula, int32 Total, const FString& Visibility)
{
    TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
    Msg->SetStringField(TEXT("type"),       TEXT("dice_roll"));
    Msg->SetStringField(TEXT("formula"),    Formula);
    Msg->SetStringField(TEXT("visibility"), Visibility);

    TSharedPtr<FJsonObject> Result = MakeShareable(new FJsonObject);
    Result->SetNumberField(TEXT("total"), Total);
    Msg->SetObjectField(TEXT("result"), Result);

    WSSend(Msg);
}

void URFAWSConnector::SendWSChat(const FString& Text, const FString& Style)
{
    TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
    Msg->SetStringField(TEXT("type"),  TEXT("chat_message"));
    Msg->SetStringField(TEXT("text"),  Text);
    Msg->SetStringField(TEXT("style"), Style);
    WSSend(Msg);
}

void URFAWSConnector::SendWSFogUpdate(const FString& FogDataJson)
{
    TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
    Msg->SetStringField(TEXT("type"),        TEXT("fog_update"));
    Msg->SetStringField(TEXT("fogDataJson"), FogDataJson);
    WSSend(Msg);
}

void URFAWSConnector::SendWSInitiative(const FString& InitiativeJson)
{
    TSharedPtr<FJsonObject> Msg = MakeShareable(new FJsonObject);
    Msg->SetStringField(TEXT("type"),           TEXT("initiative_update"));
    Msg->SetStringField(TEXT("initiativeJson"), InitiativeJson);
    WSSend(Msg);
}

void URFAWSConnector::DisconnectWebSocket()
{
    if (WebSocket.IsValid() && WebSocket->IsConnected())
    {
        WebSocket->Close(1000, TEXT("Client disconnect"));
    }
    WebSocket.Reset();
}

void URFAWSConnector::WSSend(const TSharedPtr<FJsonObject>& Payload)
{
    if (!WebSocket.IsValid() || !WebSocket->IsConnected()) return;
    FString Out;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);
    WebSocket->Send(Out);
}

// ─── HTTP Helper ──────────────────────────────────────────────

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> URFAWSConnector::MakeRequest(
    const FString& Verb, const FString& Path, const TSharedPtr<FJsonObject>& Body)
{
    auto Req = FHttpModule::Get().CreateRequest();
    Req->SetURL(APIBaseURL + Path);
    Req->SetVerb(Verb);
    Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
    Req->SetHeader(TEXT("Accept"),       TEXT("application/json"));

    if (!AuthToken.IsEmpty())
        Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + AuthToken);

    if (Body.IsValid())
    {
        FString BodyStr;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&BodyStr);
        FJsonSerializer::Serialize(Body.ToSharedRef(), Writer);
        Req->SetContentAsString(BodyStr);
    }

    return Req;
}

FRFGameSessionInfo URFAWSConnector::ParseSessionInfo(const TSharedPtr<FJsonObject>& Json)
{
    FRFGameSessionInfo Info;
    Info.GameSessionId   = Json->GetStringField(TEXT("gameSessionId"));
    Info.PlayerSessionId = Json->GetStringField(TEXT("playerSessionId"));
    Info.ServerEndpoint  = Json->GetStringField(TEXT("serverEndpoint"));

    FString IP;
    FString PortStr;
    Info.ServerEndpoint.Split(TEXT(":"), &IP, &PortStr);
    Info.ServerIP   = IP;
    Info.ServerPort = FCString::Atoi(*PortStr);
    return Info;
}

void URFAWSConnector::SaveToken(const FString& Token)
{
    FString SavePath = FPaths::ProjectSavedDir() / TEXT("rf_auth.dat");
    FFileHelper::SaveStringToFile(Token, *SavePath);
}

FString URFAWSConnector::LoadToken() const
{
    FString SavePath = FPaths::ProjectSavedDir() / TEXT("rf_auth.dat");
    FString Token;
    FFileHelper::LoadFileToString(Token, *SavePath);
    return Token.TrimStartAndEnd();
}
