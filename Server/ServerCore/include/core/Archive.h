#pragma once

#include <bit>
#include <concepts>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

#include "core/Types.h"

namespace nhn {

/// Wire framing. `size` counts the header itself, so the smallest legal packet
/// is exactly sizeof(PacketHeader) bytes.
#pragma pack(push, 1)
struct PacketHeader {
    uint16 size;
    uint16 id;
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 4, "packet header must stay 4 bytes on the wire");

// ---------------------------------------------------------------------------
// Byte order
//
// Everything is written little-endian, matching x86 and ARM in the
// configurations this project targets, so the integer paths are plain copies.
// The static_assert makes a big-endian port fail loudly instead of silently
// exchanging byte-swapped packets.
// ---------------------------------------------------------------------------
static_assert(std::endian::native == std::endian::little,
              "wire format assumes a little-endian host");

/// Writes primitives into a caller-supplied byte range. Never grows the buffer:
/// an overflow sets the failed flag and drops the write, which the packet
/// builder checks before sending.
class BufferWriter {
public:
    static constexpr bool kIsWriting = true;
    static constexpr bool kIsReading = false;

    BufferWriter() = default;
    BufferWriter(uint8* buffer, uint32 size, uint32 pos = 0)
        : _buffer(buffer), _size(size), _pos(pos) {}

    uint8* Buffer() const { return _buffer; }
    uint32 Capacity() const { return _size; }
    uint32 WriteSize() const { return _pos; }
    uint32 FreeSize() const { return _size - _pos; }
    bool Failed() const { return _failed; }

    bool WriteBytes(const void* source, uint32 length) {
        if (_failed) {
            return false;
        }
        if (length > _size - _pos) {
            _failed = true;
            return false;
        }
        std::memcpy(_buffer + _pos, source, length);
        _pos += length;
        return true;
    }

    template <class T>
        requires std::is_trivially_copyable_v<T>
    bool WritePod(const T& value) {
        return WriteBytes(&value, sizeof(T));
    }

    /// Carves out @p length bytes and returns a pointer to them. Used to
    /// back-patch the header size once the body length is known. Returns
    /// nullptr when the buffer cannot satisfy the request.
    uint8* Reserve(uint32 length) {
        if (_failed || length > _size - _pos) {
            _failed = true;
            return nullptr;
        }
        uint8* const reserved = _buffer + _pos;
        _pos += length;
        return reserved;
    }

private:
    uint8* _buffer = nullptr;
    uint32 _size = 0;
    uint32 _pos = 0;
    bool _failed = false;
};

/// Reads primitives back out of a received frame.
///
/// Every read is bounds-checked and a single overrun latches the failed flag,
/// so a truncated or hostile packet yields default-constructed fields instead
/// of reading past the buffer. Handlers reject the packet by testing Failed()
/// once, rather than checking each field.
class BufferReader {
public:
    static constexpr bool kIsWriting = false;
    static constexpr bool kIsReading = true;

    BufferReader() = default;
    BufferReader(const uint8* buffer, uint32 size, uint32 pos = 0)
        : _buffer(buffer), _size(size), _pos(pos) {}

    const uint8* Buffer() const { return _buffer; }
    uint32 Size() const { return _size; }
    uint32 ReadSize() const { return _pos; }
    uint32 FreeSize() const { return _size - _pos; }
    bool Failed() const { return _failed; }
    void Fail() { _failed = true; }

    /// True when the frame was consumed exactly, with no trailing bytes. A
    /// mismatch means the sender and receiver disagree on the layout.
    bool ConsumedExactly() const { return !_failed && _pos == _size; }

    bool ReadBytes(void* destination, uint32 length) {
        if (_failed) {
            return false;
        }
        // Subtraction rather than `_pos + length > _size`, which can wrap.
        if (length > _size - _pos) {
            _failed = true;
            return false;
        }
        std::memcpy(destination, _buffer + _pos, length);
        _pos += length;
        return true;
    }

    bool Skip(uint32 length) {
        if (_failed || length > _size - _pos) {
            _failed = true;
            return false;
        }
        _pos += length;
        return true;
    }

