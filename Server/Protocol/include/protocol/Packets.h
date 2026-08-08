#pragma once

#include "protocol/GameTypes.h"
#include "protocol/Types.h"

namespace nhn::proto {

/// Wire identifiers, grouped by link so an id read from a capture immediately
/// says which pair of processes it belongs to. Never renumber a live id.
///
///   1000..1999  client  <-> match
///   2000..2999  client  <-> chat
///   3000..3999  client  <-> instance
///   4000..4999  server  <-> server (control plane)
enum class PacketId : uint16 {
    None = 0,

    // -- client <-> match ---------------------------------------------------
    C_Hello = 1000,
    S_HelloAck = 1001,
    C_Ping = 1002,
    S_Pong = 1003,
    S_Error = 1004,

    C_RoomCreate = 1010,
    S_RoomCreateAck = 1011,
    C_RoomList = 1012,
    S_RoomList = 1013,
    C_RoomJoin = 1014,
    S_RoomJoinAck = 1015,
    C_RoomLeave = 1016,
    S_RoomLeaveAck = 1017,
    C_RoomKick = 1018,
    S_RoomKickAck = 1019,
    C_RoomReady = 1020,
    S_RoomReadyAck = 1021,
    C_RoomStart = 1022,
    S_RoomStartAck = 1023,
    C_QuickMatch = 1024,
    C_RoomSetConfig = 1025,
    S_RoomConfigChanged = 1026,
    C_QuickMatchCancel = 1027,
    S_QuickMatchQueued = 1028,
    S_QuickMatchCancelled = 1029,

    S_RoomMemberJoined = 1030,
    S_RoomMemberLeft = 1031,
    S_RoomMemberReady = 1032,
    S_RoomHostChanged = 1033,
    S_RoomKicked = 1034,
    S_RoomStateChanged = 1035,
    S_RoomClosed = 1036,
    S_GameStarting = 1037,

    C_RoomAddBot = 1040,
    C_RoomRemoveBot = 1041,
    C_RoomSetBotDifficulty = 1042,
    S_RoomBotChanged = 1043,

    // -- client <-> chat ----------------------------------------------------
    C_ChatHello = 2000,
    S_ChatHelloAck = 2001,
    C_ChatSend = 2002,
    S_ChatMessage = 2003,
    S_ChatNotice = 2004,
    S_ChatError = 2005,

    // -- client <-> instance ------------------------------------------------
    C_InstHello = 3000,
    S_InstHelloAck = 3001,
    C_InstLeave = 3002,
    S_InstMemberJoined = 3003,
    S_InstMemberLeft = 3004,
    S_InstStart = 3005,
    S_InstEnd = 3006,

    // -- the game -----------------------------------------------------------
    C_Fire = 3100,
    S_FireRejected = 3101,
    S_ShotResolved = 3102,
    S_MatchSetup = 3110,
    S_RoundStart = 3111,
    S_RoundEnd = 3112,
    S_WaveStart = 3113,
    S_WaveEnd = 3114,
    S_WorldSnapshot = 3115,
    S_CellClaimed = 3116,
    S_ItemTriggered = 3117,
    S_StatusChanged = 3118,
    S_AmmoChanged = 3119,
    S_GameOver = 3120,

    // -- control plane ------------------------------------------------------
    P_ServerHello = 4000,
    P_ServerHelloAck = 4001,
    P_Heartbeat = 4002,
    P_HeartbeatAck = 4003,

    P_TicketAdd = 4010,
    P_TicketRevoke = 4011,

    P_ChannelJoin = 4020,
    P_ChannelLeave = 4021,
    P_SessionClosed = 4022,

    P_InstanceCreate = 4030,
    P_InstanceCreateAck = 4031,
    P_InstanceReady = 4032,
    P_InstanceClosed = 4033,
};

/// Every packet declares its id as `kId` and one Serialize method that drives
/// both reading and writing.
#define NHN_PACKET(name)                        \
    static constexpr PacketId kId = PacketId::name; \
    static constexpr const char* kName = #name

// ===========================================================================
// client <-> match
// ===========================================================================

struct C_Hello {
    NHN_PACKET(C_Hello);
    std::string nickname;
    std::string clientVersion;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & nickname & clientVersion;
    }
};

