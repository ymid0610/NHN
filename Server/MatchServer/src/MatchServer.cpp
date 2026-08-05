#include "match/MatchServer.h"

#include <format>

#include "match/MatchGlobal.h"
#include "match/RoomManager.h"
#include "match/SessionManager.h"

namespace nhn::match {

using namespace proto;

namespace {

/// Runs @p body on the match server's queue with a strong self reference.
template <class Fn>
void PostToServer(MatchServer* server, Fn&& body) {
    Ref<MatchServer> self = std::static_pointer_cast<MatchServer>(server->shared_from_this());
    server->DoAsync([self, body = std::forward<Fn>(body)]() { body(self); });
}

/// Resolves the room a session is currently in, replying with @p onMissing if
/// it is not in one.
RoomRef RequireRoom(const ClientSessionRef& session) {
    const RoomId roomId = session->GetRoomId();
    if (roomId == kInvalidRoomId) {
        return nullptr;
    }
    return GRoomManager->Find(roomId);
}

}  // namespace

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void MatchServer::DispatchClientPacket(const ClientSessionRef& session, uint8* buffer,
                                       int32 length) {
    // Nothing but the hello is accepted before authentication, so a client
    // cannot reach room logic without an id.
    if (!session->IsAuthenticated() && PeekPacketId(buffer, length) != PacketId::C_Hello) {
        session->SendError(ResultCode::NotAuthenticated);
        session->Disconnect("packet before hello");
        return;
    }
    _clientDispatcher.Dispatch(session, buffer, length);
}

void MatchServer::DispatchPeerPacket(const PeerSessionRef& session, uint8* buffer, int32 length) {
    if (session->GetServerType() == ServerType::None &&
        PeekPacketId(buffer, length) != PacketId::P_ServerHello) {
        session->Disconnect("packet before server hello");
        return;
    }
    _peerDispatcher.Dispatch(session, buffer, length);
}

// ---------------------------------------------------------------------------
// Client handlers
// ---------------------------------------------------------------------------

void MatchServer::RegisterClientHandlers() {
    _clientDispatcher.On<C_Hello>([this](const ClientSessionRef& session, const C_Hello& packet) {
        if (session->IsAuthenticated()) {
            session->SendError(ResultCode::AlreadyAuthenticated);
            return;
        }
        if (!IsCleanUtf8(packet.nickname, kMaxNicknameLength)) {
            S_HelloAck ack;
            ack.result = ResultCode::NicknameInvalid;
            session->SendPacket(ack);
            session->Disconnect("invalid nickname");
            return;
        }
        if (GSessionManager->Count() >= _settings.maxSessions) {
            S_HelloAck ack;
            ack.result = ResultCode::ServerFull;
            session->SendPacket(ack);
            session->Disconnect("server full");
            return;
        }

        session->SetNickname(packet.nickname);
        const SessionId sessionId = GSessionManager->Register(session);
        session->SetAuthenticated(true);

        S_HelloAck ack;
        ack.result = ResultCode::Ok;
        ack.sessionId = sessionId;
        ack.nickname = packet.nickname;
        IssueClientTickets(session, ack);
        session->SendPacket(ack);

        LOG_INFO("session {} '{}' connected from {}", sessionId, packet.nickname,
                 session->GetNetAddress().ToString());
    });

    _clientDispatcher.On<C_Ping>([](const ClientSessionRef& session, const C_Ping& packet) {
        S_Pong pong;
        pong.clientTimeMs = packet.clientTimeMs;
        pong.serverTimeMs = NowUnixMs();
        session->SendPacket(pong);
    });

    _clientDispatcher.On<C_RoomCreate>(
        [](const ClientSessionRef& session, const C_RoomCreate& packet) {
            if (session->GetRoomId() != kInvalidRoomId) {
                S_RoomCreateAck ack;
                ack.result = ResultCode::AlreadyInRoom;
                session->SendPacket(ack);
                return;
            }

            ResultCode result = ResultCode::Ok;
            RoomRef room = GRoomManager->Create(packet.name, packet.roomType, packet.password,
                                                result);
            if (room == nullptr) {
                S_RoomCreateAck ack;
                ack.result = result;
                session->SendPacket(ack);
                return;
            }

            LOG_INFO("session {} created room {} '{}' ({}, {})", session->GetSessionId(),
                     room->GetId(), packet.name, ToString(packet.roomType),
                     room->HasPassword() ? "locked" : "open");

            // The creator joins through the normal path, which makes them host
            // as the first member and keeps one code path for roster changes.
            room->EnqueueJoin(session, packet.password);
        });

    _clientDispatcher.On<C_RoomList>(
        [](const ClientSessionRef& session, const C_RoomList& packet) {
            S_RoomList response;
            GRoomManager->Search(packet, response);
            session->SendPacket(response);
        });

    _clientDispatcher.On<C_RoomJoin>(
        [](const ClientSessionRef& session, const C_RoomJoin& packet) {
            if (session->GetRoomId() != kInvalidRoomId) {
                S_RoomJoinAck ack;
                ack.result = ResultCode::AlreadyInRoom;
                session->SendPacket(ack);
                return;
            }

            RoomRef room = GRoomManager->Find(packet.roomId);
            if (room == nullptr) {
                S_RoomJoinAck ack;
                ack.result = ResultCode::RoomNotFound;
                session->SendPacket(ack);
                return;
            }
            room->EnqueueJoin(session, packet.password);
        });

    _clientDispatcher.On<C_QuickMatch>(
        [this](const ClientSessionRef& session, const C_QuickMatch& packet) {
            if (session->GetRoomId() != kInvalidRoomId) {
                S_RoomJoinAck ack;
                ack.result = ResultCode::AlreadyInRoom;
                session->SendPacket(ack);
                return;
            }
            if (!IsValidRoomType(packet.roomType)) {
                S_RoomJoinAck ack;
                ack.result = ResultCode::RoomTypeInvalid;
                session->SendPacket(ack);
                return;
            }

            auto candidates = std::make_shared<std::vector<RoomId>>(
                GRoomManager->FindQuickMatchCandidates(
                    packet.roomType, static_cast<size_t>(_settings.quickMatchAttempts)));

            LOG_DEBUG("quick match: session {} wants {}, {} candidate(s)",
                      session->GetSessionId(), ToString(packet.roomType), candidates->size());

            QuickMatchStep(session, packet.roomType, candidates, 0);
        });

    _clientDispatcher.On<C_RoomLeave>([](const ClientSessionRef& session, const C_RoomLeave&) {
        RoomRef room = RequireRoom(session);
        S_RoomLeaveAck ack;
        ack.result = room != nullptr ? ResultCode::Ok : ResultCode::NotInRoom;
        session->SendPacket(ack);

        if (room != nullptr) {
            room->EnqueueLeave(session->GetSessionId(), LeaveReason::Left);
        }
    });

    _clientDispatcher.On<C_RoomKick>(
        [](const ClientSessionRef& session, const C_RoomKick& packet) {
            RoomRef room = RequireRoom(session);
            if (room == nullptr) {
                S_RoomKickAck ack;
                ack.result = ResultCode::NotInRoom;
                ack.targetSessionId = packet.targetSessionId;
                session->SendPacket(ack);
                return;
            }
            room->EnqueueKick(session->GetSessionId(), packet.targetSessionId);
        });

    _clientDispatcher.On<C_RoomReady>(
        [](const ClientSessionRef& session, const C_RoomReady& packet) {
            RoomRef room = RequireRoom(session);
            if (room == nullptr) {
                S_RoomReadyAck ack;
                ack.result = ResultCode::NotInRoom;
                session->SendPacket(ack);
                return;
            }
            room->EnqueueReady(session->GetSessionId(), packet.ready);
        });

    _clientDispatcher.On<C_RoomStart>([](const ClientSessionRef& session, const C_RoomStart&) {
        RoomRef room = RequireRoom(session);
        if (room == nullptr) {
            S_RoomStartAck ack;
            ack.result = ResultCode::NotInRoom;
            session->SendPacket(ack);
            return;
        }
        room->EnqueueStart(session->GetSessionId());
    });
}

// ---------------------------------------------------------------------------
// Quick match
// ---------------------------------------------------------------------------

void MatchServer::QuickMatchStep(const ClientSessionRef& session, RoomType roomType,
                                 Ref<std::vector<RoomId>> candidates, size_t index) {
    // A player who disconnected or joined a room by hand while this was in
    // flight should not be dragged into one.
    if (!session->IsConnected() || session->GetRoomId() != kInvalidRoomId) {
        return;
    }

    if (index >= candidates->size()) {
        QuickMatchCreate(session, roomType);
        return;
    }

    const RoomId roomId = (*candidates)[index];
    RoomRef room = GRoomManager->Find(roomId);
    if (room == nullptr) {
        // Closed since the index was read.
        QuickMatchStep(session, roomType, candidates, index + 1);
        return;
    }

    Ref<MatchServer> self = std::static_pointer_cast<MatchServer>(shared_from_this());
    room->EnqueueJoin(session, "", [self, session, roomType, candidates, index](ResultCode result) {
        if (result == ResultCode::Ok) {
            return;  // The room already sent the join ack with the roster.
        }
        // Full, started, or the player is on that room's kick cooldown — all
        // ordinary outcomes for an automatic match. Try the next one.
        self->QuickMatchStep(session, roomType, candidates, index + 1);
    });
}

void MatchServer::QuickMatchCreate(const ClientSessionRef& session, RoomType roomType) {
    const std::string name =
        std::format("{} #{}", ToString(roomType), _quickMatchCounter.fetch_add(1));

    ResultCode result = ResultCode::Ok;
    RoomRef room = GRoomManager->Create(name, roomType, "", result);
    if (room == nullptr) {
        S_RoomJoinAck ack;
        ack.result = result;
        session->SendPacket(ack);
        LOG_WARN("quick match: cannot create a room for session {}: {}", session->GetSessionId(),
                 ToString(result));
        return;
    }

    LOG_INFO("quick match: session {} opened room {} '{}'", session->GetSessionId(), room->GetId(),
             name);

    // No callback: this room is brand new and empty, so a failure here is a
    // real error the player should see rather than something to retry.
    room->EnqueueJoin(session, "");
}

// ---------------------------------------------------------------------------
// Peer handlers
// ---------------------------------------------------------------------------

void MatchServer::RegisterPeerHandlers() {
    _peerDispatcher.On<P_ServerHello>(
        [this](const PeerSessionRef& session, const P_ServerHello& packet) {
            if (packet.serverType == ServerType::None || packet.serverType == ServerType::Match) {
                P_ServerHelloAck ack;
                ack.result = ResultCode::InvalidRequest;
                session->SendPacket(ack);
                session->Disconnect("invalid server type");
                return;
            }

            session->SetServerType(packet.serverType);
            session->SetServerId(packet.serverId);
            session->SetPublicEndpoint(packet.publicHost, packet.publicPort);
            session->SetCapacity(packet.capacity);
            session->SetLoad(0);

            GPeerRegistry->Register(session);

            P_ServerHelloAck ack;
            ack.result = ResultCode::Ok;
            ack.heartbeatIntervalMs = 2000;
            session->SendPacket(ack);

            LOG_INFO("peer registered: {} '{}' public {}:{}", ToString(packet.serverType),
                     packet.serverId, packet.publicHost, packet.publicPort);

            ResyncPeer(session);
        });

    _peerDispatcher.On<P_Heartbeat>(
        [](const PeerSessionRef& session, const P_Heartbeat& packet) {
            P_HeartbeatAck ack;
            ack.sentAtMs = packet.sentAtMs;
            session->SendPacket(ack);
        });

    _peerDispatcher.On<P_HeartbeatAck>([](const PeerSessionRef&, const P_HeartbeatAck&) {});

    _peerDispatcher.On<P_InstanceCreateAck>(
        [this](const PeerSessionRef& session, const P_InstanceCreateAck& packet) {
            HandleInstanceCreateAck(session, packet);
        });

    _peerDispatcher.On<P_InstanceReady>(
        [this](const PeerSessionRef&, const P_InstanceReady& packet) {
            HandleInstanceReady(packet);
        });

    _peerDispatcher.On<P_InstanceClosed>(
        [this](const PeerSessionRef&, const P_InstanceClosed& packet) {
            HandleInstanceClosed(packet);
        });
}

// ---------------------------------------------------------------------------
// Tickets and channels
// ---------------------------------------------------------------------------

void MatchServer::IssueClientTickets(const ClientSessionRef& session, S_HelloAck& outAck) {
    const SessionId sessionId = session->GetSessionId();
    const std::string nickname = session->GetNickname();
    const int64 expiresAtMs = NowUnixMs() + _settings.ticketLifetimeMs;

    auto issue = [&](ServerType type, std::string& outHost, uint16& outPort,
                     std::string& outTicket) {
        if (!GPeerRegistry->GetClientEndpoint(type, outHost, outPort)) {
            // Not fatal: the client simply has no chat or no voice this run.
            LOG_WARN("no {} server registered; session {} will run without it", ToString(type),
                     sessionId);
            outHost.clear();
            outPort = 0;
            return;
        }

        outTicket = Random::Ticket();

        P_TicketAdd add;
        add.ticket = outTicket;
        add.sessionId = sessionId;
        add.nickname = nickname;
        add.expiresAtMs = expiresAtMs;

        // Pushed to every server of that type: the client is told one endpoint,
        // but which process answers is not worth a second round-trip to pin
        // down, and an unredeemed ticket expires on its own.
        GPeerRegistry->SendToAll(type, MakePacket(add));
    };

    issue(ServerType::Chat, outAck.chatHost, outAck.chatPort, outAck.chatTicket);
    issue(ServerType::Voice, outAck.voiceHost, outAck.voicePort, outAck.voiceTicket);
}

void MatchServer::PushChannelJoin(SessionId sessionId, ChannelType type, uint64 channelId) {
    PostToServer(this, [sessionId, type, channelId](const Ref<MatchServer>& self) {
        self->_memberships.insert(Membership{sessionId, type, channelId});

        P_ChannelJoin join;
        join.sessionId = sessionId;
        join.channelType = type;
        join.channelId = channelId;

        const SendBufferRef buffer = MakePacket(join);
        GPeerRegistry->SendToAll(ServerType::Chat, buffer);
        GPeerRegistry->SendToAll(ServerType::Voice, buffer);
    });
}

void MatchServer::PushChannelLeave(SessionId sessionId, ChannelType type, uint64 channelId) {
    PostToServer(this, [sessionId, type, channelId](const Ref<MatchServer>& self) {
        self->_memberships.erase(Membership{sessionId, type, channelId});

        P_ChannelLeave leave;
        leave.sessionId = sessionId;
        leave.channelType = type;
        leave.channelId = channelId;

        const SendBufferRef buffer = MakePacket(leave);
        GPeerRegistry->SendToAll(ServerType::Chat, buffer);
        GPeerRegistry->SendToAll(ServerType::Voice, buffer);
    });
}

void MatchServer::PushSessionClosed(SessionId sessionId) {
    PostToServer(this, [sessionId](const Ref<MatchServer>& self) {
        for (auto it = self->_memberships.begin(); it != self->_memberships.end();) {
            it = it->sessionId == sessionId ? self->_memberships.erase(it) : std::next(it);
        }

        P_SessionClosed closed;
        closed.sessionId = sessionId;

        const SendBufferRef buffer = MakePacket(closed);
        GPeerRegistry->SendToAll(ServerType::Chat, buffer);
        GPeerRegistry->SendToAll(ServerType::Voice, buffer);
    });
}

void MatchServer::ResyncPeer(const PeerSessionRef& peer) {
    const ServerType type = peer->GetServerType();
    if (type != ServerType::Chat && type != ServerType::Voice) {
        return;
    }

    PostToServer(this, [peer, type](const Ref<MatchServer>& self) {
        // A satellite that just (re)started knows nothing. Replaying the
        // membership list is what makes a chat server restart invisible to
        // players already sitting in rooms.
        int32 replayed = 0;
        for (const Membership& membership : self->_memberships) {
            P_ChannelJoin join;
            join.sessionId = membership.sessionId;
            join.channelType = membership.channelType;
            join.channelId = membership.channelId;
            peer->SendPacket(join);
            ++replayed;
        }
        if (replayed > 0) {
            LOG_INFO("replayed {} channel memberships to {} '{}'", replayed, ToString(type),
                     peer->GetServerId());
        }
    });
}

// ---------------------------------------------------------------------------
// Handoff
// ---------------------------------------------------------------------------

void MatchServer::BeginHandoff(const RoomRef& room,
                               const std::vector<RoomMemberInfo>& members) {
    PostToServer(this, [room, members](const Ref<MatchServer>& self) {
        PeerSessionRef peer = GPeerRegistry->PickInstanceServer();
        if (peer == nullptr) {
            room->EnqueueHandoffFailed(ResultCode::NoInstanceServer);
            return;
        }

        const InstanceId instanceId = self->_nextInstanceId++;

        PendingHandoff pending;
        pending.roomId = room->GetId();
        pending.instanceId = instanceId;
        pending.phase = HandoffPhase::AwaitingCreate;
        pending.startedAt = NowTick();
        pending.members = members;
        pending.instancePeer = peer;
        self->_pendingHandoffs[instanceId] = std::move(pending);

        peer->AddLoad(1);

        P_InstanceCreate create;
        create.instanceId = instanceId;
        create.roomId = room->GetId();
        create.roomType = room->GetRoomType();
        create.members = members;
        peer->SendPacket(create);
    });
}

void MatchServer::HandleInstanceCreateAck(const PeerSessionRef& peer,
                                          const P_InstanceCreateAck& ack) {
    PostToServer(this, [peer, ack](const Ref<MatchServer>& self) {
        const auto it = self->_pendingHandoffs.find(ack.instanceId);
        if (it == self->_pendingHandoffs.end()) {
            LOG_WARN("create-ack for unknown instance {}", ack.instanceId);
            return;
        }

        PendingHandoff& pending = it->second;
        RoomRef room = GRoomManager->Find(pending.roomId);

        if (ack.result != ResultCode::Ok || room == nullptr) {
            peer->AddLoad(-1);
            if (room != nullptr) {
                room->EnqueueHandoffFailed(ack.result != ResultCode::Ok
                                               ? ack.result
                                               : ResultCode::InstanceCreateFailed);
            }
            self->_pendingHandoffs.erase(it);
            return;
        }

        // One ticket per player, pushed to the instance before any of them is
        // told where to connect — so the instance can validate by lookup and
        // never has to ask us anything.
        const int64 expiresAtMs = NowUnixMs() + self->_settings.ticketLifetimeMs;
        std::vector<std::pair<SessionId, std::string>> issued;
        issued.reserve(pending.members.size());

        for (const RoomMemberInfo& member : pending.members) {
            const std::string ticket = Random::Ticket();

            P_TicketAdd add;
            add.ticket = ticket;
            add.sessionId = member.sessionId;
            add.nickname = member.nickname;
            add.expiresAtMs = expiresAtMs;
            peer->SendPacket(add);

            issued.emplace_back(member.sessionId, ticket);

            // Instance chat and voice run alongside the room's own channels;
            // players keep both while a match is live.
            self->PushChannelJoin(member.sessionId, ChannelType::Instance, ack.instanceId);
        }

        pending.phase = HandoffPhase::AwaitingJoin;
        pending.startedAt = NowTick();

        room->EnqueueHandoffReady(ack.instanceId, ack.host, ack.port, issued);
    });
}

void MatchServer::HandleInstanceReady(const P_InstanceReady& ready) {
    PostToServer(this, [ready](const Ref<MatchServer>& self) {
        const auto it = self->_pendingHandoffs.find(ready.instanceId);
        if (it == self->_pendingHandoffs.end()) {
            return;
        }
        it->second.phase = HandoffPhase::Running;

        if (RoomRef room = GRoomManager->Find(it->second.roomId)) {
            room->EnqueueInstanceRunning();
        }
        LOG_INFO("instance {} is running", ready.instanceId);
    });
}

void MatchServer::HandleInstanceClosed(const P_InstanceClosed& closed) {
    PostToServer(this, [closed](const Ref<MatchServer>& self) {
        const auto it = self->_pendingHandoffs.find(closed.instanceId);
        if (it == self->_pendingHandoffs.end()) {
            return;
        }

        const PendingHandoff pending = std::move(it->second);
        self->_pendingHandoffs.erase(it);

        if (PeerSessionRef peer = pending.instancePeer.lock()) {
            peer->AddLoad(-1);
        }

        for (const RoomMemberInfo& member : pending.members) {
            self->PushChannelLeave(member.sessionId, ChannelType::Instance, closed.instanceId);
        }

        if (RoomRef room = GRoomManager->Find(pending.roomId)) {
            if (pending.phase == HandoffPhase::Running) {
                room->EnqueueInstanceFinished(closed.reason);
            } else {
                // Never got off the ground; report it as a failed start so the
                // host sees a reason rather than a room stuck in Starting.
                room->EnqueueHandoffFailed(ResultCode::InstanceCreateFailed);
            }
        }
    });
}

void MatchServer::SweepHandoffs() {
    PostToServer(this, [](const Ref<MatchServer>& self) {
        const TickCount now = NowTick();
        const uint32 timeoutMs = self->_settings.handoffTimeoutMs;

        std::vector<InstanceId> expired;
        for (const auto& [instanceId, pending] : self->_pendingHandoffs) {
            if (pending.phase == HandoffPhase::Running) {
                continue;  // A live match has no deadline.
            }
            if (now - pending.startedAt >= timeoutMs) {
                expired.push_back(instanceId);
            }
        }

        for (const InstanceId instanceId : expired) {
            const auto it = self->_pendingHandoffs.find(instanceId);
            if (it == self->_pendingHandoffs.end()) {
                continue;
            }
            const PendingHandoff pending = std::move(it->second);
            self->_pendingHandoffs.erase(it);

            LOG_WARN("handoff for instance {} timed out in phase {}", instanceId,
                     static_cast<int32>(pending.phase));

            if (PeerSessionRef peer = pending.instancePeer.lock()) {
                peer->AddLoad(-1);
                P_InstanceClosed closed;
                closed.instanceId = instanceId;
                closed.reason = "handoff timed out";
                peer->SendPacket(closed);
            }

            for (const RoomMemberInfo& member : pending.members) {
                self->PushChannelLeave(member.sessionId, ChannelType::Instance, instanceId);
            }

            if (RoomRef room = GRoomManager->Find(pending.roomId)) {
                room->EnqueueHandoffFailed(ResultCode::InstanceCreateFailed);
            }
        }
    });
}

void MatchServer::StartMaintenanceTimer() {
    Ref<MatchServer> self = std::static_pointer_cast<MatchServer>(shared_from_this());
    DoTimer(1000, [self]() {
        self->SweepHandoffs();
        self->StartMaintenanceTimer();
    });
}

}  // namespace nhn::match
