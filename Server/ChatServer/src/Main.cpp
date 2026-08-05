#include "chat/ChatServer.h"
#include "common/PeerLink.h"
#include "common/ServerApp.h"
#include "core/Core.h"

using namespace nhn;
using namespace nhn::chat;

int main(int argc, char** argv) {
    if (!ServerApp::Bootstrap(argc, argv, "ChatServer")) {
        return 1;
    }

    Config& config = ServerApp::GetConfig();

    ChatServer::Settings settings;
    settings.clientPort = config.GetPort("client-port", 7800);
    settings.messageBurst = static_cast<uint32>(config.GetInt("message-burst", 4));
    settings.messagesPerSecond = config.GetInt("messages-per-10s", 15) / 10.0;
    settings.ticketSweepIntervalMs =
        static_cast<uint32>(config.GetInt("ticket-sweep-ms", 5000));

    // Constructed before the listener so a session created by an early accept
    // always finds them.
    GChatServer = std::make_shared<ChatServer>(settings);
    GTicketTable = std::make_shared<TicketTable>();
    GChannelManager = std::make_shared<ChannelManager>();
    GChannelManager->Init();

    GChatServer->RegisterClientHandlers();
    GChatServer->RegisterPeerHandlers();

    ServerServiceRef clientService = std::make_shared<ServerService>(
        NetAddress::Any(settings.clientPort),
        []() -> SessionRef { return std::make_shared<ChatSession>(); }, 4096);

    const uint16 webPort = config.GetPort("web-port", 7810);
    ServerServiceRef webService = std::make_shared<ServerService>(
        NetAddress::Any(webPort),
        []() -> SessionRef { return std::make_shared<ChatSession>(); }, 4096);
    webService->SetWebSocket(true);

    NHN_CHECK(clientService->Start(), "cannot listen for chat clients on port {}",
              settings.clientPort);
    NHN_CHECK(webService->Start(), "cannot listen for browsers on port {}", webPort);

    PeerLink::Options linkOptions;
    linkOptions.matchHost = config.GetString("match-host", "127.0.0.1");
    linkOptions.matchPort = config.GetPort("match-peer-port", 7900);
    linkOptions.serverType = proto::ServerType::Chat;
    linkOptions.serverId = config.GetString("server-id", "chat-1");
    // What clients are told to dial, which need not be the interface the
    // control link happens to originate from.
    linkOptions.publicHost = config.GetString("public-host", "127.0.0.1");
    linkOptions.publicPort = settings.clientPort;
    linkOptions.publicWebPort = webPort;
    linkOptions.capacity = config.GetInt("capacity", 0);

    PeerLinkRef peerLink = PeerLink::Create(linkOptions, GChatServer->GetPeerDispatcher());
    peerLink->Start();

    JobQueueRef maintenance = std::make_shared<JobQueue>();
    GChatServer->StartMaintenanceTimer(maintenance);

    LOG_INFO("chat server ready — clients on {}, browsers on {}", settings.clientPort, webPort);

    ServerApp::WaitForShutdown();

    peerLink->Stop();
    webService->CloseService();
    clientService->CloseService();

    ServerApp::Shutdown();

    GChannelManager.reset();
    GTicketTable.reset();
    GChatServer.reset();
    return 0;
}
