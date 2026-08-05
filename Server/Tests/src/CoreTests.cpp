#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <unordered_set>

#include "common/RateLimiter.h"
#include "common/TicketTable.h"
#include "core/Buffer.h"
#include "core/Random.h"

using namespace nhn;
using namespace nhn::proto;

// ---------------------------------------------------------------------------
// RecvBuffer
// ---------------------------------------------------------------------------

TEST(RecvBuffer, TracksReadAndWriteCursors) {
    RecvBuffer buffer(1024);
    EXPECT_EQ(buffer.DataSize(), 0u);

    ASSERT_TRUE(buffer.OnWrite(100));
    EXPECT_EQ(buffer.DataSize(), 100u);

    ASSERT_TRUE(buffer.OnRead(40));
    EXPECT_EQ(buffer.DataSize(), 60u);

    EXPECT_FALSE(buffer.OnRead(61));   // more than is buffered
    EXPECT_FALSE(buffer.OnWrite(buffer.FreeSize() + 1));
}

TEST(RecvBuffer, CompactsSoWritesNeverRunOutOfTail) {
    RecvBuffer buffer(64);  // capacity is 64 * 10
    ASSERT_EQ(buffer.FreeSize(), 640u);

    // Each cycle leaves 20 bytes behind. Without compaction the write cursor
    // would walk off the end after 13 cycles.
    for (int i = 0; i < 20; ++i) {
        ASSERT_TRUE(buffer.OnWrite(50)) << "write failed on cycle " << i;
        ASSERT_TRUE(buffer.OnRead(30));
        buffer.Clean();
    }

    EXPECT_EQ(buffer.DataSize(), 20u * 20u);
    EXPECT_GT(buffer.FreeSize(), 0u);
}

TEST(RecvBuffer, RewindsWhenFullyConsumed) {
    RecvBuffer buffer(64);
    ASSERT_TRUE(buffer.OnWrite(100));
    ASSERT_TRUE(buffer.OnRead(100));
    buffer.Clean();

    EXPECT_EQ(buffer.DataSize(), 0u);
    EXPECT_EQ(buffer.FreeSize(), 64u * 10u);
}

TEST(RecvBuffer, PreservesContentAcrossCompaction) {
    RecvBuffer buffer(16);  // small, so compaction happens quickly
    const uint32 capacity = buffer.FreeSize();

    // Write a marker near the end, consume everything before it, then force a
    // compaction and check the marker moved intact.
    ASSERT_TRUE(buffer.OnWrite(capacity - 4));
    buffer.WritePos()[0] = 0xAA;
    buffer.WritePos()[1] = 0xBB;
    buffer.WritePos()[2] = 0xCC;
    buffer.WritePos()[3] = 0xDD;
    ASSERT_TRUE(buffer.OnWrite(4));
    ASSERT_TRUE(buffer.OnRead(capacity - 4));

    buffer.Clean();

    ASSERT_EQ(buffer.DataSize(), 4u);
    EXPECT_EQ(buffer.ReadPos()[0], 0xAA);
    EXPECT_EQ(buffer.ReadPos()[3], 0xDD);
}

// ---------------------------------------------------------------------------
// SendBuffer
// ---------------------------------------------------------------------------

TEST(SendBuffer, AllocatesAndClosesWithinAChunk) {
    SendBufferRef first = SendBufferManager::Open(128);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->AllocSize(), 128u);

    std::memset(first->Buffer(), 0x11, 64);
    first->Close(64);
    EXPECT_EQ(first->WriteSize(), 64u);

    SendBufferRef second = SendBufferManager::Open(128);
    ASSERT_NE(second, nullptr);
    second->Close(128);

    // Sequential allocations from the same thread must not overlap.
    EXPECT_NE(first->Buffer(), second->Buffer());
}

TEST(SendBuffer, RecyclesChunksThroughThePool) {
    const size_t before = SendBufferManager::PooledChunkCount();
    {
        // 40 x 4 KB spans several 64 KB chunks, so chunks are retired while
        // still referenced by the vector.
        std::vector<SendBufferRef> buffers;
        for (int i = 0; i < 40; ++i) {
            SendBufferRef buffer = SendBufferManager::Open(4096);
            ASSERT_NE(buffer, nullptr);
            buffer->Close(4096);
            buffers.push_back(std::move(buffer));
        }
    }

    // Once the last reference dies the retired chunks rejoin the free list
    // instead of being freed. The chunk this thread is currently writing into
    // is still held, hence "at least one".
    EXPECT_GT(SendBufferManager::PooledChunkCount(), before);
}

