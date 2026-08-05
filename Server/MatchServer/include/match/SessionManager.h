#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "match/ClientSession.h"

namespace nhn::match {

/// Directory of authenticated players.
///
/// Ids are handed out sequentially from 1 and are never reused within a run.
/// That is fine to expose — a session id is not a credential, and everything
/// that *is* one (the chat and voice tickets) comes from a CSPRNG instead.
class SessionManager {
public:
    /// Assigns the next id and records the session.
    SessionId Register(const ClientSessionRef& session);
    void Unregister(SessionId sessionId);

    ClientSessionRef Find(SessionId sessionId) const;
    int32 Count() const;

    void ForEach(const std::function<void(const ClientSessionRef&)>& fn) const;

private:
    mutable std::mutex _lock;
    std::unordered_map<SessionId, ClientSessionRef> _sessions;
    std::atomic<SessionId> _nextSessionId{1};
};

}  // namespace nhn::match
