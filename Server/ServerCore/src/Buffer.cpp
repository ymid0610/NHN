#include "core/Buffer.h"

#include <cstring>
#include <mutex>

#include "core/FatalHandler.h"

namespace nhn {

// ---------------------------------------------------------------------------
// RecvBuffer
// ---------------------------------------------------------------------------

RecvBuffer::RecvBuffer(uint32 bufferSize)
    : _bufferSize(bufferSize), _capacity(bufferSize * kBufferCount) {
    _buffer.resize(_capacity);
}

void RecvBuffer::Clean() {
    const uint32 dataSize = DataSize();
    if (dataSize == 0) {
        // Fully consumed: rewind instead of copying.
        _readPos = 0;
        _writePos = 0;
        return;
    }

    if (FreeSize() < _bufferSize) {
        std::memmove(_buffer.data(), _buffer.data() + _readPos, dataSize);
        _readPos = 0;
        _writePos = dataSize;
    }
}

bool RecvBuffer::OnRead(uint32 numOfBytes) {
    if (numOfBytes > DataSize()) {
        return false;
    }
    _readPos += numOfBytes;
    return true;
}

bool RecvBuffer::OnWrite(uint32 numOfBytes) {
    if (numOfBytes > FreeSize()) {
        return false;
    }
    _writePos += numOfBytes;
    return true;
}

// ---------------------------------------------------------------------------
// SendBuffer
// ---------------------------------------------------------------------------

SendBuffer::SendBuffer(SendBufferChunkRef owner, uint8* buffer, uint32 allocSize)
    : _buffer(buffer), _allocSize(allocSize), _owner(std::move(owner)) {}

void SendBuffer::Close(uint32 writeSize) {
    NHN_CHECK(writeSize <= _allocSize, "send buffer overrun: wrote {} into a {} byte reservation",
              writeSize, _allocSize);
    _writeSize = writeSize;
    _owner->Close(writeSize);
}

// ---------------------------------------------------------------------------
// SendBufferChunk
// ---------------------------------------------------------------------------

void SendBufferChunk::Reset() {
    _open = false;
    _usedSize = 0;
}

SendBufferRef SendBufferChunk::Open(uint32 allocSize) {
    NHN_CHECK(allocSize <= kChunkSize, "requested {} bytes from a {} byte chunk", allocSize,
              kChunkSize);
    NHN_CHECK(!_open, "chunk already has an open buffer; Close() it first");
    if (allocSize > FreeSize()) {
        return nullptr;
    }

    _open = true;
    return std::make_shared<SendBuffer>(shared_from_this(), Buffer(), allocSize);
}

void SendBufferChunk::Close(uint32 writeSize) {
    NHN_CHECK(_open, "closing a chunk that has no open buffer");
    _open = false;
    _usedSize += writeSize;
}

// ---------------------------------------------------------------------------
// SendBufferManager
// ---------------------------------------------------------------------------

namespace {

std::mutex gChunkPoolMutex;
std::vector<SendBufferChunk*> gChunkPool;
std::atomic<bool> gChunkPoolClosed{false};

/// shared_ptr deleter: parks the chunk on the free list rather than freeing it.
void ReturnChunkToPool(SendBufferChunk* chunk) {
    if (chunk == nullptr) {
        return;
    }
    if (gChunkPoolClosed.load(std::memory_order_acquire)) {
        delete chunk;
        return;
    }
    std::lock_guard<std::mutex> guard(gChunkPoolMutex);
    gChunkPool.push_back(chunk);
}

SendBufferChunkRef AcquireChunk() {
    {
        std::lock_guard<std::mutex> guard(gChunkPoolMutex);
        if (!gChunkPool.empty()) {
            SendBufferChunk* const chunk = gChunkPool.back();
            gChunkPool.pop_back();
            return SendBufferChunkRef(chunk, ReturnChunkToPool);
        }
    }
    return SendBufferChunkRef(new SendBufferChunk(), ReturnChunkToPool);
}

/// The chunk this thread is currently carving packets out of. Thread-local so
/// the allocation fast path never contends.
thread_local SendBufferChunkRef tCurrentChunk;

}  // namespace

SendBufferRef SendBufferManager::Open(uint32 size) {
    if (tCurrentChunk == nullptr) {
        tCurrentChunk = AcquireChunk();
        tCurrentChunk->Reset();
    }

    NHN_CHECK(!tCurrentChunk->IsOpen(), "previous send buffer was never closed");

    // Retire the chunk when the request no longer fits. Any SendBuffer still
    // referencing it keeps it alive; it rejoins the pool once they all die.
    if (tCurrentChunk->FreeSize() < size) {
        tCurrentChunk = AcquireChunk();
        tCurrentChunk->Reset();
    }

    return tCurrentChunk->Open(size);
}

void SendBufferManager::Shutdown() {
    gChunkPoolClosed.store(true, std::memory_order_release);
    tCurrentChunk.reset();

    std::lock_guard<std::mutex> guard(gChunkPoolMutex);
    for (SendBufferChunk* chunk : gChunkPool) {
        delete chunk;
    }
    gChunkPool.clear();
}

size_t SendBufferManager::PooledChunkCount() {
    std::lock_guard<std::mutex> guard(gChunkPoolMutex);
    return gChunkPool.size();
}

}  // namespace nhn
