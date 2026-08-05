#include <gtest/gtest.h>

#include <array>

#include "core/Archive.h"
#include "protocol/Packets.h"

using namespace nhn;
using namespace nhn::proto;

namespace {

/// Writes a packet body (no framing header) and reads it back.
template <class T>
bool RoundTrip(T& source, T& destination, uint32 bufferSize = 4096) {
    std::vector<uint8> storage(bufferSize);

    BufferWriter writer(storage.data(), static_cast<uint32>(storage.size()));
    source.Serialize(writer);
    if (writer.Failed()) {
        return false;
    }

    BufferReader reader(storage.data(), writer.WriteSize());
    destination.Serialize(reader);
    return !reader.Failed();
}

}  // namespace

TEST(Archive, RoundTripsScalarsAndStrings) {
    C_RoomCreate source;
    source.name = "arena";
    source.roomType = RoomType::Quad;
    source.password = "hunter2";

    C_RoomCreate destination;
    ASSERT_TRUE(RoundTrip(source, destination));

    EXPECT_EQ(destination.name, "arena");
    EXPECT_EQ(destination.roomType, RoomType::Quad);
    EXPECT_EQ(destination.password, "hunter2");
}

TEST(Archive, RoundTripsNestedStructsAndVectors) {
    S_RoomJoinAck source;
    source.result = ResultCode::Ok;
    source.room.roomId = 42;
    source.room.name = "lobby";
    source.room.roomType = RoomType::Duo;
    source.room.state = RoomState::Waiting;
    source.room.capacity = 2;
    source.room.hasPassword = true;
    source.room.hostSessionId = 7;

    for (uint8 i = 0; i < 2; ++i) {
        RoomMemberInfo member;
        member.sessionId = 7 + i;
        member.nickname = std::string("player") + std::to_string(i);
        member.slot = i;
        member.isHost = (i == 0);
        member.isReady = (i == 1);
        source.room.members.push_back(member);
    }

    S_RoomJoinAck destination;
    ASSERT_TRUE(RoundTrip(source, destination));

    EXPECT_EQ(destination.room.roomId, 42u);
    EXPECT_EQ(destination.room.hasPassword, true);
    ASSERT_EQ(destination.room.members.size(), 2u);
    EXPECT_EQ(destination.room.members[0].nickname, "player0");
    EXPECT_TRUE(destination.room.members[0].isHost);
    EXPECT_TRUE(destination.room.members[1].isReady);
    EXPECT_EQ(destination.room.members[1].slot, 1);
}

TEST(Archive, RoundTripsEmptyPacket) {
    C_RoomLeave source;
    C_RoomLeave destination;
    EXPECT_TRUE(RoundTrip(source, destination));
}

TEST(Archive, WriterFailsInsteadOfOverflowing) {
    C_RoomCreate source;
    source.name = std::string(500, 'x');

    std::array<uint8, 16> tiny{};
    BufferWriter writer(tiny.data(), static_cast<uint32>(tiny.size()));
    source.Serialize(writer);

    EXPECT_TRUE(writer.Failed());
    EXPECT_LE(writer.WriteSize(), tiny.size());
}

// The reader is the only place hostile bytes are interpreted, so these are the
// tests that matter most for a hand-rolled format.

TEST(Archive, ReaderRejectsTruncatedFrame) {
    S_RoomJoinAck source;
    source.room.name = "lobby";
    source.room.members.resize(3);

    std::vector<uint8> storage(4096);
    BufferWriter writer(storage.data(), static_cast<uint32>(storage.size()));
    source.Serialize(writer);
    ASSERT_FALSE(writer.Failed());

    // Cut the frame in half; every prefix length must be rejected, not crash.
    for (uint32 length = 0; length < writer.WriteSize(); ++length) {
        BufferReader reader(storage.data(), length);
        S_RoomJoinAck destination;
        destination.Serialize(reader);
        EXPECT_TRUE(reader.Failed()) << "truncation to " << length << " bytes was accepted";
    }
}

TEST(Archive, ReaderRejectsOversizedStringLength) {
    // A string claiming 60000 bytes inside a 6-byte frame.
    std::array<uint8, 6> hostile{};
    const uint16 claimed = 60000;
    std::memcpy(hostile.data(), &claimed, sizeof(claimed));

    BufferReader reader(hostile.data(), static_cast<uint32>(hostile.size()));
    std::string value;
    SerializeValue(reader, value);

    EXPECT_TRUE(reader.Failed());
    EXPECT_TRUE(value.empty());
}

TEST(Archive, ReaderRejectsOversizedVectorCount) {
    // A vector claiming 65535 elements with no element data behind it. The read
    // must fail without attempting a huge allocation.
    std::array<uint8, 2> hostile{};
    const uint16 claimed = 65535;
    std::memcpy(hostile.data(), &claimed, sizeof(claimed));

    BufferReader reader(hostile.data(), static_cast<uint32>(hostile.size()));
    std::vector<RoomMemberInfo> members;
    SerializeValue(reader, members);

    EXPECT_TRUE(reader.Failed());
    EXPECT_TRUE(members.empty());
}

TEST(Archive, ReaderSurvivesArbitraryGarbage) {
    // Deterministic pseudo-random bytes parsed as every packet shape used on
    // the client link: none of them may read out of bounds.
    std::vector<uint8> noise(512);
    uint32 state = 0x12345678;
    for (uint8& byte : noise) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<uint8>(state >> 24);
    }

    for (uint32 length = 1; length <= noise.size(); length += 7) {
        {
            BufferReader reader(noise.data(), length);
            S_RoomList packet;
            packet.Serialize(reader);
        }
        {
            BufferReader reader(noise.data(), length);
            S_HelloAck packet;
            packet.Serialize(reader);
        }
        {
            BufferReader reader(noise.data(), length);
            P_InstanceCreate packet;
            packet.Serialize(reader);
        }
    }
    SUCCEED();  // Reaching here without a crash or hang is the assertion.
}

TEST(Archive, ConsumedExactlyDetectsTrailingBytes) {
    C_Ping source;
    source.clientTimeMs = 1234;

    std::vector<uint8> storage(64);
    BufferWriter writer(storage.data(), static_cast<uint32>(storage.size()));
    source.Serialize(writer);

    BufferReader exact(storage.data(), writer.WriteSize());
    C_Ping destination;
    destination.Serialize(exact);
    EXPECT_TRUE(exact.ConsumedExactly());
    EXPECT_EQ(destination.clientTimeMs, 1234);

    // Trailing bytes are tolerated by design, for forward compatibility.
    BufferReader padded(storage.data(), writer.WriteSize() + 4);
    C_Ping padded_destination;
    padded_destination.Serialize(padded);
    EXPECT_FALSE(padded.Failed());
    EXPECT_FALSE(padded.ConsumedExactly());
    EXPECT_EQ(padded_destination.clientTimeMs, 1234);
}
