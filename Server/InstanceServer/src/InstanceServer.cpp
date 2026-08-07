#include "instance/InstanceServer.h"

#include "instance/game/ShootingGameMode.h"

namespace nhn::instance {

using namespace proto;

Ref<InstanceManager> GInstanceManager;
Ref<TicketTable> GTicketTable;
Ref<InstanceServer> GInstanceServer;
PeerLinkRef GPeerLink;

// ---------------------------------------------------------------------------
// InstanceSession
// ---------------------------------------------------------------------------

void InstanceSession::Authenticate(SessionId sessionId, std::string nickname) {
    {
        std::lock_guard<std::mutex> guard(_lock);
        _nickname = std::move(nickname);
    }
    _sessionId.store(sessionId, std::memory_order_release);
    _authenticated.store(true, std::memory_order_release);
}

std::string InstanceSession::GetNickname() const {
    std::lock_guard<std::mutex> guard(_lock);
    return _nickname;
}

void InstanceSession::OnDisconnected() {
    const SessionId sessionId = GetSessionId();
    if (sessionId == kInvalidSessionId) {
        return;
    }
    if (InstanceRef instance = GInstanceManager->Find(GetInstanceId())) {
        instance->EnqueuePlayerLeft(sessionId, LeaveReason::Disconnected);
    }
}

void InstanceSession::OnRecvPacket(uint8* buffer, int32 length) {
    GInstanceServer->DispatchClientPacket(
        std::static_pointer_cast<InstanceSession>(shared_from_this()), buffer, length);
}

// ---------------------------------------------------------------------------
// InstanceManager
// ---------------------------------------------------------------------------

InstanceRef InstanceManager::Create(const P_InstanceCreate& request, uint32 joinTimeoutMs,
                                    uint32 tickIntervalMs) {
    InstanceRef instance = std::make_shared<Instance>(
        request.instanceId, request.roomId, request.mode, request.config, request.members,
        std::make_shared<ShootingGameMode>(request.mode, request.config, tickIntervalMs),
        joinTimeoutMs, tickIntervalMs);

    std::lock_guard<std::mutex> guard(_lock);
    _instances[request.instanceId] = instance;
    for (const RoomMemberInfo& member : request.members) {
        _sessionToInstance[member.sessionId] = request.instanceId;
    }
    return instance;
}

InstanceRef InstanceManager::Find(InstanceId instanceId) const {
    std::lock_guard<std::mutex> guard(_lock);
    const auto it = _instances.find(instanceId);
    return it != _instances.end() ? it->second : nullptr;
}

void InstanceManager::Remove(InstanceId instanceId) {
    std::lock_guard<std::mutex> guard(_lock);
    _instances.erase(instanceId);
    for (auto it = _sessionToInstance.begin(); it != _sessionToInstance.end();) {
        it = it->second == instanceId ? _sessionToInstance.erase(it) : std::next(it);
    }
}

InstanceId InstanceManager::FindInstanceForSession(SessionId sessionId) const {
    std::lock_guard<std::mutex> guard(_lock);
    const auto it = _sessionToInstance.find(sessionId);
    return it != _sessionToInstance.end() ? it->second : kInvalidInstanceId;
}

void InstanceManager::ForgetSession(SessionId sessionId) {
    std::lock_guard<std::mutex> guard(_lock);
    _sessionToInstance.erase(sessionId);
}

int32 InstanceManager::Count() const {
    std::lock_guard<std::mutex> guard(_lock);
    return static_cast<int32>(_instances.size());
}

// ---------------------------------------------------------------------------
// InstanceServer
// ---------------------------------------------------------------------------

void InstanceServer::DispatchClientPacket(const InstanceSessionRef& session, uint8* buffer,
                                          int32 length) {
    if (!session->IsAuthenticated() && PeekPacketId(buffer, length) != PacketId::C_InstHello) {
        session->Disconnect("packet before instance hello");
        return;
    }
    _clientDispatcher.Dispatch(session, buffer, length);
}

void InstanceServer::RegisterClientHandlers() {
    _clientDispatcher.On<C_InstHello>(
        [](const InstanceSessionRef& session, const C_InstHello& packet) {
            TicketTable::Entry entry;
            ResultCode reason = ResultCode::TicketInvalid;
            if (!GTicketTable->Consume(packet.ticket, NowUnixMs(), entry, reason)) {
                S_InstHelloAck ack;
                ack.result = reason;
                session->SendPacket(ack);
                session->Disconnect("ticket rejected");
                LOG_WARN("instance ticket rejected from {}: {}",
                         session->GetNetAddress().ToString(), ToString(reason));
                return;
            }

            const InstanceId instanceId =
                GInstanceManager->FindInstanceForSession(entry.sessionId);
            InstanceRef instance = GInstanceManager->Find(instanceId);
            if (instance == nullptr) {
                S_InstHelloAck ack;
                ack.result = ResultCode::InstanceNotFound;
                session->SendPacket(ack);
                session->Disconnect("no instance for this ticket");
                return;
            }

            session->Authenticate(entry.sessionId, entry.nickname);
            instance->EnqueuePlayerConnected(session, entry.sessionId);
        });

    _clientDispatcher.On<C_Fire>([](const InstanceSessionRef& session, const C_Fire& packet) {
        if (InstanceRef instance = GInstanceManager->Find(session->GetInstanceId())) {
            // Posted to the instance's queue, where the game mode has exclusive
            // access to its own state.
            instance->EnqueueFire(session->GetSessionId(), packet);
        }
    });

    _clientDispatcher.On<C_InstLeave>([](const InstanceSessionRef& session, const C_InstLeave&) {
        if (InstanceRef instance = GInstanceManager->Find(session->GetInstanceId())) {
            instance->EnqueuePlayerLeft(session->GetSessionId(), LeaveReason::Left);
        }
        session->Disconnect("left the instance");
    });
}

void InstanceServer::RegisterPeerHandlers() {
    _peerDispatcher.On<P_InstanceCreate>(
        [this](const PeerLinkSessionRef& link, const P_InstanceCreate& packet) {
            // Logged on entry, not just on success: when a handoff expires the
            // first thing worth knowing is whether the request reached here at
            // all, and the success line below cannot answer that.
            LOG_INFO("instance create requested: id {} room {} with {} players",
                     packet.instanceId, packet.roomId, packet.members.size());

            P_InstanceCreateAck ack;
            ack.instanceId = packet.instanceId;

            if (packet.members.empty()) {
                ack.result = ResultCode::InvalidRequest;
                link->Send(MakePacket(ack));
                return;
            }
            if (GInstanceManager->Find(packet.instanceId) != nullptr) {
                ack.result = ResultCode::InvalidRequest;
                link->Send(MakePacket(ack));
                return;
            }

            InstanceRef instance = GInstanceManager->Create(packet, _settings.joinTimeoutMs,
                                                            _settings.tickIntervalMs);

            ack.result = ResultCode::Ok;
            ack.host = _settings.publicHost;
            ack.port = _settings.clientPort;
            ack.webPort = _settings.webPort;
            link->Send(MakePacket(ack));

            // Only armed after the ack: the deadline is for players arriving,
            // and they cannot arrive before the match server has been told
            // where to send them.
            instance->StartJoinTimeout();

            LOG_INFO("instance {} created for room {} with {} players", packet.instanceId,
                     packet.roomId, packet.members.size());
        });

    _peerDispatcher.On<P_TicketAdd>([](const PeerLinkSessionRef&, const P_TicketAdd& packet) {
        GTicketTable->Add(packet.ticket, packet.sessionId, packet.nickname, packet.expiresAtMs);
    });

    _peerDispatcher.On<P_TicketRevoke>(
        [](const PeerLinkSessionRef&, const P_TicketRevoke& packet) {
            GTicketTable->Revoke(packet.ticket);
        });

    _peerDispatcher.On<P_InstanceClosed>(
        [](const PeerLinkSessionRef&, const P_InstanceClosed& packet) {
            // The match server gave up on this handoff before we did.
            if (InstanceRef instance = GInstanceManager->Find(packet.instanceId)) {
                instance->EnqueueClose(packet.reason.empty() ? "closed by match server"
                                                             : packet.reason);
            }
        });
}

void InstanceServer::ReportInstanceReady(InstanceId instanceId) {
    if (GPeerLink == nullptr) {
        return;
    }
    P_InstanceReady ready;
    ready.instanceId = instanceId;
    GPeerLink->SendPacket(ready);
}

void InstanceServer::ReportInstanceClosed(InstanceId instanceId, const std::string& reason) {
    if (GPeerLink == nullptr) {
        return;
    }
    P_InstanceClosed closed;
    closed.instanceId = instanceId;
    closed.reason = reason;
    GPeerLink->SendPacket(closed);
}

void InstanceServer::StartMaintenanceTimer(const JobQueueRef& queue) {
    queue->DoTimer(5000, [queue, this]() {
        GTicketTable->PurgeExpired(NowUnixMs());
        StartMaintenanceTimer(queue);
    });
}

}  // namespace nhn::instance
