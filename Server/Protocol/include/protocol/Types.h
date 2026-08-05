#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/Archive.h"
#include "core/Types.h"

namespace nhn::proto {

/// Room shapes. Capacity is data, not code — adding a six-player mode is one
/// row in the table in Protocol.cpp plus one enumerator here.
enum class RoomType : uint8 {
    None = 0,
    Duo = 1,   // 2 players
    Quad = 2,  // 4 players
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
    RoomTypeInvalid = 207,
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
const char* ToString(RoomType type);
const char* ToString(RoomState state);
const char* ToString(ChannelType type);
const char* ToString(ServerType type);
const char* ToString(LeaveReason reason);

/// Static description of a room shape.
struct RoomTypeInfo {
    RoomType type = RoomType::None;
    uint8 capacity = 0;
    /// Players required before the host may start. Equal to capacity today —
    /// arcade modes are full-lobby — but kept separate so a mode can allow
    /// starting short-handed without touching the start logic.
    uint8 minimumToStart = 0;
    const char* name = "";
};

const RoomTypeInfo* FindRoomType(RoomType type);
uint8 RoomCapacity(RoomType type);
bool IsValidRoomType(RoomType type);

/// Parses "duo"/"2" style text from the test client.
RoomType ParseRoomType(std::string_view text);

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

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId & nickname & slot & isHost & isReady;
    }
};

/// One row of a room search result. Deliberately excludes the member list —
/// browsing a lobby should not cost a full roster per room.
struct RoomSummary {
    RoomId roomId = kInvalidRoomId;
    std::string name;
    RoomType roomType = RoomType::None;
    RoomState state = RoomState::Waiting;
    uint8 memberCount = 0;
    uint8 capacity = 0;
    /// Locked rooms stay listed; only entry requires the password.
    bool hasPassword = false;
    std::string hostNickname;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & roomId & name & roomType & state & memberCount & capacity & hasPassword &
            hostNickname;
    }
};

struct RoomDetail {
    RoomId roomId = kInvalidRoomId;
    std::string name;
    RoomType roomType = RoomType::None;
    RoomState state = RoomState::Waiting;
    uint8 capacity = 0;
    bool hasPassword = false;
    SessionId hostSessionId = kInvalidSessionId;
    std::vector<RoomMemberInfo> members;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & roomId & name & roomType & state & capacity & hasPassword & hostSessionId & members;
    }
};

}  // namespace nhn::proto
