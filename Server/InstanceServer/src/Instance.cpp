#include "instance/Instance.h"

#include <algorithm>

#include "instance/InstanceServer.h"

namespace nhn::instance {

using namespace proto;

namespace {

template <class Fn>
void PostToInstance(Instance* instance, Fn&& body) {
    InstanceRef self = std::static_pointer_cast<Instance>(instance->shared_from_this());
    instance->DoAsync([self, body = std::forward<Fn>(body)]() { body(self); });
}

}  // namespace

Instance::Instance(InstanceId id, RoomId roomId, GameMode mode, MatchConfig config,
                   const std::vector<RoomMemberInfo>& expected, Ref<IGameMode> gameMode,
                   uint32 joinTimeoutMs, uint32 tickIntervalMs)
    : _id(id),
      _roomId(roomId),
      _mode(mode),
      _config(config),
      _joinTimeoutMs(joinTimeoutMs),
      _tickIntervalMs(tickIntervalMs),
      _gameMode(std::move(gameMode)) {
    _members.reserve(expected.size());
    for (const RoomMemberInfo& info : expected) {
        Member member;
        member.sessionId = info.sessionId;
        member.nickname = info.nickname;
        member.slot = info.slot;
        member.connected = false;
        _members.push_back(std::move(member));
    }
}

void Instance::StartJoinTimeout() {
    InstanceRef self = std::static_pointer_cast<Instance>(shared_from_this());
    DoTimer(_joinTimeoutMs, [self]() { self->HandleJoinTimeout(); });
}

void Instance::EnqueuePlayerConnected(const InstanceSessionRef& session, SessionId sessionId) {
    PostToInstance(this, [session, sessionId](const InstanceRef& self) {
        self->HandlePlayerConnected(session, sessionId);
    });
}

void Instance::EnqueuePlayerLeft(SessionId sessionId, LeaveReason reason) {
    PostToInstance(this, [sessionId, reason](const InstanceRef& self) {
        self->HandlePlayerLeft(sessionId, reason);
    });
}

void Instance::EnqueueClose(const std::string& reason) {
    PostToInstance(this, [reason](const InstanceRef& self) { self->HandleClose(reason); });
}

void Instance::EnqueueFire(SessionId sessionId, const C_Fire& packet) {
    PostToInstance(this, [sessionId, packet](const InstanceRef& self) {
        if (self->_state.load(std::memory_order_acquire) != State::Running) {
            return;
        }
        self->_gameMode->OnFire(*self, sessionId, packet);
    });
}

void Instance::HandlePlayerConnected(const InstanceSessionRef& session, SessionId sessionId) {
    if (_state.load(std::memory_order_acquire) == State::Finished) {
        session->Disconnect("instance already finished");
        return;
    }

    Member* member = FindMember(sessionId);
    if (member == nullptr) {
        // A valid ticket for a different instance, or a stale one.
        session->Disconnect("not a member of this instance");
        return;
    }

    member->session = session;
    member->connected = true;
    session->SetInstanceId(_id);

    S_InstHelloAck ack;
    ack.result = ResultCode::Ok;
    ack.instanceId = _id;
    ack.slot = member->slot;
    ack.members = BuildRoster();
    session->SendPacket(ack);

    S_InstMemberJoined joined;
    joined.member.sessionId = member->sessionId;
    joined.member.nickname = member->nickname;
    joined.member.slot = member->slot;
    Broadcast(MakePacket(joined), sessionId);

    LOG_INFO("instance {}: {} connected ({}/{})", _id, member->nickname,
             std::count_if(_members.begin(), _members.end(),
                           [](const Member& m) { return m.connected; }),
             _members.size());

    if (AllConnected()) {
        BeginMatch();
    }
}

void Instance::BeginMatch() {
    State expected = State::AwaitingPlayers;
    if (!_state.compare_exchange_strong(expected, State::Running)) {
        return;
    }

    S_InstStart start;
    start.serverTimeMs = NowUnixMs();
    Broadcast(MakePacket(start));

    LOG_INFO("instance {} started with {} players (mode '{}')", _id, _members.size(),
             _gameMode->Name());

    GInstanceServer->ReportInstanceReady(_id);

    _gameMode->OnStart(*this);
    _lastTickAt = NowTick();
    ScheduleTick();
}

void Instance::ScheduleTick() {
    if (_state.load(std::memory_order_acquire) != State::Running) {
        return;
    }
    InstanceRef self = std::static_pointer_cast<Instance>(shared_from_this());
    DoTimer(_tickIntervalMs, [self]() { self->HandleTick(); });
}

void Instance::HandleTick() {
    if (_state.load(std::memory_order_acquire) != State::Running) {
        return;
    }

    const TickCount now = NowTick();
    const uint64 delta = now - _lastTickAt;
    _lastTickAt = now;

    _gameMode->OnTick(*this, delta);

    ScheduleTick();
}

void Instance::HandlePlayerLeft(SessionId sessionId, LeaveReason reason) {
    Member* member = FindMember(sessionId);
    if (member == nullptr || !member->connected) {
        return;
    }

    member->connected = false;
    member->session = nullptr;

    S_InstMemberLeft left;
    left.sessionId = sessionId;
    left.reason = reason;
    Broadcast(MakePacket(left));

    LOG_INFO("instance {}: session {} left ({})", _id, sessionId, ToString(reason));

    if (_state.load(std::memory_order_acquire) == State::Running) {
        _gameMode->OnMemberLeft(*this, sessionId);
    }

    const bool anyoneLeft =
        std::any_of(_members.begin(), _members.end(), [](const Member& m) { return m.connected; });
    if (!anyoneLeft) {
        HandleClose("all players left");
    }
}

void Instance::HandleJoinTimeout() {
    if (_state.load(std::memory_order_acquire) != State::AwaitingPlayers) {
        return;  // Everyone made it; the deadline is moot.
    }

    const auto missing = std::count_if(_members.begin(), _members.end(),
                                       [](const Member& m) { return !m.connected; });
    LOG_WARN("instance {}: {} of {} players never connected", _id, missing, _members.size());

    // Abandoning is the right call over starting short-handed: these are small
    // fixed-size arcade matches, and the match server returns everyone to the
    // room where the host can retry.
    HandleClose("players failed to connect in time");
}

void Instance::HandleClose(const std::string& reason) {
    const State previous = _state.exchange(State::Finished, std::memory_order_acq_rel);
    if (previous == State::Finished) {
        return;
    }

    if (previous == State::Running) {
        _gameMode->OnEnd(*this, reason);
    }

    S_InstEnd end;
    end.reason = reason;
    Broadcast(MakePacket(end));

    for (Member& member : _members) {
        if (member.session != nullptr) {
            member.session->Disconnect("instance finished");
            member.session = nullptr;
        }
        member.connected = false;
        GInstanceManager->ForgetSession(member.sessionId);
    }

    LOG_INFO("instance {} finished: {}", _id, reason);

    GInstanceServer->ReportInstanceClosed(_id, reason);
    GInstanceManager->Remove(_id);
}

Instance::Member* Instance::FindMember(SessionId sessionId) {
    const auto it = std::find_if(_members.begin(), _members.end(),
                                 [sessionId](const Member& m) { return m.sessionId == sessionId; });
    return it != _members.end() ? &(*it) : nullptr;
}

bool Instance::AllConnected() const {
    return std::all_of(_members.begin(), _members.end(),
                       [](const Member& m) { return m.connected; });
}

std::vector<RoomMemberInfo> Instance::BuildRoster() const {
    std::vector<RoomMemberInfo> roster;
    roster.reserve(_members.size());
    for (const Member& member : _members) {
        RoomMemberInfo info;
        info.sessionId = member.sessionId;
        info.nickname = member.nickname;
        info.slot = member.slot;
        info.isHost = false;  // Host authority ends at the lobby.
        info.isReady = member.connected;
        roster.push_back(std::move(info));
    }
    return roster;
}

void Instance::Broadcast(SendBufferRef sendBuffer, SessionId exceptSessionId) {
    if (sendBuffer == nullptr) {
        return;
    }
    for (const Member& member : _members) {
        if (!member.connected || member.session == nullptr ||
            member.sessionId == exceptSessionId) {
            continue;
        }
        member.session->Send(sendBuffer);
    }
}

}  // namespace nhn::instance
