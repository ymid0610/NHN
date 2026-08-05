#include <gtest/gtest.h>

#include <array>

#include "protocol/Types.h"
#include "protocol/VoicePacket.h"

using namespace nhn;
using namespace nhn::proto;

TEST(RoomTypes, CapacitiesMatchTheSpecifiedModes) {
    EXPECT_EQ(RoomCapacity(RoomType::Duo), 2);
    EXPECT_EQ(RoomCapacity(RoomType::Quad), 4);
    EXPECT_EQ(RoomCapacity(RoomType::None), 0);
    EXPECT_TRUE(IsValidRoomType(RoomType::Duo));
    EXPECT_FALSE(IsValidRoomType(RoomType::None));
    EXPECT_FALSE(IsValidRoomType(static_cast<RoomType>(99)));
}

TEST(RoomTypes, ParsesNamesAndPlayerCounts) {
    EXPECT_EQ(ParseRoomType("duo"), RoomType::Duo);
    EXPECT_EQ(ParseRoomType("DUO"), RoomType::Duo);
    EXPECT_EQ(ParseRoomType("quad"), RoomType::Quad);
    EXPECT_EQ(ParseRoomType("2"), RoomType::Duo);
    EXPECT_EQ(ParseRoomType("4"), RoomType::Quad);
    EXPECT_EQ(ParseRoomType("nonsense"), RoomType::None);
}

TEST(TextValidation, AcceptsPlainAndMultibyteText) {
    EXPECT_TRUE(IsCleanUtf8("player", 24));
    EXPECT_TRUE(IsCleanUtf8("플레이어", 24));       // Korean, 3 bytes per char
    EXPECT_TRUE(IsCleanUtf8("emoji \xF0\x9F\x8E\xAE", 24));  // 4-byte sequence
}

TEST(TextValidation, RejectsEmptyAndOverlong) {
    EXPECT_FALSE(IsCleanUtf8("", 24));
    EXPECT_FALSE(IsCleanUtf8(std::string(25, 'x'), 24));
}

TEST(TextValidation, RejectsControlCharacters) {
    // Escape sequences and fake newlines would otherwise reach other players'
    // terminals verbatim.
    EXPECT_FALSE(IsCleanUtf8("bad\nname", 24));
    EXPECT_FALSE(IsCleanUtf8("bad\x1b[31mname", 24));
    EXPECT_FALSE(IsCleanUtf8(std::string("null\0here", 9), 24));
    EXPECT_FALSE(IsCleanUtf8("del\x7f", 24));
}

TEST(TextValidation, RejectsMalformedUtf8) {
    EXPECT_FALSE(IsCleanUtf8("\xC3", 24));              // truncated 2-byte lead
    EXPECT_FALSE(IsCleanUtf8("\xE2\x82", 24));          // truncated 3-byte lead
    EXPECT_FALSE(IsCleanUtf8("\x80\x80", 24));          // continuation as lead
    EXPECT_FALSE(IsCleanUtf8("\xC0\xAF", 24));          // overlong '/'
    EXPECT_FALSE(IsCleanUtf8("\xED\xA0\x80", 24));      // UTF-16 surrogate
    EXPECT_FALSE(IsCleanUtf8("\xF5\x80\x80\x80", 24));  // beyond U+10FFFF
}

TEST(VoicePacket, HeaderLayoutIsStable) {
    EXPECT_EQ(sizeof(VoiceHeader), 18u);
    EXPECT_EQ(kVoiceHeaderSize, 18u);
}

TEST(VoicePacket, RejectsForeignDatagrams) {
    std::array<uint8, kVoiceHeaderSize> buffer{};
    VoiceHeader header;
    header.type = VoiceMessageType::Data;
    header.sessionId = 12;
    WriteVoiceHeader(buffer.data(), header);

    VoiceHeader parsed;
    EXPECT_TRUE(ReadVoiceHeader(buffer.data(), static_cast<int32>(buffer.size()), parsed));
    EXPECT_EQ(parsed.sessionId, 12u);

    // Too short.
    EXPECT_FALSE(ReadVoiceHeader(buffer.data(), 4, parsed));

    // Wrong magic — a stray packet on a shared UDP port.
    buffer[0] = 0x00;
    EXPECT_FALSE(ReadVoiceHeader(buffer.data(), static_cast<int32>(buffer.size()), parsed));

    // Wrong version.
    buffer[0] = kVoiceMagic;
    buffer[1] = 99;
    EXPECT_FALSE(ReadVoiceHeader(buffer.data(), static_cast<int32>(buffer.size()), parsed));
}

TEST(VoicePacket, SenderStampRewritesOnlyTheSessionId) {
    std::array<uint8, kVoiceHeaderSize + 4> buffer{};
    VoiceHeader header;
    header.type = VoiceMessageType::Data;
    header.sessionId = 1;
    header.sequence = 77;
    header.timestamp = 960;
    WriteVoiceHeader(buffer.data(), header);
    buffer[kVoiceHeaderSize] = 0xAB;

    StampVoiceSender(buffer.data(), 4242);

    VoiceHeader parsed;
    ASSERT_TRUE(ReadVoiceHeader(buffer.data(), static_cast<int32>(buffer.size()), parsed));
    EXPECT_EQ(parsed.sessionId, 4242u);
    EXPECT_EQ(parsed.sequence, 77);
    EXPECT_EQ(parsed.timestamp, 960u);
    EXPECT_EQ(parsed.type, VoiceMessageType::Data);
    EXPECT_EQ(buffer[kVoiceHeaderSize], 0xAB);  // payload untouched
}
