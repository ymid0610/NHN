#include "match/RoomManager.h"

#include <algorithm>

#include "core/Log.h"
#include "match/MatchGlobal.h"
#include "match/MatchServer.h"

namespace nhn::match {

using namespace proto;

namespace {

/// Case-insensitive substring test, so searching "arena" finds "Arena".
bool ContainsFold(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) {
        return true;
    }
    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                [](char a, char b) {
                                    return std::tolower(static_cast<unsigned char>(a)) ==
                                           std::tolower(static_cast<unsigned char>(b));
                                });
    return it != haystack.end();
}

}  // namespace

RoomRef RoomManager::Create(const std::string& name, GameMode mode,
                            const std::string& password, ResultCode& outResult) {
    if (!IsValidGameMode(mode)) {
        outResult = ResultCode::GameModeInvalid;
        return nullptr;
    }
    if (!IsCleanUtf8(name, kMaxRoomNameLength)) {
        outResult = ResultCode::RoomNameInvalid;
        return nullptr;
    }
    if (password.size() > kMaxPasswordLength) {
        outResult = ResultCode::InvalidRequest;
        return nullptr;
    }

    const RoomId roomId = _nextRoomId.fetch_add(1);
    RoomRef room = std::make_shared<Room>(roomId, name, mode, password);

    {
        std::lock_guard<std::mutex> guard(_lock);
        if (static_cast<int32>(_rooms.size()) >= GMatchServer->GetSettings().maxRooms) {
            outResult = ResultCode::ServerFull;
            return nullptr;
        }
        _rooms.emplace(roomId, room);
    }

    outResult = ResultCode::Ok;
    return room;
}

RoomRef RoomManager::Find(RoomId roomId) const {
    std::lock_guard<std::mutex> guard(_lock);
    const auto it = _rooms.find(roomId);
    return it != _rooms.end() ? it->second : nullptr;
}

void RoomManager::Remove(RoomId roomId) {
    std::lock_guard<std::mutex> guard(_lock);
    _rooms.erase(roomId);
    _summaries.erase(roomId);
}

void RoomManager::UpdateSummary(RoomId roomId, const RoomSummary& summary) {
    std::lock_guard<std::mutex> guard(_lock);
    // Only for rooms still registered: a summary published by the last job of a
    // closing room must not resurrect it in the listing.
    if (_rooms.find(roomId) == _rooms.end()) {
        return;
    }
    _summaries[roomId] = summary;
}

void RoomManager::Search(const C_RoomList& request, S_RoomList& out) const {
    const uint16 pageSize =
        request.pageSize == 0 ? uint16{20} : std::min(request.pageSize, kMaxPageSize);

    std::vector<RoomSummary> matches;
    {
        std::lock_guard<std::mutex> guard(_lock);
        matches.reserve(_summaries.size());
        for (const auto& [roomId, summary] : _summaries) {
            if (request.mode != GameMode::None && summary.mode != request.mode) {
                continue;
            }
            if (request.hideFull && summary.memberCount >= summary.capacity) {
                continue;
            }
            // Locked rooms stay visible by default: the requirement is that a
            // private room is findable and only *entry* costs a password.
            if (request.hideLocked && summary.hasPassword) {
                continue;
            }
            if (!ContainsFold(summary.name, request.nameFilter)) {
                continue;
            }
            matches.push_back(summary);
        }
    }

    out.totalCount = static_cast<uint32>(matches.size());
    out.page = request.page;

    const size_t begin = static_cast<size_t>(request.page) * pageSize;
    if (begin >= matches.size()) {
        return;
    }
    const size_t end = std::min(begin + pageSize, matches.size());
    out.rooms.assign(matches.begin() + static_cast<ptrdiff_t>(begin),
                     matches.begin() + static_cast<ptrdiff_t>(end));
}

std::vector<RoomId> RoomManager::FindQuickMatchCandidates(GameMode mode, size_t limit) const {
    std::vector<const RoomSummary*> matches;
    std::vector<RoomId> result;

    std::lock_guard<std::mutex> guard(_lock);

    for (const auto& [roomId, summary] : _summaries) {
        if (summary.mode != mode) {
            continue;
        }
        if (summary.state != RoomState::Waiting) {
            continue;
        }
        if (summary.hasPassword) {
            continue;
        }
        if (summary.memberCount >= summary.capacity) {
            continue;
        }
        matches.push_back(&summary);
    }

    // Fullest first, so a room one player short gets completed before an empty
    // one is offered.
    std::sort(matches.begin(), matches.end(),
              [](const RoomSummary* a, const RoomSummary* b) {
                  if (a->memberCount != b->memberCount) {
                      return a->memberCount > b->memberCount;
                  }
                  return a->roomId < b->roomId;  // stable across calls
              });

    result.reserve(std::min(limit, matches.size()));
    for (const RoomSummary* summary : matches) {
        if (result.size() >= limit) {
            break;
        }
        result.push_back(summary->roomId);
    }
    return result;
}

int32 RoomManager::Count() const {
    std::lock_guard<std::mutex> guard(_lock);
    return static_cast<int32>(_rooms.size());
}

void RoomManager::ForEachRoom(const std::function<void(const RoomRef&)>& fn) const {
    std::vector<RoomRef> snapshot;
    {
        std::lock_guard<std::mutex> guard(_lock);
        snapshot.reserve(_rooms.size());
        for (const auto& [roomId, room] : _rooms) {
            snapshot.push_back(room);
        }
    }
    for (const RoomRef& room : snapshot) {
        fn(room);
    }
}

}  // namespace nhn::match
