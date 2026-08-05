#include "match/SessionManager.h"

#include <vector>

namespace nhn::match {

SessionId SessionManager::Register(const ClientSessionRef& session) {
    const SessionId sessionId = _nextSessionId.fetch_add(1);
    session->SetSessionId(sessionId);

    std::lock_guard<std::mutex> guard(_lock);
    _sessions[sessionId] = session;
    return sessionId;
}

void SessionManager::Unregister(SessionId sessionId) {
    std::lock_guard<std::mutex> guard(_lock);
    _sessions.erase(sessionId);
}

ClientSessionRef SessionManager::Find(SessionId sessionId) const {
    std::lock_guard<std::mutex> guard(_lock);
    const auto it = _sessions.find(sessionId);
    return it != _sessions.end() ? it->second : nullptr;
}

int32 SessionManager::Count() const {
    std::lock_guard<std::mutex> guard(_lock);
    return static_cast<int32>(_sessions.size());
}

void SessionManager::ForEach(const std::function<void(const ClientSessionRef&)>& fn) const {
    std::vector<ClientSessionRef> snapshot;
    {
        std::lock_guard<std::mutex> guard(_lock);
        snapshot.reserve(_sessions.size());
        for (const auto& [sessionId, session] : _sessions) {
            snapshot.push_back(session);
        }
    }
    for (const ClientSessionRef& session : snapshot) {
        fn(session);
    }
}

}  // namespace nhn::match
