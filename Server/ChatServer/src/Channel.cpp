#include "chat/Channel.h"

#include <algorithm>

#include "protocol/Dispatcher.h"

namespace nhn::chat {

using namespace proto;

namespace {

template <class Fn>
void PostToChannel(Channel* channel, Fn&& body) {
    ChannelRef self = std::static_pointer_cast<Channel>(channel->shared_from_this());
    channel->DoAsync([self, body = std::forward<Fn>(body)]() { body(self); });
}

}  // namespace

// ---------------------------------------------------------------------------
// Channel
// ---------------------------------------------------------------------------

void Channel::EnqueueJoin(const ChatSessionRef& session) {
    PostToChannel(this, [session](const ChannelRef& self) {
        self->_members[session->GetSessionId()] = session;
        self->_memberCount.store(self->_members.size(), std::memory_order_relaxed);
        session->SetChannelId(self->_type, self->_id);
    });
}

void Channel::EnqueueLeave(SessionId sessionId) {
    PostToChannel(this, [sessionId](const ChannelRef& self) {
        const auto it = self->_members.find(sessionId);
        if (it == self->_members.end()) {
            return;
        }
        if (it->second != nullptr) {
            it->second->SetChannelId(self->_type, 0);
        }
        self->_members.erase(it);
        self->_memberCount.store(self->_members.size(), std::memory_order_relaxed);
    });
}

void Channel::EnqueueMessage(SessionId senderSessionId, std::string nickname,
                             std::string message) {
    PostToChannel(this, [senderSessionId, nickname = std::move(nickname),
                         message = std::move(message)](const ChannelRef& self) {
        // Membership is re-checked here rather than at the call site: between a
        // client pressing enter and this job running, the match server may have
        // removed them from the room.
        if (self->_members.find(senderSessionId) == self->_members.end()) {
            return;
        }

        S_ChatMessage packet;
        packet.channelType = self->_type;
        packet.senderSessionId = senderSessionId;
        packet.nickname = nickname;
        packet.message = message;
        packet.timestampMs = NowUnixMs();

        self->Broadcast(MakePacket(packet));
    });
}

void Channel::EnqueueNotice(std::string message) {
    PostToChannel(this, [message = std::move(message)](const ChannelRef& self) {
        S_ChatNotice notice;
        notice.channelType = self->_type;
        notice.message = message;
        self->Broadcast(MakePacket(notice));
    });
}

void Channel::Broadcast(const SendBufferRef& sendBuffer) {
    if (sendBuffer == nullptr) {
        return;
    }
    for (const auto& [sessionId, session] : _members) {
        if (session != nullptr) {
            session->Send(sendBuffer);
        }
    }
}

// ---------------------------------------------------------------------------
// ChannelManager
// ---------------------------------------------------------------------------

void ChannelManager::Init() {
    _global = std::make_shared<Channel>(ChannelType::Global, kGlobalChannelId);

    std::lock_guard<std::mutex> guard(_lock);
    _channels[MakeKey(ChannelType::Global, kGlobalChannelId)] = _global;
}

ChannelRef ChannelManager::Find(ChannelType type, uint64 channelId) const {
    std::lock_guard<std::mutex> guard(_lock);
    const auto it = _channels.find(MakeKey(type, channelId));
    return it != _channels.end() ? it->second : nullptr;
}

ChannelRef ChannelManager::FindOrCreate(ChannelType type, uint64 channelId) {
    std::lock_guard<std::mutex> guard(_lock);
    const uint64 key = MakeKey(type, channelId);
    const auto it = _channels.find(key);
    if (it != _channels.end()) {
        return it->second;
    }
    ChannelRef channel = std::make_shared<Channel>(type, channelId);
    _channels[key] = channel;
    return channel;
}

void ChannelManager::RemoveIfEmpty(ChannelType type, uint64 channelId) {
    if (type == ChannelType::Global) {
        return;  // The global channel outlives every session.
    }

    std::lock_guard<std::mutex> guard(_lock);
    const uint64 key = MakeKey(type, channelId);
    const auto it = _channels.find(key);
    if (it != _channels.end() && it->second->GetMemberCount() == 0) {
        _channels.erase(it);
    }
}

void ChannelManager::RegisterSession(const ChatSessionRef& session) {
    std::lock_guard<std::mutex> guard(_lock);
    _sessions[session->GetSessionId()] = session;
}

void ChannelManager::UnregisterSession(SessionId sessionId) {
    std::lock_guard<std::mutex> guard(_lock);
    _sessions.erase(sessionId);
    _pending.erase(sessionId);
}

ChatSessionRef ChannelManager::FindSession(SessionId sessionId) const {
    std::lock_guard<std::mutex> guard(_lock);
    const auto it = _sessions.find(sessionId);
    return it != _sessions.end() ? it->second : nullptr;
}

void ChannelManager::ApplyChannelJoin(SessionId sessionId, ChannelType type, uint64 channelId) {
    ChatSessionRef session;
    {
        std::lock_guard<std::mutex> guard(_lock);
        const auto it = _sessions.find(sessionId);
        if (it == _sessions.end() || !it->second->IsAuthenticated()) {
            // The player entered a room before their chat socket finished
            // connecting. Hold the membership until they authenticate.
            _pending[sessionId].push_back(PendingMembership{type, channelId});
            return;
        }
        session = it->second;
    }

    FindOrCreate(type, channelId)->EnqueueJoin(session);
}

void ChannelManager::ApplyChannelLeave(SessionId sessionId, ChannelType type, uint64 channelId) {
    {
        std::lock_guard<std::mutex> guard(_lock);
        const auto pendingIt = _pending.find(sessionId);
        if (pendingIt != _pending.end()) {
            auto& list = pendingIt->second;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [type, channelId](const PendingMembership& p) {
                                          return p.type == type && p.channelId == channelId;
                                      }),
                       list.end());
            if (list.empty()) {
                _pending.erase(pendingIt);
            }
        }
    }

    if (ChannelRef channel = Find(type, channelId)) {
        channel->EnqueueLeave(sessionId);
    }
}

