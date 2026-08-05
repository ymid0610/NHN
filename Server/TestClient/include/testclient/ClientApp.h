#pragma once

#include <atomic>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Core.h"
#include "protocol/Dispatcher.h"
#include "protocol/Packets.h"
#include "protocol/VoicePacket.h"

namespace nhn::test {

class ClientApp;

/// Console output serialised against itself, because network events print from
/// IOCP workers while the prompt prints from the input thread.
class Console {
public:
    static void Write(const std::string& line);

    template <class... Args>
    static void Print(std::format_string<Args...> format, Args&&... args) {
        Write(std::format(format, std::forward<Args>(args)...));
    }

    /// Prefixed and coloured so incoming events stand out from command echoes.
    template <class... Args>
    static void Event(std::format_string<Args...> format, Args&&... args) {
        Write("  << " + std::format(format, std::forward<Args>(args)...));
    }

    template <class... Args>
    static void Error(std::format_string<Args...> format, Args&&... args) {
        Write("  !! " + std::format(format, std::forward<Args>(args)...));
    }
};

/// One class per link rather than a template: each routes to a different set of
/// ClientApp callbacks, and three short classes read better than three
/// specialisations.
class MatchSession : public PacketSession {
protected:
    void OnConnected() override;
    void OnDisconnected() override;
    void OnRecvPacket(uint8* buffer, int32 length) override;
};

class ChatSession : public PacketSession {
protected:
    void OnConnected() override;
    void OnDisconnected() override;
    void OnRecvPacket(uint8* buffer, int32 length) override;
};

class InstanceSession : public PacketSession {
protected:
    void OnConnected() override;
    void OnDisconnected() override;
    void OnRecvPacket(uint8* buffer, int32 length) override;
};

/// Console test client.
///
/// Everything the servers expose is reachable as a command, and any sequence of
/// commands can be replayed from a script file — which is what makes multi-
/// player scenarios (four processes, one script each) repeatable instead of
/// something you drive by hand.
///
/// Voice is exercised with synthetic frames rather than a real microphone.
/// Capturing and playing audio in a console process would be a large amount of
/// work that tests the audio stack, not the server; sending a known pattern at
/// the real frame rate and counting what comes back tests exactly the thing
/// that can break here — routing.
class ClientApp {
public:
    struct Settings {
        std::string matchHost = "127.0.0.1";
        uint16 matchPort = 7777;
        std::string nickname = "player";
        /// Synthetic voice frame interval; 20 ms matches an Opus frame.
        uint32 voiceFrameMs = 20;
        uint32 voicePayloadBytes = 60;
    };

    explicit ClientApp(Settings settings);

    void Start();
    void Stop();

    /// @return false when the command was "quit".
    bool ExecuteCommand(const std::string& line);
    void RunScript(const std::string& path);

    // -- connection state ---------------------------------------------------
    SessionId GetSessionId() const { return _sessionId.load(); }
    bool IsAuthenticated() const { return _authenticated.load(); }
    RoomId GetRoomId() const { return _roomId.load(); }
    bool IsInInstance() const { return _inInstance.load(); }

    // -- called by link sessions --------------------------------------------
    void OnMatchConnected(const SessionRef& session);
    void OnMatchDisconnected();
    void OnMatchPacket(uint8* buffer, int32 length);

    void OnChatConnected(const SessionRef& session);
    void OnChatDisconnected();
    void OnChatPacket(uint8* buffer, int32 length);

    void OnInstanceConnected(const SessionRef& session);
    void OnInstanceDisconnected();
    void OnInstancePacket(uint8* buffer, int32 length);

    void OnVoiceDatagram(const NetAddress& from, const uint8* data, int32 length);

private:
    void RegisterMatchHandlers();
    void RegisterChatHandlers();
    void RegisterInstanceHandlers();
    void RegisterGameHandlers();

    /// Converts a board cell into the point it currently occupies, using the
    /// most recent snapshot.
    ///
    /// This is exactly the calculation a real client's aiming would do, and it
    /// is why the fire packet carries the snapshot's tick rather than "now":
    /// the shot is aimed at where the sheet was seen, and the server rewinds to
    /// that moment to judge it.
    bool AimAtCell(uint16 cellIndex, int32& outX, int32& outY, uint32& outTick) const;
    bool AimAtItem(proto::ItemKind kind, int32& outX, int32& outY, uint32& outTick) const;
    void SendFire(int32 x, int32 y, uint32 tick);

    void ScheduleAutoplay();
    /// Lowest-numbered cell nobody has claimed yet, or kNoCell.
    uint16 PickTargetCell() const;

    std::string FormatBoard() const;

    void ConnectMatch(const std::string& host, uint16 port);
    void ConnectChat(const std::string& host, uint16 port, const std::string& ticket);
    void ConnectInstance(const std::string& host, uint16 port, const std::string& ticket);
    void StartVoice(const std::string& host, uint16 port, const std::string& ticket);

