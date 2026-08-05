#include "core/WebSocket.h"

#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstring>
#include <format>

#include "core/Log.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace nhn::ws {
namespace {

/// Fixed by RFC 6455. Concatenated with the client key before hashing.
constexpr char kMagicGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/// A browser's request headers are small; anything larger is not one.
constexpr int32 kMaxHandshakeBytes = 8192;

/// Matches the packet ceiling. A browser has no reason to send more in one
/// frame, and accepting more would let one message allocate without bound.
constexpr uint32 kMaxFramePayload = 64 * 1024;

std::string Sha1Base64(const std::string& input) {
    uint8 digest[20] = {};
    const NTSTATUS status = ::BCryptHash(BCRYPT_SHA1_ALG_HANDLE, nullptr, 0,
                                         reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
                                         static_cast<ULONG>(input.size()), digest,
                                         static_cast<ULONG>(sizeof(digest)));
    if (status != 0) {
        return {};
    }

    DWORD encodedSize = 0;
    if (::CryptBinaryToStringA(digest, sizeof(digest), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                               nullptr, &encodedSize) == FALSE) {
        return {};
    }

    std::string encoded(encodedSize, '\0');
    if (::CryptBinaryToStringA(digest, sizeof(digest), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                               encoded.data(), &encodedSize) == FALSE) {
        return {};
    }
    // The reported size includes the terminator.
    encoded.resize(std::strlen(encoded.c_str()));
    return encoded;
}

std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string Trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

}  // namespace

HandshakeResult TryHandshake(const uint8* data, int32 length) {
    HandshakeResult result;

    if (length <= 0) {
        return result;
    }
    if (length > kMaxHandshakeBytes) {
        result.status = ParseResult::Error;
        return result;
    }

    const std::string_view view(reinterpret_cast<const char*>(data), static_cast<size_t>(length));
    const auto headerEnd = view.find("\r\n\r\n");
    if (headerEnd == std::string_view::npos) {
        return result;  // Still arriving.
    }

    result.consumed = static_cast<int32>(headerEnd + 4);
    const std::string request(view.substr(0, headerEnd));

    // Header names are case-insensitive, and browsers do vary.
    std::string key;
    bool upgradeSeen = false;
    bool versionOk = false;

    size_t lineStart = request.find("\r\n");
    lineStart = lineStart == std::string::npos ? request.size() : lineStart + 2;
    while (lineStart < request.size()) {
        const auto lineEnd = request.find("\r\n", lineStart);
        const std::string line =
            request.substr(lineStart, lineEnd == std::string::npos ? std::string::npos
                                                                   : lineEnd - lineStart);
        lineStart = lineEnd == std::string::npos ? request.size() : lineEnd + 2;

        const auto colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string name = ToLower(Trim(line.substr(0, colon)));
        const std::string value = Trim(line.substr(colon + 1));

        if (name == "sec-websocket-key") {
            key = value;
        } else if (name == "upgrade" && ToLower(value) == "websocket") {
            upgradeSeen = true;
        } else if (name == "sec-websocket-version" && value == "13") {
            versionOk = true;
        }
    }

    if (key.empty() || !upgradeSeen || !versionOk) {
        // Also the path a raw client takes if it is pointed at the browser
        // port: it never sends an upgrade, so it is refused rather than
        // silently misinterpreted.
        result.status = ParseResult::Error;
        return result;
    }

    const std::string accept = Sha1Base64(key + kMagicGuid);
    if (accept.empty()) {
        result.status = ParseResult::Error;
        return result;
    }

    result.response = std::format(
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: {}\r\n"
        "\r\n",
        accept);
    result.status = ParseResult::Ok;
    return result;
}

ParseResult DecodeFrame(const uint8* data, int32 length, Frame& outFrame, int32& outConsumed) {
    outConsumed = 0;
    if (length < 2) {
        return ParseResult::Incomplete;
    }

    const uint8 first = data[0];
    const uint8 second = data[1];

    outFrame.fin = (first & 0x80) != 0;
    outFrame.opcode = static_cast<Opcode>(first & 0x0F);

    // Reserved bits must be clear; we negotiate no extensions.
    if ((first & 0x70) != 0) {
        return ParseResult::Error;
    }

    const bool masked = (second & 0x80) != 0;
    if (!masked) {
        // Mandatory for client-to-server frames. Tolerating an unmasked frame
        // would mean accepting traffic no browser produces.
        return ParseResult::Error;
    }

    uint64 payloadLength = second & 0x7F;
    int32 offset = 2;

    if (payloadLength == 126) {
        if (length < offset + 2) {
            return ParseResult::Incomplete;
        }
        payloadLength = (static_cast<uint64>(data[offset]) << 8) | data[offset + 1];
        offset += 2;
    } else if (payloadLength == 127) {
        if (length < offset + 8) {
            return ParseResult::Incomplete;
        }
        payloadLength = 0;
        for (int32 i = 0; i < 8; ++i) {
            payloadLength = (payloadLength << 8) | data[offset + i];
        }
        offset += 8;
    }

    if (payloadLength > kMaxFramePayload) {
        return ParseResult::Error;
    }

    if (length < offset + 4) {
        return ParseResult::Incomplete;
    }
    uint8 mask[4];
    std::memcpy(mask, data + offset, 4);
    offset += 4;

    const auto total = static_cast<int64>(offset) + static_cast<int64>(payloadLength);
    if (length < total) {
        return ParseResult::Incomplete;
    }

    outFrame.payload.resize(static_cast<size_t>(payloadLength));
    for (uint64 i = 0; i < payloadLength; ++i) {
        outFrame.payload[static_cast<size_t>(i)] = data[offset + i] ^ mask[i % 4];
    }

    outConsumed = static_cast<int32>(total);
    return ParseResult::Ok;
}

uint32 FrameHeaderSize(int32 payloadLength) {
    if (payloadLength < 126) {
        return 2;
    }
    if (payloadLength <= 0xFFFF) {
        return 4;
    }
    return 10;
}

uint32 WriteFrameHeader(uint8* out, Opcode opcode, int32 payloadLength) {
    out[0] = static_cast<uint8>(0x80 | static_cast<uint8>(opcode));  // FIN set

    if (payloadLength < 126) {
        out[1] = static_cast<uint8>(payloadLength);
        return 2;
    }
    if (payloadLength <= 0xFFFF) {
        out[1] = 126;
        out[2] = static_cast<uint8>((payloadLength >> 8) & 0xFF);
        out[3] = static_cast<uint8>(payloadLength & 0xFF);
        return 4;
    }

    out[1] = 127;
    for (int32 i = 0; i < 8; ++i) {
        out[2 + i] = static_cast<uint8>((static_cast<uint64>(payloadLength) >> (56 - i * 8)) & 0xFF);
    }
    return 10;
}

std::vector<uint8> EncodeFrame(Opcode opcode, const uint8* payload, int32 length) {
    std::vector<uint8> frame(FrameHeaderSize(length) + static_cast<size_t>(length));
    const uint32 headerSize = WriteFrameHeader(frame.data(), opcode, length);
    if (length > 0 && payload != nullptr) {
        std::memcpy(frame.data() + headerSize, payload, static_cast<size_t>(length));
    }
    return frame;
}

}  // namespace nhn::ws
