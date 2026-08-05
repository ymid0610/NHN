#include "chat/ChatSession.h"

#include "chat/ChatServer.h"

namespace nhn::chat {

using namespace proto;

ChatSession::ChatSession()
    : _sendLimiter(GChatServer != nullptr ? GChatServer->GetSettings().messageBurst : 4,
                   GChatServer != nullptr ? GChatServer->GetSettings().messagesPerSecond : 1.5) {}

void ChatSession::Authenticate(SessionId sessionId, std::string nickname) {
    {
        std::lock_guard<std::mutex> guard(_lock);
        _nickname = std::move(nickname);
    }
    _sessionId.store(sessionId, std::memory_order_release);
    _authenticated.store(true, std::memory_order_release);
}

std::string ChatSession::GetNickname() const {
    std::lock_guard<std::mutex> guard(_lock);
    return _nickname;
}

uint64 ChatSession::GetChannelId(ChannelType type) const {
    switch (type) {
        case ChannelType::Global:
            return ChannelManager::kGlobalChannelId;
        case ChannelType::Room:
            return _roomChannelId.load(std::memory_order_acquire);
        case ChannelType::Instance:
            return _instanceChannelId.load(std::memory_order_acquire);
        default:
            return 0;
    }
}

void ChatSession::SetChannelId(ChannelType type, uint64 channelId) {
    switch (type) {
        case ChannelType::Room:
            _roomChannelId.store(channelId, std::memory_order_release);
            break;
        case ChannelType::Instance:
            _instanceChannelId.store(channelId, std::memory_order_release);
            break;
        default:
            break;
    }
}

bool ChatSession::ConsumeSendBudget() {
    std::lock_guard<std::mutex> guard(_lock);
    return _sendLimiter.TryConsume(NowTick());
}

void ChatSession::OnDisconnected() {
    const SessionId sessionId = GetSessionId();
    if (sessionId == kInvalidSessionId) {
        return;
    }
    GChannelManager->OnSessionClosed(sessionId);
    LOG_DEBUG("chat session {} closed: {}", sessionId, GetDisconnectCause());
}

void ChatSession::OnRecvPacket(uint8* buffer, int32 length) {
    GChatServer->DispatchClientPacket(std::static_pointer_cast<ChatSession>(shared_from_this()),
                                      buffer, length);
}

}  // namespace nhn::chat
