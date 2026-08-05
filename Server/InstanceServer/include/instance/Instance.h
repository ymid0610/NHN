#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/Core.h"
#include "protocol/Packets.h"

namespace nhn::instance {

class Instance;
class InstanceSession;
using InstanceSessionRef = Ref<InstanceSession>;

/// Where game logic will hang off.
///
/// The networking, handoff and lifecycle around a match are complete; the rules
/// of the match itself are not implemented yet. A mode is driven entirely from
/// the instance's job queue, so an implementation may treat the instance as
/// single-threaded.
class IGameMode {
public:
    virtual ~IGameMode() = default;

    /// Every expected player has connected.
    virtual void OnStart(Instance& instance) { (void)instance; }

    /// Fixed-rate tick while the match is running.
    virtual void OnTick(Instance& instance, uint64 deltaMs) {
        (void)instance;
        (void)deltaMs;
    }

    virtual void OnMemberLeft(Instance& instance, SessionId sessionId) {
        (void)instance;
        (void)sessionId;
    }

    /// A player pulled the trigger. Already on the instance's job queue, so the
    /// mode may treat its own state as single-threaded.
    virtual void OnFire(Instance& instance, SessionId sessionId, const proto::C_Fire& packet) {
        (void)instance;
        (void)sessionId;
        (void)packet;
    }

    virtual void OnEnd(Instance& instance, const std::string& reason) {
        (void)instance;
        (void)reason;
    }

    virtual const char* Name() const { return "none"; }
};

/// Placeholder mode: keeps the match alive and does nothing else.
class NullGameMode : public IGameMode {
public:
    const char* Name() const override { return "null"; }
};

/// One live match.
class Instance : public JobQueue {
public:
    struct Member {
        SessionId sessionId = kInvalidSessionId;
        std::string nickname;
        uint8 slot = 0;
        InstanceSessionRef session;  // null until the player connects
        bool connected = false;
    };

    enum class State : uint8 {
        AwaitingPlayers,
        Running,
        Finished,
    };

    Instance(InstanceId id, RoomId roomId, proto::GameMode mode, proto::MatchConfig config,
             const std::vector<proto::RoomMemberInfo>& expected, Ref<IGameMode> gameMode,
             uint32 joinTimeoutMs, uint32 tickIntervalMs);

    void EnqueuePlayerConnected(const InstanceSessionRef& session, SessionId sessionId);
    void EnqueuePlayerLeft(SessionId sessionId, proto::LeaveReason reason);
    void EnqueueClose(const std::string& reason);
    void EnqueueFire(SessionId sessionId, const proto::C_Fire& packet);

    /// Starts the join deadline. Called once the instance is registered.
    void StartJoinTimeout();

    InstanceId GetId() const { return _id; }
    RoomId GetRoomId() const { return _roomId; }
    proto::GameMode GetGameMode() const { return _mode; }
    const proto::MatchConfig& GetConfig() const { return _config; }
    uint32 GetTickIntervalMs() const { return _tickIntervalMs; }
    State GetState() const { return _state.load(std::memory_order_acquire); }

    /// For game modes: broadcast to every connected player.
    void Broadcast(SendBufferRef sendBuffer, SessionId exceptSessionId = kInvalidSessionId);
    const std::vector<Member>& GetMembers() const { return _members; }

private:
    void HandlePlayerConnected(const InstanceSessionRef& session, SessionId sessionId);
    void HandlePlayerLeft(SessionId sessionId, proto::LeaveReason reason);
    void HandleClose(const std::string& reason);
    void HandleJoinTimeout();
    void ScheduleTick();
    void HandleTick();

    Member* FindMember(SessionId sessionId);
    bool AllConnected() const;
    void BeginMatch();

    std::vector<proto::RoomMemberInfo> BuildRoster() const;

    const InstanceId _id;
    const RoomId _roomId;
    const proto::GameMode _mode;
    const proto::MatchConfig _config;
    const uint32 _joinTimeoutMs;
    const uint32 _tickIntervalMs;

    std::vector<Member> _members;
    std::atomic<State> _state{State::AwaitingPlayers};
    Ref<IGameMode> _gameMode;
    TickCount _lastTickAt = 0;
};

using InstanceRef = Ref<Instance>;

}  // namespace nhn::instance
