#include "voice/VoiceRouter.h"

#include <array>
#include <vector>

namespace nhn::voice {

using namespace proto;

void VoiceRouter::OnDatagram(const NetAddress& from, const uint8* data, int32 length) {
    VoiceHeader header;
    if (!ReadVoiceHeader(data, length, header)) {
        // A shared UDP port collects scanner traffic and stray packets; reject
        // them on the magic and version before doing any lookup work.
        _framesDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    switch (header.type) {
        case VoiceMessageType::Hello:
            HandleHello(from, header, data, length);
            break;
        case VoiceMessageType::Data:
            HandleData(from, header, data, length);
            break;
        case VoiceMessageType::Ping:
            HandlePing(from, header);
            break;
        case VoiceMessageType::Bye:
            HandleBye(from);
            break;
        default:
            _framesDropped.fetch_add(1, std::memory_order_relaxed);
            break;
    }
}

void VoiceRouter::HandleHello(const NetAddress& from, const VoiceHeader& header,
                              const uint8* data, int32 length) {
    const int32 payloadLength = length - static_cast<int32>(kVoiceHeaderSize);
    if (payloadLength <= 0 || payloadLength > static_cast<int32>(kTicketLength) * 2) {
        return;
    }

    const std::string ticket(reinterpret_cast<const char*>(data + kVoiceHeaderSize),
                             static_cast<size_t>(payloadLength));

    TicketTable::Entry entry;
    ResultCode reason = ResultCode::TicketInvalid;
    if (!_tickets->Consume(ticket, NowUnixMs(), entry, reason)) {
        LOG_WARN("voice ticket rejected from {}: {}", from.ToString(), ToString(reason));
        return;
    }

    {
        std::lock_guard<std::mutex> guard(_lock);

        // Reconnecting from a new address: retire the old mapping so stale
        // entries do not accumulate or, worse, keep receiving audio.
        const auto existing = _peers.find(entry.sessionId);
        if (existing != _peers.end()) {
            _endpointToSession.erase(existing->second.endpoint.Key());
        }

        Peer& peer = _peers[entry.sessionId];
        peer.sessionId = entry.sessionId;
        peer.nickname = entry.nickname;
        // The address the datagram arrived from, not one the client claimed.
        // Behind NAT that mapping is the only reachable address, and it is only
        // knowable from a packet the client itself sent.
        peer.endpoint = from;
        peer.lastSeenAt = NowTick();

        _endpointToSession[from.Key()] = entry.sessionId;
    }

    (void)header;
    SendSimple(from, VoiceMessageType::HelloAck, entry.sessionId);

    LOG_INFO("voice session {} '{}' registered at {}", entry.sessionId, entry.nickname,
             from.ToString());
}

void VoiceRouter::HandleData(const NetAddress& from, const VoiceHeader& header, const uint8* data,
                             int32 length) {
    const int32 payloadLength = length - static_cast<int32>(kVoiceHeaderSize);
    if (payloadLength <= 0 || payloadLength > static_cast<int32>(kMaxVoicePayloadSize)) {
        _framesDropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    SessionId speakerId = kInvalidSessionId;
    std::vector<NetAddress> targets;

    {
        std::lock_guard<std::mutex> guard(_lock);

        // The speaker is identified by where the packet came from, never by the
        // id in the header — otherwise anyone could impersonate any player by
        // writing their id into a datagram.
        const auto endpointIt = _endpointToSession.find(from.Key());
        if (endpointIt == _endpointToSession.end()) {
            _framesDropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        speakerId = endpointIt->second;

        const auto peerIt = _peers.find(speakerId);
        if (peerIt == _peers.end()) {
            _framesDropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        peerIt->second.lastSeenAt = NowTick();

        const auto [channelType, channelId] = ResolveActiveChannel(peerIt->second);
        if (channelType == ChannelType::None) {
            _framesDropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const auto channelIt = _channels.find(MakeChannelKey(channelType, channelId));
        if (channelIt == _channels.end()) {
            _framesDropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        targets.reserve(channelIt->second.size());
        for (const SessionId listenerId : channelIt->second) {
            if (listenerId == speakerId) {
                continue;
            }
            const auto listenerIt = _peers.find(listenerId);
            if (listenerIt != _peers.end()) {
                targets.push_back(listenerIt->second.endpoint);
            }
        }
    }

    if (targets.empty()) {
        return;
    }

    // Forwarded byte-for-byte apart from the sender stamp: no decode, no
    // re-encode, no per-listener copy of the payload.
    std::array<uint8, kMaxDatagramSize> relayBuffer{};
    std::memcpy(relayBuffer.data(), data, static_cast<size_t>(length));
    StampVoiceSender(relayBuffer.data(), speakerId);

    for (const NetAddress& target : targets) {
        _socket->SendTo(target, relayBuffer.data(), length);
    }
    _framesRelayed.fetch_add(targets.size(), std::memory_order_relaxed);

    (void)header;
}

void VoiceRouter::HandlePing(const NetAddress& from, const VoiceHeader& header) {
    SessionId sessionId = kInvalidSessionId;
    {
        std::lock_guard<std::mutex> guard(_lock);
        const auto endpointIt = _endpointToSession.find(from.Key());
        if (endpointIt == _endpointToSession.end()) {
            return;
        }
        sessionId = endpointIt->second;
        const auto peerIt = _peers.find(sessionId);
        if (peerIt != _peers.end()) {
            // Keepalives are what stop a silent listener from being swept: they
            // send no audio but still need to receive it.
            peerIt->second.lastSeenAt = NowTick();
        }
    }

    std::array<uint8, kVoiceHeaderSize> buffer{};
    VoiceHeader pong;
    pong.type = VoiceMessageType::Pong;
    pong.sessionId = sessionId;
    pong.sequence = header.sequence;
    pong.timestamp = header.timestamp;
    WriteVoiceHeader(buffer.data(), pong);
    _socket->SendTo(from, buffer.data(), static_cast<int32>(buffer.size()));
}

void VoiceRouter::HandleBye(const NetAddress& from) {
    std::lock_guard<std::mutex> guard(_lock);
    const auto endpointIt = _endpointToSession.find(from.Key());
    if (endpointIt == _endpointToSession.end()) {
        return;
    }
    const SessionId sessionId = endpointIt->second;
    _endpointToSession.erase(endpointIt);
    _peers.erase(sessionId);

    for (auto& [key, members] : _channels) {
        members.erase(sessionId);
    }
}

void VoiceRouter::SendSimple(const NetAddress& to, VoiceMessageType type, SessionId sessionId) {
    std::array<uint8, kVoiceHeaderSize> buffer{};
    VoiceHeader header;
    header.type = type;
    header.sessionId = sessionId;
    WriteVoiceHeader(buffer.data(), header);
    _socket->SendTo(to, buffer.data(), static_cast<int32>(buffer.size()));
}

std::pair<ChannelType, uint64> VoiceRouter::ResolveActiveChannel(const Peer& peer) const {
    if (peer.instanceChannelId != 0) {
        return {ChannelType::Instance, peer.instanceChannelId};
    }
    if (peer.roomChannelId != 0) {
        return {ChannelType::Room, peer.roomChannelId};
    }
    return {ChannelType::None, 0};
}

void VoiceRouter::ApplyChannelJoin(SessionId sessionId, ChannelType type, uint64 channelId) {
    if (type != ChannelType::Room && type != ChannelType::Instance) {
        return;  // No global voice.
    }

    std::lock_guard<std::mutex> guard(_lock);

    // Recorded even for a session that has not sent its UDP hello yet: the
    // match server pushes membership as soon as the player enters a room, which
    // routinely precedes the voice handshake.
    Peer& peer = _peers[sessionId];
    peer.sessionId = sessionId;
    if (type == ChannelType::Room) {
        peer.roomChannelId = channelId;
    } else {
        peer.instanceChannelId = channelId;
    }

    _channels[MakeChannelKey(type, channelId)].insert(sessionId);
}

void VoiceRouter::ApplyChannelLeave(SessionId sessionId, ChannelType type, uint64 channelId) {
    std::lock_guard<std::mutex> guard(_lock);

    const auto channelIt = _channels.find(MakeChannelKey(type, channelId));
    if (channelIt != _channels.end()) {
        channelIt->second.erase(sessionId);
        if (channelIt->second.empty()) {
            _channels.erase(channelIt);
        }
    }

    const auto peerIt = _peers.find(sessionId);
    if (peerIt == _peers.end()) {
        return;
    }
    if (type == ChannelType::Room && peerIt->second.roomChannelId == channelId) {
        peerIt->second.roomChannelId = 0;
    } else if (type == ChannelType::Instance && peerIt->second.instanceChannelId == channelId) {
        peerIt->second.instanceChannelId = 0;
    }

    // A peer that never completed a hello and now belongs to nothing is just
    // bookkeeping; drop it.
    if (peerIt->second.roomChannelId == 0 && peerIt->second.instanceChannelId == 0 &&
        peerIt->second.lastSeenAt == 0) {
        _peers.erase(peerIt);
    }
}

void VoiceRouter::RemoveSession(SessionId sessionId) {
    std::lock_guard<std::mutex> guard(_lock);

    const auto peerIt = _peers.find(sessionId);
    if (peerIt != _peers.end()) {
        _endpointToSession.erase(peerIt->second.endpoint.Key());
        _peers.erase(peerIt);
    }

    for (auto it = _channels.begin(); it != _channels.end();) {
        it->second.erase(sessionId);
        it = it->second.empty() ? _channels.erase(it) : std::next(it);
    }
}

void VoiceRouter::SweepIdlePeers() {
    const TickCount now = NowTick();
    std::vector<SessionId> stale;

    {
        std::lock_guard<std::mutex> guard(_lock);
        for (const auto& [sessionId, peer] : _peers) {
            if (peer.lastSeenAt == 0) {
                continue;  // Never completed a hello; nothing to time out.
            }
            if (now - peer.lastSeenAt >= _settings.peerTimeoutMs) {
                stale.push_back(sessionId);
            }
        }

        for (const SessionId sessionId : stale) {
            const auto peerIt = _peers.find(sessionId);
            if (peerIt == _peers.end()) {
                continue;
            }
            _endpointToSession.erase(peerIt->second.endpoint.Key());
            // The channel membership stays: the match server still considers
            // them in the room, and they may re-register from a new address.
            peerIt->second.endpoint = NetAddress();
            peerIt->second.lastSeenAt = 0;
        }
    }

    if (!stale.empty()) {
        LOG_INFO("voice: {} peers timed out", stale.size());
    }
}

VoiceRouter::Stats VoiceRouter::GetStats() const {
    Stats stats;
    {
        std::lock_guard<std::mutex> guard(_lock);
        stats.peers = _peers.size();
        stats.channels = _channels.size();
    }
    stats.framesRelayed = _framesRelayed.load(std::memory_order_relaxed);
    stats.framesDropped = _framesDropped.load(std::memory_order_relaxed);
    return stats;
}

}  // namespace nhn::voice