/// Carries the endpoints and one-shot tickets for the chat and voice servers.
/// The client connects to those directly; the match server never proxies for
/// them.
struct S_HelloAck {
    NHN_PACKET(S_HelloAck);
    ResultCode result = ResultCode::Ok;
    SessionId sessionId = kInvalidSessionId;
    std::string nickname;  // echoed back after validation

    std::string chatHost;
    uint16 chatPort = 0;
    std::string chatTicket;

    std::string voiceHost;
    uint16 voicePort = 0;
    std::string voiceTicket;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & sessionId & nickname & chatHost & chatPort & chatTicket & voiceHost &
            voicePort & voiceTicket;
    }
};

struct C_Ping {
    NHN_PACKET(C_Ping);
    int64 clientTimeMs = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & clientTimeMs;
    }
};

struct S_Pong {
    NHN_PACKET(S_Pong);
    int64 clientTimeMs = 0;
    int64 serverTimeMs = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & clientTimeMs & serverTimeMs;
    }
};

struct S_Error {
    NHN_PACKET(S_Error);
    ResultCode result = ResultCode::UnknownError;
    std::string detail;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & detail;
    }
};

struct C_RoomCreate {
    NHN_PACKET(C_RoomCreate);
    std::string name;
    GameMode mode = GameMode::None;
    /// Empty means an open room. A non-empty password makes the room locked but
    /// still listed.
    std::string password;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & name & mode & password;
    }
};

struct S_RoomCreateAck {
    NHN_PACKET(S_RoomCreateAck);
    ResultCode result = ResultCode::Ok;
    RoomDetail room;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & room;
    }
};

struct C_RoomList {
    NHN_PACKET(C_RoomList);
    /// GameMode::None means "any".
    GameMode mode = GameMode::None;
    std::string nameFilter;
    bool hideFull = false;
    bool hideLocked = false;
    uint16 page = 0;
    uint16 pageSize = 20;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & mode & nameFilter & hideFull & hideLocked & page & pageSize;
    }
};

struct S_RoomList {
    NHN_PACKET(S_RoomList);
    std::vector<RoomSummary> rooms;
    uint32 totalCount = 0;
    uint16 page = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & rooms & totalCount & page;
    }
};

struct C_RoomJoin {
    NHN_PACKET(C_RoomJoin);
    RoomId roomId = kInvalidRoomId;
    std::string password;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & roomId & password;
    }
};

struct S_RoomJoinAck {
    NHN_PACKET(S_RoomJoinAck);
    ResultCode result = ResultCode::Ok;
    RoomDetail room;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & room;
    }
};

struct C_RoomLeave {
    NHN_PACKET(C_RoomLeave);
    template <class Ar>
    void Serialize(Ar&) {}
};

struct S_RoomLeaveAck {
    NHN_PACKET(S_RoomLeaveAck);
    ResultCode result = ResultCode::Ok;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result;
    }
};

struct C_RoomKick {
    NHN_PACKET(C_RoomKick);
    SessionId targetSessionId = kInvalidSessionId;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & targetSessionId;
    }
};

struct S_RoomKickAck {
    NHN_PACKET(S_RoomKickAck);
    ResultCode result = ResultCode::Ok;
    SessionId targetSessionId = kInvalidSessionId;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & targetSessionId;
    }
};

struct C_RoomReady {
    NHN_PACKET(C_RoomReady);
    bool ready = false;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & ready;
    }
};

struct S_RoomReadyAck {
    NHN_PACKET(S_RoomReadyAck);
    ResultCode result = ResultCode::Ok;
    bool ready = false;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & ready;
    }
};

struct C_RoomStart {
    NHN_PACKET(C_RoomStart);
    template <class Ar>
    void Serialize(Ar&) {}
};

struct S_RoomStartAck {
    NHN_PACKET(S_RoomStartAck);
    ResultCode result = ResultCode::Ok;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result;
    }
};

