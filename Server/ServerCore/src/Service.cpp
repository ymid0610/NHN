#include "core/Service.h"

#include "core/CoreGlobal.h"
#include "core/Listener.h"
#include "core/Log.h"

namespace nhn {

Service::Service(ServiceType type, NetAddress address, SessionFactory factory,
                 int32 maxSessionCount)
    : _type(type),
      _netAddress(address),
      _sessionFactory(std::move(factory)),
      _maxSessionCount(maxSessionCount) {}

Service::~Service() = default;

void Service::CloseService() {
    std::vector<SessionRef> sessions;
    {
        std::lock_guard<std::mutex> guard(_lock);
        sessions.assign(_sessions.begin(), _sessions.end());
    }
    for (const SessionRef& session : sessions) {
        session->Disconnect("service shutting down");
    }
}

SessionRef Service::CreateSession() {
    SessionRef session = _sessionFactory();
    if (session == nullptr) {
        return nullptr;
    }
    session->SetService(shared_from_this());
    return session;
}

void Service::AddSession(SessionRef session) {
    std::lock_guard<std::mutex> guard(_lock);
    _sessions.insert(std::move(session));
}

void Service::ReleaseSession(SessionRef session) {
    std::lock_guard<std::mutex> guard(_lock);
    _sessions.erase(session);
}

int32 Service::GetCurrentSessionCount() const {
    std::lock_guard<std::mutex> guard(_lock);
    return static_cast<int32>(_sessions.size());
}

void Service::Broadcast(SendBufferRef sendBuffer) {
    ForEachSession([&sendBuffer](const SessionRef& session) { session->Send(sendBuffer); });
}

void Service::ForEachSession(const std::function<void(const SessionRef&)>& fn) {
    // Snapshot first: handlers routinely disconnect sessions, which mutates the
    // set from inside the loop.
    std::vector<SessionRef> snapshot;
    {
        std::lock_guard<std::mutex> guard(_lock);
        snapshot.assign(_sessions.begin(), _sessions.end());
    }
    for (const SessionRef& session : snapshot) {
        fn(session);
    }
}

// ---------------------------------------------------------------------------
// ServerService
// ---------------------------------------------------------------------------

ServerService::ServerService(NetAddress address, SessionFactory factory, int32 maxSessionCount)
    : Service(ServiceType::Server, address, std::move(factory), maxSessionCount) {}

bool ServerService::Start() {
    if (!CanStart()) {
        LOG_ERROR("server service has no session factory");
        return false;
    }

    _listener = std::make_shared<Listener>();
    return _listener->StartAccept(
        std::static_pointer_cast<ServerService>(shared_from_this()));
}

void ServerService::CloseService() {
    if (_listener != nullptr) {
        _listener->CloseSocket();
    }
    Service::CloseService();
}

// ---------------------------------------------------------------------------
// ClientService
// ---------------------------------------------------------------------------

ClientService::ClientService(NetAddress targetAddress, SessionFactory factory, int32 sessionCount)
    : Service(ServiceType::Client, targetAddress, std::move(factory), sessionCount) {}

bool ClientService::Start() {
    if (!CanStart()) {
        LOG_ERROR("client service has no session factory");
        return false;
    }

    for (int32 i = 0; i < GetMaxSessionCount(); ++i) {
        SessionRef session = CreateSession();
        if (session == nullptr || !session->Connect()) {
            return false;
        }
    }
    return true;
}

}  // namespace nhn
