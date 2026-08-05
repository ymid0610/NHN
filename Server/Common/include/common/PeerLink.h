#pragma once

#include <atomic>
#include <string>

#include "core/Core.h"
#include "protocol/Dispatcher.h"
#include "protocol/Packets.h"

namespace nhn {

class PeerLink;

/// The chat, voice and instance side of a control connection.
class PeerLinkSession : public PacketSession {
public:
    explicit PeerLinkSession(Weak<PeerLink> link) : _link(std::move(link)) {}

protected:
    void OnConnected() override;
    void OnDisconnected() override;
    void OnRecvPacket(uint8* buffer, int32 length) override;

private:
    Weak<PeerLink> _link;
};

using PeerLinkSessionRef = Ref<PeerLinkSession>;

/// Outbound control link from a satellite server to the match server.
///
/// Peers dial the match server rather than the reverse, so the match server
/// needs no advance knowledge of their addresses — each announces its own
/// public endpoint on connect. The link reconnects on its own, and the match
/// server re-pushes the state that matters (tickets, channel membership) when
/// it sees a fresh P_ServerHello, so a dropped link heals without either side
/// having to reconcile diffs.
class PeerLink : public JobQueue {
public:
    struct Options {
        std::string matchHost = "127.0.0.1";
        uint16 matchPort = 7900;

        proto::ServerType serverType = proto::ServerType::None;
        std::string serverId;

        /// What clients should be told to connect to. Not necessarily the
        /// address the control link originates from.
        std::string publicHost = "127.0.0.1";
        uint16 publicPort = 0;
        /// WebSocket port, for clients that cannot open a raw socket.
        uint16 publicWebPort = 0;
        int32 capacity = 0;

        uint32 reconnectDelayMs = 2000;
        uint32 heartbeatIntervalMs = 2000;
    };

    using Dispatcher = proto::PacketDispatcher<PeerLinkSession>;

    static Ref<PeerLink> Create(Options options, Dispatcher* dispatcher);

    bool Start();
    void Stop();

    bool IsConnected() const { return _connected.load(std::memory_order_acquire); }
    const Options& GetOptions() const { return _options; }

    void Send(SendBufferRef sendBuffer);

    template <class T>
    void SendPacket(T&& packet) {
        Send(proto::MakePacket(std::forward<T>(packet)));
    }

    /// Invoked when the match server has acknowledged the hello. Satellite
    /// servers use this to (re-)publish anything the match server needs.
    void SetOnReadyHandler(std::function<void()> handler) { _onReady = std::move(handler); }

    // Called by PeerLinkSession.
    void HandleConnected(const PeerLinkSessionRef& session);
    void HandleDisconnected(const PeerLinkSessionRef& session);
    void HandlePacket(const PeerLinkSessionRef& session, uint8* buffer, int32 length);

private:
    void TryConnect();
    void ScheduleReconnect();
    void ScheduleHeartbeat();

    Options _options;
    Dispatcher* _dispatcher = nullptr;
    ClientServiceRef _service;

    mutable std::mutex _sessionLock;
    PeerLinkSessionRef _session;

    std::atomic<bool> _connected{false};
    std::atomic<bool> _stopped{false};
    std::function<void()> _onReady;
};

using PeerLinkRef = Ref<PeerLink>;

}  // namespace nhn