void ChannelManager::OnSessionAuthenticated(const ChatSessionRef& session) {
    const SessionId sessionId = session->GetSessionId();

    std::vector<PendingMembership> pending;
    {
        std::lock_guard<std::mutex> guard(_lock);
        _sessions[sessionId] = session;
        const auto it = _pending.find(sessionId);
        if (it != _pending.end()) {
            pending = std::move(it->second);
            _pending.erase(it);
        }
    }

    _global->EnqueueJoin(session);

    for (const PendingMembership& membership : pending) {
        FindOrCreate(membership.type, membership.channelId)->EnqueueJoin(session);
    }

    if (!pending.empty()) {
        LOG_DEBUG("session {} authenticated with {} queued channel memberships", sessionId,
                  pending.size());
    }
}

void ChannelManager::OnSessionClosed(SessionId sessionId) {
    std::vector<ChannelRef> channels;
    {
        std::lock_guard<std::mutex> guard(_lock);
        _sessions.erase(sessionId);
        _pending.erase(sessionId);
        channels.reserve(_channels.size());
        for (const auto& [key, channel] : _channels) {
            channels.push_back(channel);
        }
    }

    // Cheaper than tracking a reverse index: leaving a channel the session was
    // never in is a no-op, and channel counts here are small.
    for (const ChannelRef& channel : channels) {
        channel->EnqueueLeave(sessionId);
    }
}

int32 ChannelManager::ChannelCount() const {
    std::lock_guard<std::mutex> guard(_lock);
    return static_cast<int32>(_channels.size());
}

int32 ChannelManager::SessionCount() const {
    std::lock_guard<std::mutex> guard(_lock);
    return static_cast<int32>(_sessions.size());
}

}  // namespace nhn::chat
