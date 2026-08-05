#include "match/Room.h"

#include <algorithm>

#include "match/MatchGlobal.h"
#include "match/MatchServer.h"
#include "match/RoomManager.h"

namespace nhn::match {

using namespace proto;

namespace {

/// Captures a strong reference to the room so a job can never outlive it.
template <class Fn>
void PostToRoom(Room* room, Fn&& body) {
    RoomRef self = std::static_pointer_cast<Room>(room->shared_from_this());
    room->DoAsync([self, body = std::forward<Fn>(body)]() { body(self); });
}

}  // namespace

Room::Room(RoomId id, std::string name, GameMode type, std::string password)
    : _id(id),
      _name(std::move(name)),
      _mode(type),
      _capacity(ModeCapacity(type)),
      _password(std::move(password)) {
    // Defaults have to be legal for this mode before anyone touches them —
    // ammo and wave-cap choices differ per mode.
    ClampMatchConfig(_mode, _config);
}

// ---------------------------------------------------------------------------
// Enqueue: callable from any thread
// ---------------------------------------------------------------------------

void Room::EnqueueJoin(const ClientSessionRef& session, std::string password,
                       JoinCallback onComplete) {
    PostToRoom(this, [session, password = std::move(password),
                      onComplete = std::move(onComplete)](const RoomRef& self) {
        self->HandleJoin(session, password, onComplete);
    });
}

void Room::EnqueueLeave(SessionId sessionId, LeaveReason reason) {
    PostToRoom(this, [sessionId, reason](const RoomRef& self) {
        self->HandleLeave(sessionId, reason);
    });
}

void Room::EnqueueKick(SessionId requesterId, SessionId targetId) {
    PostToRoom(this, [requesterId, targetId](const RoomRef& self) {
        self->HandleKick(requesterId, targetId);
    });
}

void Room::EnqueueReady(SessionId sessionId, bool ready) {
    PostToRoom(this,
               [sessionId, ready](const RoomRef& self) { self->HandleReady(sessionId, ready); });
}

void Room::EnqueueStart(SessionId requesterId) {
    PostToRoom(this, [requesterId](const RoomRef& self) { self->HandleStart(requesterId); });
}

void Room::EnqueueSetConfig(SessionId requesterId, MatchConfig config) {
    PostToRoom(this, [requesterId, config](const RoomRef& self) {
        self->HandleSetConfig(requesterId, config);
    });
}

// ---------------------------------------------------------------------------
// Join / leave
// ---------------------------------------------------------------------------

void Room::HandleJoin(const ClientSessionRef& session, const std::string& password,
                      const JoinCallback& onComplete) {
    const SessionId sessionId = session->GetSessionId();

    // With a callback the caller owns the failure response; without one the
    // player is told directly.
    auto reject = [&session, &onComplete](ResultCode result) {
        if (onComplete) {
            onComplete(result);
            return;
        }
        S_RoomJoinAck ack;
        ack.result = result;
        session->SendPacket(ack);
    };

    if (_state != RoomState::Waiting) {
        reject(ResultCode::RoomNotWaiting);
        return;
    }

    // Checked before the password so a kicked player cannot distinguish a wrong
    // password from their own cooldown.
    const auto cooldown = _kickCooldowns.find(sessionId);
    if (cooldown != _kickCooldowns.end()) {
        if (NowTick() < cooldown->second) {
            reject(ResultCode::KickCooldown);
            return;
        }
        _kickCooldowns.erase(cooldown);
    }

    if (HasPassword() && password != _password) {
        reject(password.empty() ? ResultCode::PasswordRequired : ResultCode::WrongPassword);
        return;
    }

    if (static_cast<uint8>(_members.size()) >= _capacity) {
        reject(ResultCode::RoomFull);
        return;
    }

    if (FindMember(sessionId) != nullptr) {
        reject(ResultCode::AlreadyInRoom);
        return;
    }

    Member member;
    member.session = session;
    member.sessionId = sessionId;
    member.nickname = session->GetNickname();
    member.slot = AllocateSlot();
    member.ready = false;
    member.joinOrder = _nextJoinOrder++;

    const bool isFirstMember = _members.empty();
    _members.push_back(member);
    if (isFirstMember) {
        _hostSessionId = sessionId;
    }

    session->SetRoomId(_id);

    // Existing members learn about the newcomer; the newcomer gets the full
    // roster in its ack.
    S_RoomMemberJoined joined;
    joined.member = BuildMemberInfo(_members.back());
    Broadcast(MakePacket(joined), sessionId);

    S_RoomJoinAck ack;
    ack.result = ResultCode::Ok;
    ack.room = BuildDetail();
    session->SendPacket(ack);

    // The joiner needs the current settings; everyone else already has them.
    session->SendPacket(BuildConfigPacket(true));

    GMatchServer->PushChannelJoin(sessionId, ChannelType::Room, _id);

    LOG_INFO("room {} '{}': {} joined ({}/{})", _id, _name, member.nickname, _members.size(),
             _capacity);
    PublishSummary();

    if (onComplete) {
        onComplete(ResultCode::Ok);
    }
}

void Room::HandleLeave(SessionId sessionId, LeaveReason reason) {
    const auto it = std::find_if(_members.begin(), _members.end(),
                                 [sessionId](const Member& m) { return m.sessionId == sessionId; });
    if (it == _members.end()) {
        return;
    }

    const std::string nickname = it->nickname;
    ClientSessionRef leaving = it->session;
    const bool wasHost = (_hostSessionId == sessionId);

    _members.erase(it);

    if (leaving != nullptr && leaving->GetRoomId() == _id) {
        leaving->SetRoomId(kInvalidRoomId);
    }

    GMatchServer->PushChannelLeave(sessionId, ChannelType::Room, _id);

    S_RoomMemberLeft left;
    left.sessionId = sessionId;
    left.reason = reason;
    Broadcast(MakePacket(left));

    LOG_INFO("room {} '{}': {} left ({}) — {} remain", _id, _name, nickname, ToString(reason),
             _members.size());

    if (_members.empty()) {
        CloseRoom("last member left");
        return;
    }

    if (wasHost) {
        PromoteNextHost();
    }

    // A start that was waiting on this player can no longer succeed.
    if (_state == RoomState::Starting) {
        LOG_WARN("room {}: member left during handoff; returning to lobby", _id);
        SetState(RoomState::Waiting);
        _pendingInstanceId = kInvalidInstanceId;
    }

    PublishSummary();
}

void Room::HandleKick(SessionId requesterId, SessionId targetId) {
    ClientSessionRef requester;
    if (const Member* member = FindMember(requesterId)) {
        requester = member->session;
    }

    auto reply = [&requester, targetId](ResultCode result) {
        if (requester == nullptr) {
            return;
        }
        S_RoomKickAck ack;
        ack.result = result;
        ack.targetSessionId = targetId;
        requester->SendPacket(ack);
    };

    if (_hostSessionId != requesterId) {
        reply(ResultCode::NotHost);
        return;
    }
    if (requesterId == targetId) {
        reply(ResultCode::CannotTargetSelf);
        return;
    }
    if (_state != RoomState::Waiting) {
        reply(ResultCode::RoomNotWaiting);
        return;
    }

    const Member* target = FindMember(targetId);
    if (target == nullptr) {
        reply(ResultCode::TargetNotFound);
        return;
    }

    const uint32 cooldownMs = GMatchServer->GetSettings().kickCooldownMs;
    _kickCooldowns[targetId] = NowTick() + cooldownMs;

    if (target->session != nullptr) {
        S_RoomKicked kicked;
        kicked.roomId = _id;
        kicked.cooldownSeconds = cooldownMs / 1000;
        target->session->SendPacket(kicked);
    }

    reply(ResultCode::Ok);

    LOG_INFO("room {} '{}': host kicked {}", _id, _name, target->nickname);
    HandleLeave(targetId, LeaveReason::Kicked);
}

// ---------------------------------------------------------------------------
// Ready / start
// ---------------------------------------------------------------------------

void Room::HandleReady(SessionId sessionId, bool ready) {
    Member* member = FindMember(sessionId);
    if (member == nullptr) {
        return;
    }

    auto reply = [&member](ResultCode result, bool value) {
        if (member->session != nullptr) {
            S_RoomReadyAck ack;
            ack.result = result;
            ack.ready = value;
            member->session->SendPacket(ack);
        }
    };

    if (_state != RoomState::Waiting) {
        reply(ResultCode::RoomNotWaiting, member->ready);
        return;
    }

    member->ready = ready;
    reply(ResultCode::Ok, ready);

    S_RoomMemberReady changed;
    changed.sessionId = sessionId;
    changed.ready = ready;
    Broadcast(MakePacket(changed));
}

void Room::HandleStart(SessionId requesterId) {
    ClientSessionRef requester;
    if (const Member* member = FindMember(requesterId)) {
        requester = member->session;
    }

    auto reply = [&requester](ResultCode result) {
        if (requester != nullptr) {
            S_RoomStartAck ack;
            ack.result = result;
            requester->SendPacket(ack);
        }
    };

    if (_hostSessionId != requesterId) {
        reply(ResultCode::NotHost);
        return;
    }
    if (_state != RoomState::Waiting) {
        reply(ResultCode::RoomNotWaiting);
        return;
    }

    const GameModeDef* info = FindGameMode(_mode);
    if (info == nullptr) {
        reply(ResultCode::GameModeInvalid);
        return;
    }
    if (static_cast<uint8>(_members.size()) < info->minimumToStart) {
        reply(ResultCode::NotEnoughPlayers);
        return;
    }
    if (!AllNonHostMembersReady()) {
        reply(ResultCode::NotAllReady);
        return;
    }

    reply(ResultCode::Ok);

    // Freeze the roster: no joins or leaves are accepted while Starting, so the
    // member list handed to the instance server is the one that will connect.
    SetState(RoomState::Starting);

    std::vector<RoomMemberInfo> members;
    members.reserve(_members.size());
    for (const Member& member : _members) {
        members.push_back(BuildMemberInfo(member));
    }

    LOG_INFO("room {} '{}': starting with {} players", _id, _name, members.size());
    GMatchServer->BeginHandoff(std::static_pointer_cast<Room>(shared_from_this()), members,
                               _config);
    PublishSummary();
}

void Room::HandleSetConfig(SessionId requesterId, MatchConfig config) {
    const Member* requester = FindMember(requesterId);
    if (requester == nullptr) {
        return;
    }

    if (_hostSessionId != requesterId) {
        // Not an error worth broadcasting; just tell them nothing changed.
        if (requester->session != nullptr) {
            requester->session->SendPacket(BuildConfigPacket(false));
        }
        return;
    }
    if (_state != RoomState::Waiting) {
        if (requester->session != nullptr) {
            requester->session->SendPacket(BuildConfigPacket(false));
        }
        return;
    }

    const bool accepted = ClampMatchConfig(_mode, config);
    _config = config;

    LOG_INFO("room {} '{}': settings now rounds={} paper={}..{} ammo={} waveCap={} items=0x{:02X}",
             _id, _name, _config.rounds, _config.paperSizeMin, _config.paperSizeMax,
             _config.ammoPerWave, _config.waveLimit, _config.itemMask);

    Broadcast(MakePacket(BuildConfigPacket(accepted)));
}

S_RoomConfigChanged Room::BuildConfigPacket(bool accepted) const {
    S_RoomConfigChanged packet;
    packet.config = _config;
    packet.effectiveItemMask = EffectiveItemMask(_config.itemMask, _config.ammoPerWave);
    packet.accepted = accepted;
    return packet;
}

// ---------------------------------------------------------------------------
// Handoff outcomes
// ---------------------------------------------------------------------------

void Room::EnqueueHandoffReady(InstanceId instanceId, const std::string& host, uint16 port,
                               uint16 webPort,
                               const std::vector<std::pair<SessionId, std::string>>& tickets) {
    PostToRoom(this, [instanceId, host, port, webPort, tickets](const RoomRef& self) {
        if (self->_state != RoomState::Starting) {
            return;
        }
        self->_pendingInstanceId = instanceId;

        for (const auto& [sessionId, ticket] : tickets) {
            const Member* member = self->FindMember(sessionId);
            if (member == nullptr || member->session == nullptr) {
                continue;
            }
            S_GameStarting starting;
            starting.instanceId = instanceId;
            starting.host = host;
            // Each player is sent the port their own transport can reach.
            starting.port = member->session->IsWebSocket() ? webPort : port;
            starting.ticket = ticket;
            member->session->SendPacket(starting);
        }

        LOG_INFO("room {}: handoff to instance {} at {}:{} (web {})", self->_id, instanceId, host,
                 port, webPort);
    });
}

void Room::EnqueueHandoffFailed(ResultCode reason) {
    PostToRoom(this, [reason](const RoomRef& self) {
        if (self->_state != RoomState::Starting) {
            return;
        }
        LOG_WARN("room {}: handoff failed ({}); returning to lobby", self->_id, ToString(reason));

        self->_pendingInstanceId = kInvalidInstanceId;
        self->SetState(RoomState::Waiting);

        // Everyone is told why, so the host can retry rather than staring at a
        // room that silently refuses to start.
        S_RoomStartAck ack;
        ack.result = reason;
        self->Broadcast(MakePacket(ack));
        self->PublishSummary();
    });
}

void Room::EnqueueInstanceRunning() {
    PostToRoom(this, [](const RoomRef& self) {
        if (self->_state != RoomState::Starting) {
            return;
        }
        self->SetState(RoomState::InGame);
        self->PublishSummary();
    });
}

void Room::EnqueueInstanceFinished(const std::string& reason) {
    PostToRoom(this, [reason](const RoomRef& self) {
        if (self->_state != RoomState::InGame && self->_state != RoomState::Starting) {
            return;
        }
        LOG_INFO("room {}: instance finished ({})", self->_id, reason);
        self->_pendingInstanceId = kInvalidInstanceId;
        self->SetState(RoomState::Waiting);

        // Players return unready so nobody is dragged straight into another
        // match by a host mashing start.
        for (Member& member : self->_members) {
            member.ready = false;
        }
        self->PublishSummary();
    });
}

// ---------------------------------------------------------------------------
// Internals
// ---------------------------------------------------------------------------

Room::Member* Room::FindMember(SessionId sessionId) {
    const auto it = std::find_if(_members.begin(), _members.end(),
                                 [sessionId](const Member& m) { return m.sessionId == sessionId; });
    return it != _members.end() ? &(*it) : nullptr;
}

uint8 Room::AllocateSlot() const {
    for (uint8 slot = 0; slot < _capacity; ++slot) {
        const bool taken = std::any_of(_members.begin(), _members.end(),
                                       [slot](const Member& m) { return m.slot == slot; });
        if (!taken) {
            return slot;
        }
    }
    return 0;  // Unreachable: capacity is checked before a member is added.
}

void Room::PromoteNextHost() {
    if (_members.empty()) {
        return;
    }

    // Longest-present member inherits. Closing the room instead would punish
    // everyone for the host's connection dropping.
    const auto it = std::min_element(
        _members.begin(), _members.end(),
        [](const Member& a, const Member& b) { return a.joinOrder < b.joinOrder; });

    _hostSessionId = it->sessionId;

    S_RoomHostChanged changed;
    changed.hostSessionId = _hostSessionId;
    Broadcast(MakePacket(changed));

    LOG_INFO("room {} '{}': {} is now host", _id, _name, it->nickname);
}

void Room::CloseRoom(const std::string& reason) {
    SetState(RoomState::Closing);

    S_RoomClosed closed;
    closed.roomId = _id;
    closed.reason = reason;
    Broadcast(MakePacket(closed));

    for (Member& member : _members) {
        if (member.session != nullptr) {
            member.session->SetRoomId(kInvalidRoomId);
        }
        GMatchServer->PushChannelLeave(member.sessionId, ChannelType::Room, _id);
    }
    _members.clear();

    LOG_INFO("room {} '{}' closed: {}", _id, _name, reason);
    GRoomManager->Remove(_id);
}

void Room::SetState(RoomState state) {
    if (_state == state) {
        return;
    }
    _state = state;

    S_RoomStateChanged changed;
    changed.state = state;
    Broadcast(MakePacket(changed));
}

bool Room::AllNonHostMembersReady() const {
    return std::all_of(_members.begin(), _members.end(), [this](const Member& member) {
        return member.sessionId == _hostSessionId || member.ready;
    });
}

void Room::Broadcast(SendBufferRef sendBuffer, SessionId exceptSessionId) {
    if (sendBuffer == nullptr) {
        return;
    }
    for (const Member& member : _members) {
        if (member.sessionId == exceptSessionId || member.session == nullptr) {
            continue;
        }
        // One buffer, many sessions: the send path holds a reference per
        // session rather than copying the bytes.
        member.session->Send(sendBuffer);
    }
}

RoomMemberInfo Room::BuildMemberInfo(const Member& member) const {
    RoomMemberInfo info;
    info.sessionId = member.sessionId;
    info.nickname = member.nickname;
    info.slot = member.slot;
    info.isHost = (member.sessionId == _hostSessionId);
    info.isReady = member.ready;
    return info;
}

RoomDetail Room::BuildDetail() const {
    RoomDetail detail;
    detail.roomId = _id;
    detail.name = _name;
    detail.mode = _mode;
    detail.state = _state;
    detail.capacity = _capacity;
    detail.hasPassword = HasPassword();
    detail.hostSessionId = _hostSessionId;
    detail.members.reserve(_members.size());
    for (const Member& member : _members) {
        detail.members.push_back(BuildMemberInfo(member));
    }
    return detail;
}

void Room::PublishSummary() {
    RoomSummary summary;
    summary.roomId = _id;
    summary.name = _name;
    summary.mode = _mode;
    summary.state = _state;
    summary.memberCount = static_cast<uint8>(_members.size());
    summary.capacity = _capacity;
    summary.hasPassword = HasPassword();

    if (const auto it = std::find_if(
            _members.begin(), _members.end(),
            [this](const Member& m) { return m.sessionId == _hostSessionId; });
        it != _members.end()) {
        summary.hostNickname = it->nickname;
    }

    GRoomManager->UpdateSummary(_id, summary);
}

}  // namespace nhn::match
