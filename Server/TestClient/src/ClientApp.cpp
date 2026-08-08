#include "testclient/ClientApp.h"

#include <array>
#include <cstdio>
#include <thread>

namespace nhn::test {

using namespace proto;

ClientApp* GClientApp = nullptr;

namespace {
std::mutex gConsoleLock;
}

void Console::Write(const std::string& line) {
    std::lock_guard<std::mutex> guard(gConsoleLock);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Link sessions
// ---------------------------------------------------------------------------

void MatchSession::OnConnected() {
    GClientApp->OnMatchConnected(GetSessionRef());
}
void MatchSession::OnDisconnected() {
    GClientApp->OnMatchDisconnected();
}
void MatchSession::OnRecvPacket(uint8* buffer, int32 length) {
    GClientApp->OnMatchPacket(buffer, length);
}

void ChatSession::OnConnected() {
    GClientApp->OnChatConnected(GetSessionRef());
}
void ChatSession::OnDisconnected() {
    GClientApp->OnChatDisconnected();
}
void ChatSession::OnRecvPacket(uint8* buffer, int32 length) {
    GClientApp->OnChatPacket(buffer, length);
}

void InstanceSession::OnConnected() {
    GClientApp->OnInstanceConnected(GetSessionRef());
}
void InstanceSession::OnDisconnected() {
    GClientApp->OnInstanceDisconnected();
}
void InstanceSession::OnRecvPacket(uint8* buffer, int32 length) {
    GClientApp->OnInstancePacket(buffer, length);
}

// ---------------------------------------------------------------------------
// ClientApp
// ---------------------------------------------------------------------------

ClientApp::ClientApp(Settings settings) : _settings(std::move(settings)) {}

void ClientApp::Start() {
    _timerQueue = std::make_shared<JobQueue>();
    RegisterMatchHandlers();
    RegisterChatHandlers();
    RegisterInstanceHandlers();
    RegisterGameHandlers();
}

void ClientApp::Stop() {
    _running.store(false);
    _voiceSending.store(false);

    SessionRef match, chat, instance;
    {
        std::lock_guard<std::mutex> guard(_lock);
        match = _matchSession;
        chat = _chatSession;
        instance = _instanceSession;
    }
    if (instance != nullptr) instance->Disconnect("client shutting down");
    if (chat != nullptr) chat->Disconnect("client shutting down");
    if (match != nullptr) match->Disconnect("client shutting down");

    if (_voiceSocket != nullptr) {
        _voiceSocket->Stop();
    }
}

// ---------------------------------------------------------------------------
// Connections
// ---------------------------------------------------------------------------

void ClientApp::ConnectMatch(const std::string& host, uint16 port) {
    _matchService = std::make_shared<ClientService>(
        NetAddress(host, port), []() -> SessionRef { return std::make_shared<MatchSession>(); },
        1);
    if (!_matchService->Start()) {
        Console::Error("cannot reach the match server at {}:{}", host, port);
    }
}

void ClientApp::ConnectChat(const std::string& host, uint16 port, const std::string& ticket) {
    {
        std::lock_guard<std::mutex> guard(_lock);
        _pendingChatTicket = ticket;
    }
    _chatService = std::make_shared<ClientService>(
        NetAddress(host, port), []() -> SessionRef { return std::make_shared<ChatSession>(); }, 1);
    if (!_chatService->Start()) {
        Console::Error("cannot reach the chat server at {}:{}", host, port);
    }
}

void ClientApp::ConnectInstance(const std::string& host, uint16 port, const std::string& ticket) {
    {
        std::lock_guard<std::mutex> guard(_lock);
        _pendingChatTicket = ticket;  // reused slot for the instance handshake
    }
    _instanceService = std::make_shared<ClientService>(
        NetAddress(host, port),
        []() -> SessionRef { return std::make_shared<InstanceSession>(); }, 1);
    if (!_instanceService->Start()) {
        Console::Error("cannot reach the instance server at {}:{}", host, port);
    }
}

void ClientApp::OnMatchConnected(const SessionRef& session) {
    {
        std::lock_guard<std::mutex> guard(_lock);
        _matchSession = session;
    }
    Console::Event("connected to match server");

    C_Hello hello;
    hello.nickname = _settings.nickname;
    hello.clientVersion = "test-client/0.1";
    session->Send(MakePacket(hello));
}

void ClientApp::OnMatchDisconnected() {
    _authenticated.store(false);
    _roomId.store(kInvalidRoomId);
    {
        std::lock_guard<std::mutex> guard(_lock);
        _matchSession = nullptr;
        _roomMembers.clear();
    }
    Console::Event("match server connection closed");
}

void ClientApp::OnMatchPacket(uint8* buffer, int32 length) {
    SessionRef session;
    {
        std::lock_guard<std::mutex> guard(_lock);
        session = _matchSession;
    }
    if (session != nullptr) {
        _matchDispatcher.Dispatch(session, buffer, length);
    }
}

void ClientApp::OnChatConnected(const SessionRef& session) {
    std::string ticket;
    {
        std::lock_guard<std::mutex> guard(_lock);
        _chatSession = session;
        ticket = _pendingChatTicket;
    }

    C_ChatHello hello;
    hello.ticket = ticket;
    session->Send(MakePacket(hello));
}

void ClientApp::OnChatDisconnected() {
    _chatReady.store(false);
    std::lock_guard<std::mutex> guard(_lock);
    _chatSession = nullptr;
}

void ClientApp::OnChatPacket(uint8* buffer, int32 length) {
    SessionRef session;
    {
        std::lock_guard<std::mutex> guard(_lock);
        session = _chatSession;
    }
    if (session != nullptr) {
        _chatDispatcher.Dispatch(session, buffer, length);
    }
}

void ClientApp::OnInstanceConnected(const SessionRef& session) {
    std::string ticket;
    {
        std::lock_guard<std::mutex> guard(_lock);
        _instanceSession = session;
        ticket = _pendingChatTicket;
    }

    C_InstHello hello;
    hello.ticket = ticket;
    session->Send(MakePacket(hello));
}

void ClientApp::OnInstanceDisconnected() {
    _inInstance.store(false);
    _instanceId.store(kInvalidInstanceId);
    {
        std::lock_guard<std::mutex> guard(_lock);
        _instanceSession = nullptr;
    }
    Console::Event("instance connection closed");
}

void ClientApp::OnInstancePacket(uint8* buffer, int32 length) {
    SessionRef session;
    {
        std::lock_guard<std::mutex> guard(_lock);
        session = _instanceSession;
    }
    if (session != nullptr) {
        _instanceDispatcher.Dispatch(session, buffer, length);
    }
}

// ---------------------------------------------------------------------------
// Match handlers
// ---------------------------------------------------------------------------

void ClientApp::RegisterMatchHandlers() {
    _matchDispatcher.On<S_HelloAck>([this](const SessionRef&, const S_HelloAck& packet) {
        if (packet.result != ResultCode::Ok) {
            Console::Error("hello rejected: {}", ToString(packet.result));
            return;
        }
        _sessionId.store(packet.sessionId);
        _authenticated.store(true);
        Console::Event("authenticated as session {} '{}'", packet.sessionId, packet.nickname);

        if (!packet.chatTicket.empty() && packet.chatPort != 0) {
            {
                std::lock_guard<std::mutex> guard(_lock);
                _chatHost = packet.chatHost;
                _chatPort = packet.chatPort;
            }
            ConnectChat(packet.chatHost, packet.chatPort, packet.chatTicket);
        } else {
            Console::Event("no chat server available this run");
        }

        if (!packet.voiceTicket.empty() && packet.voicePort != 0) {
            StartVoice(packet.voiceHost, packet.voicePort, packet.voiceTicket);
        } else {
            Console::Event("no voice server available this run");
        }
    });

    _matchDispatcher.On<S_Pong>([](const SessionRef&, const S_Pong& packet) {
        Console::Event("pong: round trip {} ms", NowUnixMs() - packet.clientTimeMs);
    });

    _matchDispatcher.On<S_Error>([](const SessionRef&, const S_Error& packet) {
        Console::Error("server error: {} {}", ToString(packet.result), packet.detail);
    });

    auto applyRoom = [this](const RoomDetail& room) {
        _roomId.store(room.roomId);
        std::lock_guard<std::mutex> guard(_lock);
        _roomMembers = room.members;
        _hostSessionId = room.hostSessionId;
    };

    _matchDispatcher.On<S_RoomCreateAck>(
        [this, applyRoom](const SessionRef&, const S_RoomCreateAck& packet) {
            if (packet.result != ResultCode::Ok) {
                Console::Error("room create failed: {}", ToString(packet.result));
                return;
            }
            applyRoom(packet.room);
            Console::Event("created room {} '{}'", packet.room.roomId, packet.room.name);
        });

    _matchDispatcher.On<S_RoomJoinAck>(
        [this, applyRoom](const SessionRef&, const S_RoomJoinAck& packet) {
            if (packet.result != ResultCode::Ok) {
                Console::Error("room join failed: {}", ToString(packet.result));
                return;
            }
            applyRoom(packet.room);
            Console::Event("joined room {} '{}' ({}/{}) host={}", packet.room.roomId,
                           packet.room.name, packet.room.members.size(), packet.room.capacity,
                           packet.room.hostSessionId);
        });

    _matchDispatcher.On<S_RoomConfigChanged>(
        [this](const SessionRef&, const S_RoomConfigChanged& packet) {
            {
                std::lock_guard<std::mutex> guard(_lock);
                // Kept so 'room config key=value' edits the live settings
                // rather than resetting everything else to defaults.
                _matchConfig = packet.config;
            }
            Console::Event("settings: rounds={} paper={}..{} ammo={} waveCap={} items=0x{:02X}{}",
                           packet.config.rounds, packet.config.paperSizeMin,
                           packet.config.paperSizeMax, packet.config.ammoPerWave,
                           packet.config.waveLimit, packet.effectiveItemMask,
                           packet.accepted ? "" : " (adjusted)");
        });

    _matchDispatcher.On<S_RoomList>([this](const SessionRef&, const S_RoomList& packet) {
        {
            std::lock_guard<std::mutex> guard(_lock);
            _lastRoomList = packet.rooms;
        }
        _roomListVersion.fetch_add(1);

        Console::Print("  {} room(s), page {}", packet.totalCount, packet.page);
        for (const RoomSummary& room : packet.rooms) {
            Console::Print("    #{:<4} {:<20} {:<5} {}/{} {:<8} host={}", room.roomId, room.name,
                           ToString(room.mode), room.memberCount, room.capacity,
                           room.hasPassword ? "[locked]" : "", room.hostNickname);
        }
    });

    _matchDispatcher.On<S_RoomLeaveAck>([this](const SessionRef&, const S_RoomLeaveAck& packet) {
        if (packet.result == ResultCode::Ok) {
            _roomId.store(kInvalidRoomId);
            std::lock_guard<std::mutex> guard(_lock);
            _roomMembers.clear();
        }
        Console::Event("leave: {}", ToString(packet.result));
    });

    _matchDispatcher.On<S_RoomKickAck>([](const SessionRef&, const S_RoomKickAck& packet) {
        Console::Event("kick {}: {}", packet.targetSessionId, ToString(packet.result));
    });

    _matchDispatcher.On<S_RoomReadyAck>([](const SessionRef&, const S_RoomReadyAck& packet) {
        Console::Event("ready={} ({})", packet.ready, ToString(packet.result));
    });

    _matchDispatcher.On<S_RoomStartAck>([](const SessionRef&, const S_RoomStartAck& packet) {
        if (packet.result == ResultCode::Ok) {
            Console::Event("start accepted");
        } else {
            Console::Error("start failed: {}", ToString(packet.result));
        }
    });

    _matchDispatcher.On<S_RoomBotChanged>(
        [](const SessionRef&, const S_RoomBotChanged& packet) {
            if (packet.result != ResultCode::Ok) {
                Console::Error("bot change refused: {}", ToString(packet.result));
                return;
            }
            Console::Event("{} (slot {}) at difficulty {}", packet.bot.nickname, packet.bot.slot,
                           packet.bot.botDifficulty);
        });

    _matchDispatcher.On<S_QuickMatchQueued>(
        [](const SessionRef&, const S_QuickMatchQueued& packet) {
            if (packet.result != ResultCode::Ok) {
                Console::Error("quick match refused: {}", ToString(packet.result));
                return;
            }
            Console::Event("quick match {}: {}/{} waiting (starts at {})", ToString(packet.mode),
                           packet.waiting, packet.capacity, packet.needed);
        });

    _matchDispatcher.On<S_QuickMatchCancelled>(
        [](const SessionRef&, const S_QuickMatchCancelled& packet) {
            if (packet.result == ResultCode::Ok) {
                Console::Event("quick match cancelled");
            } else {
                Console::Error("nothing to cancel: {}", ToString(packet.result));
            }
        });

    _matchDispatcher.On<S_RoomMemberJoined>(
        [this](const SessionRef&, const S_RoomMemberJoined& packet) {
            {
                std::lock_guard<std::mutex> guard(_lock);
                _roomMembers.push_back(packet.member);
            }
            Console::Event("{} joined (slot {})", packet.member.nickname, packet.member.slot);
        });

    _matchDispatcher.On<S_RoomMemberLeft>(
        [this](const SessionRef&, const S_RoomMemberLeft& packet) {
            std::string nickname = std::to_string(packet.sessionId);
            {
                std::lock_guard<std::mutex> guard(_lock);
                for (auto it = _roomMembers.begin(); it != _roomMembers.end(); ++it) {
                    if (it->sessionId == packet.sessionId) {
                        nickname = it->nickname;
                        _roomMembers.erase(it);
                        break;
                    }
                }
            }
            Console::Event("{} left ({})", nickname, ToString(packet.reason));
        });

    _matchDispatcher.On<S_RoomMemberReady>(
        [this](const SessionRef&, const S_RoomMemberReady& packet) {
            std::lock_guard<std::mutex> guard(_lock);
            for (RoomMemberInfo& member : _roomMembers) {
                if (member.sessionId == packet.sessionId) {
                    member.isReady = packet.ready;
                    break;
                }
            }
        });

    _matchDispatcher.On<S_RoomHostChanged>(
        [this](const SessionRef&, const S_RoomHostChanged& packet) {
            {
                std::lock_guard<std::mutex> guard(_lock);
                _hostSessionId = packet.hostSessionId;
                // Keep the cached roster in step, otherwise 'room info' keeps
                // showing the old host.
                for (RoomMemberInfo& member : _roomMembers) {
                    member.isHost = (member.sessionId == packet.hostSessionId);
                }
            }
            Console::Event("host is now session {}{}", packet.hostSessionId,
                           packet.hostSessionId == _sessionId.load() ? " (you)" : "");
        });

    _matchDispatcher.On<S_RoomKicked>([this](const SessionRef&, const S_RoomKicked& packet) {
        _roomId.store(kInvalidRoomId);
        {
            std::lock_guard<std::mutex> guard(_lock);
            _roomMembers.clear();
        }
        Console::Event("you were kicked from room {} — cannot rejoin for {}s", packet.roomId,
                       packet.cooldownSeconds);
    });

    _matchDispatcher.On<S_RoomStateChanged>(
        [](const SessionRef&, const S_RoomStateChanged& packet) {
            Console::Event("room state -> {}", ToString(packet.state));
        });

    _matchDispatcher.On<S_RoomClosed>([this](const SessionRef&, const S_RoomClosed& packet) {
        _roomId.store(kInvalidRoomId);
        {
            std::lock_guard<std::mutex> guard(_lock);
            _roomMembers.clear();
        }
        Console::Event("room {} closed: {}", packet.roomId, packet.reason);
    });

    _matchDispatcher.On<S_GameStarting>([this](const SessionRef&, const S_GameStarting& packet) {
        Console::Event("game starting — instance {} at {}:{}", packet.instanceId, packet.host,
                       packet.port);
        _instanceId.store(packet.instanceId);
        ConnectInstance(packet.host, packet.port, packet.ticket);
    });
}

// ---------------------------------------------------------------------------
// Chat handlers
// ---------------------------------------------------------------------------

void ClientApp::RegisterChatHandlers() {
    _chatDispatcher.On<S_ChatHelloAck>([this](const SessionRef&, const S_ChatHelloAck& packet) {
        if (packet.result != ResultCode::Ok) {
            Console::Error("chat hello rejected: {}", ToString(packet.result));
            return;
        }
        _chatReady.store(true);
        Console::Event("chat ready");
    });

    _chatDispatcher.On<S_ChatMessage>([this](const SessionRef&, const S_ChatMessage& packet) {
        {
            std::lock_guard<std::mutex> guard(_lock);
            _lastChatMessage = packet.message;
        }
        Console::Print("  [{}] {}: {}", ToString(packet.channelType), packet.nickname,
                       packet.message);
    });

    _chatDispatcher.On<S_ChatNotice>([](const SessionRef&, const S_ChatNotice& packet) {
        Console::Print("  [{}] * {}", ToString(packet.channelType), packet.message);
    });

    _chatDispatcher.On<S_ChatError>([](const SessionRef&, const S_ChatError& packet) {
        Console::Error("chat error: {}", ToString(packet.result));
    });
}

// ---------------------------------------------------------------------------
// Instance handlers
// ---------------------------------------------------------------------------

void ClientApp::RegisterInstanceHandlers() {
    _instanceDispatcher.On<S_InstHelloAck>(
        [this](const SessionRef&, const S_InstHelloAck& packet) {
            if (packet.result != ResultCode::Ok) {
                Console::Error("instance hello rejected: {}", ToString(packet.result));
                return;
            }
            _inInstance.store(true);
            _instanceId.store(packet.instanceId);
            Console::Event("in instance {} as slot {} with {} players", packet.instanceId,
                           packet.slot, packet.members.size());
        });

    _instanceDispatcher.On<S_InstMemberJoined>(
        [](const SessionRef&, const S_InstMemberJoined& packet) {
            Console::Event("instance: {} connected", packet.member.nickname);
        });

    _instanceDispatcher.On<S_InstMemberLeft>(
        [](const SessionRef&, const S_InstMemberLeft& packet) {
            Console::Event("instance: session {} left ({})", packet.sessionId,
                           ToString(packet.reason));
        });

    _instanceDispatcher.On<S_InstStart>([](const SessionRef&, const S_InstStart&) {
        Console::Event("match started");
    });

    _instanceDispatcher.On<S_InstEnd>([this](const SessionRef&, const S_InstEnd& packet) {
        _inInstance.store(false);
        Console::Event("match ended: {}", packet.reason);
    });
}

// ---------------------------------------------------------------------------
// Game
// ---------------------------------------------------------------------------

void ClientApp::RegisterGameHandlers() {
    _instanceDispatcher.On<S_MatchSetup>([this](const SessionRef&, const S_MatchSetup& packet) {
        {
            std::lock_guard<std::mutex> guard(_lock);
            _boardSpec = packet.board;
            _matchConfig = packet.config;
            _boardCells.assign(static_cast<size_t>(packet.board.width) * packet.board.height,
                               0xFF);
            for (const RoomMemberInfo& player : packet.players) {
                if (player.sessionId == _sessionId.load()) {
                    _mySlot.store(player.slot);
                }
            }
        }
        _matchSetupSeen.store(true);
        Console::Event("match: {} on {}x{}, {} in a row, {} rounds, ammo {}, wave cap {}",
                       ToString(packet.mode), packet.board.width, packet.board.height,
                       packet.board.winLength, packet.config.rounds, packet.config.ammoPerWave,
                       packet.config.waveLimit);
    });

    _instanceDispatcher.On<S_RoundStart>([this](const SessionRef&, const S_RoundStart& packet) {
        _roundIndex.store(packet.roundIndex);
        {
            std::lock_guard<std::mutex> guard(_lock);
            std::fill(_boardCells.begin(), _boardCells.end(), uint8{0xFF});
        }
        Console::Event("round {}/{} — paper size {}", packet.roundIndex + 1, packet.roundCount,
                       packet.paperSize);
    });

    _instanceDispatcher.On<S_WaveStart>([this](const SessionRef&, const S_WaveStart& packet) {
        _waveActive.store(true);
        _myAmmo.store(packet.ammo);
        Console::Event("wave {} — ammo {}", packet.waveIndex, packet.ammo);
        // Deliberately does NOT re-arm autoplay: the chain runs continuously
        // and checks _waveActive itself. Starting another one per wave stacks
        // them up and the combined rate trips the server's shot limiter.
    });

    _instanceDispatcher.On<S_WaveEnd>([this](const SessionRef&, const S_WaveEnd&) {
        _waveActive.store(false);
    });

    _instanceDispatcher.On<S_WorldSnapshot>(
        [this](const SessionRef&, const S_WorldSnapshot& packet) {
            std::lock_guard<std::mutex> guard(_lock);
            _entities = packet.entities;
            _lastSnapshotTick = packet.tick;
        });

    _instanceDispatcher.On<S_CellClaimed>([this](const SessionRef&, const S_CellClaimed& packet) {
        std::lock_guard<std::mutex> guard(_lock);
        if (packet.cellIndex < _boardCells.size()) {
            _boardCells[packet.cellIndex] = packet.ownerSlot;
        }
        if (packet.ownerSlot == _mySlot.load()) {
            _cellsClaimedByMe.fetch_add(1);
        }
    });

    _instanceDispatcher.On<S_ShotResolved>([this](const SessionRef&, const S_ShotResolved& packet) {
        if (packet.shooter != _sessionId.load() || packet.derived) {
            return;  // Only narrate our own trigger pulls.
        }
        switch (packet.kind) {
            case HitKind::Cell:
                Console::Event("hit cell {}", packet.cellIndex);
                break;
            case HitKind::CellTaken:
                Console::Event("cell {} was already taken", packet.cellIndex);
                break;
            case HitKind::Blocker:
                Console::Event("blocked");
                break;
            case HitKind::Decoy:
                Console::Event("decoy — shot wasted");
                break;
            case HitKind::Item:
                break;  // reported by S_ItemTriggered
            case HitKind::Miss:
                Console::Event("miss");
                break;
        }
    });

    _instanceDispatcher.On<S_ItemTriggered>(
        [this](const SessionRef&, const S_ItemTriggered& packet) {
            const bool mine = packet.victimSlot == _mySlot.load();
            Console::Event("item {} triggered{}", ToString(packet.item),
                           packet.victimSlot == 0xFF
                               ? ""
                               : (mine ? " — on you!" : " — on another player"));
        });

    _instanceDispatcher.On<S_StatusChanged>(
        [this](const SessionRef&, const S_StatusChanged& packet) {
            if (packet.slot != _mySlot.load()) {
                return;
            }
            _stunned.store(HasFlag(packet.flags, StatusFlags::Stunned));
            Console::Event("status: {}{}", _stunned.load() ? "stunned " : "",
                           HasFlag(packet.flags, StatusFlags::Blinded) ? "blinded" : "");
        });

    _instanceDispatcher.On<S_AmmoChanged>([this](const SessionRef&, const S_AmmoChanged& packet) {
        if (packet.slot == _mySlot.load()) {
            _myAmmo.store(packet.ammo);
        }
    });

    _instanceDispatcher.On<S_FireRejected>(
        [](const SessionRef&, const S_FireRejected& packet) {
            Console::Error("fire rejected: {}", ToString(packet.result));
        });

    _instanceDispatcher.On<S_RoundEnd>([this](const SessionRef&, const S_RoundEnd& packet) {
        _waveActive.store(false);
        for (const PlayerScore& score : packet.scores) {
            if (score.sessionId == _sessionId.load()) {
                _myScore.store(score.score);
            }
        }
        Console::Event("round {} over — {}", packet.roundIndex + 1,
                       packet.winnerSlot == 0xFF ? "no line completed"
                                                 : "slot " + std::to_string(packet.winnerSlot));
    });

    _instanceDispatcher.On<S_GameOver>([this](const SessionRef&, const S_GameOver& packet) {
        _gameOver.store(true);
        _autoplay.store(false);
        std::string winners;
        for (const uint8 slot : packet.winnerSlots) {
            winners += (winners.empty() ? "" : ", ") + std::to_string(slot);
        }
        Console::Event("match over — winning slot(s): {}", winners);
        for (const PlayerScore& score : packet.scores) {
            Console::Print("    slot {} score {}", score.slot, score.score);
        }
    });
}

bool ClientApp::AimAtCell(uint16 cellIndex, int32& outX, int32& outY, uint32& outTick) const {
    std::lock_guard<std::mutex> guard(_lock);
    if (_boardSpec.width == 0 || cellIndex >= _boardCells.size()) {
        return false;
    }

    for (const EntityState& entity : _entities) {
        if (entity.kind != EntityKind::TargetSheet) {
            continue;
        }
        const float cellW = static_cast<float>(entity.halfWidth * 2) / _boardSpec.width;
        const float cellH = static_cast<float>(entity.halfHeight * 2) / _boardSpec.height;
        const uint16 cellX = cellIndex % _boardSpec.width;
        const uint16 cellY = cellIndex / _boardSpec.width;

        outX = static_cast<int32>(entity.x - entity.halfWidth + (cellX + 0.5f) * cellW);
        outY = static_cast<int32>(entity.y - entity.halfHeight + (cellY + 0.5f) * cellH);
        // The tick we saw it at, not "now" — the server rewinds to this.
        outTick = _lastSnapshotTick;
        return outX >= 0 && outY >= 0 && outX < kFieldWidth && outY < kFieldHeight;
    }
    return false;
}

bool ClientApp::AimAtItem(ItemKind kind, int32& outX, int32& outY, uint32& outTick) const {
    std::lock_guard<std::mutex> guard(_lock);
    for (const EntityState& entity : _entities) {
        if (entity.kind != EntityKind::Item) {
            continue;
        }
        if (kind != ItemKind::None && entity.item != kind) {
            continue;
        }
        outX = entity.x;
        outY = entity.y;
        outTick = _lastSnapshotTick;
        return outX >= 0 && outY >= 0 && outX < kFieldWidth && outY < kFieldHeight;
    }
    return false;
}

void ClientApp::SendFire(int32 x, int32 y, uint32 tick) {
    SessionRef session;
    {
        std::lock_guard<std::mutex> guard(_lock);
        session = _instanceSession;
    }
    if (session == nullptr) {
        Console::Error("not connected to an instance");
        return;
    }

    C_Fire fire;
    fire.aimX = x;
    fire.aimY = y;
    fire.clientTick = tick;
    fire.sequence = _fireSequence.fetch_add(1);
    session->Send(MakePacket(fire));
}

uint16 ClientApp::PickTargetCell() const {
    std::lock_guard<std::mutex> guard(_lock);

    // Randomly, not lowest-index: two bots scanning in the same order would
    // contest the same cell every time and burn their ammo losing the race,
    // which is a property of the bot rather than of the game.
    std::vector<uint16> free;
    free.reserve(_boardCells.size());
    for (size_t i = 0; i < _boardCells.size(); ++i) {
        if (_boardCells[i] == 0xFF) {
            free.push_back(static_cast<uint16>(i));
        }
    }
    if (free.empty()) {
        return 0xFFFF;
    }
    return free[static_cast<size_t>(Random::Range(0, static_cast<int32>(free.size()) - 1))];
}

void ClientApp::ScheduleAutoplay() {
    if (!_autoplay.load() || !_running.load()) {
        return;
    }

    if (_waveActive.load() && !_stunned.load() &&
        (_myAmmo.load() > 0 || _matchConfig.ammoPerWave == kUnlimited)) {
        int32 x = 0;
        int32 y = 0;
        uint32 tick = 0;
        bool aimed = false;

        // Roughly a third of shots go at an item. Without this the bot only
        // ever claims cells and the item effects are never exercised at all,
        // which is most of what there is to get wrong.
        if (Random::Range(0, 2) == 0) {
            aimed = AimAtItem(ItemKind::None, x, y, tick);
        }
        if (!aimed) {
            const uint16 cell = PickTargetCell();
            aimed = cell != 0xFFFF && AimAtCell(cell, x, y, tick);
        }

        if (aimed) {
            SendFire(x, y, tick);
        }
    }

    // Comfortably above the server's minimum shot interval, so autoplay is
    // never the thing that trips the rate limit.
    _timerQueue->DoTimer(150, [this]() { ScheduleAutoplay(); });
}

std::string ClientApp::FormatBoard() const {
    std::lock_guard<std::mutex> guard(_lock);
    if (_boardSpec.width == 0) {
        return "  (no board yet)";
    }

    std::string result;
    for (uint8 y = 0; y < _boardSpec.height; ++y) {
        result += "    ";
        for (uint8 x = 0; x < _boardSpec.width; ++x) {
            const uint8 owner = _boardCells[static_cast<size_t>(y) * _boardSpec.width + x];
            result += owner == 0xFF ? '.' : static_cast<char>('0' + owner);
            result += ' ';
        }
        result += '\n';
    }
    return result;
}

// ---------------------------------------------------------------------------
// Voice
// ---------------------------------------------------------------------------

void ClientApp::StartVoice(const std::string& host, uint16 port, const std::string& ticket) {
    {
        std::lock_guard<std::mutex> guard(_lock);
        _voiceServerAddress = NetAddress(host, port);
        _voiceTicket = ticket;
    }

    if (_voiceSocket == nullptr) {
        _voiceSocket = std::make_shared<UdpSocket>();
        // Port 0: the OS picks one. The server learns our address from the
        // hello datagram, which is the only thing that works behind NAT.
        if (!_voiceSocket->Start(0,
                                 [this](const NetAddress& from, const uint8* data, int32 length) {
                                     OnVoiceDatagram(from, data, length);
                                 },
                                 4)) {
            Console::Error("cannot open a local voice socket");
            _voiceSocket = nullptr;
            return;
        }
    }

    SendVoiceHello();
}

void ClientApp::SendVoiceHello() {
    NetAddress server;
    std::string ticket;
    {
        std::lock_guard<std::mutex> guard(_lock);
        server = _voiceServerAddress;
        ticket = _voiceTicket;
    }
    if (_voiceSocket == nullptr || ticket.empty()) {
        return;
    }

    std::array<uint8, kVoiceHeaderSize + kTicketLength> datagram{};
    VoiceHeader header;
    header.type = VoiceMessageType::Hello;
    header.sessionId = _sessionId.load();
    WriteVoiceHeader(datagram.data(), header);
    std::memcpy(datagram.data() + kVoiceHeaderSize, ticket.data(), ticket.size());

    _voiceSocket->SendTo(server, datagram.data(),
                         static_cast<int32>(kVoiceHeaderSize + ticket.size()));
}

void ClientApp::OnVoiceDatagram(const NetAddress& /*from*/, const uint8* data, int32 length) {
    VoiceHeader header;
    if (!ReadVoiceHeader(data, length, header)) {
        return;
    }

    switch (header.type) {
        case VoiceMessageType::HelloAck:
            _voiceReady.store(true);
            Console::Event("voice ready");
            // Keepalives start immediately: a listener who never speaks still
            // has to keep its NAT mapping and server-side entry alive.
            SendVoiceKeepalive();
            break;

        case VoiceMessageType::Data: {
            _voiceFramesReceived.fetch_add(1);
            std::lock_guard<std::mutex> guard(_voiceStatLock);
            // Mute is handled here rather than on the server: it is a listener
            // preference, and routing it through the match server would add a
            // control path for something purely local.
            if (_mutedSpeakers[header.sessionId]) {
                return;
            }
            ++_voiceFramesBySpeaker[header.sessionId];
            break;
        }

        default:
            break;
    }
}

void ClientApp::SendVoiceKeepalive() {
    if (!_running.load() || _voiceSocket == nullptr) {
        return;
    }

    NetAddress server;
    {
        std::lock_guard<std::mutex> guard(_lock);
        server = _voiceServerAddress;
    }

    std::array<uint8, kVoiceHeaderSize> datagram{};
    VoiceHeader header;
    header.type = VoiceMessageType::Ping;
    header.sessionId = _sessionId.load();
    header.sequence = _voiceSequence.load();
    WriteVoiceHeader(datagram.data(), header);
    _voiceSocket->SendTo(server, datagram.data(), static_cast<int32>(datagram.size()));

    _timerQueue->DoTimer(3000, [this]() { SendVoiceKeepalive(); });
}

void ClientApp::ScheduleVoiceFrame() {
    if (!_voiceSending.load() || !_running.load() || _voiceSocket == nullptr) {
        return;
    }

    NetAddress server;
    {
        std::lock_guard<std::mutex> guard(_lock);
        server = _voiceServerAddress;
    }

    const uint16 sequence = _voiceSequence.fetch_add(1);

    std::array<uint8, kVoiceHeaderSize + 256> datagram{};
    VoiceHeader header;
    header.type = VoiceMessageType::Data;
    header.sessionId = _sessionId.load();
    header.sequence = sequence;
    header.timestamp = static_cast<uint32>(sequence) * 960u;  // 20 ms at 48 kHz
    header.flags = sequence == 0 ? kVoiceFlagTalkspurtStart : uint8{0};
    WriteVoiceHeader(datagram.data(), header);

    // A recognisable pattern rather than silence, so a mis-routed frame is
    // visible in a capture.
    const uint32 payload = std::min(_settings.voicePayloadBytes, 256u);
    for (uint32 i = 0; i < payload; ++i) {
        datagram[kVoiceHeaderSize + i] = static_cast<uint8>((sequence + i) & 0xFF);
    }

    _voiceSocket->SendTo(server, datagram.data(), static_cast<int32>(kVoiceHeaderSize + payload));
    _voiceFramesSent.fetch_add(1);

    _timerQueue->DoTimer(_settings.voiceFrameMs, [this]() { ScheduleVoiceFrame(); });
}

bool ClientApp::WaitFor(const std::function<bool()>& predicate, uint32 timeoutMs) const {
    const TickCount deadline = NowTick() + timeoutMs;
    while (NowTick() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

}  // namespace nhn::test
