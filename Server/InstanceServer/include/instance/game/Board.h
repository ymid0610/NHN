#pragma once

#include <vector>

#include "core/Types.h"
#include "protocol/GameTypes.h"

namespace nhn::instance {

/// The marks accumulated during a round.
///
/// The board outlives the wave: a new sheet flies past each pass, but it is a
/// view onto this same grid, so marks build up until someone completes a line
/// or the round runs out of waves.
class Board {
public:
    static constexpr uint8 kNoOwner = 0xFF;
    static constexpr uint16 kNoCell = 0xFFFF;

    void Reset(uint8 width, uint8 height, uint8 winLength);

    uint8 Width() const { return _width; }
    uint8 Height() const { return _height; }
    uint8 WinLength() const { return _winLength; }
    uint16 CellCount() const { return static_cast<uint16>(_width) * _height; }

    uint16 CellIndex(uint8 x, uint8 y) const { return static_cast<uint16>(y) * _width + x; }
    uint8 OwnerAt(uint16 cellIndex) const;
    bool IsEmpty(uint16 cellIndex) const { return OwnerAt(cellIndex) == kNoOwner; }
    bool IsFull() const { return _claimed >= CellCount(); }

    /// First valid hit takes the cell — that is the whole contest in
    /// simultaneous play.
    /// @return false when the cell was already taken or out of range.
    bool Claim(uint16 cellIndex, uint8 slot);

    /// Checks only the lines running through @p cellIndex, since a line can
    /// only have been completed by the mark that was just placed.
    bool CompletesLine(uint16 cellIndex, uint8 slot) const;

    const std::vector<uint8>& Cells() const { return _cells; }

private:
    /// Counts identical owners outwards from a cell along one axis.
    uint8 CountRun(int32 x, int32 y, int32 dx, int32 dy, uint8 slot) const;

    uint8 _width = 0;
    uint8 _height = 0;
    uint8 _winLength = 0;
    uint16 _claimed = 0;
    std::vector<uint8> _cells;  // kNoOwner or a player slot
};

}  // namespace nhn::instance
