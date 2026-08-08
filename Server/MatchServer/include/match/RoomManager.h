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
    /// @param listed false creates an unlisted quick-match party — see Room's
    ///               constructor. It never enters the search index, so it
    ///               cannot be found or joined from outside.
    RoomRef Create(const std::string& name, proto::GameMode mode, const std::string& password,
                   proto::ResultCode& outResult, bool listed = true);

    RoomRef Find(RoomId roomId) const;
    void Remove(RoomId roomId);

    void UpdateSummary(RoomId roomId, const proto::RoomSummary& summary);

    void Search(const proto::C_RoomList& request, proto::S_RoomList& out) const;

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
