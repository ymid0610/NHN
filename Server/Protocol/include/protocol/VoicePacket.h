#pragma once

#include <cstddef>
#include <cstring>

#include "core/Types.h"
#include "protocol/Types.h"

namespace nhn::proto {

/// Voice runs on its own compact framing rather than the TCP packet format.
///
/// At 50 frames per second per speaker, relayed to every other member, header
/// size is the one thing that matters — so this is a fixed 18-byte struct with
/// no length prefixes, no strings and nothing to parse. The relay reads the
/// header, looks up a channel and forwards the datagram unchanged; it never
/// decodes audio.
inline constexpr uint8 kVoiceMagic = 0x9E;
inline constexpr uint8 kVoiceVersion = 1;

enum class VoiceMessageType : uint8 {
    None = 0,
    /// Client -> server, payload is the ticket text. Also establishes which
    /// address the server should relay to, since a NAT-ed client's public
    /// endpoint is only knowable from a packet it sent.
    Hello = 1,
    HelloAck = 2,
    /// Either direction. Client -> server carries the speaker's own audio;
    /// server -> client carries another member's, with sessionId rewritten to
    /// the speaker.
    Data = 3,
    Ping = 4,
    Pong = 5,
    Bye = 6,
};

/// Set on the first frame of a talkspurt so a receiver can reset its jitter
/// buffer instead of trying to bridge the silence.
inline constexpr uint8 kVoiceFlagTalkspurtStart = 0x01;

#pragma pack(push, 1)
struct VoiceHeader {
    uint8 magic = kVoiceMagic;
    uint8 version = kVoiceVersion;
    VoiceMessageType type = VoiceMessageType::None;
    uint8 flags = 0;
    /// On Data relayed by the server, the speaker's session — not the receiver's.
    SessionId sessionId = kInvalidSessionId;
    uint16 sequence = 0;
    /// Sample-clock timestamp from the encoder, used for jitter buffering.
    uint32 timestamp = 0;
};
#pragma pack(pop)

static_assert(sizeof(VoiceHeader) == 18, "voice header size is part of the wire format");

inline constexpr uint32 kVoiceHeaderSize = sizeof(VoiceHeader);
inline constexpr uint32 kMaxVoicePayloadSize = 1000;

/// Reads a header out of a datagram. Rejects anything that is not plausibly
/// ours before the relay does any lookup work — a shared UDP port receives
/// scanner traffic and stray packets as a matter of course.
inline bool ReadVoiceHeader(const uint8* data, int32 length, VoiceHeader& out) {
    if (data == nullptr || length < static_cast<int32>(kVoiceHeaderSize)) {
        return false;
    }
    std::memcpy(&out, data, sizeof(VoiceHeader));
    return out.magic == kVoiceMagic && out.version == kVoiceVersion;
}

inline void WriteVoiceHeader(uint8* data, const VoiceHeader& header) {
    std::memcpy(data, &header, sizeof(VoiceHeader));
}

/// Overwrites just the sessionId, so the relay can stamp the speaker's id onto
/// a datagram it is forwarding without rebuilding it.
inline void StampVoiceSender(uint8* data, SessionId sessionId) {
    std::memcpy(data + offsetof(VoiceHeader, sessionId), &sessionId, sizeof(SessionId));
}

}  // namespace nhn::proto
