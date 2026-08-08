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
        /// 0 for a person, 1..5 for a bot. A bot has no session, so every send
        /// path must tolerate a null one — Broadcast already does.
        uint8 botDifficulty = 0;

        /// Monotonic counter used to pick the next host: the longest-present
        /// member inherits it.
        uint64 joinOrder = 0;

        bool IsBot() const { return botDifficulty != 0; }
    };

    /// @param listed false for a quick-match party. The object is still a room
    ///               internally — it already knows how to track members, freeze
    ///               a roster and recover from a failed handoff — but it never
    ///               publishes a summary, so it cannot be listed, searched or
    ///               joined by anyone the matchmaker did not put there.
    Room(RoomId id, std::string name, proto::GameMode type, std::string password,
         bool listed = true);

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

    /// Starts without a host and without waiting for anyone to ready up.
    ///
    /// Quick match only: the players were assembled by the matchmaker rather
    /// than gathered in a lobby, so there is nobody to press start and nothing
    /// to agree on.
    void EnqueueAutoStart();

    /// Host-only bot management. Bots hold a slot, count towards the minimum to
    /// start, and are always ready — there is nobody to press the button.
    void EnqueueAddBot(SessionId requesterId, uint8 difficulty);
    void EnqueueRemoveBot(SessionId requesterId, SessionId botSessionId);
    void EnqueueSetBotDifficulty(SessionId requesterId, SessionId botSessionId, uint8 difficulty);

    /// Host-only settings change. Whatever arrives is clamped to something the
    /// mode allows and then broadcast, so a stale client cannot wedge the room
    /// with an impossible combination.
    void EnqueueSetConfig(SessionId requesterId, proto::MatchConfig config);

    /// Handoff results, delivered from the match server's peer handlers.
    /// @param webPort browser-facing port; each member is told whichever of the
    ///                two its own connection can actually reach.
    void EnqueueHandoffReady(InstanceId instanceId, const std::string& host, uint16 port,
                             uint16 webPort,
                             const std::vector<std::pair<SessionId, std::string>>& tickets);
    void EnqueueHandoffFailed(proto::ResultCode reason);
    /// The instance reported every player connected; the match is live.
    void EnqueueInstanceRunning();
    void EnqueueInstanceFinished(const std::string& reason);

    /// Immutable identity, safe to read from anywhere.
    RoomId GetId() const { return _id; }
    proto::GameMode GetGameMode() const { return _mode; }
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
    void HandleAutoStart();
    /// Shared tail of both start paths: freeze the roster and hand off.
    void BeginStart();
    void HandleSetConfig(SessionId requesterId, proto::MatchConfig config);
    void HandleAddBot(SessionId requesterId, uint8 difficulty);
    void HandleRemoveBot(SessionId requesterId, SessionId botSessionId);
    void HandleSetBotDifficulty(SessionId requesterId, SessionId botSessionId, uint8 difficulty);
    /// Sends a bot failure to the requester alone; success is broadcast.
    void SendBotResult(SessionId requesterId, proto::ResultCode result,
                       const proto::RoomMemberInfo& bot) const;
    /// True when nobody with a connection is left. Bots must not keep a room
    /// alive after the last person has gone.
    bool HasHumanMembers() const;

    /// Builds the settings broadcast, including the item set that will really
    /// spawn under the current ammo rule.
    proto::S_RoomConfigChanged BuildConfigPacket(bool accepted) const;

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
    const proto::GameMode _mode;
    const uint8 _capacity;
    /// Stored as given. Rooms are ephemeral and there is no account system to
    /// protect, so this is a door code rather than a credential — but it does
    /// travel in the clear, which is noted in the design docs.
    const std::string _password;
    const bool _listed;

    std::vector<Member> _members;
    SessionId _hostSessionId = kInvalidSessionId;
    proto::RoomState _state = proto::RoomState::Waiting;
    uint64 _nextJoinOrder = 1;

    /// Session -> tick until which they may not rejoin. Without this a kicked
    /// player simply reconnects immediately and the host has no recourse.
    std::unordered_map<SessionId, TickCount> _kickCooldowns;

    /// Match settings. Only ever touched on this room's job queue.
    proto::MatchConfig _config;

    InstanceId _pendingInstanceId = kInvalidInstanceId;
};

using RoomRef = Ref<Room>;

}  // namespace nhn::match
