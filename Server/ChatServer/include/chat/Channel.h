#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "chat/ChatSession.h"
#include "core/Core.h"

namespace nhn::chat {

/// A set of sessions that receive the same messages.
///
/// One JobQueue per channel, which is what gives messages a single consistent
/// order within a channel without serialising unrelated channels against each
/// other.
class Channel : public JobQueue {
public:
    Channel(proto::ChannelType type, uint64 id) : _type(type), _id(id) {}

    void EnqueueJoin(const ChatSessionRef& session);
    void EnqueueLeave(SessionId sessionId);
    void EnqueueMessage(SessionId senderSessionId, std::string nickname, std::string message);
    void EnqueueNotice(std::string message);

    proto::ChannelType GetType() const { return _type; }
    uint64 GetId() const { return _id; }

    /// Approximate: read without entering the queue, for logging and stats.
    size_t GetMemberCount() const { return _memberCount.load(std::memory_order_relaxed); }

private:
    void Broadcast(const SendBufferRef& sendBuffer);

    const proto::ChannelType _type;
    const uint64 _id;

    std::unordered_map<SessionId, ChatSessionRef> _members;
    std::atomic<size_t> _memberCount{0};
};

using ChannelRef = Ref<Channel>;

/// Owns every channel and the mapping from sessions to the channels they may
/// speak in.
class ChannelManager {
public:
    /// Channel id 0 of type Global. Everyone lands here on authentication.
    static constexpr uint64 kGlobalChannelId = 0;

    void Init();

    ChannelRef GetGlobal() const { return _global; }
    ChannelRef Find(proto::ChannelType type, uint64 channelId) const;
    ChannelRef FindOrCreate(proto::ChannelType type, uint64 channelId);

    void RemoveIfEmpty(proto::ChannelType type, uint64 channelId);

    /// Applies a membership change pushed by the match server. When the session
    /// has not connected to this server yet the change is remembered and
    /// applied on authentication — the match server pushes as soon as a player
    /// enters a room, which can easily precede their chat connection.
    void ApplyChannelJoin(SessionId sessionId, proto::ChannelType type, uint64 channelId);
    void ApplyChannelLeave(SessionId sessionId, proto::ChannelType type, uint64 channelId);

    /// Called once a session authenticates: joins global and replays anything
    /// that arrived early.
    void OnSessionAuthenticated(const ChatSessionRef& session);

    /// Removes a session from every channel it belongs to.
    void OnSessionClosed(SessionId sessionId);

    void RegisterSession(const ChatSessionRef& session);
    void UnregisterSession(SessionId sessionId);
    ChatSessionRef FindSession(SessionId sessionId) const;

    int32 ChannelCount() const;
    int32 SessionCount() const;

private:
    struct PendingMembership {
        proto::ChannelType type = proto::ChannelType::None;
        uint64 channelId = 0;
    };

    static uint64 MakeKey(proto::ChannelType type, uint64 channelId) {
        return (static_cast<uint64>(type) << 56) ^ channelId;
    }

    mutable std::mutex _lock;
    std::unordered_map<uint64, ChannelRef> _channels;
    std::unordered_map<SessionId, ChatSessionRef> _sessions;
    std::unordered_map<SessionId, std::vector<PendingMembership>> _pending;
    ChannelRef _global;
};

}  // namespace nhn::chat