    void SendVoiceHello();
    void ScheduleVoiceFrame();
    void SendVoiceKeepalive();

    /// Defined here rather than in the .cpp because Commands.cpp instantiates
    /// it for every request packet.
    template <class T>
    void SendToMatch(T&& packet) {
        SessionRef session;
        {
            std::lock_guard<std::mutex> guard(_lock);
            session = _matchSession;
        }
        if (session == nullptr) {
            Console::Error("not connected to the match server");
            return;
        }
        session->Send(proto::MakePacket(std::forward<T>(packet)));
    }

    // -- commands ------------------------------------------------------------
    bool HandleCommand(const std::vector<std::string>& args, const std::string& rest);
    void CommandHelp() const;
    void CommandRoom(const std::vector<std::string>& args, const std::string& rest);
    void CommandChat(const std::vector<std::string>& args, const std::string& rest);
    void CommandVoice(const std::vector<std::string>& args);
    void CommandStat() const;
    void CommandFire(const std::vector<std::string>& args);
    void CommandBoard() const;
    void CommandGame() const;
    void CommandConfig(const std::vector<std::string>& args);
    void CommandAutoplay(const std::vector<std::string>& args);
    bool CommandExpect(const std::vector<std::string>& args);

    /// Polls @p predicate until it holds or the deadline passes. Scripts use
    /// this instead of fixed sleeps so a slow machine does not turn into a
    /// spurious failure.
    bool WaitFor(const std::function<bool()>& predicate, uint32 timeoutMs) const;

    Settings _settings;

    ClientServiceRef _matchService;
    ClientServiceRef _chatService;
    ClientServiceRef _instanceService;
    Ref<UdpSocket> _voiceSocket;
    JobQueueRef _timerQueue;

    mutable std::mutex _lock;
    SessionRef _matchSession;
    SessionRef _chatSession;
    SessionRef _instanceSession;
    NetAddress _voiceServerAddress;
    std::string _voiceTicket;
    std::string _pendingChatTicket;
    std::string _chatHost;
    uint16 _chatPort = 0;
    std::vector<proto::RoomMemberInfo> _roomMembers;
    std::string _lastChatMessage;
    SessionId _hostSessionId = kInvalidSessionId;
    /// Most recent search result, so a script can join by name instead of
    /// hardcoding an id that changes every time the server restarts.
    std::vector<proto::RoomSummary> _lastRoomList;

    std::atomic<SessionId> _sessionId{kInvalidSessionId};
    std::atomic<bool> _authenticated{false};
    std::atomic<bool> _chatReady{false};
    std::atomic<bool> _voiceReady{false};
    std::atomic<bool> _voiceSending{false};
    std::atomic<bool> _inInstance{false};
    std::atomic<RoomId> _roomId{kInvalidRoomId};
    std::atomic<InstanceId> _instanceId{kInvalidInstanceId};
    std::atomic<bool> _running{true};

    /// Bumped on every S_RoomList, so a caller can tell a fresh reply from the
    /// previous one.
    std::atomic<uint32> _roomListVersion{0};

    // -- game state ---------------------------------------------------------
    // There is no renderer here, so the client keeps just enough of the world
    // to aim: the board, and the last snapshot of where everything was.
    proto::BoardSpec _boardSpec;
    proto::MatchConfig _matchConfig;
    std::vector<uint8> _boardCells;
    std::vector<proto::EntityState> _entities;
    uint32 _lastSnapshotTick = 0;

    std::atomic<uint8> _mySlot{0xFF};
    std::atomic<uint8> _myAmmo{0};
    std::atomic<uint8> _roundIndex{0};
    std::atomic<uint8> _myScore{0};
    std::atomic<bool> _matchSetupSeen{false};
    std::atomic<bool> _waveActive{false};
    std::atomic<bool> _gameOver{false};
    std::atomic<bool> _stunned{false};
    std::atomic<bool> _autoplay{false};
    std::atomic<uint16> _fireSequence{1};
    std::atomic<uint32> _cellsClaimedByMe{0};

    std::atomic<uint16> _voiceSequence{0};
    std::atomic<uint64> _voiceFramesSent{0};
    std::atomic<uint64> _voiceFramesReceived{0};
    mutable std::mutex _voiceStatLock;
    /// Received frame count per speaker: the routing assertion for scripted
    /// voice tests.
    std::unordered_map<SessionId, uint64> _voiceFramesBySpeaker;
    std::unordered_map<SessionId, bool> _mutedSpeakers;

    proto::PacketDispatcher<Session> _matchDispatcher;
    proto::PacketDispatcher<Session> _chatDispatcher;
    proto::PacketDispatcher<Session> _instanceDispatcher;
};

extern ClientApp* GClientApp;

}  // namespace nhn::test
