#pragma once

#include <string>

#include "core/Types.h"

namespace nhn {

/// Cryptographically strong randomness, used for ticket material.
///
/// The tickets are plaintext by design — nothing verifies a signature, the
/// issuing server simply pushes the string to the server that will accept it.
/// That only holds up because the value is unguessable: a sequential or
/// time-derived id would let anyone join any room's chat or voice channel by
/// enumeration. Hence a CSPRNG rather than std::mt19937.
class Random {
public:
    /// Fills @p length bytes from the system CSPRNG. Fatal on failure — there
    /// is no safe fallback for ticket material.
    static void Bytes(void* buffer, uint32 length);

    /// Lower-case hex string of @p byteCount random bytes, so kTicketLength
    /// characters means byteCount = kTicketLength / 2.
    static std::string HexToken(uint32 byteCount);

    /// A ticket string of exactly kTicketLength characters (128 bits).
    static std::string Ticket();

    static uint64 Uint64();

    /// Uniform in [minInclusive, maxInclusive]. Non-cryptographic; used for
    /// picking ids and jitter.
    static int32 Range(int32 minInclusive, int32 maxInclusive);
};

}  // namespace nhn
