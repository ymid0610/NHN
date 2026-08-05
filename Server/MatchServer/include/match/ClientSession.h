#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "core/Core.h"
#include "protocol/Dispatcher.h"
#include "protocol/Packets.h"

namespace nhn::match {

/// A connected player.
///
/// There is no account behind this: the session *is* the identity, and it lasts
/// exactly as long as the socket. Everything a player "is" — id, nickname,
/// which room they are in — lives here and dies with the connection.
class ClientSession : public PacketSession {
public:
    SessionId GetSessionId() const { return _sessionId.load(std::memory_order_acquire); }
    void SetSessionId(SessionId id) { _sessionId.store(id, std::memory_order_release); }

    bool IsAuthenticated() const { return _authenticated.load(std::memory_order_acquire); }
    void SetAuthenticated(bool value) { _authenticated.store(value, std::memory_order_release); }

    /// kInvalidRoomId when in the lobby. Written by the room's job queue and
    /// read by packet handlers, hence atomic.
    RoomId GetRoomId() const { return _roomId.load(std::memory_order_acquire); }
    void SetRoomId(RoomId id) { _roomId.store(id, std::memory_order_release); }

    std::string GetNickname() const;
    void SetNickname(std::string nickname);

    template <class T>
    void SendPacket(T&& packet) {
        Send(proto::MakePacket(std::forward<T>(packet)));
    }

    void SendError(proto::ResultCode result, std::string detail = {});

protected:
    void OnConnected() override;
    void OnDisconnected() override;
    void OnRecvPacket(uint8* buffer, int32 length) override;

private:
    std::atomic<SessionId> _sessionId{kInvalidSessionId};
    std::atomic<bool> _authenticated{false};
    std::atomic<RoomId> _roomId{kInvalidRoomId};

    mutable std::mutex _lock;
    std::string _nickname;
};

using ClientSessionRef = Ref<ClientSession>;

}  // namespace nhn::match
