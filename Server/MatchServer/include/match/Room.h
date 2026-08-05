#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Core.h"
#include "match/ClientSession.h"
#include "protocol/Packets.h"

namespace nhn::match {

/// A lobby room.
///
/// Derives from JobQueue: every mutation runs as a job, so exactly one thread
/// is ever inside a room. That removes the lock discipline the alternative
/// would need — kick, host migration and handoff all touch several members at
/// once, and doing that under per-member locks is where deadlocks come from.
///
/// Call the Enqueue* methods from packet handlers; the Handle* methods are the
/// bodies that run on the queue and assume exclusive access.
class Room : public JobQueue {
public:
    struct Member {
        ClientSessionRef session;
        SessionId sessionId = kInvalidSessionId;
        std::string nickname;
        uint8 slot = 0;
        bool ready = false;
        /// Monotonic counter used to pick the next host: the longest-present
        /// member inherits it.
        uint64 joinOrder = 0;
    };

    Room(RoomId id, std::string name, proto::RoomType type, std::string password);

    /// Reports the outcome of a join attempt.
    ///
    /// When supplied, the room stops sending the failure ack itself and leaves
    /// the decision to the caller — quick match uses this to try the next
    /// candidate instead of surfacing a race to the player.
    using JoinCallback = std::function<void(proto::ResultCode)>;

    // -- called from any thread ---------------------------------------------
    void EnqueueJoin(const ClientSessionRef& session, std::string password,
                     JoinCallback onComplete = nullptr);
    void EnqueueLeave(SessionId sessionId, proto::LeaveReason reason);
    void EnqueueKick(SessionId requesterId, SessionId targetId);
    void EnqueueReady(SessionId sessionId, bool ready);
    void EnqueueStart(SessionId requesterId);

    /// Handoff results, delivered from the match server's peer handlers.
    void EnqueueHandoffReady(InstanceId instanceId, const std::string& host, uint16 port,
                             const std::vector<std::pair<SessionId, std::string>>& tickets);
    void EnqueueHandoffFailed(proto::ResultCode reason);
    /// The instance reported every player connected; the match is live.
    void EnqueueInstanceRunning();
    void EnqueueInstanceFinished(const std::string& reason);

    /// Immutable identity, safe to read from anywhere.
    RoomId GetId() const { return _id; }
    proto::RoomType GetRoomType() const { return _roomType; }
    uint8 GetCapacity() const { return _capacity; }
    bool HasPassword() const { return !_password.empty(); }
    const std::string& GetName() const { return _name; }

private:
    // -- run on the queue ----------------------------------------------------
    void HandleJoin(const ClientSessionRef& session, const std::string& password,
                    const JoinCallback& onComplete);
    void HandleLeave(SessionId sessionId, proto::LeaveReason reason);
    void HandleKick(SessionId requesterId, SessionId targetId);
    void HandleReady(SessionId sessionId, bool ready);
    void HandleStart(SessionId requesterId);

    Member* FindMember(SessionId sessionId);
    uint8 AllocateSlot() const;
    void PromoteNextHost();
    void CloseRoom(const std::string& reason);
    void SetState(proto::RoomState state);

    /// Mirrors the room into the manager's search index. Called at the end of
    /// every mutation so browsing never has to enter a room's queue.
    void PublishSummary();

    proto::RoomDetail BuildDetail() const;
    proto::RoomMemberInfo BuildMemberInfo(const Member& member) const;

    void Broadcast(SendBufferRef sendBuffer, SessionId exceptSessionId = kInvalidSessionId);

    /// True when everyone but the host has readied up. The host expresses
    /// readiness by pressing start.
    bool AllNonHostMembersReady() const;

    const RoomId _id;
    const std::string _name;
    const proto::RoomType _roomType;
    const uint8 _capacity;
    /// Stored as given. Rooms are ephemeral and there is no account system to
    /// protect, so this is a door code rather than a credential — but it does
    /// travel in the clear, which is noted in the design docs.
    const std::string _password;

    std::vector<Member> _members;
    SessionId _hostSessionId = kInvalidSessionId;
    proto::RoomState _state = proto::RoomState::Waiting;
    uint64 _nextJoinOrder = 1;

    /// Session -> tick until which they may not rejoin. Without this a kicked
    /// player simply reconnects immediately and the host has no recourse.
    std::unordered_map<SessionId, TickCount> _kickCooldowns;

    InstanceId _pendingInstanceId = kInvalidInstanceId;
};

using RoomRef = Ref<Room>;

}  // namespace nhn::match
