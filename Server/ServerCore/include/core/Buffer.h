#pragma once

#include <array>
#include <vector>

#include "core/Types.h"

namespace nhn {

/// Accumulates bytes arriving from a socket until whole packets can be sliced
/// out of them.
///
/// Backed by a span several times the nominal size so that a partially consumed
/// stream keeps appending at the tail; only when the tail fills does Clean()
/// slide the unread remainder back to the front. Single-threaded by
/// construction — a session has exactly one receive in flight at a time.
class RecvBuffer {
public:
    explicit RecvBuffer(uint32 bufferSize);

    /// Slides unread bytes to the front when the tail can no longer hold a
    /// full read.
    void Clean();

    /// Marks @p numOfBytes as consumed by the packet framer.
    bool OnRead(uint32 numOfBytes);

    /// Marks @p numOfBytes as filled in by a completed WSARecv.
    bool OnWrite(uint32 numOfBytes);

    uint8* ReadPos() { return _buffer.data() + _readPos; }
    uint8* WritePos() { return _buffer.data() + _writePos; }
    uint32 DataSize() const { return _writePos - _readPos; }
    uint32 FreeSize() const { return _capacity - _writePos; }

private:
    static constexpr uint32 kBufferCount = 10;

    uint32 _bufferSize = 0;
    uint32 _capacity = 0;
    uint32 _readPos = 0;
    uint32 _writePos = 0;
    std::vector<uint8> _buffer;
};

class SendBufferChunk;
using SendBufferChunkRef = Ref<SendBufferChunk>;

/// One outgoing packet, carved out of a pooled chunk.
///
/// Holds a reference to its chunk so the memory outlives every pending WSASend
/// that points into it, which is what makes it safe to hand the same buffer to
/// several sessions when broadcasting.
class SendBuffer {
public:
    SendBuffer(SendBufferChunkRef owner, uint8* buffer, uint32 allocSize);
    ~SendBuffer() = default;

    uint8* Buffer() { return _buffer; }
    const uint8* Buffer() const { return _buffer; }
    uint32 AllocSize() const { return _allocSize; }
    uint32 WriteSize() const { return _writeSize; }

    /// Finalises the packet at its real length, releasing the unused tail of
    /// the reservation back to the chunk.
    void Close(uint32 writeSize);

private:
    uint8* _buffer = nullptr;
    uint32 _allocSize = 0;
    uint32 _writeSize = 0;
    SendBufferChunkRef _owner;
};

using SendBufferRef = Ref<SendBuffer>;

/// A slab that packets are bump-allocated from. Recycled through a free list
/// once every SendBuffer carved from it has been released.
class SendBufferChunk : public std::enable_shared_from_this<SendBufferChunk> {
public:
    static constexpr uint32 kChunkSize = 64 * 1024;

    void Reset();
    SendBufferRef Open(uint32 allocSize);
    void Close(uint32 writeSize);

    bool IsOpen() const { return _open; }
    uint8* Buffer() { return _buffer.data() + _usedSize; }
    uint32 FreeSize() const { return kChunkSize - _usedSize; }

private:
    std::array<uint8, kChunkSize> _buffer{};
    bool _open = false;
    uint32 _usedSize = 0;
};

/// Allocates send buffers from a thread-local chunk, so the common path takes
/// no lock at all; the global free list is touched only when a thread exhausts
/// its current chunk.
class SendBufferManager {
public:
    static SendBufferRef Open(uint32 size);

    /// Releases pooled chunks. Call during shutdown, after the IOCP workers
    /// have stopped.
    static void Shutdown();

    static size_t PooledChunkCount();
};

}  // namespace nhn
