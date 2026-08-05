#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "common/RateLimiter.h"
#include "core/Core.h"
#include "protocol/Dispatcher.h"
#include "protocol/Packets.h"

namespace nhn::chat {

/// A player's chat connection.
///
/// Authenticated by a one-shot ticket the match server minted and pushed here
/// in advance; the chat server never asks the match server anything at connect
/// time.
class ChatSession : public PacketSession {
public:
    ChatSession();

    SessionId GetSessionId() const { return _sessionId.load(std::memory_order_acquire); }
    bool IsAuthenticated() const { return _authenticated.load(std::memory_order_acquire); }

    void Authenticate(SessionId sessionId, std::string nickname);

    std::string GetNickname() const;

    /// Which concrete channel this session's room / instance slot points at.
    /// Zero means it is not in one. Clients name only a ChannelType, so this is
    /// what turns "send to my room" into an actual channel.
    uint64 GetChannelId(proto::ChannelType type) const;
    void SetChannelId(proto::ChannelType type, uint64 channelId);

    /// @return false when the caller is over its budget and the message should
    ///         be rejected.
    bool ConsumeSendBudget();

    template <class T>
    void SendPacket(T&& packet) {
        Send(proto::MakePacket(std::forward<T>(packet)));
    }

protected:
    void OnDisconnected() override;
    void OnRecvPacket(uint8* buffer, int32 length) override;

private:
    std::atomic<SessionId> _sessionId{kInvalidSessionId};
    std::atomic<bool> _authenticated{false};
    std::atomic<uint64> _roomChannelId{0};
    std::atomic<uint64> _instanceChannelId{0};

    mutable std::mutex _lock;
    std::string _nickname;
    RateLimiter _sendLimiter;
};

using ChatSessionRef = Ref<ChatSession>;

}  // namespace nhn::chat