/// "Put me in a game of this mode."
///
/// Joins a matchmaking queue. There is no room and no lobby: when enough players
/// are waiting the server starts an instance with the mode's default settings
/// and everyone receives S_GameStarting directly. Answered immediately with
/// S_QuickMatchQueued so the client can show progress while it waits.
struct C_QuickMatch {
    NHN_PACKET(C_QuickMatch);
    GameMode mode = GameMode::None;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & mode;
    }
};

/// Queue accepted, or refused with a reason.
///
/// Re-sent to everyone already waiting whenever the count changes, so a client
/// can show "2/4" without polling.
struct S_QuickMatchQueued {
    NHN_PACKET(S_QuickMatchQueued);
    ResultCode result = ResultCode::Ok;
    GameMode mode = GameMode::None;
    /// Players waiting for this mode, including the recipient.
    uint8 waiting = 0;
    /// Enough to start once the fill window closes.
    uint8 needed = 0;
    /// Starts at once on reaching this many.
    uint8 capacity = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & mode & waiting & needed & capacity;
    }
};

struct C_QuickMatchCancel {
    NHN_PACKET(C_QuickMatchCancel);
    template <class Ar>
    void Serialize(Ar&) {}
};

struct S_QuickMatchCancelled {
    NHN_PACKET(S_QuickMatchCancelled);
    /// NotInRoom when the caller was not queued — including the case where the
    /// match formed a moment before the cancel arrived, in which case
    /// S_GameStarting is already on its way.
    ResultCode result = ResultCode::Ok;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result;
    }
};

// ---------------------------------------------------------------------------
// Bots
//
// A bot is a room member like any other: it holds a slot, appears in the
// roster, counts towards the minimum to start and plays a board slot. It simply
// has no connection behind it, so the server fires its shots.
// ---------------------------------------------------------------------------

/// Host-only. Fails with RoomFull when there is no slot left.
struct C_RoomAddBot {
    NHN_PACKET(C_RoomAddBot);
    uint8 difficulty = kDefaultBotDifficulty;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & difficulty;
    }
};

/// Host-only. Refuses a session id that belongs to a person — removing those is
/// what the kick packet is for.
struct C_RoomRemoveBot {
    NHN_PACKET(C_RoomRemoveBot);
    SessionId sessionId = kInvalidSessionId;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId;
    }
};

/// Host-only. Out-of-range values are clamped into 1..5 rather than rejected.
struct C_RoomSetBotDifficulty {
    NHN_PACKET(C_RoomSetBotDifficulty);
    SessionId sessionId = kInvalidSessionId;
    uint8 difficulty = kDefaultBotDifficulty;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId & difficulty;
    }
};

/// Broadcast after a successful add or difficulty change, and sent to the
/// requester alone when one fails.
///
/// Removal rides on the ordinary S_RoomMemberLeft, since that is exactly what
/// happened as far as the roster is concerned.
struct S_RoomBotChanged {
    NHN_PACKET(S_RoomBotChanged);
    ResultCode result = ResultCode::Ok;
    RoomMemberInfo bot;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & bot;
    }
};

/// Host-only. The server clamps whatever arrives to something the mode allows
/// rather than rejecting it, then broadcasts the result.
struct C_RoomSetConfig {
    NHN_PACKET(C_RoomSetConfig);
    MatchConfig config;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & config;
    }
};

struct S_RoomConfigChanged {
    NHN_PACKET(S_RoomConfigChanged);
    MatchConfig config;
    /// Items that will actually spawn under these settings. Sent so the lobby
    /// UI and the spawner cannot disagree.
    uint32 effectiveItemMask = 0;
    /// False when the request had to be adjusted, so the sender can tell its
    /// choice did not stick.
    bool accepted = true;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & config & effectiveItemMask & accepted;
    }
};

struct S_RoomMemberJoined {
    NHN_PACKET(S_RoomMemberJoined);
    RoomMemberInfo member;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & member;
    }
};

struct S_RoomMemberLeft {
    NHN_PACKET(S_RoomMemberLeft);
    SessionId sessionId = kInvalidSessionId;
    LeaveReason reason = LeaveReason::Left;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId & reason;
    }
};

struct S_RoomMemberReady {
    NHN_PACKET(S_RoomMemberReady);
    SessionId sessionId = kInvalidSessionId;
    bool ready = false;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId & ready;
    }
};

