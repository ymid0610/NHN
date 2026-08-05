#include "match/ClientSession.h"

#include "match/MatchGlobal.h"
#include "match/MatchServer.h"
#include "match/RoomManager.h"
#include "match/SessionManager.h"

namespace nhn::match {

using namespace proto;

std::string ClientSession::GetNickname() const {
    std::lock_guard<std::mutex> guard(_lock);
    return _nickname;
}

void ClientSession::SetNickname(std::string nickname) {
    std::lock_guard<std::mutex> guard(_lock);
    _nickname = std::move(nickname);
}

void ClientSession::SendError(ResultCode result, std::string detail) {
    S_Error error;
    error.result = result;
    error.detail = std::move(detail);
    SendPacket(error);
}

void ClientSession::OnConnected() {
    LOG_DEBUG("client connected from {}", GetNetAddress().ToString());
}

void ClientSession::OnDisconnected() {
    const SessionId sessionId = GetSessionId();
    if (sessionId == kInvalidSessionId) {
        return;  // Never got past the hello; nothing was registered.
    }

    // Leaving the room is what triggers host migration, the member-left
    // broadcast and channel cleanup, so a dropped connection follows exactly
    // the same path as an explicit leave.
    if (const RoomId roomId = GetRoomId(); roomId != kInvalidRoomId) {
        if (RoomRef room = GRoomManager->Find(roomId)) {
            room->EnqueueLeave(sessionId, LeaveReason::Disconnected);
        }
    }

    GMatchServer->PushSessionClosed(sessionId);
    GSessionManager->Unregister(sessionId);

    LOG_INFO("session {} '{}' disconnected: {}", sessionId, GetNickname(), GetDisconnectCause());
}

void ClientSession::OnRecvPacket(uint8* buffer, int32 length) {
    GMatchServer->DispatchClientPacket(std::static_pointer_cast<ClientSession>(shared_from_this()),
                                       buffer, length);
}

}  // namespace nhn::match
