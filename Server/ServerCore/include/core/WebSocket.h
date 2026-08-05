#pragma once

#include <string>
#include <vector>

#include "core/Types.h"

namespace nhn::ws {

/// Minimal RFC 6455 server side: enough to carry the existing binary packets to
/// a browser, and nothing more.
///
/// A browser cannot open a raw TCP socket, so this is the only way an HTML
/// client can speak the same protocol as everything else. It sits strictly
/// below the packet framer — once a frame is unwrapped, the bytes inside are
/// the ordinary `[size][id][payload]` stream, so no handler above changes.
///
/// Text frames are not accepted. Everything on this link is binary.

enum class Opcode : uint8 {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

enum class ParseResult : uint8 {
    Ok,
    /// Not enough bytes yet; keep buffering.
    Incomplete,
    /// Malformed. The caller must close the connection — there is no
    /// resynchronising a framed stream.
    Error,
};

/// Result of inspecting a buffer for the opening HTTP upgrade request.
struct HandshakeResult {
    ParseResult status = ParseResult::Incomplete;
    /// Bytes of the request consumed, once complete.
    int32 consumed = 0;
    /// The full "HTTP/1.1 101 ..." response to write back.
    std::string response;
};

/// Looks for a complete HTTP upgrade request at the head of @p data.
///
/// Rejects anything that is not a WebSocket upgrade, which also covers someone
/// pointing a raw client at the browser port by mistake.
HandshakeResult TryHandshake(const uint8* data, int32 length);

struct Frame {
    Opcode opcode = Opcode::Binary;
    bool fin = true;
    std::vector<uint8> payload;
};

/// Decodes one frame from the head of @p data.
///
/// Client-to-server frames are always masked; an unmasked one is a protocol
/// violation rather than something to tolerate.
ParseResult DecodeFrame(const uint8* data, int32 length, Frame& outFrame, int32& outConsumed);

/// Builds a server-to-client frame. Server frames are never masked.
std::vector<uint8> EncodeFrame(Opcode opcode, const uint8* payload, int32 length);

/// Number of header bytes EncodeFrame will prepend for a payload of this size.
/// Used to reserve space without a second allocation.
uint32 FrameHeaderSize(int32 payloadLength);

/// Writes the header for @p payloadLength into @p out, which must have room for
/// FrameHeaderSize bytes.
uint32 WriteFrameHeader(uint8* out, Opcode opcode, int32 payloadLength);

}  // namespace nhn::ws