struct S_RoomHostChanged {
    NHN_PACKET(S_RoomHostChanged);
    SessionId hostSessionId = kInvalidSessionId;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & hostSessionId;
    }
};

/// Sent only to the player being removed; everyone else sees S_RoomMemberLeft.
struct S_RoomKicked {
    NHN_PACKET(S_RoomKicked);
    RoomId roomId = kInvalidRoomId;
    uint32 cooldownSeconds = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & roomId & cooldownSeconds;
    }
};

struct S_RoomStateChanged {
    NHN_PACKET(S_RoomStateChanged);
    RoomState state = RoomState::Waiting;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & state;
    }
};

struct S_RoomClosed {
    NHN_PACKET(S_RoomClosed);
    RoomId roomId = kInvalidRoomId;
    std::string reason;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & roomId & reason;
    }
};

struct S_GameStarting {
    NHN_PACKET(S_GameStarting);
    InstanceId instanceId = kInvalidInstanceId;
    std::string host;
    uint16 port = 0;
    std::string ticket;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & instanceId & host & port & ticket;
    }
};

// ===========================================================================
// client <-> chat
// ===========================================================================

struct C_ChatHello {
    NHN_PACKET(C_ChatHello);
    std::string ticket;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & ticket;
    }
};

struct S_ChatHelloAck {
    NHN_PACKET(S_ChatHelloAck);
    ResultCode result = ResultCode::Ok;
    SessionId sessionId = kInvalidSessionId;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & sessionId;
    }
};

/// The client names a channel kind, never a channel id: which room or instance
/// channel it may talk on is decided by the match server and pushed to the chat
/// server, so a client cannot address a channel it does not belong to.
struct C_ChatSend {
    NHN_PACKET(C_ChatSend);
    ChannelType channelType = ChannelType::Global;
    std::string message;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & channelType & message;
    }
};

struct S_ChatMessage {
    NHN_PACKET(S_ChatMessage);
    ChannelType channelType = ChannelType::Global;
    SessionId senderSessionId = kInvalidSessionId;
    std::string nickname;
    std::string message;
    int64 timestampMs = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & channelType & senderSessionId & nickname & message & timestampMs;
    }
};

struct S_ChatNotice {
    NHN_PACKET(S_ChatNotice);
    ChannelType channelType = ChannelType::Global;
    std::string message;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & channelType & message;
    }
};

struct S_ChatError {
    NHN_PACKET(S_ChatError);
    ResultCode result = ResultCode::UnknownError;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result;
    }
};

// ===========================================================================
// client <-> instance
// ===========================================================================

struct C_InstHello {
    NHN_PACKET(C_InstHello);
    std::string ticket;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & ticket;
    }
};

struct S_InstHelloAck {
    NHN_PACKET(S_InstHelloAck);
    ResultCode result = ResultCode::Ok;
    InstanceId instanceId = kInvalidInstanceId;
    uint8 slot = 0;
    std::vector<RoomMemberInfo> members;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & instanceId & slot & members;
    }
};

struct C_InstLeave {
    NHN_PACKET(C_InstLeave);
    template <class Ar>
    void Serialize(Ar&) {}
};

struct S_InstMemberJoined {
    NHN_PACKET(S_InstMemberJoined);
    RoomMemberInfo member;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & member;
    }
};

struct S_InstMemberLeft {
    NHN_PACKET(S_InstMemberLeft);
    SessionId sessionId = kInvalidSessionId;
    LeaveReason reason = LeaveReason::Left;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId & reason;
    }
};

/// Everyone is connected; the game mode may begin. Game logic hangs off here.
struct S_InstStart {
    NHN_PACKET(S_InstStart);
    int64 serverTimeMs = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & serverTimeMs;
    }
};

struct S_InstEnd {
    NHN_PACKET(S_InstEnd);
    std::string reason;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & reason;
    }
};

// ===========================================================================
// the game
//
// The client sends where it aimed and when. Everything else — what was there at
// that moment, who got the cell, what the item did — is decided here.
// ===========================================================================