TEST(SendBuffer, KeepsChunkAliveWhileABufferReferencesIt) {
    SendBufferRef survivor;
    {
        survivor = SendBufferManager::Open(256);
        ASSERT_NE(survivor, nullptr);
        std::memset(survivor->Buffer(), 0x5A, 256);
        survivor->Close(256);
    }
    // Force the thread-local chunk to be replaced several times over.
    for (int i = 0; i < 32; ++i) {
        SendBufferRef churn = SendBufferManager::Open(4096);
        ASSERT_NE(churn, nullptr);
        churn->Close(4096);
    }
    // The original memory must still be valid and unmodified: this is what
    // makes it safe to hand one buffer to many sessions.
    for (int i = 0; i < 256; ++i) {
        ASSERT_EQ(survivor->Buffer()[i], 0x5A) << "byte " << i << " was reused";
    }
}

// ---------------------------------------------------------------------------
// Tickets
// ---------------------------------------------------------------------------

TEST(Ticket, GeneratesUnguessableDistinctValues) {
    std::unordered_set<std::string> seen;
    for (int i = 0; i < 2000; ++i) {
        std::string ticket = Random::Ticket();
        EXPECT_EQ(ticket.size(), kTicketLength);
        EXPECT_TRUE(seen.insert(ticket).second) << "duplicate ticket: " << ticket;
    }
}

TEST(TicketTable, ConsumesExactlyOnce) {
    TicketTable table;
    table.Add("abc", 7, "player", NowUnixMs() + 10'000);

    TicketTable::Entry entry;
    ResultCode reason = ResultCode::UnknownError;

    ASSERT_TRUE(table.Consume("abc", NowUnixMs(), entry, reason));
    EXPECT_EQ(entry.sessionId, 7u);
    EXPECT_EQ(entry.nickname, "player");
    EXPECT_EQ(reason, ResultCode::Ok);

    // Replay must fail: a ticket is single use.
    EXPECT_FALSE(table.Consume("abc", NowUnixMs(), entry, reason));
    EXPECT_EQ(reason, ResultCode::TicketInvalid);
}

TEST(TicketTable, RejectsUnknownAndExpired) {
    TicketTable table;
    TicketTable::Entry entry;
    ResultCode reason = ResultCode::UnknownError;

    EXPECT_FALSE(table.Consume("never-issued", NowUnixMs(), entry, reason));
    EXPECT_EQ(reason, ResultCode::TicketInvalid);

    table.Add("stale", 1, "player", NowUnixMs() - 1);
    EXPECT_FALSE(table.Consume("stale", NowUnixMs(), entry, reason));
    EXPECT_EQ(reason, ResultCode::TicketExpired);

    // An expired ticket is dropped, so it cannot be probed repeatedly.
    EXPECT_EQ(table.Size(), 0u);
}

TEST(TicketTable, PurgesExpiredButKeepsLive) {
    TicketTable table;
    const int64 now = NowUnixMs();
    table.Add("live", 1, "a", now + 60'000);
    table.Add("dead1", 2, "b", now - 1);
    table.Add("dead2", 3, "c", now - 5000);

    EXPECT_EQ(table.PurgeExpired(now), 2u);
    EXPECT_EQ(table.Size(), 1u);
}

TEST(TicketTable, RevokeRemovesImmediately) {
    TicketTable table;
    table.Add("doomed", 5, "player", NowUnixMs() + 60'000);
    table.Revoke("doomed");

    TicketTable::Entry entry;
    ResultCode reason = ResultCode::UnknownError;
    EXPECT_FALSE(table.Consume("doomed", NowUnixMs(), entry, reason));
}

// ---------------------------------------------------------------------------
// RateLimiter
// ---------------------------------------------------------------------------

TEST(RateLimiter, AllowsABurstThenThrottles) {
    RateLimiter limiter(4, 1.5);
    const TickCount now = 1000;

    for (int i = 0; i < 4; ++i) {
        EXPECT_TRUE(limiter.TryConsume(now)) << "burst message " << i << " was rejected";
    }
    EXPECT_FALSE(limiter.TryConsume(now)) << "burst was not bounded";
}

TEST(RateLimiter, RefillsOverTime) {
    RateLimiter limiter(2, 2.0);  // two per second
    TickCount now = 1000;

    EXPECT_TRUE(limiter.TryConsume(now));
    EXPECT_TRUE(limiter.TryConsume(now));
    EXPECT_FALSE(limiter.TryConsume(now));

    now += 500;  // one token back
    EXPECT_TRUE(limiter.TryConsume(now));
    EXPECT_FALSE(limiter.TryConsume(now));
}

TEST(RateLimiter, DoesNotAccumulateBeyondBurst) {
    RateLimiter limiter(3, 10.0);
    TickCount now = 1000;

    now += 60'000;  // a long idle period
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(limiter.TryConsume(now));
    }
    // Idling must not bank an unbounded allowance.
    EXPECT_FALSE(limiter.TryConsume(now));
}
