#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "common/TicketTable.h"
#include "core/Core.h"
#include "protocol/VoicePacket.h"

namespace nhn::voice {

/// Relays voice datagrams between members of a channel.
///
/// This is a selective forwarder, not a mixer: an incoming frame is copied to
/// every other member of the speaker's channel with only the sender id
/// rewritten. The server never decodes audio, which is what keeps a four-player
/// room to a handful of memcpys per 20 ms rather than an encode/decode cycle.
///
/// A speaker is heard on the most specific channel they belong to — their
/// instance while a match is running, otherwise their room. Global voice is
/// deliberately not offered; a lobby-wide open mic is not something anyone
/// wants.
class VoiceRouter {
public:
    struct Settings {
        /// A peer that stops sending for this long is forgotten, freeing its
        /// endpoint. Clients send keepalive pings while silent.
        uint32 peerTimeoutMs = 15'000;
    };

    explicit VoiceRouter(Settings settings, Ref<UdpSocket> socket, Ref<TicketTable> tickets)
        : _settings(settings), _socket(std::move(socket)), _tickets(std::move(tickets)) {}

    /// Entry point for every datagram, called on an IOCP worker.
    void OnDatagram(const NetAddress& from, const uint8* data, int32 length);

    void ApplyChannelJoin(SessionId sessionId, proto::ChannelType type, uint64 channelId);
    void ApplyChannelLeave(SessionId sessionId, proto::ChannelType type, uint64 channelId);
    void RemoveSession(SessionId sessionId);

    /// Drops peers that have gone quiet. Driven from a timer.
    void SweepIdlePeers();

    struct Stats {
        size_t peers = 0;
        size_t channels = 0;
        uint64 framesRelayed = 0;
        uint64 framesDropped = 0;
    };
    Stats GetStats() const;

private:
    struct Peer {
        SessionId sessionId = kInvalidSessionId;
        std::string nickname;
        NetAddress endpoint;
        TickCount lastSeenAt = 0;
        uint64 roomChannelId = 0;
        uint64 instanceChannelId = 0;
    };

    void HandleHello(const NetAddress& from, const proto::VoiceHeader& header, const uint8* data,
                     int32 length);
    void HandleData(const NetAddress& from, const proto::VoiceHeader& header, const uint8* data,
                    int32 length);
    void HandlePing(const NetAddress& from, const proto::VoiceHeader& header);
    void HandleBye(const NetAddress& from);

    void SendSimple(const NetAddress& to, proto::VoiceMessageType type, SessionId sessionId);

    /// Channel the speaker should be heard on, or (None, 0) when they are in
    /// none.
    std::pair<proto::ChannelType, uint64> ResolveActiveChannel(const Peer& peer) const;

    static uint64 MakeChannelKey(proto::ChannelType type, uint64 channelId) {
        return (static_cast<uint64>(type) << 56) ^ channelId;
    }

    Settings _settings;
    Ref<UdpSocket> _socket;
    Ref<TicketTable> _tickets;

    mutable std::mutex _lock;
    std::unordered_map<SessionId, Peer> _peers;
    /// Reverse index so an inbound datagram maps to a speaker in one lookup.
    std::unordered_map<uint64, SessionId> _endpointToSession;
    std::unordered_map<uint64, std::unordered_set<SessionId>> _channels;

    std::atomic<uint64> _framesRelayed{0};
    std::atomic<uint64> _framesDropped{0};
};

}  // namespace nhn::voice
