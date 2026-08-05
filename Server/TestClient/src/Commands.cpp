#include <algorithm>
#include <fstream>
#include <sstream>
#include <thread>

#include "testclient/ClientApp.h"

namespace nhn::test {

using namespace proto;

namespace {

std::vector<std::string> Tokenise(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

/// Everything after the @p count-th token, preserved verbatim. Chat messages
/// need their spacing intact.
std::string RemainderAfter(const std::string& line, size_t count) {
    size_t position = 0;
    size_t consumed = 0;
    while (consumed < count && position < line.size()) {
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
        while (position < line.size() &&
               !std::isspace(static_cast<unsigned char>(line[position]))) {
            ++position;
        }
        ++consumed;
    }
    while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position]))) {
        ++position;
    }
    return line.substr(position);
}

uint32 ParseUint(const std::string& text, uint32 fallback) {
    try {
        return static_cast<uint32>(std::stoul(text));
    } catch (...) {
        return fallback;
    }
}

uint64 ParseUint64(const std::string& text, uint64 fallback) {
    try {
        return std::stoull(text);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

bool ClientApp::ExecuteCommand(const std::string& line) {
    const std::string trimmed = line.substr(line.find_first_not_of(" \t") == std::string::npos
                                                ? line.size()
                                                : line.find_first_not_of(" \t"));
    if (trimmed.empty() || trimmed[0] == '#') {
        return true;
    }

    const std::vector<std::string> args = Tokenise(trimmed);
    if (args.empty()) {
        return true;
    }
    return HandleCommand(args, trimmed);
}

bool ClientApp::HandleCommand(const std::vector<std::string>& args, const std::string& rest) {
    const std::string& command = args[0];

    if (command == "quit" || command == "exit") {
        return false;
    }

    if (command == "help") {
        CommandHelp();
        return true;
    }

    if (command == "echo") {
        Console::Print("{}", RemainderAfter(rest, 1));
        return true;
    }

    if (command == "sleep") {
        const uint32 ms = args.size() > 1 ? ParseUint(args[1], 100) : 100;
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return true;
    }

    if (command == "run") {
        if (args.size() < 2) {
            Console::Error("usage: run <script>");
            return true;
        }
        RunScript(args[1]);
        return true;
    }

    if (command == "nick") {
        if (args.size() < 2) {
            Console::Error("usage: nick <name>");
            return true;
        }
        _settings.nickname = args[1];
        Console::Print("  nickname set to '{}'", _settings.nickname);
        return true;
    }

    if (command == "connect") {
        std::string host = _settings.matchHost;
        uint16 port = _settings.matchPort;
        if (args.size() > 1) {
            const std::string& target = args[1];
            const auto colon = target.rfind(':');
            if (colon != std::string::npos) {
                host = target.substr(0, colon);
                port = static_cast<uint16>(ParseUint(target.substr(colon + 1), port));
            } else {
                host = target;
            }
        }
        Console::Print("  connecting to {}:{} as '{}'", host, port, _settings.nickname);
        ConnectMatch(host, port);
        return true;
    }

    if (command == "ping") {
        C_Ping ping;
        ping.clientTimeMs = NowUnixMs();
        SendToMatch(ping);
        return true;
    }

    if (command == "room") {
        CommandRoom(args, rest);
        return true;
    }

    if (command == "chat") {
        CommandChat(args, rest);
        return true;
    }

    if (command == "voice") {
        CommandVoice(args);
        return true;
    }

    if (command == "stat") {
        CommandStat();
        return true;
    }

    if (command == "expect") {
        return CommandExpect(args);
    }

    if (command == "crash") {
        // Deliberate fault, to exercise the crash path end to end: reason,
        // symbolised stack, minidump, then the wait for acknowledgement.
        NHN_FATAL("deliberate crash requested from the console");
    }

    Console::Error("unknown command '{}' — try 'help'", command);
    return true;
}

void ClientApp::CommandRoom(const std::vector<std::string>& args, const std::string& rest) {
    if (args.size() < 2) {
        Console::Error("usage: room create|list|join|leave|kick|ready|start ...");
        return;
    }

    const std::string& subcommand = args[1];

    if (subcommand == "create") {
        if (args.size() < 3) {
            Console::Error("usage: room create <duo|quad|2|4> [name] [password]");
            return;
        }
        C_RoomCreate create;
        create.roomType = ParseRoomType(args[2]);
        create.name = args.size() > 3 ? args[3] : "room";
        create.password = args.size() > 4 ? args[4] : "";
        if (create.roomType == RoomType::None) {
            Console::Error("unknown room type '{}'", args[2]);
            return;
        }
        SendToMatch(create);
        return;
    }

    if (subcommand == "quick") {
        if (args.size() < 3) {
            Console::Error("usage: room quick <duo|quad|2|4>");
            return;
        }
        C_QuickMatch quick;
        quick.roomType = ParseRoomType(args[2]);
        if (quick.roomType == RoomType::None) {
            Console::Error("unknown room type '{}'", args[2]);
            return;
        }
        SendToMatch(quick);
        return;
    }

    if (subcommand == "list") {
        C_RoomList request;
        request.pageSize = 20;
        for (size_t i = 2; i < args.size(); ++i) {
            const std::string& option = args[i];
            if (option.starts_with("--type=")) {
                request.roomType = ParseRoomType(option.substr(7));
            } else if (option.starts_with("--name=")) {
                request.nameFilter = option.substr(7);
            } else if (option.starts_with("--page=")) {
                request.page = static_cast<uint16>(ParseUint(option.substr(7), 0));
            } else if (option == "--nofull") {
                request.hideFull = true;
            } else if (option == "--nolocked") {
                request.hideLocked = true;
            }
        }
        SendToMatch(request);
        return;
    }

    if (subcommand == "join") {
        if (args.size() < 3) {
            Console::Error("usage: room join <roomId> [password]");
            return;
        }
        C_RoomJoin join;
        join.roomId = ParseUint64(args[2], 0);
        join.password = args.size() > 3 ? args[3] : "";
        SendToMatch(join);
        return;
    }

    // Room ids keep counting up for the lifetime of the match server, so a
    // script that hardcodes "join 1" breaks on the second run. Joining by name
    // searches first and then joins whatever id came back.
    if (subcommand == "joinname") {
        if (args.size() < 3) {
            Console::Error("usage: room joinname <name> [password]");
            return;
        }
        const std::string& wanted = args[2];
        const std::string password = args.size() > 3 ? args[3] : "";

        const uint32 versionBefore = _roomListVersion.load();

        C_RoomList request;
        request.nameFilter = wanted;
        request.pageSize = 50;
        SendToMatch(request);

        if (!WaitFor([this, versionBefore] { return _roomListVersion.load() != versionBefore; },
                     5000)) {
            Console::Error("no room list came back");
            return;
        }

        RoomId found = kInvalidRoomId;
        {
            std::lock_guard<std::mutex> guard(_lock);
            for (const RoomSummary& room : _lastRoomList) {
                if (room.name == wanted) {
                    found = room.roomId;
                    break;
                }
            }
        }
        if (found == kInvalidRoomId) {
            Console::Error("no room named '{}'", wanted);
            return;
        }

        C_RoomJoin join;
        join.roomId = found;
        join.password = password;
        SendToMatch(join);
        return;
    }

    if (subcommand == "leave") {
        SendToMatch(C_RoomLeave{});
        return;
    }

    if (subcommand == "kick") {
        if (args.size() < 3) {
            Console::Error("usage: room kick <sessionId|slot:<n>|nick:<name>>");
            return;
        }

        C_RoomKick kick;
        const std::string& target = args[2];

        // Session ids are not memorable, so slot and nickname forms exist for
        // hand-driven testing.
        if (target.starts_with("slot:") || target.starts_with("nick:")) {
            const bool bySlot = target.starts_with("slot:");
            const std::string key = target.substr(5);
            std::lock_guard<std::mutex> guard(_lock);
            for (const RoomMemberInfo& member : _roomMembers) {
                if ((bySlot && std::to_string(member.slot) == key) ||
                    (!bySlot && member.nickname == key)) {
                    kick.targetSessionId = member.sessionId;
                    break;
                }
            }
            if (kick.targetSessionId == kInvalidSessionId) {
                Console::Error("no room member matching '{}'", target);
                return;
            }
        } else {
            kick.targetSessionId = ParseUint64(target, 0);
        }

        SendToMatch(kick);
        return;
    }

    if (subcommand == "ready") {
        C_RoomReady ready;
        ready.ready = args.size() < 3 || args[2] != "off";
        SendToMatch(ready);
        return;
    }

    if (subcommand == "start") {
        SendToMatch(C_RoomStart{});
        return;
    }

    if (subcommand == "info") {
        std::lock_guard<std::mutex> guard(_lock);
        Console::Print("  room {} — {} member(s), host session {}", _roomId.load(),
                       _roomMembers.size(), _hostSessionId);
        for (const RoomMemberInfo& member : _roomMembers) {
            Console::Print("    slot {} session {:<4} {:<16} {}{}", member.slot, member.sessionId,
                           member.nickname, member.isHost ? "[host] " : "",
                           member.isReady ? "[ready]" : "");
        }
        return;
    }

    Console::Error("unknown room subcommand '{}'", subcommand);
    (void)rest;
}

void ClientApp::CommandChat(const std::vector<std::string>& args, const std::string& rest) {
    if (args.size() < 3) {
        Console::Error("usage: chat global|room|instance <message>");
        return;
    }

    C_ChatSend send;
    if (args[1] == "global") {
        send.channelType = ChannelType::Global;
    } else if (args[1] == "room") {
        send.channelType = ChannelType::Room;
    } else if (args[1] == "instance") {
        send.channelType = ChannelType::Instance;
    } else {
        Console::Error("unknown channel '{}'", args[1]);
        return;
    }
    send.message = RemainderAfter(rest, 2);

    SessionRef session;
    {
        std::lock_guard<std::mutex> guard(_lock);
        session = _chatSession;
    }
    if (session == nullptr) {
        Console::Error("not connected to the chat server");
        return;
    }
    session->Send(MakePacket(send));
}

void ClientApp::CommandVoice(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        Console::Error("usage: voice on|off|mute|unmute|stat");
        return;
    }

    if (args[1] == "on") {
        if (!_voiceReady.load()) {
            Console::Error("voice is not registered yet");
            return;
        }
        if (_voiceSending.exchange(true)) {
            return;
        }
        Console::Print("  sending synthetic voice every {} ms", _settings.voiceFrameMs);
        ScheduleVoiceFrame();
        return;
    }

    if (args[1] == "off") {
        _voiceSending.store(false);
        Console::Print("  voice stopped");
        return;
    }

    if (args[1] == "mute" || args[1] == "unmute") {
        if (args.size() < 3) {
            Console::Error("usage: voice {} <sessionId>", args[1]);
            return;
        }
        const SessionId target = ParseUint64(args[2], 0);
        std::lock_guard<std::mutex> guard(_voiceStatLock);
        _mutedSpeakers[target] = (args[1] == "mute");
        Console::Print("  {} session {}", args[1] == "mute" ? "muted" : "unmuted", target);
        return;
    }

    if (args[1] == "stat") {
        std::lock_guard<std::mutex> guard(_voiceStatLock);
        Console::Print("  voice: sent {}, received {}", _voiceFramesSent.load(),
                       _voiceFramesReceived.load());
        for (const auto& [speaker, count] : _voiceFramesBySpeaker) {
            Console::Print("    from session {}: {} frames{}", speaker, count,
                           _mutedSpeakers.count(speaker) && _mutedSpeakers.at(speaker)
                               ? " (muted)"
                               : "");
        }
        return;
    }

    Console::Error("unknown voice subcommand '{}'", args[1]);
}

void ClientApp::CommandStat() const {
    std::lock_guard<std::mutex> guard(_lock);
    Console::Print("  session      {}", _sessionId.load());
    Console::Print("  nickname     {}", _settings.nickname);
    Console::Print("  match        {}", _matchSession != nullptr ? "connected" : "-");
    Console::Print("  authed       {}", _authenticated.load());
    Console::Print("  chat         {}", _chatReady.load() ? "ready" : "-");
    Console::Print("  voice        {}{}", _voiceReady.load() ? "ready" : "-",
                   _voiceSending.load() ? " (sending)" : "");
    Console::Print("  room         {}", _roomId.load());
    Console::Print("  members      {}", _roomMembers.size());
    Console::Print("  instance     {}",
                   _inInstance.load() ? std::to_string(_instanceId.load()) : "-");
    Console::Print("  voice frames sent={} received={}", _voiceFramesSent.load(),
                   _voiceFramesReceived.load());
}

bool ClientApp::CommandExpect(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        Console::Error("usage: expect hello|chat|voice|room|members <n>|ingame|noroom [timeoutMs]");
        return true;
    }

    const std::string& what = args[1];
    uint32 timeoutMs = 5000;
    std::function<bool()> predicate;

    if (what == "hello") {
        timeoutMs = args.size() > 2 ? ParseUint(args[2], timeoutMs) : timeoutMs;
        predicate = [this] { return _authenticated.load(); };
    } else if (what == "chat") {
        timeoutMs = args.size() > 2 ? ParseUint(args[2], timeoutMs) : timeoutMs;
        predicate = [this] { return _chatReady.load(); };
    } else if (what == "voice") {
        timeoutMs = args.size() > 2 ? ParseUint(args[2], timeoutMs) : timeoutMs;
        predicate = [this] { return _voiceReady.load(); };
    } else if (what == "room") {
        timeoutMs = args.size() > 2 ? ParseUint(args[2], timeoutMs) : timeoutMs;
        predicate = [this] { return _roomId.load() != kInvalidRoomId; };
    } else if (what == "noroom") {
        timeoutMs = args.size() > 2 ? ParseUint(args[2], timeoutMs) : timeoutMs;
        predicate = [this] { return _roomId.load() == kInvalidRoomId; };
    } else if (what == "ingame") {
        timeoutMs = args.size() > 2 ? ParseUint(args[2], timeoutMs) : timeoutMs;
        predicate = [this] { return _inInstance.load(); };
    } else if (what == "members") {
        if (args.size() < 3) {
            Console::Error("usage: expect members <count> [timeoutMs]");
            return true;
        }
        const size_t expected = ParseUint(args[2], 0);
        timeoutMs = args.size() > 3 ? ParseUint(args[3], timeoutMs) : timeoutMs;
        predicate = [this, expected] {
            std::lock_guard<std::mutex> guard(_lock);
            return _roomMembers.size() == expected;
        };
    } else if (what == "voiceframes") {
        if (args.size() < 3) {
            Console::Error("usage: expect voiceframes <count> [timeoutMs]");
            return true;
        }
        const uint64 expected = ParseUint64(args[2], 0);
        timeoutMs = args.size() > 3 ? ParseUint(args[3], timeoutMs) : timeoutMs;
        predicate = [this, expected] { return _voiceFramesReceived.load() >= expected; };
    } else {
        Console::Error("unknown expectation '{}'", what);
        return true;
    }

    if (WaitFor(predicate, timeoutMs)) {
        Console::Print("  expect {} — ok", what);
        return true;
    }

    // A failed expectation in a script is a test failure, and stopping at the
    // first one keeps the output readable instead of cascading.
    Console::Error("expect {} — TIMED OUT after {} ms", what, timeoutMs);
    return true;
}

