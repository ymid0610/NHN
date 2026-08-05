#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>

#include "match/Room.h"
#include "protocol/Packets.h"

namespace nhn::match {

/// Owns every live room and the search index over them.
///
/// Rooms serialise their own state on their job queues, which means a search
/// cannot read them directly without entering each queue in turn. Instead every
/// room publishes a summary here whenever it changes, and browsing reads only
/// this index — so listing a thousand rooms costs one lock, not a thousand
/// queue round-trips.
class RoomManager {
public:
    RoomRef Create(const std::string& name, proto::GameMode mode, const std::string& password,
                   proto::ResultCode& outResult);

    RoomRef Find(RoomId roomId) const;
    void Remove(RoomId roomId);

    void UpdateSummary(RoomId roomId, const proto::RoomSummary& summary);

    void Search(const proto::C_RoomList& request, proto::S_RoomList& out) const;

    /// Rooms a quick-match player could drop into, best first.
    ///
    /// Ordered fullest-first rather than emptiest-first: filling one room to
    /// capacity starts a game, whereas spreading players evenly leaves several
    /// rooms one player short and nobody playing.
    ///
    /// Locked rooms are excluded — quick match cannot supply a password — as
    /// are rooms already in or entering a match.
    std::vector<RoomId> FindQuickMatchCandidates(proto::GameMode mode, size_t limit) const;

    int32 Count() const;

    /// Disconnect-time sweep: rooms whose members all vanished.
    void ForEachRoom(const std::function<void(const RoomRef&)>& fn) const;

private:
    static constexpr uint16 kMaxPageSize = 100;

    mutable std::mutex _lock;
    std::unordered_map<RoomId, RoomRef> _rooms;
    /// Ordered by id so paging is stable between requests even as rooms come
    /// and go.
    std::map<RoomId, proto::RoomSummary> _summaries;
    std::atomic<RoomId> _nextRoomId{1};
};

}  // namespace nhn::match
