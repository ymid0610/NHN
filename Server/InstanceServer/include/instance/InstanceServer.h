#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "common/PeerLink.h"
#include "common/TicketTable.h"
#include "core/Core.h"
#include "instance/Instance.h"
#include "protocol/Dispatcher.h"

namespace nhn::instance {

/// A player's connection to a running match.
class InstanceSession : public PacketSession {
public:
    SessionId GetSessionId() const { return _sessionId.load(std::memory_order_acquire); }
    bool IsAuthenticated() const { return _authenticated.load(std::memory_order_acquire); }
    void Authenticate(SessionId sessionId, std::string nickname);

    InstanceId GetInstanceId() const { return _instanceId.load(std::memory_order_acquire); }
    void SetInstanceId(InstanceId id) { _instanceId.store(id, std::memory_order_release); }

    std::string GetNickname() const;

    template <class T>
    void SendPacket(T&& packet) {
        Send(proto::MakePacket(std::forward<T>(packet)));
    }

protected:
    void OnDisconnected() override;
    void OnRecvPacket(uint8* buffer, int32 length) override;

private:
    std::atomic<SessionId> _sessionId{kInvalidSessionId};
    std::atomic<InstanceId> _instanceId{kInvalidInstanceId};
    std::atomic<bool> _authenticated{false};

    mutable std::mutex _lock;
    std::string _nickname;
};

class InstanceManager {
public:
    InstanceRef Create(const proto::P_InstanceCreate& request, uint32 joinTimeoutMs,
                       uint32 tickIntervalMs);
    InstanceRef Find(InstanceId instanceId) const;
    void Remove(InstanceId instanceId);

    /// Which instance a ticketed player belongs to. Populated when the match
    /// server creates the instance, so a redeeming client is routed without a
    /// lookup back to the match server.
    InstanceId FindInstanceForSession(SessionId sessionId) const;
    void ForgetSession(SessionId sessionId);

    int32 Count() const;

private:
    mutable std::mutex _lock;
    std::unordered_map<InstanceId, InstanceRef> _instances;
    std::unordered_map<SessionId, InstanceId> _sessionToInstance;
};

class InstanceServer;

extern Ref<InstanceManager> GInstanceManager;
extern Ref<TicketTable> GTicketTable;
extern Ref<InstanceServer> GInstanceServer;
extern PeerLinkRef GPeerLink;

class InstanceServer {
public:
    struct Settings {
        uint16 clientPort = 7850;
        /// WebSocket port, reported in the create-ack so browsers are handed a
        /// port they can actually reach.
        uint16 webPort = 7860;
        /// How long players have to arrive before the match is abandoned. Kept
        /// below the match server's handoff timeout so this side reports the
        /// failure first, with a specific reason.
        uint32 joinTimeoutMs = 12'000;
        uint32 tickIntervalMs = 100;
        std::string publicHost = "127.0.0.1";
        int32 capacity = 0;
    };

    explicit InstanceServer(Settings settings) : _settings(settings) {}

    const Settings& GetSettings() const { return _settings; }

    void RegisterClientHandlers();
    void RegisterPeerHandlers();

    void DispatchClientPacket(const InstanceSessionRef& session, uint8* buffer, int32 length);
    PeerLink::Dispatcher* GetPeerDispatcher() { return &_peerDispatcher; }

    /// Reports a finished or abandoned match back to the match server.
    void ReportInstanceClosed(InstanceId instanceId, const std::string& reason);
    void ReportInstanceReady(InstanceId instanceId);

    void StartMaintenanceTimer(const JobQueueRef& queue);

private:
    Settings _settings;
    proto::PacketDispatcher<InstanceSession> _clientDispatcher;
    PeerLink::Dispatcher _peerDispatcher;
};

}  // namespace nhn::instance
