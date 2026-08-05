#include "protocol/Types.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace nhn::proto {
namespace {

// Adding a mode is a single row here plus an enumerator.
//
//                         board   win  cap  min   ammo options   wave caps
constexpr std::array<GameModeDef, 3> kGameModes = {{
    {GameMode::TicTacToe,  3,  3,  3,   2,   2,  {1, 2, 3, 0},   {10, 0}, "tictactoe"},
    {GameMode::Gomoku9,    9,  9,  4,   4,   2,  {1, 3, 6, 0},   {10, 0}, "gomoku9"},
    {GameMode::Gomoku15,  15, 15,  5,   4,   2,  {1, 3, 6, 0},   {20, 0}, "gomoku15"},
}};

}  // namespace

const GameModeDef* FindGameMode(GameMode type) {
    for (const GameModeDef& info : kGameModes) {
        if (info.type == type) {
            return &info;
        }
    }
    return nullptr;
}

uint8 ModeCapacity(GameMode type) {
    const GameModeDef* info = FindGameMode(type);
    return info != nullptr ? info->capacity : uint8{0};
}

bool IsValidGameMode(GameMode type) {
    return FindGameMode(type) != nullptr;
}

bool IsAmmoAllowed(GameMode type, uint8 ammoPerWave) {
    const GameModeDef* info = FindGameMode(type);
    if (info == nullptr) {
        return false;
    }
    return std::find(info->ammoOptions.begin(), info->ammoOptions.end(), ammoPerWave) !=
           info->ammoOptions.end();
}

bool IsWaveLimitAllowed(GameMode type, uint8 waveLimit) {
    const GameModeDef* info = FindGameMode(type);
    if (info == nullptr) {
        return false;
    }
    return std::find(info->waveLimitOptions.begin(), info->waveLimitOptions.end(), waveLimit) !=
           info->waveLimitOptions.end();
}

GameMode ParseGameMode(std::string_view text) {
    std::string lowered(text);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    for (const GameModeDef& info : kGameModes) {
        if (lowered == info.name) {
            return info.type;
        }
    }
    // Short forms, because these get typed a lot at a test-client prompt.
    if (lowered == "ttt" || lowered == "3") {
        return GameMode::TicTacToe;
    }
    if (lowered == "g9" || lowered == "9") {
        return GameMode::Gomoku9;
    }
    if (lowered == "g15" || lowered == "15") {
        return GameMode::Gomoku15;
    }
    return GameMode::None;
}

bool IsCleanUtf8(std::string_view text, uint32 maxLength) {
    if (text.empty() || text.size() > maxLength) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const uint8*>(text.data());
    const size_t size = text.size();

    for (size_t i = 0; i < size;) {
        const uint8 lead = bytes[i];

        if (lead < 0x80) {
            // Control characters would let a client inject escape sequences or
            // fake line breaks into other players' consoles and chat.
            if (lead < 0x20 || lead == 0x7F) {
                return false;
            }
            ++i;
            continue;
        }

        int32 continuationCount = 0;
        uint32 codePoint = 0;
        if ((lead & 0xE0) == 0xC0) {
            continuationCount = 1;
            codePoint = lead & 0x1Fu;
        } else if ((lead & 0xF0) == 0xE0) {
            continuationCount = 2;
            codePoint = lead & 0x0Fu;
        } else if ((lead & 0xF8) == 0xF0) {
            continuationCount = 3;
            codePoint = lead & 0x07u;
        } else {
            return false;  // Continuation byte in lead position, or 5+ byte form.
        }

        // Continuation bytes occupy i+1 .. i+continuationCount, so all of them
        // must fall inside the string.
        if (i + static_cast<size_t>(continuationCount) >= size) {
            return false;  // Truncated sequence.
        }

        for (int32 c = 1; c <= continuationCount; ++c) {
            const uint8 continuation = bytes[i + static_cast<size_t>(c)];
            if ((continuation & 0xC0) != 0x80) {
                return false;
            }
            codePoint = (codePoint << 6) | (continuation & 0x3Fu);
        }

        // Reject overlong encodings, surrogates and out-of-range code points:
        // all of these are ways to smuggle a byte sequence past a naive filter.
        const uint32 minimum = continuationCount == 1 ? 0x80u : (continuationCount == 2 ? 0x800u : 0x10000u);
        if (codePoint < minimum || codePoint > 0x10FFFFu ||
            (codePoint >= 0xD800u && codePoint <= 0xDFFFu)) {
            return false;
        }

        i += static_cast<size_t>(continuationCount) + 1;
    }

    return true;
}

