#pragma once

#include <algorithm>
#include <array>
#include <vector>

#include "core/Core.h"
#include "match/ClientSession.h"
#include "protocol/Types.h"

namespace nhn::match {

/// Players waiting for a quick match, one list per mode.
///
/// There is no lobby behind this. A player picks a mode, waits here, and the
/// next thing they see is the game — so the queue only has to answer "who is
/// waiting for what", and the match server does the rest.
///
/// Deliberately unlocked: every method runs on the match server's job queue,
/// the same discipline the handoff bookkeeping uses. Calling these from
/// anywhere else is a bug.
class MatchQueue {
public:
    struct Waiting {
        ClientSessionRef session;
        SessionId sessionId = kInvalidSessionId;
    };

    /// @return false when the mode is not one that can be queued for.
    bool Add(proto::GameMode mode, const ClientSessionRef& session) {
        std::vector<Waiting>* list = Find(mode);
        if (list == nullptr || session == nullptr) {
            return false;
        }
        list->push_back(Waiting{session, session->GetSessionId()});
        return true;
    }

    /// @return the mode the session was waiting for, or None if it was not.
    proto::GameMode Remove(SessionId sessionId) {
        for (size_t index = 0; index < _byMode.size(); ++index) {
            std::vector<Waiting>& list = _byMode[index];
            const auto it = std::find_if(list.begin(), list.end(), [sessionId](const Waiting& w) {
                return w.sessionId == sessionId;
            });
            if (it != list.end()) {
                list.erase(it);
                return static_cast<proto::GameMode>(index);
            }
        }
        return proto::GameMode::None;
    }

    size_t Count(proto::GameMode mode) const {
        const std::vector<Waiting>* list = Find(mode);
        return list != nullptr ? list->size() : 0;
    }

    const std::vector<Waiting>& Entries(proto::GameMode mode) const {
        const std::vector<Waiting>* list = Find(mode);
        return list != nullptr ? *list : _empty;
    }

    /// Removes and returns up to @p maxCount waiters, oldest first, so nobody
    /// queues behind a later arrival.
    std::vector<Waiting> Take(proto::GameMode mode, size_t maxCount) {
        std::vector<Waiting> taken;
        std::vector<Waiting>* list = Find(mode);
        if (list == nullptr) {
            return taken;
        }

        const size_t count = std::min(maxCount, list->size());
        taken.assign(list->begin(), list->begin() + static_cast<ptrdiff_t>(count));
        list->erase(list->begin(), list->begin() + static_cast<ptrdiff_t>(count));
        return taken;
    }

    /// Forgets anyone whose connection has gone since they queued. Sweeping on
    /// use as well as on disconnect means a socket that died without a clean
    /// close cannot hold a match open.
    void PruneDisconnected(proto::GameMode mode) {
        std::vector<Waiting>* list = Find(mode);
        if (list == nullptr) {
            return;
        }
        list->erase(std::remove_if(list->begin(), list->end(),
                                   [](const Waiting& w) {
                                       return w.session == nullptr || !w.session->IsConnected();
                                   }),
                    list->end());
    }

private:
    std::vector<Waiting>* Find(proto::GameMode mode) {
        const auto index = static_cast<size_t>(mode);
        return index < _byMode.size() ? &_byMode[index] : nullptr;
    }

    const std::vector<Waiting>* Find(proto::GameMode mode) const {
        const auto index = static_cast<size_t>(mode);
        return index < _byMode.size() ? &_byMode[index] : nullptr;
    }

    /// Indexed by GameMode; slot 0 (None) stays empty.
    std::array<std::vector<Waiting>, 4> _byMode;
    std::vector<Waiting> _empty;
};

}  // namespace nhn::match
