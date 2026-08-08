#include "common/ServerApp.h"
#include "core/Core.h"
#include "match/ClientSession.h"
#include "match/MatchGlobal.h"
#include "match/MatchServer.h"
#include "match/PeerRegistry.h"
#include "match/RoomManager.h"
#include "match/SessionManager.h"

namespace nhn::match {

Ref<SessionManager> GSessionManager;
Ref<RoomManager> GRoomManager;
Ref<PeerRegistry> GPeerRegistry;
Ref<MatchServer> GMatchServer;

}  // namespace nhn::match

using namespace nhn;
using namespace nhn::match;

int main(int argc, char** argv) {
    if (!ServerApp::Bootstrap(argc, argv, "MatchServer")) {
        return 1;
    }

    Config& config = ServerApp::GetConfig();

    MatchServer::Settings settings;
    settings.clientPort = config.GetPort("client-port", 7777);
    settings.peerPort = config.GetPort("peer-port", 7900);
    settings.ticketLifetimeMs = static_cast<uint32>(config.GetInt("ticket-lifetime-ms", 30000));
    settings.handoffTimeoutMs = static_cast<uint32>(config.GetInt("handoff-timeout-ms", 15000));
    settings.kickCooldownMs = static_cast<uint32>(config.GetInt("kick-cooldown-ms", 60000));
    settings.maxRooms = config.GetInt("max-rooms", 4096);
    settings.maxSessions = config.GetInt("max-sessions", 4096);
    settings.quickMatchFillMs =
        static_cast<uint32>(config.GetInt("quick-match-fill-ms", 8000));
    settings.peerSilenceTimeoutMs =
        static_cast<uint32>(config.GetInt("peer-silence-timeout-ms", 10000));

    GSessionManager = std::make_shared<SessionManager>();
    GRoomManager = std::make_shared<RoomManager>();
    GPeerRegistry = std::make_shared<PeerRegistry>();
    GMatchServer = std::make_shared<MatchServer>(settings);

    GMatchServer->RegisterClientHandlers();
    GMatchServer->RegisterPeerHandlers();

    // Peers listen on their own port so a misconfigured client can never reach
    // control-plane packets, and the two can be firewalled separately.
    ServerServiceRef peerService = std::make_shared<ServerService>(
        NetAddress::Any(settings.peerPort),
        []() -> SessionRef { return std::make_shared<PeerSession>(); }, 64);

    ServerServiceRef clientService = std::make_shared<ServerService>(
        NetAddress::Any(settings.clientPort),
        []() -> SessionRef { return std::make_shared<ClientSession>(); },
        settings.maxSessions);

    // Browsers cannot open a raw socket, so the same session class is offered a
    // second time behind a WebSocket handshake. Identical handlers; only the
    // transport differs.
    const uint16 webPort = config.GetPort("web-port", 7787);
    ServerServiceRef webService = std::make_shared<ServerService>(
        NetAddress::Any(webPort),
        []() -> SessionRef { return std::make_shared<ClientSession>(); }, settings.maxSessions);
    webService->SetWebSocket(true);

    NHN_CHECK(peerService->Start(), "cannot listen for peers on port {}", settings.peerPort);
    NHN_CHECK(clientService->Start(), "cannot listen for clients on port {}", settings.clientPort);
    NHN_CHECK(webService->Start(), "cannot listen for browsers on port {}", webPort);

    GMatchServer->StartMaintenanceTimer();

    LOG_INFO("match server ready — clients on {}, browsers on {}, peers on {}",
             settings.clientPort, webPort, settings.peerPort);

    ServerApp::WaitForShutdown();

    webService->CloseService();
    clientService->CloseService();
    peerService->CloseService();

    ServerApp::Shutdown();

    GMatchServer.reset();
    GPeerRegistry.reset();
    GRoomManager.reset();
    GSessionManager.reset();
    return 0;
}
