#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "core/Types.h"
#include "protocol/Types.h"

namespace nhn {

/// Pre-authorised connections, as pushed by the match server.
///
/// The whole ticket scheme rests on three properties, all enforced here:
///   * unguessable — the value comes from a CSPRNG, see Random::Ticket
///   * short-lived — an unused ticket expires within seconds
///   * single-use  — consuming one removes it
///
/// Drop any of those and a plaintext ticket becomes a room number that anyone
/// can guess their way into. There is no signature to fall back on.
class TicketTable {
public:
    struct Entry {
        SessionId sessionId = kInvalidSessionId;
        std::string nickname;
        int64 expiresAtMs = 0;
    };

    void Add(const std::string& ticket, SessionId sessionId, std::string nickname,
             int64 expiresAtMs);
    void Revoke(const std::string& ticket);

    /// Validates and removes the ticket in one step.
    /// @param outReason distinguishes "never existed" from "too late", which
    ///        matters when diagnosing a slow client.
    bool Consume(const std::string& ticket, int64 nowMs, Entry& out, proto::ResultCode& outReason);

    /// Drops tickets nobody redeemed. Called on a timer; without it an unused
    /// ticket would sit in the map for the lifetime of the process.
    size_t PurgeExpired(int64 nowMs);

    size_t Size() const;
    void Clear();

private:
    mutable std::mutex _mutex;
    std::unordered_map<std::string, Entry> _tickets;
};

}  // namespace nhn
