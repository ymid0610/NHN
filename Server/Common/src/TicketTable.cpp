#include "common/TicketTable.h"

#include "core/Log.h"

namespace nhn {

void TicketTable::Add(const std::string& ticket, SessionId sessionId, std::string nickname,
                      int64 expiresAtMs) {
    std::lock_guard<std::mutex> guard(_mutex);
    _tickets[ticket] = Entry{sessionId, std::move(nickname), expiresAtMs};
}

void TicketTable::Revoke(const std::string& ticket) {
    std::lock_guard<std::mutex> guard(_mutex);
    _tickets.erase(ticket);
}

bool TicketTable::Consume(const std::string& ticket, int64 nowMs, Entry& out,
                          proto::ResultCode& outReason) {
    std::lock_guard<std::mutex> guard(_mutex);

    const auto it = _tickets.find(ticket);
    if (it == _tickets.end()) {
        outReason = proto::ResultCode::TicketInvalid;
        return false;
    }

    if (nowMs > it->second.expiresAtMs) {
        // Remove it here too: an expired ticket has no further use, and leaving
        // it would let a slow attacker keep probing the same value.
        _tickets.erase(it);
        outReason = proto::ResultCode::TicketExpired;
        return false;
    }

    out = it->second;
    _tickets.erase(it);
    outReason = proto::ResultCode::Ok;
    return true;
}

size_t TicketTable::PurgeExpired(int64 nowMs) {
    std::lock_guard<std::mutex> guard(_mutex);

    size_t removed = 0;
    for (auto it = _tickets.begin(); it != _tickets.end();) {
        if (nowMs > it->second.expiresAtMs) {
            it = _tickets.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }

    if (removed > 0) {
        LOG_DEBUG("purged {} expired tickets", removed);
    }
    return removed;
}

size_t TicketTable::Size() const {
    std::lock_guard<std::mutex> guard(_mutex);
    return _tickets.size();
}

void TicketTable::Clear() {
    std::lock_guard<std::mutex> guard(_mutex);
    _tickets.clear();
}

}  // namespace nhn