/// @param aimX,aimY  point on the target plane, in the 1920x1080 field space
/// @param clientTick simulation tick the client was displaying when it fired;
///                   the server rewinds to it, bounded by kMaxRewindMs
/// @param sequence   echoed back so the client can reconcile its own prediction
struct C_Fire {
    NHN_PACKET(C_Fire);
    int32 aimX = 0;
    int32 aimY = 0;
    uint32 clientTick = 0;
    uint16 sequence = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & aimX & aimY & clientTick & sequence;
    }
};

struct S_FireRejected {
    NHN_PACKET(S_FireRejected);
    uint16 sequence = 0;
    ResultCode result = ResultCode::InvalidRequest;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sequence & result;
    }
};

struct S_ShotResolved {
    NHN_PACKET(S_ShotResolved);
    SessionId shooter = kInvalidSessionId;
    uint16 sequence = 0;
    HitKind kind = HitKind::Miss;
    int32 hitX = 0;
    int32 hitY = 0;
    uint32 entityId = 0;
    /// Board cell when kind is Cell or CellTaken, otherwise 0xFFFF.
    uint16 cellIndex = 0xFFFF;
    /// True when this came from a grenade fragment or a ricochet rather than a
    /// trigger pull, so the client does not animate a muzzle flash for it.
    bool derived = false;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & shooter & sequence & kind & hitX & hitY & entityId & cellIndex & derived;
    }
};

/// Sent once when the match begins, so a client knows the shape of everything
/// that follows.
struct S_MatchSetup {
    NHN_PACKET(S_MatchSetup);
    GameMode mode = GameMode::None;
    MatchConfig config;
    BoardSpec board;
    uint32 fieldWidth = 0;
    uint32 fieldHeight = 0;
    uint32 waveDurationMs = 0;
    std::vector<RoomMemberInfo> players;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & mode & config & board & fieldWidth & fieldHeight & waveDurationMs & players;
    }
};

struct S_RoundStart {
    NHN_PACKET(S_RoundStart);
    uint8 roundIndex = 0;
    uint8 roundCount = 0;
    /// Cell size for this round, drawn from the configured range.
    uint8 paperSize = 0;
    uint64 roundSeed = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & roundIndex & roundCount & paperSize & roundSeed;
    }
};

struct S_RoundEnd {
    NHN_PACKET(S_RoundEnd);
    uint8 roundIndex = 0;
    /// 0xFF when the round ended with no line completed; nobody scores.
    uint8 winnerSlot = 0xFF;
    std::vector<PlayerScore> scores;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & roundIndex & winnerSlot & scores;
    }
};

struct S_WaveStart {
    NHN_PACKET(S_WaveStart);
    uint16 waveIndex = 0;
    uint32 startTick = 0;
    uint32 endTick = 0;
    uint8 ammo = 0;  // kUnlimited allowed

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & waveIndex & startTick & endTick & ammo;
    }
};

struct S_WaveEnd {
    NHN_PACKET(S_WaveEnd);
    uint16 waveIndex = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & waveIndex;
    }
};

struct S_WorldSnapshot {
    NHN_PACKET(S_WorldSnapshot);
    uint32 tick = 0;
    std::vector<EntityState> entities;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & tick & entities;
    }
};

struct S_CellClaimed {
    NHN_PACKET(S_CellClaimed);
    uint16 cellIndex = 0;
    uint8 ownerSlot = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & cellIndex & ownerSlot;
    }
};

/// An item went off. The server decides who it landed on; the seed lets each
/// client draw the splatter or the ricochet arc without needing the geometry.
struct S_ItemTriggered {
    NHN_PACKET(S_ItemTriggered);
    ItemKind item = ItemKind::None;
    uint32 entityId = 0;
    SessionId instigator = kInvalidSessionId;
    /// 0xFF when the item does not single anyone out.
    uint8 victimSlot = 0xFF;
    uint32 seed = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & item & entityId & instigator & victimSlot & seed;
    }
};

struct S_StatusChanged {
    NHN_PACKET(S_StatusChanged);
    uint8 slot = 0;
    StatusFlags flags = StatusFlags::None;
    uint32 untilTick = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & slot & flags & untilTick;
    }
};

