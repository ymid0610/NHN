#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "core/Archive.h"
#include "core/Types.h"

namespace nhn::proto {

/// A room's game mode. Room capacity, board shape and the settings the host may
/// pick all come from one table in Protocol.cpp, so a new mode is a row plus an
/// enumerator here.
///
/// Mode and room type are the same thing on purpose: there is one game, quick
/// match is per mode, and keeping them separate would only mean the matchmaker
/// had to read into a room's settings to answer "is this the mode I want".
enum class GameMode : uint8 {
    None = 0,
    TicTacToe = 1,  // 3x3,   three in a row, 2 players
    Gomoku9 = 2,    // 9x9,   four in a row,  2-4 players
    Gomoku15 = 3,   // 15x15, five in a row,  2-4 players
};

enum class RoomState : uint8 {
    Waiting = 0,
    /// Handing off to an instance server. Joins and leaves are refused while in
    /// this state so the member list cannot change mid-handoff.
    Starting = 1,
    InGame = 2,
    Closing = 3,
};

enum class ChannelType : uint8 {
    None = 0,
    Global = 1,
    Room = 2,
    Instance = 3,
};

enum class ServerType : uint8 {
    None = 0,
    Match = 1,
    Chat = 2,
    Voice = 3,
    Instance = 4,
};

enum class LeaveReason : uint8 {
    Left = 0,
    Disconnected = 1,
    Kicked = 2,
    RoomClosed = 3,
    MovedToInstance = 4,
};

enum class ResultCode : uint16 {
    Ok = 0,

    UnknownError = 1,
    InvalidRequest = 2,
    NotAuthenticated = 3,
    InternalError = 4,
    ServerFull = 5,
    RateLimited = 6,

    NicknameInvalid = 100,
    AlreadyAuthenticated = 101,

    RoomNotFound = 200,
    RoomFull = 201,
    AlreadyInRoom = 202,
    NotInRoom = 203,
    WrongPassword = 204,
    PasswordRequired = 205,
    RoomNameInvalid = 206,
    GameModeInvalid = 207,
    RoomNotWaiting = 208,
    KickCooldown = 209,

    NotHost = 300,
    CannotTargetSelf = 301,
    TargetNotFound = 302,
    NotEnoughPlayers = 303,
    NotAllReady = 304,

    TicketInvalid = 400,
    TicketExpired = 401,
    TicketAlreadyUsed = 402,

    NoInstanceServer = 500,
    InstanceCreateFailed = 501,
    InstanceNotFound = 502,

    MessageEmpty = 600,
    MessageTooLong = 601,
    MessageInvalidEncoding = 602,
    NotInChannel = 603,
};

/// Short diagnostic name, used by the test client and server logs.
const char* ToString(ResultCode code);
const char* ToString(GameMode type);
const char* ToString(RoomState state);
const char* ToString(ChannelType type);
const char* ToString(ServerType type);
const char* ToString(LeaveReason reason);

/// Sentinel for both settings below. Ammo of zero is unlimited; a wave cap of
/// zero means the round runs until the board resolves itself.
inline constexpr uint8 kUnlimited = 0;

// ---------------------------------------------------------------------------
// Bots
// ---------------------------------------------------------------------------

/// 1 is barely a threat, 5 rarely misses. Zero means "not a bot".
inline constexpr uint8 kMinBotDifficulty = 1;
inline constexpr uint8 kMaxBotDifficulty = 5;
inline constexpr uint8 kDefaultBotDifficulty = 3;

/// Bot session ids are allocated from the top of the range.
///
/// A bot needs an id because it occupies a room slot, a board slot and a score
/// line exactly as a player does — but it must never be mistaken for a session
/// anything can send to. Keeping them in their own range makes that a test
/// rather than a convention.
inline constexpr SessionId kBotSessionBase = 0x4000'0000'0000'0000ull;

inline bool IsBotSession(SessionId sessionId) {
    return sessionId >= kBotSessionBase;
}

/// Everything that varies between modes, in one place.
struct GameModeDef {
    GameMode type = GameMode::None;

    uint8 boardWidth = 0;
    uint8 boardHeight = 0;
    /// Marks in a row needed to take the round.
    uint8 winLength = 0;

    uint8 capacity = 0;
    /// Players required before the host may start. Gomoku runs 2-4, so this is
    /// genuinely below capacity there.
    uint8 minimumToStart = 0;

    /// Ammo per wave, as offered in the lobby. kUnlimited allowed.
    ///
    /// Per-mode because a 3x3 board cannot absorb six shots per player per
    /// wave — the first pass would decide the entire round.
    std::array<uint8, 4> ammoOptions{};

    /// Wave caps offered in the lobby. kUnlimited allowed.
    ///
    /// Also per-mode, for a concrete reason: on 15x15 a low cap makes five in a
    /// row statistically unreachable and every round ends scoreless. Bigger
    /// boards get a bigger cap rather than a warning the player has to
    /// understand.
    std::array<uint8, 2> waveLimitOptions{};

    const char* name = "";
};

const GameModeDef* FindGameMode(GameMode type);
uint8 ModeCapacity(GameMode type);
bool IsValidGameMode(GameMode type);

/// True when the value is one the mode actually offers. The lobby is sent the
/// options, but nothing stops a crafted packet from asking for something else.
bool IsAmmoAllowed(GameMode type, uint8 ammoPerWave);
bool IsWaveLimitAllowed(GameMode type, uint8 waveLimit);

/// Parses "gomoku9"/"g9"/"9" style text from the test client.
GameMode ParseGameMode(std::string_view text);

/// Rejects control characters and validates UTF-8. Applied to every
/// client-supplied string before it is stored or echoed to other players;
/// without accounts there is no other line of defence against a client that
/// sends a nickname full of terminal escapes.
bool IsCleanUtf8(std::string_view text, uint32 maxLength);

// ---------------------------------------------------------------------------
// Shared payload structures
// ---------------------------------------------------------------------------

struct RoomMemberInfo {
    SessionId sessionId = kInvalidSessionId;
    std::string nickname;
    uint8 slot = 0;
    bool isHost = false;
    bool isReady = false;
    /// 0 for a person, 1..5 for a bot. Carried on the wire rather than derived
    /// from the id so a client can show the difficulty without knowing how ids
    /// are allocated.
    uint8 botDifficulty = 0;

    bool IsBot() const { return botDifficulty != 0; }

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId & nickname & slot & isHost & isReady & botDifficulty;
    }
};

/// One row of a room search result. Deliberately excludes the member list —
/// browsing a lobby should not cost a full roster per room.
struct RoomSummary {
    RoomId roomId = kInvalidRoomId;
    std::string name;
    GameMode mode = GameMode::None;
    RoomState state = RoomState::Waiting;
    uint8 memberCount = 0;
    uint8 capacity = 0;
    /// Locked rooms stay listed; only entry requires the password.
    bool hasPassword = false;
    std::string hostNickname;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & roomId & name & mode & state & memberCount & capacity & hasPassword &
            hostNickname;
    }
};

struct RoomDetail {
    RoomId roomId = kInvalidRoomId;
    std::string name;
    GameMode mode = GameMode::None;
    RoomState state = RoomState::Waiting;
    uint8 capacity = 0;
    bool hasPassword = false;
    SessionId hostSessionId = kInvalidSessionId;
    std::vector<RoomMemberInfo> members;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & roomId & name & mode & state & capacity & hasPassword & hostSessionId & members;
    }
};

}  // namespace nhn::proto