void ClientApp::RunScript(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        Console::Error("cannot open script '{}'", path);
        return;
    }

    Console::Print("  --- running {} ---", path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        Console::Print("> {}", line);
        if (!ExecuteCommand(line)) {
            break;
        }
    }
    Console::Print("  --- end of {} ---", path);
}

void ClientApp::CommandHelp() const {
    Console::Print(R"(
 connection
   connect [host:port]           connect to the match server and say hello
   nick <name>                   set the nickname used on the next connect
   ping                          round-trip check
   stat                          print client state

 rooms
   room quick <duo|quad|2|4>     quick match: join the fullest open room, or open one
   room create <duo|quad|2|4> [name] [password]
   room list [--type=x] [--name=y] [--page=n] [--nofull] [--nolocked]
   room join <roomId> [password]
   room joinname <name> [password]   search by name, then join - use this in scripts
   room leave
   room kick <sessionId | slot:<n> | nick:<name>>
   room ready [on|off]
   room start
   room info                     print the local view of the room

 chat
   chat global <message>
   chat room <message>
   chat instance <message>

 voice                           synthetic frames, no microphone
   voice on | off
   voice mute <sessionId> | unmute <sessionId>
   voice stat                    per-speaker frame counts

 scripting
   run <script>                  replay a command file
   sleep <ms>
   echo <text>
   expect hello|chat|voice|room|noroom|ingame [timeoutMs]
   expect members <count> [timeoutMs]
   expect voiceframes <count> [timeoutMs]

 other
   crash                         deliberately fault, to test the crash handler
   quit)");
}

}  // namespace nhn::test