struct S_AmmoChanged {
    NHN_PACKET(S_AmmoChanged);
    uint8 slot = 0;
    uint8 ammo = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & slot & ammo;
    }
};

struct S_GameOver {
    NHN_PACKET(S_GameOver);
    /// More than one entry when the match ends level; scoreless rounds make
    /// that reachable even with an odd round count.
    std::vector<uint8> winnerSlots;
    std::vector<PlayerScore> scores;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & winnerSlots & scores;
    }
};

// ===========================================================================
// control plane
//
// Peers dial the match server, not the other way round, so the match server
// does not need their addresses configured — they announce themselves.
// ===========================================================================

struct P_ServerHello {
    NHN_PACKET(P_ServerHello);
    ServerType serverType = ServerType::None;
    std::string serverId;
    /// Address clients should be told to connect to, which is not necessarily
    /// the address this control link came from.
    std::string publicHost;
    uint16 publicPort = 0;
    /// WebSocket port for browser clients. Zero when this server has none.
    uint16 publicWebPort = 0;
    int32 capacity = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & serverType & serverId & publicHost & publicPort & publicWebPort & capacity;
    }
};

struct P_ServerHelloAck {
    NHN_PACKET(P_ServerHelloAck);
    ResultCode result = ResultCode::Ok;
    uint32 heartbeatIntervalMs = 2000;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & heartbeatIntervalMs;
    }
};

struct P_Heartbeat {
    NHN_PACKET(P_Heartbeat);
    int64 sentAtMs = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sentAtMs;
    }
};

struct P_HeartbeatAck {
    NHN_PACKET(P_HeartbeatAck);
    int64 sentAtMs = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sentAtMs;
    }
};

/// Pre-authorises one client connection. Pushed ahead of the client so the
/// receiving server can validate by table lookup without asking the match
/// server anything.
struct P_TicketAdd {
    NHN_PACKET(P_TicketAdd);
    std::string ticket;
    SessionId sessionId = kInvalidSessionId;
    std::string nickname;
    int64 expiresAtMs = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & ticket & sessionId & nickname & expiresAtMs;
    }
};

struct P_TicketRevoke {
    NHN_PACKET(P_TicketRevoke);
    std::string ticket;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & ticket;
    }
};

/// Channel membership is server-driven: after the initial ticketed connection,
/// the match server tells chat and voice which channels a session belongs to.
struct P_ChannelJoin {
    NHN_PACKET(P_ChannelJoin);
    SessionId sessionId = kInvalidSessionId;
    ChannelType channelType = ChannelType::None;
    uint64 channelId = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId & channelType & channelId;
    }
};

struct P_ChannelLeave {
    NHN_PACKET(P_ChannelLeave);
    SessionId sessionId = kInvalidSessionId;
    ChannelType channelType = ChannelType::None;
    uint64 channelId = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId & channelType & channelId;
    }
};

struct P_SessionClosed {
    NHN_PACKET(P_SessionClosed);
    SessionId sessionId = kInvalidSessionId;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & sessionId;
    }
};

struct P_InstanceCreate {
    NHN_PACKET(P_InstanceCreate);
    InstanceId instanceId = kInvalidInstanceId;
    RoomId roomId = kInvalidRoomId;
    GameMode mode = GameMode::None;
    /// Already clamped by the match server, so the instance can take it as-is.
    MatchConfig config;
    std::vector<RoomMemberInfo> members;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & instanceId & roomId & mode & config & members;
    }
};

struct P_InstanceCreateAck {
    NHN_PACKET(P_InstanceCreateAck);
    ResultCode result = ResultCode::Ok;
    InstanceId instanceId = kInvalidInstanceId;
    std::string host;
    uint16 port = 0;
    uint16 webPort = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & instanceId & host & port & webPort;
    }
};

struct P_InstanceReady {
    NHN_PACKET(P_InstanceReady);
    InstanceId instanceId = kInvalidInstanceId;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & instanceId;
    }
};

struct P_InstanceClosed {
    NHN_PACKET(P_InstanceClosed);
    InstanceId instanceId = kInvalidInstanceId;
    std::string reason;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & instanceId & reason;
    }
};

#undef NHN_PACKET

}  // namespace nhn::proto
