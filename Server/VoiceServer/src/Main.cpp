#include "common/PeerLink.h"
#include "common/ServerApp.h"
#include "common/TicketTable.h"
#include "core/Core.h"
#include "voice/VoiceRouter.h"

using namespace nhn;
using namespace nhn::voice;
using namespace nhn::proto;

namespace {

Ref<VoiceRouter> gRouter;
Ref<TicketTable> gTickets;

void RegisterPeerHandlers(PeerLink::Dispatcher& dispatcher) {
    dispatcher.On<P_TicketAdd>([](const PeerLinkSessionRef&, const P_TicketAdd& packet) {
        gTickets->Add(packet.ticket, packet.sessionId, packet.nickname, packet.expiresAtMs);
    });

    dispatcher.On<P_TicketRevoke>([](const PeerLinkSessionRef&, const P_TicketRevoke& packet) {
        gTickets->Revoke(packet.ticket);
    });

    dispatcher.On<P_ChannelJoin>([](const PeerLinkSessionRef&, const P_ChannelJoin& packet) {
        gRouter->ApplyChannelJoin(packet.sessionId, packet.channelType, packet.channelId);
    });

    dispatcher.On<P_ChannelLeave>([](const PeerLinkSessionRef&, const P_ChannelLeave& packet) {
        gRouter->ApplyChannelLeave(packet.sessionId, packet.channelType, packet.channelId);
    });

    dispatcher.On<P_SessionClosed>([](const PeerLinkSessionRef&, const P_SessionClosed& packet) {
        gRouter->RemoveSession(packet.sessionId);
    });
}

void ScheduleMaintenance(const JobQueueRef& queue, uint32 intervalMs) {
    queue->DoTimer(intervalMs, [queue, intervalMs]() {
        gTickets->PurgeExpired(NowUnixMs());
        gRouter->SweepIdlePeers();

        const VoiceRouter::Stats stats = gRouter->GetStats();
        LOG_DEBUG("voice: {} peers, {} channels, {} relayed, {} dropped", stats.peers,
                  stats.channels, stats.framesRelayed, stats.framesDropped);

        ScheduleMaintenance(queue, intervalMs);
    });
}

}  // namespace

int main(int argc, char** argv) {
    if (!ServerApp::Bootstrap(argc, argv, "VoiceServer")) {
        return 1;
    }

    Config& config = ServerApp::GetConfig();

    const uint16 voicePort = config.GetPort("voice-port", 7870);
    const uint32 peerTimeoutMs = static_cast<uint32>(config.GetInt("peer-timeout-ms", 15000));
    const int32 concurrentReceives = config.GetInt("concurrent-receives", 16);

    gTickets = std::make_shared<TicketTable>();

    Ref<UdpSocket> socket = std::make_shared<UdpSocket>();

    VoiceRouter::Settings routerSettings;
    routerSettings.peerTimeoutMs = peerTimeoutMs;
    gRouter = std::make_shared<VoiceRouter>(routerSettings, socket, gTickets);

    const bool started = socket->Start(
        voicePort,
        [](const NetAddress& from, const uint8* data, int32 length) {
            gRouter->OnDatagram(from, data, length);
        },
        concurrentReceives);
    NHN_CHECK(started, "cannot bind voice udp port {}", voicePort);

    PeerLink::Dispatcher peerDispatcher;
    RegisterPeerHandlers(peerDispatcher);

    PeerLink::Options linkOptions;
    linkOptions.matchHost = config.GetString("match-host", "127.0.0.1");
    linkOptions.matchPort = config.GetPort("match-peer-port", 7900);
    linkOptions.serverType = ServerType::Voice;
    linkOptions.serverId = config.GetString("server-id", "voice-1");
    linkOptions.publicHost = config.GetString("public-host", "127.0.0.1");
    linkOptions.publicPort = voicePort;
    linkOptions.capacity = config.GetInt("capacity", 0);

    PeerLinkRef peerLink = PeerLink::Create(linkOptions, &peerDispatcher);
    peerLink->Start();

    JobQueueRef maintenance = std::make_shared<JobQueue>();
    ScheduleMaintenance(maintenance, 5000);

    LOG_INFO("voice server ready — udp relay on {}", voicePort);

    ServerApp::WaitForShutdown();

    peerLink->Stop();
    socket->Stop();

    ServerApp::Shutdown();

    gRouter.reset();
    gTickets.reset();
    return 0;
}
