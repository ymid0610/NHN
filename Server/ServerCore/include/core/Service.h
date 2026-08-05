#pragma once

#include <functional>
#include <mutex>
#include <unordered_set>
#include <vector>

#include "core/Net.h"
#include "core/Session.h"

namespace nhn {

enum class ServiceType : uint8 {
    Server,  // accepts connections
    Client,  // dials out
};

using SessionFactory = std::function<SessionRef()>;

/// Owns a set of sessions that share one role: a listening endpoint, or an
/// outbound link to a peer server. A process may run several — the match server
/// runs one for players and one for peer servers — and they all share the
/// single process-wide completion port.
class Service : public std::enable_shared_from_this<Service> {
public:
    Service(ServiceType type, NetAddress address, SessionFactory factory, int32 maxSessionCount);
    virtual ~Service();

    virtual bool Start() = 0;
    virtual void CloseService();

    bool CanStart() const { return _sessionFactory != nullptr; }

    SessionRef CreateSession();
    void AddSession(SessionRef session);
    void ReleaseSession(SessionRef session);

    /// Sends one buffer to every session. The buffer is shared, not copied.
    void Broadcast(SendBufferRef sendBuffer);

    /// Snapshots the session set and runs @p fn outside the lock, so callbacks
    /// are free to disconnect sessions or take other locks.
    void ForEachSession(const std::function<void(const SessionRef&)>& fn);

    /// Sessions accepted here speak WebSocket beneath the packet framing.
    /// Set before Start; the session classes and handlers are identical either
    /// way, only the transport differs.
    void SetWebSocket(bool value) { _webSocket = value; }
    bool IsWebSocket() const { return _webSocket; }

    ServiceType GetType() const { return _type; }
    NetAddress GetNetAddress() const { return _netAddress; }
    int32 GetMaxSessionCount() const { return _maxSessionCount; }
    int32 GetCurrentSessionCount() const;

protected:
    ServiceType _type;
    NetAddress _netAddress;
    SessionFactory _sessionFactory;
    int32 _maxSessionCount = 0;
    bool _webSocket = false;

    mutable std::mutex _lock;
    std::unordered_set<SessionRef> _sessions;
};

using ServiceRef = Ref<Service>;

class Listener;

class ServerService : public Service {
public:
    ServerService(NetAddress address, SessionFactory factory, int32 maxSessionCount = 4096);

    bool Start() override;
    void CloseService() override;

private:
    Ref<Listener> _listener;
};

using ServerServiceRef = Ref<ServerService>;

class ClientService : public Service {
public:
    ClientService(NetAddress targetAddress, SessionFactory factory, int32 sessionCount = 1);

    bool Start() override;
};

using ClientServiceRef = Ref<ClientService>;

}  // namespace nhn
