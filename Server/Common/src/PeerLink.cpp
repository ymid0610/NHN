#include "common/PeerLink.h"

namespace nhn {

using namespace proto;

// ---------------------------------------------------------------------------
// PeerLinkSession
// ---------------------------------------------------------------------------

void PeerLinkSession::OnConnected() {
    if (Ref<PeerLink> link = _link.lock()) {
        link->HandleConnected(std::static_pointer_cast<PeerLinkSession>(shared_from_this()));
    }
}

void PeerLinkSession::OnDisconnected() {
    if (Ref<PeerLink> link = _link.lock()) {
        link->HandleDisconnected(std::static_pointer_cast<PeerLinkSession>(shared_from_this()));
    }
}

void PeerLinkSession::OnRecvPacket(uint8* buffer, int32 length) {
    if (Ref<PeerLink> link = _link.lock()) {
        link->HandlePacket(std::static_pointer_cast<PeerLinkSession>(shared_from_this()), buffer,
                           length);
    }
}

// ---------------------------------------------------------------------------
// PeerLink
// ---------------------------------------------------------------------------

Ref<PeerLink> PeerLink::Create(Options options, Dispatcher* dispatcher) {
    Ref<PeerLink> link = std::make_shared<PeerLink>();
    link->_options = std::move(options);
    link->_dispatcher = dispatcher;
    return link;
}

bool PeerLink::Start() {
    Weak<PeerLink> weakSelf = std::static_pointer_cast<PeerLink>(shared_from_this());

    _service = std::make_shared<ClientService>(
        NetAddress(_options.matchHost, _options.matchPort),
        [weakSelf]() -> SessionRef { return std::make_shared<PeerLinkSession>(weakSelf); }, 1);

    TryConnect();
    return true;
}

void PeerLink::Stop() {
    _stopped.store(true, std::memory_order_release);

    PeerLinkSessionRef session;
    {
        std::lock_guard<std::mutex> guard(_sessionLock);
        session = _session;
        _session.reset();
    }
    if (session != nullptr) {
        session->Disconnect("peer link stopping");
    }
}

void PeerLink::TryConnect() {
    if (_stopped.load(std::memory_order_acquire)) {
        return;
    }

    LOG_DEBUG("dialling match server at {}:{}", _options.matchHost, _options.matchPort);
    if (!_service->Start()) {
        ScheduleReconnect();
    }
}

void PeerLink::ScheduleReconnect() {
    if (_stopped.load(std::memory_order_acquire)) {
        return;
    }

    Ref<PeerLink> self = std::static_pointer_cast<PeerLink>(shared_from_this());
    DoTimer(_options.reconnectDelayMs, [self]() { self->TryConnect(); });
}

void PeerLink::HandleConnected(const PeerLinkSessionRef& session) {
    {
        std::lock_guard<std::mutex> guard(_sessionLock);
        _session = session;
    }
    _connected.store(true, std::memory_order_release);

    LOG_INFO("control link to match server established");

    P_ServerHello hello;
    hello.serverType = _options.serverType;
    hello.serverId = _options.serverId;
    hello.publicHost = _options.publicHost;
    hello.publicPort = _options.publicPort;
    hello.capacity = _options.capacity;
    session->Send(MakePacket(hello));

    ScheduleHeartbeat();
}

void PeerLink::HandleDisconnected(const PeerLinkSessionRef& session) {
    {
        std::lock_guard<std::mutex> guard(_sessionLock);
        // A stale session from a previous attempt must not clear a newer one.
        if (_session != session) {
            return;
        }
        _session.reset();
    }
    _connected.store(false, std::memory_order_release);

    LOG_WARN("control link lost ({}); reconnecting in {} ms", session->GetDisconnectCause(),
             _options.reconnectDelayMs);
    ScheduleReconnect();
}

void PeerLink::HandlePacket(const PeerLinkSessionRef& session, uint8* buffer, int32 length) {
    const PacketId id = PeekPacketId(buffer, length);

    // The handshake and heartbeat are the link's own business; everything else
    // belongs to the owning server.
    if (id == PacketId::P_ServerHelloAck) {
        P_ServerHelloAck ack;
        if (!ParsePacket(ack, buffer, length)) {
            session->Disconnect("malformed P_ServerHelloAck");
            return;
        }
        if (ack.result != ResultCode::Ok) {
            LOG_ERROR("match server rejected registration: {}", ToString(ack.result));
            session->Disconnect("registration rejected");
            return;
        }
        if (ack.heartbeatIntervalMs > 0) {
            _options.heartbeatIntervalMs = ack.heartbeatIntervalMs;
        }
        LOG_INFO("registered with match server as {} '{}'", ToString(_options.serverType),
                 _options.serverId);
        if (_onReady) {
            _onReady();
        }
        return;
    }

    if (id == PacketId::P_Heartbeat) {
        P_Heartbeat heartbeat;
        if (ParsePacket(heartbeat, buffer, length)) {
            P_HeartbeatAck ack;
            ack.sentAtMs = heartbeat.sentAtMs;
            session->Send(MakePacket(ack));
        }
        return;
    }

    if (id == PacketId::P_HeartbeatAck) {
        return;
    }

    if (_dispatcher != nullptr) {
        _dispatcher->Dispatch(session, buffer, length);
    }
}

void PeerLink::ScheduleHeartbeat() {
    Ref<PeerLink> self = std::static_pointer_cast<PeerLink>(shared_from_this());
    DoTimer(_options.heartbeatIntervalMs, [self]() {
        if (self->_stopped.load(std::memory_order_acquire) || !self->IsConnected()) {
            return;
        }
        P_Heartbeat heartbeat;
        heartbeat.sentAtMs = NowUnixMs();
        self->Send(MakePacket(heartbeat));
        self->ScheduleHeartbeat();
    });
}

void PeerLink::Send(SendBufferRef sendBuffer) {
    if (sendBuffer == nullptr) {
        return;
    }

    PeerLinkSessionRef session;
    {
        std::lock_guard<std::mutex> guard(_sessionLock);
        session = _session;
    }

    if (session == nullptr) {
        // Dropped on purpose. The match server re-pushes authoritative state
        // when the link comes back, so buffering here would only replay stale
        // information.
        LOG_DEBUG("control link down; dropping outbound packet");
        return;
    }
    session->Send(std::move(sendBuffer));
}

}  // namespace nhn
