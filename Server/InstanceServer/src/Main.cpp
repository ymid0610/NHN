#include "common/PeerLink.h"
#include "common/ServerApp.h"
#include "core/Core.h"
#include "instance/InstanceServer.h"

using namespace nhn;
using namespace nhn::instance;

int main(int argc, char** argv) {
    if (!ServerApp::Bootstrap(argc, argv, "InstanceServer")) {
        return 1;
    }

    Config& config = ServerApp::GetConfig();

    InstanceServer::Settings settings;
    settings.clientPort = config.GetPort("client-port", 7850);
    settings.joinTimeoutMs = static_cast<uint32>(config.GetInt("join-timeout-ms", 12000));
    settings.tickIntervalMs = static_cast<uint32>(config.GetInt("tick-interval-ms", 100));
    settings.publicHost = config.GetString("public-host", "127.0.0.1");
    settings.capacity = config.GetInt("capacity", 0);

    GInstanceServer = std::make_shared<InstanceServer>(settings);
    GTicketTable = std::make_shared<TicketTable>();
    GInstanceManager = std::make_shared<InstanceManager>();

    GInstanceServer->RegisterClientHandlers();
    GInstanceServer->RegisterPeerHandlers();

    ServerServiceRef clientService = std::make_shared<ServerService>(
        NetAddress::Any(settings.clientPort),
        []() -> SessionRef { return std::make_shared<InstanceSession>(); }, 1024);

    NHN_CHECK(clientService->Start(), "cannot listen for instance clients on port {}",
              settings.clientPort);

    PeerLink::Options linkOptions;
    linkOptions.matchHost = config.GetString("match-host", "127.0.0.1");
    linkOptions.matchPort = config.GetPort("match-peer-port", 7900);
    linkOptions.serverType = proto::ServerType::Instance;
    linkOptions.serverId = config.GetString("server-id", "instance-1");
    linkOptions.publicHost = settings.publicHost;
    linkOptions.publicPort = settings.clientPort;
    linkOptions.capacity = settings.capacity;

    GPeerLink = PeerLink::Create(linkOptions, GInstanceServer->GetPeerDispatcher());
    GPeerLink->Start();

    JobQueueRef maintenance = std::make_shared<JobQueue>();
    GInstanceServer->StartMaintenanceTimer(maintenance);

    LOG_INFO("instance server ready — clients on {}", settings.clientPort);

    ServerApp::WaitForShutdown();

    GPeerLink->Stop();
    clientService->CloseService();

    ServerApp::Shutdown();

    GPeerLink.reset();
    GInstanceManager.reset();
    GTicketTable.reset();
    GInstanceServer.reset();
    return 0;
}