const char* ToString(ResultCode code) {
    switch (code) {
        case ResultCode::Ok: return "Ok";
        case ResultCode::UnknownError: return "UnknownError";
        case ResultCode::InvalidRequest: return "InvalidRequest";
        case ResultCode::NotAuthenticated: return "NotAuthenticated";
        case ResultCode::InternalError: return "InternalError";
        case ResultCode::ServerFull: return "ServerFull";
        case ResultCode::RateLimited: return "RateLimited";
        case ResultCode::NicknameInvalid: return "NicknameInvalid";
        case ResultCode::AlreadyAuthenticated: return "AlreadyAuthenticated";
        case ResultCode::RoomNotFound: return "RoomNotFound";
        case ResultCode::RoomFull: return "RoomFull";
        case ResultCode::AlreadyInRoom: return "AlreadyInRoom";
        case ResultCode::NotInRoom: return "NotInRoom";
        case ResultCode::WrongPassword: return "WrongPassword";
        case ResultCode::PasswordRequired: return "PasswordRequired";
        case ResultCode::RoomNameInvalid: return "RoomNameInvalid";
        case ResultCode::GameModeInvalid: return "GameModeInvalid";
        case ResultCode::RoomNotWaiting: return "RoomNotWaiting";
        case ResultCode::KickCooldown: return "KickCooldown";
        case ResultCode::NotHost: return "NotHost";
        case ResultCode::CannotTargetSelf: return "CannotTargetSelf";
        case ResultCode::TargetNotFound: return "TargetNotFound";
        case ResultCode::NotEnoughPlayers: return "NotEnoughPlayers";
        case ResultCode::NotAllReady: return "NotAllReady";
        case ResultCode::TicketInvalid: return "TicketInvalid";
        case ResultCode::TicketExpired: return "TicketExpired";
        case ResultCode::TicketAlreadyUsed: return "TicketAlreadyUsed";
        case ResultCode::NoInstanceServer: return "NoInstanceServer";
        case ResultCode::InstanceCreateFailed: return "InstanceCreateFailed";
        case ResultCode::InstanceNotFound: return "InstanceNotFound";
        case ResultCode::MessageEmpty: return "MessageEmpty";
        case ResultCode::MessageTooLong: return "MessageTooLong";
        case ResultCode::MessageInvalidEncoding: return "MessageInvalidEncoding";
        case ResultCode::NotInChannel: return "NotInChannel";
    }
    return "Unrecognised";
}

const char* ToString(GameMode type) {
    const GameModeDef* info = FindGameMode(type);
    return info != nullptr ? info->name : "none";
}

const char* ToString(RoomState state) {
    switch (state) {
        case RoomState::Waiting: return "waiting";
        case RoomState::Starting: return "starting";
        case RoomState::InGame: return "in-game";
        case RoomState::Closing: return "closing";
    }
    return "?";
}

const char* ToString(ChannelType type) {
    switch (type) {
        case ChannelType::None: return "none";
        case ChannelType::Global: return "global";
        case ChannelType::Room: return "room";
        case ChannelType::Instance: return "instance";
    }
    return "?";
}

const char* ToString(ServerType type) {
    switch (type) {
        case ServerType::None: return "none";
        case ServerType::Match: return "match";
        case ServerType::Chat: return "chat";
        case ServerType::Voice: return "voice";
        case ServerType::Instance: return "instance";
    }
    return "?";
}

const char* ToString(LeaveReason reason) {
    switch (reason) {
        case LeaveReason::Left: return "left";
        case LeaveReason::Disconnected: return "disconnected";
        case LeaveReason::Kicked: return "kicked";
        case LeaveReason::RoomClosed: return "room closed";
        case LeaveReason::MovedToInstance: return "moved to instance";
    }
    return "?";
}

}  // namespace nhn::proto
