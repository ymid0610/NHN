#pragma once

// Winsock2 must precede Windows.h or winsock.h gets pulled in first and the
// two headers conflict.
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <MSWSock.h>
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "mswsock.lib")

namespace nhn {

using int8 = std::int8_t;
using int16 = std::int16_t;
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint8 = std::uint8_t;
using uint16 = std::uint16_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

template <class T>
using Ref = std::shared_ptr<T>;

template <class T>
using Weak = std::weak_ptr<T>;

/// Identifier of a connected player. Assigned by the match server on hello and
/// valid only for the lifetime of that server process — there are no accounts.
using SessionId = uint64;
using RoomId = uint64;
using InstanceId = uint64;

inline constexpr SessionId kInvalidSessionId = 0;
inline constexpr RoomId kInvalidRoomId = 0;
inline constexpr InstanceId kInvalidInstanceId = 0;

/// Upper bound on a single framed packet, header included. Anything larger is
/// treated as a protocol violation and the connection is dropped.
inline constexpr uint32 kMaxPacketSize = 16 * 1024;

/// Per-session receive buffer. Sized to hold several max-size packets so a
/// burst does not force a compaction on every read.
inline constexpr uint32 kRecvBufferSize = 64 * 1024;

/// Nicknames are cosmetic and non-unique; the cap only bounds allocation.
inline constexpr uint32 kMaxNicknameLength = 24;
inline constexpr uint32 kMaxRoomNameLength = 32;
inline constexpr uint32 kMaxPasswordLength = 16;
inline constexpr uint32 kMaxChatMessageLength = 256;

/// Length of the hex-encoded ticket string issued by the match server.
inline constexpr uint32 kTicketLength = 32;

/// Milliseconds since an unspecified epoch, from a monotonic clock.
using TickCount = uint64;

TickCount NowTick();

/// Milliseconds since the Unix epoch. Only used for display and log stamps.
int64 NowUnixMs();

}  // namespace nhn
