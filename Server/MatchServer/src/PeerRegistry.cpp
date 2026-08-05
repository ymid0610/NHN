#include "match/PeerRegistry.h"

#include <algorithm>

#include "core/Log.h"
#include "match/MatchGlobal.h"
#include "match/MatchServer.h"

namespace nhn::match {

using namespace proto;

// ---------------------------------------------------------------------------
// PeerSession
// ---------------------------------------------------------------------------

std::string PeerSession::GetServerId() const {
    std::lock_guard<std::mutex> guard(_lock);
    return _serverId;
}

void PeerSession::SetServerId(std::string id) {
    std::lock_guard<std::mutex> guard(_lock);
    _serverId = std::move(id);
}

std::string PeerSession::GetPublicHost() const {
    std::lock_guard<std::mutex> guard(_lock);
    return _publicHost;
}

void PeerSession::SetPublicEndpoint(std::string host, uint16 port, uint16 webPort) {
    {
        std::lock_guard<std::mutex> guard(_lock);
        _publicHost = std::move(host);
    }
    _publicPort.store(port, std::memory_order_release);
    _publicWebPort.store(webPort, std::memory_order_release);
}

void PeerSession::OnDisconnected() {
    const ServerType type = GetServerType();
    if (type != ServerType::None) {
        LOG_WARN("peer {} '{}' disconnected: {}", ToString(type), GetServerId(),
                 GetDisconnectCause());
    }
    GPeerRegistry->Unregister(std::static_pointer_cast<PeerSession>(shared_from_this()));
}

void PeerSession::OnRecvPacket(uint8* buffer, int32 length) {
    // Any packet counts as a sign of life, not just the heartbeat.
    MarkSeen();
    GMatchServer->DispatchPeerPacket(std::static_pointer_cast<PeerSession>(shared_from_this()),
                                     buffer, length);
}

// ---------------------------------------------------------------------------
// PeerRegistry
// ---------------------------------------------------------------------------

void PeerRegistry::Register(const PeerSessionRef& session) {
    session->MarkSeen();

    std::lock_guard<std::mutex> guard(_lock);
    if (std::find(_peers.begin(), _peers.end(), session) == _peers.end()) {
        _peers.push_back(session);
    }
}

int32 PeerRegistry::DropSilentPeers(uint32 timeoutMs) {
    const TickCount now = NowTick();

    std::vector<PeerSessionRef> stale;
    {
        std::lock_guard<std::mutex> guard(_lock);
        for (const PeerSessionRef& peer : _peers) {
            const TickCount lastSeen = peer->GetLastSeen();
            if (lastSeen != 0 && now - lastSeen >= timeoutMs) {
                stale.push_back(peer);
            }
        }
    }

    // Disconnecting runs OnDisconnected, which unregisters — so it happens
    // outside the lock.
    for (const PeerSessionRef& peer : stale) {
        LOG_WARN("peer {} '{}' has been silent for {} ms; dropping it",
                 ToString(peer->GetServerType()), peer->GetServerId(), now - peer->GetLastSeen());
        peer->Disconnect("peer stopped responding");
    }
    return static_cast<int32>(stale.size());
}

void PeerRegistry::Unregister(const PeerSessionRef& session) {
    std::lock_guard<std::mutex> guard(_lock);
    _peers.erase(std::remove(_peers.begin(), _peers.end(), session), _peers.end());
}

PeerSessionRef PeerRegistry::FindAny(ServerType type) const {
    std::lock_guard<std::mutex> guard(_lock);
    for (const PeerSessionRef& peer : _peers) {
        if (peer->GetServerType() == type && peer->IsConnected()) {
            return peer;
        }
    }
    return nullptr;
}

PeerSessionRef PeerRegistry::PickInstanceServer() const {
    std::lock_guard<std::mutex> guard(_lock);

    PeerSessionRef best;
    int32 bestLoad = 0;
    for (const PeerSessionRef& peer : _peers) {
        if (peer->GetServerType() != ServerType::Instance || !peer->IsConnected()) {
            continue;
        }
        // Capacity of zero means "unbounded", which is what a single instance
        // server running locally reports.
        const int32 capacity = peer->GetCapacity();
        const int32 load = peer->GetLoad();
        if (capacity > 0 && load >= capacity) {
            continue;
        }
        if (best == nullptr || load < bestLoad) {
            best = peer;
            bestLoad = load;
        }
    }
    return best;
}

bool PeerRegistry::GetClientEndpoint(ServerType type, bool forWebSocket, std::string& outHost,
                                     uint16& outPort) const {
    const PeerSessionRef peer = FindAny(type);
    if (peer == nullptr) {
        return false;
    }
    outHost = peer->GetPublicHost();
    outPort = forWebSocket ? peer->GetPublicWebPort() : peer->GetPublicPort();
    return !outHost.empty() && outPort != 0;
}

void PeerRegistry::ForEach(ServerType type,
                           const std::function<void(const PeerSessionRef&)>& fn) const {
    std::vector<PeerSessionRef> snapshot;
    {
        std::lock_guard<std::mutex> guard(_lock);
        for (const PeerSessionRef& peer : _peers) {
            if (peer->GetServerType() == type) {
                snapshot.push_back(peer);
            }
        }
    }
    for (const PeerSessionRef& peer : snapshot) {
        fn(peer);
    }
}

void PeerRegistry::SendToAll(ServerType type, const SendBufferRef& sendBuffer) {
    if (sendBuffer == nullptr) {
        return;
    }
    ForEach(type, [&sendBuffer](const PeerSessionRef& peer) { peer->Send(sendBuffer); });
}

int32 PeerRegistry::Count(ServerType type) const {
    std::lock_guard<std::mutex> guard(_lock);
    return static_cast<int32>(std::count_if(_peers.begin(), _peers.end(),
                                            [type](const PeerSessionRef& peer) {
                                                return peer->GetServerType() == type;
                                            }));
}

}  // namespace nhn::match