    template <class T>
        requires std::is_trivially_copyable_v<T>
    bool ReadPod(T& value) {
        return ReadBytes(&value, sizeof(T));
    }

private:
    const uint8* _buffer = nullptr;
    uint32 _size = 0;
    uint32 _pos = 0;
    bool _failed = false;
};

template <class Ar>
concept Archive = std::same_as<Ar, BufferWriter> || std::same_as<Ar, BufferReader>;

namespace detail {

template <class>
inline constexpr bool kAlwaysFalse = false;

template <class T>
struct IsStdVector : std::false_type {};

template <class T, class A>
struct IsStdVector<std::vector<T, A>> : std::true_type {};

template <class T>
concept HasSerializeMethod = requires(T& value, BufferWriter& writer) { value.Serialize(writer); };

}  // namespace detail

template <Archive Ar, class T>
void SerializeValue(Ar& archive, T& value);

/// The single operator every packet body is written in terms of:
///
///     template <class Ar> void Serialize(Ar& ar) { ar & id & name & members; }
///
/// One definition drives both directions, so a field can never be written in an
/// order the reader does not expect.
template <Archive Ar, class T>
Ar& operator&(Ar& archive, T& value) {
    SerializeValue(archive, value);
    return archive;
}

template <Archive Ar, class T>
void SerializeValue(Ar& archive, T& value) {
    if constexpr (std::is_same_v<T, bool>) {
        if constexpr (Ar::kIsWriting) {
            const uint8 raw = value ? uint8{1} : uint8{0};
            archive.WritePod(raw);
        } else {
            uint8 raw = 0;
            archive.ReadPod(raw);
            value = raw != 0;
        }
    } else if constexpr (std::is_enum_v<T>) {
        using Underlying = std::underlying_type_t<T>;
        if constexpr (Ar::kIsWriting) {
            const auto raw = static_cast<Underlying>(value);
            archive.WritePod(raw);
        } else {
            Underlying raw{};
            archive.ReadPod(raw);
            value = static_cast<T>(raw);
        }
    } else if constexpr (std::is_arithmetic_v<T>) {
        if constexpr (Ar::kIsWriting) {
            archive.WritePod(value);
        } else {
            archive.ReadPod(value);
        }
    } else if constexpr (std::is_same_v<T, std::string>) {
        if constexpr (Ar::kIsWriting) {
            // Longer strings are truncated rather than failing the whole
            // packet; every producer already clamps to a domain-specific limit.
            const auto length =
                static_cast<uint16>(value.size() > 0xFFFFu ? 0xFFFFu : value.size());
            archive.WritePod(length);
            archive.WriteBytes(value.data(), length);
        } else {
            uint16 length = 0;
            if (!archive.ReadPod(length)) {
                value.clear();
                return;
            }
            if (length > archive.FreeSize()) {
                // Claimed length exceeds what is left; refuse before allocating.
                archive.Fail();
                value.clear();
                return;
            }
            value.resize(length);
            archive.ReadBytes(value.data(), length);
        }
    } else if constexpr (detail::IsStdVector<T>::value) {
        using Element = typename T::value_type;
        if constexpr (Ar::kIsWriting) {
            const auto count =
                static_cast<uint16>(value.size() > 0xFFFFu ? 0xFFFFu : value.size());
            archive.WritePod(count);
            for (uint16 i = 0; i < count; ++i) {
                SerializeValue(archive, value[i]);
            }
        } else {
            uint16 count = 0;
            if (!archive.ReadPod(count)) {
                value.clear();
                return;
            }
            value.clear();
            // A hostile count cannot force a large allocation: reserve is
            // bounded by the bytes actually remaining, and the element reads
            // below fail as soon as the frame runs out.
            const uint32 maxPlausible = archive.FreeSize();
            value.reserve(count < maxPlausible ? count : maxPlausible);
            for (uint16 i = 0; i < count; ++i) {
                Element element{};
                SerializeValue(archive, element);
                if (archive.Failed()) {
                    value.clear();
                    return;
                }
                value.push_back(std::move(element));
            }
        }
    } else if constexpr (detail::HasSerializeMethod<T>) {
        value.Serialize(archive);
    } else {
        static_assert(detail::kAlwaysFalse<T>,
                      "type has no serialization rule; give it a Serialize(Ar&) method");
    }
}

}  // namespace nhn
