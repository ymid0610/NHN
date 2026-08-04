#pragma once

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

    S_RoomMemberJoined = 1030,
    S_RoomMemberLeft = 1031,
    S_RoomMemberReady = 1032,
    S_RoomHostChanged = 1033,
    S_RoomKicked = 1034,
    S_RoomStateChanged = 1035,
    S_RoomClosed = 1036,
    S_GameStarting = 1037,

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
    RoomType roomType = RoomType::None;
    /// Empty means an open room. A non-empty password makes the room locked but
    /// still listed.
    std::string password;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & name & roomType & password;
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
    /// RoomType::None means "any".
    RoomType roomType = RoomType::None;
    std::string nameFilter;
    bool hideFull = false;
    bool hideLocked = false;
    uint16 page = 0;
    uint16 pageSize = 20;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & roomType & nameFilter & hideFull & hideLocked & page & pageSize;
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
    int32 capacity = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & serverType & serverId & publicHost & publicPort & capacity;
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
    RoomType roomType = RoomType::None;
    std::vector<RoomMemberInfo> members;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & instanceId & roomId & roomType & members;
    }
};

struct P_InstanceCreateAck {
    NHN_PACKET(P_InstanceCreateAck);
    ResultCode result = ResultCode::Ok;
    InstanceId instanceId = kInvalidInstanceId;
    std::string host;
    uint16 port = 0;

    template <class Ar>
    void Serialize(Ar& ar) {
        ar & result & instanceId & host & port;
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
