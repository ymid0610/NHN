#include "core/Random.h"

#include <bcrypt.h>

#include <random>
#include <vector>

#include "core/FatalHandler.h"

#pragma comment(lib, "bcrypt.lib")

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace nhn {
namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

thread_local std::mt19937_64 tEngine{[] {
    std::random_device device;
    return (static_cast<uint64>(device()) << 32) ^ device();
}()};

}  // namespace

void Random::Bytes(void* buffer, uint32 length) {
    const NTSTATUS status = ::BCryptGenRandom(nullptr, static_cast<PUCHAR>(buffer), length,
                                              BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    // Falling back to a weak generator here would silently make every ticket
    // in the process predictable, so this is fatal rather than degraded.
    NHN_CHECK(status == STATUS_SUCCESS, "BCryptGenRandom failed with 0x{:08X}",
              static_cast<uint32>(status));
}

std::string Random::HexToken(uint32 byteCount) {
    std::vector<uint8> raw(byteCount);
    Bytes(raw.data(), byteCount);

    std::string token;
    token.resize(static_cast<size_t>(byteCount) * 2);
    for (uint32 i = 0; i < byteCount; ++i) {
        token[static_cast<size_t>(i) * 2] = kHexDigits[raw[i] >> 4];
        token[static_cast<size_t>(i) * 2 + 1] = kHexDigits[raw[i] & 0x0F];
    }
    return token;
}

std::string Random::Ticket() {
    return HexToken(kTicketLength / 2);
}

uint64 Random::Uint64() {
    return tEngine();
}

int32 Random::Range(int32 minInclusive, int32 maxInclusive) {
    if (minInclusive >= maxInclusive) {
        return minInclusive;
    }
    std::uniform_int_distribution<int32> distribution(minInclusive, maxInclusive);
    return distribution(tEngine);
}

}  // namespace nhn
